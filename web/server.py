import asyncio
import json
import os
from pathlib import Path
from aiohttp import web, WSMsgType

BASE_DIR = Path(__file__).parent
UPLOAD_DIR = BASE_DIR / "uploads"
STATIC_DIR = BASE_DIR / "static"
UPLOAD_DIR.mkdir(exist_ok=True)

CHUNK_SIZE = 64 * 1024
connected_clients = set()


def human_size(n: int) -> str:
    for unit in ["B", "KB", "MB", "GB", "TB"]:
        if n < 1024:
            return f"{n:.1f} {unit}"
        n /= 1024
    return f"{n:.1f} PB"


def file_info(p: Path) -> dict:
    st = p.stat()
    return {
        "name": p.name,
        "size": st.st_size,
        "size_human": human_size(st.st_size),
        "mtime": int(st.st_mtime),
    }


async def broadcast(message: dict):
    if not connected_clients:
        return
    data = json.dumps(message)
    dead = set()
    for ws in connected_clients:
        try:
            await ws.send_str(data)
        except Exception:
            dead.add(ws)
    connected_clients.difference_update(dead)


async def index(request):
    return web.FileResponse(STATIC_DIR / "index.html")


async def list_files(request):
    files = sorted(UPLOAD_DIR.iterdir(), key=lambda p: p.stat().st_mtime, reverse=True)
    files = [f for f in files if f.is_file()]
    return web.json_response({"files": [file_info(f) for f in files]})


async def upload(request):
    reader = await request.multipart()
    saved = []
    while True:
        part = await reader.next()
        if part is None:
            break
        if part.name != "file":
            continue
        filename = os.path.basename(part.filename or "unnamed")
        if not filename:
            continue
        dest = UPLOAD_DIR / filename
        base, ext = os.path.splitext(filename)
        i = 1
        while dest.exists():
            dest = UPLOAD_DIR / f"{base} ({i}){ext}"
            i += 1

        size = 0
        with open(dest, "wb") as f:
            while True:
                chunk = await part.read_chunk(CHUNK_SIZE)
                if not chunk:
                    break
                f.write(chunk)
                size += len(chunk)
                await broadcast({
                    "type": "progress",
                    "name": dest.name,
                    "received": size,
                    "total": int(part.headers.get("Content-Length", 0) or 0),
                })

        saved.append(file_info(dest))
        await broadcast({"type": "uploaded", "file": saved[-1]})

    return web.json_response({"ok": True, "files": saved})


async def download(request):
    name = request.match_info["name"]
    path = (UPLOAD_DIR / name).resolve()
    if not str(path).startswith(str(UPLOAD_DIR.resolve())) or not path.is_file():
        raise web.HTTPNotFound()
    return web.FileResponse(path, headers={
        "Content-Disposition": f'attachment; filename="{path.name}"'
    })


async def delete(request):
    name = request.match_info["name"]
    path = (UPLOAD_DIR / name).resolve()
    if not str(path).startswith(str(UPLOAD_DIR.resolve())) or not path.is_file():
        raise web.HTTPNotFound()
    path.unlink()
    await broadcast({"type": "deleted", "name": name})
    return web.json_response({"ok": True})


async def websocket_handler(request):
    ws = web.WebSocketResponse()
    await ws.prepare(request)
    connected_clients.add(ws)
    try:
        async for msg in ws:
            if msg.type == WSMsgType.TEXT:
                if msg.data == "ping":
                    await ws.send_str("pong")
            elif msg.type == WSMsgType.ERROR:
                break
    finally:
        connected_clients.discard(ws)
    return ws


def create_app():
    app = web.Application(client_max_size=10 * 1024 ** 3)
    app.router.add_get("/", index)
    app.router.add_get("/api/files", list_files)
    app.router.add_post("/api/upload", upload)
    app.router.add_get("/api/download/{name}", download)
    app.router.add_delete("/api/delete/{name}", delete)
    app.router.add_get("/ws", websocket_handler)
    app.router.add_static("/static/", STATIC_DIR)
    return app


if __name__ == "__main__":
    app = create_app()
    print("=" * 50)
    print("  TCP File Transfer Web UI")
    print("  Открой в браузере: http://localhost:8000")
    print("=" * 50)
    web.run_app(app, host="0.0.0.0", port=8000)
