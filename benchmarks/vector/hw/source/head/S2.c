#include "head_defines.h"


void S2() {
	volatile dType_8u * STR_IN  	= (dType_8u *)(S2In);
	volatile dType_8u * STR_OUT  	= (dType_8u *)(S2Out);

	for (dType_Reg i = 0; i < INPUT_SIZE; i++) {
			*STR_OUT = (*STR_IN) * 2;
        }
}
