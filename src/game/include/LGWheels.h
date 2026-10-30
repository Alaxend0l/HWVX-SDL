/* Layout of the Logitech wheel object (main/LLGWheels' "LGWheels" class).
 * One object per wheel port, holding the Wheels, Force and Condition subobjects
 * the LG SDK uses back to back, plus the per-channel force parameters that were
 * last played on the port.  Offsets come from the raw accesses in src/LGWheels.c. */
#ifndef LGWHEELS_H
#define LGWHEELS_H

#include "common.h"

/* The wheel itself: 0x880 bytes, embedded in LGWheels at 0x82C. */
typedef struct Wheels
{
	struct {
		/* 0x000 per-channel controller state */
		u8		unk0[2];
		/* 0x002 */
		u8		pedalFlags;		/* bit 3 says both pedals are connected */
		/* 0x003 */
		u8		unk3[6];
		/* 0x009 */
		s8		opensErr;		/* 0 once the wheel opened */
		}		unk0[4];			/* 0x000 */
	s16		nonLinValues[4][256];	/* 0x028 non-linear force table */
	u32		overallGainId[4];		/* 0x828 per-channel gain ids, handed to Force (by reference) */
		/* 0x838 */
		u32		unk838[4];		/* device type, as returned by SIProbe */
		/* 0x848 */
		u8		unk848[0x38];
		/* 0x880 */

		/* The wheel's own methods, in main/Wheels. */
		Wheels();
		u8		IsConnected(s32 chan);
		/* 0x880 */
		u8		IsOpened(s32 chan);
		/* 0x880 */
		u8		PedalsConnected(s32 chan);
		/* 0x880 */
		f32		CalculateNonLinValue(s32 input, u8 coefficient, s16 min, s16 max);
		/* 0x880 */
		void		GenerateNonLinValues(s32 chan, u8 coefficient);
		/* 0x880 */
		s32		DownloadForce(s32 chan, u32 &gainId, u32 max, s16 magnitude, u16 direction, u32 a, u8 b, u8 c, u8 d);
		/* 0x880 */
		s16		ReadAll(void);		/* opens every wheel that is connected but not open yet;
												the channel that gave up, -1 once they all read fine */
		/* 0x880 */
} Wheels;

/* The force-feedback effect set: 0xA0 bytes, embedded in LGWheels at 0x10AC. */
typedef struct Force
{
	u8		unk0[0xA0];

		Force();
		/* The gain one of these effects plays at, in main/Force. */
		void		SetOverallForceGain(u32 &effectId, s32 gain);
		/* Start playing one of the class's effects on a channel, in main/Force. */
		s32		Start(s32 chan, s32 effectId);
		/* 0x0 */
		s32		GetOverallForceGain(u32 &effectId);
		/* 0x0 */
} Force;

/* The effect classes whose methods the port hands to Force: each one is 0xA0
 * bytes and holds the eight effect slots of every channel, with the id of the
 * effect that slot plays (-1 once it is free).  Which effect a class plays is
 * in its name; the bookkeeping they share is this layout. */
typedef struct Condition
{
	u8		playing[4][8];		/* 0x000 is this channel playing this slot? */
	/* 0x020 */
	u32		ids[4][8];		/* 0x020 effect ids, -1 once the slot is free */
	/* 0x0A0 */

	/* The condition class's methods, in main/Condition: damper effect 1 is the
	 * slot LGWheels' PlayDamperForce plays. */
	/* 0x0 */
	Condition();
	long DownloadForce(long chan, long effect, unsigned long &device, unsigned char type, unsigned long duration, unsigned long startDelay, signed char offset, unsigned char deadband, unsigned char saturationPos, unsigned char saturationNeg, short coefficientPos, short coefficientNeg);
	/* 0x0 */
	long UpdateForce(long chan, long effect, unsigned char type, unsigned long duration, unsigned long startDelay, signed char offset, unsigned char deadband, unsigned char saturationPos, unsigned char saturationNeg, short coefficientPos, short coefficientNeg);
	/* 0x0 */
} Condition;

typedef struct Constant
{
	u8		playing[4][8];			/* 0x000 is this channel playing this slot? */
	/* 0x020 */
	u32		ids[4][8];			/* 0x020 effect ids, -1 once the slot is free */
	/* 0x0A0 */

		/* The effect classes' methods, in main/Constant. */
		Constant();
		long		DownloadForce(long chan, long effect, unsigned long &device, unsigned long duration, unsigned long startDelay, short magnitude, unsigned short direction, unsigned long attackTime, unsigned long fadeTime, unsigned char attackLevel, unsigned char fadeLevel);
		/* 0x0A0 */
		long		UpdateForce(long chan, long effect, unsigned long duration, unsigned long startDelay, short magnitude, unsigned short direction, unsigned long attackTime, unsigned long fadeTime, unsigned char attackLevel, unsigned char fadeLevel);
		/* 0x0 */
} Constant;

typedef struct Periodic
{
	u8		playing[4][8];			/* 0x000 is this channel playing this slot? */
	/* 0x020 */
	u32		ids[4][8];			/* 0x020 effect ids, -1 once the slot is free */
	/* 0x0A0 */

		/* The effect classes' methods, in main/Periodic: periodic effect 1 is the
		 * slot PlayFrontalCollisionForce plays. */
		/* 0x0 */
		Periodic();
		long DownloadForce(long chan, long effect, unsigned long &device, unsigned char type, unsigned long duration, unsigned long startDelay, unsigned char saturation, unsigned short offset, unsigned short coefficient, unsigned short end, short decay, unsigned long rate, unsigned long repeatTime, unsigned char a, unsigned char b);
		/* 0x0A0 */
		long UpdateForce(long chan, long effect, unsigned char type, unsigned long duration, unsigned long startDelay, unsigned char saturation, unsigned short offset, unsigned short coefficient, unsigned short end, short decay, unsigned long rate, unsigned long repeatTime, unsigned char a, unsigned char b);
		/* 0x0 */
} Periodic;

typedef struct Ramp
{
	u8		playing[4][8];		/* 0x000 is this channel playing this slot? */
	/* 0x020 */
	u32		ids[4][8];		/* 0x020 effect ids, -1 once the slot is free */
	/* 0x0A0 */
	Ramp();
} Ramp;

typedef struct LGWheels
{
	u8		unk0[0x28];			/* 0x000 port state */
	u8		nonLinValues[0x800];		/* 0x028 copy of the wheel's table */
		/* 0x828 */
		u8		airborne[4];		/* unknown; pads the wheel subobject to 0x82C */
	Wheels		limbo;		/* 0x82C */
	Force		limb;		/* 0x10AC */
	Condition	condition;		/* 0x114C */
	Constant constantForce;		/* 0x11EC */
	Periodic periodicForce;		/* 0x128C */
	Ramp rampForce;		/* 0x132C */
	u8 savedGain;		/* 0x13CC */
	u8 unk13CD;		/* 0x13CD */
	struct {
		/* 0x13CE spring force last played on the channel */
		s8		offet;
		u8		overflow;
		s16		efficiency;
		}		limp[4];
  struct {
    /* 0x13DE constant force last played on the channel */
    s16 force;
    u16 direction;
    } constantParams[4];
	struct {
		/* 0x13EE damper force last played on the channel */
		u8		offet;
		/* 0x13F0 */
		s16		efficiency;
		}		limper[4];
		/* 0x13FE */
  u8 unk13FE[0x10];
	s16		limbi[4];		/* 0x140E frontal collision force */
		/* 0x1416 */

	/* The port's own methods, defined in src/LGWheels.cpp; these are the ROM's
	 * IsOpened__8LGWheelsFl, PedalsConnected__8LGWheelsFl, SetOverallForceGain__8LGWheelsFll,
	 * GetOverallForceGain__8LGWheelsFl, GenerateNonLinValues__8LGWheelsFlUc and
	 * SameSpringForceParams__8LGWheelsFlScUcs, which hand their arguments to the subobjects. */
	u8		IsOpened(s32 chan);
		/* 0x1416 */
	u8		PedalsConnected(s32 chan);
			/* 0x1416 */
		void		SetOverallForceGain(s32 chan, s32 gain);
		s32		GetOverallForceGain(s32 chan);
		/* 0x1416 */
		void		GenerateNonLinValues(s32 chan, u8 coefficient);
		/* 0x1416 */
		LGWheels();
		void		InitLGLibrary(void);
		/* 0x1416 */
		u8		SameSpringForceParams(s32 chan, s8 offset, u8 saturation, s16 coefficient);
		/* 0x1416 */
		void		ReadAll(void);		/* 0x1416 lets every wheel read; frees chan's slots in
															 every effect class once one gives up */
		/* 0x1416 */
            u8		IsConnected(s32 chan);
        /* 0x1416 */
		void		PlayConstantForce(s32 chan, s16 magnitude, u16 direction);
		/* 0x1416 */
		u8		SameConstantForceParams(s32 chan, s16 magnitude, u16 direction);
		/* 0x1416 */
		void		PlayDamperForce(s32 chan, u8 coefficient, s16 offset);
		/* 0x1416 */
	 u8		SameDamperForceParams(s32 chan, u8 coefficient, s16 offset);
		/* Spring force (chan, offset, saturation, coefficient) the port plays now. */
		void		PlaySpringForce(s32 chan, s8 offset, u8 saturation, s16 coefficient);
		/* 0x1416 */
		/* Damper force (chan, coefficient, offset) the same ones limper plays now? */
		/* 0x1416 */
		/* Frontal collision force (chan, force) the port replays through the periodic effect. */
		void		PlayFrontalCollisionForce(s32 chan, u8 force);
		/* 0x1416 */
		u8		SameFrontalCollisionForceParams(s32 chan, s16 force);
		/* 0x1416 */
		void		PlayCarAirborne(s32 chan);
		void		StopCarAirborne(s32 chan);
} LGWheels;

#endif /* LGWHEELS_H */
