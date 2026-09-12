const dropzone = document.getElementById("dropzone");
const fileInput = document.getElementById("fileInput");
const fileList = document.getElementById("fileList");
const progressList = document.getElementById("progressList");
const filesCount = document.getElementById("filesCount");
const statusEl = document.getElementById("status");

let files = [];

// ---------- helpers ----------
function formatDate(ts) {
  return new Date(ts * 1000).toLocaleString("ru-RU");
}
function iconFor(name) {
  const ext = name.split(".").pop().toLowerCase();
  if (["png","jpg","jpeg","gif","webp","svg"].includes(ext)) return "🖼️";
  if (["mp4","mkv","mov","avi"].includes(ext)) return "🎬";
  if (["mp3","wav","flac"].includes(ext)) return "🎵";
  if (["zip","rar","7z","tar","gz"].includes(ext)) return "📦";
  if (["pdf"].includes(ext)) return "📕";
  if (["doc","docx"].includes(ext)) return "📘";
  if (["txt","md","log"].includes(ext)) return "📄";
  if (["cpp","hpp","c","h","py","js","ts","html","css"].includes(ext)) return "💻";
  return "📎";
}

// ---------- render ----------
function renderFiles() {
  filesCount.textContent = files.length;
  if (!files.length) {
    fileList.innerHTML = '<li class="empty">Пока пусто — загрузи первый файл</li>';
    return;
  }
  fileList.innerHTML = files.map(f => `
    <li class="file-item">
      <div class="file-icon">${iconFor(f.name)}</div>
      <div class="file-meta">
        <div class="file-name">${escapeHtml(f.name)}</div>
        <div class="file-sub">${f.size_human} · ${formatDate(f.mtime)}</div>
      </div>
      <div class="file-actions">
        <a class="btn" href="/api/download/${encodeURIComponent(f.name)}" download>Скачать</a>
        <button class="danger" data-name="${escapeHtml(f.name)}">Удалить</button>
      </div>
    </li>
  `).join("");

  fileList.querySelectorAll("button.danger").forEach(btn => {
    btn.addEventListener("click", () => deleteFile(btn.dataset.name));
  });
}
function escapeHtml(s) {
  return s.replace(/[&<>"']/g, c => ({ "&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;" }[c]));
}

// ---------- API ----------
async function loadFiles() {
  const r = await fetch("/api/files");
  const d = await r.json();
  files = d.files;
  renderFiles();
}
async function deleteFile(name) {
  if (!confirm(`Удалить "${name}"?`)) return;
  await fetch(`/api/delete/${encodeURIComponent(name)}`, { method: "DELETE" });
}

// ---------- upload ----------
function uploadFile(file) {
  const id = "p" + Math.random().toString(36).slice(2);
  const el = document.createElement("div");
  el.className = "progress-item";
  el.id = id;
  el.innerHTML = `
    <div class="row">
      <span class="name">${escapeHtml(file.name)}</span>
      <span class="pct">0%</span>
    </div>
    <div class="bar"><div class="bar-fill"></div></div>
  `;
  progressList.appendChild(el);

  const xhr = new XMLHttpRequest();
  xhr.open("POST", "/api/upload");
  xhr.upload.onprogress = (e) => {
    if (e.lengthComputable) {
      const pct = Math.round(e.loaded / e.total * 100);
      el.querySelector(".pct").textContent = pct + "%";
      el.querySelector(".bar-fill").style.width = pct + "%";
    }
  };
  xhr.onload = () => {
    el.classList.add("done");
    el.querySelector(".pct").textContent = "✓";
    el.querySelector(".bar-fill").style.width = "100%";
    setTimeout(() => el.remove(), 1500);
    loadFiles();
  };
  xhr.onerror = () => {
    el.querySelector(".pct").textContent = "❌ ошибка";
  };
  const fd = new FormData();
  fd.append("file", file, file.name);
  xhr.send(fd);
}

// ---------- dropzone ----------
dropzone.addEventListener("click", () => fileInput.click());
fileInput.addEventListener("change", () => {
  [...fileInput.files].forEach(uploadFile);
  fileInput.value = "";
});
["dragenter","dragover"].forEach(ev =>
  dropzone.addEventListener(ev, e => { e.preventDefault(); dropzone.classList.add("dragover"); })
);
["dragleave","drop"].forEach(ev =>
  dropzone.addEventListener(ev, e => { e.preventDefault(); dropzone.classList.remove("dragover"); })
);
dropzone.addEventListener("drop", e => {
  [...e.dataTransfer.files].forEach(uploadFile);
});

// ---------- websocket ----------
function connectWS() {
  const proto = location.protocol === "https:" ? "wss" : "ws";
  const ws = new WebSocket(`${proto}://${location.host}/ws`);
  ws.onopen = () => {
    statusEl.textContent = "🟢 онлайн";
    statusEl.className = "status online";
  };
  ws.onclose = () => {
    statusEl.textContent = "🔴 оффлайн";
    statusEl.className = "status offline";
    setTimeout(connectWS, 2000);
  };
  ws.onmessage = (e) => {
    try {
      const msg = JSON.parse(e.data);
      if (msg.type === "uploaded" || msg.type === "deleted") loadFiles();
    } catch (_) {}
  };
}

loadFiles();
connectWS();