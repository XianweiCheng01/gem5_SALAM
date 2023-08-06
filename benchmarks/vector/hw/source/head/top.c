#include "head_defines.h"

void top(uint64_t in_addr, uint64_t out_addr, uint64_t const_addr) {

	//Initialize Accelerators
	volatile uint8_t * S1 	= (uint8_t *)S1_MMR;
	volatile uint8_t * S2 		= (uint8_t *)S2_MMR;
	volatile uint8_t * S3 		= (uint8_t *)S3_MMR;

	//Initialize DMAs
	//StreamDma
	volatile uint8_t  * StrDmaFlags				= (uint8_t  *)(STREAM_DMA_MMR);
	volatile uint64_t * StrDmaRdAddr			= (uint64_t *)(STREAM_DMA_MMR+4);
	volatile uint64_t * StrDmaWrAddr			= (uint64_t *)(STREAM_DMA_MMR+12);
	volatile uint32_t * StrDmaRdFrameSize		= (uint32_t *)(STREAM_DMA_MMR+20);
	volatile uint8_t  * StrDmaNumRdFrames		= (uint8_t  *)(STREAM_DMA_MMR+24);
	volatile uint8_t  * StrDmaRdFrameBuffSize	= (uint8_t  *)(STREAM_DMA_MMR+25);
	volatile uint32_t * StrDmaWrFrameSize		= (uint32_t *)(STREAM_DMA_MMR+26);
	volatile uint8_t  * StrDmaNumWrFrames		= (uint8_t  *)(STREAM_DMA_MMR+30);
	volatile uint8_t  * StrDmaWrFrameBuffSize	= (uint8_t  *)(STREAM_DMA_MMR+31);
	//MemDma
	volatile uint8_t  * MemDmaFlags				= (uint8_t  *)(CLUSTER_DMA_MMR);
	volatile uint64_t * MemDmaRdAddr			= (uint64_t *)(CLUSTER_DMA_MMR+1);
	volatile uint64_t * MemDmaWrAddr			= (uint64_t *)(CLUSTER_DMA_MMR+9);
	volatile uint32_t * MemDmaCopyLen			= (uint32_t *)(CLUSTER_DMA_MMR+17);
	//Initialize DRAM-Stream DMA
	*StrDmaRdAddr = in_addr;
	*StrDmaRdFrameSize = INPUT_SIZE;
	*StrDmaNumRdFrames = 1;
	*StrDmaRdFrameBuffSize = 1;
	//Initialize Stream-DRAM DMA
	*StrDmaWrAddr = out_addr;
	*StrDmaWrFrameSize = OUTPUT_SIZE;
	*StrDmaNumWrFrames = 1;
	*StrDmaWrFrameBuffSize = 1;
	//Start Stream DMAs
	*StrDmaFlags = STR_DMA_INIT_RD | STR_DMA_INIT_WR;

	//Transfer constants table
	*MemDmaRdAddr  = const_addr;
	*MemDmaWrAddr  = S1Buffer;
	*MemDmaCopyLen = INPUT_SIZE*1;
	*MemDmaFlags   = MEM_DMA_INIT;
	//Poll DMA for finish
	while ((*MemDmaFlags & MEM_DMA_INTR) != MEM_DMA_INTR);
	//Start Norm Conv
	*S1 = 0x01;
	//Start DW Conv
	*S2 = 0x01;
	//Start PW Conv
	*S3 = 0x01;

	//Wait for all accelerators to finish before sending interrupt to CPU
	while ((*StrDmaFlags & 0x08) == 0x08);
	return;
}