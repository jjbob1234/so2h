#!/usr/bin/env python3
"""
so2h_ui_anim.py

Renders the OPEN or CLOSE performance of a scene as a GIF plus a filmstrip, using the same
solver (so2h_ui_model) and the same drawing path (so2h_ui_render) as the still previews.

It exists so the animation can be judged without a build: the entrance and the exit are
data (a reveal mode plus a delay per node) and this walks the same frames the game will.

  python3 tools/so2h_ui_anim.py --scene tools/scenes/pause.py --out /tmp/pause44 \
      --mode open --scale 4 --frames 62 --strip 8
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import so2h_ui_model as M      # noqa: E402
import so2h_ui_render as R     # noqa: E402
from PIL import Image          # noqa: E402


def frames_for(ctx, sheets, mode, count, scale, shadows, backdrop):
    out = []
    if mode == "open":
        ctx.open()
    else:
        ctx.close()
    for _ in range(count):
        ctx.update()
        out.append(R.render(ctx, sheets, scale, backdrop, shadows=shadows).convert("RGB"))
    return out


def filmstrip(imgs, cols, pick):
    sel = [imgs[i] for i in pick]
    w, h = sel[0].size
    tw, th = w // 3, h // 3
    rows = (len(sel) + cols - 1) // cols
    strip = Image.new("RGB", (cols * tw, rows * th), (12, 12, 14))
    for k, im in enumerate(sel):
        strip.paste(im.resize((tw, th), Image.LANCZOS), ((k % cols) * tw, (k // cols) * th))
    return strip


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scene", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--mode", choices=("open", "close"), default="open")
    ap.add_argument("--aspect", type=float, default=16.0 / 9.0)
    ap.add_argument("--scale", type=int, default=4)
    ap.add_argument("--frames", type=int, default=62)
    ap.add_argument("--strip", type=int, default=8, help="filmstrip columns")
    ap.add_argument("--no-shadows", action="store_true")
    ap.add_argument("--no-backdrop", action="store_true")
    args = ap.parse_args()

    sheets = R.load_sheets()
    M.Ctx.rings = {n: (sh.rings[0], sh.rings[1], sh.unit) for n, sh in sheets.items()}
    build = R.load_scene(args.scene)

    ctx = build(args.aspect)
    ctx.settle()   # let the aspect morph finish first; only the reveal is being shown
    imgs = frames_for(ctx, sheets, args.mode, args.frames, args.scale,
                      not args.no_shadows, not args.no_backdrop)

    gif = "{}_{}.gif".format(args.out, args.mode)
    small = [im.resize((im.size[0] // 2, im.size[1] // 2), Image.LANCZOS) for im in imgs]
    hold = [small[-1]] * 18
    small[0].save(gif, save_all=True, append_images=small[1:] + hold,
                  duration=1000 // 30, loop=0, optimize=True)
    print("wrote", gif)

    n = len(imgs)
    cols = args.strip
    picks = sorted(set([int(round(i * (n - 1) / float(cols * 2 - 1))) for i in range(cols * 2)]))
    p = "{}_{}_strip.png".format(args.out, args.mode)
    filmstrip(imgs, cols, picks).save(p)
    print("wrote", p, "frames", picks)


if __name__ == "__main__":
    main()
