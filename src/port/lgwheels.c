/* Logitech wheel SDK replacement.
 *
 * The original drove Logitech GameCube wheels (Speed Force) through the
 * LGWheels C++ class on top of the SI bus. On the port no wheel is reported,
 * so the game uses the ordinary controller path in Input_NGC.c; SDL already
 * exposes PC wheels as joysticks/gamepads there. Force feedback calls are
 * accepted and ignored.
 *
 * TODO: report SDL wheels (SDL_GetJoystickType == SDL_JOYSTICK_TYPE_WHEEL)
 * here and map the effects onto SDL_Haptic (spring, damper, constant).
 */
#include "lgwheels.h"

#include <stdlib.h>

void *__ct__8LGWheelsFv(void *self) { return self; }
void ReadAll__8LGWheelsFv(void *self) { (void)self; }

u8 IsOpened__8LGWheelsFl(void *self, s32 chan) { (void)self; (void)chan; return 0; }
s8 IsConnected__8LGWheelsFl(void *self, s32 chan) { (void)self; (void)chan; return 0; }
u8 PedalsConnected__8LGWheelsFl(void *self, s32 chan) { (void)self; (void)chan; return 0; }

void GenerateNonLinValues__8LGWheelsFlUc(void *self, s32 chan, u8 value) {
    (void)self; (void)chan; (void)value;
}
void PlaySpringForce__8LGWheelsFlScUcs(void *self, s32 chan, s8 offset, u8 saturation, s16 coefficient) {
    (void)self; (void)chan; (void)offset; (void)saturation; (void)coefficient;
}
void PlayDamperForce__8LGWheelsFlUcs(void *self, s32 chan, u8 coefficient, s16 offset) {
    (void)self; (void)chan; (void)coefficient; (void)offset;
}
void PlayConstantForce__8LGWheelsFlsUs(void *self, s32 chan, s16 magnitude, u16 direction) {
    (void)self; (void)chan; (void)magnitude; (void)direction;
}
void PlayFrontalCollisionForce__8LGWheelsFlUc(void *self, s32 chan, u8 force) {
    (void)self; (void)chan; (void)force;
}
void PlayCarAirborne__8LGWheelsFl(void *self, s32 chan) { (void)self; (void)chan; }
void StopCarAirborne__8LGWheelsFl(void *self, s32 chan) { (void)self; (void)chan; }

/* Logitech SI-level driver (below the LGWheels class). */
s32 LGOpen(s32 chan, u32 *handle) { (void)chan; *handle = 0xFFFFFFFFu; return -1; }
s32 LGRead(void *status) { (void)status; return 0; }

/* CodeWarrior's operator new(unsigned long), used to create the LGWheels object. */
void *__nw__FUl(u32 size) { return calloc(1, size); }
