# SO2H pause freeze - root cause

## Symptom
Opening the pause menu hard-hangs: frozen picture, dead audio, no log line, no stack, no
crash dialog. Live bisection with `gSo2h.Ui.DrawMax`: **16 drawn nodes is fine, 17 hangs**,
reproducibly, independent of animation state.

## Root cause
`So2h_UiDraw_Slice` passed `repeat = sheet->grow != SO2H_UI_GROW_STRETCH` for
`SO2H_UI_DRAW_NINESLICE` as well as `SO2H_UI_DRAW_RUN` (so2h_ui_draw.c:681). Every sheet in
the pack is authored `grow = run`, so **every NINESLICE node tiled its run band at native
pitch instead of stretching it.**

The Python model - the thing that rendered the previews that were approved - does the
opposite: `draw_frame(..., repeat = st.drawMode != M.DRAW_NINESLICE, ...)`
(tools/so2h_ui_render.py:239). NINESLICE stretches; only RUN / RING / TILE repeat.

The cost of that divergence, measured over the real solved scene (both aspects, 240 settled
frames, /tmp/cq3.py):

| | quads/frame | Gfx words needed |
|---|---|---|
| C as shipped, 16:9 | 3873 | ~39,370 |
| C as shipped, 4:3 | 3560 | ~36,198 |
| model / fixed, 16:9 | 1062 | ~11,260 |
| model / fixed, 4:3 | 954 | ~10,138 |

`overlayBuffer` is `Gfx[0x4000]` = **16,384 words** (mm/include/gfx.h:51). The menu was
asking for **2.4x the entire overlay display-list arena.**

Worst offenders as shipped: `rightWall` 252 quads, `gameDeco` 252, `bottomWall` 240/264,
`heartWin` 192, `leftWin` 154 - and the tiny 130x10 context rows emitted **117 quads each**,
because a 64-texel tile scaled to 0.2175 is a 3.38 px cell repeated 37 times across the row.

### Why it presents as a hang, and why exactly at node 17
Cumulative words at 16:9, per drawn node:

```
draw#15  ctxRow0   cum 1421 quads  ~14,310 words   (leaves ~2,000)
draw#16  ctxRow1   cum 1538 quads  ~15,482 words   (leaves   ~900)
draw#17  ctxRow2   cum 1655 quads  ~16,604 words   (over the arena)
```

Our own emitters are all budget-guarded (`So2h_UiGfxRoom`) and decline politely, so *we*
never overrun. **The vanilla writers that come after us in the frame are not guarded** - they
assume the overlay arena always has room, because before this menu existed it always did.
Once the menu leaves under ~900 words, the next writer walks off the end of `overlayBuffer`
into the neighbouring pool buffer, Fast3D executes garbage, and the process wedges: frozen
picture, dead audio, no log, no stack. That is exactly the reported failure, and the 16/17
cliff is the point where the leftover room stops covering the rest of the frame.

It also explains every measurement that looked innocent:
- Nodes 76 (`ctxRow0`) and 77 (`ctxRow1`) really are byte-identical - nothing is wrong with
  either one. The fault is cumulative, so it lands on whichever node happens to be 17th.
- The earlier budget accounting said "only 3,939 of 16,384 words at 17 nodes" because it was
  computed from the *model's* quad counts (9 and 25 per frame node), not the C emitter's
  (117 and 252). The model was never wrong about the art - it was wrong about C.
- The degenerate/sub-texel tile guard, the `gap[16]` audit and the loop-bound sweep were all
  correct and all irrelevant.

## Fix
1. **NINESLICE stretches its run band; only RUN repeats at native pitch.**
   so2h_ui_draw.c:681 now passes `false` for NINESLICE. A descriptor that genuinely wants
   whole native tiles asks for `SO2H_UI_DRAW_RUN`, which is what that mode is for. This also
   makes the C output match the approved previews for the first time. RING and TILE keep
   native pitch, which is what the model does too.
2. **A real tail reserve.** The budget is now armed at
   `overlay.d - SO2H_UI_GFX_TAIL_RESERVE` (2048 words) instead of the raw arena end, so the
   menu drops its own tail rather than starving the unguarded writers behind it. This class
   of failure cannot hang the game again even if some future page gets heavy - it degrades
   into missing art, which is visible and reportable.

## After installing this build
Clear the diagnostic CVars - they persist across restarts:
`gSo2h.Ui.DrawMax`, `gSo2h.Ui.TraceFrames`, `gSo2h.Ui.NoDraw`, `gSo2h.Ui.NoUpdate`,
`gSo2h.Ui.NoSfxMix`.
