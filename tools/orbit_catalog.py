#!/usr/bin/env python3
"""orbit-catalog: the game server's list of PS2 games for the ORBIT launcher (phase 17).

GET  /catalog        one line per ISO under <root>/DVD and <root>/CD, tab-separated:
                     path  size  serial  hash  title   (serial SLUS_213.76 form, hash = rcheevos rc_hash_ps2,
                     "-" while not hashed yet or unreadable). Answered from the cache, never waiting for a hash:
                     a background thread hashes new or changed ISOs (key: path, size, mtime) with orbit_hash.
POST /vmc/<name>     creates <root>/VMC/<name>.bin (empty 8 MB card, the launcher's format) if missing.

Runs as a user service on nuld next to udpfs_server.py: tools/orbit_catalog.py --root /srv/data/ps2 --port 18290
"""
import argparse, json, os, re, subprocess, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

OPL_PREFIX = re.compile(r"^[A-Z]{4}_\d{3}\.\d{2}\.")  # "SLUS_209.46.God of War.iso" (OPL naming)


def title_of(name):  # as the launcher's iso_title
    t = name[12:] if OPL_PREFIX.match(name) else name
    root, ext = os.path.splitext(t)
    return root if ext.lower() in (".iso", ".vcd", ".elf") else t


class Catalog:
    def __init__(self, root, tool, cache):
        self.root, self.tool, self.cache_path = root, tool, cache
        self.lock = threading.Lock()
        try:
            self.cache = json.load(open(cache))
        except (OSError, ValueError):
            self.cache = {}  # path -> [size, mtime, serial, hash]

    def isos(self):
        for d in ("DVD", "CD"):
            try:
                names = sorted(os.listdir(os.path.join(self.root, d)))
            except OSError:
                continue
            for n in names:
                p = os.path.join(d, n)
                if n.lower().endswith(".iso") and os.path.isfile(os.path.join(self.root, p)):
                    st = os.stat(os.path.join(self.root, p))
                    yield p, st.st_size, int(st.st_mtime)

    def text(self):
        out = []
        with self.lock:
            for p, size, mtime in self.isos():
                c = self.cache.get(p)
                serial, h = (c[2], c[3]) if c and c[0] == size and c[1] == mtime else ("-", "-")
                out.append(f"{p}\t{size}\t{serial}\t{h}\t{title_of(os.path.basename(p))}\n")
        return "".join(out)

    def refresh(self):  # hash what is new or changed; drop what is gone
        todo = [(p, s, m) for p, s, m in self.isos() if (c := self.cache.get(p)) is None or c[:2] != [s, m]]
        for p, size, mtime in todo:
            r = subprocess.run([self.tool, "hash", os.path.join(self.root, p)], capture_output=True, text=True)
            serial, h = (r.stdout.split("\t") + ["-", "-"])[:2] if r.stdout else ("-", "-")
            with self.lock:
                self.cache[p] = [size, mtime, serial.strip(), h.strip()]
            print(f"hashed {p}: {serial.strip()} {h.strip()}", flush=True)
        with self.lock:
            present = {p for p, _, _ in self.isos()}
            for p in [p for p in self.cache if p not in present]:
                del self.cache[p]
            if todo:
                tmp = self.cache_path + ".tmp"
                json.dump(self.cache, open(tmp, "w"))
                os.replace(tmp, self.cache_path)

    def vmc(self, name):
        if not re.fullmatch(r"[A-Za-z0-9_.-]{1,32}", name) or name.startswith("."):
            return 400
        path = os.path.join(self.root, "VMC", name + ".bin")
        if os.path.exists(path):
            if os.path.getsize(path) == 8 * 1024 * 1024:
                return 200
            # a card cut short (a write over udpfs that stalled, 2026-10-03) is no card: kept aside, never deleted
            os.replace(path, path + time.strftime(".short-%Y%m%d-%H%M%S"))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        return 201 if subprocess.run([self.tool, "vmc", path]).returncode == 0 else 500


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default="/srv/data/ps2")
    ap.add_argument("--port", type=int, default=18290)
    ap.add_argument("--tool", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "orbit_hash"))
    ap.add_argument("--cache", default=os.path.expanduser("~/.cache/orbit-catalog.json"))
    ap.add_argument("--rescan", type=int, default=60, help="seconds between scans for new ISOs")
    a = ap.parse_args()
    os.makedirs(os.path.dirname(a.cache), exist_ok=True)
    cat = Catalog(a.root, a.tool, a.cache)

    def scanner():
        while True:
            try:
                cat.refresh()
            except Exception as e:  # keep serving the last good list
                print("scan failed:", e, flush=True)
            time.sleep(a.rescan)

    threading.Thread(target=scanner, daemon=True).start()

    class H(BaseHTTPRequestHandler):
        def send(self, code, body=b""):
            self.send_response(code)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            self.send(200, cat.text().encode()) if self.path == "/catalog" else self.send(404)

        def do_POST(self):
            m = re.fullmatch(r"/vmc/([^/]+)", self.path)
            self.send(cat.vmc(m.group(1)) if m else 404)

        def log_message(self, fmt, *args):
            print("%s %s" % (self.client_address[0], fmt % args), flush=True)

    ThreadingHTTPServer(("0.0.0.0", a.port), H).serve_forever()


if __name__ == "__main__":
    main()
