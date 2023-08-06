#include "../hw/source/common/support.h"

#define input_addr       0x8c000000
#define params_addr      0x8d000000
#define output_addr      0x9c000000
#define head_top         0x2f000000




void runHead(uint64_t input, uint64_t output, uint64_t params);