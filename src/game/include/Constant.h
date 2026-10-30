#ifndef CONSTANT_H
#define CONSTANT_H
#include "Force.h"

class Constant : public Force
{
public:
	Constant();

	long DownloadForce(long chan, long effect, unsigned long &device, unsigned long duration, unsigned long startDelay, short magnitude, unsigned short direction, unsigned long attackTime, unsigned long fadeTime, unsigned char attackLevel, unsigned char fadeLevel);

	long UpdateForce(long chan, long effect, unsigned long duration, unsigned long startDelay, short magnitude, unsigned short direction, unsigned long attackTime, unsigned long fadeTime, unsigned char attackLevel, unsigned char fadeLevel);
};

#endif
