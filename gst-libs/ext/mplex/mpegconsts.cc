
/*
 *  mpegconsts.c:  Video format constants for MPEG and utilities for display
 *                 and conversion to format used for yuv4mpeg
 *
 *  Copyright (C) 2001 Andrew Stevens <andrew.stevens@philips.com>
 *  Copyright (C) 2001 Matthew Marjanovic <maddog@mir.com>
 *
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of version 2 of the GNU General Public License
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <config.h>
#include "mpegconsts.h"
#include "yuv4mpeg.h"
#include "yuv4mpeg_intern.h"

static y4m_ratio_t mpeg_framerates[] = {
  Y4M_FPS_UNKNOWN,
  Y4M_FPS_NTSC_FILM,
  Y4M_FPS_FILM,
  Y4M_FPS_PAL,
  Y4M_FPS_NTSC,
  Y4M_FPS_30,
  Y4M_FPS_PAL_FIELD,
  Y4M_FPS_NTSC_FIELD,
  Y4M_FPS_60
};


#define MPEG_NUM_RATES (sizeof(mpeg_framerates)/sizeof(mpeg_framerates[0]))
const mpeg_framerate_code_t mpeg_num_framerates = MPEG_NUM_RATES;

static const char *framerate_definitions[MPEG_NUM_RATES] = {
  "illegal",
  "24000.0/1001.0 (NTSC 3:2 pulldown converted FILM)",
  "24.0 (NATIVE FILM)",
  "25.0 (PAL/SECAM VIDEO / converted FILM)",
  "30000.0/1001.0 (NTSC VIDEO)",
  "30.0",
  "50.0 (PAL FIELD RATE)",
  "60000.0/1001.0 (NTSC FIELD RATE)",
  "60.0"
};


static const char *mpeg1_aspect_ratio_definitions[] = {
  "1:1 (square pixels)",
  "1:0.6735",
  "1:0.7031 (16:9 Anamorphic PAL/SECAM for 720x578/352x288 images)",
  "1:0.7615",
  "1:0.8055",
  "1:0.8437 (16:9 Anamorphic NTSC for 720x480/352x240 images)",
  "1:0.8935",
  "1:0.9375 (4:3 PAL/SECAM for 720x578/352x288 images)",
  "1:0.9815",
  "1:1.0255",
  "1:1:0695",
  "1:1.1250 (4:3 NTSC for 720x480/352x240 images)",
  "1:1.1575",
  "1:1.2015"
};

static const y4m_ratio_t mpeg1_aspect_ratios[] = {
  Y4M_SAR_MPEG1_1,
  Y4M_SAR_MPEG1_2,
  Y4M_SAR_MPEG1_3,		/* Anamorphic 16:9 PAL */
  Y4M_SAR_MPEG1_4,
  Y4M_SAR_MPEG1_5,
  Y4M_SAR_MPEG1_6,		/* Anamorphic 16:9 NTSC */
  Y4M_SAR_MPEG1_7,
  Y4M_SAR_MPEG1_8,		/* PAL/SECAM 4:3 */
  Y4M_SAR_MPEG1_9,
  Y4M_SAR_MPEG1_10,
  Y4M_SAR_MPEG1_11,
  Y4M_SAR_MPEG1_12,		/* NTSC 4:3 */
  Y4M_SAR_MPEG1_13,
  Y4M_SAR_MPEG1_14,
};

static const char *mpeg2_aspect_ratio_definitions[] = {
  "1:1 pixels",
  "4:3 display",
  "16:9 display",
  "2.21:1 display"
};


static const y4m_ratio_t mpeg2_aspect_ratios[] = {
  Y4M_DAR_MPEG2_1,
  Y4M_DAR_MPEG2_2,
  Y4M_DAR_MPEG2_3,
  Y4M_DAR_MPEG2_4
};

static const char **aspect_ratio_definitions[2] = {
  mpeg1_aspect_ratio_definitions,
  mpeg2_aspect_ratio_definitions
};

static const y4m_ratio_t *mpeg_aspect_ratios[2] = {
  mpeg1_aspect_ratios,
  mpeg2_aspect_ratios
};

const mpeg_aspect_code_t mpeg_num_aspect_ratios[2] = {
  sizeof (mpeg1_aspect_ratios) / sizeof (mpeg1_aspect_ratios[0]),
  sizeof (mpeg2_aspect_ratios) / sizeof (mpeg2_aspect_ratios[0])
};

/*
 * Convert MPEG frame-rate code to corresponding frame-rate
 */

y4m_ratio_t
mpeg_framerate (mpeg_framerate_code_t code)
{
  if (code == 0 || code > mpeg_num_framerates)
    return y4m_fps_UNKNOWN;
  else
    return mpeg_framerates[code];
}

/*
 * Look-up MPEG frame rate code for a (exact) frame rate.
 */


mpeg_framerate_code_t
mpeg_framerate_code (y4m_ratio_t framerate)
{
  mpeg_framerate_code_t i;

  y4m_ratio_reduce (&framerate);
  for (i = 1; i < mpeg_num_framerates; ++i) {
    if (Y4M_RATIO_EQL (framerate, mpeg_framerates[i]))
      return i;
  }
  return 0;
}


/* small enough to distinguish 1/1000 from 1/1001 */
#define MPEG_FPS_TOLERANCE 0.0001


y4m_ratio_t
mpeg_conform_framerate (double fps)
{
  mpeg_framerate_code_t i;
  y4m_ratio_t result;

  /* try to match it to a standard frame rate */
  for (i = 1; i < mpeg_num_framerates; i++) {
    double deviation = 1.0 - (Y4M_RATIO_DBL (mpeg_framerates[i]) / fps);

    if ((deviation > -MPEG_FPS_TOLERANCE) && (deviation < +MPEG_FPS_TOLERANCE))
      return mpeg_framerates[i];
  }
  /* no luck?  just turn it into a ratio (6 decimal place accuracy) */
  result.n = (int) ((fps * 1000000.0) + 0.5);
  result.d = 1000000;
  y4m_ratio_reduce (&result);
  return result;
}



/*
 * Convert MPEG aspect-ratio code to corresponding aspect-ratio
 */

y4m_ratio_t
mpeg_aspect_ratio (int mpeg_version, mpeg_aspect_code_t code)
{
  y4m_ratio_t ratio;

  if (mpeg_version < 1 || mpeg_version > 2)
    return y4m_sar_UNKNOWN;
  if (code == 0 || code > mpeg_num_aspect_ratios[mpeg_version - 1])
    return y4m_sar_UNKNOWN;
  else {
    ratio = mpeg_aspect_ratios[mpeg_version - 1][code - 1];
    y4m_ratio_reduce (&ratio);
    return ratio;
  }
}

/*
 * Look-up corresponding MPEG aspect ratio code given an exact aspect ratio.
 *
 * WARNING: The semantics of aspect ratio coding *changed* between
 * MPEG1 and MPEG2.  In MPEG1 it is the *pixel* aspect ratio. In
 * MPEG2 it is the (far more sensible) aspect ratio of the eventual
 * display.
 *
 */

mpeg_aspect_code_t
mpeg_frame_aspect_code (int mpeg_version, y4m_ratio_t aspect_ratio)
{
  mpeg_aspect_code_t i;
  y4m_ratio_t red_ratio = aspect_ratio;

  y4m_ratio_reduce (&red_ratio);
  if (mpeg_version < 1 || mpeg_version > 2)
    return 0;
  for (i = 1; i < mpeg_num_aspect_ratios[mpeg_version - 1]; ++i) {
    y4m_ratio_t red_entry = mpeg_aspect_ratios[mpeg_version - 1][i - 1];

    y4m_ratio_reduce (&red_entry);
    if (Y4M_RATIO_EQL (red_entry, red_ratio))
      return i;
  }

  return 0;

}



/*
 * Guess the correct MPEG aspect ratio code,
 *  given the true sample aspect ratio and frame size of a video stream
 *  (and the MPEG version, 1 or 2).
 *
 * Returns 0 if it has no good guess.
 *
 */


/* this is big enough to accommodate the difference between 720 and 704 */
#define GUESS_ASPECT_TOLERANCE 0.03

mpeg_aspect_code_t
mpeg_guess_mpeg_aspect_code (int mpeg_version, y4m_ratio_t sampleaspect,
			     int frame_width, int frame_height)
{
  if (Y4M_RATIO_EQL (sampleaspect, y4m_sar_UNKNOWN)) {
    return 0;
  }
  switch (mpeg_version) {
    case 1:
      if (Y4M_RATIO_EQL (sampleaspect, y4m_sar_SQUARE)) {
	return 1;
      } else if (Y4M_RATIO_EQL (sampleaspect, y4m_sar_NTSC_CCIR601)) {
	return 12;
      } else if (Y4M_RATIO_EQL (sampleaspect, y4m_sar_NTSC_16_9)) {
	return 6;
      } else if (Y4M_RATIO_EQL (sampleaspect, y4m_sar_PAL_CCIR601)) {
	return 8;
      } else if (Y4M_RATIO_EQL (sampleaspect, y4m_sar_PAL_16_9)) {
	return 3;
      }
      return 0;
      break;
    case 2:
      if (Y4M_RATIO_EQL (sampleaspect, y4m_sar_SQUARE)) {
	return 1;		/* '1' means square *pixels* in MPEG-2; go figure. */
      } else {
	unsigned int i;
	double true_far;	/* true frame aspect ratio */

	true_far =
	  (double) (sampleaspect.n * frame_width) / (double) (sampleaspect.d * frame_height);
	/* start at '2'... */
	for (i = 2; i < mpeg_num_aspect_ratios[mpeg_version - 1]; i++) {
	  double ratio = true_far / Y4M_RATIO_DBL (mpeg_aspect_ratios[mpeg_version - 1][i - 1]);

	  if ((ratio > (1.0 - GUESS_ASPECT_TOLERANCE)) && (ratio < (1.0 + GUESS_ASPECT_TOLERANCE)))
	    return i;
	}
	return 0;
      }
      break;
    default:
      return 0;
      break;
  }
}




/*
 * Guess the true sample aspect ratio of a video stream,
 *  given the MPEG aspect ratio code and the actual frame size
 *  (and the MPEG version, 1 or 2).
 *
 * Returns y4m_sar_UNKNOWN if it has no good guess.
 *
 */
y4m_ratio_t
mpeg_guess_sample_aspect_ratio (int mpeg_version,
				mpeg_aspect_code_t code, int frame_width, int frame_height)
{
  switch (mpeg_version) {
    case 1:
      /* MPEG-1 codes turn into SAR's, just not quite the right ones.
         For the common/known values, we provide the ratio used in practice,
         otherwise say we don't know. */
      switch (code) {
	case 1:
	  return y4m_sar_SQUARE;
	  break;
	case 3:
	  return y4m_sar_PAL_16_9;
	  break;
	case 6:
	  return y4m_sar_NTSC_16_9;
	  break;
	case 8:
	  return y4m_sar_PAL_CCIR601;
	  break;
	case 12:
	  return y4m_sar_NTSC_CCIR601;
	  break;
	default:
	  return y4m_sar_UNKNOWN;
	  break;
      }
      break;
    case 2:
      /* MPEG-2 codes turn into Frame Aspect Ratios, though not exactly the
         FAR's used in practice.  For common/standard frame sizes, we provide
         the original SAR; otherwise, we say we don't know. */
      if (code == 1) {
	return y4m_sar_SQUARE;	/* '1' means square *pixels* in MPEG-2 */
      } else if ((code >= 2) && (code <= 4)) {
	return y4m_guess_sar (frame_width, frame_height, mpeg2_aspect_ratios[code - 1]);
      } else {
	return y4m_sar_UNKNOWN;
      }
      break;
    default:
      return y4m_sar_UNKNOWN;
      break;
  }
}





/*
 * Look-up MPEG explanatory definition string for frame rate code
 *
 */


const char *
mpeg_framerate_code_definition (mpeg_framerate_code_t code)
{
  if (code == 0 || code >= mpeg_num_framerates)
    return "UNDEFINED: illegal/reserved frame-rate ratio code";

  return framerate_definitions[code];
}

/*
 * Look-up MPEG explanatory definition string aspect ratio code for an
 * aspect ratio code
 *
 */

const char *
mpeg_aspect_code_definition (int mpeg_version, mpeg_aspect_code_t code)
{
  if (mpeg_version < 1 || mpeg_version > 2)
    return "UNDEFINED: illegal MPEG version";

  if (code < 1 || code > mpeg_num_aspect_ratios[mpeg_version - 1])
    return "UNDEFINED: illegal aspect ratio code";

  return aspect_ratio_definitions[mpeg_version - 1][code - 1];
}


/*
 * Look-up explanatory definition of interlace field order code
 *
 */

const char *
mpeg_interlace_code_definition (int yuv4m_interlace_code)
{
  const char *def;

  switch (yuv4m_interlace_code) {
    case Y4M_UNKNOWN:
      def = "unknown";
      break;
    case Y4M_ILACE_NONE:
      def = "none/progressive";
      break;
    case Y4M_ILACE_TOP_FIRST:
      def = "top-field-first";
      break;
    case Y4M_ILACE_BOTTOM_FIRST:
      def = "bottom-field-first";
      break;
    default:
      def = "UNDEFINED: illegal video interlacing type-code!";
      break;
  }
  return def;
}


/* 
 * Local variables:
 *  c-file-style: "stroustrup"
 *  tab-width: 4
 *  indent-tabs-mode: nil
 * End:
 */
