#!/usr/bin/env bash
# BGFX parity experiments (Workbench D-041). Each stage takes its own hub FIFO
# (AGENTS.md order 7): the client and pd-tests builds under `build`, pd-tests
# under `test-runner`, and the game-runtime slot only for the ready smoke batch.
# After each smoke its parity images and client log are copied to a private
# evidence folder on D: (ROM-derived: never in git), before the smoke store can
# recycle the install.
# usage: bash bgfx_exp.sh <run-name> <smoke> [smoke ...]
set -u
RUN="$1"; shift
SMOKES="$*"
SID=claude-week-pd-bgfx-classic
HUB=/c/Users/mikeh/Perfect-Dark-2/perfect_dark-mike
WT=/d/PD2-wt/bgfx-classic
SP=/c/Users/mikeh/AppData/Local/Temp/claude/414ce348b8df686f/2be94b0d-7557-43a7-a035-87b831c87e15/scratchpad
OUT="$SP/$RUN"
EV="/d/PD2-wt/bgfx-parity-evidence/$RUN"
LOG="$SP/$RUN-driver.log"
B=.claude/session-builds/bgfx-classic
mkdir -p "$OUT" "$EV"
cd "$WT" || exit 2
BRANCH=$(git branch --show-current)
HEAD8=$(git rev-parse HEAD | cut -c1-8)

say() { echo "$* $(date -u +%H:%M:%SZ)" >> "$LOG"; }
hub() { (cd "$HUB" && pwsh -NoProfile -File Tools/CodexCoordination/CodexCoordination.ps1 "$@"); }

# take <type> <resource> <title> <details>: queue, wait for the turn, start. Prints the queue id.
take() {
    local out q
    out=$(hub queue -SessionId "$SID" -Type "$1" -Resource "$2" -Title "$3" -Eta "$4" -Details "$5" 2>&1)
    q=$(echo "$out" | grep -o 'q_[0-9]*_[0-9a-f]*' | head -1)
    if [ -z "$q" ]; then say "queue $2 failed: $(echo "$out" | tr '\n' ' ' | cut -c1-300)"; return 1; fi
    say "queued $2 $q"
    hub wait-turn -SessionId "$SID" -QueueId "$q" -WaitTimeoutSeconds 36000 >> "$LOG" 2>&1 || { say "wait-turn $q rc=$?"; return 1; }
    hub start -SessionId "$SID" -QueueId "$q" >> "$LOG" 2>&1
    say "started $2 $q"
    echo "$q"
}
give() { hub finish -SessionId "$SID" -QueueId "$1" -Result "$2" -Summary "$3" >> "$LOG" 2>&1; say "finished $1 $2"; }

build() {
    local target=$1 n=0 out rc
    while [ $n -lt 20 ]; do
        n=$((n + 1))
        out=$(powershell -NoProfile -ExecutionPolicy Bypass -File devtools/build-session.ps1 -Session bgfx-classic -Target "$target" 2>&1)
        rc=$?
        if echo "$out" | grep -q "owns the lock"; then
            say "build $target attempt $n: storage lock busy"
            sleep 45
            continue
        fi
        echo "$out" | tail -6 >> "$LOG"
        cp -f "$B/_build-session.out.log" "$OUT/build-$target.out.log" 2>/dev/null
        cp -f "$B/_build-session.err.log" "$OUT/build-$target.err.log" 2>/dev/null
        say "build $target rc=$rc"
        return $rc
    done
    say "build $target: lock still busy"
    return 9
}

say "driver start $RUN on $BRANCH $HEAD8; smokes: $SMOKES"
hub heartbeat -SessionId "$SID" -Status active -Eta "120m" \
    -CurrentTask "D-041 parity experiments on $BRANCH $HEAD8: build, pd-tests, then a smoke batch ($RUN)" >> "$LOG" 2>&1

# 1. Build (hub build FIFO plus the wrapper's own queue).
q=$(take build build "BGFX $RUN: client and pd-tests build of $HEAD8" "25m" "week-pd/bgfx-parity-proof in D:\\PD2-wt\\bgfx-classic, session build bgfx-classic") || exit 3
build client; crc=$?
ctrc=1
if [ $crc -eq 0 ]; then build tests; ctrc=$?; fi
if [ $crc -eq 0 ] && [ $ctrc -eq 0 ]; then give "$q" done "BGFX $RUN: client and pd-tests built at $HEAD8"; else give "$q" failed "BGFX $RUN: build failed at $HEAD8 (client rc=$crc, tests rc=$ctrc)"; fi
[ $crc -eq 0 ] || { say "client build failed: stopping"; exit 7; }

# 2. pd-tests (test-runner FIFO), on a pipe.
if [ $ctrc -eq 0 ]; then
    q=$(take test test-runner "BGFX $RUN: pd-tests of $HEAD8" "8m" "session build bgfx-classic, -NoBuild") || exit 4
    powershell -NoProfile -ExecutionPolicy Bypass -File devtools/run-pd-tests.ps1 -Session bgfx-classic -NoBuild 2>&1 | cat > "$OUT/pd-tests.log"
    TR=${PIPESTATUS[0]}
    tail -4 "$OUT/pd-tests.log" >> "$LOG"
    say "pd-tests exit=$TR"
    if [ "$TR" -eq 0 ]; then give "$q" done "BGFX $RUN: pd-tests passed at $HEAD8"; else give "$q" failed "BGFX $RUN: pd-tests exit $TR at $HEAD8"; fi
fi

# 3. Static guards (analysis, no exclusive resource).
python -X utf8 tools/native_write_policy_guard.py --compile-commands "$B/compile_commands.json" > "$OUT/write-guard.json" 2>&1
say "write guard rc=$? $(grep -o '"passed": [a-z]*' "$OUT/write-guard.json" | tail -1)"
python -X utf8 tools/bgfx_dependency_audit.py --compile-commands "$B/compile_commands.json" --exe "$B/PerfectDark.exe" > "$OUT/bgfx-audit.json" 2>&1
say "bgfx audit rc=$? $(grep -o '"passed": [a-z]*' "$OUT/bgfx-audit.json" | tail -1)"

# 4. Freeze the client the smokes run.
have=$(grep -a -o "$BRANCH [0-9a-f]\{8\}" "$B/PerfectDark.exe" | head -1)
if [ "$have" != "$BRANCH $HEAD8" ]; then say "session build is '$have', not $BRANCH $HEAD8: no smoke run"; exit 5; fi
mkdir -p .claude/smoke-ref
cp -f "$B/PerfectDark.exe" .claude/smoke-ref/PerfectDark.exe || exit 6
say "frozen sha256 $(sha256sum .claude/smoke-ref/PerfectDark.exe | cut -c1-16) $have"

# 5. The smoke batch, alone in the game-runtime slot.
BIN='D:\PD2-wt\bgfx-classic\.claude\smoke-ref\PerfectDark.exe'
BASE=$(tr -d '\r\n' < "$SP/base-path.txt")
n_smokes=$(echo $SMOKES | wc -w)
q=$(take smoke game-runtime "BGFX $RUN: $n_smokes renderer smokes on $HEAD8" "$((n_smokes * 6))m" "frozen client of $HEAD8; base $BASE; smokes: $SMOKES") || exit 8
summary=""
fails=0
for name in $SMOKES; do
    start=$(date +%s)
    ls -d .claude/smoke-storage/installs/*/ 2>/dev/null | sort > "$OUT/$name.before"
    powershell -NoProfile -ExecutionPolicy Bypass -File tools/smoke-verify/run.ps1 -Test "$name" \
        -Install "$BASE" -SourceBinary "$BIN" -VerboseAssertions > "$OUT/$name.log" 2>&1
    rc=$?
    [ $rc -eq 0 ] || fails=$((fails + 1))
    summ=$(grep -a -E '^\s*(PASS|FAILED) [0-9]+/[0-9]+ assertions' "$OUT/$name.log" | tail -1 | tr -s ' ' | sed 's/^ //')
    ls -d .claude/smoke-storage/installs/*/ 2>/dev/null | sort > "$OUT/$name.after"
    inst=$(comm -13 "$OUT/$name.before" "$OUT/$name.after" | grep -- "-$name-" | head -1)
    inst=${inst%/}
    if [ -n "$inst" ]; then
        mkdir -p "$EV/$name"
        cp -p "$inst"/parity-*.bmp* "$EV/$name/" 2>/dev/null
        cp -p "$inst/logs/game client/pd-client.log" "$EV/$name/pd-client.log" 2>/dev/null
        grep -a "PARITY: " "$EV/$name/pd-client.log" > "$OUT/$name.parity.txt" 2>/dev/null
    fi
    say "$name rc=$rc $(( $(date +%s) - start ))s :: $summ :: $(cut -c1-200 "$OUT/$name.parity.txt" 2>/dev/null | tr '\n' ';')"
    summary="$summary ${name#renderer_}=${summ:-rc$rc};"
done
result=done
[ $fails -eq 0 ] || result=failed
give "$q" "$result" "$(echo "BGFX $RUN on $HEAD8 (base $BASE):$summary" | cut -c1-1100)"
hub heartbeat -SessionId "$SID" -Status active -Eta "30m" \
    -CurrentTask "D-041 $RUN finished on $HEAD8; reading the parity results" >> "$LOG" 2>&1
say "DRIVER_DONE fails=$fails"
