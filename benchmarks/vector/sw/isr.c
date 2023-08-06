#include <stdio.h>
#include "host.h"

void head_isr(void)
{
	printf("Interrupt\n\r");
	//stage += 1;
	volatile uint8_t  * ISR_MMR  = (uint8_t  *)(head_top);
	ISR_MMR[0] = 0x0;
}

