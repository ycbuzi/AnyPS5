import sys
out = open(sys.argv[1], "w", encoding="utf-8")
for pair in sys.argv[2:]:
    name, path = pair.split("=")
    data = open(path, "rb").read()
    out.write("static const unsigned char %s[] = {%s};\n" % (name, ",".join(str(b) for b in data)))
