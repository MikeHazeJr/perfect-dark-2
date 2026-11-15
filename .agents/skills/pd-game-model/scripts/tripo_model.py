"""Tripo 3D model jobs for the pd-game-model skill (see SKILL.md beside this folder).

Runs text-, image- and multiview-to-model jobs through the installed "Tripo 3D"
Blender add-on (0.7.7) and its bundled SDK, using the add-on's own stored API key
(loaded by the add-on's loader; never printed or written), and downloads the
results. Generalised from the crystal pistol asset session's tripo_generate.py.

Run in a background Blender with the add-on enabled (not --factory-startup):
  blender -b --python tripo_model.py -- <out_dir> [options] <job> [<job> ...]

Jobs:
  <label>=image:<png>                 image_to_model on one image
  <label>=multiview:<front>,<left>,<back>,<right>   multiview_to_model (v2.x only;
                                      leave a slot empty with "-")
  <label>=text:<prompt.txt>           text_to_model
  <label>=task:<task id>              wait for / download an earlier task (free)
With no jobs it only prints the balance.

Options (before the jobs):
  --face-limit=N        cap on faces (set from the SKILL.md budget table)
  --smart-low-poly      clean low-poly topology
  --parts               generate_parts (separate parts, for rigged pieces)
  --version=V           model version (default v3.0-20250812; multiview uses v2.5-20250123)
  --no-texture          geometry only (cheaper)

Each job gets its own client: the SDK's fallback HTTP client (Blender's Python has no
aiohttp) holds one connection and cannot serve concurrent requests. The SDK's
get_task() imports tripo3d.geo_utils, which needs aiohttp, so a process-local stub
stands in for it (it only decides a China-region header). Every job (task id,
settings, files, balance before and after) is appended to <out_dir>/tripo_runs.json.
"""
import asyncio
import datetime
import importlib.util
import json
import os
import shutil
import sys
import time
import types

import bpy

argv = sys.argv[sys.argv.index("--") + 1:]
out_dir = os.path.abspath(argv[0])
options = [a for a in argv[1:] if a.startswith("--")]
jobs = [a.split("=", 1) for a in argv[1:] if not a.startswith("--")]
os.makedirs(out_dir, exist_ok=True)

settings = {"texture": True, "pbr": True}
version = "v3.0-20250812"
for opt in options:
    if opt.startswith("--face-limit="):
        settings["face_limit"] = int(opt.split("=", 1)[1])
    elif opt == "--smart-low-poly":
        settings["smart_low_poly"] = True
    elif opt == "--parts":
        settings["generate_parts"] = True
    elif opt.startswith("--version="):
        version = opt.split("=", 1)[1]
    elif opt == "--no-texture":
        settings["texture"] = False
        settings["pbr"] = False
    else:
        raise SystemExit("unknown option " + opt)

for label, spec in jobs:
    kind, _, value = spec.partition(":")
    if kind == "image" and not os.path.isfile(value):
        raise SystemExit(f"missing image for {label}: {value}")
    if kind == "multiview":
        for path in value.split(","):
            if path != "-" and not os.path.isfile(path):
                raise SystemExit(f"missing view for {label}: {path}")
    if kind == "text" and not os.path.isfile(value):
        raise SystemExit(f"missing prompt for {label}: {value}")
    if kind not in ("image", "multiview", "text", "task"):
        raise SystemExit(f"unknown job kind for {label}: {kind}")

addon = sys.modules.get("tripo-3d-for-blender")
if addon is None:
    raise SystemExit("Tripo 3D add-on is not enabled in this Blender's preferences")
addon.operators.load_api_key_from_local(None)          # the add-on's own key loader
_key = getattr(bpy.context.scene, "api_key", "")
if not _key:
    raise SystemExit("Tripo 3D add-on has no stored API key")
TripoClient = addon.tripo3d.TripoClient

if importlib.util.find_spec("aiohttp") is None:
    _stub = types.ModuleType(addon.__name__ + ".tripo3d.geo_utils")
    _stub.get_china_mainland_status = lambda: None
    _stub.set_china_mainland_status = lambda status: None
    _stub.detect_china_mainland_sync = lambda timeout=3.0: None
    sys.modules.setdefault(_stub.__name__, _stub)


async def balance():
    async with TripoClient(api_key=_key) as client:
        return await client.get_balance()


async def one(label, spec):
    kind, _, value = spec.partition(":")
    started = time.time()
    rec = {"label": label, "kind": kind, "input": value, "version": version,
           "settings": settings, "submitted_utc": datetime.datetime.utcnow().isoformat() + "Z"}
    try:
        async with TripoClient(api_key=_key) as client:
            if kind == "task":
                tid = value
            elif kind == "image":
                tid = await client.image_to_model(image=value, model_version=version, **settings)
            elif kind == "multiview":
                views = [None if p == "-" else p for p in value.split(",")]
                mv_version = version if version.startswith("v2") else "v2.5-20250123"
                rec["version"] = mv_version
                tid = await client.multiview_to_model(images=views, model_version=mv_version, **settings)
            else:
                with open(value, encoding="utf-8") as f:
                    prompt = f.read().strip()
                tid = await client.text_to_model(prompt=prompt, model_version=version, **settings)
            rec["task_id"] = tid
            print(f"[tripo] {label}: task {tid}", flush=True)
            task = await client.wait_for_task(tid, polling_interval=3.0, timeout=1200)
            rec["status"] = str(getattr(task.status, "value", task.status))
            rec["seconds"] = round(time.time() - started, 1)
            print(f"[tripo] {label}: {rec['status']} after {rec['seconds']}s", flush=True)
            if rec["status"] != "success":
                rec["error"] = {"code": getattr(task, "error_code", None),
                                "msg": getattr(task, "error_msg", None)}
                return rec
            files = await client.download_task_models(task, out_dir)
            rec["files"] = {}
            for name, path in files.items():
                if path:
                    dst = os.path.join(out_dir, f"{label}_{name}{os.path.splitext(path)[1]}")
                    shutil.move(path, dst)
                    rec["files"][name] = os.path.basename(dst)
            preview = await client.download_rendered_image(task, out_dir,
                                                           filename=f"{label}_tripo_preview.webp")
            if preview:
                rec["files"]["tripo_preview"] = os.path.basename(preview)
    except Exception as error:                     # record it and carry on with other jobs
        rec["status"] = "exception"
        rec["error"] = repr(error)[:500]
        print(f"[tripo] {label}: exception {error!r}"[:600], flush=True)
    return rec


async def main():
    before = await balance()
    print(f"[tripo] balance before: {before.balance} (frozen {before.frozen})", flush=True)
    if not jobs:
        return
    recs = await asyncio.gather(*(one(label, spec) for label, spec in jobs))
    after = await balance()
    print(f"[tripo] balance after: {after.balance} (frozen {after.frozen})", flush=True)
    run = {"run_utc": datetime.datetime.utcnow().isoformat() + "Z",
           "addon": "Tripo 3D 0.7.7 (bundled tripo3d SDK)", "api": TripoClient.BASE_URL,
           "balance_before": before.balance, "balance_after": after.balance,
           "frozen_after": after.frozen, "jobs": recs}
    log = os.path.join(out_dir, "tripo_runs.json")
    runs = []
    if os.path.exists(log):
        with open(log, encoding="utf-8") as f:
            runs = json.load(f)
    runs.append(run)
    with open(log, "w", encoding="utf-8") as f:
        json.dump(runs, f, indent=2)
    print("[tripo] RESULT " + json.dumps(run)[:2000], flush=True)


asyncio.run(main())
