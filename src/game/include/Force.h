#ifndef FORCE_H
#define FORCE_H

class Force
{
public:
		Force();

	 long GetOverallForceGain(unsigned long &effectId);
	 long Start(long chan, long effect);
	long Destroy(long chan, long effect);
	void SetOverallForceGain(unsigned long &effectId, long gain);
	void InitVars();

	/* Which effect slots each channel is using, and the ids they play. */
	unsigned char playing[4][8];
	unsigned long effectIds[4][8];
};

#endif
