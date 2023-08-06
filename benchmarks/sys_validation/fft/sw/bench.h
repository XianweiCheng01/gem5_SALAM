#include "../defines.h"
#include "data.h"

volatile int stage;

typedef struct {
    double * real;
    double * img;
    double * real_twid;
    double * img_twid;
    double * real_check;
    double * img_check;
} fft_struct;

void genData(fft_struct * ffts) {
    int i;
    for (i = 0; i < FFT_SIZE; i++) {
        ffts->real[i] = re[i];
        ffts->img[i] = co[i];
        ffts->real_check[i] = re_chk[i];
        ffts->img_check[i] = co_chk[i];
    }
    for (i = 0; i < FFT_SIZE/2; i++) {
        ffts->real_twid[i] = re_twid[i];
        ffts->img_twid[i] = co_twid[i];
    }
}

void fft_cpu(double* realbase, double* imgbase, double* realtwidbase, double* imgtwidbase) {

    //volatile uint8_t * realbase     = (uint8_t *)REALADDR;
    //volatile uint8_t * imgbase      = (uint8_t *)IMGADDR;
    //volatile uint8_t * realtwidbase = (uint8_t *)REALTWIDADDR;
    //volatile uint8_t * imgtwidbase  = (uint8_t *)IMGTWIDADDR;
    volatile double  * real         = (double  *)realbase;
    volatile double  * img          = (double  *)imgbase;
    volatile double  * real_twid    = (double  *)realtwidbase;
    volatile double  * img_twid     = (double  *)imgtwidbase;

    int even, odd, span, log, rootindex;
    double temp;

    log = 0;

    outer:
    for(span=FFT_SIZE>>1; span; span>>=1, log++){
        inner:
        for(odd=span; odd<FFT_SIZE; odd++){
            odd |= span;
            even = odd ^ span;

            temp = real[even] + real[odd];
            real[odd] = real[even] - real[odd];
            real[even] = temp;

            temp = img[even] + img[odd];
            img[odd] = img[even] - img[odd];
            img[even] = temp;

            rootindex = (even<<log) & (FFT_SIZE - 1);
            if(rootindex){
                temp = real_twid[rootindex] * real[odd] -
                    img_twid[rootindex]  * img[odd];
                img[odd] = real_twid[rootindex]*img[odd] +
                    img_twid[rootindex]*real[odd];
                real[odd] = temp;
            }
        }
    }
}
