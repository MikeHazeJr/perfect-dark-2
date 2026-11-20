"""Write the classic-GLSL variants of the renderer parity smokes (D-041
experiment 2). Run from the worktree root: python -X utf8 make_glsl_fixtures.py
"""
import json
from pathlib import Path

FLAG = "--debug-bgfx-scene-classic-glsl"
LINE = "SCENARIO\\.RENDER: --debug-bgfx-scene-classic-glsl: bgfx scene program from the GL path's GLSL text"
BAD = "SCENARIO\\.RENDER: --debug-bgfx-scene-classic-glsl: (GL scene shader source unavailable|attribute|uniform declarations|program not created)"
PREFIX = ("2.0 renderer parity experiment (Workbench D-041): the same run as {base} with "
          "--debug-bgfx-scene-classic-glsl, which builds the bgfx scene program from the GL "
          "path's own GLSL 1.30 text (attribute names and vec4 uniform widths changed for "
          "bgfx, the arithmetic character for character). Differences that remain in the "
          "scene's surfaces do not come from the shader text. ")

tests = Path("tools/smoke-verify/tests")
for base in ("renderer_parity_solo_smoke", "renderer_parity_mp_smoke", "renderer_parity_menu_smoke"):
    src = tests / (base + ".json")
    d = json.loads(src.read_text(encoding="utf-8"))
    name = base.replace("_smoke", "_classic_glsl_smoke")
    assert d["scenario_name"] == base
    d["scenario_name"] = name
    d["description"] = PREFIX.format(base=base) + d["description"]
    if "classic-glsl" not in d["tags"]:
        d["tags"] = d["tags"] + ["classic-glsl"]
    assert FLAG not in d["boot_args"]
    d["boot_args"] = d["boot_args"] + [FLAG]
    req = d["assertions"]["required_lines"]
    assert req[0] == "SMOKE: scenario=" + base
    req[0] = "SMOKE: scenario=" + name
    req.insert(2, LINE)
    d["assertions"]["forbidden_patterns"] = d["assertions"]["forbidden_patterns"] + [BAD]
    for p in ("port/fast3d/scenario_scene_renderer.cpp", "port/fast3d/scenario_scene_renderer_bgfx.cpp"):
        if p not in d["paths_of_interest"]:
            d["paths_of_interest"] = [p] + d["paths_of_interest"]
    last = max(s.get("at_ms", 0) for s in d["input_sequence"])
    waits = sum(s.get("timeout_ms", 0) for s in d["input_sequence"] if s.get("type") == "wait_until")
    assert d["timeout_seconds"] * 1000 > last + waits, (name, last, waits)
    out = tests / (name + ".json")
    out.write_text(json.dumps(d, indent=2, ensure_ascii=True) + "\n", encoding="utf-8", newline="\n")
    print("wrote", out, "timeout", d["timeout_seconds"], "last", last, "waits", waits)
