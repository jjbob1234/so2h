# so2h_ui scene: the merged pause menu, declared. Rendered by tools/so2h_ui_render.py.
#
# SHEET RULES (Jay's, binding):
#   1. FrameBacked is for windows drawn OVER THE GAMEPLAY WINDOW - in-game menu mods.
#      It is NOT used in the pause menu. Do not add it to this file.
#   2. BigButton is a 2x2 ATLAS of 4 individual sprites (PILL, PILL_RED, SQUARE,
#      SQUARE_SUNKEN). Never nine-slice it. Draw one slice - native, or scaled whole.
#      Same for ContentBox and MiniButton.
#   3. Glass overlays gameplay. Filled is the opaque wallpaper chrome.
#
# THE MAGENTA RULE (from /home/user/Attachments/image_a2nmHf.png):
#   The checkerboard region - the left 61% by the top 69%, i.e. x up to 183 units from the
#   right edge and y up to 166 - is the GAMEPLAY WINDOW. NOTHING is declared inside it.
#   The hearts, the item grid, "Return" and "To Map" belong to the game, not to this menu.
#   This scene only builds the L of chrome around it.
#
# BORDER WIDTH IS NOT OPTIONAL:
#   Every child of a framed window is parented to that window and attaches BORDER, so the
#   solver insets it by one whole border tile on ALL FOUR sides - horizontally too. Nothing
#   in here is positioned by eyeballed absolute numbers inside another frame; the ring does
#   it, which is why widening a parent cannot push a child under its own chrome.
#   attach EDGE is used ONLY where a window is deliberately meant to bleed past its parent.
#
# COMPOSITION:
#   Two oversized FILLED slabs form the L - one down the right, one across the bottom -
#   each pushed 20 units past its own outer edges so those border rings land off-screen and
#   the self-tiling centre reaches the screen edge with no gap. Only the inner edges facing
#   the gameplay keep a visible border.
#
#   On that wallpaper sit windows at DIFFERENT scales, deliberately overlapping: big frames
#   that run off the right and bottom, mid frames inset in them, small native-size
#   BigButton squares, and a scaled-up song block that runs off the bottom-left corner.
#
# Units are DESIGN UNITS on a 240-tall canvas; width is aspect-dependent (320 at 4:3,
# 426.7 at 16:9). Right-side things anchor END and bottom things anchor END, so the L
# tracks the screen edge at any aspect instead of a baked-in mockup width.
#
# This file is DATA. Adding a panel is one row. Nothing here touches solver code.

GLASS = "gSo2hSheetFrameGlass"
FILLED = "gSo2hSheetFrameFilled"
EMPTY = "gSo2hSheetFrameEmpty"
RAISING = "gSo2hSheetFrameRaising"
POPUP = "gSo2hSheetFramePopup"
BIG = "gSo2hSheetBigButton"
MINI = "gSo2hSheetMiniButton"
SONGBK = "gSo2hSheetSongBackdrop"
# True 3x3 @64 nine-slices - one border ring, one repeating middle cell.
SLAB9 = "gSo2hSheetSlab9"          # opaque filled slab
SLOT_EMPTY = "gSo2hSheetSlotEmpty"  # full bevel, transparent centre
SLOT_ITEM = "gSo2hSheetSlotItem"    # full bevel, INSET recessed centre - the item slot
SLOT_THIN = "gSo2hSheetSlotThin"    # thin bevel, transparent centre
ITEMBOX = "gSo2hSheetItemBox"       # 5x5 grow-friendly recessed panel

# Ocarina song note colours. Warp / temple songs carry their canon colour; the plain
# ocarina songs stay white. This is DATA - the note art is one white sprite, tinted.
WHITE = (255, 255, 255)
SONGS = [
    # page 0 - Majora's Mask
    ("Song of Time",        (170, 225, 255)),
    ("Song of Healing",     (150, 210, 235)),
    ("Epona's Song",        WHITE),
    ("Song of Soaring",     WHITE),
    ("Song of Storms",      WHITE),
    ("Sonata of Awakening", (176, 112, 224)),
    ("Goron's Lullaby",     (226, 90, 72)),
    ("New Wave Bossa Nova", (86, 148, 236)),
    ("Elegy of Emptiness",  (236, 192, 82)),
    ("Oath to Order",       (122, 214, 122)),
    ("Inverted Song",       (170, 225, 255)),
    ("Double Time",         (170, 225, 255)),
    # page 1 - Ocarina of Time
    ("Zelda's Lullaby",     WHITE),
    ("Saria's Song",        WHITE),
    ("Sun's Song",          WHITE),
    ("Song of Time (OoT)",  WHITE),
    ("Scarecrow's Song",    WHITE),
    ("Song of Storms (OoT)", WHITE),
    ("Minuet of Forest",    (108, 224, 108)),
    ("Bolero of Fire",      (232, 82, 60)),
    ("Serenade of Water",   (94, 150, 240)),
    ("Requiem of Spirit",   (236, 154, 62)),
    ("Nocturne of Shadow",  (176, 106, 220)),
    ("Prelude of Light",    (240, 226, 108)),
]


def build(aspect):
    import so2h_ui_model as M

    A, S = M.Axis, M.Style
    NS = M.DRAW_NINESLICE
    RUN = M.DRAW_RUN
    RING = M.DRAW_RING       # frame chrome with its own fill cells skipped
    NAT = M.DRAW_NATIVE
    EDGE = M.ATTACH_EDGE
    BORDER = M.ATTACH_BORDER
    SPILL = M.CLIP_SPILL
    d = []

    # One 64-texel tile and one 32-texel tile in design units at the canon tile scale.
    # Used ONLY to size native-art cells (BigButton, MiniButton) - never to fake a border.
    TILE = (M.DESIGN_H * M.TILE_SRC) / M.MOCK_H
    T64 = 64.0 * TILE / M.TILE_SRC
    T32 = 32.0 * TILE / M.TILE_SRC

    def add(parent, name, x, y, sheet=None, slice=M.INVALID, mode=NS, layer=0,
            kind=M.PANEL, layout=M.FREE, cols=0, rows=0, gap=0.0, alpha=255,
            clip=M.CLIP_INHERIT, attach=BORDER, scale=1.0, rgb=(255, 255, 255),
            reveal=M.REVEAL_NONE, shadow=True, revealFrom=M.FROM_AUTO, revealDelay=0,
            revealJitter=M.REVEAL_JITTER):
        i = len(d)
        d.append(M.Desc(i, parent, kind=kind, layout=layout, x=x, y=y, cols=cols,
                        rows=rows, gap=gap, clip=clip, name=name, attach=attach,
                        style=S(sheet=sheet if sheet else M.INVALID, slice=slice,
                                drawMode=mode if sheet else M.DRAW_NONE,
                                alpha=alpha, layer=layer, scale=scale, rgb=rgb,
                                reveal=reveal, shadow=shadow, revealFrom=revealFrom,
                                revealDelay=revealDelay, revealJitter=revealJitter)))
        return i

    def dataRows(parent, prefix, count, in_x, in_y, w, h, layer):
        """A stack of dark recessed rows filling a framed window's inner recess.

        One call = one list. `h` is the row PITCH: rows ABUT so neighbours share a
        single bevel seam, which is what makes a run of them read as one list box
        instead of a column of separate slots. Adding a list to any window is this
        one line - no layout, clipping or asset code is involved.
        """
        s = h / (3.0 * T64)
        out = []
        for r in range(count):
            out.append(add(parent, "%sRow%d" % (prefix, r), A(M.START, in_x, w),
                           A(M.START, in_y + r * h, h), sheet=SLOT_ITEM, mode=NS,
                           layer=layer, kind=M.CELL, scale=s, attach=EDGE,
                           clip=SPILL, shadow=False))
        return out

    # ================================================================== the open sequence
    # Pausing plays the menu ON. Vanilla animates the whole pause screen as one object; this
    # menu is a collage of windows at different scales, so it assembles instead: each window
    # slides in from the screen edge it is ALREADY NEAREST TO (FROM_AUTO derives that from the
    # solved rect), staggered by a handful of frames.
    #
    # The entire sequence is the numbers below and nothing else. There is no timeline, no
    # keyframes and no per-window animation code: a new panel joins the open by naming a
    # delay, and if it names none it is simply already there. Travel distance is derived from
    # each rect, so every window is fully off-screen at t=0 at every aspect.
    #
    # Order reads outside-in: the wallpaper L lands first, then the big frames on it, then
    # the readouts inside them, then the bottom bar and the small furniture last.
    # The stagger is deliberately WIDE: one window's move takes REVEAL_FRAMES (22) frames, and
    # the last delay lands at 27, so the whole assembly plays over about 50 frames (~0.8s at
    # 60fps) instead of arriving in a single clump. On top of these numbers every node is dealt
    # 0..REVEAL_JITTER extra frames from the seed of THIS open, so the menu never opens the
    # same way twice while the choreography below stays exactly as authored.
    IN = M.REVEAL_SLIDE
    D_WALL = 0      # the L of wallpaper - the surface everything else lands on
    D_QUEST = 6     # right column
    D_STAT = 11     # data column
    D_SONG = 16     # song block, bottom left
    D_BAR = 21      # tool belt
    D_CTX = 34      # context menu, last - it is the thing the player reads first
    # The song block is three separate arrivals, not one: the staff lands, then the note
    # container pops onto it, then the context list comes up under that. The gaps are small
    # on purpose - and each node also draws its own 0..revealJitter frames on top, so the
    # three never land on the same beat twice running.
    D_GAME = 2      # the peep hole drops in just after the wallpaper it lands on
    D_STAFF = D_SONG + 3
    D_NOTES = D_SONG + 8

    root = add(0, "root", A(M.FILL), A(M.FILL), kind=M.ROOT)

    # ------------------------------------------------------------------ gameplay window
    # The exposed gameplay region is anchored to the screen's top-left corner and its right
    # edge is the vertical seam, GAME_R in from the right edge. Its HEIGHT IS DERIVED so that
    # the region is 4:3 at the 4:3 design width - the game image is then unsquashed there:
    #
    #     GAME_W (at 4:3) = 320 - 183 = 137   ->   GAME_H = 137 * 3/4 = 102.75
    #
    # This replaces the old baked y-seam of 166 (which made the region 137x166, i.e. 0.83 -
    # taller than wide). Every piece that meets the bottom of the gameplay window derives its
    # own edge from GAME_H, so this one number moves the whole horizontal seam.
    # The seam is a fixed distance in from the right edge, so the gameplay window gets WIDER
    # as the screen does. Its height is then whatever makes that width match THE SCREEN'S OWN
    # aspect, so the game image is unsquashed at every aspect - one rule, no per-aspect
    # branch:
    #
    #   4:3   screen 320.0 -> W 137.0 -> H 137.0 / (4/3)  = 102.75
    #   16:9  screen 426.7 -> W 243.7 -> H 243.7 / (16/9) = 137.06
    #
    # So the horizontal seam sits HIGHER at 4:3 and LOWER at 16:9, and everything under it
    # follows via SONG_DY. Widescreen shows more game, not a stretched game.
    GAME_R = 183.0
    ASPECT = aspect if aspect else (4.0 / 3.0)
    GAME_W = (M.DESIGN_H * ASPECT) - GAME_R
    GAME_H = GAME_W / ASPECT
    SONG_DY = GAME_H - 166.0    # how far everything under the seam travels (negative = up)

    # ============================================================ wallpaper chrome (L)
    # Right slab: 20 past the right edge, 20 past top and bottom, left edge exactly on the
    # gameplay seam (183 from the right). Only its LEFT border is on screen.
    add(root, "rightWall", A(M.END, -20.0, 203.0), A(M.STRETCH, -20.0, -20.0),
        sheet=FILLED, layer=1, clip=SPILL,
        reveal=IN, revealFrom=M.FROM_RIGHT, revealDelay=D_WALL)

    # Bottom slab: past both sides and the bottom, top edge on the gameplay seam. Height is
    # derived from GAME_H so the top edge rides the seam. Only its TOP border is on screen.
    add(root, "bottomWall", A(M.STRETCH, -20.0, -20.0),
        A(M.END, -20.0, (M.DESIGN_H + 20.0) - GAME_H),
        sheet=FILLED, layer=1, clip=SPILL,
        reveal=IN, revealFrom=M.FROM_BOTTOM, revealDelay=D_WALL)

    # ============================================================ gamescreen container
    # Behind EVERYTHING (layer 0) sits the container the gameplay window is cut out of. It is
    # the filled body art drawn in RING mode: every cell inside its outer border ring is
    # skipped, so its fill is never emitted and there is a REAL HOLE in the middle. The
    # checkerboard - the live game image - shows through that hole; the surviving ring is the
    # chrome that surrounds it.
    #
    # Geometry runs the other way round from every other node here: the HOLE is the thing with
    # a fixed size (it is the gameplay window, whose aspect must stay honest), so the
    # container is that window INFLATED by one border ring on each side. Nothing about the
    # window is typed twice.
    # TWO SEPARATE THINGS, deliberately not one node:
    #
    #   1. gameScreen - the BACKGROUND / GAP FILLER. Solid body art (FrameFilled) in RING mode
    #      so its fill cells are dropped, with a deliberately FAT ring. Its only job is to fill
    #      the cracks between the peep hole and the surrounding menu windows. Layer 0, strictly
    #      below every other drawn node here, so the song sheet, the right column, the stat
    #      panels and the bottom bar all draw OVER it. Its inner edge is a raw tile boundary
    #      with no finished lip - ugly, and never seen, because the border laps over it.
    #
    #   2. gameDeco - the WINDOW BORDER. The transparent Empty frame, so the PEEP HOLE stays
    #      genuinely open: vanilla kaleido draws into it and reads straight through the alpha-0
    #      centre. Layer 2 - above the wallpaper and the background ring, below the menu
    #      windows (layers 3+), so it can never cover the song sheet.
    #
    # The border LAPS INWARD over the gameplay window by LAP_IN. That is what makes its bevel
    # read as the window's own border rather than as chrome sitting beside it, and it is why
    # this is a peep hole: the kaleido content passes UNDER the border's inner edge.
    #
    # The hole stays the fixed thing; both rects are derived outward from it, so the gameplay
    # window's aspect stays honest at every screen aspect and nothing is typed twice.
    DECO_S = 0.75
    GS_S = 2.5
    DECO_C = 2.0 * T64 * DECO_S       # gameDeco's content ring, 2 tiles at its scale
    GS_C = 2.0 * T64 * GS_S           # gameScreen's fat content ring - the gap filler
    # LAP_IN is the WHOLE border ring (2 tiles at DECO_S): the border's outer edge sits exactly
    # on the gameplay window rect and every one of its texels laps INWARD over the window, so
    # the bevel reads as the window's own frame and kaleido passes under it. Measured against
    # Jay's composite: hole x -34.3..171.0 y 18.2..122.3 vs his -32.5..170.7 / 20.2..123.0.
    LAP_IN = 2.0 * T64 * DECO_S
    OVERLAP = T64 * DECO_S            # border laps this far over the background's raw edge

    # Border hole = gameplay window shrunk by LAP_IN, so its rect is the window inflated by
    # (content ring - LAP_IN).
    DECO_OUT = DECO_C - LAP_IN
    # Background hole = the border's rect shrunk by OVERLAP, so its rect is that inflated by its
    # own content ring. GS_IN expresses that offset against the border's rect.
    GS_IN = GS_C - OVERLAP

    add(root, "gameScreen",
        A(M.STRETCH, -(DECO_OUT + GS_IN), (GAME_R - DECO_OUT) - GS_IN),
        A(M.START, -(DECO_OUT + GS_IN), GAME_H + (2.0 * DECO_OUT) + (2.0 * GS_IN)),
        sheet=FILLED, mode=RING, layer=0, attach=EDGE, clip=SPILL, scale=GS_S,
        reveal=M.REVEAL_FALL, revealDelay=D_GAME)
    gameScreen = len(d) - 1

    # Border: child of the background, uniformly inset by GS_IN on all four sides, so its lap
    # over the background's raw inner edge is constant at any aspect.
    gameDeco = add(gameScreen, "gameDeco", A(M.STRETCH, GS_IN, GS_IN),
                   A(M.STRETCH, GS_IN, GS_IN),
                   sheet=EMPTY, layer=2, attach=EDGE, clip=SPILL, scale=DECO_S)

    # Two oversized MiniButton arrows sit INSIDE the peep hole, flush into its bottom-left and
    # bottom-right corners. This is a DELIBERATE MAGENTA-RULE EXCEPTION: the only two things
    # this scene declares inside the gameplay window, because they are the window's own
    # controls rather than menu chrome.
    # HOLE_IN is where the Empty frame's art actually stops being transparent, measured off the
    # render: the frame's outer shadow rows are see-through, so the visible hole edge is this
    # far inside gameDeco's rect. Both arrows are children of gameDeco, so they ride the hole
    # at every aspect and nothing about the hole is typed twice.
    HOLE_IN = 18.66
    PEEP_S = 2.8
    PEEP = T32 * PEEP_S
    add(gameDeco, "peepLeft", A(M.START, HOLE_IN, PEEP), A(M.END, HOLE_IN, PEEP),
        sheet=MINI, slice="ARROW_LEFT", mode=NAT, layer=2, kind=M.CELL, scale=PEEP_S,
        attach=EDGE, clip=SPILL)
    add(gameDeco, "peepRight", A(M.END, HOLE_IN, PEEP), A(M.END, HOLE_IN, PEEP),
        sheet=MINI, slice="ARROW_RIGHT", mode=NAT, layer=2, kind=M.CELL, scale=PEEP_S,
        attach=EDGE, clip=SPILL)

    # ============================================ quest window - right column, large scale
    # NOT contained inside the wallpaper slab: it is WIDER than the slab behind it, starts
    # further left than the slab's left edge, and bleeds 20 units off the right so its right
    # border ring lands off-screen. Only the top, left and bottom outer edges are visible;
    # the inner edge runs all the way into the screen corners.
    questWin = add(root, "questWin", A(M.END, -20.0, 218.0), A(M.START, 2.0, 116.0),
                   sheet=EMPTY, layer=2, attach=EDGE, clip=SPILL,
                   reveal=IN, revealFrom=M.FROM_RIGHT, revealDelay=D_QUEST)

    # Two insets. The left one is the heart-container / quest-status readout, the right one
    # is the remains readout. Both are children of the quest window and attach BORDER, so
    # the window's horizontal chrome is subtracted before they are placed.
    # SLOT is declared first because the heart window's height is derived from it: the item
    # row lives BELOW the heart container, so the container stops two slot rows short of the
    # quest window's inner floor.
    # The stat window's top is stated HERE, above its own block, because the quest column
    # below is measured off it: the heart panel's bottom edge lands exactly on the top edge
    # of the data panels, so the two blocks meet with no gap at any aspect.
    STAT_Y = 108.0                               # statWin's outer top
    STAT_TOP = STAT_Y + T64                      # + one RAISING border tile = panel tops

    SUB_S = 0.55
    SLOT = 16.0
    SLOT_S = SLOT / (3.0 * T64)
    ITEM_COLS, ITEM_ROWS = 3, 2

    # The heart column is TALLER THAN ITS OWN WINDOW: it starts on the quest window's border
    # ring, then runs down past that window's floor and over the stat window's top chrome,
    # stopping exactly on the top edge of the data panels. Its height is therefore DERIVED
    # from where those panels start, not from the quest window - so the column and the panels
    # meet on the same line at every aspect and nothing has to be re-measured.
    #
    # Because it now covers ground the stat window's frame also covers, it draws on a HIGHER
    # layer than that frame (5 vs 4) - the extension is meant to sit ON the chrome, and the
    # quest slots (6) and the notebook (7) still sit on the extension.
    HEART_IN = T64 * SUB_S                        # the left window's own border tile
    LEFT_TOP = 2.0 + T64                          # questWin's y start + its border ring
    LEFT_H = (STAT_TOP + HEART_IN) - LEFT_TOP
    # This column does not slide with the rest: it GROWS. It starts as a short letterbox
    # pinned at its own top edge and opens downward to full height, taking the heart readout
    # inside it along with it - one table entry, no extra animation code and nothing for the
    # child to declare.
    leftWin = add(questWin, "leftWin", A(M.START, 0.0, 88.0), A(M.START, 0.0, LEFT_H),
                  sheet=EMPTY, layer=5, scale=SUB_S,
                  reveal=M.REVEAL_GROW, revealDelay=D_QUEST + 4)
    # Remains readout: starts exactly where the left window ends - no dead gap between them -
    # and its right edge still rides the quest window's border ring, which is already off the
    # right of the screen because the quest window bleeds 20 past it.
    # Moved 11 further left so its bevel butts straight against the dark panel - the chunky
    # divider that used to sit between the two top panels is gone.
    add(questWin, "remainsWin", A(M.STRETCH, 77.0, 0.0), A(M.STRETCH, 0.0, 0.0),
        sheet=EMPTY, layer=3, scale=SUB_S)

    # The heart container / quest-status readout fills the WHOLE left window - the item row
    # no longer lives inside it, so nothing is reserved at the bottom.
    # Jay's edit fills this panel's interior DARK: it is a recessed content area, not a
    # transparent hole, so it takes the 5x5 recessed panel art instead of the Empty frame.
    add(leftWin, "heartWin", A(M.STRETCH, 0.0, 0.0), A(M.STRETCH, 0.0, 0.0),
        sheet=ITEMBOX, layer=5, scale=0.4)

    # ===================================================================== quest item row
    # OFF GRID and OUTSIDE the quest window: parented to root, straddling the quest window's
    # bottom edge so the top row sits on its lower chrome and the bottom row lands on the
    # wallpaper below. END-anchored on x with the same right edge the row had inside the
    # window, so it tracks the screen edge exactly like the quest window does.
    # A nine-sliced slot needs THREE tiles on each axis (border, middle, border), so the art
    # scale is derived from the wanted slot size - never the other way round.
    ROW_W = ITEM_COLS * SLOT
    ROW_R = 122.2          # distance from the right edge to the row's right edge
    ROW_Y = 91.0           # top of the first slot row
    # The row itself does NOT animate - the SLOTS do, individually. Each one FALLS in from
    # above on a gravity curve with a per-cell delay, so the grid assembles object by object
    # instead of sliding on as one sheet. The cascade is column-then-row, which reads as the
    # row being dealt out left to right.
    itemRow = add(root, "itemRow", A(M.END, ROW_R, ROW_W), A(M.START, ROW_Y, ITEM_ROWS * SLOT),
                  kind=M.GROUP, attach=EDGE, clip=SPILL)
    for r in range(ITEM_ROWS):
        for c in range(ITEM_COLS):
            add(itemRow, "questSlot%d%d" % (r, c),
                A(M.START, c * SLOT, SLOT), A(M.START, r * SLOT, SLOT),
                sheet=SLOT_ITEM, mode=NS, layer=6, kind=M.CELL, scale=SLOT_S,
                attach=EDGE, clip=SPILL, shadow=False,
                reveal=M.REVEAL_FALL, revealDelay=D_QUEST + 2 + c + (2 * r))

    # Bombers' Notebook: the one OUTSET slot - BigButton SQUARE, the raised variant - so it
    # reads as sticking out of the row instead of being another hole. Its TOP-LEFT corner is
    # exactly the top-left corner of r2c3 (bottom row, third column); it is larger, so it
    # grows right and down out of the row.
    # The SQUARE sprite has 3 texels of transparent pad on its left and 2 on its top, so the
    # placement backs those out - otherwise the ART corner sits inside the r2c3 corner.
    NB_S = 1.35
    NB = T64 * NB_S
    TEX = (T64 / 64.0) * NB_S
    add(itemRow, "notebook",
        A(M.START, (ITEM_COLS - 1) * SLOT - 3.0 * TEX, NB),
        A(M.START, (ITEM_ROWS - 1) * SLOT - 2.0 * TEX, NB),
        sheet=BIG, slice="SQUARE", mode=NAT, layer=7, kind=M.CELL, scale=NB_S,
        attach=EDGE, clip=SPILL,
        reveal=M.REVEAL_FALL, revealDelay=D_QUEST + 4 + (ITEM_COLS - 1) + (2 * (ITEM_ROWS - 1)))

    # ================================================================= stat / data window
    # Overhangs the column to the LEFT, runs off the right, and now reaches DOWN far enough
    # to overlap the bottom-centre bonus panel by ~5 units. Drawn on a higher layer than
    # that panel so the overlap reads as this window sitting on top.
    # END -14 with width 197 puts the left edge exactly on the gameplay seam
    # (screenX1 - 183) at EVERY aspect - no overhang into the gameplay window, and no baked
    # mockup width. It now reaches from just under the quest window down over the bottom
    # wall's top chrome; the item row above draws on a higher layer, so the row sits ON it.
    # Top stays at 108. The bottom now runs PAST the screen edge so the three panels, which
    # stretch to the inner height, reach down to the bonus bar. The bar is drawn on a higher
    # layer than the panels, so the bar cuts them off cleanly at its own top edge.
    STAT_H = 151.25
    statWin = add(root, "statWin", A(M.END, -14.0, 197.0), A(M.START, STAT_Y, STAT_H),
                  sheet=RAISING, layer=4, attach=EDGE, clip=SPILL,
                  reveal=IN, revealFrom=M.FROM_RIGHT, revealDelay=D_STAT)

    # Three stat boxes: one on each inner edge, one exactly centred between them, all three
    # stretched to the full inner height. Same width, so CENTER lands perfectly between.
    STAT_W = 56.0
    STAT_S = 0.55
    # Each panel GROWS: it starts as a short letterbox pinned at the window's content top
    # and opens downward, dragging its scroll arrows and its whole row list with it. Three
    # delays one beat apart, so they unroll left to right rather than as one block.
    statBoxes = [
        add(statWin, "statL", A(M.START, 0.0, STAT_W), A(M.STRETCH, 0.0, 0.0),
            sheet=POPUP, layer=5, scale=STAT_S,
            reveal=M.REVEAL_GROW, revealDelay=D_STAT + 3),
        add(statWin, "statM", A(M.CENTER, 0.0, STAT_W), A(M.STRETCH, 0.0, 0.0),
            sheet=POPUP, layer=5, scale=STAT_S,
            reveal=M.REVEAL_GROW, revealDelay=D_STAT + 5),
        add(statWin, "statR", A(M.END, 0.0, STAT_W), A(M.STRETCH, 0.0, 0.0),
            sheet=POPUP, layer=5, scale=STAT_S,
            reveal=M.REVEAL_GROW, revealDelay=D_STAT + 7),
    ]

    # ---------------------------------------------------------- stat panel scroll bars
    # Each data panel scrolls, so each one gets an up/down arrow pair sitting IN its own
    # right-hand border groove - the popup's bevel already reads as the track, so no track
    # art is declared. Two rows per panel, and the panel list above is what they loop over:
    # adding a fourth data panel picks up its scroll bar for free.
    #
    # X: same rule the song page arrows use - centre the arrow inside the popup's border
    #    band, measured from the frame rect edge, so it scales with the frame.
    # Y: the up arrow's top sits on the popup's measured content recess (POPUP_IN_Y at this
    #    frame's scale). The down arrow is anchored so its BOTTOM lands exactly on the bonus
    #    bar's top edge - the panels run down past it behind the bar (layer 6 cuts them off),
    #    so the panel's own bottom is not the visible bottom and cannot be used here.
    SB_S = 0.6                                   # 32-texel arrow at 60% - Jay's edit size
    SB = T32 * SB_S
    SB_IN_X = 70.0 * (TILE / M.TILE_SRC) * STAT_S
    SB_IN_Y = 72.0 * (TILE / M.TILE_SRC) * STAT_S
    SB_X = (SB_IN_X - SB) * 0.5
    # The bonus bar below is exactly two 32-texel rows tall and anchored to the screen
    # bottom, so its top edge is this - stated once here, asserted against BONUS_H below.
    BAR_TOP = M.DESIGN_H - 2.0 * T32
    SB_DOWN_Y = BAR_TOP - SB - STAT_TOP
    for n, box in zip(("L", "M", "R"), statBoxes):
        add(box, "stat%sUp" % n, A(M.END, SB_X, SB), A(M.START, SB_IN_Y, SB),
            sheet=MINI, slice="ARROW_UP", mode=NAT, layer=5, kind=M.CELL,
            scale=SB_S, attach=EDGE, clip=SPILL)
        add(box, "stat%sDown" % n, A(M.END, SB_X, SB), A(M.START, SB_DOWN_Y, SB),
            sheet=MINI, slice="ARROW_DOWN", mode=NAT, layer=5, kind=M.CELL,
            scale=SB_S, attach=EDGE, clip=SPILL)

    # ------------------------------------------------------------- stat panel data rows
    # Each data panel is a LIST. The rows fill the popup's measured inner recess and abut,
    # so the run reads as one list box. The pitch is DERIVED: the visible list runs from the
    # panel's content recess down to the bar's top edge (the panels themselves continue
    # behind the bar), divided by the row count - so the last row lands exactly on the bar
    # and no partial row is ever visible.
    #
    # A row is a slot, not a widget: what goes IN it (text, an icon, a bar) is a draw
    # callback on the consumer side. Adding a fourth panel above picks up its rows for free.
    STAT_ROWS = 9
    ROW_H = (BAR_TOP - (STAT_TOP + SB_IN_Y)) / STAT_ROWS
    ROW_W = STAT_W - 2.0 * SB_IN_X
    for n, box in zip(("L", "M", "R"), statBoxes):
        dataRows(box, "stat%s" % n, STAT_ROWS, SB_IN_X, SB_IN_Y, ROW_W, ROW_H, 5)

    # ================================================================ bottom bonus region
    # A slim bar: the 9-slice's TOP AND BOTTOM ROWS ONLY, no middle row, run out sideways.
    #
    # This is data, not a special draw mode. Height is set to EXACTLY TWO tiles, so in
    # So2h_UiSolveFrameAxis the two fixed rows already consume the whole span, slack comes out
    # 0, and the middle row emits no quads at all (repeat mode stops at MIN_QUAD_PX). The two
    # border rows land back to back on a single seam. Horizontally it is still a normal run,
    # so the middle column tiles across the full width.
    BONUS_S = T32 / T64        # one drawn row = one 32-texel arrow tall
    BONUS_H = 2.0 * T64 * BONUS_S
    assert abs((M.DESIGN_H - BONUS_H) - BAR_TOP) < 1e-6, "BAR_TOP drifted from the bar"
    # Left edge now runs the full width of the screen instead of starting mid-way. Layer 6 -
    # above the stat window (4) and its panels (5) - so the panels stretching down from above
    # are cut off by the bar rather than covering it.
    bonusWin = add(root, "bonusWin", A(M.STRETCH, 0.0, -14.0), A(M.END, 0.0, BONUS_H),
                   reveal=IN, revealFrom=M.FROM_BOTTOM, revealDelay=D_BAR,
                    sheet=SLAB9, layer=6, attach=EDGE, clip=SPILL, scale=BONUS_S)

    # ===================================================================== song block
    # The whole block reads as the BOTTOM OF THE GAMEPLAY WINDOW continuing downward off the
    # screen, so both windows are anchored to the LEFT SCREEN EDGE and the staff stops short
    # of the gameplay seam instead of crossing it.
    #
    # Song preview: a POPUP frame with the SongBackdrop staff art inside it. STRETCHed from
    # 8 units off the left edge to 10 units short of the seam (screenX1 - 183), so at every
    # aspect it is exactly the gameplay window's width minus a hair - shorter than it was,
    # measured from the left edge, at both 4:3 and 16:9. No baked mockup width.
    # Jay's edit slides the whole staff group one tile RIGHT and two tiles UP, so the bar
    # laps over the bottom of the peep hole instead of clearing it.
    STAFF_DX = T64 / 2.0      # +1 32-texel tile
    STAFF_DY = -T64           # -2 32-texel tiles
    SONG_L = -8.0 + STAFF_DX
    SONG_R = 193.0 - STAFF_DX  # 183 seam + 10 clearance, shifted right
    prevWin = add(root, "songPrevWin", A(M.STRETCH, SONG_L, SONG_R),
                  A(M.START, 158.0 + SONG_DY + STAFF_DY, 56.0),
                  sheet=POPUP, layer=4, attach=EDGE, clip=SPILL, scale=0.95,
                  reveal=IN, revealFrom=M.FROM_LEFT, revealDelay=D_SONG)
    # The staff strip's top and bottom rows are transparent padding, so it is lifted to let
    # that padding ride the frame's own chrome and only the staff land in the content area.
    # STRETCHed now, not a fixed width, so it tracks the frame at any aspect.
    add(prevWin, "songStaff", A(M.STRETCH, 0.0, 0.0), A(M.START, -21.0, 4.0 * T32 * 2.1),
        sheet=SONGBK, mode=RUN, layer=4, clip=SPILL, scale=2.1,
        reveal=IN, revealFrom=M.FROM_LEFT, revealDelay=D_STAFF)

    # Song selection: a 2x6 container - 12 slots - with left/right buttons that page between
    # the first 12 songs and the second 12. The cell count is a loop bound over the SONGS
    # table and the colours come out of that same table, so adding a song is one row.
    SONG_COLS, SONG_ROWS = 6, 2
    NOTE_S = 1.5
    NOTE_P = T32 * NOTE_S
    ARROW_S = 1.3
    ARROW = T32 * ARROW_S

    SEL_S = 0.75
    TILEP = T64 * SEL_S      # one drawn frame tile at this window's scale

    # The POPUP frame's dark recess does NOT begin on a tile boundary. Measured off the art:
    # the inner bevel line runs at source texel 64-70 across and 65-72 down, so the recess
    # starts at texel 70 / 72 - about 1.1 tiles, NOT the 2 tiles attach=CONTENT would inset.
    # These two numbers are that measurement, converted at the draw scale.
    POPUP_IN_X = 70.0 * (TILE / M.TILE_SRC) * SEL_S
    POPUP_IN_Y = 72.0 * (TILE / M.TILE_SRC) * SEL_S

    # Symmetric fit, every edge derived from the measured recess:
    #   the note block's top-left corner IS the frame's inner (dark) corner, and the frame
    #   carries the same measured inset on the right and bottom, so the block is fully
    #   enclosed - no column hanging over the border ring.
    BLOCK_W = SONG_COLS * NOTE_P
    BLOCK_H = SONG_ROWS * NOTE_P
    SEL_W = 2.0 * POPUP_IN_X + BLOCK_W
    SEL_H = 2.0 * POPUP_IN_Y + BLOCK_H
    # Jay's edit centres the select block ON THE STAFF BAR and hangs it off the bar's bottom
    # border, so it is a CHILD of the staff frame: CENTER on x holds that relationship at any
    # aspect, and the y anchor is measured off the staff frame's own bottom edge.
    SEL_LAP = 20.5            # how far the block laps up onto the staff frame's bottom border
    # POP, not SLIDE: it rises into place from below, sails a few units past the staff it
    # hangs off, and snaps back down onto it - so the block reads as being thrown onto the
    # staff rather than parked there. Its own beat, after the staff has landed.
    selWin = add(prevWin, "songSelWin", A(M.CENTER, 0.0, SEL_W),
                 A(M.END, SEL_LAP - SEL_H, SEL_H),
                 sheet=POPUP, layer=5, attach=EDGE, clip=SPILL, scale=SEL_S,
                 reveal=M.REVEAL_POP, revealFrom=M.FROM_BOTTOM, revealDelay=D_NOTES)

    # The 12 notes live in an invisible group pinned to that inner corner. attach=EDGE plus
    # the measured offsets, because the sheet's declared content ring (2 tiles) is coarser
    # than where the art's recess actually starts.
    noteWin = add(selWin, "songNotes", A(M.START, POPUP_IN_X, BLOCK_W),
                  A(M.START, POPUP_IN_Y, BLOCK_H),
                  kind=M.GROUP, attach=EDGE, clip=SPILL)

    # Page arrows are MOUNTED ON THE FRAME, centred in its left and right border bands, and
    # vertically centred on the window. They are children of selWin, so they hold that
    # relationship at any size, and because they live inside the frame's own footprint they
    # can never leave the screen while the frame is on it - structural, not a clamp.
    ARROW_IN = (POPUP_IN_X - ARROW) * 0.5
    add(selWin, "songPageL", A(M.START, ARROW_IN, ARROW), A(M.CENTER, 0.0, ARROW),
        sheet=MINI, slice="ARROW_LEFT", mode=NAT, layer=6, kind=M.CELL, scale=ARROW_S,
        attach=EDGE, clip=SPILL)
    add(selWin, "songPageR", A(M.END, ARROW_IN, ARROW), A(M.CENTER, 0.0, ARROW),
        sheet=MINI, slice="ARROW_RIGHT", mode=NAT, layer=6, kind=M.CELL, scale=ARROW_S,
        attach=EDGE, clip=SPILL)
    page = 0
    for r in range(SONG_ROWS):
        for c in range(SONG_COLS):
            idx = page * (SONG_COLS * SONG_ROWS) + r * SONG_COLS + c
            name, rgb = SONGS[idx]
            add(noteWin, "song%d%d" % (r, c),
                A(M.START, c * NOTE_P, NOTE_P), A(M.START, r * NOTE_P, NOTE_P),
                sheet=MINI, slice="NOTE", mode=NAT, layer=6, kind=M.CELL, scale=NOTE_S,
                rgb=rgb, attach=EDGE, clip=SPILL)

    # =================================================================== context menu
    # A small list window hanging off the BOTTOM of the song select block: same POPUP frame,
    # same row art, centred on the block so it reads as its continuation. It runs down PAST
    # the bonus bar's top edge - the bar is on a higher layer (6 vs this 4), so the bar cuts
    # it off and no bottom border is ever visible. That is the same trick the stat panels use.
    #
    # It is a child of selWin, so it inherits that block's horizontal centre for free at any
    # aspect, and its top is measured off selWin's bottom edge by THIS window's own recess
    # (CTX_IN_Y, at CTX_S - not selWin's scale), so it laps up under that block by exactly
    # one of its own border bands. No baked y.
    CTX_S = STAT_S
    CTX_IN_X = 70.0 * (TILE / M.TILE_SRC) * CTX_S
    CTX_IN_Y = 72.0 * (TILE / M.TILE_SRC) * CTX_S
    CTX_ROWS = 3                 # title row + two entries
    CTX_W = 149.3
    CTX_H = 2.0 * CTX_IN_Y + CTX_ROWS * ROW_H
    ctxWin = add(selWin, "ctxWin", A(M.CENTER, 0.0, CTX_W),
                 A(M.END, CTX_IN_Y - CTX_H, CTX_H),
                 sheet=POPUP, layer=4, attach=EDGE, clip=SPILL, scale=CTX_S,
                 reveal=M.REVEAL_SWAP, revealFrom=M.FROM_BOTTOM, revealDelay=D_CTX)
    dataRows(ctxWin, "ctx", CTX_ROWS, CTX_IN_X, CTX_IN_Y,
             CTX_W - 2.0 * CTX_IN_X, ROW_H, 4)

    # ============================================================ page buttons (32x32)
    # Parented to the bonus panel and anchored to its lower corners, so they ride its
    # border ring rather than sitting in the screen corners where the song block lives.
    # With the middle row gone the bar has no interior, so the arrows can no longer attach
    # BORDER (that would inset them into zero height). They ride the bar centred instead.
    # The bar deliberately bleeds 14 off the right edge, so the right arrow is inset by that
    # bleed plus one border row; the left arrow by one border row. Both are measured off the
    # bar, so they hold at any aspect.
    ARROW_ROW = T64 * BONUS_S
    add(bonusWin, "pageLeft", A(M.START, ARROW_ROW, T32), A(M.CENTER, 0.0, T32),
        sheet=MINI, slice="ARROW_LEFT", mode=NAT, layer=9, kind=M.CELL, attach=EDGE,
        clip=SPILL)
    add(bonusWin, "pageRight", A(M.END, 14.0 + ARROW_ROW, T32), A(M.CENTER, 0.0, T32),
        sheet=MINI, slice="ARROW_RIGHT", mode=NAT, layer=9, kind=M.CELL, attach=EDGE,
        clip=SPILL)

    # ================================================================== toolbar slots
    # The bar is the TOOL BELT: a run of recessed cells between the two page arrows, one per
    # tool the game has. The count is DERIVED, never baked - it is the tool table's length
    # capped by how many whole cells fit between the arrows at this aspect, so the run grows
    # as tools are added and there are NO blank cells past the last real tool: the tail is
    # just bare bar. Adding a tool is one row in TOOLS.
    TOOLS = ["slot%02d" % i for i in range(26)]   # placeholder table - Part B fills it
    TOOL_W = 1.5 * T32          # measured cell width
    TOOL_H = 10.6               # measured cell height, centred on the bar's two rows
    TOOL_GAP = 2.97
    TOOL_P = TOOL_W + TOOL_GAP
    # The bar bleeds 14 off the right edge, so its own width is the screen plus that bleed.
    BAR_W = (M.DESIGN_H * aspect) + 14.0
    TOOL_X0 = ARROW_ROW + T32 + TOOL_GAP * 0.5           # clear of the left page arrow
    TOOL_END = BAR_W - (14.0 + ARROW_ROW + T32) - TOOL_GAP * 0.5  # clear of the right one
    TOOL_N = min(len(TOOLS), int((TOOL_END - TOOL_X0) / TOOL_P))
    for k in range(TOOL_N):
        add(bonusWin, "tool%02d" % k, A(M.START, TOOL_X0 + k * TOOL_P, TOOL_W),
            A(M.CENTER, 0.0, TOOL_H), sheet=SLOT_ITEM, mode=NS, layer=7, kind=M.CELL,
            scale=TOOL_H / (3.0 * T64), attach=EDGE, clip=SPILL, shadow=False)

    return M.Ctx(d, aspect=aspect)
