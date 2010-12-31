/* 
 *   DTMF Receiver module, part of:
 *      BSD Telephony Of Mexico "Zapata" Telecom Library, version 1.10  12/9/01
 *
 *   Part of the "Zapata" Computer Telephony Technology.
 *
 *   See http://www.bsdtelephony.com.mx
 *
 *
 *  The technologies, software, hardware, designs, drawings, scheumatics, board
 *  layouts and/or artwork, concepts, methodologies (including the use of all
 *  of these, and that which is derived from the use of all of these), all other
 *  intellectual properties contained herein, and all intellectual property
 *  rights have been and shall continue to be expressly for the benefit of all
 *  mankind, and are perpetually placed in the public domain, and may be used,
 *  copied, and/or modified by anyone, in any manner, for any legal purpose,
 *  without restriction.
 *
 *   This module written by Stephen Underwood.
 */

/*
	tone_detect.c - General telephony tone detection, and specific
                        detection of DTMF.

        Copyright (C) 2001  Steve Underwood <steveu@coppice.org>

        Despite my general liking of the GPL, I place this code in the
        public domain for the benefit of all mankind - even the slimy
        ones who might try to proprietize my work and use it to my
        detriment.
*/

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <fcntl.h>
#include "tone_detect.h"

#ifndef FALSE
#define FALSE   0
#endif
#ifndef TRUE
#define TRUE    (!FALSE)
#endif

//#define USE_3DNOW

/* Basic DTMF specs:
 *
 * Minimum tone on = 40ms
 * Minimum tone off = 50ms
 * Maximum digit rate = 10 per second
 * Normal twist <= 8dB accepted
 * Reverse twist <= 4dB accepted
 * S/N >= 15dB will detect OK
 * Attenuation <= 26dB will detect OK
 * Frequency tolerance +- 1.5% will detect, +-3.5% will reject
 */

#define SAMPLE_RATE                 8000.0

#define DTMF_THRESHOLD              8.0e7
#define FAX_THRESHOLD              8.0e7
#define FAX_2ND_HARMONIC       		2.0     /* 4dB */
#define DTMF_NORMAL_TWIST           6.3 /* 8dB */
#define DTMF_REVERSE_TWIST          ((isradio) ? 4.0 : 2.5)     /* 4dB normal */
#define DTMF_RELATIVE_PEAK_ROW      6.3 /* 8dB */
#define DTMF_RELATIVE_PEAK_COL      6.3 /* 8dB */
#define DTMF_2ND_HARMONIC_ROW       ((isradio) ? 1.7 : 2.5)     /* 4dB normal */
#define DTMF_2ND_HARMONIC_COL       63.1        /* 18dB */

static tone_detection_descriptor_t dtmf_detect_row[4];
static tone_detection_descriptor_t dtmf_detect_col[4];
static tone_detection_descriptor_t dtmf_detect_row_2nd[4];
static tone_detection_descriptor_t dtmf_detect_col_2nd[4];
static tone_detection_descriptor_t fax_detect;
static tone_detection_descriptor_t fax_detect_2nd;

static float dtmf_row[] = {
  697.0, 770.0, 852.0, 941.0
};

static float dtmf_col[] = {
  1209.0, 1336.0, 1477.0, 1633.0
};

static float fax_freq = 1100.0;

static char dtmf_positions[] = "123A" "456B" "789C" "*0#D";

static void
goertzel_init (goertzel_state_t * s, tone_detection_descriptor_t * t)
{
  s->v2 = s->v3 = 0.0;
  s->fac = t->fac;
}

/*- End of function --------------------------------------------------------*/

#if defined(USE_3DNOW)
static inline void
_dtmf_goertzel_update (goertzel_state_t * s, float x[], int samples)
{
  int n;
  float v;
  int i;
  float vv[16];

  vv[4] = s[0].v2;
  vv[5] = s[1].v2;
  vv[6] = s[2].v2;
  vv[7] = s[3].v2;
  vv[8] = s[0].v3;
  vv[9] = s[1].v3;
  vv[10] = s[2].v3;
  vv[11] = s[3].v3;
  vv[12] = s[0].fac;
  vv[13] = s[1].fac;
  vv[14] = s[2].fac;
  vv[15] = s[3].fac;

  //v1 = s->v2;
  //s->v2 = s->v3;
  //s->v3 = s->fac*s->v2 - v1 + x[0];

  __asm__ __volatile__ (" femms;\n"
      " movq        16(%%edx),%%mm2;\n"
      " movq        24(%%edx),%%mm3;\n"
      " movq        32(%%edx),%%mm4;\n"
      " movq        40(%%edx),%%mm5;\n"
      " movq        48(%%edx),%%mm6;\n"
      " movq        56(%%edx),%%mm7;\n"
      " jmp         1f;\n"
      " .align 32;\n"
      " 1: ;\n"
      " prefetch    (%%eax);\n"
      " movq        %%mm3,%%mm1;\n"
      " movq        %%mm2,%%mm0;\n"
      " movq        %%mm5,%%mm3;\n"
      " movq        %%mm4,%%mm2;\n"
      " pfmul       %%mm7,%%mm5;\n"
      " pfmul       %%mm6,%%mm4;\n"
      " pfsub       %%mm1,%%mm5;\n"
      " pfsub       %%mm0,%%mm4;\n"
      " movq        (%%eax),%%mm0;\n"
      " movq        %%mm0,%%mm1;\n"
      " punpckldq   %%mm0,%%mm1;\n"
      " add         $4,%%eax;\n"
      " pfadd       %%mm1,%%mm5;\n"
      " pfadd       %%mm1,%%mm4;\n"
      " dec         %%ecx;\n"
      " jnz         1b;\n"
      " movq        %%mm2,16(%%edx);\n"
      " movq        %%mm3,24(%%edx);\n"
      " movq        %%mm4,32(%%edx);\n"
      " movq        %%mm5,40(%%edx);\n"
      " femms;\n"::"c" (samples), "a" (x), "d" (vv)
      :"memory", "eax", "ecx");

  s[0].v2 = vv[4];
  s[1].v2 = vv[5];
  s[2].v2 = vv[6];
  s[3].v2 = vv[7];
  s[0].v3 = vv[8];
  s[1].v3 = vv[9];
  s[2].v3 = vv[10];
  s[3].v3 = vv[11];
}
#endif
/*- End of function --------------------------------------------------------*/

void
zap_goertzel_update (goertzel_state_t * s, int16_t x[], int samples)
{
  int i;
  float v1;

  for (i = 0; i < samples; i++) {
    v1 = s->v2;
    s->v2 = s->v3;
    s->v3 = s->fac * s->v2 - v1 + x[i];
  }
}

/*- End of function --------------------------------------------------------*/

float
zap_goertzel_result (goertzel_state_t * s)
{
  return s->v3 * s->v3 + s->v2 * s->v2 - s->v2 * s->v3 * s->fac;
}

/*- End of function --------------------------------------------------------*/

void
zap_dtmf_detect_init (dtmf_detect_state_t * s)
{
  int i;
  float theta;

  s->hit1 = s->hit2 = 0;

  for (i = 0; i < 4; i++) {
    theta = 2.0 * G_PI * (dtmf_row[i] / SAMPLE_RATE);
    dtmf_detect_row[i].fac = 2.0 * cos (theta);

    theta = 2.0 * G_PI * (dtmf_col[i] / SAMPLE_RATE);
    dtmf_detect_col[i].fac = 2.0 * cos (theta);

    theta = 2.0 * G_PI * (dtmf_row[i] * 2.0 / SAMPLE_RATE);
    dtmf_detect_row_2nd[i].fac = 2.0 * cos (theta);

    theta = 2.0 * G_PI * (dtmf_col[i] * 2.0 / SAMPLE_RATE);
    dtmf_detect_col_2nd[i].fac = 2.0 * cos (theta);

    goertzel_init (&s->row_out[i], &dtmf_detect_row[i]);
    goertzel_init (&s->col_out[i], &dtmf_detect_col[i]);
    goertzel_init (&s->row_out2nd[i], &dtmf_detect_row_2nd[i]);
    goertzel_init (&s->col_out2nd[i], &dtmf_detect_col_2nd[i]);

    s->energy = 0.0;
  }

  /* Same for the fax dector */
  theta = 2.0 * G_PI * (fax_freq / SAMPLE_RATE);
  fax_detect.fac = 2.0 * cos (theta);
  goertzel_init (&s->fax_tone, &fax_detect);

  /* Same for the fax dector 2nd harmonic */
  theta = 2.0 * G_PI * (fax_freq * 2.0 / SAMPLE_RATE);
  fax_detect_2nd.fac = 2.0 * cos (theta);
  goertzel_init (&s->fax_tone2nd, &fax_detect_2nd);

  s->current_sample = 0;
  s->detected_digits = 0;
  s->lost_digits = 0;
  s->digits[0] = '\0';
  s->mhit = 0;
}

/*- End of function --------------------------------------------------------*/

int
zap_dtmf_detect (dtmf_detect_state_t * s,
    int16_t amp[], int samples, int isradio)
{

  float row_energy[4];
  float col_energy[4];
  float fax_energy;
  float fax_energy_2nd;
  float famp;
  float v1;
  int i;
  int j;
  int sample;
  int best_row;
  int best_col;
  int hit;
  int limit;

  hit = 0;
  for (sample = 0; sample < samples; sample = limit) {
    /* 102 is optimised to meet the DTMF specs. */
    if ((samples - sample) >= (102 - s->current_sample))
      limit = sample + (102 - s->current_sample);
    else
      limit = samples;
#if defined(USE_3DNOW)
    _dtmf_goertzel_update (s->row_out, amp + sample, limit - sample);
    _dtmf_goertzel_update (s->col_out, amp + sample, limit - sample);
    _dtmf_goertzel_update (s->row_out2nd, amp + sample, limit2 - sample);
    _dtmf_goertzel_update (s->col_out2nd, amp + sample, limit2 - sample);
    /* XXX Need to fax detect for 3dnow too XXX */
#warning "Fax Support Broken"
#else
    /* The following unrolled loop takes only 35% (rough estimate) of the 
       time of a rolled loop on the machine on which it was developed */
    for (j = sample; j < limit; j++) {
      famp = amp[j];

      s->energy += famp * famp;

      /* With GCC 2.95, the following unrolled code seems to take about 35%
         (rough estimate) as long as a neat little 0-3 loop */
      v1 = s->row_out[0].v2;
      s->row_out[0].v2 = s->row_out[0].v3;
      s->row_out[0].v3 = s->row_out[0].fac * s->row_out[0].v2 - v1 + famp;

      v1 = s->col_out[0].v2;
      s->col_out[0].v2 = s->col_out[0].v3;
      s->col_out[0].v3 = s->col_out[0].fac * s->col_out[0].v2 - v1 + famp;

      v1 = s->row_out[1].v2;
      s->row_out[1].v2 = s->row_out[1].v3;
      s->row_out[1].v3 = s->row_out[1].fac * s->row_out[1].v2 - v1 + famp;

      v1 = s->col_out[1].v2;
      s->col_out[1].v2 = s->col_out[1].v3;
      s->col_out[1].v3 = s->col_out[1].fac * s->col_out[1].v2 - v1 + famp;

      v1 = s->row_out[2].v2;
      s->row_out[2].v2 = s->row_out[2].v3;
      s->row_out[2].v3 = s->row_out[2].fac * s->row_out[2].v2 - v1 + famp;

      v1 = s->col_out[2].v2;
      s->col_out[2].v2 = s->col_out[2].v3;
      s->col_out[2].v3 = s->col_out[2].fac * s->col_out[2].v2 - v1 + famp;

      v1 = s->row_out[3].v2;
      s->row_out[3].v2 = s->row_out[3].v3;
      s->row_out[3].v3 = s->row_out[3].fac * s->row_out[3].v2 - v1 + famp;

      v1 = s->col_out[3].v2;
      s->col_out[3].v2 = s->col_out[3].v3;
      s->col_out[3].v3 = s->col_out[3].fac * s->col_out[3].v2 - v1 + famp;

      v1 = s->col_out2nd[0].v2;
      s->col_out2nd[0].v2 = s->col_out2nd[0].v3;
      s->col_out2nd[0].v3 =
          s->col_out2nd[0].fac * s->col_out2nd[0].v2 - v1 + famp;

      v1 = s->row_out2nd[0].v2;
      s->row_out2nd[0].v2 = s->row_out2nd[0].v3;
      s->row_out2nd[0].v3 =
          s->row_out2nd[0].fac * s->row_out2nd[0].v2 - v1 + famp;

      v1 = s->col_out2nd[1].v2;
      s->col_out2nd[1].v2 = s->col_out2nd[1].v3;
      s->col_out2nd[1].v3 =
          s->col_out2nd[1].fac * s->col_out2nd[1].v2 - v1 + famp;

      v1 = s->row_out2nd[1].v2;
      s->row_out2nd[1].v2 = s->row_out2nd[1].v3;
      s->row_out2nd[1].v3 =
          s->row_out2nd[1].fac * s->row_out2nd[1].v2 - v1 + famp;

      v1 = s->col_out2nd[2].v2;
      s->col_out2nd[2].v2 = s->col_out2nd[2].v3;
      s->col_out2nd[2].v3 =
          s->col_out2nd[2].fac * s->col_out2nd[2].v2 - v1 + famp;

      v1 = s->row_out2nd[2].v2;
      s->row_out2nd[2].v2 = s->row_out2nd[2].v3;
      s->row_out2nd[2].v3 =
          s->row_out2nd[2].fac * s->row_out2nd[2].v2 - v1 + famp;

      v1 = s->col_out2nd[3].v2;
      s->col_out2nd[3].v2 = s->col_out2nd[3].v3;
      s->col_out2nd[3].v3 =
          s->col_out2nd[3].fac * s->col_out2nd[3].v2 - v1 + famp;

      v1 = s->row_out2nd[3].v2;
      s->row_out2nd[3].v2 = s->row_out2nd[3].v3;
      s->row_out2nd[3].v3 =
          s->row_out2nd[3].fac * s->row_out2nd[3].v2 - v1 + famp;

      /* Update fax tone */
      v1 = s->fax_tone.v2;
      s->fax_tone.v2 = s->fax_tone.v3;
      s->fax_tone.v3 = s->fax_tone.fac * s->fax_tone.v2 - v1 + famp;

      v1 = s->fax_tone.v2;
      s->fax_tone2nd.v2 = s->fax_tone2nd.v3;
      s->fax_tone2nd.v3 = s->fax_tone2nd.fac * s->fax_tone2nd.v2 - v1 + famp;
    }
#endif
    s->current_sample += (limit - sample);
    if (s->current_sample < 102)
      continue;

    /* Detect the fax energy, too */
    fax_energy = zap_goertzel_result (&s->fax_tone);

    /* We are at the end of a DTMF detection block */
    /* Find the peak row and the peak column */
    row_energy[0] = zap_goertzel_result (&s->row_out[0]);
    col_energy[0] = zap_goertzel_result (&s->col_out[0]);

    for (best_row = best_col = 0, i = 1; i < 4; i++) {
      row_energy[i] = zap_goertzel_result (&s->row_out[i]);
      if (row_energy[i] > row_energy[best_row])
        best_row = i;
      col_energy[i] = zap_goertzel_result (&s->col_out[i]);
      if (col_energy[i] > col_energy[best_col])
        best_col = i;
    }
    hit = 0;
    /* Basic signal level test and the twist test */
    if (row_energy[best_row] >= DTMF_THRESHOLD
        &&
        col_energy[best_col] >= DTMF_THRESHOLD
        &&
        col_energy[best_col] < row_energy[best_row] * DTMF_REVERSE_TWIST
        && col_energy[best_col] * DTMF_NORMAL_TWIST > row_energy[best_row]) {
      /* Relative peak test */
      for (i = 0; i < 4; i++) {
        if ((i != best_col
                && col_energy[i] * DTMF_RELATIVE_PEAK_COL >
                col_energy[best_col])
            || (i != best_row
                && row_energy[i] * DTMF_RELATIVE_PEAK_ROW >
                row_energy[best_row])) {
          break;
        }
      }
      /* ... and second harmonic test */
      if (i >= 4
          &&
          (row_energy[best_row] + col_energy[best_col]) > 42.0 * s->energy
          &&
          zap_goertzel_result (&s->col_out2nd[best_col]) *
          DTMF_2ND_HARMONIC_COL < col_energy[best_col]
          && zap_goertzel_result (&s->row_out2nd[best_row]) *
          DTMF_2ND_HARMONIC_ROW < row_energy[best_row]) {
        hit = dtmf_positions[(best_row << 2) + best_col];
        /* Look for two successive similar results */
        /* The logic in the next test is:
           We need two successive identical clean detects, with
           something different preceeding it. This can work with
           back to back differing digits. More importantly, it
           can work with nasty phones that give a very wobbly start
           to a digit. */
        if (hit == s->hit3 && s->hit3 != s->hit2) {
          s->mhit = hit;
          s->digit_hits[(best_row << 2) + best_col]++;
          s->detected_digits++;
          if (s->current_digits < MAX_DTMF_DIGITS) {
            s->digits[s->current_digits++] = hit;
            s->digits[s->current_digits] = '\0';
          } else {
            s->lost_digits++;
          }
        }
      }
    }
    if (!hit && (fax_energy >= FAX_THRESHOLD)
        && (fax_energy > s->energy * 21.0)) {
      fax_energy_2nd = zap_goertzel_result (&s->fax_tone2nd);
      if (fax_energy_2nd * FAX_2ND_HARMONIC < fax_energy) {
#if 0
        printf ("Fax energy/Second Harmonic: %f/%f\n", fax_energy,
            fax_energy_2nd);
#endif
        /* XXX Probably need better checking than just this the energy XXX */
        hit = 'f';
        s->fax_hits++;
      }                         /* Don't reset fax hits counter */
    } else {
      if (s->fax_hits > 5) {
        s->mhit = 'f';
        s->detected_digits++;
        if (s->current_digits < MAX_DTMF_DIGITS) {
          s->digits[s->current_digits++] = hit;
          s->digits[s->current_digits] = '\0';
        } else {
          s->lost_digits++;
        }
      }
      s->fax_hits = 0;
    }
    s->hit1 = s->hit2;
    s->hit2 = s->hit3;
    s->hit3 = hit;
    /* Reinitialise the detector for the next block */
    for (i = 0; i < 4; i++) {
      goertzel_init (&s->row_out[i], &dtmf_detect_row[i]);
      goertzel_init (&s->col_out[i], &dtmf_detect_col[i]);
      goertzel_init (&s->row_out2nd[i], &dtmf_detect_row_2nd[i]);
      goertzel_init (&s->col_out2nd[i], &dtmf_detect_col_2nd[i]);
    }
    goertzel_init (&s->fax_tone, &fax_detect);
    goertzel_init (&s->fax_tone2nd, &fax_detect_2nd);
    s->energy = 0.0;
    s->current_sample = 0;
  }
  if ((!s->mhit) || (s->mhit != hit)) {
    s->mhit = 0;
    return (0);
  }
  return (hit);
}

/*- End of function --------------------------------------------------------*/

int
zap_dtmf_get (dtmf_detect_state_t * s, char *buf, int max)
{
  if (max > s->current_digits)
    max = s->current_digits;
  if (max > 0) {
    memcpy (buf, s->digits, max);
    memmove (s->digits, s->digits + max, s->current_digits - max);
    s->current_digits -= max;
  }
  buf[max] = '\0';
  return max;
}

/*- End of function --------------------------------------------------------*/
/*- End of file ------------------------------------------------------------*/
