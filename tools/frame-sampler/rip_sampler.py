"""rip_sampler.py -- where is a running game client's busiest thread executing?

Waits for a process named PerfectDark.exe whose image path contains a given text,
then until it exits (or a time limit) records:
  * every second: process priority class, power throttling state, and the CPU time
    each of its busiest threads used in that second;
  * about 50 times a second: the instruction pointer of the busiest thread, plus
    the values on its stack that point into the client's own code (possible callers).
It reads the process only (the sampled thread is suspended for well under a
millisecond per sample and always resumed). Nothing is written but the output file.

Usage: rip_sampler.py <image path text> <out file> [max seconds] [samples per second]
"""
import ctypes
import ctypes.wintypes as wt
import sys
import time

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)

TH32CS_SNAPPROCESS = 0x2
TH32CS_SNAPTHREAD = 0x4
PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
SYNCHRONIZE = 0x00100000
THREAD_SUSPEND_RESUME = 0x0002
THREAD_GET_CONTEXT = 0x0008
THREAD_QUERY_INFORMATION = 0x0040
INVALID_HANDLE = wt.HANDLE(-1).value
CONTEXT_SIZE = 1232
CONTEXT_FLAGS_OFFSET = 0x30
CONTEXT_RSP_OFFSET = 0x98
CONTEXT_RIP_OFFSET = 0xF8
CONTEXT_CONTROL_AMD64 = 0x100001
STACK_QWORDS = 192


class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ProcessID", wt.DWORD),
                ("th32DefaultHeapID", ctypes.c_size_t), ("th32ModuleID", wt.DWORD),
                ("cntThreads", wt.DWORD), ("th32ParentProcessID", wt.DWORD),
                ("pcPriClassBase", wt.LONG), ("dwFlags", wt.DWORD),
                ("szExeFile", wt.WCHAR * 260)]


class THREADENTRY32(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ThreadID", wt.DWORD),
                ("th32OwnerProcessID", wt.DWORD), ("tpBasePri", wt.LONG),
                ("tpDeltaPri", wt.LONG), ("dwFlags", wt.DWORD)]


class MODULEINFO(ctypes.Structure):
    _fields_ = [("lpBaseOfDll", ctypes.c_void_p), ("SizeOfImage", wt.DWORD),
                ("EntryPoint", ctypes.c_void_p)]


class POWER_THROTTLING(ctypes.Structure):
    _fields_ = [("Version", wt.ULONG), ("ControlMask", wt.ULONG), ("StateMask", wt.ULONG)]


def proto(fn, restype, *argtypes):
    fn.restype = restype
    fn.argtypes = list(argtypes)
    return fn


CreateToolhelp32Snapshot = proto(k32.CreateToolhelp32Snapshot, wt.HANDLE, wt.DWORD, wt.DWORD)
Process32FirstW = proto(k32.Process32FirstW, wt.BOOL, wt.HANDLE, ctypes.POINTER(PROCESSENTRY32W))
Process32NextW = proto(k32.Process32NextW, wt.BOOL, wt.HANDLE, ctypes.POINTER(PROCESSENTRY32W))
Thread32First = proto(k32.Thread32First, wt.BOOL, wt.HANDLE, ctypes.POINTER(THREADENTRY32))
Thread32Next = proto(k32.Thread32Next, wt.BOOL, wt.HANDLE, ctypes.POINTER(THREADENTRY32))
CloseHandle = proto(k32.CloseHandle, wt.BOOL, wt.HANDLE)
OpenProcess = proto(k32.OpenProcess, wt.HANDLE, wt.DWORD, wt.BOOL, wt.DWORD)
OpenThread = proto(k32.OpenThread, wt.HANDLE, wt.DWORD, wt.BOOL, wt.DWORD)
QueryFullProcessImageNameW = proto(k32.QueryFullProcessImageNameW, wt.BOOL, wt.HANDLE, wt.DWORD,
                                   wt.LPWSTR, ctypes.POINTER(wt.DWORD))
GetProcessTimes = proto(k32.GetProcessTimes, wt.BOOL, wt.HANDLE, ctypes.POINTER(wt.FILETIME),
                        ctypes.POINTER(wt.FILETIME), ctypes.POINTER(wt.FILETIME),
                        ctypes.POINTER(wt.FILETIME))
GetThreadTimes = proto(k32.GetThreadTimes, wt.BOOL, wt.HANDLE, ctypes.POINTER(wt.FILETIME),
                       ctypes.POINTER(wt.FILETIME), ctypes.POINTER(wt.FILETIME),
                       ctypes.POINTER(wt.FILETIME))
SuspendThread = proto(k32.SuspendThread, wt.DWORD, wt.HANDLE)
ResumeThread = proto(k32.ResumeThread, wt.DWORD, wt.HANDLE)
GetThreadContext = proto(k32.GetThreadContext, wt.BOOL, wt.HANDLE, ctypes.c_void_p)
ReadProcessMemory = proto(k32.ReadProcessMemory, wt.BOOL, wt.HANDLE, ctypes.c_void_p,
                          ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t))
WaitForSingleObject = proto(k32.WaitForSingleObject, wt.DWORD, wt.HANDLE, wt.DWORD)
GetPriorityClass = proto(k32.GetPriorityClass, wt.DWORD, wt.HANDLE)
GetProcessInformation = proto(k32.GetProcessInformation, wt.BOOL, wt.HANDLE, ctypes.c_int,
                              ctypes.c_void_p, wt.DWORD)
EnumProcessModulesEx = proto(psapi.EnumProcessModulesEx, wt.BOOL, wt.HANDLE,
                             ctypes.POINTER(ctypes.c_void_p), wt.DWORD,
                             ctypes.POINTER(wt.DWORD), wt.DWORD)
GetModuleInformation = proto(psapi.GetModuleInformation, wt.BOOL, wt.HANDLE, ctypes.c_void_p,
                             ctypes.POINTER(MODULEINFO), wt.DWORD)
GetModuleFileNameExW = proto(psapi.GetModuleFileNameExW, wt.DWORD, wt.HANDLE, ctypes.c_void_p,
                             wt.LPWSTR, wt.DWORD)


class PROCESSOR_PERF(ctypes.Structure):
    # SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION; KernelTime includes IdleTime.
    _fields_ = [("IdleTime", ctypes.c_int64), ("KernelTime", ctypes.c_int64),
                ("UserTime", ctypes.c_int64), ("DpcTime", ctypes.c_int64),
                ("InterruptTime", ctypes.c_int64), ("InterruptCount", wt.ULONG)]


ntdll = ctypes.WinDLL("ntdll")
NtQuerySystemInformation = proto(ntdll.NtQuerySystemInformation, ctypes.c_long, ctypes.c_int,
                                 ctypes.c_void_p, wt.ULONG, ctypes.POINTER(wt.ULONG))
GetThreadInformation = proto(k32.GetThreadInformation, wt.BOOL, wt.HANDLE, ctypes.c_int,
                             ctypes.c_void_p, wt.DWORD)
MAX_CORES = 256


def core_times():
    """(busy, total) 100 ns counts per logical processor, or [] when unavailable."""
    arr = (PROCESSOR_PERF * MAX_CORES)()
    got = wt.ULONG(0)
    if NtQuerySystemInformation(8, arr, ctypes.sizeof(arr), ctypes.byref(got)) != 0:
        return []
    n = got.value // ctypes.sizeof(PROCESSOR_PERF)
    return [(arr[i].KernelTime - arr[i].IdleTime + arr[i].UserTime,
             arr[i].KernelTime + arr[i].UserTime) for i in range(n)]


def filetime(ft):
    return ((ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 1e7


def find_process(text):
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    if snap in (None, INVALID_HANDLE):
        return None
    found = None
    try:
        entry = PROCESSENTRY32W()
        entry.dwSize = ctypes.sizeof(entry)
        ok = Process32FirstW(snap, ctypes.byref(entry))
        while ok:
            if entry.szExeFile.lower() == "perfectdark.exe":
                h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, entry.th32ProcessID)
                if h:
                    buf = ctypes.create_unicode_buffer(1024)
                    size = wt.DWORD(1024)
                    path = buf.value if QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(size)) else ""
                    CloseHandle(h)
                    if text.lower() in path.lower():
                        found = (entry.th32ProcessID, path)
                        break
            ok = Process32NextW(snap, ctypes.byref(entry))
    finally:
        CloseHandle(snap)
    return found


def thread_ids(pid):
    out = []
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)
    if snap in (None, INVALID_HANDLE):
        return out
    try:
        entry = THREADENTRY32()
        entry.dwSize = ctypes.sizeof(entry)
        ok = Thread32First(snap, ctypes.byref(entry))
        while ok:
            if entry.th32OwnerProcessID == pid:
                out.append(entry.th32ThreadID)
            ok = Thread32Next(snap, ctypes.byref(entry))
    finally:
        CloseHandle(snap)
    return out


def modules(hproc):
    arr = (ctypes.c_void_p * 1024)()
    needed = wt.DWORD(0)
    out = []
    if not EnumProcessModulesEx(hproc, arr, ctypes.sizeof(arr), ctypes.byref(needed), 3):
        return out
    count = min(1024, needed.value // ctypes.sizeof(ctypes.c_void_p))
    for i in range(count):
        info = MODULEINFO()
        if not GetModuleInformation(hproc, arr[i], ctypes.byref(info), ctypes.sizeof(info)):
            continue
        buf = ctypes.create_unicode_buffer(1024)
        GetModuleFileNameExW(hproc, arr[i], buf, 1024)
        out.append((info.lpBaseOfDll or 0, info.SizeOfImage, buf.value))
    return out


def main():
    text = sys.argv[1]
    out_path = sys.argv[2]
    limit = float(sys.argv[3]) if len(sys.argv) > 3 else 400.0
    hz = float(sys.argv[4]) if len(sys.argv) > 4 else 50.0
    wait_until = time.time() + limit
    proc = None
    while time.time() < wait_until:
        if text.startswith("pid:"):
            proc = (int(text[4:]), "(given pid)")
        else:
            proc = find_process(text)
        if proc:
            break
        time.sleep(0.25)
    out = open(out_path, "w", encoding="ascii", errors="replace")
    if not proc:
        out.write("NOPROCESS\n")
        out.close()
        print("rip_sampler: no matching process within the limit")
        return 2
    pid, path = proc
    hproc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, False, pid)
    if not hproc:
        out.write("OPENFAILED %d\n" % ctypes.get_last_error())
        out.close()
        print("rip_sampler: cannot open process", pid, ctypes.get_last_error())
        return 3
    c, e, k, u = wt.FILETIME(), wt.FILETIME(), wt.FILETIME(), wt.FILETIME()
    GetProcessTimes(hproc, ctypes.byref(c), ctypes.byref(e), ctypes.byref(k), ctypes.byref(u))
    # FILETIME epoch (1601) to Unix epoch.
    created = filetime(c) - 11644473600.0
    out.write("PROCESS pid=%d created=%.3f path=%s\n" % (pid, created, path))

    raw = (ctypes.c_char * (CONTEXT_SIZE + 32))()
    ctx = (ctypes.addressof(raw) + 15) & ~15
    stack = (ctypes.c_uint64 * STACK_QWORDS)()
    got = ctypes.c_size_t(0)

    handles = {}      # tid -> handle
    last_cpu = {}     # tid -> cpu seconds at the last second mark
    busiest = None
    targets = []
    last_cores = []
    exe_lo = exe_hi = 0
    next_second = 0.0
    next_modules = time.time() + 1.5
    period = 1.0 / hz
    next_sample = time.perf_counter()
    samples = 0
    try:
        while time.time() < wait_until:
            if WaitForSingleObject(hproc, 0) == 0:
                break
            now = time.time()
            if now >= next_modules:
                mods = modules(hproc)
                if mods:
                    exe_lo, exe_hi = mods[0][0], mods[0][0] + mods[0][1]
                    out.write("MODULES %.3f %d\n" % (now, len(mods)))
                    for base, size, name in mods:
                        out.write("M %x %x %s\n" % (base, size, name))
                next_modules = now + 10.0
            if now >= next_second:
                next_second = now + 1.0
                for tid in thread_ids(pid):
                    if tid not in handles:
                        h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT
                                       | THREAD_QUERY_INFORMATION, False, tid)
                        if h:
                            handles[tid] = h
                deltas = []
                for tid, h in list(handles.items()):
                    tc, te, tk, tu = wt.FILETIME(), wt.FILETIME(), wt.FILETIME(), wt.FILETIME()
                    if not GetThreadTimes(h, ctypes.byref(tc), ctypes.byref(te),
                                          ctypes.byref(tk), ctypes.byref(tu)):
                        continue
                    cpu = filetime(tk) + filetime(tu)
                    deltas.append((cpu - last_cpu.get(tid, cpu), filetime(tk), tid))
                    last_cpu[tid] = cpu
                deltas.sort(reverse=True)
                if deltas and deltas[0][0] > 0:
                    busiest = deltas[0][2]
                    targets = [tid for d, _, tid in deltas[:2] if d > 0.03]
                throttle = POWER_THROTTLING(1, 0, 0)
                ok = GetProcessInformation(hproc, 4, ctypes.byref(throttle), ctypes.sizeof(throttle))
                out.write("SEC %.3f prio=%x throttle=%s threads=%d busiest=%s top=%s\n" % (
                    now, GetPriorityClass(hproc),
                    ("%x/%x" % (throttle.ControlMask, throttle.StateMask)) if ok else "n/a",
                    len(handles), busiest,
                    ",".join("%d:%.3f" % (tid, d) for d, _, tid in deltas[:6])))
                # The busiest thread's own power throttling state, and how busy
                # each logical processor was over the last second (the whole
                # machine: which cores carry the load, performance or efficiency).
                tthrottle = POWER_THROTTLING(1, 0, 0)
                tok = busiest in handles and GetThreadInformation(
                    handles[busiest], 3, ctypes.byref(tthrottle), ctypes.sizeof(tthrottle))
                cores = core_times()
                loads = []
                if cores and len(cores) == len(last_cores):
                    for i, ((busy, total), (pbusy, ptotal)) in enumerate(zip(cores, last_cores)):
                        if total > ptotal:
                            loads.append((100.0 * (busy - pbusy) / (total - ptotal), i))
                last_cores = cores
                loads.sort(reverse=True)
                out.write("CPU %.3f tthrottle=%s n=%d top=%s\n" % (
                    now, ("%x/%x" % (tthrottle.ControlMask, tthrottle.StateMask)) if tok else "n/a",
                    len(cores), ",".join("%d:%.0f" % (i, pct) for pct, i in loads[:6])))
            for target in targets:
                h = handles.get(target)
                if not h:
                    continue
                ctypes.memset(ctx, 0, CONTEXT_SIZE)
                ctypes.c_uint32.from_address(ctx + CONTEXT_FLAGS_OFFSET).value = CONTEXT_CONTROL_AMD64
                rip = rsp = 0
                nread = 0
                if SuspendThread(h) != 0xFFFFFFFF:
                    try:
                        if GetThreadContext(h, ctx):
                            rip = ctypes.c_uint64.from_address(ctx + CONTEXT_RIP_OFFSET).value
                            rsp = ctypes.c_uint64.from_address(ctx + CONTEXT_RSP_OFFSET).value
                            if rsp and ReadProcessMemory(hproc, rsp, stack, ctypes.sizeof(stack),
                                                         ctypes.byref(got)):
                                nread = got.value // 8
                    finally:
                        ResumeThread(h)
                if rip:
                    callers = [("%x" % stack[i]) for i in range(nread)
                               if exe_lo <= stack[i] < exe_hi]
                    out.write("S %.3f %d %x %s\n" % (time.time(), target, rip, ",".join(callers[:40])))
                    samples += 1
            next_sample += period
            delay = next_sample - time.perf_counter()
            if delay > 0:
                time.sleep(delay)
            else:
                next_sample = time.perf_counter()
    finally:
        mods = modules(hproc)
        if mods:
            out.write("MODULES %.3f %d\n" % (time.time(), len(mods)))
            for base, size, name in mods:
                out.write("M %x %x %s\n" % (base, size, name))
        for h in handles.values():
            CloseHandle(h)
        CloseHandle(hproc)
        out.write("END %.3f samples=%d\n" % (time.time(), samples))
        out.close()
    print("rip_sampler: pid", pid, "samples", samples, "->", out_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
