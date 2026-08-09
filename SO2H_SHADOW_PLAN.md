# SO2H UI — drop shadow in C (Option A: offscreen FB + real blur)

## 1. Current state: the shadow is spec-only

The drop shadow exists as *specification* in three places and is implemented in none:

| Location | What it is |
|---|---|
| `mm/2s2h/Menu/so2h_ui.h:260` | `u8 noShadow` style field |
| `mm/2s2h/Menu/so2h_ui.h:276-283` | `SO2H_UI_SHADOW_*` tuning block (blur 32px @1440p, ofs 0.75/1.05 of blur reach, strength 0.7) |
| `tools/so2h_gen_scene.py:152` | generator emits `0 if st.shadow else 1` per node — the table is already correct |

`mm/2s2h/Menu/so2h_ui_draw.c` (980 lines) contains **zero** occurrences of `shadow`. It never reads
`noShadow` and never emits a shadow pass.

The only working implementation is `tools/so2h_ui_render.py:256 cast_shadow()` — the **Python preview
renderer**. That is why every approved preview has shadows and the game has never had them.

## 2. What LUS already gives us (all verified present)

Everything Option A needs already exists and is already used by this port:

- `G_SETFB` / `G_RESETFB` are **real display-list opcodes** (`interpreter.cpp:4658-4659`) —
  `gsSPSetFB` / `gsSPResetFB`. FB switching is sequenceable from C, not C++-immediate only.
- `gfx_create_framebuffer(w, h, nw, nh, resize)` — `interpreter.cpp:5444`.
- `gDPSetTextureImageFB` + `gDPImageRectangle` — draw an FB as a texture into an arbitrary rect,
  with hardware bilinear filtering. Used today by `FB_DrawFromFramebufferRect`.
- `gDPCopyFB` — FB→FB copy with a `copyOnce` / `hasCopied` flag, i.e. **a built-in cache primitive**.
- `gfx_register_fb_texture(cpuAddr, fbId)` (`interpreter.cpp:4912`) maps a fake CPU address to a GPU
  FB; `ImportTexture` (`:1237-1247`) then binds the GPU FB directly — full resolution, no CPU readback.
- `mm/2s2h/framebuffer_effects.c` is the existing, working precedent for all of the above
  (`gPauseFrameBuffer`, `gBlurFrameBuffer`, `gReusableFrameBuffer`, `gN64ResFrameBuffer`).

**No shader access is needed.** The gaussian is done as a ping-pong downscale/upscale between two FBs
using `gDPImageRectangle`; hardware bilinear does the smoothing. 2-3 ping-pong passes ≈ a 32px gaussian.

## 3. Caching — yes, and the numbers say it is nearly free

Measured on the real solved tree (240 update ticks, 16:9):

| Metric | Value |
|---|---|
| Total nodes | 107 |
| **Shadow casters** | **45** |
| Frames where any node's geometry (pos or size) changes | **51** — last change at frame 50 |
| Frames where any node's **size** changes | **40** — last at frame 40 |

After frame 50 the entire tree is **static forever**. So:

- A dirty-hash cache regenerates for ~51 frames of the open, then **never again** while the menu is up.
- Keyed on *size* only, it stops regenerating at frame 40 and pure translation is free.

This is exactly the "animate the cache" idea and it is the right design.

## 4. Budget — the Gfx pool must grow first

Measured quads/frame for the real tree:

| Aspect | quads | Gfx commands | pool |
|---|---|---|---|
| 16:9 | 1046 | ~12,616 | 16,384 |
| 4:3 | 947 | ~11,428 | 16,384 |

`mm/include/gfx.h:51` — `Gfx overlayBuffer[0x4000]`. Already at 77% before any shadow work.

Naive per-quad shadow re-draw (488 casting quads) would need +5,856 commands and overflow.
**Option A does not**: the whole shadow pass is a handful of FB blits plus one composite quad per
caster — roughly **+60 to +120 commands per frame**, cached. The pool still gets bumped to `0x10000`
for headroom (512KB, free on PC, and consistent with the no-N64-limits rule).

## 5. The design fork — needs your call

Both options share the same machinery; they differ in *what* gets cached.

### A1 — Full-screen shadow cache (per layer)

One shadow FB per populated layer. Render every caster's silhouette on that layer into it, blur it
once, composite it under that layer's nodes.

- Regenerates whenever *anything* on that layer moves → ~51 frames during the open, then zero.
- **Simple.** ~1 FB per layer, one composite quad per layer per frame.
- Layer occupancy (casters): L0:4, L1:2, L2:4, L3:1, L4:4, L5:12, L6:15, L7:1, L9:2 → 9 FBs.
- Downside: one node twitching redirties the whole layer. Irrelevant here, since the tree freezes.

### A2 — Per-node shadow atlas

One large atlas FB (e.g. 2048²). Each caster renders its silhouette **once at rest size**, blurred
once, into its own atlas rect. Each frame every caster draws its atlas sub-rect into its *current*
animated rect.

- Regeneration is keyed on **size only** → stops at frame 40; translation/pop is pure cache animation.
- Literally "animate the cache" — exactly what you described.
- Cost: 45 quads/frame, always. Slightly more than A1.
- More complex: needs atlas allocation, and scaling a pre-blurred silhouette means the blur radius
  scales with the node during a GROW reveal (a growing window's shadow starts crisp-ish and softens).
  A1 reblurs each frame so its blur is always physically correct.

**Recommendation: A1.** The tree goes static at frame 50, so A2's advantage (cheap animation) buys
~10 frames of savings while introducing atlas management and a blur-scaling artifact during GROW.
A1 matches the Python preview exactly, which is the stated goal of Option A.

## 6. Layer bug found on the way (separate, must fix)

`pageLeft` (table 79) and `pageRight` (table 80) are authored at **layer 9**.
`so2h_ui_draw.c:889` is `for (layer = 0; layer < SO2H_UI_MAX_LAYERS; layer++)` and
`SO2H_UI_MAX_LAYERS` is 8 (`so2h_ui.h:65`).

**Those two page arrows have never been drawn in-game.** They appear in every Python preview because
`so2h_ui_render.py` sorts by layer with no upper bound. Confirmed in the generated table
(`so2h_ui_scene_pause.c:1210` and `:1225`, both `255, 255, 255, 255, 9,`).

Fix is either bump `SO2H_UI_MAX_LAYERS` to 10, or move those two nodes to layer 7. Bumping the
constant is safer (it is a pure loop bound) and the sim already tracks it
(`tools/so2h_ui_sim.py:67`). Either way `tools/so2h_ui_sim.py` should gain an assert that no node's
layer exceeds `MAX_LAYERS-1`, so this class of bug cannot recur silently.

## 7. Implementation order (one commit, per your call)

1. Bump `overlayBuffer` `0x4000` → `0x10000` (`mm/include/gfx.h:51`).
2. Fix the layer-9 bug + add the sim invariant assert.
3. `so2h_ui_shadow.c` — FB creation, silhouette pass, ping-pong blur, composite, dirty hash.
4. Wire `noShadow` into `So2h_UiDraw_Tree`'s per-layer walk.
5. CVar `gSo2h.Ui.NoShadow` (default off = shadows ON) to A/B it in-game.
6. Full static sweep (11-file `gcc -fsyntax-only` → 0; both `--check` generators; `rectchk` zero
   drift; `quadcount`), commit, push, poll CI per `AGENT_BUILD_RULES.md`, deliver zip + summary.
