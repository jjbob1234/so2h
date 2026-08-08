#!/usr/bin/env python3
"""
so2h_ui_render -- render a declared so2h_ui tree to PNG using the real sheets.

This is the approval loop: Jay sees exactly what the C will draw, at any aspect,
without a Windows build. Geometry comes from tools/so2h_ui_model.py (the faithful
port of the C solver) and seams come from model.solve_frame_axis, so a seam here
lands on the same texel it will land on in game.

Usage:
    python3 tools/so2h_ui_render.py --scene tools/scenes/pause.py --out /tmp/pause
        renders /tmp/pause_4x3.png and /tmp/pause_16x9.png
    python3 tools/so2h_ui_render.py --scene ... --aspect 2.35 --scale 4
"""

import argparse
import json
import os
import sys

import numpy as np
from PIL import Image, ImageFilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import so2h_ui_model as M  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHEET_DIR = os.path.join(REPO, "mm", "assets", "custom", "textures", "so2h_menu")
MANIFEST = os.path.join(SHEET_DIR, "sheets.json")


# ---------------------------------------------------------------------------------------
# Sheets
# ---------------------------------------------------------------------------------------
class Sheet(object):
    def __init__(self, name, path, unit, cols, rows, run, slices, rings=0, grow="run"):
        # Rings are declared in WHOLE TILES: [borderTiles, contentTiles]. A scalar n is
        # shorthand for a single-ring sheet. Kept in tiles here, converted to design units
        # by the model at the current tile scale - same as So2h_UiSheet_RingPx.
        if isinstance(rings, (int, float)):
            rings = [int(rings), int(rings)]
        self.rings = (int(rings[0]), int(rings[1]))
        self.grow = grow
        self.name = name
        self.path = path
        self.unit = unit
        self.cols = cols
        self.rows = rows
        self.run = run          # dict cols/rows -> [lo, hi], or None for an atlas
        self.slices = slices    # name -> (col, row)
        self._img = None
        self._cells = {}

    @property
    def img(self):
        if self._img is None:
            self._img = Image.open(self.path).convert("RGBA")
        return self._img

    def cell(self, col, row):
        key = (col, row)
        if key not in self._cells:
            u = self.unit
            box = (col * u, row * u, min((col + 1) * u, self.img.width),
                   min((row + 1) * u, self.img.height))
            self._cells[key] = self.img.crop(box)
        return self._cells[key]

    def band(self, axis):
        """Run band [lo, hi] on 'cols' or 'rows'; (-1, -2) means no band."""
        if not self.run or axis not in self.run:
            return (-1, -2)
        lo, hi = self.run[axis]
        return (lo, hi)


def load_sheets():
    man = json.load(open(MANIFEST))
    out = {}
    for s in man.get("sheets", []):
        grid = s.get("grid", [1, 1])
        out[s["name"]] = Sheet(
            s["name"], os.path.join(SHEET_DIR, s["file"]), s.get("unit", 32),
            grid[0], grid[1], s.get("run"),
            {k: tuple(v) for k, v in (s.get("slices") or {}).items()},
            s.get("rings", 0), s.get("grow", "run"))
    for name, e in (man.get("legacyTiles") or {}).items() if isinstance(
            man.get("legacyTiles"), dict) else []:
        pass
    legacy = man.get("legacyTiles")
    if isinstance(legacy, list):
        for e in legacy:
            nm = e["name"] if isinstance(e, dict) else e
            fn = e.get("file", nm + ".rgba32.png") if isinstance(e, dict) else nm + ".rgba32.png"
            p = os.path.join(SHEET_DIR, fn)
            if os.path.exists(p) and nm not in out:
                im = Image.open(p)
                out[nm] = Sheet(nm, p, max(im.width, im.height), 1, 1, None, {})
    return out


# ---------------------------------------------------------------------------------------
# Drawing
# ---------------------------------------------------------------------------------------
def tint(img, rgb, alpha):
    if rgb == (255, 255, 255) and alpha >= 255:
        return img
    r, g, b, a = img.split()
    if rgb != (255, 255, 255):
        r = r.point(lambda v: int(v * rgb[0] / 255.0))
        g = g.point(lambda v: int(v * rgb[1] / 255.0))
        b = b.point(lambda v: int(v * rgb[2] / 255.0))
    if alpha < 255:
        a = a.point(lambda v: int(v * alpha / 255.0))
    return Image.merge("RGBA", (r, g, b, a))


def checkerboard(w, h, cell, a=(214, 0, 190, 255), b=(150, 0, 133, 255)):
    img = Image.new("RGBA", (w, h), a)
    dark = Image.new("RGBA", (cell, cell), b)
    for y in range(0, h, cell):
        for x in range(0, w, cell):
            if ((x // cell) + (y // cell)) & 1:
                img.alpha_composite(dark, (x, y))
    return img


# ---------------------------------------------------------------------------------------
# Programmatic drop shadows
# ---------------------------------------------------------------------------------------
# One implementation for the whole tree, mirroring the SO2H_UI_SHADOW_* block in so2h_ui.h.
# A casting node is rendered to a scratch target, its ALPHA is taken as the silhouette,
# offset and blurred, and burnt into the canvas underneath the node itself. Nothing is
# authored per feature: shadows are on by default and a node opts out with shadow=False.
SHADOW_BLUR_PX = 32.0     # gaussian radius at the reference framebuffer height
SHADOW_REF_H = 1440.0
# The offset has to clear the blur reach or the shadow hides under the caster: 32px of
# gaussian at 1440p is 5.3 design units, so the offset is stated as a multiple of it.
SHADOW_OFS = (0.75, 1.05)  # x blur-reach, in design units -> 4.0 / 5.6
SHADOW_STRENGTH = 0.7
SHADOW_OFS_U = (0.0, 0.0)   # resolved from SHADOW_OFS at render time


def burn(base, alpha):
    """Colour burn `base` (RGBA uint8 HxWx4) by a 0..1 shadow coverage map."""
    a = np.clip(alpha * SHADOW_STRENGTH, 0.0, 0.995)[..., None]
    bg = base[..., :3].astype(np.float32) / 255.0
    out = 1.0 - np.minimum(1.0, (1.0 - bg) / np.maximum(1e-3, 1.0 - a))
    base[..., :3] = np.clip(out, 0.0, 1.0) * 255.0 + 0.5
    return base


class Canvas(object):
    def __init__(self, ctx, scale, backdrop=True):
        self.ctx = ctx
        self.scale = scale
        self.ox = -ctx.screenX0
        w = int(round(ctx.screenW * scale))
        h = int(round(M.DESIGN_H * scale))
        self.img = Image.new("RGBA", (w, h), (0, 0, 0, 255))
        if backdrop:
            self.img.alpha_composite(checkerboard(w, h, max(1, scale * 4)))

    # -- preview only -------------------------------------------------------------------
    # Everything this menu does NOT declare is the gameplay window. Painting it as the
    # mockup's magenta checkerboard makes that obvious at a glance: any checker still
    # visible is the game drawing there, and any checker COVERED by chrome is a bug.

    def scratch(self):
        """A transparent target with identical geometry - used for shadow silhouettes."""
        c = Canvas.__new__(Canvas)
        c.ctx, c.scale, c.ox = self.ctx, self.scale, self.ox
        c.img = Image.new("RGBA", self.img.size, (0, 0, 0, 0))
        return c

    def px(self, x, y):
        return (int(round((x + self.ox) * self.scale)), int(round(y * self.scale)))

    def blit(self, tile, x0, y0, x1, y1, clip, rgb=(255, 255, 255), alpha=255):
        """Draw one source tile into design-space rect, software-clipped to `clip`."""
        w, h = x1 - x0, y1 - y0
        if w <= 0.01 or h <= 0.01:
            return
        sx0, sy0, sx1, sy1 = 0.0, 0.0, float(tile.width), float(tile.height)
        if clip is not None:
            # Analytic clip: crop the dest rect and advance s/t by the same fraction.
            nx0, ny0 = max(x0, clip.x0), max(y0, clip.y0)
            nx1, ny1 = min(x1, clip.x1), min(y1, clip.y1)
            if nx1 <= nx0 or ny1 <= ny0:
                return
            sx0 += (nx0 - x0) / w * tile.width
            sx1 -= (x1 - nx1) / w * tile.width
            sy0 += (ny0 - y0) / h * tile.height
            sy1 -= (y1 - ny1) / h * tile.height
            x0, y0, x1, y1 = nx0, ny0, nx1, ny1

        src = tile.crop((int(round(sx0)), int(round(sy0)),
                         max(int(round(sx1)), int(round(sx0)) + 1),
                         max(int(round(sy1)), int(round(sy0)) + 1)))
        dx0, dy0 = self.px(x0, y0)
        dx1, dy1 = self.px(x1, y1)
        dw, dh = max(1, dx1 - dx0), max(1, dy1 - dy0)
        src = tint(src.resize((dw, dh), Image.NEAREST), rgb, alpha)
        self.img.alpha_composite(src, (dx0, dy0))


def draw_frame(cv, sheet, rect, clip, repeat, rgb, alpha, scale=1.0, hollow=False):
    cell = sheet.unit * (cv.ctx.tile / M.TILE_SRC) * scale
    lox, hix = sheet.band("cols")
    loy, hiy = sheet.band("rows")
    xs = M.solve_frame_axis(sheet.cols, lox, hix, cell, rect.w, repeat)
    ys = M.solve_frame_axis(sheet.rows, loy, hiy, cell, rect.h, repeat)
    # The hole in whole CELL INDICES, exactly as So2h_UiDrawFrame computes it: the rings are
    # tile-aligned, so this is an integer tile count and never a pixel scan.
    ring = sheet.rings[1] if hollow else 0   # CONTENT ring: drop the fill cells only
    hl, ht = ring, ring
    hr = sheet.cols - 1 - ring if hollow else -1
    hb = sheet.rows - 1 - ring if hollow else -1
    for py, sh, cy in ys:
        for px, sw, cx in xs:
            if hollow and hl <= cx <= hr and ht <= cy <= hb:
                continue
            cv.blit(sheet.cell(cx, cy), rect.x0 + px, rect.y0 + py,
                    rect.x0 + px + sw, rect.y0 + py + sh, clip, rgb, alpha)


def draw_node(cv, sheets, i):
    ctx = cv.ctx
    d, n = ctx.desc[i], ctx.node[i]
    st = d.style
    if not n.visible or st.drawMode == M.DRAW_NONE or st.sheet is M.INVALID:
        return
    sheet = sheets.get(st.sheet) if isinstance(st.sheet, str) else None
    if sheet is None:
        return
    clip = n.clipRect if d.clip != M.CLIP_SPILL else None
    a = int(round(st.alpha * n.alpha))
    if st.drawMode in (M.DRAW_NINESLICE, M.DRAW_RUN, M.DRAW_TILE, M.DRAW_RING):
        draw_frame(cv, sheet, n.rect, clip, st.drawMode != M.DRAW_NINESLICE, st.rgb, a,
                   getattr(st, 'scale', 1.0), hollow=st.drawMode == M.DRAW_RING)
    else:
        col, row = (0, 0)
        if st.slice is not M.INVALID and st.slice in sheet.slices:
            col, row = sheet.slices[st.slice]
        elif isinstance(st.slice, (list, tuple)):
            col, row = st.slice
        tile = sheet.cell(col, row)
        r = n.rect
        if st.drawMode == M.DRAW_NATIVE:
            w = sheet.unit * (ctx.tile / M.TILE_SRC) * getattr(st, 'scale', 1.0)
            cx, cy = (r.x0 + r.x1) * 0.5, (r.y0 + r.y1) * 0.5
            r = M.Rect(cx - w * 0.5, cy - w * 0.5, cx + w * 0.5, cy + w * 0.5)
        cv.blit(tile, r.x0, r.y0, r.x1, r.y1, clip, st.rgb, a)


def cast_shadow(cv, scratch, sheets, i, blur):
    """Draw node i's silhouette, blur it, and burn it into the canvas."""
    d, n = cv.ctx.desc[i], cv.ctx.node[i]
    if not getattr(d.style, "shadow", True) or not n.visible:
        return
    if d.style.drawMode == M.DRAW_NONE or d.style.sheet is M.INVALID:
        return
    draw_node(scratch, sheets, i)
    # Work on the node's own footprint plus the blur reach only - the shadow cannot travel
    # further than that, so the whole pass stays cheap no matter how many nodes cast.
    pad = int(blur * 3) + 2
    x0, y0 = cv.px(n.rect.x0 + SHADOW_OFS_U[0], n.rect.y0 + SHADOW_OFS_U[1])
    x1, y1 = cv.px(n.rect.x1 + SHADOW_OFS_U[0], n.rect.y1 + SHADOW_OFS_U[1])
    W, H = cv.img.size
    bx0, by0 = max(0, x0 - pad), max(0, y0 - pad)
    bx1, by1 = min(W, x1 + pad), min(H, y1 + pad)
    if bx1 <= bx0 or by1 <= by0:
        scratch.img.paste((0, 0, 0, 0), (0, 0, W, H))
        return
    # The silhouette sits at the node's own position in the scratch, so sampling it for a
    # destination pixel means reading back by the offset.
    ox = int(round(SHADOW_OFS_U[0] * cv.scale))
    oy = int(round(SHADOW_OFS_U[1] * cv.scale))
    sil = scratch.img.crop((bx0 - ox, by0 - oy, bx1 - ox, by1 - oy)).split()[3]
    sil = sil.filter(ImageFilter.GaussianBlur(blur))
    cov = np.asarray(sil, dtype=np.float32) / 255.0
    box = (bx0, by0, bx1, by1)
    reg = np.array(cv.img.crop(box))
    cv.img.paste(Image.fromarray(burn(reg, cov), "RGBA"), box)
    scratch.img.paste((0, 0, 0, 0), (0, 0, W, H))


def render(ctx, sheets, scale=3, backdrop=True, shadows=True):
    cv = Canvas(ctx, scale, backdrop)
    order = sorted(range(len(ctx.desc)), key=lambda i: (ctx.desc[i].style.layer, i))
    blur = SHADOW_BLUR_PX * (cv.img.size[1] / SHADOW_REF_H)
    reach = SHADOW_BLUR_PX / SHADOW_REF_H * M.DESIGN_H
    global SHADOW_OFS_U
    SHADOW_OFS_U = (SHADOW_OFS[0] * reach, SHADOW_OFS[1] * reach)
    scratch = cv.scratch() if shadows else None
    for i in order:
        if shadows:
            cast_shadow(cv, scratch, sheets, i, blur)
        draw_node(cv, sheets, i)
    return cv.img


# ---------------------------------------------------------------------------------------
def load_scene(path):
    ns = {"M": M}
    exec(compile(open(path).read(), path, "exec"), ns)
    if "build" not in ns:
        raise SystemExit("scene {} must define build(aspect) -> Ctx".format(path))
    return ns["build"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scene", required=True)
    ap.add_argument("--out", required=True, help="output base path, no extension")
    ap.add_argument("--aspect", type=float, action="append", default=None)
    ap.add_argument("--scale", type=int, default=3)
    ap.add_argument("--no-shadows", action="store_true",
                    help="disable the programmatic drop-shadow pass (it is on by default)")
    ap.add_argument("--no-backdrop", action="store_true",
                    help="skip the magenta gameplay-window checkerboard")
    args = ap.parse_args()

    sheets = load_sheets()
    build = load_scene(args.scene)
    # Hand the solver the same ring table the C reads out of the generated sheet def, so
    # the preview insets content exactly where the game will.
    M.Ctx.rings = {n: (sh.rings[0], sh.rings[1], sh.unit)
                     for n, sh in sheets.items()}
    aspects = args.aspect or [4.0 / 3.0, 16.0 / 9.0]
    for a in aspects:
        ctx = build(a)
        ctx.settle()
        img = render(ctx, sheets, args.scale, not args.no_backdrop,
                     shadows=not args.no_shadows)
        tag = "4x3" if abs(a - 4 / 3) < 1e-3 else ("16x9" if abs(a - 16 / 9) < 1e-3
                                                   else "{:.2f}".format(a).replace(".", "_"))
        p = "{}_{}.png".format(args.out, tag)
        img.convert("RGB").save(p)
        print("wrote", p, img.size)


if __name__ == "__main__":
    main()
