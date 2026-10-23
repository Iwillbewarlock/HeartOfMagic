"""Read-only MO2/EFSH/DDS evidence audit. Writes JSON to stdout; never extracts assets.

Usage: python corpse-assets-audit.py <MO2 instance> <profile name> <BF request folder> [BF log]
Requires Pillow, numpy and lz4 (DDS and Skyrim SE BSA decompression).
Modlist order: first enabled entry wins loose conflicts; archives.txt is load order.
"""
import io
import json
import re
import struct
import sys
import zlib
from pathlib import Path

import lz4.frame
import numpy as np
from PIL import Image


def u32(data, offset=0):
    return struct.unpack_from("<I", data, offset)[0]


def subrecords(data):
    offset = 0
    extended = None
    while offset + 6 <= len(data):
        name = data[offset:offset + 4].decode("ascii")
        size = struct.unpack_from("<H", data, offset + 4)[0]
        offset += 6
        if name == "XXXX":
            extended = u32(data, offset)
            offset += size
            continue
        size = extended if extended is not None else size
        extended = None
        yield name, data[offset:offset + size]
        offset += size
    assert offset == len(data)


def shader_records(path, wanted):
    with path.open("rb") as stream:
        header = stream.read(24)
        if header[:4] != b"TES4":
            return []
        masters = [v.rstrip(b"\0").decode().lower()
                   for k, v in subrecords(stream.read(u32(header, 4))) if k == "MAST"]
        masters.append(path.name.lower())
        if not any(name in masters for name, _ in wanted):
            return []
        result = []
        while header := stream.read(24):
            size = u32(header, 4)
            if header[:4] == b"GRUP":
                if header[8:12] != b"EFSH":
                    stream.seek(size - 24, 1)
                continue
            ident = u32(header, 12)
            owner = masters[ident >> 24] if ident >> 24 < len(masters) else "?"
            if header[:4] != b"EFSH" or (owner, ident & 0xFFFFFF) not in wanted:
                stream.seek(size, 1)
                continue
            body = stream.read(size)
            if u32(header, 8) & 0x40000:
                body = zlib.decompress(body[4:])
            fields = dict(subrecords(body))
            data = fields["DATA"]
            # Disk DATA has 32-bit form references; runtime pointers/padding add 16 bytes.
            result.append({
                "owner": owner, "id": f"{ident & 0xFFFFFF:06X}", "plugin": str(path),
                "edid": fields.get("EDID", b"").rstrip(b"\0").decode(),
                "textures": {k: fields.get(k, b"").rstrip(b"\0").decode()
                             for k in ("ICON", "ICO2", "NAM7", "NAM8", "NAM9")},
                "particle_model": fields.get("MODL", b"").rstrip(b"\0").decode(),
                "addon_form": f"{u32(data, 0xF4):08X}",
                "flags": f"{u32(data, 0x180):08X}",
                "colours": {k: list(data[o:o + 3]) for k, o in {
                    "fill1": 0x10, "edge": 0x38, "particle1": 0xBC,
                    "particle2": 0xC0, "particle3": 0xC4, "edge2": 0x10C,
                    "fill2": 0x138, "fill3": 0x13C}.items()},
            })
        return result


def bsa_entries(path, wanted):
    with path.open("rb") as stream:
        header = stream.read(36)
        if header[:4] != b"BSA\0":
            raise ValueError(f"Not BSA: {path}")
        version, offset, flags, folders, files, folder_bytes, file_bytes, _ = struct.unpack_from("<8I", header, 4)
        if version not in (103, 104, 105) or flags & 3 != 3:
            raise ValueError(f"Unsupported BSA header: {path}, {version}, {flags}")
        width = 24 if version == 105 else 16
        stream.seek(offset)
        folder_table = stream.read(folders * width)
        entries = []
        for index in range(folders):
            count = u32(folder_table, index * width + 8)
            length = stream.read(1)[0]
            folder = stream.read(length).rstrip(b"\0").decode("latin1").lower()
            records = stream.read(16 * count)
            entries.extend((folder, u32(records, i * 16 + 8), u32(records, i * 16 + 12))
                           for i in range(count))
        names = stream.read(file_bytes).split(b"\0")
        assert len(entries) == files and len(names) >= files, str(path)
        for (folder, size, start), name in zip(entries, names):
            asset = folder + "\\" + name.decode("latin1").lower()
            if asset in wanted:
                yield asset, {"archive": str(path), "offset": start, "size": size,
                              "version": version, "flags": flags}


def read_asset(source):
    if "loose" in source:
        return Path(source["loose"]).read_bytes()
    with Path(source["archive"]).open("rb") as stream:
        stream.seek(source["offset"])
        data = stream.read(source["size"] & 0x3FFFFFFF)
    if source["flags"] & 0x100:
        data = data[data[0] + 1:]
    compressed = bool(source["flags"] & 4) != bool(source["size"] & 0x40000000)
    if compressed:
        size = u32(data)
        data = (lz4.frame.decompress(data[4:])
                if source["version"] == 105 else zlib.decompress(data[4:]))
        assert len(data) == size
    return data


def texture_stats(raw):
    image = Image.open(io.BytesIO(raw)).convert("RGBA")
    rgba = np.asarray(image, dtype=np.float64).reshape(-1, 4) / 255.0
    rgb, alpha = rgba[:, :3], rgba[:, 3]
    maximum, minimum = rgb.max(1), rgb.min(1)
    chroma = maximum - minimum
    saturation = np.divide(chroma, maximum, out=np.zeros_like(chroma), where=maximum > 0)
    hue = np.zeros_like(chroma)
    for channel, step in ((0, 0), (1, 2), (2, 4)):
        mask = (rgb.argmax(1) == channel) & (chroma > 0)
        hue[mask] = ((rgb[mask, (channel + 1) % 3] - rgb[mask, (channel + 2) % 3]) / chroma[mask] + step) / 6 % 1
    lit = (maximum > 0.1) & (alpha > 0)
    colourful = (saturation >= 0.45) & (maximum >= 0.30)
    orange = (hue >= 15 / 360) & (hue <= 55 / 360)
    red = ((hue < 15 / 360) | (hue >= 345 / 360)) & (maximum >= 0.55)
    fire = lit & colourful & (orange | red)
    palette_fire = (maximum > 0.1) & colourful & ((hue <= 55 / 360) | (hue >= 345 / 360))
    return {"size": list(image.size), "rgb_equal_all": bool(np.all(chroma == 0)),
            "max_rgb_spread": round(float(chroma.max() * 255), 3),
            "base_fire_share": round(float(alpha[fire].sum() / alpha[lit].sum()), 8) if lit.any() else 0,
            "palette_fire_share": round(float(palette_fire.sum() / (maximum > 0.1).sum()), 8) if (maximum > 0.1).any() else 0,
            "visible_rgb_mean": [round(float(v), 4) for v in np.average(rgb, axis=0, weights=alpha)]}


def main():
    root, profile, request_dir = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
    settings = root / "profiles" / profile
    enabled = [line[1:] for line in (settings / "modlist.txt").read_text(encoding="utf-8-sig").splitlines() if line.startswith("+")]
    roots = [root / "overwrite"] + [root / "mods" / name for name in enabled] + [root / "Stock Game" / "Data"]
    top_files = {}
    for folder in roots:
        if folder.is_dir():
            for path in folder.iterdir():
                if path.is_file():
                    top_files.setdefault(path.name.lower(), path)
    wanted = {("fec.esp", value) for value in (0x801, 0x819, 0x823, 0x896, 0x826, 0x8E2, 0x800)} | {("skyrim.esm", 0x1B212)}
    records, winners = [], {}
    active_plugins = [line.lstrip("*").lower() for line in (settings / "plugins.txt").read_text(encoding="utf-8-sig").splitlines() if line.startswith("*")]
    for name in active_plugins:
        if name in top_files:
            for record in shader_records(top_files[name], wanted):
                records.append(record)
                winners[(record["owner"], record["id"])] = record
    assets = {("textures\\" + path).lower() for rec in winners.values() for path in rec["textures"].values() if path}
    candidates = {asset: [] for asset in assets}
    for folder in roots:
        for asset in assets:
            path = folder / asset
            if path.is_file():
                candidates[asset].append({"loose": str(path)})
    archives = [line.lower() for line in (settings / "archives.txt").read_text(encoding="utf-8-sig").splitlines() if line and not line.startswith("#")]
    errors, scanned = [], 0
    for name in reversed(archives):
        if name not in top_files:
            errors.append("archive not found: " + name)
            continue
        try:
            for asset, source in bsa_entries(top_files[name], assets):
                candidates[asset].append(source)
            scanned += 1
        except Exception as exc:
            errors.append(str(exc))
    resolved = {}
    request = json.loads((request_dir / "request.json").read_text(encoding="utf-8-sig"))
    base_requests = {row["path"]: row for row in request["efshBases"]}
    not_fire = (request_dir / "staging" / "efshbase-notfire.txt").read_text().splitlines()
    for asset, sources in sorted(candidates.items()):
        row = {"candidates_priority_order": sources}
        if asset in base_requests:
            row["base_request"] = base_requests[asset]
            key = base_requests[asset].get("key", "")
            row["not_fire_cache"] = [line for line in not_fire if key and line.startswith(key + " ")]
        if sources:
            try:
                raw = read_asset(sources[0])
                row["stats"] = texture_stats(raw)
                dumped = request_dir / "data" / asset
                row["game_dump"] = "same" if dumped.is_file() and dumped.read_bytes() == raw else "different" if dumped.is_file() else "not dumped"
            except Exception as exc:
                row["read_error"] = str(exc)
        resolved[asset] = row
    copies = {}
    if len(sys.argv) > 4:
        log = Path(sys.argv[4]).read_text(encoding="utf-8-sig")
        corpse_lines = "\n".join(line for line in log.splitlines() if "corpse attach" in line)
        for key, name in sorted(set(re.findall(r"copies\\([0-9a-f]{16})\\(palette\.dds|copy\.dds)", corpse_lines))):
            path = root / "overwrite" / "textures" / "heartofmagic" / "blackflame" / "copies" / key / name
            data = path.read_bytes()
            assert data[84:88] == bytes(4) and u32(data, 88) == 32
            assert struct.unpack_from("<4I", data, 92) == (0xFF, 0xFF00, 0xFF0000, 0xFF000000)
            pixels = np.frombuffer(data[128:], dtype=np.uint8).reshape(-1, 4)
            copies[key + "/" + name] = {"mips": u32(data, 28), "texels": len(pixels),
                "rgb_equal_all_mips": bool(np.all((pixels[:, 0] == pixels[:, 1]) & (pixels[:, 0] == pixels[:, 2])))}
    print(json.dumps({"profile": profile, "enabled_mods": len(enabled), "active_plugins": len(active_plugins),
                      "archives_scanned": scanned, "archive_errors": errors, "records_override_order": records,
                      "assets": resolved, "corpse_copies": copies}, indent=2))


if __name__ == "__main__":
    main()
