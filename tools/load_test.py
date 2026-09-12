#!/usr/bin/env python3
"""Simple repeatable load bench for the adaptive TCP server.
Requires a built client/server and a writable working directory."""
import argparse, os, subprocess, time, concurrent.futures, statistics

def run_one(client, port, source, index, chunk=None):
    out=f"load_{index}.bin"
    cmd=[client,'-p',str(port),'-src',source,'-dst',f'127.0.0.1:{out}']
    if chunk: cmd += ['-chunk',str(chunk)]
    t=time.perf_counter(); p=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True); sec=time.perf_counter()-t
    try: os.remove(out)
    except OSError: pass
    return p.returncode,sec,p.stdout.splitlines()[-1] if p.stdout else ''

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--client',default='./client');ap.add_argument('--port',type=int,default=4040);ap.add_argument('--file',required=True);ap.add_argument('--clients',type=int,default=4);ap.add_argument('--repeats',type=int,default=2);ap.add_argument('--chunk',type=int);a=ap.parse_args()
    jobs=[i for i in range(a.clients*a.repeats)]; results=[]
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.clients) as ex:
        fs=[ex.submit(run_one,a.client,a.port,a.file,i,a.chunk) for i in jobs]
        for f in fs: results.append(f.result())
    times=[x[1] for x in results if x[0]==0]; size=os.path.getsize(a.file); speeds=[size/t/1024/1024 for t in times if t]
    print(f"clients={a.clients} repeats={a.repeats} size={size/1048576:.1f} MiB")
    print(f"success={len(times)}/{len(results)} avg_time={statistics.mean(times):.3f}s avg_speed={statistics.mean(speeds):.2f} MiB/s")
if __name__=='__main__': main()
