import ctypes, os, sys, time

dll, folder, seconds = sys.argv[1], sys.argv[2], int(sys.argv[3])
os.makedirs(folder, exist_ok=True)
lib = ctypes.CDLL(dll)
lib.PayloadMain.argtypes = [ctypes.c_wchar_p]
print("PayloadMain", lib.PayloadMain(folder), flush=True)
time.sleep(seconds)
try:
    print(open(os.path.join(folder, "gamehost.txt"), encoding="utf-8", errors="replace").read(), flush=True)
except OSError as error:
    print(error, flush=True)
os._exit(0)
