/*
 * File: so2h_ui_sfx.c
 * Description: SO2H [Menu] the sound of the open / close performance.
 *
 * ---------------------------------------------------------------------------------------
 * WHY THE MENU MIXES ITS OWN VOICES
 * ---------------------------------------------------------------------------------------
 * The performance wants one travel voice and one landing thud PER MOVING NODE, all at once.
 * The vanilla sfx bank cannot do that: gChannelsPerBank[layout][BANK_SYSTEM] is 2 in every
 * one of the four channel layouts (mm/src/audio/code_8019AF00.c), so at most two menu voices
 * can sound together and everything else is evicted by priority. The bank -> channel split is
 * compiled into the NA_BGM_GENERAL_SFX sequence that ships inside the ROM, so it cannot be
 * widened from here. Twenty windows landing would come out as a two-voice stutter.
 *
 * So the menu keeps a small pool of its own and mixes it into the SAME output block the audio
 * thread has just filled, one step before that block is handed to AudioPlayer_Play. It owns no
 * sequence, no channel and no sfx table entry, and it re-reads the game's MasterVolume and
 * SoundEffectsVolume CVars every block, so the volume sliders and mute behave exactly as they
 * do for game audio. (playback.c applies MasterVolume to game notes only, which is why BOTH
 * factors are applied here rather than one.)
 *
 * The two sfx-bank slots are still bound, behind SO2H_UI_SFX_BANK_ENABLED, so a future one-off
 * UI sound can take the ordinary route. Nothing in the reveal / close path may use them, or
 * every sound would be triggered twice.
 *
 * ---------------------------------------------------------------------------------------
 * THREADING
 * ---------------------------------------------------------------------------------------
 * So2h_UiSfx_Slide / _SlideStop / _Placed are called from the GAME thread while laying out.
 * So2h_UiSfx_MixInto runs on the AUDIO thread. They meet over one single-producer /
 * single-consumer ring: the producer fills a slot then publishes it by bumping the write index,
 * the consumer only ever reads below that index. A full ring drops the command rather than
 * blocking - one missing click in a twenty-piece clatter is not worth a stalled game thread.
 * Voices are allocated and freed ONLY inside the drain, i.e. only on the audio thread.
 *
 * Pitch is per node and per open: So2h_UiHash(nodeId ^ openSeed) picks it, so the clatter is
 * different every time the menu opens but identical if you replay the same open, and it never
 * calls rand() (which the rest of the menu is also careful not to do).
 *
 * CAVEAT, deliberate: ogg decode is asynchronous and progressive (AudioSampleFactory's
 * OggDecoderWorker fills sampleAddr on a detached thread and never raises a "done" flag). The
 * only signal available is sampleAddr becoming non-NULL. Boot to the first pause is orders of
 * magnitude longer than the ~250 ms decode, so latching on non-NULL is safe in practice; if it
 * ever were not, the worst case is one quiet menu open.
 */

#include "so2h_ui_internal.h"

#include "global.h"
#include "sfx.h"
#include "assets/2s2h_assets.h"

#include <libultraship/bridge/consolevariablebridge.h>

#if SO2H_UI_SFX_ENABLED

// The header spells the ids out numerically so it does not have to include sfx.h. Keep honest.
#if SO2H_UI_SFX_SLIDE_ID != NA_SE_SY_DUMMY_13
#error "SO2H_UI_SFX_SLIDE_ID no longer matches NA_SE_SY_DUMMY_13"
#endif
#if SO2H_UI_SFX_PLACED_ID != NA_SE_SY_DUMMY_14
#error "SO2H_UI_SFX_PLACED_ID no longer matches NA_SE_SY_DUMMY_14"
#endif

Sample* ResourceMgr_LoadSampleByName(const char* path);
SoundFont* ResourceMgr_LoadAudioSoundFontByName(const char* path);
extern char** gFontMap;

// ---------------------------------------------------------------------------------------
// Latched samples
// ---------------------------------------------------------------------------------------
// Written once by the game thread in So2h_UiSfx_Bind, read by the audio thread. A voice keeps
// its own copy of these two fields, so a torn read can never be observed mid-playback: the
// pointer and the length are only ever picked up together, at voice start, after sBound.

typedef struct So2hUiSfxClip {
    const s16* pcm; // mono s16 at the ogg's native rate
    s32 frames;
} So2hUiSfxClip;

static So2hUiSfxClip sClip[2]; // [0] = slide (looping bed), [1] = placed (one shot)
static volatile u8 sBound = false;

#define SO2H_UI_SFX_CLIP_SLIDE 0
#define SO2H_UI_SFX_CLIP_PLACED 1

// ---------------------------------------------------------------------------------------
// Command ring (game thread -> audio thread)
// ---------------------------------------------------------------------------------------

#define SO2H_UI_SFX_CMD_START 0 // begin a voice: clip, pitch, looping or not
#define SO2H_UI_SFX_CMD_STOP 1  // release the looping voice belonging to one node

typedef struct So2hUiSfxCmd {
    u8 op;
    u8 clip;
    s16 node;
    f32 pitch;
} So2hUiSfxCmd;

static So2hUiSfxCmd sCmd[SO2H_UI_SFX_CMDS];
static volatile u32 sCmdWrite = 0; // only the game thread advances this
static volatile u32 sCmdRead = 0;  // only the audio thread advances this

static void So2h_UiSfx_Push(u8 op, u8 clip, s16 node, f32 pitch) {
    u32 w = sCmdWrite;
    u32 next = (w + 1) & (SO2H_UI_SFX_CMDS - 1);

    if (next == sCmdRead) {
        return; // ring full - drop it rather than stall the game thread
    }

    sCmd[w & (SO2H_UI_SFX_CMDS - 1)].op = op;
    sCmd[w & (SO2H_UI_SFX_CMDS - 1)].clip = clip;
    sCmd[w & (SO2H_UI_SFX_CMDS - 1)].node = node;
    sCmd[w & (SO2H_UI_SFX_CMDS - 1)].pitch = pitch;

    // Publish only after the payload is written. There is exactly one producer, so a plain
    // store is enough ordering on every target this runs on.
    sCmdWrite = next;
}

// ---------------------------------------------------------------------------------------
// Voice pool (audio thread only)
// ---------------------------------------------------------------------------------------

#define SO2H_UI_SFX_ENV_ATTACK 0
#define SO2H_UI_SFX_ENV_HOLD 1
#define SO2H_UI_SFX_ENV_RELEASE 2

typedef struct So2hUiSfxVoice {
    u8 active;
    u8 loop;
    u8 env;
    s16 node;      // owner, so a STOP can find its travel voice again
    const s16* pcm;
    s32 frames;
    f32 pos;       // fractional read head, in source frames
    f32 step;      // source frames consumed per output frame
    f32 gain;      // static per-clip level, before the envelope
    f32 envGain;   // 0..1, ramped
    f32 envStep;   // per output frame
} So2hUiSfxVoice;

static So2hUiSfxVoice sVoice[SO2H_UI_SFX_VOICES];
static f32 sAcc[SO2H_UI_SFX_MAX_FRAMES]; // mono accumulator, reused every block

static So2hUiSfxVoice* So2h_UiSfx_Alloc(void) {
    s32 i;
    s32 quietest = 0;
    f32 quietestGain = 1.0e9f;

    for (i = 0; i < SO2H_UI_SFX_VOICES; i++) {
        if (!sVoice[i].active) {
            return &sVoice[i];
        }
    }

    // Overprovisioned on purpose, so this should not happen. If it does, steal the voice that
    // is already fading out hardest - the least audible thing to lose.
    for (i = 0; i < SO2H_UI_SFX_VOICES; i++) {
        f32 g = sVoice[i].gain * sVoice[i].envGain;

        if (g < quietestGain) {
            quietestGain = g;
            quietest = i;
        }
    }

    return &sVoice[quietest];
}

static void So2h_UiSfx_Drain(void) {
    while (sCmdRead != sCmdWrite) {
        const So2hUiSfxCmd* c = &sCmd[sCmdRead & (SO2H_UI_SFX_CMDS - 1)];

        if (c->op == SO2H_UI_SFX_CMD_START) {
            const So2hUiSfxClip* clip = &sClip[c->clip];

            if ((clip->pcm != NULL) && (clip->frames > 1)) {
                So2hUiSfxVoice* v = So2h_UiSfx_Alloc();

                v->active = true;
                v->loop = (c->clip == SO2H_UI_SFX_CLIP_SLIDE);
                v->env = SO2H_UI_SFX_ENV_ATTACK;
                v->node = c->node;
                v->pcm = clip->pcm;
                v->frames = clip->frames;
                v->pos = 0.0f;
                v->step = SO2H_UI_SFX_TUNING * c->pitch;
                v->gain = (c->clip == SO2H_UI_SFX_CLIP_SLIDE) ? SO2H_UI_SFX_SLIDE_VOL : SO2H_UI_SFX_PLACED_VOL;
                v->envGain = 0.0f;
                v->envStep = 1.0f / (f32)SO2H_UI_SFX_ATTACK_F;
            }
        } else {
            s32 i;

            // Release every looping voice this node owns. A node only ever holds one, but
            // sweeping the pool costs nothing and cannot leave a voice stuck on.
            for (i = 0; i < SO2H_UI_SFX_VOICES; i++) {
                if (sVoice[i].active && sVoice[i].loop && (sVoice[i].node == c->node) &&
                    (sVoice[i].env != SO2H_UI_SFX_ENV_RELEASE)) {
                    sVoice[i].env = SO2H_UI_SFX_ENV_RELEASE;
                    sVoice[i].envStep = 1.0f / (f32)SO2H_UI_SFX_RELEASE_F;
                }
            }
        }

        sCmdRead = (sCmdRead + 1) & (SO2H_UI_SFX_CMDS - 1);
    }
}

/** One voice into the mono accumulator. Linear interpolation; loops wrap, one-shots retire. */
static void So2h_UiSfx_RenderVoice(So2hUiSfxVoice* v, s32 frames) {
    s32 i;

    for (i = 0; i < frames; i++) {
        s32 i0 = (s32)v->pos;
        s32 i1 = i0 + 1;
        f32 frac;
        f32 s;

        if (i0 >= v->frames) {
            if (!v->loop) {
                v->active = false;
                return;
            }
            v->pos -= (f32)v->frames;
            i0 = (s32)v->pos;
            i1 = i0 + 1;
        }
        if (i1 >= v->frames) {
            i1 = v->loop ? 0 : i0;
        }

        frac = v->pos - (f32)i0;
        s = ((f32)v->pcm[i0] * (1.0f - frac)) + ((f32)v->pcm[i1] * frac);

        switch (v->env) {
            case SO2H_UI_SFX_ENV_ATTACK:
                v->envGain += v->envStep;
                if (v->envGain >= 1.0f) {
                    v->envGain = 1.0f;
                    v->env = SO2H_UI_SFX_ENV_HOLD;
                }
                break;

            case SO2H_UI_SFX_ENV_RELEASE:
                v->envGain -= v->envStep;
                if (v->envGain <= 0.0f) {
                    v->envGain = 0.0f;
                    v->active = false;
                    return;
                }
                break;

            default:
                break;
        }

        sAcc[i] += s * v->gain * v->envGain;
        v->pos += v->step;
    }

    // A one-shot that ran off the end on the very last frame retires here rather than waiting
    // for the next block to notice.
    if (!v->loop && ((s32)v->pos >= v->frames)) {
        v->active = false;
    }
}

void So2h_UiSfx_MixInto(s16* out, s32 frames) {
    f32 vol;
    s32 i;
    s32 any = false;

    if (!sBound || (out == NULL) || (frames <= 0)) {
        return;
    }
    // Freeze diagnostics. The game thread waits on the audio thread every block (BenPort.cpp
    // OTRAudio_Thread / cv_from_thread), so a wedge in here would look exactly like a graphics
    // hang: picture frozen, audio dead, no log. This switch takes the whole pool out of the
    // path so that possibility can be eliminated in one pause.
    if (CVarGetInteger("gSo2h.Ui.NoSfxMix", 0)) {
        return;
    }
    if (frames > SO2H_UI_SFX_MAX_FRAMES) {
        frames = SO2H_UI_SFX_MAX_FRAMES; // cannot happen at 560*3, but never write past the end
    }

    So2h_UiSfx_Drain();

    for (i = 0; i < SO2H_UI_SFX_VOICES; i++) {
        if (sVoice[i].active) {
            any = true;
            break;
        }
    }
    if (!any) {
        return; // silent menu costs one scan, not a cleared accumulator
    }

    for (i = 0; i < frames; i++) {
        sAcc[i] = 0.0f;
    }

    for (i = 0; i < SO2H_UI_SFX_VOICES; i++) {
        if (sVoice[i].active) {
            So2h_UiSfx_RenderVoice(&sVoice[i], frames);
        }
    }

    // Both factors, every block: MasterVolume is applied to game notes down in playback.c and
    // the SFX slider is a sequence-player port scale, so neither reaches this buffer by itself.
    vol = CVarGetFloat("gSettings.Audio.MasterVolume", 1.0f) * CVarGetFloat("gSettings.Audio.SoundEffectsVolume", 1.0f) *
          SO2H_UI_SFX_BUS;
    // Dev hook. Muted here rather than at the trigger so the voices still allocate, run and
    // free exactly as they would audibly - the pool is the thing being debugged.
    if (CVarGetInteger("gSo2h.Ui.SfxMute", 0)) {
        vol = 0.0f;
    }
    if (vol <= 0.0f) {
        return;
    }

    for (i = 0; i < frames; i++) {
        f32 s = sAcc[i] * vol;
        s32 l = (s32)out[(i * 2) + 0] + (s32)s;
        s32 r = (s32)out[(i * 2) + 1] + (s32)s;

        out[(i * 2) + 0] = (s16)((l > 32767) ? 32767 : ((l < -32768) ? -32768 : l));
        out[(i * 2) + 1] = (s16)((r > 32767) ? 32767 : ((r < -32768) ? -32768 : r));
    }
}

// ---------------------------------------------------------------------------------------
// Binding (game thread)
// ---------------------------------------------------------------------------------------

#if SO2H_UI_SFX_BANK_ENABLED
// Live parameters for the bank route. AudioSfx holds these POINTERS for the voice's lifetime,
// so they must be static - never stack.
static f32 sBankFreq = SO2H_UI_SFX_TUNING;
static f32 sBankSlideVol = SO2H_UI_SFX_SLIDE_VOL;
static f32 sBankPlacedVol = SO2H_UI_SFX_PLACED_VOL;
static s8 sBankReverb = 0;
static Vec3f sBankPos = { 0.0f, 0.0f, 0.0f };
static u8 sBankBound = false;

/**
 * The soundfont the SYSTEM sfx bank is currently playing out of.
 *
 * Read from the live channel rather than hardcoded: the bank -> font mapping belongs to the sfx
 * sequence, and reading it back is the only answer that cannot go stale.
 */
static SoundFont* So2h_UiSfx_SystemFont(void) {
    SequencePlayer* seqPlayer = &gAudioCtx.seqPlayers[SEQ_PLAYER_SFX];
    SequenceChannel* channel;
    s32 fontId;

    if (!seqPlayer->enabled) {
        return NULL;
    }

    channel = seqPlayer->channels[BANK_SYSTEM];
    if ((channel == NULL) || (channel == &gAudioCtx.sequenceChannelNone)) {
        return NULL;
    }

    fontId = channel->fontId;
    if ((fontId == 0xFF) || (gFontMap == NULL) || (gFontMap[fontId] == NULL)) {
        return NULL;
    }

    return ResourceMgr_LoadAudioSoundFontByName(gFontMap[fontId]);
}

/** Park the two samples in two unused system-bank dummy slots. No vanilla sound is lost. */
static void So2h_UiSfx_BindBank(Sample* slide, Sample* placed) {
    SoundFont* font;

    if (sBankBound) {
        return;
    }

    font = So2h_UiSfx_SystemFont();
    if ((font == NULL) || (font->soundEffects == NULL)) {
        return; // audio not up yet - try again next frame
    }
    if (font->numSfx <= SO2H_UI_SFX_PLACED_IDX) {
        sBankBound = true; // nothing we can do about it; stop re-checking forever
        return;
    }

    font->soundEffects[SO2H_UI_SFX_SLIDE_IDX].tunedSample.sample = slide;
    font->soundEffects[SO2H_UI_SFX_SLIDE_IDX].tunedSample.tuning = SO2H_UI_SFX_TUNING;
    font->soundEffects[SO2H_UI_SFX_PLACED_IDX].tunedSample.sample = placed;
    font->soundEffects[SO2H_UI_SFX_PLACED_IDX].tunedSample.tuning = SO2H_UI_SFX_TUNING;
    sBankBound = true;
}

void So2h_UiSfx_PlayBank(s32 placed) {
    if (!sBankBound) {
        return;
    }
    if (placed) {
        AudioSfx_PlaySfx(SO2H_UI_SFX_PLACED_ID, &sBankPos, 0, &sBankFreq, &sBankPlacedVol, &sBankReverb);
    } else {
        AudioSfx_PlaySfx(SO2H_UI_SFX_SLIDE_ID, &sBankPos, 0, &sBankFreq, &sBankSlideVol, &sBankReverb);
    }
}
#else
void So2h_UiSfx_PlayBank(s32 placed) {
    (void)placed;
}
#endif

void So2h_UiSfx_Bind(void) {
    Sample* slide;
    Sample* placed;

    if (sBound) {
        return;
    }

    slide = ResourceMgr_LoadSampleByName(dgSo2hSfxSlide);
    placed = ResourceMgr_LoadSampleByName(dgSo2hSfxPlaced);
    if ((slide == NULL) || (placed == NULL)) {
        return; // resource not loaded yet
    }
    // See the CAVEAT at the top: sampleAddr is allocated by the ogg worker, so NULL means the
    // decode has not started. There is no completion flag to wait on.
    if ((slide->sampleAddr == NULL) || (placed->sampleAddr == NULL)) {
        return;
    }

    sClip[SO2H_UI_SFX_CLIP_SLIDE].pcm = (const s16*)slide->sampleAddr;
    sClip[SO2H_UI_SFX_CLIP_SLIDE].frames = (s32)(slide->size / sizeof(s16));
    sClip[SO2H_UI_SFX_CLIP_PLACED].pcm = (const s16*)placed->sampleAddr;
    sClip[SO2H_UI_SFX_CLIP_PLACED].frames = (s32)(placed->size / sizeof(s16));

#if SO2H_UI_SFX_BANK_ENABLED
    So2h_UiSfx_BindBank(slide, placed);
#endif

    // Published last: the audio thread reads nothing until this is set.
    sBound = true;
}

// ---------------------------------------------------------------------------------------
// Triggers (game thread)
// ---------------------------------------------------------------------------------------

/** Deterministic pitch for one node this open: 1.0 +/- spread, flat across the range. */
static f32 So2h_UiSfx_Pitch(s32 nodeId, u32 seed, f32 spread) {
    u32 h = So2h_UiHash((u32)nodeId ^ (seed * 0x9E3779B9u));
    f32 u = (f32)(h & 0xFFFF) / 65535.0f; // 0..1

    return 1.0f + (((u * 2.0f) - 1.0f) * spread);
}

void So2h_UiSfx_Slide(s32 nodeId, u32 seed) {
    if (!sBound || (nodeId < 0) || (nodeId >= SO2H_UI_MAX_NODES)) {
        return;
    }
    So2h_UiSfx_Push(SO2H_UI_SFX_CMD_START, SO2H_UI_SFX_CLIP_SLIDE, (s16)nodeId,
                    So2h_UiSfx_Pitch(nodeId, seed, SO2H_UI_SFX_SLIDE_PITCH));
}

void So2h_UiSfx_SlideStop(s32 nodeId) {
    if (!sBound || (nodeId < 0) || (nodeId >= SO2H_UI_MAX_NODES)) {
        return;
    }
    // Release, not a hard cut - stopping a loop dead is the one thing that always clicks.
    So2h_UiSfx_Push(SO2H_UI_SFX_CMD_STOP, SO2H_UI_SFX_CLIP_SLIDE, (s16)nodeId, 1.0f);
}

void So2h_UiSfx_Placed(s32 nodeId, u32 seed) {
    if (!sBound || (nodeId < 0) || (nodeId >= SO2H_UI_MAX_NODES)) {
        return;
    }
    So2h_UiSfx_Push(SO2H_UI_SFX_CMD_START, SO2H_UI_SFX_CLIP_PLACED, (s16)nodeId,
                    So2h_UiSfx_Pitch(nodeId, seed ^ 0xA5A5A5A5u, SO2H_UI_SFX_PLACED_PITCH));
}

#else

void So2h_UiSfx_Bind(void) {
}
void So2h_UiSfx_Slide(s32 nodeId, u32 seed) {
    (void)nodeId;
    (void)seed;
}
void So2h_UiSfx_SlideStop(s32 nodeId) {
    (void)nodeId;
}
void So2h_UiSfx_Placed(s32 nodeId, u32 seed) {
    (void)nodeId;
    (void)seed;
}
void So2h_UiSfx_MixInto(s16* out, s32 frames) {
    (void)out;
    (void)frames;
}
void So2h_UiSfx_PlayBank(s32 placed) {
    (void)placed;
}

#endif
