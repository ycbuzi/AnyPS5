import json, sys, os, threading, time
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler

FIX = sys.argv[1]
PORT = int(sys.argv[2])
fx = json.load(open(FIX))
base = os.path.dirname(FIX)
log = open(sys.argv[3], "w")

class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, *a): pass
    def chk(self):
        ok = self.headers.get("Host") == "127.0.0.1:8790"
        log.write(f"{self.command} {self.path} host_ok={ok}\n"); log.flush()
        if not ok:
            self.send_response(403); self.send_header("Content-Length", "0"); self.end_headers()
        return ok
    def body(self, code, data, ctype):
        self.send_response(code); self.send_header("Content-Type", ctype); self.send_header("Content-Length", str(len(data))); self.end_headers(); self.wfile.write(data)
    def do_GET(self):
        if not self.chk(): return
        if self.path == "/library.js":
            return self.body(200, ("window.LIBRARY = " + json.dumps(fx["library"]) + ";").encode(), "text/javascript")
        if self.path == "/events":
            self.send_response(200); self.send_header("Content-Type", "text/event-stream"); self.send_header("Cache-Control", "no-cache"); self.end_headers()
            self.wfile.write(("data: " + json.dumps(fx["state"]) + "\n\n").encode()); self.wfile.flush()
            time.sleep(1.0)
            self.wfile.write(("data: " + json.dumps({"toast": "Mock host toast"}) + "\n\n").encode()); self.wfile.flush()
            while True:
                time.sleep(5)
                try: self.wfile.write(b": ping\n\n"); self.wfile.flush()
                except Exception: return
        p = os.path.join(base, self.path.lstrip("/"))
        if self.path.startswith("/art/") and os.path.isfile(p):
            return self.body(200, open(p, "rb").read(), "image/jpeg")
        self.body(404, b"", "text/plain")
    def do_POST(self):
        if not self.chk(): return
        n = int(self.headers.get("Content-Length", "0"))
        data = self.rfile.read(n)
        log.write(f"  origin={self.headers.get('Origin')} body={data.decode()[:200]}\n"); log.flush()
        self.body(200, b'{"ok":true,"data":{}}', "application/json")

ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
