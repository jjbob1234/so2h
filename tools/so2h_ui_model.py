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

import math

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

# ---------------------------------------------------------------------------------------
# The open / close performance. Every number the animation has lives in this block.
# ---------------------------------------------------------------------------------------
REVEAL_FRAMES = 22       # how long one window's entrance takes
GROW_FROM = 0.10         # a GROW window starts at this fraction of its solved height
REVEAL_JITTER = 6        # max extra frames of per-node desync, drawn per open
# POP: arrive early, sail a little PAST the resting place, then snap back onto it.
POP_SETTLE = 0.65        # fraction of the entrance spent travelling; the rest is the pop
POP_OVER = 6.0           # design units it overshoots by - ABSOLUTE, not a share of travel,
                         # so a node that enters from off-screen still only pops "a little"
# The bulge is pulse(u*u), not a sine: squaring u leans the peak late (it tops out at
# u = 0.707), so the node drifts out and snaps back fast, and the overlay needs no sinf.
# Close: everything gathers UPWARD for a beat, then the floor drops out.
CLOSE_RISE_F = 6         # frames of build-up
CLOSE_RISE_U = 5.0       # design units it rises during the build-up
CLOSE_GRAVITY = 2.6      # units per frame^2 once it lets go
CLOSE_JITTER = 4         # max frames of per-node desync on the way out
CLOSE_SQUASH = 0.55      # vertical stretch ceiling at terminal speed
CLOSE_PINCH = 0.30       # how much of that stretch is paid back as horizontal squeeze


def hash_u32(x):
    """Deterministic per-node noise. Mirrors So2h_UiHash - a fixed integer scramble, NOT
    rand(), so a frame can be re-solved as many times as it likes and the jitter a node was
    dealt this open does not change underneath it."""
    x = (x * 2654435761) & 0xFFFFFFFF
    x ^= x >> 15
    x = (x * 2246822519) & 0xFFFFFFFF
    x ^= x >> 13
    return x
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
(REVEAL_NONE, REVEAL_FADE, REVEAL_SLIDE, REVEAL_UNROLL, REVEAL_SWAP,
 REVEAL_GROW, REVEAL_FALL, REVEAL_POP) = range(8)
# reveal entry edge. AUTO derives it: a node leaves and returns through the screen edge it
# is already nearest to, so a window that lives bottom-left enters from the bottom-left with
# no per-node direction authored anywhere.
FROM_AUTO, FROM_LEFT, FROM_RIGHT, FROM_TOP, FROM_BOTTOM = range(5)
# draw modes
DRAW_NONE, DRAW_STRETCH, DRAW_NATIVE, DRAW_NINESLICE, DRAW_TILE, DRAW_RUN, DRAW_RING = range(7)
# DRAW_RING: NINESLICE/RUN with every cell inside the outer border ring skipped, so the
# frame has a real hole where its fill would be. Mirrors SO2H_UI_DRAW_RING in so2h_ui.h.

# Which ring of the parent frame a child solves against. Mirrors So2hUiAttach.
ATTACH_CONTENT, ATTACH_BORDER, ATTACH_EDGE = range(3)

# How a frame fills space bigger than its art. Mirrors So2hUiGrow. Lives on the SHEET, not
# the call site - these interiors are self-tiling, so stretching them smears the texture.
GROW_RUN, GROW_STRETCH = range(2)

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


def fall_ease(t):
    """Gravity, not easing: accelerate all the way down, land, then a single small bounce.
    Mirrors So2h_Ui_FallEase. Used by REVEAL_FALL so slots drop rather than glide."""
    t = clampf(t, 0.0, 1.0)
    if t <= 0.82:
        u = t / 0.82
        return u * u
    u = (t - 0.82) / 0.18
    return 1.0 - (0.07 * (4.0 * u * (1.0 - u)))


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
    __slots__ = ("sheet", "slice", "drawMode", "reveal", "rgb", "alpha", "layer", "scale",
                 "shadow", "revealFrom", "revealDelay", "revealJitter")

    def __init__(self, sheet=INVALID, slice=INVALID, drawMode=DRAW_NONE, reveal=REVEAL_NONE,
                 rgb=(255, 255, 255), alpha=255, layer=0, scale=1.0, shadow=True,
                 revealFrom=FROM_AUTO, revealDelay=0, revealJitter=REVEAL_JITTER):
        self.sheet, self.slice, self.drawMode = sheet, slice, drawMode
        self.reveal, self.rgb, self.alpha, self.layer = reveal, rgb, alpha, layer
        # Per-node art scale. The collage puts the same sheets on screen at different
        # sizes; a frame's TILE gets this big, so a scaled window keeps its border art in
        # proportion instead of stretching. Rings scale with it or content drifts.
        self.scale = float(scale)
        # Programmatic drop shadow: DEFAULT ON, opted out per node. Mirrors
        # So2hUiStyle.noShadow (inverted here so the scene reads shadow=False).
        self.shadow = bool(shadow)
        # Entry edge and stagger, both optional. The stagger is in frames and is the ONLY
        # thing that orders the open sequence: no timeline, no keyframes, no per-window code.
        self.revealFrom = revealFrom
        self.revealDelay = int(revealDelay)
        # Per-node desync, in frames, drawn from the open's seed. Default is ON: no two opens
        # land in the same order, so the menu never feels like a canned cutscene.
        self.revealJitter = int(revealJitter)


class Desc(object):
    """One descriptor row. Mirrors So2hUiDesc."""

    def __init__(self, id, parent, kind=PANEL, state=ENABLED, layout=FREE, clip=CLIP_INHERIT,
                 x=None, y=None, cols=0, rows=0, gap=0.0, pitch=0.0, count=0, growEnd=0,
                 style=None, cellState=None, nav=(INVALID, INVALID, INVALID, INVALID), name="",
                 attach=0):
        self.id, self.parent, self.kind, self.state = id, parent, kind, state
        self.layout, self.clip = layout, clip
        self.x = x or Axis(FILL)
        self.y = y or Axis(FILL)
        self.cols, self.rows, self.gap = cols, rows, gap
        self.pitch, self.count, self.growEnd = pitch, count, growEnd
        self.attach = attach   # 0 CONTENT, 1 BORDER, 2 EDGE
        self.style = style or Style()
        self.cellState = cellState
        self.navUp, self.navDown, self.navLeft, self.navRight = nav
        self.name = name or "node{}".format(id)


class Variant(object):
    def __init__(self, node, canon, x=None, y=None, state=None):
        self.node, self.canon, self.x, self.y, self.state = node, canon, x, y, state


class Node(object):
    __slots__ = ("rect", "canonRect", "clipRect", "alpha", "revealT", "state", "visible",
                 "focusable", "swapT", "swapOut", "swapPending", "swapApply", "revealHold",
                 "closeF")

    def __init__(self):
        self.rect = Rect()
        self.canonRect = [Rect(), Rect()]
        self.clipRect = Rect()
        self.alpha = 1.0
        self.revealT = 0.0
        self.revealHold = 0
        self.closeF = -1        # frames into this node's exit; -1 = not closing
        self.swapT = 1.0
        self.swapOut = False
        self.swapPending = False
        self.swapApply = None
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

        # One seed per open. Every node's jitter is hash_u32(node ^ openSeed), so the desync
        # is unique per open but stable inside one open no matter how often we re-solve.
        self.openSeed = 1
        self.closing = False
        self.closeFrames = 0

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

    # Ring table, injected by the renderer from sheets.json: name -> (borderTiles,
    # contentTiles, unit). Rings are WHOLE TILES - a 5x5 @64 frame is an outer border ring,
    # an inner border ring, then a centre cell that tiles the fill, so it is (1, 2, 64).
    # Empty in a bare sim, which then behaves like every ring is zero.
    rings = {}

    def ring_px(self, sheet, attach, scale=1.0):
        """Ring inset in design units at the current tile scale. Mirrors So2h_UiSheet_RingPx."""
        if attach == ATTACH_EDGE:
            return (0.0, 0.0, 0.0, 0.0)
        e = self.rings.get(sheet)
        if not e:
            return (0.0, 0.0, 0.0, 0.0)
        borderT, contentT, unit = e
        n = borderT if attach == ATTACH_BORDER else contentT
        # tile/TILE_SRC, not tile/unit - this is the same scalar draw_frame sizes a cell
        # with. Dividing by the sheet's own unit would halve the rings on a 64-texel sheet
        # while its tiles drew at full size.
        v = n * unit * (self.tile / TILE_SRC) * scale
        return (v, v, v, v)

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

            # Solve against the parent's attached ring. Mirrors So2h_UiSolveCanon: a framed
            # parent owns both border rings, so a child that ignores them lands under chrome.
            bl = bt = br = bb = 0.0
            pstyle = self.desc[d.parent].style
            if pstyle.drawMode in (DRAW_NINESLICE, DRAW_RUN, DRAW_TILE, DRAW_RING):
                bl, bt, br, bb = self.ring_px(pstyle.sheet, getattr(d, "attach", 0),
                                              getattr(pstyle, "scale", 1.0))
            px_, py_ = pr.x0 + bl, pr.y0 + bt
            pw_, ph_ = pr.w - bl - br, pr.h - bt - bb
            if pw_ < 0.0:
                px_ += pw_ * 0.5
                pw_ = 0.0
            if ph_ < 0.0:
                py_ += ph_ * 0.5
                ph_ = 0.0

            x, w = self.solve_axis(ax, px_ / sx, pw_ / sx)
            y, h = self.solve_axis(ay, py_ / sy, ph_ / sy)
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

    def reveal_delay(self, i):
        """Table delay plus this open's random slack for this node. The table sets the
        CHOREOGRAPHY (which window is early, which is late); the jitter makes sure no two
        opens are ever bar-for-bar identical. Mirrors So2h_UiRevealDelay."""
        st = self.desc[i].style
        j = st.revealJitter
        if j <= 0:
            return st.revealDelay
        return st.revealDelay + (hash_u32(i ^ self.openSeed) % (j + 1))

    def close_delay(self, i):
        """Same idea on the way out, but tighter - a beat of slop, not a second stagger."""
        if CLOSE_JITTER <= 0:
            return 0
        return hash_u32((i * 7919) ^ (self.openSeed + 0x9E3779B9)) % (CLOSE_JITTER + 1)

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
                # The stagger is spent BEFORE the node starts moving, so every window shares
                # one clock and the sequence is just a column of delay numbers in the table.
                if n.revealHold < self.reveal_delay(i):
                    n.revealHold += 1
                else:
                    n.revealT = min(1.0, n.revealT + (1.0 / REVEAL_FRAMES))
            n.alpha = base
            if d.style.reveal == REVEAL_FADE:
                n.alpha *= smoothstep(n.revealT)
            if n.state == DISABLED:
                n.alpha *= 0.45

    # -- slide-on ------------------------------------------------------------------------
    def slide_edge(self, i):
        """The screen edge a SLIDE node enters through, and how far off it has to start.

        AUTO is DERIVED from the solved rect: whichever screen edge the node's own centre is
        closest to. Distance is measured from the rect, so a node only ever travels exactly
        far enough to be fully hidden - no baked travel, and it holds at any aspect.
        """
        d, r = self.desc[i], self.node[i].rect
        e = d.style.revealFrom
        if d.style.reveal == REVEAL_FALL:
            e = FROM_TOP     # a thing that falls comes from above. Never derived.
        if e == FROM_AUTO:
            cx, cy = (r.x0 + r.x1) * 0.5, (r.y0 + r.y1) * 0.5
            dl, dr = cx - self.screenX0, self.screenX1 - cx
            dt, db = cy, DESIGN_H - cy
            m = min(dl, dr, dt, db)
            e = (FROM_LEFT if m == dl else FROM_RIGHT if m == dr else
                 FROM_TOP if m == dt else FROM_BOTTOM)
        if e == FROM_LEFT:
            return (-(r.x1 - self.screenX0), 0.0)
        if e == FROM_RIGHT:
            return (self.screenX1 - r.x0, 0.0)
        if e == FROM_TOP:
            return (0.0, -r.y1)
        return (0.0, DESIGN_H - r.y0)

    def advance_slides(self):
        """SLIDE, FALL and POP are the same displacement with a different curve: SLIDE eases
        in and out, FALL accelerates and bounces once, POP arrives early and overshoots.
        All three derive travel from the rect, so none of them bakes a distance."""
        for i, d in enumerate(self.desc):
            rv = d.style.reveal
            if rv != REVEAL_SLIDE and rv != REVEAL_FALL and rv != REVEAL_POP:
                continue
            n = self.node[i]
            if n.revealT >= 1.0:
                continue
            dx, dy = self.slide_edge(i)
            t = n.revealT
            over = 0.0
            if rv == REVEAL_FALL:
                k = 1.0 - fall_ease(t)
            elif rv == REVEAL_POP:
                # The travel finishes EARLY (at POP_SETTLE), and the frames left over are
                # spent past the target: one bulge out to POP_OVER and back to exactly 0.
                k = 1.0 - smoothstep(min(1.0, t / POP_SETTLE))
                if t > POP_SETTLE:
                    u = (t - POP_SETTLE) / (1.0 - POP_SETTLE)
                    over = -POP_OVER * pulse(u * u)
            else:
                k = 1.0 - smoothstep(t)
            if over != 0.0:
                # Overshoot runs along the SAME axis the node entered on, so it is derived
                # from the travel vector and needs no per-node direction of its own.
                L = math.hypot(dx, dy) or 1.0
                self.offset_subtree(i, (dx * k) + (over * dx / L),
                                    (dy * k) + (over * dy / L))
            else:
                self.offset_subtree(i, dx * k, dy * k)

    # -- grow-open -----------------------------------------------------------------------
    def advance_grows(self):
        """REVEAL_GROW: the node starts as a short letterbox pinned at its own TOP edge and
        opens downward to its solved height. Everything inside it is scaled with it, so a
        window declares nothing extra to animate its contents."""
        for i, d in enumerate(self.desc):
            if d.style.reveal != REVEAL_GROW:
                continue
            n = self.node[i]
            if n.revealT >= 1.0:
                continue
            k = GROW_FROM + ((1.0 - GROW_FROM) * smoothstep(n.revealT))
            self.stretch_subtree(i, 1.0, k, n.rect.x0, n.rect.y0)

    def stretch_subtree(self, root, sx, sy, px, py):
        """Scale a node and its descendants about the pivot (px, py) in design units."""
        moved = {root}
        for j in range(root, len(self.desc)):
            if j == root or self.desc[j].parent in moved:
                moved.add(j)
                for r in (self.node[j].rect, self.node[j].clipRect):
                    r.x0 = px + ((r.x0 - px) * sx)
                    r.x1 = px + ((r.x1 - px) * sx)
                    r.y0 = py + ((r.y0 - py) * sy)
                    r.y1 = py + ((r.y1 - py) * sy)

    # -- the exit ------------------------------------------------------------------------
    def open(self):
        """Restart the entrance with a fresh desync seed. Mirrors So2h_Ui_Open."""
        self.closing = False
        self.closeFrames = 0
        self.openSeed = hash_u32(self.openSeed + 0x2545F491) or 1
        for n in self.node:
            n.revealT = 0.0
            n.revealHold = 0
            n.closeF = -1
            # A SWAP node opens the same way it comes back from a swap: up off the bottom.
            n.swapT = 0.0
            n.swapOut = False
            n.swapPending = False

    def close(self):
        """Mirrors So2h_Ui_Close. The close is not the open played backwards: everything
        gathers UPWARD for a few frames of held breath, then the floor drops out and each
        node falls under gravity, stretching as it picks up speed."""
        self.closing = True
        self.closeFrames = 0
        for n in self.node:
            n.closeF = 0

    def close_done(self):
        if not self.closing:
            return False
        for i, n in enumerate(self.node):
            if not n.visible:
                continue
            f = n.closeF - self.close_delay(i) - CLOSE_RISE_F
            if f < 0:
                return False
            if (0.5 * CLOSE_GRAVITY * f * f) < (DESIGN_H + 40.0):
                return False
        return True

    def advance_close(self):
        if not self.closing:
            return
        self.closeFrames += 1
        for i, d in enumerate(self.desc):
            n = self.node[i]
            if n.closeF < 0:
                continue
            n.closeF += 1
            f = n.closeF - self.close_delay(i)
            if f <= 0:
                continue
            if f <= CLOSE_RISE_F:
                # build-up: rise, easing OUT so it looks like it is being pulled up short
                u = smoothstep(f / float(CLOSE_RISE_F))
                self.offset_subtree(i, 0.0, -CLOSE_RISE_U * u)
                continue
            g = f - CLOSE_RISE_F
            dy = -CLOSE_RISE_U + (0.5 * CLOSE_GRAVITY * g * g)
            v = CLOSE_GRAVITY * g
            sy = 1.0 + min(CLOSE_SQUASH, v * 0.03)
            # Only a FRACTION of the vertical stretch is paid back horizontally: a strict
            # 1/sy conserves area but turns a wide window into a noodle, which reads as a
            # bug rather than as speed.
            sx = 1.0 / (1.0 + (CLOSE_PINCH * (sy - 1.0)))
            cx = (n.rect.x0 + n.rect.x1) * 0.5
            cy = (n.rect.y0 + n.rect.y1) * 0.5
            self.stretch_subtree(i, sx, sy, cx, cy)
            self.offset_subtree(i, 0.0, dy)

    # -- off-screen swap -----------------------------------------------------------------
    def swap(self, i, apply=None):
        """Ask a REVEAL_SWAP node to change content off-screen. Mirrors So2h_Ui_Swap."""
        if self.desc[i].style.reveal != REVEAL_SWAP:
            if apply:
                apply(i)
            return False
        n = self.node[i]
        n.swapPending = True
        n.swapApply = apply
        n.swapOut = True
        return True

    def advance_swaps(self):
        """Drives REVEAL_SWAP nodes off the bottom of the screen and back.

        The displacement is DERIVED from the solved rect (how far this node's top is from
        the screen bottom), never from a baked travel distance - so a node that comes back
        twice as tall still ends up fully hidden on the way out, and a node whose rect the
        solver moved for a different aspect needs no new numbers.
        """
        for i, d in enumerate(self.desc):
            if d.style.reveal != REVEAL_SWAP:
                continue
            n = self.node[i]
            if n.swapOut:
                n.swapT = max(0.0, n.swapT - (1.0 / MORPH_FRAMES))
                if n.swapT <= 0.0:
                    # Fully off-screen: this is the ONLY moment content is allowed to change.
                    fn = getattr(n, "swapApply", None)
                    if n.swapPending and fn:
                        fn(i)
                    n.swapPending = False
                    n.swapOut = False
            elif n.swapT < 1.0:
                # On the way IN, a SWAP node is also part of the opening performance, so its
                # rise waits out the same revealDelay + jitter every other window does. That
                # is why the context menu can be given its own beat without a second
                # mechanism: the entrance and the content swap are the same move.
                if n.revealHold >= self.reveal_delay(i):
                    n.swapT = min(1.0, n.swapT + (1.0 / MORPH_FRAMES))
            if n.swapT < 1.0:
                dy = (1.0 - smoothstep(n.swapT)) * (DESIGN_H - n.rect.y0)
                self.offset_subtree(i, 0.0, dy)

    def offset_subtree(self, root, dx, dy):
        moved = {root}
        for j in range(root, len(self.desc)):
            if j == root or self.desc[j].parent in moved:
                moved.add(j)
                r = self.node[j].rect
                r.x0 += dx; r.x1 += dx; r.y0 += dy; r.y1 += dy
                c = self.node[j].clipRect
                c.x0 += dx; c.x1 += dx; c.y0 += dy; c.y1 += dy

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
        self.advance_slides()
        self.advance_grows()
        self.advance_swaps()
        self.advance_close()

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
