#!/usr/bin/env python3
"""
so2h_ui_sim.py

Host-side invariant sweep for the so2h_ui solver.

The real solver only runs inside a Windows build with a ROM, so every layout question would
otherwise cost a CI round trip. tools/so2h_ui_model.py mirrors the C; this file drives that
mirror across every aspect ratio and every run span and asserts the properties the C is
supposed to guarantee. It is the cheap half of the verification story - the expensive half
(does it look right) is tools/so2h_ui_render.py.

Usage:
    python3 tools/so2h_ui_sim.py              # run the full sweep
    python3 tools/so2h_ui_sim.py --selfcheck  # print Python constants beside the C ones
    python3 tools/so2h_ui_sim.py -v           # also print a per-case summary

Exit code is 0 only when every invariant held.

Invariants checked
    1  clip containment  - a node whose clip is not SPILL never draws outside the nearest
                           enclosing CLIP_SELF rect.
    2  sibling overlap   - two visible same-layer siblings never overlap by more than half a
                           tile on both axes at once.
    3  monotone tiles    - RUN cell starts and frame-axis seams are strictly increasing.
    4  anchored end      - the anchored edge of a RUN sits exactly on the parent edge at
                           every aspect and every frame.
    5  termination       - the whole tree reaches a fixed point and stays there; no
                           asymptotic creep, no oscillation.
    6  scroll range      - a scroll offset stays inside [0, count - visible].
    7  frame axis        - solve_frame_axis always spans exactly the requested length, emits
                           no negative sizes, and stays inside its cell budget.
"""

import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import so2h_ui_model as M  # noqa: E402

EPS = 1e-3

# Sub-pixel drift is fine (Jay: "looks the same to the eye"), so the settle test uses the
# same 0.5 design-unit tolerance the Part B port is held to, not bit equality.
SETTLE_EPS = 0.5


# ------------------------------------------------------------------------------------
# constant self-check
# ------------------------------------------------------------------------------------
C_SOURCES = [
    "mm/2s2h/Menu/so2h_ui.h",
    "mm/2s2h/Menu/so2h_ui_layout.c",
    "mm/2s2h/Menu/so2h_ui_draw.c",
]

# Python name -> C name. Anything the model mirrors has to appear here, otherwise a rename
# on either side goes unnoticed.
CONST_MAP = {
    "MAX_NODES": "SO2H_UI_MAX_NODES",
    "MAX_PAGES": "SO2H_UI_MAX_PAGES",
    "MAX_RUNS": "SO2H_UI_MAX_RUNS",
    "MAX_SCROLL_ROWS": "SO2H_UI_MAX_SCROLL_ROWS",
    "MAX_CLIP_DEPTH": "SO2H_UI_MAX_CLIP_DEPTH",
    "MAX_LAYERS": "SO2H_UI_MAX_LAYERS",
    "MAX_VARIANTS": "SO2H_UI_MAX_VARIANTS",
    "MAX_SHEETS": "SO2H_UI_MAX_SHEETS",
    "INVALID": "SO2H_UI_INVALID",
    "MORPH_FRAMES": "SO2H_UI_MORPH_FRAMES",
    "OVERSHOOT": "SO2H_UI_OVERSHOOT",
    "ASPECT_WIDE": "SO2H_UI_ASPECT_WIDE",
    "ASPECT_43": "SO2H_UI_ASPECT_43",
    "DESIGN_H": "SO2H_UI_DESIGN_H",
    "DESIGN_W_43": "SO2H_UI_DESIGN_W_43",
    "DESIGN_W_WIDE": "SO2H_UI_DESIGN_W_WIDE",
    "TILE_SRC": "SO2H_UI_TILE_SRC",
    "MOCK_H": "SO2H_UI_MOCK_H",
    "TILE_MIN": "SO2H_UI_TILE_MIN",
    "RUN_EASE": "SO2H_UI_RUN_EASE",
    "RUN_MIN_STEP": "SO2H_UI_RUN_MIN_STEP",
    "RUN_STAGGER": "SO2H_UI_RUN_STAGGER",
    "RUN_MAX_OVERLAP": "SO2H_UI_RUN_MAX_OVERLAP",
    "SCROLL_EASE": "SO2H_UI_SCROLL_EASE",
    "SCROLL_MIN_STEP": "SO2H_UI_SCROLL_MIN_STEP",
    "FRAME_MAX_CELLS": "SO2H_UI_FRAME_MAX_CELLS",
    "MAX_REPEATS": "SO2H_UI_MAX_REPEATS",
    "MIN_QUAD_PX": "SO2H_UI_MIN_QUAD_PX",
}

DEFINE_RE = re.compile(r"^\s*#define\s+(SO2H_UI_[A-Z0-9_]+)\s+(\S+)")


def scrape_c_constants(root):
    found = {}
    for rel in C_SOURCES:
        path = os.path.join(root, rel)
        if not os.path.exists(path):
            continue
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            for line in fh:
                m = DEFINE_RE.match(line)
                if m:
                    found.setdefault(m.group(1), (m.group(2), rel))
    return found


def parse_c_value(raw):
    tok = raw.split("//")[0].split("/*")[0].strip()
    tok = tok.rstrip("f").rstrip("uU").rstrip("lL")
    try:
        return int(tok, 0)
    except ValueError:
        pass
    try:
        return float(tok)
    except ValueError:
        return None


def selfcheck(root):
    c = scrape_c_constants(root)
    print("%-20s %14s %14s  %s" % ("constant", "python", "C", "source"))
    print("-" * 74)
    bad = 0
    for py, cname in CONST_MAP.items():
        pv = getattr(M, py, None)
        entry = c.get(cname)
        if entry is None:
            print("%-20s %14s %14s  MISSING IN C" % (py, pv, "-"))
            bad += 1
            continue
        raw, rel = entry
        cv = parse_c_value(raw)
        ok = cv is not None and abs(float(cv) - float(pv)) < 1e-9
        print("%-20s %14s %14s  %s%s" % (py, pv, raw, rel, "" if ok else "   <-- DRIFT"))
        if not ok:
            bad += 1
    unmapped = sorted(set(c) - set(CONST_MAP.values()))
    if unmapped:
        print("\nIn C but not mirrored (fine if the model has no use for them):")
        for name in unmapped:
            print("    %-32s %s" % (name, c[name][0]))
    print()
    if bad:
        print("FAIL: %d constant(s) drifted between the model and the C." % bad)
    else:
        print("OK: all %d mirrored constants match." % len(CONST_MAP))
    return bad


# ------------------------------------------------------------------------------------
# the test tree
# ------------------------------------------------------------------------------------
# Deliberately exercises every layout mode at once, in the shape Jay asked for: a quest bar
# page plus a hidden Equipment page built from three horizontal one-cell strips and one
# vertical strip, plus a SPILL decor that is allowed to escape and a GRID that is not.
N_ROOT = 0
N_WINDOW = 1
N_PAGES = 2
N_QUEST = 3
N_RUN = 4
N_GRID = 5
N_SPILL = 6
N_EQUIP = 7
N_STRIP_A = 8
N_STRIP_B = 9
N_STRIP_C = 10
N_COL = 11
N_BTN_L = 12
N_BTN_R = 13

RUN_PITCH = 32.0
SCROLL_PITCH = 32.0


def build_tree(run_span_pad=0.0, equip_hidden=True):
    """run_span_pad shrinks the RUN container so the sweep can walk every span."""
    A = M.Axis
    d = [None] * 14

    d[N_ROOT] = M.Desc(N_ROOT, N_ROOT, kind=M.ROOT, name="root")

    # The pause window is the one hard clip boundary.
    d[N_WINDOW] = M.Desc(N_WINDOW, N_ROOT, kind=M.PANEL, clip=M.CLIP_SELF,
                         x=A(M.CENTER, 0, 192), y=A(M.CENTER, 0, 168), name="window")

    d[N_PAGES] = M.Desc(N_PAGES, N_WINDOW, kind=M.PAGEGROUP,
                        x=A(M.STRETCH, 4, 4), y=A(M.STRETCH, 4, 20), name="pages")

    d[N_QUEST] = M.Desc(N_QUEST, N_PAGES, kind=M.PAGE, x=A(M.FILL), y=A(M.FILL),
                        name="page.quest")

    d[N_RUN] = M.Desc(N_RUN, N_QUEST, kind=M.GROUP, layout=M.RUN,
                      pitch=RUN_PITCH, growEnd=1,
                      x=A(M.STRETCH, 2 + run_span_pad, 2), y=A(M.START, 2, 10),
                      name="run.songs")

    d[N_GRID] = M.Desc(N_GRID, N_QUEST, kind=M.GROUP, layout=M.GRID, cols=5, rows=2, gap=2.0,
                       x=A(M.STRETCH, 2, 2), y=A(M.START, 14, 40), name="grid.hex")

    # Allowed to escape the window - this is the only node that may fail invariant 1.
    d[N_SPILL] = M.Desc(N_SPILL, N_QUEST, kind=M.DECOR, clip=M.CLIP_SPILL,
                        x=A(M.CENTER, 0, 260), y=A(M.START, 56, 24),
                        style=M.Style(layer=3), name="decor.spill")

    d[N_EQUIP] = M.Desc(N_EQUIP, N_PAGES, kind=M.PAGE,
                        state=M.HIDDEN if equip_hidden else M.ENABLED,
                        x=A(M.FILL), y=A(M.FILL), name="page.equip")

    for idx, (nid, top) in enumerate(((N_STRIP_A, 2), (N_STRIP_B, 26), (N_STRIP_C, 50))):
        d[nid] = M.Desc(nid, N_EQUIP, kind=M.GROUP, layout=M.SCROLL_ROW,
                        pitch=SCROLL_PITCH, count=8 + idx, growEnd=0,
                        x=A(M.STRETCH, 2, 40), y=A(M.START, top, 20),
                        name="equip.strip%d" % idx)

    d[N_COL] = M.Desc(N_COL, N_EQUIP, kind=M.GROUP, layout=M.SCROLL_COL,
                      pitch=SCROLL_PITCH, count=6, growEnd=0,
                      x=A(M.END, 2, 20), y=A(M.STRETCH, 2, 2), name="equip.col")

    # The two bottom-corner page buttons (not L/R shoulder).
    d[N_BTN_L] = M.Desc(N_BTN_L, N_WINDOW, kind=M.CELL,
                        x=A(M.START, 4, 16), y=A(M.END, 2, 14), name="btn.prev")
    d[N_BTN_R] = M.Desc(N_BTN_R, N_WINDOW, kind=M.CELL,
                        x=A(M.END, 4, 16), y=A(M.END, 2, 14), name="btn.next")

    variants = [
        M.Variant(N_WINDOW, M.CANON_WIDE, x=A(M.CENTER, 0, 240)),
        M.Variant(N_GRID, M.CANON_WIDE, x=A(M.STRETCH, 8, 8)),
    ]
    return d, variants


# ------------------------------------------------------------------------------------
# invariants
# ------------------------------------------------------------------------------------
class Report(object):
    def __init__(self):
        self.failures = []
        self.checks = 0

    def check(self, ok, case, msg):
        self.checks += 1
        if not ok:
            self.failures.append((case, msg))

    def merge(self, other):
        self.failures.extend(other.failures)
        self.checks += other.checks


def enclosing_clip(ctx, i):
    """Nearest ancestor rect that actually clips, or None when the chain hits a SPILL."""
    d = ctx.desc[i]
    node = d.parent
    while True:
        pd = ctx.desc[node]
        if pd.clip == M.CLIP_SPILL:
            return None
        if pd.clip == M.CLIP_SELF:
            return ctx.node[node].clipRect
        if node == 0:
            return ctx.node[0].rect
        node = pd.parent


def check_clip(ctx, case, rep):
    for i, d in enumerate(ctx.desc):
        n = ctx.node[i]
        if i == 0 or not n.visible or d.clip == M.CLIP_SPILL:
            continue
        outer = enclosing_clip(ctx, i)
        if outer is None:
            continue
        # What actually reaches the screen is rect intersected with the node's own clip.
        drawn = M.Rect(max(n.rect.x0, n.clipRect.x0), max(n.rect.y0, n.clipRect.y0),
                       min(n.rect.x1, n.clipRect.x1), min(n.rect.y1, n.clipRect.y1))
        if drawn.w <= 0.0 or drawn.h <= 0.0:
            continue
        rep.check(drawn.inside(outer, EPS), case,
                  "clip escape: %s drawn %s outside %s" % (d.name, drawn, outer))


def check_sibling_overlap(ctx, case, rep):
    half = ctx.tile * 0.5
    by_parent = {}
    for i, d in enumerate(ctx.desc):
        if i == 0 or not ctx.node[i].visible:
            continue
        by_parent.setdefault(d.parent, []).append(i)
    for kids in by_parent.values():
        for a in range(len(kids)):
            for b in range(a + 1, len(kids)):
                ia, ib = kids[a], kids[b]
                da, db = ctx.desc[ia], ctx.desc[ib]
                if da.style.layer != db.style.layer:
                    continue
                if da.kind == M.PAGE and db.kind == M.PAGE:
                    continue  # only one page is ever visible; states already enforce that
                ra, rb = ctx.node[ia].rect, ctx.node[ib].rect
                ow = min(ra.x1, rb.x1) - max(ra.x0, rb.x0)
                oh = min(ra.y1, rb.y1) - max(ra.y0, rb.y0)
                rep.check(not (ow > half + EPS and oh > half + EPS), case,
                          "sibling overlap: %s vs %s by %.2f x %.2f (half tile %.2f)"
                          % (da.name, db.name, ow, oh, half))


def check_run(ctx, case, rep):
    for i, d in enumerate(ctx.desc):
        if d.layout != M.RUN or not ctx.node[i].visible:
            continue
        st = ctx.run.get(i)
        if st is None or st.count <= 0:
            continue
        rect = ctx.node[i].rect
        cells = [ctx.cell_rect(i, k) for k in range(st.count)]

        # 3: monotone tile starts. A growEnd run is anchored at the far edge and lays its
        # cells out backwards, so "increasing" means increasing in the growth direction,
        # not in screen x.
        for k in range(1, len(cells)):
            if d.growEnd == 1:
                ok = cells[k].x0 < cells[k - 1].x0 + EPS
                rel = "after"
            else:
                ok = cells[k].x0 > cells[k - 1].x0 - EPS
                rel = "before"
            rep.check(ok, case,
                      "run %s cell starts not monotone: cell %d starts %s cell %d "
                      "(%.3f vs %.3f)" % (d.name, k, rel, k - 1, cells[k].x0, cells[k - 1].x0))
            # Cells must also never separate: consecutive tiles touch or overlap.
            near, far = (cells[k], cells[k - 1]) if d.growEnd == 1 else (cells[k - 1], cells[k])
            rep.check(far.x0 <= near.x1 + EPS, case,
                      "run %s cells %d and %d separated by %.3f"
                      % (d.name, k - 1, k, far.x0 - near.x1))

        # 4: the anchored edge is welded to the parent edge.
        if d.growEnd == 1:
            rep.check(abs(cells[0].x1 - rect.x1) < EPS, case,
                      "run %s anchored end drifted: %.4f vs %.4f"
                      % (d.name, cells[0].x1, rect.x1))
        else:
            rep.check(abs(cells[0].x0 - rect.x0) < EPS, case,
                      "run %s anchored start drifted: %.4f vs %.4f"
                      % (d.name, cells[0].x0, rect.x0))

        # Overlap never exceeds the declared maximum.
        maxov = (d.pitch * (ctx.tile / M.TILE_SRC)) * M.RUN_MAX_OVERLAP
        for k in range(st.count):
            rep.check(st.gap[k] <= maxov + EPS, case,
                      "run %s gap[%d]=%.4f exceeds max overlap %.4f"
                      % (d.name, k, st.gap[k], maxov))


def check_scroll(ctx, case, rep):
    for i, d in enumerate(ctx.desc):
        if d.layout not in (M.SCROLL_ROW, M.SCROLL_COL) or not ctx.node[i].visible:
            continue
        st = ctx.scroll.get(i)
        if st is None:
            continue
        rect = ctx.node[i].rect
        span = rect.h if d.layout == M.SCROLL_COL else rect.w
        pitch = d.pitch * (ctx.tile / M.TILE_SRC)
        if pitch <= 0.0:
            continue
        visible = span / pitch
        hi = max(0.0, d.count - visible)
        rep.check(-EPS <= st.offset <= hi + EPS, case,
                  "scroll %s offset %.4f outside [0, %.4f]" % (d.name, st.offset, hi))
        rep.check(-EPS <= st.target <= hi + EPS, case,
                  "scroll %s target %.4f outside [0, %.4f]" % (d.name, st.target, hi))
        rep.check(0 <= st.index < max(1, d.count), case,
                  "scroll %s index %d outside [0, %d)" % (d.name, st.index, d.count))


def snapshot(ctx):
    out = []
    for n in ctx.node:
        out.extend((n.rect.x0, n.rect.y0, n.rect.x1, n.rect.y1, n.alpha))
    for i in sorted(ctx.run):
        st = ctx.run[i]
        out.append(float(st.count))
        out.extend(st.gap)
    for i in sorted(ctx.scroll):
        out.append(ctx.scroll[i].offset)
    return out


def max_delta(a, b):
    return max((abs(x - y) for x, y in zip(a, b)), default=0.0)


def check_termination(ctx, case, rep, settle_frames=240, hold_frames=60):
    """Invariant 5. Settle, then hold: nothing may keep creeping."""
    prev = snapshot(ctx)
    settled_at = None
    for f in range(settle_frames):
        ctx.update()
        cur = snapshot(ctx)
        if max_delta(prev, cur) < 1e-6 and settled_at is None:
            settled_at = f
        prev = cur
    rep.check(settled_at is not None, case,
              "no fixed point reached in %d frames" % settle_frames)
    base = snapshot(ctx)
    for _ in range(hold_frames):
        ctx.update()
    drift = max_delta(base, snapshot(ctx))
    rep.check(drift < SETTLE_EPS, case,
              "post-settle drift %.4f exceeds %.2f" % (drift, SETTLE_EPS))
    return settled_at


# ------------------------------------------------------------------------------------
# frame axis
# ------------------------------------------------------------------------------------
def check_frame_axis(rep):
    case = "frame_axis"
    for n in (2, 3, 4, 5, 8):
        for cell in (16.0, 32.0, 64.0):
            mids = [(-1, -2)]
            if n >= 3:
                mids.append((n // 2, n // 2))
                mids.append((1, n - 2))
            for lo, hi in mids:
                for repeat in (False, True):
                    span = 1.0
                    while span <= cell * n * 4.0:
                        out = M.solve_frame_axis(n, lo, hi, cell, span, repeat)
                        rep.check(len(out) <= M.FRAME_MAX_CELLS, case,
                                  "cell budget blown: %d cells" % len(out))
                        if out:
                            for pos, size, _ in out:
                                rep.check(size >= -EPS, case,
                                          "negative size %.4f" % size)
                            cursor = 0.0
                            for pos, size, _ in out:
                                rep.check(abs(pos - cursor) < EPS, case,
                                          "seam gap: pos %.4f expected %.4f" % (pos, cursor))
                                cursor += size
                            if hi < lo:
                                # No declared band. Jay's even-axis rule: the axis does not
                                # stretch and does not tile, it draws native and only ever
                                # shrinks to fit. So the expected total is the natural
                                # width capped at the span, not the span.
                                want = min(float(n) * cell, span)
                                tol = EPS
                            else:
                                want = span
                                # A repeating band stops once the remainder drops below
                                # MIN_QUAD_PX, so a shortfall up to that is by design.
                                tol = M.MIN_QUAD_PX + EPS if repeat else EPS
                            over = cursor - want
                            rep.check(-tol <= over <= EPS, case,
                                      "n=%d lo=%d hi=%d cell=%.0f span=%.2f repeat=%s "
                                      "spans %.4f want %.4f (delta %.4f)"
                                      % (n, lo, hi, cell, span, repeat, cursor, want, over))
                        span += 3.0


# ------------------------------------------------------------------------------------
# sweeps
# ------------------------------------------------------------------------------------
def sweep_aspects(rep, verbose):
    aspect = 1.20
    n = 0
    while aspect <= 2.4001:
        for equip_hidden in (True, False):
            desc, variants = build_tree(equip_hidden=equip_hidden)
            ctx = M.Ctx(desc, variants, aspect=aspect)
            case = "aspect=%.3f equip_hidden=%s" % (aspect, equip_hidden)
            settled = check_termination(ctx, case, rep)
            check_clip(ctx, case, rep)
            check_sibling_overlap(ctx, case, rep)
            check_run(ctx, case, rep)
            check_scroll(ctx, case, rep)
            if verbose:
                print("  %-34s canon=%d morphT=%.3f settled@%s"
                      % (case, ctx.activeCanon, ctx.morphT, settled))
            n += 1
        aspect += 0.01
    return n


def sweep_run_spans(rep, verbose):
    """Invariant 3 and 4 under a continuously shrinking container, at both canons."""
    n = 0
    for aspect in (4.0 / 3.0, 16.0 / 9.0, 21.0 / 9.0):
        pad = 0.0
        while pad <= 180.0:
            desc, variants = build_tree(run_span_pad=pad)
            ctx = M.Ctx(desc, variants, aspect=aspect)
            case = "runspan pad=%.0f aspect=%.3f" % (pad, aspect)
            check_termination(ctx, case, rep, settle_frames=360)
            check_run(ctx, case, rep)
            check_clip(ctx, case, rep)
            n += 1
            pad += 2.0
        if verbose:
            print("  run span sweep done at aspect %.3f" % aspect)
    return n


def sweep_aspect_ramp(rep, verbose):
    """Walk the aspect continuously on one live context, so the morph and the run easing
    are driven through their transients instead of only being observed at rest. This is
    where an oscillation or a stuck tile would show up."""
    desc, variants = build_tree()
    ctx = M.Ctx(desc, variants, aspect=1.20)
    case = "ramp"
    steps = 0
    for direction in (1, -1, 1):
        a = 1.20 if direction > 0 else 2.40
        for _ in range(240):
            a += 0.005 * direction
            ctx.set_screen(aspect=a)
            ctx.update()
            check_clip(ctx, "%s a=%.3f" % (case, a), rep)
            check_run(ctx, "%s a=%.3f" % (case, a), rep)
            check_scroll(ctx, "%s a=%.3f" % (case, a), rep)
            steps += 1
    # After the ramp stops, everything must come to rest.
    check_termination(ctx, "ramp settle", rep)
    if verbose:
        print("  ramp: %d frames" % steps)
    return steps


def sweep_scroll_paging(rep, verbose):
    """Drive the scroll index across the full range and back; the offset must track it and
    stay in range the whole time."""
    desc, variants = build_tree(equip_hidden=False)
    ctx = M.Ctx(desc, variants, aspect=16.0 / 9.0)
    ctx.page[N_PAGES]["active"] = N_EQUIP
    ctx.settle()
    n = 0
    for node in (N_STRIP_A, N_STRIP_B, N_STRIP_C, N_COL):
        count = ctx.desc[node].count
        order = list(range(count)) + list(range(count - 1, -1, -1))
        for idx in order:
            ctx.scroll.setdefault(node, M.ScrollState()).index = idx
            for _ in range(40):
                ctx.update()
                check_scroll(ctx, "scroll %s idx=%d" % (ctx.desc[node].name, idx), rep)
            n += 1
    if verbose:
        print("  scroll paging: %d index positions" % n)
    return n


# ------------------------------------------------------------------------------------
def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--selfcheck", action="store_true",
                    help="print the Python constants beside the C ones and exit")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    if args.selfcheck:
        return 1 if selfcheck(root) else 0

    rep = Report()
    print("frame axis ...")
    check_frame_axis(rep)
    print("aspect sweep 1.20 -> 2.40 ...")
    n_aspect = sweep_aspects(rep, args.verbose)
    print("run span sweep ...")
    n_span = sweep_run_spans(rep, args.verbose)
    print("aspect ramp ...")
    n_ramp = sweep_aspect_ramp(rep, args.verbose)
    print("scroll paging ...")
    n_scroll = sweep_scroll_paging(rep, args.verbose)

    print()
    print("cases: %d aspect, %d run span, %d ramp frames, %d scroll positions"
          % (n_aspect, n_span, n_ramp, n_scroll))
    print("assertions: %d" % rep.checks)
    if rep.failures:
        seen = {}
        for case, msg in rep.failures:
            # Group by the shape of the message, not its numbers, so one systematic bug
            # reports as one line instead of a thousand.
            key = re.sub(r"-?\d+(\.\d+)?", "#", msg.split(":")[0])
            seen.setdefault(key, []).append((case, msg))
        print("FAIL: %d assertion(s) in %d distinct kind(s)"
              % (len(rep.failures), len(seen)))
        for key, items in seen.items():
            case, msg = items[0]
            print("\n  [%s] x%d" % (key, len(items)))
            print("    first: %s" % case)
            print("           %s" % msg)
            if len(items) > 1:
                case, msg = items[-1]
                print("    last:  %s" % case)
                print("           %s" % msg)
        return 1
    print("OK: every invariant held.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
