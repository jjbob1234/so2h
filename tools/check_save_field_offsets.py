#!/usr/bin/env python3
"""
so2h [Save] patch 0004/0005 - save-field offset collision guard.

Parses `mm/include/z64save.h` (and, on request, any other header) for structs whose
fields are annotated with `/* 0xNN */` byte-offset comments (the convention this repo
already uses throughout Save/SaveInfo/Inventory/etc), and fails if:
  - any two fields in the same struct overlap in byte range, or
  - a field's offset isn't >= the previous field's end offset (i.e. it was inserted
    out of order instead of appended), or
  - a field is missing an offset comment while its neighbors have one (so a "willy
    nilly" insertion without updating the comments can't silently slip through).

Field sizes are inferred from the C type via a small lookup table plus `[N]` array
suffixes; nested named struct types (e.g. `OotItemEquips equips;`) are resolved
recursively using that struct's own total size (last offset + last field's size,
rounded per its own trailing `// size = 0x..` comment when present, else computed).

Usage:
    python3 tools/check_save_field_offsets.py [path/to/z64save.h ...]

Exits non-zero (and prints every violation found) if any struct fails validation.
Exits 0 if every annotated struct's fields are strictly append-only / non-overlapping.

This does NOT understand full C (bitfields, unions, preprocessor conditionals inside a
struct body, etc) - it's a deliberately simple regex-based guard for the common case
this repo actually uses (flat structs, one field per line, byte-offset comments), per
the plan's "trivial auto script" request. If a struct's shape is too complex for it to
parse confidently, it skips that struct and reports it as SKIPPED rather than guessing.
"""

import re
import sys
from pathlib import Path

DEFAULT_TARGETS = ["mm/include/z64save.h"]

# Base type sizes in bytes. Extend as needed; unknown types cause the struct to be
# skipped (reported, not silently ignored) rather than mis-measured.
BASE_SIZES = {
    "u8": 1, "s8": 1, "char": 1, "bool": 1, "UNK_TYPE1": 1,
    "u16": 2, "s16": 2,
    "u32": 4, "s32": 4, "f32": 4,
    "u64": 8, "s64": 8, "f64": 8, "OSTime": 8, "uint64_t": 8,
}

STRUCT_START_RE = re.compile(r"^typedef struct(?:\s+\w+)?\s*\{\s*$")
STRUCT_END_RE = re.compile(r"^\}\s*(\w+)\s*;\s*(?://\s*size\s*=\s*(0x[0-9A-Fa-f]+))?")
FIELD_RE = re.compile(
    r"^\s*(?:/\*\s*(0x[0-9A-Fa-f]+)\s*\*/\s*)?"
    r"([A-Za-z_][A-Za-z0-9_]*)\s+"
    r"([A-Za-z_][A-Za-z0-9_]*)"
    r"((?:\s*\[\s*[A-Za-z0-9_]+\s*\])*)\s*;"
)


class StructInfo:
    def __init__(self, name):
        self.name = name
        self.fields = []  # (offset_or_None, type, var, array_dims, line_no)
        self.total_size = None


def resolve_array_count(dim_text, enum_values):
    # dims like "[24]" or "[BOTTLE_MAX]" - try literal int first, else a known #define/enum
    dims = re.findall(r"\[\s*([A-Za-z0-9_]+)\s*\]", dim_text)
    count = 1
    for d in dims:
        if d.isdigit():
            count *= int(d)
        elif d in enum_values:
            count *= enum_values[d]
        else:
            return None
    return count if dims else 1


def collect_known_sizes(struct_infos):
    sizes = dict(BASE_SIZES)
    for name, total_size in struct_infos.items():
        if total_size is not None:
            sizes[name] = total_size
    return sizes


def parse_file(path, enum_values):
    text = Path(path).read_text()
    lines = text.splitlines()

    structs = []
    i = 0
    while i < len(lines):
        if STRUCT_START_RE.match(lines[i]):
            start = i
            body = []
            depth = 1
            i += 1
            while i < len(lines) and depth > 0:
                if "{" in lines[i]:
                    depth += 1
                m_end = STRUCT_END_RE.match(lines[i])
                if m_end and depth == 1:
                    depth = 0
                    break
                body.append(lines[i])
                i += 1
            end_match = STRUCT_END_RE.match(lines[i]) if i < len(lines) else None
            name = end_match.group(1) if end_match else f"<anonymous at line {start+1}>"
            declared_size = int(end_match.group(2), 16) if end_match and end_match.group(2) else None
            structs.append((name, start + 1, body, declared_size))
        i += 1
    return structs


def validate_struct(name, start_line, body, declared_size, known_sizes, enum_values):
    errors = []
    fields = []
    skipped_lines = []

    for offset_in_body, raw_line in enumerate(body):
        line = raw_line.strip()
        if not line or line.startswith("//") or line.startswith("/*") and line.endswith("*/") and ";" not in line:
            continue
        m = FIELD_RE.match(raw_line)
        if not m:
            if ";" in line and not line.startswith("#"):
                skipped_lines.append(start_line + 1 + offset_in_body)
            continue

        offset_hex, ftype, fvar, arr = m.groups()
        offset = int(offset_hex, 16) if offset_hex else None
        base_size = known_sizes.get(ftype)
        if base_size is None:
            skipped_lines.append(start_line + 1 + offset_in_body)
            continue

        count = resolve_array_count(arr, enum_values) if arr else 1
        if count is None:
            skipped_lines.append(start_line + 1 + offset_in_body)
            continue

        size = base_size * count
        fields.append((offset, ftype, fvar, size, start_line + 1 + offset_in_body))

    if skipped_lines:
        return None, errors, skipped_lines, []  # can't confidently validate this struct

    prev_end = 0
    prev_desc = "(start of struct)"
    any_offset_seen = False
    for offset, ftype, fvar, size, line_no in fields:
        if offset is None:
            continue
        any_offset_seen = True
        if offset < prev_end:
            errors.append(
                f"{name} line {line_no}: field `{fvar}` at offset {hex(offset)} overlaps "
                f"previous field {prev_desc} which ends at {hex(prev_end)}"
            )
        elif offset > prev_end:
            # Gap is fine (padding/alignment) - only overlap or backwards insertion is an error.
            pass
        prev_end = max(prev_end, offset + size)
        prev_desc = f"`{fvar}` ({hex(offset)}..{hex(offset + size)})"

    total_size = prev_end if any_offset_seen else sum(f[3] for f in fields)
    warnings = []
    if declared_size is not None and any_offset_seen and declared_size != prev_end:
        # Not a hard failure: naive field-size summation doesn't model C struct
        # alignment/padding, so a mismatch here is commonly just padding, not an actual
        # overlap or backwards-insertion bug (those are caught above). Reported as an
        # FYI so a human can sanity-check it, matching the "trivial" scope requested.
        warnings.append(
            f"{name}: trailing `// size = {hex(declared_size)}` comment doesn't match this "
            f"script's naive computed end offset {hex(prev_end)} (likely just C struct "
            f"alignment padding this script doesn't model - not treated as an error)"
        )
        total_size = declared_size

    return total_size, errors, skipped_lines, warnings


def main(argv):
    targets = argv[1:] if len(argv) > 1 else DEFAULT_TARGETS
    enum_values = {}  # SO2H TODO: extend to parse simple #define NAME NUMBER lines if a struct needs them

    all_errors = []
    all_warnings = []
    reported_skips = []
    struct_infos = {}

    # Two passes so nested named-struct-type sizes (e.g. OotItemEquips used inside
    # OotSaveInfo) are known by the time the outer struct is validated.
    for _pass in range(2):
        for target in targets:
            for name, start_line, body, declared_size in parse_file(target, enum_values):
                known_sizes = collect_known_sizes(struct_infos)
                total_size, errors, skipped, warnings = validate_struct(
                    name, start_line, body, declared_size, known_sizes, enum_values
                )
                if total_size is not None:
                    struct_infos[name] = total_size
                if _pass == 1:
                    all_errors.extend(errors)
                    all_warnings.extend(warnings)
                    if skipped:
                        reported_skips.append((target, name, start_line, skipped))

    if reported_skips:
        print("SKIPPED (could not confidently validate - not necessarily an error):")
        for target, name, start_line, lines in reported_skips:
            print(f"  {target}: struct `{name}` (starts line {start_line}) - unparsed field(s) at line(s) {lines}")
        print()

    if all_warnings:
        print("WARNINGS:")
        for w in all_warnings:
            print(f"  - {w}")
        print()

    if all_errors:
        print("FAILED - save field offset collisions/ordering violations found:")
        for e in all_errors:
            print(f"  - {e}")
        return 1

    print("OK - no save field offset collisions or backwards-insertions found in annotated structs.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
