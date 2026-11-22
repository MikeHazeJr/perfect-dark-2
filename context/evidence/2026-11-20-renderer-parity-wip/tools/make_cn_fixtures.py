"""Write the constant-noise variants of the three renderer parity smokes.
Run from the worktree root: python -X utf8 make_cn_fixtures.py
"""
import json
from pathlib import Path

FLAG = "--debug-renderer-constant-noise"
LINE = "renderer: --debug-renderer-constant-noise: combiner noise held at 0.5 in every backend"
PREFIX = ("2.0 renderer parity experiment (Workbench D-041): the same run as {base} with "
          "--debug-renderer-constant-noise, which gives the combiner's noise term frame count 0 "
          "and noise scale 0 in both backends, so its random() returns exactly 0 (noise input "
          "0.5; the dithered alpha test keeps only opaque texels). Differences that remain do "
          "not come from the noise hash. ")

tests = Path("tools/smoke-verify/tests")
for base in ("renderer_parity_menu_smoke", "renderer_parity_solo_smoke", "renderer_parity_mp_smoke"):
    src = tests / (base + ".json")
    d = json.loads(src.read_text(encoding="utf-8"))
    name = base.replace("_smoke", "_constant_noise_smoke")
    assert d["scenario_name"] == base
    d["scenario_name"] = name
    d["description"] = PREFIX.format(base=base) + d["description"]
    if "constant-noise" not in d["tags"]:
        d["tags"] = d["tags"] + ["constant-noise"]
    assert FLAG not in d["boot_args"]
    d["boot_args"] = d["boot_args"] + [FLAG]
    req = d["assertions"]["required_lines"]
    assert req[0] == "SMOKE: scenario=" + base
    req[0] = "SMOKE: scenario=" + name
    req.insert(2, LINE)
    if "port/fast3d/gfx_pc.cpp" not in d["paths_of_interest"]:
        d["paths_of_interest"] = ["port/fast3d/gfx_pc.cpp", "port/fast3d/gfx_opengl.cpp"] + d["paths_of_interest"]
    # schedule bound (B-1135): timeout must exceed the last at_ms plus every wait timeout
    last = max(s.get("at_ms", 0) for s in d["input_sequence"])
    waits = sum(s.get("timeout_ms", 0) for s in d["input_sequence"] if s.get("type") == "wait_until")
    assert d["timeout_seconds"] * 1000 > last + waits, (name, last, waits)
    out = tests / (name + ".json")
    out.write_text(json.dumps(d, indent=2, ensure_ascii=True) + "\n", encoding="utf-8", newline="\n")
    print("wrote", out, "timeout", d["timeout_seconds"], "last", last, "waits", waits)
