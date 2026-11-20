import collections, glob, os, struct, sys

NAMES = {333: "Elect", 334: "All", 335: "Any", 336: "AllEqual", 337: "Broadcast", 338: "BroadcastFirst", 339: "Ballot",
         340: "InverseBallot", 341: "BallotBitExtract", 342: "BallotBitCount", 343: "BallotFindLSB", 344: "BallotFindMSB",
         345: "Shuffle", 346: "ShuffleXor", 347: "ShuffleUp", 348: "ShuffleDown", 349: "IAdd", 350: "FAdd", 351: "IMul",
         352: "FMul", 353: "SMin", 354: "UMin", 355: "FMin", 356: "SMax", 357: "UMax", 358: "FMax", 359: "BitwiseAnd",
         360: "BitwiseOr", 361: "BitwiseXor", 362: "LogicalAnd", 363: "LogicalOr", 364: "LogicalXor", 365: "QuadBroadcast",
         366: "QuadSwap", 4421: "SubgroupBallotKHR", 4422: "SubgroupFirstInvocationKHR", 4432: "SubgroupReadInvocationKHR"}
MODELS = {0: "VS", 3: "GS", 4: "PS", 5: "CS"}
BUILTINS = {36: "SubgroupSize", 40: "SubgroupId", 41: "SubgroupLocalInvocationId", 4416: "SubgroupEqMask", 4417: "SubgroupGeMask",
            4418: "SubgroupGtMask", 4419: "SubgroupLeMask", 4420: "SubgroupLtMask"}


def scan(path):
    w = struct.unpack("<%dI" % (os.path.getsize(path) // 4), open(path, "rb").read())
    i, ops, model, builtins = 5, collections.Counter(), "?", set()
    while i < len(w):
        op, n = w[i] & 0xFFFF, w[i] >> 16
        if n == 0:
            break
        if op == 15:
            model = MODELS.get(w[i + 1], str(w[i + 1]))
        elif op in NAMES:
            ops[NAMES[op]] += 1
        elif op == 71 and w[i + 2] == 11 and w[i + 3] in BUILTINS:
            builtins.add(BUILTINS[w[i + 3]])
        i += n
    return model, ops, builtins


stages, opfiles, builtinfiles, total = collections.Counter(), collections.Counter(), collections.Counter(), 0
for f in sorted(glob.glob(os.path.join(sys.argv[1], "*.spv"))):
    model, ops, builtins = scan(f)
    if not ops and not builtins:
        continue
    total += 1
    stages[model] += 1
    for k in ops:
        opfiles[k] += 1
    for k in builtins:
        builtinfiles[k] += 1
print("shaders with subgroup use:", total, dict(stages))
print("ops (files):", opfiles.most_common())
print("builtins (files):", builtinfiles.most_common())
