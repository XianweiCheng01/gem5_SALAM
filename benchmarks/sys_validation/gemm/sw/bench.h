#include "../defines.h"

#define rcIndex     (r*ROW + c)

volatile int stage;

typedef struct {
    TYPE * a;
    TYPE * b;
    TYPE * c;
    int row_size;
    int col_size;
} gemm_struct;

void genData(gemm_struct * ges) {
    int r, c, k, mult, sum;

    for( r=0; r < ges->row_size; r++ ) {
        for( c=0; c < ges->col_size; c++ ) {
            ges->a[rcIndex] = rcIndex;
            ges->b[rcIndex] = (ges->row_size * ges->col_size) - 1 - rcIndex;
        }
    }
}

void gemm_cpu(TYPE* m1base, TYPE* m2base, TYPE* m3base){
    //uint8_t * m1base = (uint8_t *)M1ADDR;
    //uint8_t * m2base = (uint8_t *)M2ADDR;
    //uint8_t * m3base = (uint8_t *)M3ADDR;
    TYPE    * m1     = (TYPE    *)m1base;
    TYPE    * m2     = (TYPE    *)m2base;
    TYPE    * m3     = (TYPE    *)m3base;
    int k_col, i_col;
    TYPE mult, sum;
    for(int i=0;i<ROW;i++) {
        for(int j=0;j<COL;j++) {
            i_col = i * COL;
            sum = 0;
            //#pragma clang loop unroll(full)
            for(int k=0;k<ROW;k++) {
                k_col = k * COL;
                mult = m1[i_col + k] * m2[k_col + j];
                sum += mult;
            }
            m3[i_col + j]  = sum;
        }
    }
}
