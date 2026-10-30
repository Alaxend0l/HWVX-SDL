/* Logitech wheel SDK (LGWheels C++ class in the original), as called from the
 * game with its CodeWarrior-mangled names. See lgwheels.c. */
#ifndef PORT_LGWHEELS_H
#define PORT_LGWHEELS_H

#include "port.h"

void *__ct__8LGWheelsFv(void *self);
void ReadAll__8LGWheelsFv(void *self);
u8 IsOpened__8LGWheelsFl(void *self, s32 chan);
s8 IsConnected__8LGWheelsFl(void *self, s32 chan);
u8 PedalsConnected__8LGWheelsFl(void *self, s32 chan);
void GenerateNonLinValues__8LGWheelsFlUc(void *self, s32 chan, u8 value);
void PlaySpringForce__8LGWheelsFlScUcs(void *self, s32 chan, s8 offset, u8 saturation, s16 coefficient);
void PlayDamperForce__8LGWheelsFlUcs(void *self, s32 chan, u8 coefficient, s16 offset);
void PlayConstantForce__8LGWheelsFlsUs(void *self, s32 chan, s16 magnitude, u16 direction);
void PlayFrontalCollisionForce__8LGWheelsFlUc(void *self, s32 chan, u8 force);
void PlayCarAirborne__8LGWheelsFl(void *self, s32 chan);
void StopCarAirborne__8LGWheelsFl(void *self, s32 chan);

s32 LGOpen(s32 chan, u32 *handle);
s32 LGRead(void *status);
void *__nw__FUl(u32 size);

#endif
