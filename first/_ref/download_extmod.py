# download_extmod.py - 补下载 MicroPython 缺失的 extmod/ 目录
# 原 download.py 的 PREFIXES 漏掉了 extmod/, 导致 py/mphal.h 引用的
# extmod/virtpin.h 等头文件缺失, 编译失败。
# 用法: python _ref/download_extmod.py
import json, os, sys, urllib.request, threading, queue

ROOT = "E:/project/clion/project_system/first"
SRC = os.path.join(ROOT, "_ref", "tree.json")
OUT = os.path.join(ROOT, "micropython")
TAG = "v1.22.0"
BASE = "https://raw.githubusercontent.com/micropython/micropython/%s/" % TAG

with open(SRC, "r", encoding="utf-8") as f:
    data = json.load(f)

paths = []
for item in data["tree"]:
    if item["type"] != "blob":
        continue
    p = item["path"]
    if p.startswith("extmod/"):
        paths.append(p)

print("extmod files to download:", len(paths))

q = queue.Queue()
for p in paths:
    q.put(p)

lock = threading.Lock()
done = [0]
errors = []

def worker():
    while True:
        try:
            p = q.get_nowait()
        except queue.Empty:
            return
        dst = os.path.join(OUT, p)
        try:
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            url = BASE + p
            req = urllib.request.Request(url, headers={"User-Agent": "curl/8"})
            with urllib.request.urlopen(req, timeout=60) as r:
                body = r.read()
            with open(dst, "wb") as w:
                w.write(body)
            with lock:
                done[0] += 1
        except Exception as e:
            with lock:
                errors.append((p, str(e)))

ths = []
for _ in range(12):
    t = threading.Thread(target=worker)
    t.daemon = True
    t.start()
    ths.append(t)

import time
while any(t.is_alive() for t in ths):
    time.sleep(2)
    with lock:
        d = done[0]
    sys.stdout.write("\r  %d/%d" % (d, len(paths)))
    sys.stdout.flush()
for t in ths:
    t.join()

print("\nDONE done=%d errors=%d" % (done[0], len(errors)))
for e in errors[:20]:
    print("ERR", e)
