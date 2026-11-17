# Frame sampler

Three scripts that show where a running game client spends its time. They
need no special build and change nothing in the client. They were written to
find B-1234 (riding a mod vehicle ran at 20 frames a second) and found it in
one run, after reading the code had produced a wrong guess.

Use them when the game is slow and the log does not say why.

## What each script does

| Script | Use |
|---|---|
| `rip_sampler.py` | Records samples from a running client into a text file. |
| `rip_report.py` | Summary for a vehicle ride: frame rate from the log, CPU use, modules, functions. |
| `rip_chain.py` | The innermost call chains between two times. Use this for anything that is not a ride. |
| `rip_sampler_selftest.py` | Checks the sampler works on this machine (samples a Python busy loop). |

## Recording

```bash
python tools/frame-sampler/rip_sampler.py <image path text> <out file> [max seconds] [samples per second]
```

The sampler waits for a `PerfectDark.exe` whose full path contains
`<image path text>`, samples it until it exits or the time limit passes, and
writes the output file. Start it **before** the client, in the background.
`pid:1234` in place of the text samples that process id (any program).

A smoke's client runs from that session's install folder, so the text is the
part of the path that only your runs have, for example
`pd2-smoke\mods\installs`. With a smoke:

```bash
python tools/frame-sampler/rip_sampler.py 'pd2-smoke\mods\installs' out/rip.txt 420 50 &
powershell -File tools/smoke-verify/run.ps1 -Test maian_vehicle_boost_smoke ...
wait
```

It records, about 50 times a second, the instruction pointer of the busiest
thread and the values on its stack that point into the client's own code;
and once a second the CPU time of each busy thread, the process priority
class, the power throttling state and the load of each logical processor.

It only reads. The sampled thread is suspended for well under a millisecond
per sample and always resumed. It needs no administrator rights for a
process you started and changes no system setting.

## Reading a recording

```bash
python tools/frame-sampler/rip_report.py <samples> <client exe> <client log>
python tools/frame-sampler/rip_chain.py <samples> <client exe> <from s> <to s> [depth]
```

`<client exe>` must be the build that ran (function names come from its
symbols through `nm`; pass another `nm` path as the last argument if MSYS2 is
not at `C:\msys64`). `<client log>` is the run's `pd-client.log`; times are
seconds since the process started, which is what the log's stamps count.

How to read the numbers:

- **CPU seconds per second** of the main thread. Near 1.0 the thread never
  rests, so the frame rate is limited by what that thread computes. Around
  0.2 to 0.5 at 60 frames a second is normal: the rest is the frame limiter.
- **Module shares.** `ntdll.dll` means the thread is in a system call or
  waiting. The chain tells which: `sysSleep <- frametimeCalculate` is the
  frame limiter resting, anything else under `ntdll.dll` is a system call the
  game makes.
- **Chains** (`rip_chain.py`) are the first client functions found on the
  stack, nearest first. The first one or two are reliable. Deeper names come
  from scanning the stack, which also holds old return addresses, so names
  such as `register_frame_ctor` or an unrelated function can appear; trust a
  chain that repeats across most samples and makes sense in the source.
- **The function list in `rip_report.py`** counts every client function seen
  anywhere on the stack. It suggests where to look; it is not a call graph.
- **Logical processors by load**: which numbers are performance cores
  depends on the processor. Check that the load is not parked on efficiency
  cores and that power throttling reads `0/0` before blaming the game.

Limits: 50 samples a second needs a few seconds of the slow state to say
anything; time inside the kernel shows as `ntdll.dll`; inlined functions do
not appear.

## The worked example (B-1234)

Mounted on the Maian vehicle the main thread used 0.98 CPU-seconds per
second, and 95% of its samples were the one chain

```
[ntdll.dll] <- modelRodataIsReadable <- bgTestHitOnObj
```

The shot hit test was asking the operating system whether each vertex batch
was readable, 5,829 system calls for one test of the model. The base
hoverbike showed the same chain in a third of its samples and held 60 frames
a second only because its model is small. See `context/bugs.md` B-1234 and
`context/systemic-bugs.md` SP-95.
