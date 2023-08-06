#include "../defines.h"

volatile int stage;

typedef struct {
    TYPE * C;
    TYPE * inp;
    TYPE * sol;
    TYPE * check;
} stencil_struct;

int checkData( stencil_struct * sts ) {
    int i, j, k;

    for( i=0; i < SIZE; i++ ) {
        if (sts->sol[i]!= sts->check[i]) {
            printf("Check Failed\n");
            return 0;
        }
    }
    printf("Check Passed\n");

    return 1;
}

void genData(stencil_struct * sts) {

    int i, j, k, sum0, sum1, mul0, mul1;

    for( i=0; i < SIZE; i++ ) {
        sts->inp[i] = i;
    }

    for( i=0; i < C_SIZE; i++ ) {
        sts->C[i] = i+2;
    }

    for(j=0; j < COL; j++) {
        for(k=0; k < ROW; k++) {
            sts->check[INDX(ROW, COL, k, j, 0)] = sts->inp[INDX(ROW, COL, k, j, 0)];
            sts->check[INDX(ROW, COL, k, j, HYT-1)] = sts->inp[INDX(ROW, COL, k, j, HYT-1)];
        }
    }
    for(i=1; i < HYT-1; i++) {
        for(k=0; k < ROW; k++) {
            sts->check[INDX(ROW, COL, k, 0, i)] = sts->inp[INDX(ROW, COL, k, 0, i)];
            sts->check[INDX(ROW, COL, k, COL-1, i)] = sts->inp[INDX(ROW, COL, k, COL-1, i)];
        }
    }
    for(i=1; i < HYT-1; i++) {
        for(j=1; j < COL-1; j++) {
            sts->check[INDX(ROW, COL, 0, j, i)] = sts->inp[INDX(ROW, COL, 0, j, i)];
            sts->check[INDX(ROW, COL, ROW-1, j, i)] = sts->inp[INDX(ROW, COL, ROW-1, j, i)];
        }
    }

    // Stencil computation
    for(i = 1; i < HYT - 1; i++){
        for(j = 1; j < COL - 1; j++){
            for(k = 1; k < ROW - 1; k++){
                sum0 = sts->inp[INDX(ROW, COL, k, j, i)];
                sum1 = sts->inp[INDX(ROW, COL, k, j, i + 1)] +
                       sts->inp[INDX(ROW, COL, k, j, i - 1)] +
                       sts->inp[INDX(ROW, COL, k, j + 1, i)] +
                       sts->inp[INDX(ROW, COL, k, j - 1, i)] +
                       sts->inp[INDX(ROW, COL, k + 1, j, i)] +
                       sts->inp[INDX(ROW, COL, k - 1, j, i)];
                mul0 = sum0 * sts->C[0];
                mul1 = sum1 * sts->C[1];
                sts->check[INDX(ROW, COL, k, j, i)] = mul0 + mul1;
            }
        }
    }
}

void stencil3d_cpu(TYPE* cbase, TYPE* origbase, TYPE* solbase) {
    //uint8_t * cbase    = (uint8_t *)CADDR;
    //uint8_t * origbase = (uint8_t *)ORIGADDR;
    //uint8_t * solbase  = (uint8_t *)SOLADDR;
    TYPE    * C        = (TYPE    *)cbase;
    TYPE    * orig     = (TYPE    *)origbase;
    TYPE    * sol      = (TYPE    *)solbase;

    int i, j, k;
    TYPE sum0, sum1, mul0, mul1;

    // Handle boundary conditions by filling with original values
    height_bound_col :
    //#pragma clang loop unroll(disable)
    for(j=0; j<COL; j++) {
        height_bound_row :
        // #pragma clang loop unroll(disable)
        //#pragma clang loop unroll_count(2)
        for(k=0; k<ROW; k++) {
            sol[INDX(ROW, COL, k, j, 0)] = orig[INDX(ROW, COL, k, j, 0)];
            sol[INDX(ROW, COL, k, j, HYT-1)] = orig[INDX(ROW, COL, k, j, HYT-1)];
        }
    }
    col_bound_height :
    //#pragma clang loop unroll(disable)
    for(i=1; i<HYT-1; i++) {
        col_bound_row :
        // #pragma clang loop unroll(disable)
        //#pragma clang loop unroll_count(2)
        for(k=0; k<ROW; k++) {
            sol[INDX(ROW, COL, k, 0, i)] = orig[INDX(ROW, COL, k, 0, i)];
            sol[INDX(ROW, COL, k, COL-1, i)] = orig[INDX(ROW, COL, k, COL-1, i)];
        }
    }
    row_bound_height :
    //#pragma clang loop unroll(disable)
    for(i=1; i<HYT-1; i++) {
        row_bound_col :
        // #pragma clang loop unroll(disable)
        //#pragma clang loop unroll_count(2)
        for(j=1; j<COL-1; j++) {
            sol[INDX(ROW, COL, 0, j, i)] = orig[INDX(ROW, COL, 0, j, i)];
            sol[INDX(ROW, COL, ROW-1, j, i)] = orig[INDX(ROW, COL, ROW-1, j, i)];
        }
    }

    // Stencil computation
    loop_height :
    //#pragma clang loop unroll_count(2)
    for(i = 1; i < HYT - 1; i++){
        loop_col :
        //#pragma clang loop unroll_count(2)
        for(j = 1; j < COL - 1; j++){
            loop_row :
            //#pragma clang loop unroll_count(2)
            for(k = 1; k < ROW - 1; k++){
                sum0 = orig[INDX(ROW, COL, k, j, i)];
                sum1 = orig[INDX(ROW, COL, k, j, i + 1)] +
                       orig[INDX(ROW, COL, k, j, i - 1)] +
                       orig[INDX(ROW, COL, k, j + 1, i)] +
                       orig[INDX(ROW, COL, k, j - 1, i)] +
                       orig[INDX(ROW, COL, k + 1, j, i)] +
                       orig[INDX(ROW, COL, k - 1, j, i)];
                mul0 = sum0 * C[0];
                mul1 = sum1 * C[1];
                sol[INDX(ROW, COL, k, j, i)] = mul0 + mul1;
            }
        }
    }
}
