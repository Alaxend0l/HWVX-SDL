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

/* Arrow keys the OS repeated since Port_KeyboardRepeats last ran. */
static u32 s_keyRepeats;

u32 Port_KeyboardRepeats(void) {
    u32 bits = s_keyRepeats;

    s_keyRepeats = 0;
    return bits;
}

static void MouseEvent(const SDL_Event *ev);

void Pad_PortHandleEvent(const SDL_Event *ev) {
    int i;
    MouseEvent(ev);   /* mouse steering (below) */
    if (ev->type == SDL_EVENT_KEY_DOWN && ev->key.repeat) {
        switch (ev->key.scancode) {
        case SDL_SCANCODE_UP: s_keyRepeats |= 1u << PORT_KEY_UP; break;
        case SDL_SCANCODE_DOWN: s_keyRepeats |= 1u << PORT_KEY_DOWN; break;
        case SDL_SCANCODE_LEFT: s_keyRepeats |= 1u << PORT_KEY_LEFT; break;
        case SDL_SCANCODE_RIGHT: s_keyRepeats |= 1u << PORT_KEY_RIGHT; break;
        default: break;
        }
    } else if (ev->type == SDL_EVENT_GAMEPAD_ADDED) {
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
    if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_EAST)) b |= PAD_BUTTON_B;
    if (SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_WEST)) b |= PAD_BUTTON_X;
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

/* Main stick directions pressed by HWVX_AUTOINPUT for pad 0: -1, 0, 1 per
 * axis. PADClamp gives these the full gate value on each axis, so two make a
 * full diagonal instead of the octagon's corner (40, 40), which the menus
 * (|x| > 100 after Input_NGC's scaling) don't count. */
static int s_keyStickX, s_keyStickY;

/* Keyboard: the keys the game reads (src/game/Port_Keyboard.c maps them to
 * the PC release's controls, past the pad: menus get slots, races player
 * actions). The keyboard counts as the controller on port 1. */
u32 Port_KeyboardKeys(void) {
    static const SDL_Scancode keys[] = {
        [PORT_KEY_UP] = SDL_SCANCODE_UP, [PORT_KEY_DOWN] = SDL_SCANCODE_DOWN,
        [PORT_KEY_LEFT] = SDL_SCANCODE_LEFT, [PORT_KEY_RIGHT] = SDL_SCANCODE_RIGHT,
        [PORT_KEY_ENTER] = SDL_SCANCODE_RETURN, [PORT_KEY_ESCAPE] = SDL_SCANCODE_ESCAPE,
        [PORT_KEY_SPACE] = SDL_SCANCODE_SPACE, [PORT_KEY_CTRL] = SDL_SCANCODE_LCTRL,
        [PORT_KEY_SHIFT] = SDL_SCANCODE_LSHIFT, [PORT_KEY_A] = SDL_SCANCODE_A, [PORT_KEY_D] = SDL_SCANCODE_D,
        [PORT_KEY_W] = SDL_SCANCODE_W, [PORT_KEY_S] = SDL_SCANCODE_S, [PORT_KEY_GRAVE] = SDL_SCANCODE_GRAVE,
        [PORT_KEY_X] = SDL_SCANCODE_X,
    };
    const bool *k = SDL_GetKeyboardState(NULL);
    u32 bits = 0, i;

    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        if (k[keys[i]]) {
            bits |= 1u << i;
        }
    }
    /* either Ctrl/Shift, and Enter on the keypad */
    if (k[SDL_SCANCODE_RCTRL]) bits |= 1u << PORT_KEY_CTRL;
    if (k[SDL_SCANCODE_RSHIFT]) bits |= 1u << PORT_KEY_SHIFT;
    if (k[SDL_SCANCODE_KP_ENTER]) bits |= 1u << PORT_KEY_ENTER;
    return bits;
}

/* Debugging: HWVX_AUTOINPUT="entry,..." presses buttons on port 1, each held
 * for 10 frames (frames = retraces). Entries run in order:
 *   N:btn   press at frame N after boot;
 *   +N:btn  press once the game has run N frames in a row without reading
 *           the disc since the previous press was released, so loading
 *           times don't shift the schedule (use this for menu navigation).
 * Either form takes an optional hold length: btn*N holds for N frames.
 * Buttons: a b x y start z l r up down left right; main stick: sup sdown
 * sleft sright. A button name ending in 2-4 presses it on that port instead
 * (a2: A on port 2); the ports named count as connected from the start. Each
 * press is logged with its frame. */
static u32 s_frame;

static int s_autoStickX, s_autoStickY;
static int s_autoPort;            /* the port the held button is on, 0-3 */

/* The ports HWVX_AUTOINPUT names (bit per port, port 1 not counted). */
static u32 AutoPorts(void) {
    static int parsed;
    static u32 ports;
    const char *e;
    int i;

    if (!parsed) {
        parsed = 1;
        e = SDL_getenv("HWVX_AUTOINPUT");
        for (i = 1; e != NULL && e[i] != 0; i++) {
            if (e[i] >= '2' && e[i] <= '4' && e[i - 1] >= 'a' && e[i - 1] <= 'z') {
                ports |= 1u << (e[i] - '1');
            }
        }
    }
    return ports;
}

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
        if (SDL_sscanf(next + rel, "%u:%15[a-z0-9]%n", &frame, name, &n) != 2) {
            Port_Log("HWVX_AUTOINPUT: can't parse \"%s\"", next);
            next = NULL;
        } else if (rel ? (s_frame >= pressEnd && quiet >= frame) : s_frame >= frame) {
            unsigned hold = 10;
            int m = 0;
            if (next[rel + n] == '*' && SDL_sscanf(next + rel + n + 1, "%u%n", &hold, &m) == 1) {
                n += 1 + m;   /* btn*N: hold for N frames */
            }
            SDL_strlcpy(held, name, sizeof(held));
            s_autoPort = 0;
            if (held[0] != 0 && held[SDL_strlen(held) - 1] >= '2' && held[SDL_strlen(held) - 1] <= '4') {
                s_autoPort = held[SDL_strlen(held) - 1] - '1';
                held[SDL_strlen(held) - 1] = 0;
            }
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
    st[0].err = PAD_ERR_NONE;   /* the keyboard is player 1's controller */
    {
        u16 bits = AutoInput();

        for (i = 1; i < PAD_MAX_CONTROLLERS; i++) {
            if (AutoPorts() & (1u << i)) {
                st[i].err = PAD_ERR_NONE;
            }
        }
        st[s_autoPort].button |= bits;
    }
    if (s_autoStickX) st[0].stickX = (s8)s_autoStickX;
    if (s_autoStickY) st[0].stickY = (s8)s_autoStickY;
    s_keyStickX = s_autoStickX ? (s_autoStickX > 0 ? 1 : -1) : 0;
    s_keyStickY = s_autoStickY ? (s_autoStickY > 0 ? 1 : -1) : 0;
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
        if (i == 0) {   /* autoinput directions: full on each axis (see s_keyStickX) */
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
    return (s_pads[chan] != NULL || chan == 0 || (AutoPorts() & (1u << chan))) ? SI_GC_CONTROLLER
                                                                                : SI_ERROR_NO_RESPONSE;
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

/* Mouse steering, the PC release's (off by default; port.h). The PC read the
 * mouse every input frame: the cursor's move since the last frame (it put the
 * cursor back in the middle of the screen each time, GetCursorPos and
 * SetCursorPos, hotwheels.exe 0x456b5e), the left and right buttons
 * (WM_LBUTTONDOWN and WM_RBUTTONDOWN, 0x455b60). Here SDL's relative mode
 * gives the same moves, the operating system's pointer speed applied as
 * GetCursorPos had it, while the game says the player is racing; the
 * game side turns them into the PC's stick and buttons (Port_Keyboard.c).
 *
 * Testing: HWVX_MOUSE_INJECT="dx,dy,btn*frames;..." plays mouse events
 * (SDL_PushEvent, through the same path) from the first frame the mouse is
 * captured, each entry for that many frames (btn: - l r lr). The real mouse
 * is then neither captured nor read (the capture is only logged), so a test
 * run leaves the desktop's mouse alone. */
#define MOUSE_INJECT_ID 0x7E57u

static volatile int s_mouseWant;
static u32 s_mouseWantFrame;
static int s_mouseCaptured;
static f32 s_mouseDX, s_mouseDY;
static u32 s_mouseButtons;

static const char *MouseInject(void) {
    static int read;
    static const char *script;

    if (!read) {
        read = 1;
        script = SDL_getenv("HWVX_MOUSE_INJECT");
    }
    return script;
}

int Port_MouseSteerOn(void) {
    static int env = -2;

    if (env == -2) {
        const char *e = SDL_getenv("HWVX_MOUSESTEER");

        env = e != NULL ? SDL_atoi(e) != 0 : -1;
    }
    return env >= 0 ? env : g_port.mouseSteer;
}

void Port_MouseSteerWant(int racing) {
    s_mouseWant = racing && Port_MouseSteerOn();
    s_mouseWantFrame = s_frame;
}

u32 Port_MouseSteerRead(f32 *dx, f32 *dy) {
    *dx = s_mouseDX;
    *dy = s_mouseDY;
    s_mouseDX = s_mouseDY = 0.0f;
    return s_mouseCaptured ? s_mouseButtons : 0;
}

static void MouseEvent(const SDL_Event *ev) {
    int test = MouseInject() != NULL;

    switch (ev->type) {
    case SDL_EVENT_MOUSE_MOTION:
        if (s_mouseCaptured && (!test || ev->motion.which == MOUSE_INJECT_ID)) {
            s_mouseDX += ev->motion.xrel;
            s_mouseDY += ev->motion.yrel;
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        u32 bit = ev->button.button == SDL_BUTTON_LEFT ? PORT_MOUSE_LEFT
                  : ev->button.button == SDL_BUTTON_RIGHT ? PORT_MOUSE_RIGHT : 0;

        if (test && ev->button.which != MOUSE_INJECT_ID) {
            break;
        }
        if (ev->type == SDL_EVENT_MOUSE_BUTTON_UP) {
            s_mouseButtons &= ~bit;
        } else if (s_mouseCaptured) {
            s_mouseButtons |= bit;
        }
        break;
    }
    default:
        break;
    }
}

/* HWVX_MOUSE_INJECT: this frame's events, while captured. */
static void MouseInjectFrame(void) {
    static const char *next;
    static int started, frames, dx, dy;
    static u32 buttons, held;
    SDL_Event ev;
    u32 b;

    if (!started) {
        started = 1;
        next = MouseInject();
    }
    while (frames == 0 && next != NULL && *next) {
        char btn[4] = "-";
        int n = 0;

        frames = 1;
        if (SDL_sscanf(next, "%d,%d,%3[-lr]%n", &dx, &dy, btn, &n) < 3) {
            Port_Log("HWVX_MOUSE_INJECT: can't parse \"%s\"", next);
            next = NULL;
            frames = 0;
            break;
        }
        next += n;
        if (*next == '*') {
            next++;
            frames = SDL_atoi(next);
            while (*next >= '0' && *next <= '9') next++;
        }
        if (*next == ';') next++;
        buttons = (SDL_strchr(btn, 'l') ? PORT_MOUSE_LEFT : 0) | (SDL_strchr(btn, 'r') ? PORT_MOUSE_RIGHT : 0);
        Port_Log("mouse inject: %d,%d %s for %d frames (frame %u)", dx, dy, btn, frames, (unsigned)s_frame);
    }
    if (frames == 0) {
        dx = dy = 0;
        buttons = 0;
    } else {
        frames--;
    }
    if (dx != 0 || dy != 0) {
        SDL_zero(ev);
        ev.type = SDL_EVENT_MOUSE_MOTION;
        ev.motion.which = MOUSE_INJECT_ID;
        ev.motion.xrel = (float)dx;
        ev.motion.yrel = (float)dy;
        SDL_PushEvent(&ev);
    }
    for (b = PORT_MOUSE_LEFT; b <= PORT_MOUSE_RIGHT; b <<= 1) {
        if ((buttons ^ held) & b) {
            SDL_zero(ev);
            ev.type = buttons & b ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            ev.button.which = MOUSE_INJECT_ID;
            ev.button.button = b == PORT_MOUSE_LEFT ? SDL_BUTTON_LEFT : SDL_BUTTON_RIGHT;
            ev.button.down = (buttons & b) != 0;
            SDL_PushEvent(&ev);
        }
    }
    held = buttons;
}

/* Before each event pump (vi.c): capture the mouse while the game wants it
 * (said within the last two frames: loading screens don't ask) and the
 * window has the focus; release it otherwise. */
void Pad_PortMouseUpdate(void) {
    int test = MouseInject() != NULL;
    int focused = test || (g_port.window != NULL && (SDL_GetWindowFlags(g_port.window) & SDL_WINDOW_INPUT_FOCUS));
    int want = s_mouseWant && s_frame - s_mouseWantFrame <= 2 && focused;

    if (want != s_mouseCaptured) {
        if (!test && g_port.window != NULL) {
            SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE, "1");
            SDL_SetWindowRelativeMouseMode(g_port.window, want);
        }
        Port_Log("mouse steering: %s%s", want ? "captured" : "released", test ? " (test: not really)" : "");
        s_mouseCaptured = want;
        s_mouseDX = s_mouseDY = 0.0f;
        s_mouseButtons = 0;
    }
    if (s_mouseCaptured && test) {
        MouseInjectFrame();
    }
}
