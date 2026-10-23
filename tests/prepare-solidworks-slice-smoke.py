"""Prepare public, flattened profile fixtures. Never reads account/user data."""
from pathlib import Path
import json
import shutil

root = Path(__file__).resolve().parents[1]
profiles = root / "upstream/OrcaSlicer/resources/profiles/Flashforge"
out = root / "artifacts/solidworks-slice-smoke"
out.mkdir(parents=True, exist_ok=True)
for name in ("plate.3mf", "thumbnail.png"):
    (out / name).unlink(missing_ok=True)
presets = {}
for path in profiles.rglob("*.json"):
    value = json.loads(path.read_text(encoding="utf-8"))
    if "name" in value:
        presets[value["name"]] = value


def flatten(name):
    value = presets[name]
    result = flatten(value["inherits"]) if value.get("inherits") else {}
    result.update(value)
    result["inherits"] = ""
    return result


for filename, name in [
    ("machine.json", "Flashforge Adventurer 5M 0.4 Nozzle"),
    ("process.json", "0.20mm Standard @Flashforge AD5M 0.4 Nozzle"),
    ("filament.json", "Generic PLA @Flashforge"),
]:
    value = flatten(name)
    value["from"] = "user"
    value["version"] = "2.3.0.0"
    (out / filename).write_text(json.dumps(value), encoding="utf-8")
shutil.copyfile(root / "upstream/OrcaSlicer/tests/data/cone_tilted_seam_pcurve.step", out / "model.step")
print(out)
