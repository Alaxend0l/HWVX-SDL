/* PAD: GameCube controllers from SDL gamepads (plus keyboard on port 1). */
#include "port_internal.h"

typedef struct PADStatus {
    u16 button;
    s8 stickX;
    s8 stickY;
    s8 substickX;
    s8 substickY;
    u8 triggerLeft;
    u8 triggerRight;
    u8 analogA;
    u8 analogB;
    s8 err;
} PADStatus;

static SDL_Gamepad *s_pads[PAD_MAX_CONTROLLERS];
static void (*s_samplingCallback)(void);

static void OpenPad(SDL_JoystickID id) {
    int i;
    for (i = 0; i < PAD_MAX_CONTROLLERS; i++) {
        if (s_pads[i] == NULL) {
            s_pads[i] = SDL_OpenGamepad(id);
            if (s_pads[i] != NULL) {
                Port_Log("Controller %d: %s", i + 1, SDL_GetGamepadName(s_pads[i]));
            }
            return;
        }
    }
}

void Pad_PortHandleEvent(const SDL_Event *ev) {
    int i;
    if (ev->type == SDL_EVENT_GAMEPAD_ADDED) {
        for (i = 0; i < PAD_MAX_CONTROLLERS; i++) {
            if (s_pads[i] != NULL && SDL_GetGamepadID(s_pads[i]) == ev->gdevice.which) {
                return;
            }
        }
        OpenPad(ev->gdevice.which);
    } else if (ev->type == SDL_EVENT_GAMEPAD_REMOVED) {
        for (i = 0; i < PAD_MAX_CONTROLLERS; i++) {
            if (s_pads[i] != NULL && SDL_GetGamepadID(s_pads[i]) == ev->gdevice.which) {
                SDL_CloseGamepad(s_pads[i]);
                s_pads[i] = NULL;
            }
        }
    }
}

BOOL PADInit(void) {
    int count = 0;
    int i;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    for (i = 0; i < count; i++) {
        OpenPad(ids[i]);
    }
    SDL_free(ids);
    return TRUE;
}

static s8 Stick(Sint16 v, int invert) {
    int s = v / 256; /* -128..127 */
    if (invert) {
        s = -s - 1;
    }
    /* GameCube sticks reach roughly +-100 at the gate. */
    s = s * 100 / 128;
    return (s8)SDL_clamp(s, -128, 127);
}

static u8 Trigger(Sint16 v) {
    return (u8)SDL_clamp(v / 128, 0, 255);
}

static void ReadGamepad(SDL_Gamepad *gp, PADStatus *st) {
    u16 b = 0;

    if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_SOUTH)) b |= PAD_BUTTON_A;
    if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_WEST)) b |= PAD_BUTTON_B;
    if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_EAST)) b |= PAD_BUTTON_X;
    if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_NORTH)) b |= PAD_BUTTON_Y;
    if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_START)) b |= PAD_BUTTON_START;
    if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) b |= PAD_TRIGGER_Z;
    if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_DPAD_UP)) b |= PAD_BUTTON_UP;
    if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) b |= PAD_BUTTON_DOWN;
    if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) b |= PAD_BUTTON_LEFT;
    if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) b |= PAD_BUTTON_RIGHT;

    st->stickX = Stick(SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTX), 0);
    st->stickY = Stick(SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTY), 1);
    st->substickX = Stick(SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHTX), 0);
    st->substickY = Stick(SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHTY), 1);
    st->triggerLeft = Trigger(SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
    st->triggerRight = Trigger(SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
    /* Full press clicks the digital button, as on the real triggers. */
    if (st->triggerLeft > 200) b |= PAD_TRIGGER_L;
    if (st->triggerRight > 200) b |= PAD_TRIGGER_R;
    st->analogA = (b & PAD_BUTTON_A) ? 0xFF : 0;
    st->analogB = (b & PAD_BUTTON_B) ? 0xFF : 0;
    st->button = b;
    st->err = PAD_ERR_NONE;
}

/* Main stick directions pressed on the keyboard (or by HWVX_AUTOINPUT) for pad
 * 0: -1, 0, 1 per axis. PADClamp gives these the full gate value on each axis,
 * so two keys make a full diagonal instead of the octagon's corner (40, 40),
 * which the menus (|x| > 100 after Input_NGC's scaling) don't count. */
static int s_keyStickX, s_keyStickY;

/* Keyboard: arrows/WASD steer, Space/Z = A, X = B, C = X, V = Y, Enter = Start,
 * Q/E = L/R triggers, Shift = Z. */
static void ReadKeyboard(PADStatus *st) {
    const bool *k = SDL_GetKeyboardState(NULL);
    u16 b = 0;
    int x = 0, y = 0;

    if (k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_Z]) b |= PAD_BUTTON_A;
    if (k[SDL_SCANCODE_X]) b |= PAD_BUTTON_B;
    if (k[SDL_SCANCODE_C]) b |= PAD_BUTTON_X;
    if (k[SDL_SCANCODE_V]) b |= PAD_BUTTON_Y;
    if (k[SDL_SCANCODE_RETURN]) b |= PAD_BUTTON_START;
    if (k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT]) b |= PAD_TRIGGER_Z;
    if (k[SDL_SCANCODE_Q]) { b |= PAD_TRIGGER_L; st->triggerLeft = 0xFF; }
    if (k[SDL_SCANCODE_E]) { b |= PAD_TRIGGER_R; st->triggerRight = 0xFF; }
    if (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A]) x -= 100;
    if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) x += 100;
    if (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W]) y += 100;
    if (k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S]) y -= 100;
    if (k[SDL_SCANCODE_I]) b |= PAD_BUTTON_UP;
    if (k[SDL_SCANCODE_K]) b |= PAD_BUTTON_DOWN;
    if (k[SDL_SCANCODE_J]) b |= PAD_BUTTON_LEFT;
    if (k[SDL_SCANCODE_L]) b |= PAD_BUTTON_RIGHT;

    st->button |= b;
    if (x != 0) st->stickX = (s8)x;
    if (y != 0) st->stickY = (s8)y;
    if (b & PAD_BUTTON_A) st->analogA = 0xFF;
    if (b & PAD_BUTTON_B) st->analogB = 0xFF;
    st->err = PAD_ERR_NONE;
}

/* Debugging: HWVX_AUTOINPUT="entry,..." presses buttons on port 1, each held
 * for 10 frames (frames = retraces). Entries run in order:
 *   N:btn   press at frame N after boot;
 *   +N:btn  press once the game has run N frames in a row without reading
 *           the disc since the previous press was released, so loading
 *           times don't shift the schedule (use this for menu navigation).
 * Either form takes an optional hold length: btn*N holds for N frames.
 * Buttons: a b x y start z l r up down left right; main stick: sup sdown
 * sleft sright. Each press is logged with its frame. */
static u32 s_frame;

static int s_autoStickX, s_autoStickY;

static u16 AutoInput(void) {
    static const struct { const char *name; u16 bit; } names[] = {
        { "a", PAD_BUTTON_A }, { "b", PAD_BUTTON_B }, { "x", PAD_BUTTON_X }, { "y", PAD_BUTTON_Y },
        { "start", PAD_BUTTON_START }, { "z", PAD_TRIGGER_Z }, { "l", PAD_TRIGGER_L }, { "r", PAD_TRIGGER_R },
        { "up", PAD_BUTTON_UP }, { "down", PAD_BUTTON_DOWN }, { "left", PAD_BUTTON_LEFT }, { "right", PAD_BUTTON_RIGHT },
    };
    static const char *next;      /* next entry to run */
    static u32 pressEnd, lastReads, lastFrame, quiet;
    static char held[16];
    static int started;
    u16 bits = 0;
    int i;

    s_autoStickX = s_autoStickY = 0;
    if (!started) {
        started = 1;
        next = SDL_getenv("HWVX_AUTOINPUT");
    }
    if (s_frame != lastFrame) {   /* PADRead may run more than once a frame */
        lastFrame = s_frame;
        if (g_portDiscReads != lastReads) {
            lastReads = g_portDiscReads;
            quiet = 0;
        } else {
            quiet++;
        }
    }
    if (held[0] == 0 && next != NULL && *next) {
        char name[16];
        unsigned frame = 0;
        int n = 0, rel = *next == '+';
        if (SDL_sscanf(next + rel, "%u:%15[a-z]%n", &frame, name, &n) != 2) {
            Port_Log("HWVX_AUTOINPUT: can't parse \"%s\"", next);
            next = NULL;
        } else if (rel ? (s_frame >= pressEnd && quiet >= frame) : s_frame >= frame) {
            unsigned hold = 10;
            int m = 0;
            if (next[rel + n] == '*' && SDL_sscanf(next + rel + n + 1, "%u%n", &hold, &m) == 1) {
                n += 1 + m;   /* btn*N: hold for N frames */
            }
            SDL_strlcpy(held, name, sizeof(held));
            pressEnd = s_frame + hold;
            Port_Log("autoinput: %s at frame %u for %u frames", held, (unsigned)s_frame, hold);
            next += rel + n;
            if (*next == ',') {
                next++;
            }
        }
    }
    if (held[0] != 0) {
        for (i = 0; i < (int)SDL_arraysize(names); i++) {
            if (SDL_strcmp(held, names[i].name) == 0) {
                bits |= names[i].bit;
            }
        }
        /* main stick, as the keyboard sets it */
        if (SDL_strcmp(held, "sup") == 0) s_autoStickY = 100;
        if (SDL_strcmp(held, "sdown") == 0) s_autoStickY = -100;
        if (SDL_strcmp(held, "sleft") == 0) s_autoStickX = -100;
        if (SDL_strcmp(held, "sright") == 0) s_autoStickX = 100;
        if (s_frame + 1 >= pressEnd) {
            held[0] = 0;
            quiet = 0;   /* count quiet frames from the release */
        }
    }
    return bits;
}

u32 PADRead(struct PADStatus *status) {
    PADStatus *st = (PADStatus *)status;
    u32 motorMask = 0;
    int i;

    for (i = 0; i < PAD_MAX_CONTROLLERS; i++) {
        memset(&st[i], 0, sizeof(PADStatus));
        if (s_pads[i] != NULL) {
            ReadGamepad(s_pads[i], &st[i]);
            motorMask |= 0x80000000u >> i;
        } else {
            st[i].err = PAD_ERR_NO_CONTROLLER;
        }
    }
    ReadKeyboard(&st[0]);
    st[0].button |= AutoInput();
    if (s_autoStickX) st[0].stickX = (s8)s_autoStickX;
    if (s_autoStickY) st[0].stickY = (s8)s_autoStickY;
    {
        const bool *k = SDL_GetKeyboardState(NULL);
        int kx = (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) - (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A]);
        int ky = (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W]) - (k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S]);
        s_keyStickX = s_autoStickX ? (s_autoStickX > 0 ? 1 : -1) : kx;
        s_keyStickY = s_autoStickY ? (s_autoStickY > 0 ? 1 : -1) : ky;
    }
    if (st[0].button) {
        st[0].err = PAD_ERR_NONE;
    }
    return motorMask;
}

/* PADClamp as in the SDK: dead zones, then the sticks limited to an octagon
 * (main stick at most 72, C stick 59) and triggers to 0..150. The game relies
 * on these ranges (Input_NGC.c scales the stick by 1.68919 into a signed
 * byte, which only fits after clamping). */
static void ClampStick(s8 *px, s8 *py, int max, int xy, int min) {
    int x = *px, y = *py, sx = 1, sy = 1, d;

    if (x < 0) { sx = -1; x = -x; }
    if (y < 0) { sy = -1; y = -y; }
    x = x <= min ? 0 : x - min;
    y = y <= min ? 0 : y - min;
    if (x == 0 && y == 0) {
        *px = *py = 0;
        return;
    }
    if (xy * y <= xy * x) {
        d = xy * x + (max - xy) * y;
    } else {
        d = xy * y + (max - xy) * x;
    }
    if (xy * max < d) {
        x = xy * max * x / d;
        y = xy * max * y / d;
    }
    *px = (s8)(sx * x);
    *py = (s8)(sy * y);
}

static void ClampTrigger(u8 *t, int min, int max) {
    if (*t <= min) {
        *t = 0;
    } else {
        if (*t > max) {
            *t = (u8)max;
        }
        *t = (u8)(*t - min);
    }
}

void PADClamp(struct PADStatus *status) {
    PADStatus *st = (PADStatus *)status;
    int i;

    for (i = 0; i < PAD_MAX_CONTROLLERS; i++) {
        if (st[i].err != PAD_ERR_NONE) {
            continue;
        }
        ClampStick(&st[i].stickX, &st[i].stickY, 72, 40, 15);
        if (i == 0) {   /* keyboard directions: full on each axis (see s_keyStickX) */
            if (s_keyStickX) st[i].stickX = (s8)(72 * s_keyStickX);
            if (s_keyStickY) st[i].stickY = (s8)(72 * s_keyStickY);
        }
        ClampStick(&st[i].substickX, &st[i].substickY, 59, 31, 15);
        ClampTrigger(&st[i].triggerLeft, 30, 180);
        ClampTrigger(&st[i].triggerRight, 30, 180);
    }
}

void PADControlMotor(s32 chan, u32 command) {
    if (chan >= 0 && chan < PAD_MAX_CONTROLLERS && s_pads[chan] != NULL) {
        /* 1 = rumble, 0 = stop, 2 = hard stop */
        SDL_RumbleGamepad(s_pads[chan], command == 1 ? 0xC000 : 0, command == 1 ? 0xC000 : 0,
                          command == 1 ? 1000 : 0);
    }
}

void *PADSetSamplingCallback(void (*callback)(void)) {
    void *old = (void *)s_samplingCallback;
    s_samplingCallback = callback;
    return old;
}

/* On the GameCube the sampling callback ran from the SI interrupt after each
 * controller poll; here it runs once per retrace (vi.c). The game's callback
 * calls PADRead itself, so it must not run from inside PADRead. */
void Pad_PortSample(void) {
    static int running;
    s_frame++;
    if (s_samplingCallback != NULL && !running) {
        running = 1;
        s_samplingCallback();
        running = 0;
    }
}

void SIInit(void) {}

/* Device type on a controller port: a standard pad wherever SDL has one (and
 * on port 1, which the keyboard drives), nothing elsewhere. Wheels are never
 * reported (see lgwheels.c). */
#define SI_GC_CONTROLLER 0x09000000u
#define SI_ERROR_NO_RESPONSE 0x00000008u
u32 SIProbe(s32 chan) {
    if (chan < 0 || chan >= PAD_MAX_CONTROLLERS) {
        return SI_ERROR_NO_RESPONSE;
    }
    return (s_pads[chan] != NULL || chan == 0) ? SI_GC_CONTROLLER : SI_ERROR_NO_RESPONSE;
}

BOOL PADReset(u32 mask) {
    (void)mask; /* SDL re-opens gamepads itself on (re)connect */
    return TRUE;
}
void SISetSamplingRate(u32 msec) { (void)msec; }

/* No Logitech wheel support; the game falls back to the controller. */
s32 SIResetSteeringAsync(s32 chan, void *callback) { (void)chan; (void)callback; return -1; }
s32 SIReadSteering(s32 chan, void *status) { (void)chan; (void)status; return -1; }
s32 SIControlSteering(s32 chan, u32 control, s32 level) { (void)chan; (void)control; (void)level; return -1; }
void *SISetSteeringSamplingCallback(void *callback) { (void)callback; return NULL; }
