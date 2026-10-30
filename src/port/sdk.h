/* The subset of the Dolphin (GameCube) SDK that the game calls, implemented by
 * src/port on top of SDL3, OpenGL and miniaudio.
 *
 * Signatures follow the real SDK; enum parameters are plain integers. Structs
 * the game defines itself (AXVPB, PADStatus, CARDStat, ...) are only referred
 * to by tag here so the game's own layouts stay authoritative.
 */
#ifndef SDK_H
#define SDK_H

#include <math.h>
#include <string.h>

#include "port.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- shared SDK types ------------------------------------------------- */

typedef f32 Mtx[3][4];
typedef f32 Mtx44[4][4];
typedef f32 (*MtxPtr)[4];

typedef s64 OSTime;
typedef u32 OSTick;

struct AXVPB;
struct PADStatus;
struct CARDStat;
struct CARDFileInfo;
struct OSCalendarTime;
struct GXRenderModeObj;
struct GXTexObj;
struct OSFontHeader;

/* ---- OS ---------------------------------------------------------------- */

#define OS_BUS_CLOCK 162000000u
#define OS_TIMER_CLOCK (OS_BUS_CLOCK / 4)
#define OSTicksToMilliseconds(t) ((t) / (OS_TIMER_CLOCK / 1000))
#define OSMillisecondsToTicks(ms) ((ms) * (OS_TIMER_CLOCK / 1000))
#define OSSecondsToTicks(s) ((s) * OS_TIMER_CLOCK)

void OSInit(void);
void OSReport(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void OSPanic(const char *file, int line, const char *fmt, ...) __attribute__((noreturn));

void *OSGetArenaLo(void);
void *OSGetArenaHi(void);
void OSSetArenaLo(void *addr);
void OSSetArenaHi(void *addr);
void *OSInitAlloc(void *arenaStart, void *arenaEnd, int maxHeaps);
int OSCreateHeap(void *start, void *end);
int OSSetCurrentHeap(int heap);
void *OSAllocFromHeap(int heap, u32 size);
void OSFreeToHeap(int heap, void *ptr);
extern int __OSCurrHeap;

BOOL OSDisableInterrupts(void);
BOOL OSEnableInterrupts(void);
BOOL OSRestoreInterrupts(BOOL level);

OSTime OSGetTime(void);
OSTick OSGetTick(void);
void OSTicksToCalendarTime(OSTime ticks, struct OSCalendarTime *td);

/* Threads are opaque buffers the game allocates (0x310 bytes on GameCube). */
typedef void *(*OSThreadFunc)(void *);
BOOL OSCreateThread(void *thread, OSThreadFunc func, void *param, void *stack, u32 stackSize,
                    s32 priority, u16 attr);
s32 OSResumeThread(void *thread);
s32 OSSuspendThread(void *thread);
void OSCancelThread(void *thread);
void OSExitThread(void *val);
void *OSGetCurrentThread(void);

typedef void *OSMessage;
typedef struct OSMessageQueue {
    u8 opaque[0x20];
} OSMessageQueue;
void OSInitMessageQueue(OSMessageQueue *mq, OSMessage *msgArray, s32 msgCount);
BOOL OSSendMessage(OSMessageQueue *mq, OSMessage msg, s32 flags);
BOOL OSReceiveMessage(OSMessageQueue *mq, OSMessage *msg, s32 flags);

void OSInitAlarm(void);
u32 OSGetResetCode(void);
void OSResetSystem(int reset, u32 resetCode, BOOL forceMenu);
BOOL OSGetResetButtonState(void);
u32 OSGetSoundMode(void);
void OSSetSoundMode(u32 mode);
BOOL OSInitFont(struct OSFontHeader *fontData);
char *OSGetFontTexture(char *string, void **image, s32 *x, s32 *y, s32 *width);

void DCFlushRange(void *addr, u32 nBytes);
void DCInvalidateRange(void *addr, u32 nBytes);
void DCZeroRange(void *addr, u32 nBytes);
void DCStoreRange(void *addr, u32 nBytes);
void LCEnable(void);
void LCDisable(void);
void LCQueueWait(u32 len);

/* ---- DVD (reads from the data directory) ------------------------------ */

typedef struct DVDCommandBlock {
    u8 opaque[0x30];
} DVDCommandBlock;

/* GameCube layout and size (0x3C): the game allocates it itself and reads
 * `length` at 0x34. The port keeps its SDL stream inside the command block. */
typedef struct DVDFileInfo {
    union {
        DVDCommandBlock cb;
        void *host;   /* port: SDL_IOStream */
    };
    u32 startAddr;    /* 0x30 */
    u32 length;       /* 0x34 */
    u32 callback;     /* 0x38 (unused by the port) */
} __attribute__((packed, aligned(4))) DVDFileInfo;
_Static_assert(sizeof(DVDFileInfo) == 0x3C, "DVDFileInfo must keep the GameCube size");

void DVDInit(void);
BOOL DVDOpen(const char *fileName, DVDFileInfo *fileInfo);
BOOL DVDClose(DVDFileInfo *fileInfo);
s32 DVDReadPrio(DVDFileInfo *fileInfo, void *addr, s32 length, s32 offset, s32 prio);
BOOL DVDReadAsyncPrio(DVDFileInfo *fileInfo, void *addr, s32 length, s32 offset,
                      void (*callback)(s32, DVDFileInfo *), s32 prio);
s32 DVDGetCommandBlockStatus(const DVDCommandBlock *block);
s32 DVDGetDriveStatus(void);
BOOL DVDCancel(DVDCommandBlock *block);
BOOL DVDInquiryAsync(DVDCommandBlock *block, void *info, void (*callback)(s32, DVDCommandBlock *));
#define DVDGetLength(fi) ((fi)->length)

/* ---- VI ---------------------------------------------------------------- */

void VIInit(void);
void VIConfigure(const struct GXRenderModeObj *rm);
void VIFlush(void);
void VISetNextFrameBuffer(void *fb);
void VISetBlack(BOOL black);
void VIWaitForRetrace(void);
u32 VIGetNextField(void);
u32 VIGetTvFormat(void);
void *VISetPostRetraceCallback(void (*callback)(u32));

/* ---- PAD / SI ---------------------------------------------------------- */

#define PAD_MAX_CONTROLLERS 4
#define PAD_ERR_NONE 0
#define PAD_ERR_NO_CONTROLLER -1
#define PAD_ERR_NOT_READY -2
#define PAD_ERR_TRANSFER -3

#define PAD_BUTTON_LEFT 0x0001
#define PAD_BUTTON_RIGHT 0x0002
#define PAD_BUTTON_DOWN 0x0004
#define PAD_BUTTON_UP 0x0008
#define PAD_TRIGGER_Z 0x0010
#define PAD_TRIGGER_R 0x0020
#define PAD_TRIGGER_L 0x0040
#define PAD_BUTTON_A 0x0100
#define PAD_BUTTON_B 0x0200
#define PAD_BUTTON_X 0x0400
#define PAD_BUTTON_Y 0x0800
#define PAD_BUTTON_START 0x1000

BOOL PADInit(void);
BOOL PADReset(u32 mask);
u32 SIProbe(s32 chan);
u32 PADRead(struct PADStatus *status);
void PADClamp(struct PADStatus *status);
void PADControlMotor(s32 chan, u32 command);
void *PADSetSamplingCallback(void (*callback)(void));
void SIInit(void);
void SISetSamplingRate(u32 msec);

/* Logitech steering wheel (SI steering library): reported as absent. */
s32 SIResetSteeringAsync(s32 chan, void *callback);
s32 SIReadSteering(s32 chan, void *status);
s32 SIControlSteering(s32 chan, u32 control, s32 level);
void *SISetSteeringSamplingCallback(void *callback);

/* ---- CARD (memory card -> save directory) ----------------------------- */

#define CARD_RESULT_READY 0
#define CARD_RESULT_BUSY -1
#define CARD_RESULT_WRONGDEVICE -2
#define CARD_RESULT_NOCARD -3
#define CARD_RESULT_NOFILE -4
#define CARD_RESULT_IOERROR -5
#define CARD_RESULT_BROKEN -6
#define CARD_RESULT_EXIST -7
#define CARD_RESULT_NOENT -8
#define CARD_RESULT_INSSPACE -9
#define CARD_RESULT_NOPERM -10
#define CARD_RESULT_LIMIT -11
#define CARD_RESULT_NAMETOOLONG -12
#define CARD_RESULT_ENCODING -13
#define CARD_RESULT_CANCELED -14
#define CARD_RESULT_FATAL_ERROR -128

void CARDInit(void);
s32 CARDProbeEx(s32 chan, s32 *memSize, s32 *sectorSize);
s32 CARDMount(s32 chan, void *workArea, void *detachCallback);
s32 CARDUnmount(s32 chan);
s32 CARDCheck(s32 chan);
s32 CARDFormat(s32 chan);
s32 CARDFreeBlocks(s32 chan, s32 *byteNotUsed, s32 *filesNotUsed);
s32 CARDGetEncoding(s32 chan, u16 *encode);
s32 CARDGetSectorSize(s32 chan, u32 *size);
s32 CARDCreate(s32 chan, const char *fileName, u32 size, struct CARDFileInfo *fileInfo);
s32 CARDOpen(s32 chan, const char *fileName, struct CARDFileInfo *fileInfo);
s32 CARDClose(struct CARDFileInfo *fileInfo);
s32 CARDDelete(s32 chan, const char *fileName);
s32 CARDRead(struct CARDFileInfo *fileInfo, void *buf, s32 length, s32 offset);
s32 CARDWrite(struct CARDFileInfo *fileInfo, const void *buf, s32 length, s32 offset);
s32 CARDGetStatus(s32 chan, s32 fileNo, struct CARDStat *stat);
s32 CARDSetStatus(s32 chan, s32 fileNo, struct CARDStat *stat);

/* ---- AI / AR / AX (audio -> miniaudio) -------------------------------- */

void AIInit(u8 *stack);
void AIReset(void);
void AIInitDMA(u32 startAddr, u32 length);
void AIStartDMA(void);
void *AIRegisterDMACallback(void (*callback)(void));
void AISetDSPSampleRate(u32 rate);

u32 ARInit(u32 *stackIndexAddr, u32 numEntries);
void ARReset(void);
u32 ARGetBaseAddress(void);
u32 ARGetSize(void);
void ARQInit(void);
void ARQReset(void);
void ARQPostRequest(void *task, u32 owner, u32 type, u32 priority, u32 source, u32 dest,
                    u32 length, void (*callback)(u32));
void ARQFlushQueue(void);
/* ARAM is emulated in host memory; port helper to reach it. */
u8 *Port_ARAMBase(void);

typedef void (*AXCallback)(void);
typedef void (*AXAuxCallback)(void *data, void *context);
void AXInit(void);
void AXQuit(void);
struct AXVPB *AXAcquireVoice(u32 priority, void (*callback)(void *), u32 userContext);
void AXFreeVoice(struct AXVPB *p);
void AXSetVoicePriority(struct AXVPB *p, u32 priority);
AXCallback AXRegisterCallback(AXCallback callback);
void AXRegisterAuxACallback(AXAuxCallback callback, void *context);
void AXRegisterAuxBCallback(AXAuxCallback callback, void *context);

struct AXFX_DELAY;
BOOL AXFXDelayInit(struct AXFX_DELAY *delay);
BOOL AXFXDelaySettings(struct AXFX_DELAY *delay);
BOOL AXFXDelayShutdown(struct AXFX_DELAY *delay);
void AXFXDelayCallback(void *data, void *context);

/* ---- MTX / VEC (portable C versions of the PS* routines) --------------- */

void PSMTXIdentity(Mtx m);
void PSMTXConcat(const Mtx a, const Mtx b, Mtx ab);
void PSMTXScale(Mtx m, f32 x, f32 y, f32 z);
void C_MTXOrtho(Mtx44 m, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f);
void C_MTXPerspective(Mtx44 m, f32 fovY, f32 aspect, f32 n, f32 f);

void PSVECAdd(const void *a, const void *b, void *ab);
void PSVECSubtract(const void *a, const void *b, void *a_b);
void PSVECScale(const void *src, void *dst, f32 scale);
void PSVECNormalize(const void *src, void *unit);
f32 PSVECMag(const void *v);
f32 PSVECSquareMag(const void *v);
f32 PSVECDotProduct(const void *a, const void *b);
void PSVECCrossProduct(const void *a, const void *b, void *axb);
f32 PSVECSquareDistance(const void *a, const void *b);

/* ---- GX (emulated on OpenGL 3.3) -------------------------------------- */

#include "gx.h"
#include "lgwheels.h"

/* ---- misc -------------------------------------------------------------- */

void EXIInit(void);
void GBAConnectivityInit(void);
void GBAConnectivityLoop(void);

/* ---- C library behaviour the game relies on ------------------------------- */

/* CodeWarrior's MSL rand(): the ANSI sample generator, 0..32767. The game
 * divides by 32768.0f to get [0, 1), which glibc's RAND_MAX would break. */
int Port_Rand(void);
void Port_SRand(unsigned int seed);
#define rand() Port_Rand()
#define srand(s) Port_SRand(s)

/* ---- CodeWarrior PowerPC intrinsics -------------------------------------- */

static inline f64 __fabs(f64 x) { return fabs(x); }
static inline f32 __fabsf(f32 x) { return fabsf(x); }
static inline int __abs(int x) { return x < 0 ? -x : x; }
/* frsqrte is a ~5-bit estimate the game refines with Newton steps; exact is fine. */
static inline f64 __frsqrte(f64 x) { return 1.0 / sqrt(x); }
static inline u32 __mulhwu(u32 a, u32 b) { return (u32)(((u64)a * b) >> 32); }
/* store halfword byte-reversed: a later (native) load of it reads bswap(v),
 * as it would on the GameCube */
static inline void __sthbrx(u16 v, void *base, int off) {
    u16 r = bswap16(v);
    memcpy((u8 *)base + off, &r, 2);
}

#ifdef __cplusplus
}
#endif

#endif /* SDK_H */
