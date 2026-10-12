#!/usr/bin/env python3
"""Separate original reverb preset parameters from translated native DSP code.

This operates on the local output of reverb_assets.py. It never embeds preset
values: coefficients and delay offsets are emitted only to the private input.
Native instruction/dataflow structure remains executable C with provenance.
"""
import argparse
from pathlib import Path
import re

COEFFICIENT = re.compile(r"(?P<prefix>source_reverb_multiply\(\([^\n]*?\),)(?P<value>-?\d+)(?P<suffix>\))")
DELAY = re.compile(r"(?P<prefix>unsigned at\d+=\(cursor\+)(?P<value>\d+)(?P<suffix>\)&SOURCE_REVERB_MASK;)")


def split(data: bytes, include: str = "generated/source_data/reverb_parameters.inc") -> tuple[bytes, bytes]:
    text = data.decode("utf-8")
    if not re.fullmatch(r"[A-Za-z0-9_./]+", include) or ".." in include.split("/"):
        raise ValueError("Unsafe reverb parameter include")
    values = {"coefficient": [], "delay": []}

    def replace(kind, match):
        value = int(match["value"])
        if (kind == "coefficient" and not -4096 <= value <= 4095) or (kind == "delay" and not 0 <= value <= 32767):
            raise ValueError("Reverb preset value cannot fit the reviewed runtime domain")
        index = len(values[kind])
        values[kind].append(value)
        return match["prefix"] + f"source_reverb_{kind}[{index}]" + match["suffix"]

    coefficient_calls = text.count("source_reverb_multiply(")
    delay_calls = len(re.findall(r"unsigned at\d+=", text))
    text = COEFFICIENT.sub(lambda match: replace("coefficient", match), text)
    text = DELAY.sub(lambda match: replace("delay", match), text)
    if not coefficient_calls or not delay_calls or len(values["coefficient"]) != coefficient_calls or len(values["delay"]) != delay_calls:
        raise ValueError("Unknown generated reverb parameter syntax; review the translator before export")
    private = ["/* Original effect preset parameters, generated locally; not public source. */",
               "#ifndef JGR_GENERATED_REVERB_PARAMETERS", "#define JGR_GENERATED_REVERB_PARAMETERS"]
    for kind in ("coefficient", "delay"):
        private.append(f"static const int source_reverb_{kind}[{len(values[kind])}]={{" +
                       ",".join(map(str, values[kind])) + "};")
    private.append("#endif")
    public = f'#include "{include}"\n' + text
    return public.encode(), ("\n".join(private) + "\n").encode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--code", type=Path, required=True)
    parser.add_argument("--parameters", type=Path, required=True)
    parser.add_argument("--include", default="generated/source_data/reverb_parameters.inc",
                        help="Reviewed relative include used by emitted native code")
    args = parser.parse_args()
    try:
        code, parameters = split(args.input.read_bytes(), args.include)
        for path, data in ((args.code, code), (args.parameters, parameters)):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
    except (OSError, ValueError) as exc:
        parser.exit(1, f"source-data-reverb: {exc}\n")


if __name__ == "__main__":
    main()
