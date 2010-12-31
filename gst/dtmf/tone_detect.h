/* 
 *   Header file for DTMF Receiver module, part of:
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
	tone_detect.h - General telephony tone detection, and specific
                        detection of DTMF.

        Copyright (C) 2001  Steve Underwood <steveu@coppice.org>

        Despite my general liking of the GPL, I place this code in the
        public domain for the benefit of all mankind - even the slimy
        ones who might try to proprietize my work and use it to my
        detriment.
*/

#ifndef __TONE_DETECT_H__
#define __TONE_DETECT_H__

#include "_stdint.h"

#include <glib.h>

typedef struct
{
    float v2;
    float v3;
    float fac;
} goertzel_state_t;

#define	MAX_DTMF_DIGITS 128

typedef struct
{
    int hit1;
    int hit2;
    int hit3;
    int hit4;
    int mhit;

    goertzel_state_t row_out[4];
    goertzel_state_t col_out[4];
    goertzel_state_t row_out2nd[4];
    goertzel_state_t col_out2nd[4];
	goertzel_state_t fax_tone;
	goertzel_state_t fax_tone2nd;
    float energy;
    
    int current_sample;
    char digits[MAX_DTMF_DIGITS + 1];
    int current_digits;
    int detected_digits;
    int lost_digits;
    int digit_hits[16];
	int fax_hits;
} dtmf_detect_state_t;

typedef struct
{
    float fac;
} tone_detection_descriptor_t;

void zap_goertzel_update(goertzel_state_t *s,
                     gint16 x[],
                     int samples);
float zap_goertzel_result (goertzel_state_t *s);

void zap_dtmf_detect_init (dtmf_detect_state_t *s);
int zap_dtmf_detect (dtmf_detect_state_t *s,
                 gint16 amp[],
                 int samples,
		 int isradio);
int zap_dtmf_get (dtmf_detect_state_t *s,
              char *buf,
              int max);

#endif /* __TONE_DETECT_H__ */

/*- End of file ------------------------------------------------------------*/
