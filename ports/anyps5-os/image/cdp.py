"""Drive the AnyPS5 OS console through Chromium's DevTools port (127.0.0.1:9222) with only the Python standard library.

    python3 cdp.py eval '<javascript>'          prints the result
    python3 cdp.py shot out.png                 saves a screenshot of the console page
    python3 cdp.py run '<javascript>' out.png [wait seconds]   eval, wait, then screenshot
Runs on the Chromebook (the port listens on localhost only).
"""
import base64
import json
import os
import socket
import struct
import sys
import time
import urllib.request


def page_ws():
    for t in json.load(urllib.request.urlopen("http://127.0.0.1:9222/json", timeout=10)):
        if t.get("type") == "page" and t.get("url", "").startswith("http://127.0.0.1:8790"):
            return t["webSocketDebuggerUrl"]
    sys.exit("no AnyPS5 console page in Chromium")


class WS:
    def __init__(self, url):
        host, path = url[len("ws://"):].split("/", 1)
        h, p = host.split(":")
        self.s = socket.create_connection((h, int(p)), timeout=60)
        key = base64.b64encode(os.urandom(16)).decode()
        self.s.sendall(("GET /%s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n" % (path, host, key)).encode())
        buf = b""
        while b"\r\n\r\n" not in buf:
            buf += self.s.recv(4096)
        if b" 101 " not in buf.split(b"\r\n", 1)[0]:
            sys.exit("websocket refused: " + buf.split(b"\r\n", 1)[0].decode())
        self.rest = buf.split(b"\r\n\r\n", 1)[1]
        self.n = 0

    def _read(self, k):
        while len(self.rest) < k:
            chunk = self.s.recv(1 << 20)
            if not chunk:
                raise EOFError
            self.rest += chunk
        out, self.rest = self.rest[:k], self.rest[k:]
        return out

    def send(self, obj):
        data = json.dumps(obj).encode()
        head = bytearray([0x81])
        n = len(data)
        if n < 126:
            head.append(0x80 | n)
        elif n < 65536:
            head.append(0x80 | 126)
            head += struct.pack(">H", n)
        else:
            head.append(0x80 | 127)
            head += struct.pack(">Q", n)
        mask = os.urandom(4)
        self.s.sendall(bytes(head) + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

    def recv(self):
        msg = b""
        while True:
            b1, b2 = self._read(2)
            n = b2 & 0x7F
            if n == 126:
                n = struct.unpack(">H", self._read(2))[0]
            elif n == 127:
                n = struct.unpack(">Q", self._read(8))[0]
            msg += self._read(n)
            if b1 & 0x80:
                return json.loads(msg)

    def call(self, method, **params):
        self.n += 1
        self.send({"id": self.n, "method": method, "params": params})
        while True:
            r = self.recv()
            if r.get("id") == self.n:
                if "error" in r:
                    sys.exit("%s failed: %s" % (method, r["error"]))
                return r.get("result", {})


def evaluate(ws, js):
    r = ws.call("Runtime.evaluate", expression=js, returnByValue=True, awaitPromise=True)
    if r.get("exceptionDetails"):
        return "exception: " + json.dumps(r["exceptionDetails"].get("exception", {}).get("description", r["exceptionDetails"]))[:500]
    return r.get("result", {}).get("value")


def shot(ws, path):
    data = ws.call("Page.captureScreenshot", format="png")["data"]
    with open(path, "wb") as f:
        f.write(base64.b64decode(data))
    return path


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    ws = WS(page_ws())
    cmd = sys.argv[1]
    if cmd == "eval":
        print(json.dumps(evaluate(ws, sys.argv[2]))[:4000])
    elif cmd == "shot":
        print(shot(ws, sys.argv[2]))
    elif cmd == "run":
        print(json.dumps(evaluate(ws, sys.argv[2]))[:2000])
        time.sleep(float(sys.argv[4]) if len(sys.argv) > 4 else 3)
        print(shot(ws, sys.argv[3]))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
