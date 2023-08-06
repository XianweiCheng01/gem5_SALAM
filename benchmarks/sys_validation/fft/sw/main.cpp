#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "bench.h"
//#include "fft.h"
#include "../../../common/m5ops.h"

fft_struct ffts;

#define BASE            0x80c00000

#define REAL_OFFSET     0
#define IMG_OFFSET      8*FFT_SIZE
#define RTWID_OFFSET    16*FFT_SIZE
#define ITWID_OFFSET    20*FFT_SIZE
#define RCHK_OFFSET     24*FFT_SIZE
#define ICHK_OFFSET     32*FFT_SIZE

volatile uint8_t  * top           = (uint8_t  *)0x2f200000;
volatile uint32_t * loc_real      = (uint32_t *)0x2f200001;
volatile uint32_t * loc_img       = (uint32_t *)0x2f200009;
volatile uint32_t * loc_real_twid = (uint32_t *)0x2f200011;
volatile uint32_t * loc_img_twid  = (uint32_t *)0x2f200019;

int main(void) {
	double *real       	= (double *)(BASE+REAL_OFFSET);
	double *img        	= (double *)(BASE+IMG_OFFSET);
	double *real_twid  	= (double *)(BASE+RTWID_OFFSET);
	double *img_twid   	= (double *)(BASE+ITWID_OFFSET);
	double *real_check 	= (double *)(BASE+RCHK_OFFSET);
	double *img_check  	= (double *)(BASE+ICHK_OFFSET);

    volatile int count = 0;
    stage = 0;

    ffts.real       = real;
    ffts.img        = img;
    ffts.real_twid  = real_twid;
    ffts.img_twid   = img_twid;
    ffts.real_check = real_check;
    ffts.img_check  = img_check;

    printf("Generating data\n");
    genData(&ffts);
    printf("Data generated\n");

    if(0) {
        *loc_real       = (uint32_t)(void *)real;
        *loc_img        = (uint32_t)(void *)img;
        *loc_real_twid  = (uint32_t)(void *)real_twid;
        *loc_img_twid   = (uint32_t)(void *)img_twid;

        *top = 0x01;
        while (stage < 1) count++;
    } else {
	fft_cpu(real, img, real_twid, img_twid);
    }
    printf("Job complete\n");

#ifdef CHECK
    bool fail = false;
	double creal, cimg;
    for (int i = 0; i < FFT_SIZE; i++) {
        creal = real[i] - real_check[i];
        cimg = img[i] - img_check[i];

        if((creal > EPSILON) || (creal < -EPSILON)) {
            fail = true;
        }
        if((cimg > EPSILON) || (cimg < -EPSILON)) {
            fail = true;
        }
        if (fail) {
            printf("Diff[%i] = Real: %f, Img: %f \n", i, real, img);
            break;
        }
    }
    if(fail)
        printf("Check Failed\n");
    else
        printf("Check Passed\n");
    /**/
#endif
    m5_dump_stats();
    m5_exit();
}
