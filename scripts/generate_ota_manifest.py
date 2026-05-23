import argparse
import hashlib
import json
from pathlib import Path


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--firmware", required=True, help="Path to firmware.bin")
    ap.add_argument("--version", required=True, help="Version string (e.g. 2026.05.23-1)")
    ap.add_argument("--url", required=True, help="Public URL to firmware.bin")
    ap.add_argument("--out", required=True, help="Output manifest.json path")
    args = ap.parse_args()

    fw_path = Path(args.firmware)
    if not fw_path.exists():
        raise SystemExit(f"Firmware not found: {fw_path}")

    manifest = {
        "version": args.version,
        "url": args.url,
        "size": fw_path.stat().st_size,
        "sha256": sha256_file(fw_path),
    }

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()

