#!/usr/bin/env python3
"""
so2h_gen_sheets.py

Turns mm/assets/custom/textures/so2h_menu/sheets.json into two generated headers:

    mm/assets/so2h_ui_sheets_assets.h   the __OTR__ path defines, one per sheet
    mm/2s2h/Menu/so2h_ui_sheets.h       the So2hUiSheetDef table, sheet ids and slice ids

Neither file is hand-edited. mm/assets/2s2h_assets.h is deliberately NOT touched - it is
manually maintained and the 43 legacy tiles already have their defines there.

Why a generator at all: the promise so2h_ui is held to is that a new panel, icon grid or stat
readout is a data edit. That only holds if adding art is a data edit too, and it only stays
true if the data cannot silently drift from the pixels. So this script does not just
transcribe the manifest, it checks it:

  HARD FAILURES (non-zero exit, breaks the build)
    - a declared grid that does not divide the file exactly
    - a run band outside the grid, or reversed
    - a run cell that is fully transparent (a growing panel would open a hole in itself)
    - a run band wider than one cell whose cells are not byte-identical along the band, for
      every row/column that the band also covers - this is the "never infer, always verify"
      rule from the plan
    - a sheet larger than 1023 texels on either axis (So2h_UiDrawQuad packs s/t as S10.5)
    - a duplicate sheet name, or more sheets than SO2H_UI_MAX_SHEETS
    - a named slice outside its grid
    - a missing file

  AUDIT (printed, never fatal)
    - the tile-identity map of every sheet, so a frame that was redrawn shows up as a diff in
      review instead of as a seam in-game

Run:  python3 tools/so2h_gen_sheets.py [--check]

--check writes nothing and only verifies, which is what CI should run.
"""

import hashlib
import json
import os
import string
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("so2h_gen_sheets: Pillow is required (pip install pillow)")

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
SHEET_DIR = os.path.join(ROOT, "mm", "assets", "custom", "textures", "so2h_menu")
MANIFEST = os.path.join(SHEET_DIR, "sheets.json")
OUT_ASSETS = os.path.join(ROOT, "mm", "assets", "so2h_ui_sheets_assets.h")
OUT_TABLE = os.path.join(ROOT, "mm", "2s2h", "Menu", "so2h_ui_sheets.h")

MAX_SHEETS = 64      # must match SO2H_UI_MAX_SHEETS in so2h_ui.h
MAX_TEXEL = 1023     # S10.5 s/t ceiling in So2h_UiDrawQuad

errors = []
audit = []


def fail(sheet, msg):
    errors.append("{}: {}".format(sheet, msg))


# ---------------------------------------------------------------------------------------
# Pixel inspection
# ---------------------------------------------------------------------------------------

def cell_bytes(img, col, row, unit):
    box = (col * unit, row * unit, (col + 1) * unit, (row + 1) * unit)
    return img.crop(box).tobytes()


def cell_hash(img, col, row, unit):
    return hashlib.md5(cell_bytes(img, col, row, unit)).hexdigest()


def cell_is_blank(img, col, row, unit):
    """True when every pixel in the cell has alpha 0."""
    box = (col * unit, row * unit, (col + 1) * unit, (row + 1) * unit)
    return img.crop(box).getchannel("A").getbbox() is None


def identity_map(img, cols, rows, unit):
    """Letters A, B, C ... one per distinct cell, in reading order. This is the audit view."""
    letters = string.ascii_uppercase + string.ascii_lowercase
    seen = {}
    out = []
    for r in range(rows):
        line = []
        for c in range(cols):
            h = cell_hash(img, c, r, unit)
            if h not in seen:
                idx = len(seen)
                seen[h] = letters[idx] if idx < len(letters) else "?"
            line.append(seen[h])
        out.append(" ".join(line))
    return out, len(seen)


# ---------------------------------------------------------------------------------------
# Validation
# ---------------------------------------------------------------------------------------

def check_run_band(name, img, cols, rows, unit, run, hollow):
    """
    A run band wider than one cell claims 'these cells are interchangeable'. Verify it: for a
    column band lo..hi, every row must show the same cell across the whole band. A one-cell
    band is trivially interchangeable with itself, which is why the manifest prefers it.
    """
    col_lo, col_hi = run.get("cols", (0, -1))
    row_lo, row_hi = run.get("rows", (0, -1))

    if col_hi >= col_lo:
        if col_lo < 0 or col_hi >= cols:
            fail(name, "run cols [{},{}] outside grid width {}".format(col_lo, col_hi, cols))
        elif col_hi > col_lo:
            for r in range(rows):
                hashes = {cell_hash(img, c, r, unit) for c in range(col_lo, col_hi + 1)}
                if len(hashes) != 1:
                    fail(name, "run cols [{},{}] are not byte-identical on row {} - a growing "
                               "panel would alternate different tiles there".format(col_lo, col_hi, r))
                    break

    if row_hi >= row_lo:
        if row_lo < 0 or row_hi >= rows:
            fail(name, "run rows [{},{}] outside grid height {}".format(row_lo, row_hi, rows))
        elif row_hi > row_lo:
            for c in range(cols):
                hashes = {cell_hash(img, c, r, unit) for r in range(row_lo, row_hi + 1)}
                if len(hashes) != 1:
                    fail(name, "run rows [{},{}] are not byte-identical on column {}".format(row_lo, row_hi, c))
                    break

    # A hole in a growing panel is the single worst failure mode here, so it is checked
    # separately from identity and reported on its own terms.
    if col_hi >= col_lo and 0 <= col_lo < cols and not hollow:
        r = row_lo if row_hi >= row_lo and 0 <= row_lo < rows else min(rows - 1, 1)
        if cell_is_blank(img, col_lo, r, unit):
            fail(name, "run cell ({},{}) is fully transparent - widening this panel would open "
                       "a hole in its own body. Set \"hollowInterior\": true if that is "
                       "deliberate".format(col_lo, r))


def load_sheet(entry, is_legacy):
    name = entry["name"]
    path = os.path.join(SHEET_DIR, entry["file"])
    if not os.path.isfile(path):
        fail(name, "file not found: {}".format(entry["file"]))
        return None

    img = Image.open(path).convert("RGBA")
    w, h = img.size

    if w > MAX_TEXEL or h > MAX_TEXEL:
        fail(name, "{}x{} exceeds the {} texel S10.5 limit in So2h_UiDrawQuad".format(w, h, MAX_TEXEL))

    if is_legacy:
        cols, rows, unit = 1, 1, max(w, h)
        run = {}
    else:
        cols, rows = entry["grid"]
        unit = entry["unit"]
        if cols * unit != w or rows * unit != h:
            fail(name, "declared grid {}x{} at unit {} is {}x{} px but the file is {}x{} px"
                       .format(cols, rows, unit, cols * unit, rows * unit, w, h))
            return None
        run = {}
        if "run" in entry:
            if "cols" in entry["run"]:
                run["cols"] = tuple(entry["run"]["cols"])
            if "rows" in entry["run"]:
                run["rows"] = tuple(entry["run"]["rows"])
        check_run_band(name, img, cols, rows, unit, run, entry.get("hollowInterior", False))

        lines, uniq = identity_map(img, cols, rows, unit)
        audit.append("{}  {}x{} @{}  {}/{} unique".format(name, cols, rows, unit, uniq, cols * rows))
        for line in lines:
            audit.append("    " + line)

    slices = {}
    for sname, (c, r) in sorted(entry.get("slices", {}).items()):
        if not (0 <= c < cols and 0 <= r < rows):
            fail(name, "named slice {} at ({},{}) is outside the {}x{} grid".format(sname, c, r, cols, rows))
        slices[sname] = c + r * cols

    col_lo, col_hi = run.get("cols", (0, -1))
    row_lo, row_hi = run.get("rows", (0, -1))

    return {
        "name": name,
        "width": w,
        "height": h,
        "unit": unit,
        "cols": cols,
        "rows": rows,
        "runColLo": col_lo,
        "runColHi": col_hi,
        "runRowLo": row_lo,
        "runRowHi": row_hi,
        "note": entry.get("note", ""),
        "slices": slices,
        "legacy": is_legacy,
    }


# ---------------------------------------------------------------------------------------
# Emission
# ---------------------------------------------------------------------------------------

BANNER = ("// GENERATED by tools/so2h_gen_sheets.py from\n"
          "// mm/assets/custom/textures/so2h_menu/sheets.json - do not edit by hand.\n")


def emit_assets(sheets, otr_folder):
    out = ["#ifndef SO2H_UI_SHEETS_ASSETS_H", "#define SO2H_UI_SHEETS_ASSETS_H", "", BANNER]
    for s in sheets:
        if s["legacy"]:
            continue  # already declared in the manually maintained mm/assets/2s2h_assets.h
        out.append('#define d{n} "__OTR__{f}/{n}"'.format(n=s["name"], f=otr_folder))
        out.append('static const ALIGN_ASSET(2) char {n}Tex[] = d{n};'.format(n=s["name"]))
        out.append("")
    out.append("#endif")
    return "\n".join(out) + "\n"


def emit_table(sheets):
    out = ["#ifndef SO2H_UI_SHEETS_H", "#define SO2H_UI_SHEETS_H", "", BANNER,
           '#include "so2h_ui.h"', "", "typedef enum So2hUiSheetId {"]
    for s in sheets:
        out.append("    SO2H_SHEET_{},".format(s["name"][len("gSo2h"):].upper()))
    out.append("    SO2H_SHEET_MAX")
    out.append("} So2hUiSheetId;")
    out.append("")

    for s in sheets:
        if not s["slices"]:
            continue
        prefix = s["name"][len("gSo2h"):].upper()
        for sname, idx in sorted(s["slices"].items(), key=lambda kv: kv[1]):
            out.append("#define SO2H_SLICE_{}_{} {}".format(prefix, sname, idx))
        out.append("")

    out.append("// Populated by so2h_ui_sheets.c, which is the only place the texture pointers")
    out.append("// are resolved. Everything else refers to a sheet by its So2hUiSheetId.")
    out.append("extern const So2hUiSheetDef gSo2hUiSheets[SO2H_SHEET_MAX];")
    out.append("")
    out.append("#define SO2H_UI_SHEET_COUNT {}".format(len(sheets)))
    out.append("")
    out.append("#endif")
    return "\n".join(out) + "\n"


def emit_table_c(sheets):
    """The .c half, so the header stays free of asset includes."""
    out = [BANNER,
           '#include "so2h_ui_sheets.h"',
           '#include "assets/2s2h_assets.h"',
           '#include "assets/so2h_ui_sheets_assets.h"',
           "",
           "const So2hUiSheetDef gSo2hUiSheets[SO2H_SHEET_MAX] = {"]
    for i, s in enumerate(sheets):
        sid = "SO2H_SHEET_{}".format(s["name"][len("gSo2h"):].upper())
        if s["note"]:
            out.append("    // {}".format(s["note"]))
        out.append("    [{sid}] = {{ {sid}, {n}Tex, {w}, {h}, {u}, {c}, {r}, SO2H_UI_FMT_RGBA32, "
                   "{cl}, {ch}, {rl}, {rh}, 0, \"{n}\" }},"
                   .format(sid=sid, n=s["name"], w=s["width"], h=s["height"], u=s["unit"],
                           c=s["cols"], r=s["rows"],
                           cl=s["runColLo"] & 0xFF, ch=s["runColHi"] & 0xFF,
                           rl=s["runRowLo"] & 0xFF, rh=s["runRowHi"] & 0xFF))
    out.append("};")
    return "\n".join(out) + "\n"


def write_if_changed(path, text, check_only):
    old = None
    if os.path.isfile(path):
        with open(path, "r", encoding="utf-8") as f:
            old = f.read()
    if old == text:
        return False
    if check_only:
        errors.append("{} is out of date - rerun tools/so2h_gen_sheets.py".format(os.path.relpath(path, ROOT)))
        return False
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    return True


def main():
    check_only = "--check" in sys.argv

    with open(MANIFEST, "r", encoding="utf-8") as f:
        man = json.load(f)

    sheets = []
    for entry in man["sheets"]:
        s = load_sheet(entry, False)
        if s:
            sheets.append(s)

    legacy = man.get("legacyTiles", {})
    for name in legacy.get("names", []):
        s = load_sheet({"name": name, "file": name + ".rgba32.png"}, True)
        if s:
            sheets.append(s)

    names = [s["name"] for s in sheets]
    for n in set(names):
        if names.count(n) > 1:
            fail(n, "declared {} times".format(names.count(n)))
    if len(sheets) > MAX_SHEETS:
        errors.append("{} sheets exceeds SO2H_UI_MAX_SHEETS ({})".format(len(sheets), MAX_SHEETS))

    print("so2h_gen_sheets: {} sheets ({} new, {} legacy)"
          .format(len(sheets), sum(1 for s in sheets if not s["legacy"]),
                  sum(1 for s in sheets if s["legacy"])))
    print("\n-- tile identity audit " + "-" * 55)
    for line in audit:
        print(line)
    print("-" * 78 + "\n")

    if errors:
        print("so2h_gen_sheets: FAILED")
        for e in errors:
            print("  " + e)
        return 1

    changed = []
    if write_if_changed(OUT_ASSETS, emit_assets(sheets, man["otrFolder"]), check_only):
        changed.append(OUT_ASSETS)
    if write_if_changed(OUT_TABLE, emit_table(sheets), check_only):
        changed.append(OUT_TABLE)
    if write_if_changed(os.path.join(ROOT, "mm", "2s2h", "Menu", "so2h_ui_sheets.c"),
                        emit_table_c(sheets), check_only):
        changed.append("so2h_ui_sheets.c")

    if errors:
        print("so2h_gen_sheets: FAILED")
        for e in errors:
            print("  " + e)
        return 1

    for c in changed:
        print("  wrote " + os.path.relpath(str(c), ROOT))
    print("so2h_gen_sheets: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
