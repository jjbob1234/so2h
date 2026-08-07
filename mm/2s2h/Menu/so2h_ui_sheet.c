/*
 * File: so2h_ui_sheet.c
 * Description: SO2H [Menu] so2h_ui sheet registry and slice lookup.
 *
 * ---------------------------------------------------------------------------------------
 * WHY A REGISTRY AND NOT A PILE OF DEFINES
 * ---------------------------------------------------------------------------------------
 * The first pass gave every one of 43 tiles its own texture define, its own asset-header
 * entry and its own draw call, and 26 of them ended up unused because nothing recorded what
 * was on which sheet. Here a sheet is one texture plus a declared grid, a slice is an index
 * into that grid, and the whole mapping comes from a checked-in manifest
 * (mm/assets/custom/textures/so2h_menu/sheets.json) rather than from anybody looking at
 * pixels and guessing.
 *
 * Slice ids are packed rather than named at runtime: (row * cols) + col. Names live in the
 * manifest and in the generated header, where they cost nothing at runtime.
 */

#include "so2h_ui_internal.h"

static const So2hUiSheetDef* sSheets = NULL;
static s32 sSheetCount = 0;

void So2h_Ui_RegisterSheets(const So2hUiSheetDef* sheets, s32 count) {
    if ((sheets == NULL) || (count <= 0)) {
        sSheets = NULL;
        sSheetCount = 0;
        return;
    }
    if (count > SO2H_UI_MAX_SHEETS) {
        count = SO2H_UI_MAX_SHEETS;
    }
    sSheets = sheets;
    sSheetCount = count;
}

/**
 * Whole sheet record. Exposed to so2h_ui_draw.c because the drawer needs the grid, the unit
 * and the declared repeating range, not just a source rect.
 */
const So2hUiSheetDef* So2h_UiSheet_Def(u16 sheet) {
    s32 i;

    if (sSheets == NULL) {
        return NULL;
    }
    for (i = 0; i < sSheetCount; i++) {
        if (sSheets[i].id == sheet) {
            return &sSheets[i];
        }
    }
    return NULL;
}

u16 So2h_Ui_Slice(u16 sheet, s16 col, s16 row) {
    const So2hUiSheetDef* s = So2h_UiSheet_Def(sheet);

    if ((s == NULL) || (col < 0) || (row < 0) || (col >= s->cols) || (row >= s->rows)) {
        return SO2H_UI_INVALID;
    }
    return (u16)((row * s->cols) + col);
}

/**
 * Source rect of a slice, in texels. This is what feeds gDPLoadTextureTile - sub-rect
 * sampling that the codebase has had all along (mm/include/PR/gbi.h:3529, used by PreRender.c
 * and z_fbdemo.c) and that the first pass simply never reached for, which is why every piece
 * of art needed its own whole-texture file.
 */
s32 So2h_UiSheet_SliceRect(u16 sheet, u16 slice, s16* sx, s16* sy, s16* sw, s16* sh) {
    const So2hUiSheetDef* s = So2h_UiSheet_Def(sheet);
    s16 col;
    s16 row;

    if ((s == NULL) || (slice == SO2H_UI_INVALID) || (s->cols <= 0)) {
        return 0;
    }
    if ((s32)slice >= (s32)(s->cols * s->rows)) {
        return 0;
    }
    col = (s16)(slice % s->cols);
    row = (s16)(slice / s->cols);

    *sx = (s16)(col * s->unit);
    *sy = (s16)(row * s->unit);
    *sw = s->unit;
    *sh = s->unit;

    return 1;
}

s32 So2h_UiSheet_Dims(u16 sheet, s16* w, s16* h) {
    const So2hUiSheetDef* s = So2h_UiSheet_Def(sheet);

    if (s == NULL) {
        return 0;
    }
    *w = s->width;
    *h = s->height;

    return 1;
}

const void* So2h_UiSheet_Texture(u16 sheet) {
    const So2hUiSheetDef* s = So2h_UiSheet_Def(sheet);

    return (s != NULL) ? s->texture : NULL;
}

/**
 * Float form of So2h_UiSheet_SliceRect. The drawer works in f32 the whole way down and only
 * collapses to integers at the rect command, so handing it s16 here would reintroduce the
 * double-rounding the layout engine exists to avoid.
 */
s32 So2h_UiSheet_SliceRectF(u16 sheet, u16 slice, f32* sx, f32* sy, f32* sw, f32* sh) {
    s16 x;
    s16 y;
    s16 w;
    s16 h;

    if (!So2h_UiSheet_SliceRect(sheet, slice, &x, &y, &w, &h)) {
        return 0;
    }
    *sx = (f32)x;
    *sy = (f32)y;
    *sw = (f32)w;
    *sh = (f32)h;

    return 1;
}
