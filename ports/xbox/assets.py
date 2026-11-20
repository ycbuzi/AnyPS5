import os
import struct
import sys
import zlib


def png(path, w, h):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    row = b"\x00" + b"\x10\x30\x80" * w
    raw = row * h
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(data)


out = sys.argv[1]
os.makedirs(out, exist_ok=True)
for name, w, h in (("StoreLogo.png", 50, 50), ("Square150x150Logo.png", 150, 150), ("Square44x44Logo.png", 44, 44), ("SplashScreen.png", 620, 300)):
    png(os.path.join(out, name), w, h)
