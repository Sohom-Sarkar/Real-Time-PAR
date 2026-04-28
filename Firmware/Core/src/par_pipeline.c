/*
 * par_pipeline.c  —  Real-time feature extraction + inference pipeline
 *
 * Processing stages:
 *   1. Causal biquad IIR filters (noise LPF 18 Hz + gravity LPF 0.3 Hz)
 *   2. 284 time-domain and frequency-domain features per window
 *   3. Top-100 feature selection via pre-computed index lookup table
 *   4. Z-score normalisation with training-set statistics
 *   5. Random Forest inference via par_wrapper
 *
 * Key design notes
 * ────────────────
 * • All large buffers are static globals to avoid stack overflow on M4.
 * • Filters are CAUSAL (one pass forward), matching real-time constraints.
 *   Training used offline zero-phase filtering; causal real-time filters give a
 *   small accuracy difference which is accounted for in the gain calibration.
 * • Jerk signals (127 samples) are zero-padded to 128 for the FFT — a
 *   negligible approximation (one trailing zero).
 * • The 128-pt FFT is a self-contained iterative radix-2 DIT implementation
 *   (no CMSIS-DSP required).
 */

#include "par_pipeline.h"
#include "par_wrapper.h"
#include "par_features.h"   /* PAR_FEAT_MEAN[], PAR_FEAT_STD[], PAR_N_FEATURES */

#include <math.h>
#include <string.h>
#include <stdint.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * Constants
 * ═══════════════════════════════════════════════════════════════════════════ */

#define WIN_LEN      128
#define HOP_LEN       64   /* 50 % overlap */
#define N_ALL_FEATS  284
#define N_SEL_FEATS  100

#define ACCEL_SENS  16384.0f   /* LSB/g    (±2 g  full scale) */
#define GYRO_SENS     131.0f   /* LSB/°/s  (±250 °/s full scale) */
#define FS             50.0f   /* sampling frequency, Hz */

/* ═══════════════════════════════════════════════════════════════════════════
 * Biquad filter state
 *   state[i] = { x[n-1], x[n-2], y[n-1], y[n-2] }
 *   Coefficients: [b0, b1, b2, -a1, -a2]  (CMSIS-DSP DF1 format)
 *   Recurrence:  y = c[0]*x + c[1]*x1 + c[2]*x2 + c[3]*y1 + c[4]*y2
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Noise LPF  18 Hz 2nd-order Butterworth */
static const float NLPF[5] = {
    0.5299672271f, 1.0599344541f, 0.5299672271f, -0.8252323807f, -0.2946365276f
};
/* Gravity LPF  0.3 Hz 2nd-order Butterworth */
static const float GLPF[5] = {
    0.0003460413f, 0.0006920827f, 0.0003460413f,  1.9466975408f, -0.9480817061f
};

/* 6 channels of noise LPF: acc_x, acc_y, acc_z, gyro_x, gyro_y, gyro_z */
static float noise_st[6][4];   /* {x1, x2, y1, y2} */
/* 3 channels of gravity LPF: acc_x, acc_y, acc_z */
static float grav_st[3][4];

static inline float biquad(const float *c, float *st, float x)
{
    float y = c[0]*x + c[1]*st[0] + c[2]*st[1] + c[3]*st[2] + c[4]*st[3];
    st[1] = st[0];  st[0] = x;
    st[3] = st[2];  st[2] = y;
    return y;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Circular buffer — stores the last WIN_LEN samples of 9 signals:
 *   [0] body_acc_x   [1] body_acc_y   [2] body_acc_z
 *   [3] gravity_x    [4] gravity_y    [5] gravity_z
 *   [6] gyro_x       [7] gyro_y       [8] gyro_z
 * ═══════════════════════════════════════════════════════════════════════════ */

static float   circ[WIN_LEN][9];
static int     circ_head;     /* write index (next slot) */
static uint32_t sample_cnt;

/* ═══════════════════════════════════════════════════════════════════════════
 * Majority-vote result buffer
 * ═══════════════════════════════════════════════════════════════════════════ */

#define VOTE_LEN  5
static int   vote_buf[VOTE_LEN];
static int   vote_pos;
static int   vote_filled;
static int   last_activity  = -1;
static float last_confidence = 0.0f;

/* ═══════════════════════════════════════════════════════════════════════════
 * Feature index lookup table
 *   Maps selected-feature slot (0..99) → all-feature index (0..283).
 *   Selected by RF feature importance on training data.
 *   Full feature order (284 total) — 5 time-domain groups (40 each)
 *   and 3 frequency-domain groups (28 each):
 *     body_acc[0-39], gravity_acc[40-79], body_gyro[80-119],
 *     body_acc_jerk[120-159], body_gyro_jerk[160-199],
 *     freq_body_acc[200-227], freq_body_gyro[228-255], freq_body_acc_jerk[256-283]
 *   Each TD group: x/y/z/mag * (mean,std,min,max,rms,mad,iqr,energy,zcr) + sma + 3 corr
 *   Each FD group: x/y/z/mag * (dc,peak_freq,mean_freq,entropy,3 band energies)
 * ═══════════════════════════════════════════════════════════════════════════ */

static const uint16_t FEAT_IDX[N_SEL_FEATS] = {
     49,  44,  52,  40,  42,  51,  43,  56,  53,  47,
    123, 260,   1,   5,   7,  67,  74,  71,   3,  69,
    124,  28,   4, 127, 125, 216,  70, 204, 121,  32,
     31, 225, 221,  34,  88, 282,  27,  30,  24, 257,
     60, 152, 153, 276, 281, 154, 196,  36,  58,  61,
    228, 268, 134, 232, 277, 126,  94, 130, 212, 267,
    151, 168,   6,  65, 272,  62,  37, 220, 187, 148,
     76, 262, 271,  10, 206, 136, 175,  87, 156,  82,
    210, 251, 230, 184, 165,  83, 183, 147, 213, 185,
     84,  11,  22, 118, 164, 236, 203, 135,  17, 227,
};

static const char *CLASS_NAMES[PAR_NUM_CLASSES] = {
    "STATIONARY", "WALKING", "CLIMBING", "LAYING"
};

/* ═══════════════════════════════════════════════════════════════════════════
 * Working buffers (static → lives in .bss, not stack)
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Extracted window in chronological order */
static float win[WIN_LEN][9];

/* Jerk signals (first difference × FS), length WIN_LEN-1 = 127 */
static float jerk_acc[WIN_LEN - 1][3];   /* body_acc jerk */
static float jerk_gyr[WIN_LEN - 1][3];   /* gyro jerk     */

/* Full 284-feature vector */
static float all_feats[N_ALL_FEATS];

/* Selected + normalised 100-feature vector passed to RF */
static float sel_feats[N_SEL_FEATS];

/* Scratch buffer for insertion-sort IQR (128 floats) */
static float sort_buf[WIN_LEN];

/* FFT buffers — one pair of real/imag arrays (reused per axis) */
static float fft_re[WIN_LEN];
static float fft_im[WIN_LEN];

/* ═══════════════════════════════════════════════════════════════════════════
 * 128-point radix-2 DIT FFT (in-place, complex)
 * After calling fft128(re, im), re[k]+j*im[k] are the DFT coefficients.
 * For a real input set im[] = 0 before calling.
 * ═══════════════════════════════════════════════════════════════════════════ */

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

static void fft128(float *re, float *im)
{
    /* Bit-reversal permutation */
    int j = 0;
    for (int i = 1; i < WIN_LEN; i++) {
        int bit = WIN_LEN >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float t;
            t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }

    /* Cooley-Tukey butterfly stages (log2(128) = 7) */
    for (int s = 1; s <= 7; s++) {
        int m  = 1 << s;
        int m2 = m >> 1;
        float ang = -(float)M_PI / (float)m2;   /* -π/m2 = -2π/m */
        float wRe = cosf(ang), wIm = sinf(ang);
        for (int k = 0; k < WIN_LEN; k += m) {
            float tRe = 1.0f, tIm = 0.0f;
            for (int jj = 0; jj < m2; jj++) {
                float uRe = re[k + jj],       uIm = im[k + jj];
                float vRe = tRe * re[k+jj+m2] - tIm * im[k+jj+m2];
                float vIm = tRe * im[k+jj+m2] + tIm * re[k+jj+m2];
                re[k + jj]      = uRe + vRe;
                im[k + jj]      = uIm + vIm;
                re[k + jj + m2] = uRe - vRe;
                im[k + jj + m2] = uIm - vIm;
                float nTRe = tRe * wRe - tIm * wIm;
                float nTIm = tRe * wIm + tIm * wRe;
                tRe = nTRe; tIm = nTIm;
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Feature computation helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * Insertion sort on src[0..n-1] into sort_buf[].
 * Used only for the IQR feature (128 elements → fast enough at 180 MHz).
 */
static void isort(const float *src, int n)
{
    memcpy(sort_buf, src, (size_t)n * sizeof(float));
    for (int i = 1; i < n; i++) {
        float key = sort_buf[i];
        int   k   = i - 1;
        while (k >= 0 && sort_buf[k] > key) {
            sort_buf[k + 1] = sort_buf[k];
            k--;
        }
        sort_buf[k + 1] = key;
    }
}

/*
 * Compute the 9 per-axis time-domain features into feat_out[0..8].
 *   mean, std, min, max, rms, mad, iqr, energy, zcr
 */
static void td_axis(float *feat_out, const float *sig, int n)
{
    float sum = 0.0f, sum_sq = 0.0f;
    float mn  = sig[0], mx = sig[0];
    for (int i = 0; i < n; i++) {
        float v = sig[i];
        sum    += v;
        sum_sq += v * v;
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    float mean   = sum    / (float)n;
    float energy = sum_sq / (float)n;              /* mean-squared = energy */
    float var    = energy - mean * mean;
    float std    = (var > 0.0f) ? sqrtf(var) : 0.0f;
    float rms    = sqrtf(energy);

    /* MAD */
    float mad_s = 0.0f;
    for (int i = 0; i < n; i++) mad_s += fabsf(sig[i] - mean);
    float mad = mad_s / (float)n;

    /* IQR via insertion sort */
    isort(sig, n);
    float iqr = sort_buf[3 * n / 4] - sort_buf[n / 4];

    /* ZCR — matches numpy: count sign transitions / len(sig)
     * np.sign(0)=0, so a transition 0→positive also counts */
    int zcr_cnt = 0;
    for (int i = 1; i < n; i++) {
        float sa = sig[i - 1], sb = sig[i];
        int   sa_pos = (sa > 0.0f) ? 1 : ((sa < 0.0f) ? -1 : 0);
        int   sb_pos = (sb > 0.0f) ? 1 : ((sb < 0.0f) ? -1 : 0);
        if (sa_pos != sb_pos) zcr_cnt++;
    }
    float zcr = (float)zcr_cnt / (float)n;

    feat_out[0] = mean;
    feat_out[1] = std;
    feat_out[2] = mn;
    feat_out[3] = mx;
    feat_out[4] = rms;
    feat_out[5] = mad;
    feat_out[6] = iqr;
    feat_out[7] = energy;
    feat_out[8] = zcr;
}

/*
 * Compute 40 time-domain features for one 3-axis group into feat_out[0..39].
 * sig_col[3]: pointers to x[], y[], z[] arrays of length n.
 *
 *   Indices 0–8:   x features
 *   Indices 9–17:  y features
 *   Indices 18–26: z features
 *   Indices 27–35: magnitude features
 *   Index  36:     SMA
 *   Index  37:     corr_xy
 *   Index  38:     corr_xz
 *   Index  39:     corr_yz
 */
static void td_3axis(float *feat_out,
                     const float *sx, const float *sy, const float *sz,
                     int n)
{
    /* per-axis features */
    td_axis(feat_out +  0, sx, n);
    td_axis(feat_out +  9, sy, n);
    td_axis(feat_out + 18, sz, n);

    /* magnitude */
    float mag_buf[WIN_LEN];
    for (int i = 0; i < n; i++) {
        mag_buf[i] = sqrtf(sx[i]*sx[i] + sy[i]*sy[i] + sz[i]*sz[i]);
    }
    td_axis(feat_out + 27, mag_buf, n);

    /* SMA — signal magnitude area */
    float sma = 0.0f;
    for (int i = 0; i < n; i++)
        sma += fabsf(sx[i]) + fabsf(sy[i]) + fabsf(sz[i]);
    feat_out[36] = sma / (float)n;

    /* Axis correlations (Pearson) */
    float mx = 0.0f, my = 0.0f, mz = 0.0f;
    for (int i = 0; i < n; i++) { mx += sx[i]; my += sy[i]; mz += sz[i]; }
    mx /= n; my /= n; mz /= n;

    float cxy = 0.0f, cxz = 0.0f, cyz = 0.0f;
    float vx  = 0.0f, vy  = 0.0f, vz  = 0.0f;
    for (int i = 0; i < n; i++) {
        float dx = sx[i] - mx, dy = sy[i] - my, dz = sz[i] - mz;
        cxy += dx * dy;   cxz += dx * dz;   cyz += dy * dz;
        vx  += dx * dx;   vy  += dy * dy;   vz  += dz * dz;
    }
    feat_out[37] = (vx > 0.0f && vy > 0.0f) ? cxy / sqrtf(vx * vy) : 0.0f;
    feat_out[38] = (vx > 0.0f && vz > 0.0f) ? cxz / sqrtf(vx * vz) : 0.0f;
    feat_out[39] = (vy > 0.0f && vz > 0.0f) ? cyz / sqrtf(vy * vz) : 0.0f;
}

/*
 * Compute 7 frequency-domain features for one axis into feat_out[0..6].
 * Input sig[n] is zero-padded to WIN_LEN=128 if n < 128.
 *
 * Features:
 *   [0] DC component  (fft_mag_norm[0] = fft_mag[0] / (n/2))
 *   [1] peak_freq     (frequency of highest bin, excluding DC)
 *   [2] mean_freq     (spectral centroid, excluding DC)
 *   [3] entropy       (spectral entropy of PSD, excluding DC)
 *   [4] band energy   [0, 5) Hz
 *   [5] band energy   [5, 10) Hz
 *   [6] band energy   [10, 18) Hz
 *
 * Band boundaries for 128-pt FFT at 50 Hz (bin spacing 50/128 ≈ 0.3906 Hz):
 *   [0,5)  Hz : bins 0–12
 *   [5,10) Hz : bins 13–25
 *   [10,18)Hz : bins 26–46
 */
static void fd_axis(float *feat_out, const float *sig, int n)
{
    /* Load into FFT buffer, zero-pad to 128 */
    for (int i = 0; i < n; i++)      { fft_re[i] = sig[i]; fft_im[i] = 0.0f; }
    for (int i = n; i < WIN_LEN; i++) { fft_re[i] = 0.0f;  fft_im[i] = 0.0f; }

    fft128(fft_re, fft_im);

    /* Magnitude for positive-frequency bins [0..64] */
    float mag[WIN_LEN / 2 + 1];   /* 65 bins */
    for (int k = 0; k <= WIN_LEN / 2; k++) {
        mag[k] = sqrtf(fft_re[k]*fft_re[k] + fft_im[k]*fft_im[k]);
    }

    /* DC (normalised by n/2 to match Python fft_mag_norm = fft_mag / (n/2)) */
    float half_n = (float)n * 0.5f;
    feat_out[0] = mag[0] / half_n;

    /* Frequency axis: freq[k] = k * FS / WIN_LEN  for k=0..64 */
    /* Peak frequency (skip bin 0) */
    int   peak_k   = 1;
    float peak_mag = mag[1];
    for (int k = 2; k <= WIN_LEN / 2; k++) {
        if (mag[k] > peak_mag) { peak_mag = mag[k]; peak_k = k; }
    }
    feat_out[1] = (float)peak_k * FS / (float)WIN_LEN;

    /* Spectral centroid (weighted mean frequency, excluding DC) */
    float total_pow = 0.0f, weighted_f = 0.0f;
    for (int k = 1; k <= WIN_LEN / 2; k++) {
        float f = (float)k * FS / (float)WIN_LEN;
        total_pow  += mag[k];
        weighted_f += f * mag[k];
    }
    feat_out[2] = (total_pow > 1e-12f) ? weighted_f / total_pow : 0.0f;

    /* Spectral entropy (PSD = mag² / sum(mag²)) */
    float psd_sum = 0.0f;
    for (int k = 1; k <= WIN_LEN / 2; k++) psd_sum += mag[k] * mag[k];
    float entropy = 0.0f;
    if (psd_sum > 1e-12f) {
        for (int k = 1; k <= WIN_LEN / 2; k++) {
            float p = (mag[k] * mag[k]) / psd_sum;
            if (p > 1e-12f) entropy -= p * log2f(p);
        }
    }
    feat_out[3] = entropy;

    /* Band energies: sum of mag[k]² for each band
     * Band [0,5)  Hz → bins 0..12  (12 * 50/128 = 4.69 Hz < 5)
     * Band [5,10) Hz → bins 13..25 (25 * 50/128 = 9.77 Hz < 10)
     * Band [10,18)Hz → bins 26..46 (46 * 50/128 = 17.97 Hz < 18) */
    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
    for (int k = 0;  k <= 12; k++) b0 += mag[k] * mag[k];
    for (int k = 13; k <= 25; k++) b1 += mag[k] * mag[k];
    for (int k = 26; k <= 46; k++) b2 += mag[k] * mag[k];
    feat_out[4] = b0;
    feat_out[5] = b1;
    feat_out[6] = b2;
}

/*
 * Compute 28 frequency-domain features for a 3-axis group into feat_out[0..27].
 *   [0..6]   x features
 *   [7..13]  y features
 *   [14..20] z features
 *   [21..27] magnitude features
 */
static void fd_3axis(float *feat_out,
                     const float *sx, const float *sy, const float *sz,
                     int n)
{
    fd_axis(feat_out +  0, sx, n);
    fd_axis(feat_out +  7, sy, n);
    fd_axis(feat_out + 14, sz, n);

    /* Magnitude */
    float mag_buf[WIN_LEN];
    for (int i = 0; i < n; i++)
        mag_buf[i] = sqrtf(sx[i]*sx[i] + sy[i]*sy[i] + sz[i]*sz[i]);
    fd_axis(feat_out + 21, mag_buf, n);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Column accessors — extract one signal column from the 2D win[][] array
 * ═══════════════════════════════════════════════════════════════════════════ */

static float col_buf[WIN_LEN];   /* scratch for column extraction */

static const float *col(int c, int n)
{
    for (int i = 0; i < n; i++) col_buf[i] = win[i][c];
    return col_buf;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Compute all 284 features from win[][] and jerk buffers
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Scratch column buffers for three axes */
static float sx_buf[WIN_LEN], sy_buf[WIN_LEN], sz_buf[WIN_LEN];

static void compute_features(void)
{
    int n128 = WIN_LEN;
    int n127 = WIN_LEN - 1;
    float *f = all_feats;

    /* Helper macro: extract column c from win[][] into buf */
#define EXTRACT_COL(buf, c, n)  do { \
    for (int _i = 0; _i < (n); _i++) (buf)[_i] = win[_i][(c)]; \
} while (0)

    /* ── body_acc features (indices 0–39) ── */
    EXTRACT_COL(sx_buf, 0, n128);
    EXTRACT_COL(sy_buf, 1, n128);
    EXTRACT_COL(sz_buf, 2, n128);
    td_3axis(f + 0, sx_buf, sy_buf, sz_buf, n128);

    /* ── gravity_acc features (indices 40–79) ── */
    EXTRACT_COL(sx_buf, 3, n128);
    EXTRACT_COL(sy_buf, 4, n128);
    EXTRACT_COL(sz_buf, 5, n128);
    td_3axis(f + 40, sx_buf, sy_buf, sz_buf, n128);

    /* ── body_gyro features (indices 80–119) ── */
    EXTRACT_COL(sx_buf, 6, n128);
    EXTRACT_COL(sy_buf, 7, n128);
    EXTRACT_COL(sz_buf, 8, n128);
    td_3axis(f + 80, sx_buf, sy_buf, sz_buf, n128);

    /* ── body_acc_jerk features (indices 120–159) ── */
    for (int i = 0; i < n127; i++) {
        sx_buf[i] = jerk_acc[i][0];
        sy_buf[i] = jerk_acc[i][1];
        sz_buf[i] = jerk_acc[i][2];
    }
    td_3axis(f + 120, sx_buf, sy_buf, sz_buf, n127);

    /* ── body_gyro_jerk features (indices 160–199) ── */
    for (int i = 0; i < n127; i++) {
        sx_buf[i] = jerk_gyr[i][0];
        sy_buf[i] = jerk_gyr[i][1];
        sz_buf[i] = jerk_gyr[i][2];
    }
    td_3axis(f + 160, sx_buf, sy_buf, sz_buf, n127);

    /* ── freq_body_acc (indices 200–227) ── */
    EXTRACT_COL(sx_buf, 0, n128);
    EXTRACT_COL(sy_buf, 1, n128);
    EXTRACT_COL(sz_buf, 2, n128);
    fd_3axis(f + 200, sx_buf, sy_buf, sz_buf, n128);

    /* ── freq_body_gyro (indices 228–255) ── */
    EXTRACT_COL(sx_buf, 6, n128);
    EXTRACT_COL(sy_buf, 7, n128);
    EXTRACT_COL(sz_buf, 8, n128);
    fd_3axis(f + 228, sx_buf, sy_buf, sz_buf, n128);

    /* ── freq_body_accJerk (indices 256–283) ── */
    for (int i = 0; i < n127; i++) {
        sx_buf[i] = jerk_acc[i][0];
        sy_buf[i] = jerk_acc[i][1];
        sz_buf[i] = jerk_acc[i][2];
    }
    fd_3axis(f + 256, sx_buf, sy_buf, sz_buf, n127);   /* zero-pads to 128 */

#undef EXTRACT_COL
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Inference on one window
 * ═══════════════════════════════════════════════════════════════════════════ */

static void run_inference(void)
{
    compute_features();

    /* Select 100 features and normalise */
    for (int i = 0; i < N_SEL_FEATS; i++) {
        float v = all_feats[FEAT_IDX[i]];
        sel_feats[i] = (v - PAR_FEAT_MEAN[i]) / PAR_FEAT_STD[i];
    }

    int cls = par_predict(sel_feats);

    /* Majority vote over last VOTE_LEN predictions */
    vote_buf[vote_pos] = cls;
    vote_pos = (vote_pos + 1) % VOTE_LEN;
    if (vote_filled < VOTE_LEN) vote_filled++;

    int votes[PAR_NUM_CLASSES] = {0};
    for (int i = 0; i < vote_filled; i++) votes[vote_buf[i]]++;

    int best = 0;
    for (int c = 1; c < PAR_NUM_CLASSES; c++) {
        if (votes[c] > votes[best]) best = c;
    }

    last_activity   = best;
    last_confidence = (float)votes[best] / (float)vote_filled;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public API
 * ═══════════════════════════════════════════════════════════════════════════ */

void PAR_Init(void)
{
    memset(noise_st,   0, sizeof(noise_st));
    memset(grav_st,    0, sizeof(grav_st));
    memset(circ,       0, sizeof(circ));
    memset(vote_buf,   0, sizeof(vote_buf));
    circ_head      = 0;
    sample_cnt     = 0;
    vote_pos       = 0;
    vote_filled    = 0;
    last_activity  = -1;
    last_confidence = 0.0f;
}

void PAR_PushSample(int16_t ax_raw, int16_t ay_raw, int16_t az_raw,
                    int16_t gx_raw, int16_t gy_raw, int16_t gz_raw)
{
    /* 1. Convert to physical units */
    float ax_g  = (float)ax_raw / ACCEL_SENS;
    float ay_g  = (float)ay_raw / ACCEL_SENS;
    float az_g  = (float)az_raw / ACCEL_SENS;
    float gx_d  = (float)gx_raw / GYRO_SENS;
    float gy_d  = (float)gy_raw / GYRO_SENS;
    float gz_d  = (float)gz_raw / GYRO_SENS;

    /* 2. Noise LPF on all 6 channels */
    float nax = biquad(NLPF, noise_st[0], ax_g);
    float nay = biquad(NLPF, noise_st[1], ay_g);
    float naz = biquad(NLPF, noise_st[2], az_g);
    float ngx = biquad(NLPF, noise_st[3], gx_d);
    float ngy = biquad(NLPF, noise_st[4], gy_d);
    float ngz = biquad(NLPF, noise_st[5], gz_d);

    /* 3. Gravity LPF (applied to noise-filtered acc) */
    float gx = biquad(GLPF, grav_st[0], nax);
    float gy = biquad(GLPF, grav_st[1], nay);
    float gz = biquad(GLPF, grav_st[2], naz);

    /* 4. Body acceleration = total(noise-filtered) − gravity */
    float bax = nax - gx;
    float bay = nay - gy;
    float baz = naz - gz;

    /* 5. Store in circular buffer */
    circ[circ_head][0] = bax;
    circ[circ_head][1] = bay;
    circ[circ_head][2] = baz;
    circ[circ_head][3] = gx;
    circ[circ_head][4] = gy;
    circ[circ_head][5] = gz;
    circ[circ_head][6] = ngx;
    circ[circ_head][7] = ngy;
    circ[circ_head][8] = ngz;
    circ_head = (circ_head + 1) % WIN_LEN;
    sample_cnt++;

    /* 6. Trigger at 128 samples and every 64 thereafter (50 % overlap) */
    if (sample_cnt < WIN_LEN) return;
    if ((sample_cnt - WIN_LEN) % HOP_LEN != 0) return;

    /* 7. Extract window in chronological order
     *    circ_head now points to the oldest sample */
    for (int i = 0; i < WIN_LEN; i++) {
        int idx = (circ_head + i) % WIN_LEN;
        memcpy(win[i], circ[idx], 9 * sizeof(float));
    }

    /* 8. Compute jerk (first difference × FS) */
    for (int i = 0; i < WIN_LEN - 1; i++) {
        jerk_acc[i][0] = (win[i+1][0] - win[i][0]) * FS;
        jerk_acc[i][1] = (win[i+1][1] - win[i][1]) * FS;
        jerk_acc[i][2] = (win[i+1][2] - win[i][2]) * FS;
        jerk_gyr[i][0] = (win[i+1][6] - win[i][6]) * FS;
        jerk_gyr[i][1] = (win[i+1][7] - win[i][7]) * FS;
        jerk_gyr[i][2] = (win[i+1][8] - win[i][8]) * FS;
    }

    /* 9. Feature extraction + inference */
    run_inference();
}

int PAR_GetActivity(void)
{
    return last_activity;
}

const char *PAR_GetActivityName(int class_idx)
{
    if (class_idx < 0 || class_idx >= PAR_NUM_CLASSES) return "UNKNOWN";
    return CLASS_NAMES[class_idx];
}

float PAR_GetConfidence(void)
{
    return last_confidence;
}
