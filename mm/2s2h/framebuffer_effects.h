#ifndef FRAMEBUFFER_EFFECTS_H
#define FRAMEBUFFER_EFFECTS_H

#include "ultra64.h"

extern s32 gPauseFrameBuffer;
extern s32 gBlurFrameBuffer;
extern s32 gReusableFrameBuffer;
extern s32 gN64ResFrameBuffer;

void FB_CreateFramebuffers(void);
void FB_CopyToFramebuffer(Gfx** gfxp, s32 fb_src, s32 fb_dest, u8 oncePerFrame, u8* hasCopied);
void FB_WriteFramebufferSliceToCPU(Gfx** gfxp, void* buffer, u8 byteSwap);
void FB_DrawFromFramebuffer(Gfx** gfxp, s32 fb, u8 alpha);
void FB_DrawFromFramebufferScaled(Gfx** gfxp, s32 fb, u8 alpha, float scaleX, float scaleY);
// SO2H [Menu]: top-left-origin variant of FB_DrawFromFramebufferScaled. Takes an explicit
// destination rect in N64 320x240 screen coordinates and clears the screen to black first,
// so no stale garbage is left outside the shrunken image.
void FB_DrawFromFramebufferRect(Gfx** gfxp, s32 fb, u8 alpha, s32 leftX, s32 topY, s32 rightX, s32 bottomY);

#endif // FRAMEBUFFER_EFFECTS_H
