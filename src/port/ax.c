/* AX/AI/ARAM audio replacement on SDL3 audio.
 *
 * The game's sound engine (Voice_.c) programs DSP voice parameter blocks
 * (AXVPB) directly by byte offset, so voices here are raw 0x200-byte blocks in
 * GameCube layout which this software mixer interprets every 5 ms AX frame.
 *
 * Threads: the game's AX frame callback is game code, so it must run under the
 * giant lock. A dedicated AX thread runs callback + mixer and queues each
 * frame on an SDL audio stream, keeping at most RING_FRAMES queued; the
 * stream's device callback only wakes the AX thread and never touches game
 * state. Without an output device the thread paces itself on the clock, so
 * the game's sound timing still runs. */
#include "port_internal.h"

void Port_GiantLock(void);
void Port_GiantUnlock(void);

#define AX_RATE 48000
#define AX_FRAME_SAMPLES (AX_RATE / 200) /* 5 ms */
#define AX_MAX_VOICES 64
#define AX_VPB_SIZE 0x200
#define ARAM_SIZE (16u << 20)
#define ARAM_BASE 0x4000u /* the SDK reserves the first 16 KB for the DSP */
#define RING_FRAMES (AX_FRAME_SAMPLES * 8)

/* Byte offsets inside an AXVPB (GameCube layout). */
#define VPB_PRIORITY 0x0C
#define VPB_USERCTX 0x14
#define VPB_INDEX 0x18
#define VPB_SYNC 0x1C
#define VPB_MIXERCTRL 0x144
#define VPB_STATE 0x146
#define VPB_VL 0x14A
#define VPB_VDELTAL 0x14C
#define VPB_VR 0x14E
#define VPB_VDELTAR 0x150
#define VPB_VE 0x19C
#define VPB_VEDELTA 0x19E
#define VPB_LOOPFLAG 0x1A6
#define VPB_FORMAT 0x1A8
#define VPB_LOOPADDR 0x1AA
#define VPB_ENDADDR 0x1AE
#define VPB_CURADDR 0x1B2
#define VPB_RATIO 0x1DE
#define VPB_FRAC 0x1E2

#define FMT_ADPCM 0x00
#define FMT_PCM16 0x0A
#define FMT_PCM8 0x19

static u8 *s_aram;
static u8 s_vpbs[AX_MAX_VOICES][AX_VPB_SIZE] __attribute__((aligned(32)));
static void (*s_voiceCallbacks[AX_MAX_VOICES])(void *);
static int s_voiceInUse[AX_MAX_VOICES];
static AXCallback s_frameCallback;
static AXAuxCallback s_auxA, s_auxB;
static void *s_auxACtx, *s_auxBCtx;

static SDL_AudioStream *s_stream;  /* NULL: no output device */
static SDL_Thread *s_axThread;
static SDL_Semaphore *s_ringSpace;
static volatile int s_running;

#define RD16(p, o) (*(u16 *)((u8 *)(p) + (o)))
#define RDS16(p, o) (*(s16 *)((u8 *)(p) + (o)))
#define RD32(p, o) (*(u32 *)((u8 *)(p) + (o)))

/* ---- ARAM -------------------------------------------------------------- */

u8 *Port_ARAMBase(void) { return s_aram; }

u32 ARInit(u32 *stackIndexAddr, u32 numEntries) {
    (void)stackIndexAddr;
    (void)numEntries;
    if (s_aram == NULL) {
        s_aram = SDL_calloc(1, ARAM_SIZE);
    }
    return ARAM_BASE;
}

void ARReset(void) {}
u32 ARGetBaseAddress(void) { return ARAM_BASE; }
u32 ARGetSize(void) { return ARAM_SIZE; }
void ARQInit(void) {}
void ARQReset(void) {}
void ARQFlushQueue(void) {}

/* type 0: main memory -> ARAM, 1: ARAM -> main memory. Main memory addresses
 * are 32-bit handles (see port.h). Completes immediately. */
void ARQPostRequest(void *task, u32 owner, u32 type, u32 priority, u32 source, u32 dest,
                    u32 length, void (*callback)(u32)) {
    (void)owner;
    (void)priority;
    if (type == 0) {
        if (dest + length > ARAM_SIZE) {
            Port_Fatal("ARQ: ARAM write out of range (0x%X+0x%X)", dest, length);
        }
        memcpy(s_aram + dest, PTR(source), length);
    } else {
        if (source + length > ARAM_SIZE) {
            Port_Fatal("ARQ: ARAM read out of range (0x%X+0x%X)", source, length);
        }
        memcpy(PTR(dest), s_aram + source, length);
    }
    if (callback != NULL) {
        callback(HND(task));
    }
}

/* ---- AI ---------------------------------------------------------------- */

void AIInit(u8 *stack) { (void)stack; }
void AIReset(void) {}
void AIInitDMA(u32 startAddr, u32 length) { (void)startAddr; (void)length; }
void AIStartDMA(void) {}
void *AIRegisterDMACallback(void (*callback)(void)) { (void)callback; return NULL; }
void AISetDSPSampleRate(u32 rate) {
    if (rate != 1) {
        PORT_TODO("32 kHz DSP rate");
    }
}

/* ---- voices ------------------------------------------------------------ */

struct AXVPB *AXAcquireVoice(u32 priority, void (*callback)(void *), u32 userContext) {
    int i;
    int victim = -1;
    u32 lowest = priority;

    for (i = 0; i < AX_MAX_VOICES; i++) {
        if (!s_voiceInUse[i]) {
            victim = i;
            break;
        }
    }
    if (victim < 0) {
        /* Steal the lowest-priority voice below ours. */
        for (i = 0; i < AX_MAX_VOICES; i++) {
            u32 p = RD32(s_vpbs[i], VPB_PRIORITY);
            if (p < lowest) {
                lowest = p;
                victim = i;
            }
        }
        if (victim < 0) {
            return NULL;
        }
        if (s_voiceCallbacks[victim] != NULL) {
            s_voiceCallbacks[victim](s_vpbs[victim]);
        }
    }
    memset(s_vpbs[victim], 0, AX_VPB_SIZE);
    RD32(s_vpbs[victim], VPB_PRIORITY) = priority;
    RD32(s_vpbs[victim], VPB_USERCTX) = userContext;
    RD32(s_vpbs[victim], VPB_INDEX) = (u32)victim;
    s_voiceCallbacks[victim] = callback;
    s_voiceInUse[victim] = 1;
    return (struct AXVPB *)s_vpbs[victim];
}

static int VoiceIndex(struct AXVPB *p) {
    ptrdiff_t i = ((u8 *)p - &s_vpbs[0][0]) / AX_VPB_SIZE;
    if (i < 0 || i >= AX_MAX_VOICES) {
        Port_Fatal("not an AX voice: %p", (void *)p);
    }
    return (int)i;
}

void AXFreeVoice(struct AXVPB *p) {
    int i = VoiceIndex(p);
    RD16(p, VPB_STATE) = 0;
    RD32(p, VPB_PRIORITY) = 0;
    s_voiceCallbacks[i] = NULL;
    s_voiceInUse[i] = 0;
}

void AXSetVoicePriority(struct AXVPB *p, u32 priority) {
    RD32(p, VPB_PRIORITY) = priority;
}

AXCallback AXRegisterCallback(AXCallback callback) {
    AXCallback old = s_frameCallback;
    s_frameCallback = callback;
    return old;
}

void AXRegisterAuxACallback(AXAuxCallback callback, void *context) {
    s_auxA = callback;
    s_auxACtx = context;
}

void AXRegisterAuxBCallback(AXAuxCallback callback, void *context) {
    s_auxB = callback;
    s_auxBCtx = context;
}

/* Reverb (AXFX delay) on the aux buses isn't mixed yet. */
BOOL AXFXDelayInit(struct AXFX_DELAY *delay) { (void)delay; return TRUE; }
BOOL AXFXDelaySettings(struct AXFX_DELAY *delay) { (void)delay; return TRUE; }
BOOL AXFXDelayShutdown(struct AXFX_DELAY *delay) { (void)delay; return TRUE; }
void AXFXDelayCallback(void *data, void *context) { (void)data; (void)context; }

/* ---- mixer ------------------------------------------------------------- */

static float FetchSample(u16 format, u32 addr) {
    if (format == FMT_PCM16) {
        u32 b = addr * 2;
        return b + 1 < ARAM_SIZE ? (s16)Port_ReadBE16(s_aram + b) * (1.0f / 32768.0f) : 0.0f;
    }
    if (format == FMT_PCM8) {
        return addr < ARAM_SIZE ? (s8)s_aram[addr] * (1.0f / 128.0f) : 0.0f;
    }
    return 0.0f;
}

static void MixVoice(u8 *vpb, float *out) {
    u16 format = RD16(vpb, VPB_FORMAT);
    u32 cur = RD32(vpb, VPB_CURADDR);
    u32 end = RD32(vpb, VPB_ENDADDR);
    u32 loop = RD32(vpb, VPB_LOOPADDR);
    u16 loopFlag = RD16(vpb, VPB_LOOPFLAG);
    u32 ratio = RD32(vpb, VPB_RATIO); /* 16.16 */
    u32 frac = RD16(vpb, VPB_FRAC);
    s32 vl = RDS16(vpb, VPB_VL);
    s32 vr = RDS16(vpb, VPB_VR);
    s32 dl = RDS16(vpb, VPB_VDELTAL);
    s32 dr = RDS16(vpb, VPB_VDELTAR);
    s32 ve = RD16(vpb, VPB_VE);
    s32 ved = RDS16(vpb, VPB_VEDELTA);
    int i;

    if (format == FMT_ADPCM) {
        PORT_TODO("ADPCM voices");
        RD16(vpb, VPB_STATE) = 0;
        return;
    }
    for (i = 0; i < AX_FRAME_SAMPLES; i++) {
        float s0 = FetchSample(format, cur);
        float s1 = FetchSample(format, cur + 1 <= end ? cur + 1 : (loopFlag ? loop : cur));
        float s = s0 + (s1 - s0) * (frac * (1.0f / 65536.0f));
        float env = ve * (1.0f / 32768.0f);

        out[i * 2 + 0] += s * env * (vl * (1.0f / 32768.0f));
        out[i * 2 + 1] += s * env * (vr * (1.0f / 32768.0f));

        vl = SDL_clamp(vl + dl, 0, 0x7FFF);
        vr = SDL_clamp(vr + dr, 0, 0x7FFF);
        ve = SDL_clamp(ve + ved, 0, 0x7FFF);
        frac += ratio;
        cur += frac >> 16;
        frac &= 0xFFFF;
        if (cur > end) {
            if (loopFlag) {
                cur = loop + (cur - end - 1);
            } else {
                RD16(vpb, VPB_STATE) = 0;
                break;
            }
        }
    }
    RD32(vpb, VPB_CURADDR) = cur;
    RD16(vpb, VPB_FRAC) = (u16)frac;
    RD16(vpb, VPB_VL) = (u16)vl;
    RD16(vpb, VPB_VR) = (u16)vr;
    RD16(vpb, VPB_VE) = (u16)ve;
    RD32(vpb, VPB_SYNC) = 0;
}

static void RunAXFrame(float *out) {
    int i;

    memset(out, 0, sizeof(float) * AX_FRAME_SAMPLES * 2);
    if (s_frameCallback != NULL) {
        s_frameCallback();
    }
    for (i = 0; i < AX_MAX_VOICES; i++) {
        if (s_voiceInUse[i] && RD16(s_vpbs[i], VPB_STATE) == 1) {
            MixVoice(s_vpbs[i], out);
        }
    }
    for (i = 0; i < AX_FRAME_SAMPLES * 2; i++) {
        out[i] = SDL_clamp(out[i], -1.0f, 1.0f);
    }
}

#define FRAME_BYTES ((int)sizeof(float) * AX_FRAME_SAMPLES * 2)

static int SDLCALL AXThread(void *unused) {
    float frame[AX_FRAME_SAMPLES * 2];
    Uint64 next = SDL_GetTicksNS();
    (void)unused;

    while (s_running) {
        if (s_stream != NULL) {
            /* keep at most RING_FRAMES queued: wait for the device to drain */
            if (SDL_GetAudioStreamQueued(s_stream) > (RING_FRAMES - AX_FRAME_SAMPLES) * (int)sizeof(float) * 2) {
                SDL_WaitSemaphoreTimeout(s_ringSpace, 5);
                continue;
            }
        } else {
            /* silent: one AX frame per 5 ms of real time */
            Uint64 now = SDL_GetTicksNS();
            if (now < next) {
                SDL_DelayNS(next - now);
            }
            next += SDL_NS_PER_SECOND / 200;
        }
        Port_GiantLock();
        RunAXFrame(frame);
        Port_GiantUnlock();
        if (s_stream != NULL) {
            SDL_PutAudioStreamData(s_stream, frame, FRAME_BYTES);
        }
    }
    return 0;
}

/* Runs on SDL's audio thread whenever the device wants more: just wake the
 * AX thread (an underrun plays silence). */
static void SDLCALL DeviceCallback(void *userdata, SDL_AudioStream *stream, int additional, int total) {
    (void)userdata;
    (void)stream;
    (void)additional;
    (void)total;
    SDL_SignalSemaphore(s_ringSpace);
}

void AXInit(void) {
    SDL_AudioSpec spec;

    if (s_running) {
        return;
    }
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = AX_RATE;
    s_ringSpace = SDL_CreateSemaphore(0);
    s_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, DeviceCallback, NULL);
    if (s_stream == NULL) {
        Port_Log("audio: no output device (%s); continuing silently", SDL_GetError());
    } else {
        SDL_ResumeAudioStreamDevice(s_stream);
    }
    s_running = 1;
    s_axThread = SDL_CreateThread(AXThread, "ax", NULL);
}

void AXQuit(void) {
    Audio_PortShutdown();
}

void Audio_PortShutdown(void) {
    if (!s_running) {
        return;
    }
    s_running = 0;
    SDL_WaitThread(s_axThread, NULL);
    if (s_stream != NULL) {
        SDL_DestroyAudioStream(s_stream);  /* also closes the device */
        s_stream = NULL;
    }
    SDL_DestroySemaphore(s_ringSpace);
    s_ringSpace = NULL;
}
