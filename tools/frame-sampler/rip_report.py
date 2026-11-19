"""rip_report.py -- read a rip_sampler.py file against the client's symbols and log.

Shows, for the ride (first to last 'VEHICLE: input' line of the client log) and for
the seconds before the mount: the frame rate from the log, each busy thread's CPU
use, which module the sampled threads were executing in, the client functions the
samples fell in, and the client functions seen on the stack (possible callers).

Usage: rip_report.py <samples file> <client exe> <client log> [nm path] [top n]
"""
import bisect
import re
import subprocess
import sys
from collections import Counter

sys.stdout.reconfigure(encoding="ascii", errors="replace")
samples_path, exe_path, log_path = sys.argv[1:4]
nm = sys.argv[4] if len(sys.argv) > 4 else r"C:\msys64\mingw64\bin\nm.exe"
top = int(sys.argv[5]) if len(sys.argv) > 5 else 22
IMAGE_BASE = 0x140000000

# --- the client's text symbols ---
addrs, names = [], []
for line in subprocess.run([nm, "-n", "--defined-only", exe_path], capture_output=True,
                           text=True, errors="replace").stdout.splitlines():
    parts = line.split(" ", 2)
    if len(parts) == 3 and parts[1] in "tT" and not parts[2].startswith(".text"):
        addrs.append(int(parts[0], 16))
        names.append(parts[2][:70])


def symbol(va):
    i = bisect.bisect_right(addrs, va) - 1
    return names[i] if i >= 0 else "(before text)"


# --- the samples file ---
created = None
mods = []
secs = []
cpus = []
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
    elif line.startswith("SEC "):
        m = re.match(r"SEC ([\d.]+) prio=(\w+) throttle=(\S+) threads=(\d+) busiest=(\S+) top=(.*)", line)
        tops = [(int(a), float(b)) for a, b in (x.split(":") for x in m.group(6).split(",") if x)]
        secs.append((float(m.group(1)), m.group(2), m.group(3), tops))
    elif line.startswith("CPU "):
        m = re.match(r"CPU ([\d.]+) tthrottle=(\S+) n=(\d+) top=(.*)", line)
        loads = [(int(a), float(b)) for a, b in (x.split(":") for x in m.group(4).split(",") if x)]
        cpus.append((float(m.group(1)), m.group(2), int(m.group(3)), loads))
    elif line.startswith("S "):
        f = line.split(" ")
        callers = [int(x, 16) for x in f[4].split(",")] if len(f) > 4 and f[4] else []
        samples.append((float(f[1]), int(f[2]), int(f[3], 16), callers))
exe_base = mods[0][0] if mods else 0
print("process created %.3f, %d samples, %d second marks, %d modules, client base %x"
      % (created or 0, len(samples), len(secs), len(mods), exe_base))


def module_of(addr):
    for base, size, name in mods:
        if base <= addr < base + size:
            return name
    return "(no module)"


# --- the client log: ride window and frame rate ---
stamp = re.compile(r"^\[(\d+):(\d+\.\d+)\] (.*)$")
ride = []
mounted = None
bmove = []
for line in open(log_path, encoding="utf-8", errors="replace"):
    m = stamp.match(line)
    if not m:
        continue
    t = int(m.group(1)) * 60 + float(m.group(2))
    text = m.group(3)
    if text.startswith("VEHICLE: input player=0"):
        ride.append(t)
    elif text.startswith("VEHICLE: mounted") and mounted is None:
        mounted = t
    elif text.startswith("BMOVE: allowc1x"):
        u = re.search(r"lvupdate240=(\d+)", text)
        bmove.append((t, int(u.group(1)) if u else -1))
print("log: mounted at %s, ride lines %d (%.1f..%.1f)" % (
    mounted, len(ride), ride[0] if ride else 0, ride[-1] if ride else 0))
if len(ride) > 1:
    print("log: ride fps per 30 frames:", " ".join(
        "%.0f" % (30.0 / (b - a)) for a, b in zip(ride, ride[1:]) if b > a))
print("log: BMOVE (time, lvupdate240):", " ".join("%.1f/%d" % x for x in bmove))


def window(name, lo, hi):
    """Report the samples whose time since process creation is in [lo, hi)."""
    print()
    print("==== %s: %.1f s to %.1f s after process start ====" % (name, lo, hi))
    marks = [s for s in secs if lo <= s[0] - created < hi]
    if marks:
        cpu = Counter()
        for _, _, _, tops in marks:
            for tid, d in tops:
                cpu[tid] += d
        print("priority class %s, power throttling %s" % (marks[-1][1], marks[-1][2]))
        print("CPU seconds per second, busiest threads:", ", ".join(
            "%d: %.2f" % (tid, total / len(marks)) for tid, total in cpu.most_common(5)))
    cmarks = [c for c in cpus if lo <= c[0] - created < hi]
    if cmarks:
        load = Counter()
        for _, _, _, loads in cmarks:
            for core, pct in loads:
                load[core] += pct
        # Which numbers are performance cores depends on the processor (on an
        # i9-12900KF Windows numbers the performance cores' threads 0 to 15
        # and the efficiency cores 16 to 23); the README says how to read it.
        print("busiest thread's power throttling %s; logical processors by load "
              "(of %d): %s" % (
                  cmarks[-1][1], cmarks[-1][2],
                  ", ".join("#%d %.0f%%" % (core, total / len(cmarks))
                            for core, total in load.most_common(6))))
    rows = [s for s in samples if lo <= s[0] - created < hi]
    by_thread = Counter(s[1] for s in rows)
    for tid, count in by_thread.most_common(2):
        mine = [s for s in rows if s[1] == tid]
        print()
        print("-- thread %d: %d samples" % (tid, count))
        where = Counter(module_of(s[2]) for s in mine)
        print("   module: " + ", ".join("%s %.0f%%" % (n, 100.0 * c / count)
                                         for n, c in where.most_common(6)))
        leaf = Counter()
        seen = Counter()
        for _, _, rip, callers in mine:
            if exe_base and module_of(rip) == mods[0][2]:
                leaf[symbol(rip - exe_base + IMAGE_BASE)] += 1
            for name in {symbol(c - exe_base + IMAGE_BASE) for c in callers}:
                seen[name] += 1
        if leaf:
            print("   in the client itself (leaf function, share of this thread's samples):")
            for name, c in leaf.most_common(top):
                print("     %5.1f%%  %s" % (100.0 * c / count, name))
        if seen:
            print("   client functions on the stack (share of samples that show them):")
            for name, c in seen.most_common(top + 14):
                print("     %5.1f%%  %s" % (100.0 * c / count, name))


if ride and mounted is not None:
    window("before the mount", max(0.0, mounted - 6.0), mounted - 0.5)
    window("the ride", ride[0] + 0.5, ride[-1])
else:
    last = samples[-1][0] - created if samples else 0
    window("the last 10 seconds", last - 10.0, last)
