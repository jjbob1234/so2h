#!/usr/bin/env python3
"""
so2h_slice_menu_sheet.py

Slices the SO2H OOT/MM-style GUI sheet into individual textures under
mm/assets/custom/textures/so2h_menu/.

Everything is cut on the sheet's native 35 px grid (or 70 px = 2x2 cells for the
big pieces) and is NEVER rescaled - the art is authored at 35, so resampling it
to 32/48 was the wrong call and is gone. This is a PC build (LUS/Fast3D), so the
N64 4 KiB TMEM ceiling does not apply and every piece ships as plain RGBA32 with
its full soft alpha intact.

The only pieces that are not grid-aligned are the two authored example panels on
the left of the sheet; those are cut on their real pixel bounds so the 9-slice
seams land exactly on the bevel.

Run:  python3 tools/so2h_slice_menu_sheet.py [sheet.png]
"""

import os
import sys

from PIL import Image

OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "mm", "assets", "custom", "textures",
                       "so2h_menu")

CELL = 35


def cell(col, row, w=1, h=1):
    """Rect for a w x h block of native 35 px grid cells at (col, row)."""
    return (col * CELL, row * CELL, (col + w) * CELL, (row + h) * CELL)


# --- panel A: the big double-bevel frame, authored at (27,54)-(322,317) -------
# Outer dark edge at x=27..29, inner bevel line at x=71..78 -> a 52 px band
# captures the whole bevel with a clean pixel of slack.
A_X0, A_Y0, A_X1, A_Y1 = 27, 54, 322, 317
BAND = 52
A_CX = (A_X0 + A_X1) // 2
A_CY = (A_Y0 + A_Y1) // 2

PIECES = {
    # 9-slice frame. Corners are 52x52, edges are a 35 px run across the band,
    # fill is a 35x35 patch of the recessed interior. All tiled/stretched in C.
    "gSo2hFrameTL": (A_X0, A_Y0, A_X0 + BAND, A_Y0 + BAND),
    "gSo2hFrameTR": (A_X1 - BAND, A_Y0, A_X1, A_Y0 + BAND),
    "gSo2hFrameBL": (A_X0, A_Y1 - BAND, A_X0 + BAND, A_Y1),
    "gSo2hFrameBR": (A_X1 - BAND, A_Y1 - BAND, A_X1, A_Y1),
    "gSo2hFrameTop": (A_CX - 17, A_Y0, A_CX + 18, A_Y0 + BAND),
    "gSo2hFrameBottom": (A_CX - 17, A_Y1 - BAND, A_CX + 18, A_Y1),
    "gSo2hFrameLeft": (A_X0, A_CY - 17, A_X0 + BAND, A_CY + 18),
    "gSo2hFrameRight": (A_X1 - BAND, A_CY - 17, A_X1, A_CY + 18),
    "gSo2hFrameFill": (A_CX - 17, A_CY - 17, A_CX + 18, A_CY + 18),

    # --- panel B: the thin rounded window, shipped whole (210x210) -----------
    # Used as the song-preview window in the bottom-right corner.
    "gSo2hWindow": (350, 0, 560, 210),

    # --- 70 px (2x2 cell) pieces --------------------------------------------
    "gSo2hCrossRed": cell(16, 2, 2, 2),
    "gSo2hRoundTile": cell(16, 4, 2, 2),
    "gSo2hCellTile": cell(16, 6, 2, 2),
    "gSo2hSlotRecess": cell(14, 8, 2, 2),
    "gSo2hStaffLines": cell(20, 2, 2, 2),

    # --- 35x70 -----------------------------------------------------------------
    "gSo2hStaffClef": cell(20, 0, 1, 2),

    # --- single 35 px cells ---------------------------------------------------
    "gSo2hNoteWhite": cell(16, 0),
    "gSo2hNoteBlue": cell(17, 0),
    "gSo2hNoteBlack": cell(16, 1),
    "gSo2hNoteLocked": cell(17, 1),

    "gSo2hMarkTriDark": cell(18, 4),
    "gSo2hMarkTriGold": cell(19, 4),
    "gSo2hMarkPawnDark": cell(18, 5),
    "gSo2hMarkPawnLight": cell(19, 5),
    "gSo2hMarkPawnCyan": cell(18, 6),
    "gSo2hMarkPawnCyanX": cell(19, 6),
    "gSo2hMarkT": cell(18, 7),
    "gSo2hMarkVs": cell(19, 7),

    "gSo2hSlotDark": cell(16, 8),
    "gSo2hSlotLight": cell(17, 8),
    "gSo2hSlotHatch": cell(18, 8),

    "gSo2hBtnArrowLeft": cell(19, 3),
    "gSo2hBtnArrowDown": cell(19, 8),
    "gSo2hBtnArrowUp": cell(18, 9),
    "gSo2hBtnArrowRight": cell(19, 9),
    "gSo2hBtnCross": cell(16, 9),
    "gSo2hBtnCircle": cell(17, 9),
    "gSo2hBtnCUp": cell(20, 6),
    "gSo2hBtnCLeft": cell(21, 6),
    "gSo2hBtnCRight": cell(20, 7),
    "gSo2hBtnCDown": cell(21, 7),
    "gSo2hBtnB": cell(20, 8),
    "gSo2hBtnA": cell(21, 8),
}


def main():
    sheet_path = sys.argv[1] if len(sys.argv) > 1 else "/home/user/Attachments/ootmm_menu_style70px_cbV9MS.png"
    sheet = Image.open(sheet_path).convert("RGBA")
    os.makedirs(OUT_DIR, exist_ok=True)

    # wipe any previous (wrongly rescaled) slices so stale names cannot ship
    for stale in os.listdir(OUT_DIR):
        if stale.endswith(".png"):
            os.remove(os.path.join(OUT_DIR, stale))

    print("slicing %s (%dx%d) -> %d pieces" % (sheet_path, sheet.width, sheet.height, len(PIECES)))
    for name, (x0, y0, x1, y1) in sorted(PIECES.items()):
        assert 0 <= x0 < x1 <= sheet.width and 0 <= y0 < y1 <= sheet.height, name
        crop = sheet.crop((x0, y0, x1, y1))
        path = os.path.join(OUT_DIR, "%s.rgba32.png" % name)
        crop.save(path)
        print("  %-22s %3dx%-3d  @(%3d,%3d)" % (name, crop.width, crop.height, x0, y0))

    print("done -> %s" % os.path.normpath(OUT_DIR))


if __name__ == "__main__":
    main()
