/**
 * OUCS Engine - Audio Analysis
 * oucs_analysis.c
 *
 * Implements:
 *   - BPM detection (autocorrelation on onset strength)
 *   - Musical key detection (chromagram via DFT)
 *   - Mood tagging (energy/valence heuristic)
 *   - Waveform pre-computation (RMS per window)
 *   - Acoustic fingerprinting (spectral hash)
 *
 * Uses only C standard library (no external deps).
 * Input: raw PCM f32 samples.
 *
 * License: MIT
 */

#include "oucs_internal.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ─────────────────────────────────────────────────────────────
   PCM HELPERS
   For MP3/FLAC inputs the caller should decode first.
   We provide a raw PCM analysis API.
───────────────────────────────────────────────────────────── */

/* ─────────────────────────────────────────────────────────────
   WAVEFORM PRE-COMPUTATION
   Computes RMS amplitude for each 1024-sample window.
───────────────────────────────────────────────────────────── */

/**
 * Compute waveform (RMS per window) from raw f32 PCM.
 * @param pcm         Float32 mono PCM samples
 * @param num_samples Total sample count
 * @param window_size Samples per RMS window (default 1024)
 * @param out         Output waveform struct (caller frees via oucs_waveform_free)
 */
int oucs_analysis_waveform(const float *pcm, size_t num_samples,
                             uint32_t window_size, OucsWaveform *out) {
    if (!pcm || !out || num_samples == 0) return OUCS_ERR_NULL_PARAM;
    if (window_size == 0) window_size = 1024;

    uint64_t num_windows = (num_samples + window_size - 1) / window_size;
    out->samples = (float *)malloc((size_t)num_windows * sizeof(float));
    if (!out->samples) return OUCS_ERR_NOMEM;
    out->count = num_windows;

    for (uint64_t w = 0; w < num_windows; w++) {
        size_t start = (size_t)(w * window_size);
        size_t end   = start + window_size;
        if (end > num_samples) end = num_samples;
        double sum = 0.0;
        for (size_t i = start; i < end; i++) sum += (double)pcm[i] * (double)pcm[i];
        out->samples[w] = (float)sqrt(sum / (double)(end - start));
    }
    return OUCS_OK;
}

/* ─────────────────────────────────────────────────────────────
   BPM DETECTION (Autocorrelation)
───────────────────────────────────────────────────────────── */

/**
 * Detect BPM from raw f32 PCM.
 * Algorithm:
 *   1. Compute onset strength envelope (energy difference per hop)
 *   2. Autocorrelate the envelope
 *   3. Find peak in range [40BPM, 240BPM]
 *
 * @param pcm         Mono float32 PCM
 * @param num_samples Sample count
 * @param sample_rate Hz (e.g. 44100)
 * @param bpm_out     Detected BPM
 */
int oucs_analysis_bpm(const float *pcm, size_t num_samples,
                       uint32_t sample_rate, float *bpm_out) {
    if (!pcm || !bpm_out || num_samples < 2048) return OUCS_ERR_INVALID_ARG;

    const uint32_t hop = 512;
    size_t num_hops = num_samples / hop;

    /* Step 1: onset strength = max(0, E[n] - E[n-1]) */
    float *onset = (float *)calloc(num_hops, sizeof(float));
    if (!onset) return OUCS_ERR_NOMEM;

    float prev_energy = 0.0f;
    for (size_t h = 0; h < num_hops; h++) {
        float energy = 0.0f;
        for (uint32_t i = 0; i < hop; i++) {
            float s = pcm[h * hop + i];
            energy += s * s;
        }
        energy /= (float)hop;
        float diff = energy - prev_energy;
        onset[h] = diff > 0.0f ? diff : 0.0f;
        prev_energy = energy;
    }

    /* Step 2: Autocorrelate onset strength */
    /* BPM range: 40-240 BPM → period in hops:
       period = (sample_rate / hop) * (60 / BPM) */
    float fps = (float)sample_rate / (float)hop;
    uint32_t min_period = (uint32_t)(fps * 60.0f / 240.0f);
    uint32_t max_period = (uint32_t)(fps * 60.0f / 40.0f);
    if (max_period >= num_hops) max_period = (uint32_t)num_hops - 1;

    float best_corr = -1.0f;
    uint32_t best_period = min_period;

    for (uint32_t lag = min_period; lag <= max_period; lag++) {
        float corr = 0.0f;
        size_t count = 0;
        for (size_t i = 0; i + lag < num_hops; i++) {
            corr += onset[i] * onset[i + lag];
            count++;
        }
        if (count > 0) corr /= (float)count;
        if (corr > best_corr) {
            best_corr   = corr;
            best_period = lag;
        }
    }
    free(onset);

    *bpm_out = fps * 60.0f / (float)best_period;
    return OUCS_OK;
}

/* ─────────────────────────────────────────────────────────────
   MUSICAL KEY DETECTION (Chromagram)
───────────────────────────────────────────────────────────── */

/**
 * Key profiles for Krumhansl-Schmuckler algorithm
 * (major and minor key profiles)
 */
static const float key_profile_major[12] = {
    6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f,
    2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f
};
static const float key_profile_minor[12] = {
    6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f,
    2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f
};

/**
 * DFT-based chromagram: compute energy in 12 pitch classes.
 */
static void compute_chromagram(const float *pcm, size_t num_samples,
                                uint32_t sample_rate, float *chroma) {
    memset(chroma, 0, 12 * sizeof(float));
    /* For each of 12 pitch classes (C=0 to B=11),
       compute energy at that frequency using a simplified DFT */
    for (int p = 0; p < 12; p++) {
        /* Middle octave reference: C4=261.63 Hz */
        float freq = 261.63f * powf(2.0f, (float)p / 12.0f);
        double cos_sum = 0.0, sin_sum = 0.0;
        size_t step = sample_rate / 2000 + 1; /* downsample for speed */
        size_t count = 0;
        for (size_t i = 0; i < num_samples; i += step) {
            double t = (double)i / (double)sample_rate;
            double angle = 2.0 * M_PI * (double)freq * t;
            cos_sum += (double)pcm[i] * cos(angle);
            sin_sum += (double)pcm[i] * sin(angle);
            count++;
        }
        chroma[p] = (float)(sqrt(cos_sum*cos_sum + sin_sum*sin_sum) / (double)count);
    }
}

/**
 * Detect musical key.
 * Returns: OucsMusicalKey (0-11) for C-B, plus sets *is_minor.
 */
int oucs_analysis_key(const float *pcm, size_t num_samples,
                       uint32_t sample_rate, uint8_t *key_out, int *is_minor_out) {
    if (!pcm || !key_out) return OUCS_ERR_INVALID_ARG;

    float chroma[12];
    compute_chromagram(pcm, num_samples, sample_rate, chroma);

    /* Normalize */
    float sum = 0.0f;
    for (int i = 0; i < 12; i++) sum += chroma[i];
    if (sum > 0.0f) for (int i = 0; i < 12; i++) chroma[i] /= sum;

    float best_score = -1e30f;
    int   best_key   = 0;
    int   best_minor = 0;

    /* Try all 12 rotations for major and minor */
    for (int root = 0; root < 12; root++) {
        float sm = 0.0f, sn = 0.0f;
        for (int i = 0; i < 12; i++) {
            sm += chroma[(i + root) % 12] * key_profile_major[i];
            sn += chroma[(i + root) % 12] * key_profile_minor[i];
        }
        if (sm > best_score) { best_score = sm; best_key = root; best_minor = 0; }
        if (sn > best_score) { best_score = sn; best_key = root; best_minor = 1; }
    }

    *key_out = (uint8_t)best_key;
    if (is_minor_out) *is_minor_out = best_minor;
    return OUCS_OK;
}

/* ─────────────────────────────────────────────────────────────
   MOOD DETECTION
───────────────────────────────────────────────────────────── */

/**
 * Compute mood flags from energy and estimated tempo.
 * Simple heuristic:
 *   - High energy + fast tempo → ENERGETIC + HAPPY
 *   - Low energy + slow tempo  → CALM or SAD
 *   - High energy + minor key  → ANGRY
 */
uint8_t oucs_analysis_mood(float bpm, float avg_rms, int is_minor) {
    uint8_t mood = 0;
    int fast   = bpm > 120.0f;
    int loud   = avg_rms > 0.15f;

    if (fast && loud && !is_minor) mood |= OUCS_MOOD_ENERGETIC | OUCS_MOOD_HAPPY;
    else if (fast && loud && is_minor) mood |= OUCS_MOOD_ENERGETIC | OUCS_MOOD_ANGRY;
    else if (!fast && !loud && is_minor) mood |= OUCS_MOOD_SAD | OUCS_MOOD_CALM;
    else if (!fast && !loud) mood |= OUCS_MOOD_CALM;
    else if (!fast && is_minor) mood |= OUCS_MOOD_ROMANTIC | OUCS_MOOD_SAD;
    else mood |= OUCS_MOOD_HAPPY;

    return mood;
}

/* ─────────────────────────────────────────────────────────────
   ACOUSTIC FINGERPRINT
   A simplified spectral hash fingerprint.
   (Chromaprint integration available via OUCS_USE_CHROMAPRINT)
───────────────────────────────────────────────────────────── */

#define FP_BANDS        32
#define FP_FRAMES       64
#define FP_FRAME_STEP   1024

/**
 * Compute a 256-byte spectral hash fingerprint from raw f32 PCM.
 * The hash is reproducible for the same audio, order-invariant to
 * small perturbations (within ~2 semitones).
 */
int oucs_analysis_fingerprint(const float *pcm, size_t num_samples,
                                uint32_t sample_rate,
                                OucsFingerprint *fp_out) {
    if (!pcm || !fp_out || num_samples < FP_FRAME_STEP) return OUCS_ERR_INVALID_ARG;

    fp_out->size = FP_BANDS * FP_FRAMES / 8; /* pack bits */
    fp_out->data = (uint8_t *)calloc(1, fp_out->size);
    if (!fp_out->data) return OUCS_ERR_NOMEM;

    float band_energies[FP_FRAMES][FP_BANDS];
    size_t step = (num_samples - FP_FRAME_STEP) / (FP_FRAMES - 1);
    if (step == 0) step = 1;

    for (int f = 0; f < FP_FRAMES; f++) {
        size_t start = (size_t)(f * step);
        if (start + FP_FRAME_STEP > num_samples) start = num_samples - FP_FRAME_STEP;

        /* Compute DFT at FP_BANDS log-spaced frequencies */
        for (int b = 0; b < FP_BANDS; b++) {
            float freq = 100.0f * powf(2.0f, (float)b / (float)FP_BANDS * 4.0f);
            double cos_s = 0, sin_s = 0;
            size_t sub = FP_FRAME_STEP / 8;
            for (size_t i = start; i < start + FP_FRAME_STEP; i += sub) {
                double t = (double)(i - start) / (double)sample_rate;
                double a = 2.0 * M_PI * (double)freq * t;
                cos_s += (double)pcm[i] * cos(a);
                sin_s += (double)pcm[i] * sin(a);
            }
            band_energies[f][b] = (float)(cos_s * cos_s + sin_s * sin_s);
        }
    }

    /* Difference fingerprint: bit = 1 if energy[f][b] > energy[f][b+1] */
    size_t bit = 0;
    for (int f = 0; f < FP_FRAMES; f++) {
        for (int b = 0; b < FP_BANDS - 1; b++) {
            if (band_energies[f][b] > band_energies[f][b + 1]) {
                fp_out->data[bit / 8] |= (1 << (bit % 8));
            }
            bit++;
            if (bit >= (size_t)(fp_out->size * 8)) goto done;
        }
    }
done:
    return OUCS_OK;
}

/**
 * Compare two fingerprints. Returns similarity score 0.0-1.0.
 * Score > 0.85 typically means same song.
 */
float oucs_fingerprint_similarity(const OucsFingerprint *a, const OucsFingerprint *b) {
    if (!a || !b || !a->data || !b->data) return 0.0f;
    uint32_t min_size = a->size < b->size ? a->size : b->size;
    int matches = 0, total = 0;
    for (uint32_t i = 0; i < min_size; i++) {
        uint8_t diff = a->data[i] ^ b->data[i];
        /* Count set bits (Brian Kernighan method — portable, no builtins) */
        int set_bits = 0;
        uint8_t tmp = diff;
        while (tmp) { set_bits += tmp & 1; tmp >>= 1; }
        matches += 8 - set_bits;
        total   += 8;
    }
    return total > 0 ? (float)matches / (float)total : 0.0f;
}
