#!/usr/bin/env python3
"""
so2h_gen_scene - turn an authored Python scene into the C descriptor table.

    python3 tools/so2h_gen_scene.py            # regenerate
    python3 tools/so2h_gen_scene.py --check    # fail if the checked-in files are stale

WHY THIS EXISTS
---------------
The pause scene is authored once, in tools/scenes/pause.py, and read by four consumers: the
still renderer, the animation harness, the invariant sim, and the game. Only the first three
are Python. If the C table were hand-written, the renders Jay approves would be a statement
about a *different* scene than the one that ships, and the zero-drift rect regression that
has guarded this layout since pause43 would only ever be comparing Python against Python.

So the C table is generated. pause.py stays the single source of truth, an approved GIF is a
guarantee about the game, and `--check` in CI means a stale table fails the build instead of
drifting quietly.

WHAT IT EMITS
-------------
    mm/2s2h/Menu/so2h_ui_scene_pause.h   - the So2hUiId enum, one entry per named node
    mm/2s2h/Menu/so2h_ui_scene_pause.c   - sPauseDesc[] + sPauseVariants[] + an accessor

Both files are generated. Do not edit them; edit tools/scenes/pause.py.

THE TWO CANONS
--------------
The scene is built at both canon aspects and diffed. Nodes shared by both carry a variant row
for whichever axes actually differ; nodes that only exist at 16:9 (the toolbar tail) are
emitted once and given a HIDDEN state override at 4:3. That is exactly the sparse-override
shape So2hUiVariant was designed for, and it is derived rather than maintained by hand.
"""

import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "scenes"))

import so2h_ui_model as M      # noqa: E402
import so2h_ui_render as R     # noqa: E402

OUT_C = os.path.join(ROOT, "mm", "2s2h", "Menu", "so2h_ui_scene_pause.c")
OUT_H = os.path.join(ROOT, "mm", "2s2h", "Menu", "so2h_ui_scene_pause.h")

# Python constant -> C spelling. Every one of these is asserted to agree numerically with the
# header at the bottom of this file, so a renamed or reordered C enum is caught here and not
# by a menu that lays itself out wrongly at runtime.
KIND = ["SO2H_UI_ROOT", "SO2H_UI_PANEL", "SO2H_UI_GROUP", "SO2H_UI_PAGEGROUP",
        "SO2H_UI_PAGE", "SO2H_UI_CELL", "SO2H_UI_DECOR", "SO2H_UI_CUSTOM"]
STATE = ["SO2H_UI_ENABLED", "SO2H_UI_DISABLED", "SO2H_UI_HIDDEN"]
LAYOUT = ["SO2H_UI_LAYOUT_FREE", "SO2H_UI_LAYOUT_GRID", "SO2H_UI_LAYOUT_RUN",
          "SO2H_UI_LAYOUT_SCROLL_ROW", "SO2H_UI_LAYOUT_SCROLL_COL",
          "SO2H_UI_LAYOUT_STACK_H", "SO2H_UI_LAYOUT_STACK_V"]
CLIP = ["SO2H_UI_CLIP_INHERIT", "SO2H_UI_CLIP_SELF", "SO2H_UI_CLIP_SPILL"]
AXIS = ["SO2H_UI_AXIS_START", "SO2H_UI_AXIS_END", "SO2H_UI_AXIS_STRETCH",
        "SO2H_UI_AXIS_CENTER", "SO2H_UI_AXIS_FILL"]
ATTACH = ["SO2H_UI_ATTACH_CONTENT", "SO2H_UI_ATTACH_BORDER", "SO2H_UI_ATTACH_EDGE"]
DRAW = ["SO2H_UI_DRAW_NONE", "SO2H_UI_DRAW_STRETCH", "SO2H_UI_DRAW_NATIVE",
        "SO2H_UI_DRAW_NINESLICE", "SO2H_UI_DRAW_TILE", "SO2H_UI_DRAW_RUN",
        "SO2H_UI_DRAW_RING"]
REVEAL = ["SO2H_UI_REVEAL_NONE", "SO2H_UI_REVEAL_FADE", "SO2H_UI_REVEAL_SLIDE",
          "SO2H_UI_REVEAL_UNROLL", "SO2H_UI_REVEAL_SWAP", "SO2H_UI_REVEAL_GROW",
          "SO2H_UI_REVEAL_FALL", "SO2H_UI_REVEAL_POP"]
FROM = ["SO2H_UI_FROM_AUTO", "SO2H_UI_FROM_LEFT", "SO2H_UI_FROM_RIGHT",
        "SO2H_UI_FROM_TOP", "SO2H_UI_FROM_BOTTOM"]

BANNER = ("/*\n"
          " * GENERATED FILE - DO NOT EDIT.\n"
          " *\n"
          " * Produced by tools/so2h_gen_scene.py from tools/scenes/pause.py, which is the single\n"
          " * source of truth for this layout. Editing this file by hand will be undone by the next\n"
          " * run of the generator, and CI runs it with --check.\n"
          " *\n"
          " * Edit the scene, then: python3 tools/so2h_gen_scene.py\n"
          " */\n")


def f(v):
    """A float literal that round-trips. repr gives the shortest exact form; C wants an f."""
    v = float(v)
    if v == int(v) and abs(v) < 1e15:
        return "%.1ff" % v
    return "%rf" % v


def ident(name):
    """nodeName -> SO2H_PAUSE_NODE_NAME.

    Deliberately NOT the SO2H_UI_ prefix: a node called "root" would then spell
    SO2H_UI_ROOT, which is already the So2hUiKind enumerator, and the table would compile
    into something that means the wrong thing rather than failing.
    """
    out = []
    for i, ch in enumerate(name):
        if ch.isupper() and i and not name[i - 1].isupper():
            out.append("_")
        out.append(ch.upper())
    return "SO2H_PAUSE_" + "".join(out)


def sheet_ident(sheet, sheet_names):
    """A scene names a sheet by its asset symbol; the C table wants the generated enum."""
    if sheet == M.INVALID or sheet is None:
        return "SO2H_UI_INVALID"
    if sheet not in sheet_names:
        raise SystemExit("scene references unknown sheet %r - not in sheets.json" % (sheet,))
    return sheet_names[sheet]


def slice_ident(sheet, sl, sheet_names):
    """Named slices become the SO2H_SLICE_<SHEET>_<NAME> defines so2h_gen_sheets emits."""
    if sl == M.INVALID or sl is None:
        return "SO2H_UI_INVALID"
    if isinstance(sl, int):
        return str(sl)
    if sheet == M.INVALID or sheet not in sheet_names:
        raise SystemExit("slice %r declared without a sheet" % (sl,))
    return "SO2H_SLICE_%s_%s" % (sheet_names[sheet][len("SO2H_SHEET_"):], sl)


def axis(a):
    return "{ %s, %s, %s }" % (AXIS[a.mode], f(a.a), f(a.b))


def emit_desc(d, sheet_names):
    st = d.style
    r, g, b = st.rgb
    return "\n".join([
        "    {",
        "        /* id      */ %s," % ident(d.name),
        "        /* parent  */ %s," % (ident_of_parent(d, sheet_names)),
        "        /* kind    */ %s, %s, %s, %s," % (KIND[d.kind], STATE[d.state],
                                                   LAYOUT[d.layout], CLIP[d.clip]),
        "        /* x       */ %s," % axis(d.x),
        "        /* y       */ %s," % axis(d.y),
        "        /* run     */ %d, %d, %s, %s, %d, %d," % (d.cols, d.rows, f(d.gap),
                                                           f(d.pitch), d.count, d.growEnd),
        "        /* attach  */ %s," % ATTACH[d.attach],
        "        /* style   */ { %s, %s, %s, %s, %s," % (
            sheet_ident(st.sheet, sheet_names),
            slice_ident(st.sheet, st.slice, sheet_names),
            f(st.scale), DRAW[st.drawMode], REVEAL[st.reveal]),
        "                       %d, %d, %d, %d, %d," % (r, g, b, st.alpha, st.layer),
        "                       %s, %d, %d, %d }," % (FROM[st.revealFrom], st.revealDelay,
                                                      st.revealJitter,
                                                      0 if st.shadow else 1),
        "        /* draw    */ NULL, NULL,",
        "        /* nav     */ SO2H_UI_INVALID, SO2H_UI_INVALID, SO2H_UI_INVALID, SO2H_UI_INVALID,",
        '        /* name    */ "%s",' % d.name,
        "    },",
    ])


_PARENTS = {}


def ident_of_parent(d, sheet_names):
    return _PARENTS[d.id]


def build_scene(scene, aspect):
    return scene.build(aspect)


def diff_axis(a, b):
    return (a.mode != b.mode) or (a.a != b.a) or (a.b != b.b)


def generate(scene_name="pause"):
    scene = __import__(scene_name)

    sheets = R.load_sheets()
    M.Ctx.rings = {n: (sh.rings[0], sh.rings[1], sh.unit) for n, sh in sheets.items()}

    # Sheet id -> the C enum name the sheet generator emitted, so the table references
    # SO2H_SHEET_* rather than a bare index that silently rots when sheets.json changes.
    # The enum spelling is so2h_gen_sheets' rule, applied to the same manifest, so the two
    # generators cannot disagree about what a sheet is called.
    man = json.load(open(os.path.join(ROOT, "mm", "assets", "custom", "textures",
                                      "so2h_menu", "sheets.json")))
    sheet_names = {}
    for nm in ([s["name"] for s in man.get("sheets", [])] +
               list((man.get("legacyTiles") or {}).get("names", []))):
        sheet_names[nm] = "SO2H_SHEET_" + nm[len("gSo2h"):].upper()

    # The two CANON aspects, not M.ASPECT_WIDE / M.ASPECT_43 - those are the morph
    # thresholds (1.55 / 1.45), and building at a threshold gives a toolbar of the wrong
    # length. 16:9 and 4:3 are what the renders Jay approved were made at.
    wide = build_scene(scene, 16.0 / 9.0)
    four = build_scene(scene, 4.0 / 3.0)

    names_w = [d.name for d in wide.desc]
    names_4 = [d.name for d in four.desc]

    # The authored order must agree for every node the two canons share, or ids mean two
    # different things depending on aspect and every variant row below is nonsense.
    shared = len(names_4)
    if names_w[:shared] != names_4:
        for i, (a, b) in enumerate(zip(names_w, names_4)):
            if a != b:
                raise SystemExit("scene node order diverges at index %d: 16:9 %r vs 4:3 %r" % (i, a, b))
        raise SystemExit("scene node order diverges between canons")

    seen = {}
    for d in wide.desc:
        k = ident(d.name)
        if k in seen:
            raise SystemExit("node names %r and %r both spell %s" % (seen[k], d.name, k))
        seen[k] = d.name

    _PARENTS.clear()
    for d in wide.desc:
        _PARENTS[d.id] = "0" if d.id == 0 else ident(wide.desc[d.parent].name)

    # ---------------------------------------------------------------- variants
    variants = []
    for i in range(shared):
        dw, d4 = wide.desc[i], four.desc[i]
        hx, hy = diff_axis(dw.x, d4.x), diff_axis(dw.y, d4.y)
        stov = "0xFF" if dw.state == d4.state else STATE[d4.state]
        if hx or hy or stov != "0xFF":
            variants.append((ident(dw.name), "SO2H_UI_CANON_43", int(hx), int(hy), stov,
                             axis(d4.x), axis(d4.y)))
    # Nodes the narrow canon does not have at all: emitted once, hidden at 4:3.
    for i in range(shared, len(names_w)):
        dw = wide.desc[i]
        variants.append((ident(dw.name), "SO2H_UI_CANON_43", 0, 0, "SO2H_UI_HIDDEN",
                         axis(dw.x), axis(dw.y)))

    # ---------------------------------------------------------------- header
    h = [BANNER, "#ifndef SO2H_UI_SCENE_PAUSE_H", "#define SO2H_UI_SCENE_PAUSE_H", "",
         '#include "so2h_ui.h"', "",
         "// One identifier per authored node. Content code addresses the menu through these and",
         "// never through a raw index, so inserting a window in pause.py cannot silently renumber",
         "// something a draw callback was pointing at.",
         "typedef enum So2hUiPauseId {"]
    for d in wide.desc:
        h.append("    %s," % ident(d.name))
    h += ["    SO2H_PAUSE_NODE_MAX", "} So2hUiPauseId;", "",
          "// The generated table. Pass straight to So2h_Ui_Init.",
          "const So2hUiDesc* So2h_UiScene_Pause(s32* outCount);",
          "const So2hUiVariant* So2h_UiScene_PauseVariants(s32* outCount);", "",
          "#endif", ""]

    # ---------------------------------------------------------------- source
    c = [BANNER, '#include "so2h_ui_scene_pause.h"', '#include "so2h_ui_sheets.h"', "",
         "// %d nodes at 16:9, %d at 4:3, %d sparse canon overrides." % (
             len(names_w), shared, len(variants)),
         "static const So2hUiDesc sPauseDesc[] = {"]
    for d in wide.desc:
        c.append(emit_desc(d, sheet_names))
    c += ["};", ""]

    if variants:
        c.append("static const So2hUiVariant sPauseVariants[] = {")
        for node, canon, hx, hy, stov, ax, ay in variants:
            c.append("    { %s, %s, %d, %d, %s, %s, %s }," % (node, canon, hx, hy, stov, ax, ay))
        c += ["};", ""]
    else:
        c += ["static const So2hUiVariant sPauseVariants[1] = { { 0, 0, 0, 0, 0xFF, { 0, 0.0f, 0.0f },",
              "                                                   { 0, 0.0f, 0.0f } } };", ""]

    c += ["const So2hUiDesc* So2h_UiScene_Pause(s32* outCount) {",
          "    if (outCount != NULL) {",
          "        *outCount = (s32)(sizeof(sPauseDesc) / sizeof(sPauseDesc[0]));",
          "    }",
          "    return sPauseDesc;",
          "}", "",
          "const So2hUiVariant* So2h_UiScene_PauseVariants(s32* outCount) {",
          "    if (outCount != NULL) {",
          "        *outCount = %s;" % ("(s32)(sizeof(sPauseVariants) / sizeof(sPauseVariants[0]))" if variants else "0"),
          "    }",
          "    return sPauseVariants;",
          "}", ""]

    return "\n".join(h), "\n".join(c), len(names_w), shared, len(variants)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scene", default="pause")
    ap.add_argument("--check", action="store_true",
                    help="exit non-zero if the checked-in files differ from a fresh generation")
    args = ap.parse_args()

    h, c, n_wide, n_43, n_var = generate(args.scene)

    if args.check:
        stale = []
        for path, want in ((OUT_H, h), (OUT_C, c)):
            have = open(path).read() if os.path.exists(path) else None
            if have != want:
                stale.append(os.path.relpath(path, ROOT))
        if stale:
            print("so2h_gen_scene: STALE - %s" % ", ".join(stale))
            print("  run: python3 tools/so2h_gen_scene.py")
            return 1
        print("so2h_gen_scene: up to date (%d nodes, %d overrides)" % (n_wide, n_var))
        return 0

    open(OUT_H, "w").write(h)
    open(OUT_C, "w").write(c)
    print("so2h_gen_scene: %d nodes 16:9 / %d nodes 4:3, %d canon overrides" % (n_wide, n_43, n_var))
    print("  -> %s" % os.path.relpath(OUT_H, ROOT))
    print("  -> %s" % os.path.relpath(OUT_C, ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
