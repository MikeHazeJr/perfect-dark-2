"""Self-test of rip_sampler.py on a child process of this script (a Python busy loop).

Starts a child that computes for six seconds, samples it by process id, and
prints what the samples file holds and which modules the samples fell in. A
working sampler shows a few hundred "S" lines, most of them inside the Python
runtime's own module.

Usage: rip_sampler_selftest.py [output directory]   (default: a temporary one)
"""
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

here = Path(__file__).resolve().parent
outdir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(tempfile.mkdtemp(prefix="rip-selftest-"))
outdir.mkdir(parents=True, exist_ok=True)
out = outdir / "rip_selftest.txt"
child = subprocess.Popen([sys.executable, "-c",
                          "import time, math\nt = time.time()\nx = 0.0\n"
                          "while time.time() - t < 6:\n    x += math.sin(x) * 1.0001\n"])
try:
    rc = subprocess.call([sys.executable, str(here / "rip_sampler.py"),
                          "pid:%d" % child.pid, str(out), "9", "50"])
finally:
    child.wait()
print("sampler exit", rc, "child exit", child.returncode)
kinds = Counter()
mods = []
rips = []
for line in out.read_text("ascii", "replace").splitlines():
    kinds[line.split(" ", 1)[0]] += 1
    if line.startswith("M "):
        base, size, name = line.split(" ", 3)[1:]
        mods.append((int(base, 16), int(size, 16), name))
    elif line.startswith("S "):
        rips.append(int(line.split(" ")[3], 16))
    elif line.startswith(("PROCESS", "SEC", "END")) and kinds[line.split(" ", 1)[0]] <= 3:
        print(line[:200])
print(dict(kinds))
where = Counter()
for rip in rips:
    name = "(unknown)"
    for base, size, mod in mods:
        if base <= rip < base + size:
            name = mod.rsplit("\\", 1)[-1]
            break
    where[name] += 1
print(where.most_common(6))
print("samples file:", out)
sys.exit(0 if rc == 0 and kinds["S"] > 50 else 1)
