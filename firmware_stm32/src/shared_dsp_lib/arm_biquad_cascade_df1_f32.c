#include "main.h"

/* CMSIS-DSP Biquad structure alignment */
typedef struct {
    uint32_t numStages;
    float *pState;
    float *pCoeffs;
} arm_biquad_casd_df1_inst_f32;

void arm_biquad_cascade_df1_init_f32(arm_biquad_casd_df1_inst_f32 *S, uint8_t numStages, float *pCoeffs, float *pState) {
    S->numStages = numStages;
    S->pCoeffs = pCoeffs;
    /* Clear state buffer (set to 0) */
    for (uint32_t i = 0; i < (numStages * 4U); i++) {
        pState[i] = 0.0f;
    }
    S->pState = pState;
}

void arm_biquad_cascade_df1_f32(const arm_biquad_casd_df1_inst_f32 *S, const float *pSrc, float *pDst, uint32_t blockSize) {
    float *pState = S->pState;
    float *pCoeffs = S->pCoeffs;
    float *pIn = (float *)pSrc;
    float *pOut = pDst;
    
    uint32_t stage = S->numStages;

    do {
        /* Read coefficients for the current stage */
        float b0 = pCoeffs[0];
        float b1 = pCoeffs[1];
        float b2 = pCoeffs[2];
        float a1 = pCoeffs[3];
        float a2 = pCoeffs[4];
        pCoeffs += 5;

        /* Read state variables */
        float x1 = pState[0];
        float x2 = pState[1];
        float y1 = pState[2];
        float y2 = pState[3];

        for (uint32_t i = 0; i < blockSize; i++) {
            float in = pIn[i];
            /* Execute Direct Form I Biquad equation */
            float out = (b0 * in) + (b1 * x1) + (b2 * x2) + (a1 * y1) + (a2 * y2);

            /* Update state variables */
            x2 = x1;
            x1 = in;
            y2 = y1;
            y1 = out;

            pOut[i] = out;
        }

        /* Save state values back for the next frame processing */
        pState[0] = x1;
        pState[1] = x2;
        pState[2] = y1;
        pState[3] = y2;
        pState += 4;

        /* Switch pointers for cascade multi-stage execution */
        pIn = pDst;
        pOut = pDst;

    } while (--stage);
}
