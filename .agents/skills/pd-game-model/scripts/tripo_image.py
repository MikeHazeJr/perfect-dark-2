"""Generate one image with Tripo's v3 text-to-image API, using the installed
"Tripo 3D" Blender add-on's stored API key (loaded by the add-on's own loader;
the key is never printed or written). Logs the balance before and after and
appends a record of the job to <out_dir>/tripo_image_runs.json.

Run in a background Blender with the add-on enabled in user preferences (so
not --factory-startup):
  blender -b --python tripo_image.py -- <out_dir> <label> <prompt.txt> [model] [size] [quality] [edit task id]
With an edit task id it runs image-to-image on that earlier image instead.
With the label "balance" it only prints the account balance (free).

Defaults: model chat_image_2, size 1536x1024, quality medium (the base rate).
API: https://developers.tripo3d.com/en/docs/generation-text-to-image and
.../generation-image-to-image. Part of the pd-game-model skill (SKILL.md beside
this folder).
"""
import datetime
import json
import os
import sys
import time
import urllib.error
import urllib.request

import bpy

# The add-on's key is accepted on the .ai host (openapi.tripo3d.com answers
# "Invalid API key" for it); override with TRIPO_V3_BASE.
BASE = os.environ.get("TRIPO_V3_BASE", "https://openapi.tripo3d.ai/v3")

argv = sys.argv[sys.argv.index("--") + 1:]
out_dir = os.path.abspath(argv[0])
label = argv[1]
prompt_path = argv[2]
model = argv[3] if len(argv) > 3 else "chat_image_2"
size = argv[4] if len(argv) > 4 else "1536x1024"
quality = argv[5] if len(argv) > 5 else "medium"
# Optional: the task id of an earlier image to edit (image-to-image).
edit_input = argv[6] if len(argv) > 6 else ""
os.makedirs(out_dir, exist_ok=True)

addon = sys.modules.get("tripo-3d-for-blender")
if addon is None:
    raise SystemExit("Tripo 3D add-on is not enabled in this Blender's preferences")
addon.operators.load_api_key_from_local(None)          # the add-on's own key loader
_key = getattr(bpy.context.scene, "api_key", "")
if not _key:
    raise SystemExit("Tripo 3D add-on has no stored API key")


def call(method, path, body=None):
    data = json.dumps(body).encode("utf-8") if body is not None else None
    req = urllib.request.Request(BASE + path, data=data, method=method, headers={
        "Authorization": "Bearer " + _key, "Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as error:
        return {"http_error": error.code,
                "body": error.read().decode("utf-8", "replace")[:600]}
    except urllib.error.URLError as error:
        return {"url_error": str(error.reason)}


def balance():
    result = call("GET", "/account/balance")
    return result.get("data", result)


if label == "balance":          # free check: print the balance and stop
    print("[tripo] balance:", json.dumps(balance()), flush=True)
    raise SystemExit(0)

with open(prompt_path, encoding="utf-8") as f:
    prompt = f.read().strip()
record = {"label": label, "model": model, "size": size, "quality": quality,
          "prompt_file": os.path.abspath(prompt_path),
          "started_utc": datetime.datetime.utcnow().isoformat() + "Z"}
record["balance_before"] = balance()
print("[tripo] balance before:", json.dumps(record["balance_before"]), flush=True)

body = {"model": model, "prompt": prompt, "size": size, "output_format": "png"}
if model.startswith("chat_image"):
    body["quality"] = quality
if edit_input:
    body["input"] = edit_input
    record["edit_input"] = edit_input
created = call("POST", "/generation/image-to-image" if edit_input else "/generation/text-to-image", body)
task_id = (created.get("data") or {}).get("task_id") if isinstance(created, dict) else None
record["create"] = {k: v for k, v in created.items() if k != "data"} if not task_id else {"task_id": task_id}
print("[tripo] create:", json.dumps(record["create"])[:600], flush=True)

result = None
if task_id:
    deadline = time.time() + 15 * 60
    while time.time() < deadline:
        polled = call("GET", "/tasks/" + task_id)
        data = polled.get("data") if isinstance(polled, dict) else None
        if not data:
            print("[tripo] poll:", json.dumps(polled)[:300], flush=True)
            time.sleep(10)
            continue
        status = data.get("status")
        print("[tripo] %s %s%%" % (status, data.get("progress")), flush=True)
        if status in ("success", "failed", "cancelled", "banned", "expired"):
            result = data
            break
        time.sleep(6)

if result:
    record["status"] = result.get("status")
    record["credits_consumed"] = result.get("credits_consumed")
    output = result.get("output") or {}
    url = output.get("generated_image_url") or output.get("generated_image")
    if result.get("status") == "success" and url:
        path = os.path.join(out_dir, label + ".png")
        with urllib.request.urlopen(url, timeout=120) as resp, open(path, "wb") as out:
            out.write(resp.read())
        record["image"] = path
        print("[tripo] saved", path, flush=True)
    else:
        record["output_keys"] = sorted(output.keys())
record["task_id"] = task_id
record["balance_after"] = balance()
print("[tripo] balance after:", json.dumps(record["balance_after"]), flush=True)

runs_path = os.path.join(out_dir, "tripo_image_runs.json")
runs = []
if os.path.exists(runs_path):
    with open(runs_path, encoding="utf-8") as f:
        runs = json.load(f)
runs.append(record)
with open(runs_path, "w", encoding="utf-8") as f:
    json.dump(runs, f, indent=2)
