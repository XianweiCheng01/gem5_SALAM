#include "../defines.h"

#include "data.h"

#define MATCH_SCORE 1
#define MISMATCH_SCORE -1
#define GAP_SCORE -1

#define ALIGN '\\'
#define SKIPA '^'
#define SKIPB '<'

#define MAX(A,B) ( ((A)>(B))?(A):(B) )

volatile int stage;

typedef struct {
    char * seqA;
    char * seqB;
    char * alignedA;
    char * alignedB;
    char * checkA;
    char * checkB;
} nw_struct;

void genData(nw_struct * nws) {
    int i;
    for (i = 0; i < ALEN; i++) {
        nws->seqA[i] = a[i];
    }
    for (i = 0; i < BLEN; i++) {
        nws->seqB[i] = b[i];
    }
    for (i = 0; i < (ALEN+BLEN); i++) {
        nws->checkA[i] = alia[i];
        nws->checkB[i] = alib[i];
    }
}

void needwun_cpu(char* base){

    volatile uint8_t * SEQA     = (uint8_t *)base;
    volatile uint8_t * SEQB     = (uint8_t *)(base+ALEN);
    volatile uint8_t * alignedA = (uint8_t *)(base+(ALEN+BLEN));
    volatile uint8_t * alignedB = (uint8_t *)(base+2*(ALEN+BLEN));
    volatile uint8_t * mbase    = (uint8_t *)(base+3*(ALEN+BLEN));
    volatile uint8_t * ptr      = (uint8_t *)(base+3*(ALEN+BLEN)+4*(ALEN+1)*(BLEN+1));
    volatile int32_t * M        = (int32_t *)mbase;

    int score, up_left, up, left, max;
    int row, row_up, r;
    int a_idx, b_idx;
    int a_str_idx, b_str_idx;

    init_row:
    for(a_idx=0; a_idx<(ALEN+1); a_idx++){
        M[a_idx] = a_idx * GAP_SCORE;
    }
    init_col:
    for(b_idx=0; b_idx<(BLEN+1); b_idx++){
        M[b_idx*(ALEN+1)] = b_idx * GAP_SCORE;
    }

    // Matrix filling loop
    fill_out:
    for(b_idx=1; b_idx<(BLEN+1); b_idx++){
        fill_in:
        for(a_idx=1; a_idx<(ALEN+1); a_idx++){
            if(SEQA[a_idx-1] == SEQB[b_idx-1]){
                score = MATCH_SCORE;
            } else {
                score = MISMATCH_SCORE;
            }

            row_up = (b_idx-1)*(ALEN+1);
            row = (b_idx)*(ALEN+1);

            up_left = M[row_up + (a_idx-1)] + score;
            up      = M[row_up + (a_idx  )] + GAP_SCORE;
            left    = M[row    + (a_idx-1)] + GAP_SCORE;

            max = MAX(up_left, MAX(up, left));

            M[row + a_idx] = max;
            if(max == left){
                ptr[row + a_idx] = SKIPB;
            } else if(max == up){
                ptr[row + a_idx] = SKIPA;
            } else{
                ptr[row + a_idx] = ALIGN;
            }
        }
    }

    // TraceBack (n.b. aligned sequences are backwards to avoid string appending)
    a_idx = ALEN;
    b_idx = BLEN;
    a_str_idx = 0;
    b_str_idx = 0;

    trace:
    while(a_idx>0 || b_idx>0) {
        r = b_idx*(ALEN+1);
        if (ptr[r + a_idx] == ALIGN){
            alignedA[a_str_idx++] = SEQA[a_idx-1];
            alignedB[b_str_idx++] = SEQB[b_idx-1];
            a_idx--;
            b_idx--;
        }
        else if (ptr[r + a_idx] == SKIPB){
            alignedA[a_str_idx++] = SEQA[a_idx-1];
            alignedB[b_str_idx++] = '-';
            a_idx--;
        }
        else{ // SKIPA
            alignedA[a_str_idx++] = '-';
            alignedB[b_str_idx++] = SEQB[b_idx-1];
            b_idx--;
        }
    }

    // Pad the result
    pad_a:
    for( ; a_str_idx<ALEN+BLEN; a_str_idx++ ) {
      alignedA[a_str_idx] = '_';
    }
    pad_b:
    for( ; b_str_idx<ALEN+BLEN; b_str_idx++ ) {
      alignedB[b_str_idx] = '_';
    }
}
