#ifndef CONDITION_H
#define CONDITION_H
#include "Force.h"

class Condition : public Force
{
public:
	Condition();

	long DownloadForce(long chan, long effect, unsigned long &device, unsigned char type, unsigned long duration, unsigned long startDelay, signed char offset, unsigned char deadband, unsigned char saturationPos, unsigned char saturationNeg, short coefficientPos, short coefficientNeg);
	/* The same effect re-uploaded into the device it already sits in (main/Condition),
	 * used by LGWheels' PlayDamperForce with damper effect 1, type 8. */
	long UpdateForce(long chan, long effect, unsigned char type, unsigned long duration, unsigned long startDelay, signed char offset, unsigned char deadband, unsigned char saturationPos, unsigned char saturationNeg, short coefficientPos, short coefficientNeg);
};

#endif
