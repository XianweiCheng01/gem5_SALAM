#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "host.h"
#include "../../common/m5ops.h"

int main(void) {
	m5_reset_stats();
    volatile uint64_t *input = (uint64_t*) input_addr;
    *input = 0x0102030405060708;
    volatile uint64_t *params = (uint64_t*) params_addr;
    *params = 0x0102030405060708;
    volatile uint64_t *output = (uint64_t*) output_addr;
    runHead(input_addr,output_addr,params_addr);
    uint64_t result = 0x0102030405060708;
    if ((result<<3) != *output) {
        printf("val = 0x%" PRIx64 "\n", *output);
    } else {
        printf("Result was correct\n");
    }

	m5_dump_stats();
	m5_exit();
    return 0;
}

void runHead(uint64_t input, uint64_t output, uint64_t params) {
    volatile uint8_t  * MMR  = (uint8_t  *)(head_top);
    uint64_t * ARGS = (uint64_t *)(head_top+1);
    printf("\n Setting args for HEAD \n");
    ARGS[0] = input;
    ARGS[1] = output;
    ARGS[2] = params;
    printf("\n Running HEAD\n");
    MMR[0]  = 0x01;
    while (MMR[0] != 0x0) {}
    printf("\n Finished HEAD\n");
}


