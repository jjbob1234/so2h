#!/usr/bin/env python3
"""
so2h_ui_model.py

A line-for-line Python port of the so2h_ui solver in mm/2s2h/Menu/so2h_ui_layout.c, plus the
frame-axis splitter from so2h_ui_draw.c.

It exists because the only way to run the real thing is a Windows build with a ROM, which
means every layout question would otherwise cost a full CI round trip. With the solver
mirrored here, tools/so2h_ui_sim.py can sweep every aspect and every span in a second, and
tools/so2h_ui_render.py can draw the solved tree from the real sheets so a look can be
approved without a build at all.

The port is deliberately dumb: same names, same order of operations, same magic numbers,
no numpy, no cleverness. If it stops matching the C, that is a bug in this file, and
tools/so2h_ui_sim.py --selfcheck prints the constants side by side so the drift is visible.

Nothing here imports Pillow; the renderer does that on its own.
"""

# --- constants, mirrored from so2h_ui.h and so2h_ui_layout.c -----------------------------
MAX_NODES = 192
MAX_PAGES = 12
MAX_RUNS = 24
MAX_SCROLL_ROWS = 16
MAX_CLIP_DEPTH = 4
MAX_LAYERS = 8
MAX_VARIANTS = 64
MAX_SHEETS = 64
INVALID = 0xFFFF

MORPH_FRAMES = 14
OVERSHOOT = 0.06
ASPECT_WIDE = 1.55
ASPECT_43 = 1.45

DESIGN_H = 240.0
DESIGN_W_43 = 320.0
DESIGN_W_WIDE = 426.0

TILE_SRC = 32.0
MOCK_H = 988.0
TILE_MIN = 4.0

RUN_EASE = 0.125
RUN_MIN_STEP = 0.15
RUN_STAGGER = 0.6
RUN_MAX_OVERLAP = 0.5

SCROLL_EASE = 0.2
SCROLL_MIN_STEP = 0.01

FRAME_MAX_CELLS = 96
MAX_REPEATS = 64          # SO2H_UI_MAX_REPEATS in so2h_ui_draw.c
MIN_QUAD_PX = 0.4         # SO2H_UI_MIN_QUAD_PX in so2h_ui_draw.c

# kinds
ROOT, PANEL, GROUP, PAGEGROUP, PAGE, CELL, DECOR, CUSTOM = range(8)
# states
ENABLED, DISABLED, HIDDEN = range(3)
# layouts
FREE, GRID, RUN, SCROLL_ROW, SCROLL_COL, STACK_H, STACK_V = range(7)
# axis modes
START, END, STRETCH, CENTER, FILL = range(5)
# clip
CLIP_INHERIT, CLIP_SELF, CLIP_SPILL = range(3)
# reveal
REVEAL_NONE, REVEAL_FADE, REVEAL_SLIDE, REVEAL_UNROLL = range(4)
# draw modes
DRAW_NONE, DRAW_STRETCH, DRAW_NATIVE, DRAW_NINESLICE, DRAW_TILE, DRAW_RUN = range(6)

CANON_43, CANON_WIDE = 0, 1


def lerp(a, b, t):
    return a + ((b - a) * t)


def pulse(t):
    if t <= 0.0 or t >= 1.0:
        return 0.0
    return 4.0 * t * (1.0 - t)


def clampf(v, lo, hi):
    return lo if v < lo else (hi if v > hi else v)


def smoothstep(t):
    """So2h_Ui_SmoothStep, carried over unchanged from so2h_quest_layout.c."""
    t = clampf(t, 0.0, 1.0)
    return t * t * (3.0 - (2.0 * t))


class Rect(object):
    __slots__ = ("x0", "y0", "x1", "y1")

    def __init__(self, x0=0.0, y0=0.0, x1=0.0, y1=0.0):
        self.x0, self.y0, self.x1, self.y1 = x0, y0, x1, y1

    @property
    def w(self):
        return self.x1 - self.x0

    @property
    def h(self):
        return self.y1 - self.y0

    def copy(self):
        return Rect(self.x0, self.y0, self.x1, self.y1)

    def intersects(self, o):
        return not (self.x1 <= o.x0 or o.x1 <= self.x0 or self.y1 <= o.y0 or o.y1 <= self.y0)

    def overlap_area(self, o):
        w = min(self.x1, o.x1) - max(self.x0, o.x0)
        h = min(self.y1, o.y1) - max(self.y0, o.y0)
        return (w * h) if (w > 0 and h > 0) else 0.0

    def inside(self, o, eps=0.01):
        return (self.x0 >= o.x0 - eps and self.y0 >= o.y0 - eps and
                self.x1 <= o.x1 + eps and self.y1 <= o.y1 + eps)

    def __repr__(self):
        return "Rect({:.2f},{:.2f},{:.2f},{:.2f})".format(self.x0, self.y0, self.x1, self.y1)


class Axis(object):
    __slots__ = ("mode", "a", "b")

    def __init__(self, mode=START, a=0.0, b=0.0):
        self.mode, self.a, self.b = mode, float(a), float(b)


class Style(object):
    __slots__ = ("sheet", "slice", "drawMode", "reveal", "rgb", "alpha", "layer")

    def __init__(self, sheet=INVALID, slice=INVALID, drawMode=DRAW_NONE, reveal=REVEAL_NONE,
                 rgb=(255, 255, 255), alpha=255, layer=0):
        self.sheet, self.slice, self.drawMode = sheet, slice, drawMode
        self.reveal, self.rgb, self.alpha, self.layer = reveal, rgb, alpha, layer


class Desc(object):
    """One descriptor row. Mirrors So2hUiDesc."""

    def __init__(self, id, parent, kind=PANEL, state=ENABLED, layout=FREE, clip=CLIP_INHERIT,
                 x=None, y=None, cols=0, rows=0, gap=0.0, pitch=0.0, count=0, growEnd=0,
                 style=None, cellState=None, nav=(INVALID, INVALID, INVALID, INVALID), name=""):
        self.id, self.parent, self.kind, self.state = id, parent, kind, state
        self.layout, self.clip = layout, clip
        self.x = x or Axis(FILL)
        self.y = y or Axis(FILL)
        self.cols, self.rows, self.gap = cols, rows, gap
        self.pitch, self.count, self.growEnd = pitch, count, growEnd
        self.style = style or Style()
        self.cellState = cellState
        self.navUp, self.navDown, self.navLeft, self.navRight = nav
        self.name = name or "node{}".format(id)


class Variant(object):
    def __init__(self, node, canon, x=None, y=None, state=None):
        self.node, self.canon, self.x, self.y, self.state = node, canon, x, y, state


class Node(object):
    __slots__ = ("rect", "canonRect", "clipRect", "alpha", "revealT", "state", "visible", "focusable")

    def __init__(self):
        self.rect = Rect()
        self.canonRect = [Rect(), Rect()]
        self.clipRect = Rect()
        self.alpha = 1.0
        self.revealT = 0.0
        self.state = ENABLED
        self.visible = True
        self.focusable = False


class RunState(object):
    def __init__(self):
        self.count = 0
        self.targetCount = 0
        self.gap = [0.0] * 16
        self.fade = 1.0
        self.seeded = False


class ScrollState(object):
    def __init__(self):
        self.offset = 0.0
        self.target = 0.0
        self.index = 0
        self.seeded = False


class Ctx(object):
    """Mirrors So2hUiCtx. One instance per simulated menu."""

    def __init__(self, desc, variants=(), screen_w=None, aspect=None):
        self.desc = list(desc)
        self.variants = list(variants)
        self.node = [Node() for _ in self.desc]
        self.run = {}
        self.scroll = {}
        self.page = {}
        self.stateOverride = [None] * len(self.desc)
        self.focus = INVALID
        self.showHiddenPages = False

        self.morphFrames = 0
        self.morphT = 0.0
        self.activeCanon = CANON_43
        self.tile = (DESIGN_H * TILE_SRC) / MOCK_H
        if self.tile < TILE_MIN:
            self.tile = TILE_MIN

        # Seed each PAGEGROUP with its first non-hidden page, as So2h_Ui_Init does.
        for i, d in enumerate(self.desc):
            if d.kind == PAGEGROUP:
                self.page[i] = {"active": INVALID, "cursor": INVALID}
        for i, d in enumerate(self.desc):
            if d.kind == PAGE and self.desc[d.parent].kind == PAGEGROUP:
                ps = self.page[d.parent]
                if ps["active"] == INVALID and d.state != HIDDEN:
                    ps["active"] = i

        self.set_screen(screen_w=screen_w, aspect=aspect)

    # -- screen -------------------------------------------------------------------------
    def set_screen(self, screen_w=None, aspect=None):
        """
        The C reads OTRGetRectDimensionFromLeftEdge / FromRightEdge. Those return the
        widescreen-extended rect space: height stays 240 and width grows with the aspect,
        centred on the 4:3 box. Reproduced exactly so screen x0 goes negative like it does
        in-game (about -53 at 16:9), which is the thing that makes a hardware scissor unusable
        and is therefore worth simulating rather than idealising away.
        """
        if screen_w is None:
            screen_w = DESIGN_H * (aspect if aspect else (4.0 / 3.0))
        self.screenW = float(screen_w)
        self.screenX0 = (DESIGN_W_43 - self.screenW) * 0.5
        self.screenX1 = self.screenX0 + self.screenW
        self.aspect = self.screenW / DESIGN_H

    # -- state --------------------------------------------------------------------------
    def resolved_state(self, i):
        ov = self.stateOverride[i]
        st = ov if ov is not None else self.desc[i].state
        if st == HIDDEN and self.showHiddenPages and self.desc[i].kind == PAGE:
            st = ENABLED
        return st

    def resolve_states(self):
        for i, d in enumerate(self.desc):
            state = self.resolved_state(i)
            if i > 0:
                pd = self.desc[d.parent]
                ps_state = self.node[d.parent].state
                if ps_state == HIDDEN:
                    state = HIDDEN
                elif ps_state == DISABLED:
                    state = DISABLED
                if d.kind == PAGE and pd.kind == PAGEGROUP:
                    ps = self.page.get(d.parent)
                    if ps is None or ps["active"] != i:
                        state = HIDDEN
            n = self.node[i]
            n.state = state
            n.visible = state != HIDDEN
            n.focusable = (state == ENABLED and d.kind not in (DECOR, GROUP, PAGEGROUP))

    # -- geometry -----------------------------------------------------------------------
    @staticmethod
    def solve_axis(axis, parentPos, parentSize):
        if axis.mode == END:
            size = axis.b
            pos = parentSize - axis.a - size
        elif axis.mode == STRETCH:
            pos = axis.a
            size = parentSize - axis.a - axis.b
        elif axis.mode == CENTER:
            size = axis.b
            pos = ((parentSize - size) * 0.5) + axis.a
        elif axis.mode == FILL:
            pos = 0.0
            size = parentSize
        else:
            pos = axis.a
            size = axis.b
        if size < 0.0:
            size = 0.0
        return parentPos + pos, size

    def find_variant(self, id, canon):
        for v in self.variants:
            if v.node == id and v.canon == canon:
                return v
        return None

    def solve_canon(self, canon):
        designW = DESIGN_W_WIDE if canon == CANON_WIDE else DESIGN_W_43
        sx = self.screenW / designW
        sy = 1.0
        for i, d in enumerate(self.desc):
            out = self.node[i].canonRect[canon]
            if i == 0:
                out.x0, out.y0, out.x1, out.y1 = self.screenX0, 0.0, self.screenX1, DESIGN_H
                continue
            if self.node[i].state == HIDDEN:
                out.x0 = out.y0 = out.x1 = out.y1 = 0.0
                continue
            v = self.find_variant(i, canon)
            ax = v.x if (v and v.x) else d.x
            ay = v.y if (v and v.y) else d.y
            pr = self.node[d.parent].canonRect[canon]
            x, w = self.solve_axis(ax, pr.x0 / sx, pr.w / sx)
            y, h = self.solve_axis(ay, pr.y0 / sy, pr.h / sy)
            out.x0, out.y0 = x * sx, y * sy
            out.x1, out.y1 = (x + w) * sx, (y + h) * sy

    def resolve_clips(self):
        for i, d in enumerate(self.desc):
            n = self.node[i]
            if i == 0:
                n.clipRect = n.rect.copy()
                continue
            inh = self.node[d.parent].clipRect
            if d.clip == CLIP_SELF:
                n.clipRect = Rect(max(n.rect.x0, inh.x0), max(n.rect.y0, inh.y0),
                                  min(n.rect.x1, inh.x1), min(n.rect.y1, inh.y1))
            elif d.clip == CLIP_SPILL:
                n.clipRect = self.node[0].rect.copy()
            else:
                n.clipRect = inh.copy()
            if n.clipRect.x1 < n.clipRect.x0:
                n.clipRect.x1 = n.clipRect.x0
            if n.clipRect.y1 < n.clipRect.y0:
                n.clipRect.y1 = n.clipRect.y0

    def advance_reveals(self):
        for i, d in enumerate(self.desc):
            n = self.node[i]
            base = (d.style.alpha / 255.0) if d.style.alpha else 1.0
            if not n.visible:
                n.revealT = 0.0
                n.alpha = 0.0
                continue
            if d.style.reveal == REVEAL_NONE:
                n.revealT = 1.0
            elif n.revealT < 1.0:
                n.revealT = min(1.0, n.revealT + (1.0 / MORPH_FRAMES))
            n.alpha = base
            if d.style.reveal == REVEAL_FADE:
                n.alpha *= smoothstep(n.revealT)
            if n.state == DISABLED:
                n.alpha *= 0.45

    # -- run / scroll --------------------------------------------------------------------
    def solve_run(self, i, span, pitch):
        d = self.desc[i]
        st = self.run.setdefault(i, RunState())
        if pitch <= 0.0:
            return
        maxTiles = len(st.gap)
        want = d.cols if d.cols > 0 else int(span / pitch)
        want = max(1, min(want, maxTiles))

        if not st.seeded:
            st.count = want
            st.targetCount = want
            st.gap = [0.0] * maxTiles
            st.fade = 1.0
            st.seeded = True

        st.targetCount = want
        if st.count < st.targetCount:
            st.count = st.targetCount
            st.fade = 1.0

        deficit = max(0.0, (st.count * pitch) - span)
        remaining = deficit
        maxOverlap = pitch * RUN_MAX_OVERLAP

        for k in range(st.count - 1, -1, -1):
            target = min(remaining, maxOverlap)
            if k < (st.count - 1) and maxOverlap > 0.0:
                if (st.gap[k + 1] / maxOverlap) < RUN_STAGGER:
                    target = st.gap[k]
            delta = target - st.gap[k]
            step = delta * RUN_EASE
            if -RUN_MIN_STEP < step < RUN_MIN_STEP:
                step = delta
            st.gap[k] = clampf(st.gap[k] + step, 0.0, maxOverlap)
            remaining = max(0.0, remaining - st.gap[k])

        if remaining > 0.0 and st.count > 1:
            st.fade -= 0.25
            if st.fade <= 0.0:
                st.count -= 1
                st.fade = 1.0
        else:
            st.fade = 1.0

    def solve_scroll(self, i, span, pitch):
        d = self.desc[i]
        st = self.scroll.setdefault(i, ScrollState())
        if pitch <= 0.0:
            return
        visible = span / pitch
        maxOffset = max(0.0, d.count - visible)

        idx = float(st.index)
        if idx < st.target:
            st.target = idx
        elif idx > ((st.target + visible) - 1.0):
            st.target = (idx - visible) + 1.0
        st.target = clampf(st.target, 0.0, maxOffset)

        if not st.seeded:
            st.offset = st.target
            st.seeded = True
            return
        delta = st.target - st.offset
        if -SCROLL_MIN_STEP < delta < SCROLL_MIN_STEP:
            st.offset = st.target
        else:
            st.offset += delta * SCROLL_EASE

    def cell_rect(self, parent, index):
        d = self.desc[parent]
        rect = self.node[parent].rect
        out = Rect()

        if d.layout == GRID:
            if d.cols <= 0 or d.rows <= 0 or index < 0 or index >= d.cols * d.rows:
                return out
            gap = d.gap * (self.tile / TILE_SRC)
            cw = (rect.w - (gap * (d.cols - 1))) / d.cols
            ch = (rect.h - (gap * (d.rows - 1))) / d.rows
            c, r = index % d.cols, index // d.cols
            out.x0 = rect.x0 + (c * (cw + gap))
            out.y0 = rect.y0 + (r * (ch + gap))
            out.x1, out.y1 = out.x0 + cw, out.y0 + ch
            return out

        if d.layout in (SCROLL_ROW, SCROLL_COL):
            st = self.scroll.get(parent)
            pitch = d.pitch * (self.tile / TILE_SRC)
            if st is None or pitch <= 0.0 or index < 0 or index >= d.count:
                return out
            vertical = d.layout == SCROLL_COL
            if d.growEnd == 1:
                lead = (st.offset - index) * pitch
            else:
                lead = (index - st.offset) * pitch
            if vertical:
                out.x0, out.x1 = rect.x0, rect.x1
                if d.growEnd == 1:
                    out.y1 = rect.y1 + lead
                    out.y0 = out.y1 - pitch
                else:
                    out.y0 = rect.y0 + lead
                    out.y1 = out.y0 + pitch
            else:
                out.y0, out.y1 = rect.y0, rect.y1
                if d.growEnd == 1:
                    out.x1 = rect.x1 + lead
                    out.x0 = out.x1 - pitch
                else:
                    out.x0 = rect.x0 + lead
                    out.x1 = out.x0 + pitch
            return out

        if d.layout == RUN:
            st = self.run.get(parent)
            pitch = d.pitch * (self.tile / TILE_SRC)
            if st is None or pitch <= 0.0 or index < 0 or index >= st.count:
                return out
            offset = 0.0
            for k in range(index):
                offset += pitch - st.gap[k]
            if d.growEnd == 1:
                out.x1 = rect.x1 - offset
                out.x0 = out.x1 - pitch
            else:
                out.x0 = rect.x0 + offset
                out.x1 = out.x0 + pitch
            out.y0, out.y1 = rect.y0, rect.y1
            return out

        return out

    # -- the frame --------------------------------------------------------------------
    def update(self):
        """One frame. Mirrors So2h_UiLayout_Update exactly, including the order."""
        if self.aspect >= ASPECT_WIDE:
            self.activeCanon = CANON_WIDE
        elif self.aspect <= ASPECT_43:
            self.activeCanon = CANON_43

        if self.activeCanon == CANON_WIDE:
            self.morphFrames = min(MORPH_FRAMES, self.morphFrames + 1)
        else:
            self.morphFrames = max(0, self.morphFrames - 1)

        t = self.morphFrames / float(MORPH_FRAMES)
        self.morphT = smoothstep(t)
        p = pulse(t) * OVERSHOOT

        self.resolve_states()
        self.solve_canon(CANON_43)
        self.solve_canon(CANON_WIDE)

        for i in range(len(self.desc)):
            n = self.node[i]
            a, b = n.canonRect[CANON_43], n.canonRect[CANON_WIDE]
            n.rect.x0 = lerp(a.x0, b.x0, self.morphT)
            n.rect.y0 = lerp(a.y0, b.y0, self.morphT)
            n.rect.x1 = lerp(a.x1, b.x1, self.morphT)
            n.rect.y1 = lerp(a.y1, b.y1, self.morphT)
            if p > 0.0:
                cx, cy = (n.rect.x0 + n.rect.x1) * 0.5, (n.rect.y0 + n.rect.y1) * 0.5
                hw = n.rect.w * 0.5 * (1.0 + p)
                hh = n.rect.h * 0.5 * (1.0 - p)
                n.rect.x0, n.rect.x1 = cx - hw, cx + hw
                n.rect.y0, n.rect.y1 = cy - hh, cy + hh

        self.resolve_clips()

        for i, d in enumerate(self.desc):
            if not self.node[i].visible:
                continue
            pitch = d.pitch * (self.tile / TILE_SRC)
            if d.layout == RUN:
                self.solve_run(i, self.node[i].rect.w, pitch)
            elif d.layout == SCROLL_ROW:
                self.solve_scroll(i, self.node[i].rect.w, pitch)
            elif d.layout == SCROLL_COL:
                self.solve_scroll(i, self.node[i].rect.h, pitch)

        self.advance_reveals()

    def settle(self, frames=240):
        for _ in range(frames):
            self.update()


# ---------------------------------------------------------------------------------------
# The frame-axis splitter from so2h_ui_draw.c. Shared by the sim (invariants) and the
# renderer (actual pixels), so both agree on where a seam lands.
# ---------------------------------------------------------------------------------------
def solve_frame_axis(n, lo, hi, cell, span, repeat, maxOut=FRAME_MAX_CELLS):
    """Returns a list of (pos, size, cellIndex)."""
    out = []
    if n <= 0 or cell <= 0.0 or maxOut <= 0:
        return out
    if lo < 0 or hi < lo or hi >= n:
        lo, hi = -1, -2

    midCount = (hi - lo + 1) if hi >= lo else 0
    fixedCount = n - midCount
    fixed = fixedCount * cell
    scale = 1.0
    if fixed > span and fixed > 0.0:
        scale = span / fixed
        fixed = span
    slack = max(0.0, span - fixed)

    cursor = 0.0
    i = 0
    while i < n:
        isMid = lo <= i <= hi
        if not isMid:
            if len(out) >= maxOut:
                break
            size = cell * scale
            out.append((cursor, size, i))
            cursor += size
            i += 1
            continue
        if i != lo:
            i += 1
            continue
        if not repeat:
            each = slack / midCount
            for k in range(midCount):
                if len(out) >= maxOut:
                    break
                out.append((cursor, each, lo + k))
                cursor += each
        else:
            remaining = slack
            k = 0
            while remaining > MIN_QUAD_PX and len(out) < maxOut and k < MAX_REPEATS:
                w = min(remaining, cell)
                out.append((cursor, w, lo + (k % midCount)))
                cursor += w
                remaining -= w
                k += 1
        i = hi + 1
    return out
