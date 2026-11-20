import socket
import sys
import time


def main():
    host = open(sys.argv[1]).read().strip()
    port = int(sys.argv[2])
    deadline = time.time() + float(sys.argv[3])
    tries = 0
    while time.time() < deadline:
        tries += 1
        try:
            s = socket.create_connection((host, port), timeout=3)
        except OSError:
            time.sleep(1)
            continue
        s.settimeout(30)
        s.sendall(b"hello from pc")
        total = 0
        t0 = time.time()
        while True:
            chunk = s.recv(1 << 20)
            if not chunk:
                break
            total += len(chunk)
        s.close()
        secs = max(time.time() - t0, 1e-6)
        print("connected after %d tries, got %.1f MB at %.1f MB/s" % (tries, total / 1048576, total / 1048576 / secs))
        return 0
    print("no connection after %d tries" % tries)
    return 1


if __name__ == "__main__":
    sys.exit(main())
