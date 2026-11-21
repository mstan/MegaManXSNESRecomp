#!/usr/bin/env python3
"""Embed the verified Project Zero character-only asset pack in the USA build."""
from hashlib import sha256
from pathlib import Path
import sys

PACK_SHA256 = "e030e255e61108f9236aa3602ea2cb476cd8b373f09d016994b64cbf77688535"

output = Path(sys.argv[1])
source = sys.argv[2] if len(sys.argv) > 2 else ""
data = Path(source).read_bytes() if source else b""
if source and sha256(data).hexdigest() != PACK_SHA256:
    sys.exit("Project Zero asset pack differs from the reviewed v1 pack. Rebuild it with mmx_project_zero_pack.")

output.parent.mkdir(parents=True, exist_ok=True)
with output.open("w", encoding="ascii", newline="\n") as f:
    f.write("#include <stddef.h>\n#include <stdint.h>\n\n")
    f.write("const uint8_t kMmxProjectZeroData[] = {\n")
    for i in range(0, len(data), 32):
        f.write("  " + ", ".join(f"0x{byte:02x}" for byte in data[i:i+32]) + ",\n")
    if not data:
        f.write("  0,\n")
    f.write(f"}};\nconst size_t kMmxProjectZeroDataSize = {len(data)};\n")
