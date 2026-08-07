#!/usr/bin/env python3
"""
so2h_ui_render.py

Renders a so2h_ui tree to PNG on the host, from the real sheets, using the real solver.

The point is approval without a build. tools/so2h_ui_model.py mirrors the layout solver and
the frame-axis splitter, so a seam lands here exactly where the C puts it on screen; the
sheet table is read straight out of the generated mm/2s2h/Menu/so2h_ui_sheets.c so the
renderer cannot disagree with the build about a sheet's grid, unit or run band.

What is faithful
    - layout, morph, run overlap, scroll offset          (shared code with the C)
    - nine-slice / run seam positions and cropped source rects
    - analytic clipping: destination intersected, s/t advanced by the same fraction
    - dsdx/dtdy taken from the UNCROPPED destination, so art does not squash as it
      slides out of a clip window
    - the widescreen-extended rect space, including a negative screen x0

What is not
    - the N64 combiner. Colour and alpha are applied as a straight modulate, which is what
      the menu's blend mode amounts to, but do not read exact pixel values off this.
    - texture filtering. Sampling is nearest by default (--filter bilinear to soften).

Usage
    python3 tools/so2h_ui_render.py                       # 4:3 and 16:9 of the demo tree
    python3 tools/so2h_ui_render.py --aspect 2.33
    python3 tools/so2h_ui_render.py --scale 4 --out /tmp/menu
    python3 tools/so2h_ui_render.py --sheet gSo2hSheetFrameFilled --grid
    python3 tools/so2h_ui_render.py --list
"""

import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import so2h_ui_model as M  # noqa: E402

try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.stderr.write("This tool needs Pillow: pip install pillow\n")
    raise

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHEET_TABLE = os.path.join(ROOT, "mm/2s2h/Menu/so2h_ui_sheets.c")
SHEET_DIR = os.path.join(ROOT, "mm/assets/custom/textures/so2h_menu")

FMT_RGBA32, FMT_IA8 = 0, 1


# ------------------------------------------------------------------------------------
# sheet table
# ------------------------------------------------------------------------------------
class Sheet(object):
    __slots__ = ("id", "name", "width", "height", "unit", "cols", "rows", "fmt",
                 "runColLo", "runColHi", "runRowLo", "runRowHi", "path", "_img")

    def __init__(self, **kw):
        for k, v in kw.items():
            setattr(self, k, v)
        self._img = None

    @property
    def image(self):
        """Loaded lazily; a tree usually touches a handful of the 54."""
        if self._img is None:
            if not os.path.exists(self.path):
                raise SystemExit("missing sheet art: %s" % self.path)
            img = Image.open(self.path).convert("RGBA")
            if (img.width, img.height) != (self.width, self.height):
                raise SystemExit(
                    "%s is %dx%d on disk but the generated table says %dx%d - "
                    "re-run tools/so2h_gen_sheets.py"
                    % (self.name, img.width, img.height, self.width, self.height))
            self._img = img
        return self._img


ROW_RE = re.compile(
    r"\[\s*(SO2H_SHEET_[A-Z0-9_]+)\s*\]\s*=\s*\{(.*?)\}\s*,", re.S)


def load_sheets():
    """Parses the generated table. Deliberately reads the C, not sheets.json: the C is what
    the build compiles, so if the two ever disagree the renderer sides with the build."""
    if not os.path.exists(SHEET_TABLE):
        raise SystemExit("run tools/so2h_gen_sheets.py first - %s is missing" % SHEET_TABLE)
    src = open(SHEET_TABLE, "r", encoding="utf-8", errors="replace").read()
    # Drop everything up to and including the array's opening brace. The declaration
    # `gSo2hUiSheets[SO2H_SHEET_MAX] = {` has the same shape as a designated initialiser and
    # would otherwise match first and swallow the first real row.
    decl = re.search(r"gSo2hUiSheets\s*\[[^\]]*\]\s*=\s*\{", src)
    if decl:
        src = src[decl.end():]
    out = {}
    order = []
    for m in ROW_RE.finditer(src):
        # The array declaration itself is `gSo2hUiSheets[SO2H_SHEET_MAX] = {`, which matches
        # the same shape as a designated initialiser and would swallow the first real row.
        if m.group(1) == "SO2H_SHEET_MAX":
            continue
        parts = [p.strip() for p in m.group(2).split(",")]
        if len(parts) < 14:
            continue
        name = parts[13].strip().strip('"')

        def num(tok):
            tok = tok.strip()
            return int(tok, 0) if tok.lstrip("-").isdigit() or tok.startswith("0x") else 0

        sh = Sheet(id=len(order), name=name,
                   width=num(parts[2]), height=num(parts[3]), unit=num(parts[4]),
                   cols=num(parts[5]), rows=num(parts[6]),
                   fmt=FMT_IA8 if "IA8" in parts[7] else FMT_RGBA32,
                   runColLo=num(parts[8]), runColHi=num(parts[9]),
                   runRowLo=num(parts[10]), runRowHi=num(parts[11]),
                   path=os.path.join(SHEET_DIR, name + ".rgba32.png"))
        out[name] = sh
        order.append(sh)
    if not out:
        raise SystemExit("parsed 0 sheets out of %s" % SHEET_TABLE)
    return out, order


# ------------------------------------------------------------------------------------
# the emitter - mirrors So2h_UiDrawQuad
# ------------------------------------------------------------------------------------
def rnd(v):
    return int(v + 0.5) if v >= 0.0 else -int(-v + 0.5)


class Canvas(object):
    def __init__(self, ctx, scale, filter_mode):
        root = ctx.node[0].rect
        self.ox, self.oy = root.x0, root.y0
        self.w = int(round(root.w * scale))
        self.h = int(round(root.h * scale))
        self.scale = scale
        self.resample = Image.BILINEAR if filter_mode == "bilinear" else Image.NEAREST
        self.img = Image.new("RGBA", (self.w, self.h), (12, 12, 16, 255))
        self.quads = 0
        self.skipped = 0

    def px(self, x, y):
        return ((x - self.ox) * self.scale, (y - self.oy) * self.scale)

    def draw_quad(self, sheet, sx, sy, sw, sh, dst, clip, rgb=(255, 255, 255), alpha=1.0):
        """Faithful port of So2h_UiDrawQuad, including the uncropped dsdx/dtdy rule."""
        dw, dh = dst.x1 - dst.x0, dst.y1 - dst.y0
        if dw < M.MIN_QUAD_PX or dh < M.MIN_QUAD_PX or sw <= 0.0 or sh <= 0.0:
            self.skipped += 1
            return
        vx0, vy0 = max(dst.x0, clip.x0), max(dst.y0, clip.y0)
        vx1, vy1 = min(dst.x1, clip.x1), min(dst.y1, clip.y1)
        if vx1 <= vx0 or vy1 <= vy0:
            self.skipped += 1
            return

        # Scale is fixed by the UNCROPPED destination. Getting this wrong is the classic
        # bug where art squashes as it slides out of a clip window.
        tex_per_px_x = sw / dw
        tex_per_px_y = sh / dh
        s = sx + ((vx0 - dst.x0) * tex_per_px_x)
        t = sy + ((vy0 - dst.y0) * tex_per_px_y)
        vis_sw = (vx1 - vx0) * tex_per_px_x
        vis_sh = (vy1 - vy0) * tex_per_px_y

        x0, y0 = self.px(vx0, vy0)
        x1, y1 = self.px(vx1, vy1)
        ix0, iy0, ix1, iy1 = rnd(x0), rnd(y0), rnd(x1), rnd(y1)
        if ix1 <= ix0 or iy1 <= iy0:
            self.skipped += 1
            return

        src = sheet.image
        cx0 = max(0, int(s))
        cy0 = max(0, int(t))
        cx1 = min(src.width, max(cx0 + 1, int(round(s + vis_sw))))
        cy1 = min(src.height, max(cy0 + 1, int(round(t + vis_sh))))
        if cx1 <= cx0 or cy1 <= cy0:
            self.skipped += 1
            return

        patch = src.crop((cx0, cy0, cx1, cy1)).resize((ix1 - ix0, iy1 - iy0), self.resample)
        if rgb != (255, 255, 255) or alpha < 0.999:
            r, g, b, a = patch.split()
            if rgb != (255, 255, 255):
                r = r.point(lambda v, m=rgb[0]: (v * m) // 255)
                g = g.point(lambda v, m=rgb[1]: (v * m) // 255)
                b = b.point(lambda v, m=rgb[2]: (v * m) // 255)
            if alpha < 0.999:
                a = a.point(lambda v, m=alpha: int(v * m))
            patch = Image.merge("RGBA", (r, g, b, a))
        self.img.alpha_composite(patch, (ix0, iy0))
        self.quads += 1


# ------------------------------------------------------------------------------------
# drawers - mirror so2h_ui_draw.c
# ------------------------------------------------------------------------------------
def cell_src(sheet, col, row):
    sx = float(col * sheet.unit)
    sy = float(row * sheet.unit)
    sw = float(sheet.unit)
    sh = float(sheet.unit)
    if sx + sw > sheet.width:
        sw = sheet.width - sx
    if sy + sh > sheet.height:
        sh = sheet.height - sy
    return sx, sy, max(0.0, sw), max(0.0, sh)


def cell_screen(sheet, tile):
    return (sheet.unit / 32.0) * tile


def draw_frame(cv, sheet, dst, clip, repeat, tile, rgb, alpha):
    cell = cell_screen(sheet, tile)
    xs = M.solve_frame_axis(sheet.cols, sheet.runColLo, sheet.runColHi, cell,
                            dst.x1 - dst.x0, repeat)
    ys = M.solve_frame_axis(sheet.rows, sheet.runRowLo, sheet.runRowHi, cell,
                            dst.y1 - dst.y0, repeat)
    for ypos, ysize, yc in ys:
        for xpos, xsize, xc in xs:
            r = M.Rect(dst.x0 + xpos, dst.y0 + ypos,
                       dst.x0 + xpos + xsize, dst.y0 + ypos + ysize)
            if r.x1 < clip.x0 or r.x0 > clip.x1 or r.y1 < clip.y0 or r.y0 > clip.y1:
                continue
            sx, sy, sw, sh = cell_src(sheet, xc, yc)
            # A cropped repeat shows only the leading part of its cell; the source rect is
            # cropped by the same fraction or the seam shifts.
            if xsize < cell - 0.01:
                sw *= xsize / cell
            if ysize < cell - 0.01:
                sh *= ysize / cell
            if sw < 1.0 or sh < 1.0:
                continue
            cv.draw_quad(sheet, sx, sy, sw, sh, r, clip, rgb, alpha)


def draw_tiled(cv, sheet, slice_idx, dst, clip, tile, rgb, alpha):
    cell = cell_screen(sheet, tile)
    if cell <= 0.0 or sheet.cols <= 0:
        return
    sx, sy, sw, sh = cell_src(sheet, slice_idx % sheet.cols, slice_idx // sheet.cols)
    y, gy = dst.y0, 0
    while y < dst.y1 and gy < M.MAX_REPEATS:
        row_h = min(cell, dst.y1 - y)
        if y > clip.y1:
            break
        if (y + cell) >= clip.y0:
            x, gx = dst.x0, 0
            while x < dst.x1 and gx < M.MAX_REPEATS:
                col_w = min(cell, dst.x1 - x)
                if x > clip.x1:
                    break
                if (x + cell) >= clip.x0:
                    r = M.Rect(x, y, x + col_w, y + row_h)
                    cv.draw_quad(sheet, sx, sy, sw * (col_w / cell), sh * (row_h / cell),
                                 r, clip, rgb, alpha)
                x += cell
                gx += 1
        y += cell
        gy += 1


def draw_slice(cv, sheet, slice_idx, dst, clip, mode, tile, rgb, alpha):
    if sheet.cols <= 0:
        return
    col = slice_idx % sheet.cols
    row = slice_idx // sheet.cols
    sx, sy, sw, sh = cell_src(sheet, col, row)
    if mode == M.DRAW_NATIVE:
        cell = cell_screen(sheet, tile)
        cx = (dst.x0 + dst.x1) * 0.5
        cy = (dst.y0 + dst.y1) * 0.5
        dst = M.Rect(cx - cell * 0.5, cy - cell * 0.5, cx + cell * 0.5, cy + cell * 0.5)
    cv.draw_quad(sheet, sx, sy, sw, sh, dst, clip, rgb, alpha)


def draw_node(cv, ctx, sheets_by_id, i):
    d = ctx.desc[i]
    n = ctx.node[i]
    st = d.style
    if not n.visible or st.drawMode == M.DRAW_NONE or st.sheet == M.INVALID:
        return
    sheet = sheets_by_id.get(st.sheet)
    if sheet is None:
        return
    rect, clip = n.rect, n.clipRect
    slice_idx = 0 if st.slice == M.INVALID else st.slice
    if st.drawMode == M.DRAW_NINESLICE:
        draw_frame(cv, sheet, rect, clip, False, ctx.tile, st.rgb, n.alpha)
    elif st.drawMode == M.DRAW_RUN:
        draw_frame(cv, sheet, rect, clip, True, ctx.tile, st.rgb, n.alpha)
    elif st.drawMode == M.DRAW_TILE:
        draw_tiled(cv, sheet, slice_idx, rect, clip, ctx.tile, st.rgb, n.alpha)
    else:
        draw_slice(cv, sheet, slice_idx, rect, clip, st.drawMode, ctx.tile, st.rgb, n.alpha)


def draw_tree(cv, ctx, sheets_by_id):
    """Layer walk, same order as So2h_UiDraw_Tree: low layers first, tree order within."""
    for layer in range(M.MAX_LAYERS):
        for i in range(len(ctx.desc)):
            if ctx.desc[i].style.layer == layer:
                draw_node(cv, ctx, sheets_by_id, i)


def draw_cells(cv, ctx, sheets_by_id, cell_style):
    """Container cells are not nodes, so they are drawn from cell_rect the way a consumer's
    cell callback would. cell_style maps a node id to (sheet, slice_of_index)."""
    for i, d in enumerate(ctx.desc):
        if i not in cell_style or not ctx.node[i].visible:
            continue
        sheet_name, slice_fn = cell_style[i]
        sheet = sheets_by_id.get(sheet_name)
        if sheet is None:
            continue
        if d.layout == M.GRID:
            count = d.cols * d.rows
        elif d.layout == M.RUN:
            count = ctx.run.get(i).count if ctx.run.get(i) else 0
        else:
            count = d.count
        clip = ctx.node[i].clipRect
        for k in range(count):
            r = ctx.cell_rect(i, k)
            if r.w <= 0.0 or r.h <= 0.0:
                continue
            draw_slice(cv, sheet, slice_fn(k), r, clip, M.DRAW_STRETCH, ctx.tile,
                       (255, 255, 255), ctx.node[i].alpha)


# ------------------------------------------------------------------------------------
# overlays
# ------------------------------------------------------------------------------------
def annotate(cv, ctx, show_clip):
    dr = ImageDraw.Draw(cv.img, "RGBA")
    for i, d in enumerate(ctx.desc):
        n = ctx.node[i]
        if not n.visible:
            continue
        x0, y0 = cv.px(n.rect.x0, n.rect.y0)
        x1, y1 = cv.px(n.rect.x1, n.rect.y1)
        colour = {M.CLIP_SPILL: (255, 90, 90, 200),
                  M.CLIP_SELF: (90, 200, 255, 200)}.get(d.clip, (120, 255, 120, 110))
        dr.rectangle([x0, y0, max(x0, x1 - 1), max(y0, y1 - 1)], outline=colour)
        dr.text((x0 + 2, y0 + 1), d.name, fill=(255, 255, 255, 220))
        if show_clip and d.clip == M.CLIP_SELF:
            cx0, cy0 = cv.px(n.clipRect.x0, n.clipRect.y0)
            cx1, cy1 = cv.px(n.clipRect.x1, n.clipRect.y1)
            dr.rectangle([cx0, cy0, max(cx0, cx1 - 1), max(cy0, cy1 - 1)],
                         outline=(255, 220, 60, 180))
    # The 4:3 safe box, so it is obvious what widescreen is adding.
    sx0, _ = cv.px(0.0, 0.0)
    sx1, _ = cv.px(M.DESIGN_W_43, 0.0)
    dr.rectangle([sx0, 0, sx1 - 1, cv.h - 1], outline=(255, 255, 255, 60))


def sheet_grid_png(sheet, out_path, scale):
    """Contact sheet of one atlas with its grid and run band drawn on, for picking slices."""
    img = sheet.image.copy()
    if scale != 1:
        img = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
    base = Image.new("RGBA", img.size, (24, 24, 28, 255))
    base.alpha_composite(img)
    dr = ImageDraw.Draw(base, "RGBA")
    u = sheet.unit * scale
    for c in range(sheet.cols + 1):
        dr.line([(c * u, 0), (c * u, base.height)], fill=(0, 255, 255, 110))
    for r in range(sheet.rows + 1):
        dr.line([(0, r * u), (base.width, r * u)], fill=(0, 255, 255, 110))
    lo, hi = sheet.runColLo, sheet.runColHi
    if hi < sheet.cols and hi >= lo:
        dr.rectangle([lo * u, 0, (hi + 1) * u - 1, base.height - 1], outline=(255, 80, 80, 220))
    lo, hi = sheet.runRowLo, sheet.runRowHi
    if hi < sheet.rows and hi >= lo:
        dr.rectangle([0, lo * u, base.width - 1, (hi + 1) * u - 1], outline=(255, 80, 80, 220))
    for r in range(sheet.rows):
        for c in range(sheet.cols):
            dr.text((c * u + 2, r * u + 2), str(r * sheet.cols + c), fill=(255, 255, 0, 210))
    base.save(out_path)
    return out_path


# ------------------------------------------------------------------------------------
# demo tree - the shape Jay asked for
# ------------------------------------------------------------------------------------
def demo_tree(sheets):
    A, S = M.Axis, M.Style
    F = "gSo2hSheetFrameFilled"
    E = "gSo2hSheetFrameEmpty"
    B = "gSo2hSheetBigButton"
    SB = "gSo2hSheetSongBackdrop"
    IT = "gSo2hSheetItems"

    d = []

    def add(**kw):
        kw.setdefault("id", len(d))
        kw.setdefault("parent", 0)
        d.append(M.Desc(**kw))
        return kw["id"]

    root = add(id=0, parent=0, kind=M.ROOT, name="root")
    window = add(parent=root, kind=M.PANEL, clip=M.CLIP_SELF,
                 x=A(M.CENTER, 0, 200), y=A(M.CENTER, 0, 168),
                 style=S(sheet=F, drawMode=M.DRAW_RUN, layer=0), name="window")
    body = add(parent=window, kind=M.PANEL,
               x=A(M.STRETCH, 10, 10), y=A(M.STRETCH, 10, 26),
               style=S(sheet=E, drawMode=M.DRAW_NINESLICE, layer=1), name="body")
    strip = add(parent=body, kind=M.GROUP, layout=M.RUN, pitch=32.0, growEnd=0, cols=6,
                x=A(M.STRETCH, 6, 6), y=A(M.START, 6, 16),
                style=S(sheet=SB, drawMode=M.DRAW_RUN, layer=2), name="songs")
    grid = add(parent=body, kind=M.GROUP, layout=M.GRID, cols=5, rows=2, gap=3.0,
               x=A(M.CENTER, 0, 120), y=A(M.START, 28, 48),
               style=S(layer=2), name="hexgrid")
    btn_l = add(parent=window, kind=M.CELL, x=A(M.START, 8, 22), y=A(M.END, 6, 18),
                style=S(sheet=B, slice=0, drawMode=M.DRAW_STRETCH, layer=3), name="btn.prev")
    btn_r = add(parent=window, kind=M.CELL, x=A(M.END, 8, 22), y=A(M.END, 6, 18),
                style=S(sheet=B, slice=1, drawMode=M.DRAW_STRETCH, layer=3), name="btn.next")

    variants = [M.Variant(window, M.CANON_WIDE, x=A(M.CENTER, 0, 260))]
    cell_style = {
        grid: (IT, lambda k: k + 28),
        strip: (IT, lambda k: k + 1),
    }
    return d, variants, cell_style


# ------------------------------------------------------------------------------------
def render(sheets, aspect, scale, filter_mode, annotate_on, show_clip, out_path):
    desc, variants, cell_style = demo_tree(sheets)
    ctx = M.Ctx(desc, variants, aspect=aspect)
    ctx.settle()
    cv = Canvas(ctx, scale, filter_mode)
    draw_tree(cv, ctx, sheets)
    draw_cells(cv, ctx, sheets, cell_style)
    if annotate_on:
        annotate(cv, ctx, show_clip)
    cv.img.save(out_path)
    print("%-46s %dx%d  aspect %.3f  canon %d  %d quads, %d skipped"
          % (out_path, cv.w, cv.h, ctx.aspect, ctx.activeCanon, cv.quads, cv.skipped))
    return out_path


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--aspect", type=float, action="append",
                    help="aspect to render; repeatable. Default 4:3 and 16:9.")
    ap.add_argument("--scale", type=int, default=3, help="pixels per design unit")
    ap.add_argument("--filter", choices=("nearest", "bilinear"), default="nearest")
    ap.add_argument("--out", default="/tmp/so2h_ui", help="output path prefix")
    ap.add_argument("--annotate", action="store_true", help="draw node rects and names")
    ap.add_argument("--clip", action="store_true", help="also draw clip rects")
    ap.add_argument("--sheet", help="render one sheet as an annotated contact sheet")
    ap.add_argument("--grid", action="store_true", help="use with --sheet")
    ap.add_argument("--list", action="store_true", help="list the registered sheets")
    args = ap.parse_args()

    sheets, order = load_sheets()

    if args.list:
        print("%-30s %9s %5s %6s %-12s %s" % ("name", "size", "unit", "grid", "run c/r", "art"))
        for sh in order:
            print("%-30s %4dx%-4d %5d %3dx%-3d %-12s %s"
                  % (sh.name, sh.width, sh.height, sh.unit, sh.cols, sh.rows,
                     "%d-%d / %d-%d" % (sh.runColLo, sh.runColHi, sh.runRowLo, sh.runRowHi),
                     "ok" if os.path.exists(sh.path) else "MISSING"))
        print("\n%d sheets" % len(order))
        return 0

    if args.sheet:
        sh = sheets.get(args.sheet)
        if sh is None:
            raise SystemExit("no such sheet: %s (try --list)" % args.sheet)
        out = "%s_%s.png" % (args.out, sh.name)
        sheet_grid_png(sh, out, max(1, args.scale))
        print(out)
        return 0

    aspects = args.aspect or [4.0 / 3.0, 16.0 / 9.0]
    for a in aspects:
        out = "%s_%s.png" % (args.out, ("%.3f" % a).replace(".", "_"))
        render(sheets, a, args.scale, args.filter, args.annotate, args.clip, out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
