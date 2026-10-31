/* The PC release's keyboard controls, brought back. Port-only file.
 *
 * The PC and GameCube versions share the input layer: 20 virtual keys
 * ("slots") per controller, which the controller layouts (g_keyBinding,
 * byte for byte the same in hotwheels.exe) assign to the player's 14
 * actions. The PC filled the slots from DirectInput and the keyboard, the
 * GameCube from the pad (g_pKeyToDI).
 *
 * Here the keyboard (pad.c, Port_KeyboardKeys) is controller 1:
 * - in menus (and the pause menu): arrows move, Enter accepts (A, and Start,
 *   which "Press START" and many rows take, except in the pause menu, where
 *   Start resumes), Escape goes back (B), X is the X button (stats); these go
 *   into the slots (Port_KeyboardSlots, from Input_Heartbeat). Screens put
 *   up during gameplay that wait for buttons (the mission information window
 *   at the start, briefings) get the same, but Enter is A only there, as
 *   Start would also pause;
 * - racing: the PC release's default controls, given to the player's actions
 *   directly, whatever controller layout is chosen (Port_KeyboardActions and
 *   Port_KeyboardSteer, from the player's control reader); Escape pauses
 *   (Start).
 *
 * Actions (Player.down[]): 0 strafe / stunt roll left, 1 change weapon,
 * 2 accelerate, 3 brake / reverse, 5 power slide, 6/7 stick up/down (stunt
 * flips), 8/9 stick right/left (steering), 10 strafe / stunt roll right,
 * 11 fire, 12 change camera. */
#include "common.h"
#include "CarA.h"
#include "Input.h"

/* The PC release's default keys for the actions. */
static const struct {
    u8 key;
    u8 action;
} s_actions[] = {
    { PORT_KEY_UP, 2 },         /* Accelerate */
    { PORT_KEY_DOWN, 3 },       /* Brake / Reverse */
    { PORT_KEY_CTRL, 1 },       /* Change Weapon */
    { PORT_KEY_SPACE, 11 },     /* Fire Weapon */
    { PORT_KEY_SHIFT, 5 },      /* Power Slide */
    { PORT_KEY_A, 0 },          /* Strafe / Stunt Roll L */
    { PORT_KEY_D, 10 },         /* Strafe / Stunt Roll R */
    { PORT_KEY_GRAVE, 12 },     /* Change Camera */
    { PORT_KEY_LEFT, 9 },       /* Steering Left */
    { PORT_KEY_RIGHT, 8 },      /* Steering Right */
    { PORT_KEY_W, 6 },          /* Stunt Flip Forward */
    { PORT_KEY_S, 7 },          /* Stunt Flip Backward */
};

/* Slots (g_pKeyToDI): 0 Start, 2-5 left/right/up/down, 6 B, 7 X, 9 A. */
enum { SLOT_START = 0, SLOT_LEFT = 2, SLOT_RIGHT = 3, SLOT_UP = 4, SLOT_DOWN = 5, SLOT_B = 6, SLOT_X = 7, SLOT_A = 9 };

#define KEY(k) (keys & (1u << (k)))

/* In a race, not paused: the keys are the player's controls. */
extern u8 g_pauseState[];   /* State_GamePlay.c: a u32 */
s32 Port_InterfaceTakingInput(void);

/* In gameplay, a screen waits for buttons (the mission information window,
 * a briefing). */
static s32 MissionScreen(void) {
    return Port_InterfaceTakingInput();
}

static s32 InGameplay(void) {
    return StateManager_CurrState() == 5 && *(u32 *)(void *)g_pauseState == 0;
}

/* Racing: the keys are the player's controls. */
static s32 Racing(void) {
    return InGameplay() && !MissionScreen();
}

/* Input_Heartbeat, after the pads: the keyboard's slots on controller 1. */
void Port_KeyboardSlots(VirtualPad *pad) {
    static u32 held;
    u32 keys = Port_KeyboardKeys(), now = 0, released, i;

    if (Racing()) {
        if (KEY(PORT_KEY_ESCAPE)) now |= 1u << SLOT_START;
    } else if (InGameplay()) {   /* a briefing: no Start, which would pause */
        if (KEY(PORT_KEY_UP)) now |= 1u << SLOT_UP;
        if (KEY(PORT_KEY_DOWN)) now |= 1u << SLOT_DOWN;
        if (KEY(PORT_KEY_LEFT)) now |= 1u << SLOT_LEFT;
        if (KEY(PORT_KEY_RIGHT)) now |= 1u << SLOT_RIGHT;
        if (KEY(PORT_KEY_ENTER)) now |= 1u << SLOT_A;
        if (KEY(PORT_KEY_ESCAPE)) now |= 1u << SLOT_B;
        if (KEY(PORT_KEY_X)) now |= 1u << SLOT_X;   /* the X button (stats) */
    } else {
        if (KEY(PORT_KEY_UP)) now |= 1u << SLOT_UP;
        if (KEY(PORT_KEY_DOWN)) now |= 1u << SLOT_DOWN;
        if (KEY(PORT_KEY_LEFT)) now |= 1u << SLOT_LEFT;
        if (KEY(PORT_KEY_RIGHT)) now |= 1u << SLOT_RIGHT;
        /* Start too for "Press START" and the like, but not in the pause
         * menu, where Start resumes */
        if (KEY(PORT_KEY_ENTER)) now |= 1u << SLOT_A | (StateManager_CurrState() == 5 ? 0 : 1u << SLOT_START);
        if (KEY(PORT_KEY_ESCAPE)) now |= 1u << SLOT_B;
        if (KEY(PORT_KEY_X)) now |= 1u << SLOT_X;   /* the X button (stats) */
    }
    released = held & ~now;
    pad->keyPress |= now & ~held;
    pad->keyDown = (pad->keyDown & ~released) | now;
    pad->keyRelease |= released;
    for (i = 0; i < 20; i++) {
        if (now & (1u << i)) {
            pad->keyValue[i] = 0xFF;
        }
    }
    held = now;
}

/* The player's control reader, after the pad: the keyboard's actions, for
 * the human player on controller 1 in a race. */
void Port_KeyboardActions(Player *player) {
    static u32 held;
    u32 keys, now = 0, i;

    if (player->device != 0 || !Racing()) {
        held = 0;
        return;
    }
    keys = Port_KeyboardKeys();
    for (i = 0; i < sizeof(s_actions) / sizeof(s_actions[0]); i++) {
        if (KEY(s_actions[i].key)) {
            now |= 1u << s_actions[i].action;
            player->down[s_actions[i].action] = 0xFF;
        }
    }
    player->buttonsHeld |= now & ~held;
    held = now;
}

/* ... and after the stick: steering and stunt flips are stick directions. */
void Port_KeyboardSteer(Player *player) {
    u32 keys;

    if (player->device != 0 || !Racing()) {
        return;
    }
    keys = Port_KeyboardKeys();
    if (KEY(PORT_KEY_W)) player->stickY = 0x7F;
    if (KEY(PORT_KEY_S)) player->stickY = -0x7F;
    if (KEY(PORT_KEY_LEFT)) player->stickX = -0x7F;
    if (KEY(PORT_KEY_RIGHT)) player->stickX = 0x7F;
}
