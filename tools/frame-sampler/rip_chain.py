"""rip_chain.py -- the innermost client frames of each sample, in stack order.

For the busiest thread, between two times since process start, print the most
common chains of the first few client functions found on the stack (the first
is the nearest to the sampled instruction), and the leaf module.

Usage: rip_chain.py <samples file> <client exe> <from s> <to s> [depth] [nm path]
"""
import bisect
import re
import subprocess
import sys
from collections import Counter

sys.stdout.reconfigure(encoding="ascii", errors="replace")
samples_path, exe_path = sys.argv[1:3]
lo, hi = float(sys.argv[3]), float(sys.argv[4])
depth = int(sys.argv[5]) if len(sys.argv) > 5 else 4
nm = sys.argv[6] if len(sys.argv) > 6 else r"C:\msys64\mingw64\bin\nm.exe"
IMAGE_BASE = 0x140000000

addrs, names = [], []
for line in subprocess.run([nm, "-n", "--defined-only", exe_path], capture_output=True,
                           text=True, errors="replace").stdout.splitlines():
    parts = line.split(" ", 2)
    if len(parts) == 3 and parts[1] in "tT" and not parts[2].startswith(".text"):
        addrs.append(int(parts[0], 16))
        names.append(parts[2][:60])


def symbol(va):
    i = bisect.bisect_right(addrs, va) - 1
    return names[i] if i >= 0 else "(before text)"


created = None
mods = []
samples = []
for line in open(samples_path, encoding="ascii", errors="replace"):
    line = line.rstrip("\n")
    if line.startswith("PROCESS"):
        created = float(re.search(r"created=([\d.]+)", line).group(1))
    elif line.startswith("MODULES"):
        mods = []
    elif line.startswith("M "):
        base, size, name = line.split(" ", 3)[1:]
        mods.append((int(base, 16), int(size, 16), name.rsplit("\\", 1)[-1]))
    elif line.startswith("S "):
        f = line.split(" ")
        callers = [int(x, 16) for x in f[4].split(",")] if len(f) > 4 and f[4] else []
        samples.append((float(f[1]), int(f[2]), int(f[3], 16), callers))

exe_base, exe_size, exe_name = mods[0]


def module_of(addr):
    for base, size, name in mods:
        if base <= addr < base + size:
            return name
    return "(no module)"


rows = [s for s in samples if lo <= s[0] - created < hi]
tid = Counter(s[1] for s in rows).most_common(1)[0][0]
mine = [s for s in rows if s[1] == tid]
chains = Counter()
for _, _, rip, callers in mine:
    chain = []
    if exe_base <= rip < exe_base + exe_size:
        chain.append("[" + symbol(rip - exe_base + IMAGE_BASE) + "]")
    else:
        chain.append("[" + module_of(rip) + "]")
    for addr in callers:
        if exe_base <= addr < exe_base + exe_size:
            name = symbol(addr - exe_base + IMAGE_BASE)
            if not chain or chain[-1] != name:
                chain.append(name)
        if len(chain) > depth:
            break
    chains[" <- ".join(chain)] += 1
print("thread %d, %d samples in %.1f..%.1f s" % (tid, len(mine), lo, hi))
for chain, count in chains.most_common(14):
    print("%5.1f%%  %s" % (100.0 * count / len(mine), chain))
