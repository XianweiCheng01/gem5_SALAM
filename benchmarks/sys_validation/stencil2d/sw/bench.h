#include "../defines.h"

#define F_SIZE       3

#define EPSILON 1.0e-6
#define rcIndex     (r*COL + c)

volatile int stage;

typedef struct {
    TYPE * inp;
    TYPE * sol;
    TYPE * filter;
    TYPE * check;
} stencil_struct;

int checkData( stencil_struct * sts ) {
    int i;

    for (i = 0; i < ROW*COL; i++) {
        if (sts->sol[i]!= sts->check[i]) {
            printf("Check Failed\n");
            return 0;
        }
    }
    printf("Check Passed\n");

    return 1;
}

void genData(stencil_struct * sts) {
    int r, c, k1, k2, temp, mul, sol;

    for( r=0; r < ROW; r++ ) {
        for( c=0; c < COL; c++ ) {
            sts->inp[rcIndex] = rcIndex;
            //printf("Input[%d]=%d\n", rcIndex, rcIndex);
        }
    }

    for(k1=0; k1 < F_SIZE; k1++) {
        for (k2=0; k2 < F_SIZE; k2++){
            sts->filter[k1*F_SIZE + k2] = k1*F_SIZE + k2;
        }
    }

    for( r=0; r < ROW-2; r++ ) {
        for( c=0; c < COL-2; c++ ) {
            temp = (TYPE)0;

            for(k1=0; k1 < F_SIZE; k1++) {
                for (k2=0; k2 < F_SIZE; k2++){
                    mul = sts->filter[k1*F_SIZE + k2] * sts->inp[(r+k1)*COL + c + k2];
                    temp += mul;
                }
            }
            sts->check[rcIndex] = temp;
        }
    }

}

void stencil_cpu (TYPE* origbase, TYPE* solbase, TYPE* filterbase){
    //uint8_t * origbase   = (uint8_t *)ORIGADDR;
    //uint8_t * solbase    = (uint8_t *)SOLADDR;
    //uint8_t * filterbase = (uint8_t *)FILTERADDR;
    TYPE    * orig       = (TYPE    *)origbase;
    TYPE    * sol        = (TYPE    *)solbase;
    TYPE    * filter     = (TYPE    *)filterbase;
    int r, c, k1, k2;
    TYPE temp, mul;

    stencil_label1:
    //#pragma clang loop unroll(disable)
    for (c=0; c<COL-2; c++) {
        stencil_label2:
        //#pragma clang loop unroll_count(2)
        for (r=0; r<ROW-2; r++) {
            temp = (TYPE)0;
            stencil_label3:
            //#pragma clang loop unroll(disable)
            for (k1=0;k1<3;k1++){
                stencil_label4:
                //#pragma clang loop unroll(full)
                for (k2=0;k2<3;k2++){
                    mul = filter[k1*3 + k2] * orig[(r+k1)*COL + c+k2];
                    temp += mul;
                }
            }
            sol[(r*COL) + c] = temp;
        }
    }
}
