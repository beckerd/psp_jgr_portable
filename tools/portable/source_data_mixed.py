#!/usr/bin/env python3
"""Recover reviewed C declarations from a supported user-disc executable.

Recipes contain addresses, storage formats and C syntax with placeholders.
They contain no original initializer payloads. Every generated declaration
must match its reviewed SHA-256 before any output is written.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct

from source_data_tables import EXECUTABLE_SHA256, IMAGE_BASE

RECIPES = Path(__file__).with_name("source_data_mixed.json")


def read(image: bytes, address: int, kind: str, base=IMAGE_BASE):
    if kind not in ("b", "B", "h", "H", "i", "I", "f"):
        raise ValueError("Unsupported original storage format")
    offset = address - base
    if offset < 0 or offset + struct.calcsize(kind) > len(image):
        raise ValueError("Original table address is outside the supplied executable")
    value = struct.unpack_from("<" + kind, image, offset)[0]
    if isinstance(value, float) and not math.isfinite(value):
        raise ValueError("Non-finite original table value")
    return value


def literal(value, spec):
    suffix = spec.get("suffix", "")
    if suffix not in ("", "f", "u", "U", "F"):
        raise ValueError("Invalid C literal suffix")
    if spec["style"] == "integer":
        if value != int(value):
            raise ValueError("Fractional value in integer declaration")
        text = str(int(value))
    elif spec["style"] == "hex":
        if value != int(value) or value < 0:
            raise ValueError("Invalid hexadecimal value")
        text = "0x" + format(int(value), "0" + str(spec.get("width", 1)) + "x")
    elif spec["style"] == "decimal":
        precision = spec["precision"]
        if not isinstance(precision, int) or not 0 <= precision <= 24:
            raise ValueError("Invalid decimal precision")
        text = format(value, "." + str(precision) + "f")
        if precision == 0:
            text += "."
        if spec.get("omit_zero"):
            text = re.sub(r"^(-?)0\.", r"\1.", text)
    elif spec["style"] == "hex_escape":
        if value != int(value) or not 0 <= value <= 255:
            raise ValueError("Invalid string byte")
        text = "\\x" + format(int(value), "02x")
    else:
        raise ValueError("Unknown C literal format")
    return text + suffix


def raw_values(image, recipe):
    source = recipe["source"]
    mode = source.get("mode", "contiguous")
    kind = source["format"]
    base = int(source["base"], 0) if "base" in source else IMAGE_BASE
    view = lambda address, storage: read(image, address, storage, base)
    if mode == "contiguous":
        address = int(source["address"], 0)
        stride = source.get("stride", struct.calcsize(kind))
        return [view(address + i * stride, kind) for i in range(len(recipe["formats"]))]
    if mode == "addresses":
        return [view(int(address, 0), kind) for address in source["addresses"]]
    if mode == "pointer_field":
        table = int(source["table"], 0)
        return [view(view(table + i * 4, "I") + source["offset"], kind)
                for i in range(len(recipe["formats"]))]
    if mode == "mixed_record":
        address = int(source["address"], 0)
        return [view(address + i * 4, storage)
                for i, storage in enumerate(source["storage"])]
    if mode == "formation_rows":
        table = int(source["table"], 0)
        # Original pointer rows include an extra creator position after the
        # member vectors. The PSP array's unlisted members are C zero-fill.
        return [view(view(table + row * 4, "I") + i * 4, kind)
                for row in range(source["rows"]) for i in range((row + 1) * 3)]
    if mode == "fields":
        result = []
        for field in source["fields"]:
            if field.get("unused_slot_zero"):
                # Explicit C padding for a port-side slot rejected before use.
                # This is never a fallback for a missing source value.
                result.append(0)
                continue
            value = view(int(field["address"], 0), field.get("format", kind))
            if field.get("unsigned32"):
                value = int(value) & 0xffffffff
            result.append(value)
        return result
    raise ValueError("Unknown original table recipe")


def values(image, recipe):
    result = raw_values(image, recipe)
    source = recipe["source"]
    if source.get("sort"):
        result = sorted(result)
    for index in source.get("negate_indices", []):
        if not isinstance(index, int) or not 0 <= index < len(result):
            raise ValueError("Invalid source coordinate negation")
        result[index] = -result[index]
    if source.get("swap_red_blue"):
        result = [(int(value) & 0xff00ff00) | ((int(value) >> 16) & 255) |
                  ((int(value) & 255) << 16) for value in result]
    return result


def bgm_text(image, recipe):
    source = recipe["source"]
    address = int(source["address"], 0)
    if source["mode"] == "bgm_names":
        names = []
        for index in range(source["count"]):
            offset = address - IMAGE_BASE + index * source["stride"]
            end = offset + source["width"]
            if offset < 0 or end > len(image):
                raise ValueError("BGM name is outside executable")
            raw = image[offset:end].split(b"\0", 1)[0]
            if not raw.endswith(b".ADX") or not re.fullmatch(rb"[A-Z0-9_]+\.ADX", raw):
                raise ValueError("Unexpected BGM filename")
            names.append('"' + raw[:-4].decode("ascii") + '"')
        return recipe["prefix"] + ",".join(names) + recipe["suffix"]
    rows = []
    for index in range(source["count"]):
        node = read(image, address + index * 12, "I")
        kind = read(image, address + index * 12 + 4, "I")
        if read(image, address + index * 12 + 8, "I") or kind > 5:
            raise ValueError("Unexpected BGM entry")
        seen, sequence = {}, []
        while node and node not in seen:
            if len(sequence) >= 32:
                raise ValueError("BGM sequence exceeds runtime capacity")
            seen[node] = len(sequence)
            value = read(image, node, "I")
            if value >= source["names"]:
                raise ValueError("Invalid BGM filename index")
            sequence.append(value)
            node = read(image, node + 4, "I")
        if not sequence:
            raise ValueError("Empty BGM sequence")
        # Runtime ABI zero-fills the unused sequence capacity.
        padded = sequence + [0] * (32 - len(sequence))
        rows.append("  {%d, %d, %d, {%s}}," %
                    (kind, len(sequence), seen.get(node, -1), ",".join(map(str, padded))))
    return recipe["prefix"] + "\n".join(rows) + recipe["suffix"]


def extract(image: bytes, recipes: dict | None = None, *, expected_sha256=EXECUTABLE_SHA256, banks=None):
    if hashlib.sha256(image).hexdigest() != expected_sha256:
        raise ValueError("Unsupported executable identity")
    recipes = recipes if recipes is not None else json.loads(RECIPES.read_text())
    if recipes.get("schema") != 1:
        raise ValueError("Unsupported mixed-data recipe schema")
    output = {}
    for recipe in recipes["declarations"]:
        name = recipe["output"]
        if not re.fullmatch(r"[A-Za-z0-9_]+\.inc", name) or name in output:
            raise ValueError("Unsafe or duplicate generated declaration")
        data_image = image
        source = recipe["source"]
        if "file" in source:
            file = source["file"]
            if not re.fullmatch(r"[A-Z0-9_]+/[A-Za-z0-9_]+\.bin", file):
                raise ValueError("Unsafe original bank path")
            if banks is None or file not in banks:
                raise ValueError("Missing original bank: " + file)
            data_image = banks[file]
            if hashlib.sha256(data_image).hexdigest() != source["sha256"]:
                raise ValueError("Unsupported original bank identity: " + file)
        if recipe["source"].get("mode") in ("bgm_names", "bgm_entries"):
            text = bgm_text(image, recipe)
        else:
            segments = recipe["syntax"].split("@@")
            items = values(data_image, recipe)
            if len(items) != len(recipe["formats"]) or len(segments) != len(items) + 1:
                raise ValueError("Declaration placeholder count does not match source values")
            text = segments[0]
            for value, spec, tail in zip(items, recipe["formats"], segments[1:]):
                text += literal(value, spec) + tail
        data = text.encode("ascii")
        if hashlib.sha256(data).hexdigest() != recipe["sha256"]:
            raise ValueError("Generated declaration does not match review: " + name)
        output[name] = data
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--afs", type=Path, help="Extracted verified original AFS banks")
    args = parser.parse_args()
    try:
        recipes = json.loads(RECIPES.read_text())
        banks = {}
        for recipe in recipes["declarations"]:
            file = recipe["source"].get("file")
            if file:
                if not args.afs or not re.fullmatch(r"[A-Z0-9_]+/[A-Za-z0-9_]+\.bin", file):
                    raise ValueError("--afs is required for original bank data")
                banks[file] = (args.afs / file).read_bytes()
        declarations = extract(args.executable.read_bytes(), recipes, banks=banks)
        args.out.mkdir(parents=True, exist_ok=True)
        for name, data in declarations.items():
            (args.out / name).write_bytes(data)
        print(f"Recovered {len(declarations)} exact mixed-source declarations from user executable")
    except (OSError, ValueError, KeyError, struct.error) as exc:
        parser.exit(1, f"source-data-mixed: {exc}\n")


if __name__ == "__main__":
    main()
