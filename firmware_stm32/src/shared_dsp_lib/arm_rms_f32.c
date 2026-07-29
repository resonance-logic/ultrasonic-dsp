#include "main.h"
#include <math.h>

void arm_rms_f32(const float *pSrc, uint32_t blockSize, float *pResult) {
    float sum = 0.0f;
    uint32_t blkCnt;

    /* Compute sum of squares */
    for (blkCnt = 0; blkCnt < blockSize; blkCnt++) {
        float in = pSrc[blkCnt];
        sum += in * in;
    }

    /* Compute Root Mean Square and store the result */
    *pResult = sqrtf(sum / (float)blockSize);
}
