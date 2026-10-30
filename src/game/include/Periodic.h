#ifndef PERIODIC_H
#define PERIODIC_H
#include "Force.h"

class Periodic : public Force
{
public:
	Periodic();
	long DownloadForce(long chan, long effect, unsigned long &device, unsigned char type, unsigned long duration, unsigned long startDelay, unsigned char magnitude, unsigned short direction, unsigned short period, unsigned short phase, short offset, unsigned long attackTime, unsigned long fadeTime, unsigned char attackLevel, unsigned char fadeLevel);
	long UpdateForce(long chan, long effect, unsigned char type, unsigned long duration, unsigned long startDelay, unsigned char axis, unsigned short magnitude, unsigned short offset, unsigned short phase, short period, unsigned long attackTime, unsigned long fadeTime, unsigned char attackLevel, unsigned char fadeLevel);
};

#endif
