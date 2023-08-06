#include "../common/support.h"

/***********************************************************
 * Computation Defines
 ***********************************************************/
#define VECTOR_SIZE		8


// StreamDMA
#define INPUT_SIZE		VECTOR_SIZE
#define OUTPUT_SIZE		VECTOR_SIZE

/***********************************************************
 * Cluster Base Address
 ***********************************************************/
#define BASE			0x2F000000
/***********************************************************
 * MMR Addresses
 ***********************************************************/
#define TOP_MMR			BASE + 0x0000
#define STREAM_DMA_MMR	BASE + 0x0041
#define CLUSTER_DMA_MMR	BASE + 0x0069
#define S1_MMR  		BASE + 0x007E
#define S2_MMR			BASE + 0x007F
#define S3_MMR			BASE + 0x0080

/***********************************************************
 * Memory Buffer and SPM Addresses
 ***********************************************************/
#define StreamIn		BASE + 0x0061
#define StreamOut		BASE + 0x0061

#define S1In 			StreamIn
#define S1Buffer		BASE + 0x0081
#define S1Out			BASE + 0x0774

#define S2In 			S1Out
#define S2Out			BASE + 0x18E5

#define S3In 			S2Out
#define S3Out			StreamOut