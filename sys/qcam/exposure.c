/* exposure.c
 *
 * Time-stamp: <02 Sep 96 11:52:21 HST edo@eosys.com>
 *
 * Version 0.2
 */


/******************************************************************

Copyright (C) 1996 by Ed Orcutt Systems

Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use,
copy, modify, merge, publish, and/or distribute copies of the
Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

1. The above copyright notice and this permission notice shall
   be included in all copies or substantial portions of the
   Software.

2. Redistribution for profit requires the express, written
   permission of the author.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT.  IN NO EVENT SHALL ED ORCUTT SYSTEMS BE LIABLE
FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

******************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include "qcam.h"
#include "qcamip.h"

/* Prototypes for private (static) functions used by the routines
 * within this file.  Externally visible functions should be
 * prototyped in qcamip.h
 */

static int qcip_pixel_average (struct qcam *q, scanbuf * scan);
static int qcip_luminance_std (struct qcam *q, scanbuf * scan, int avg);

/* Private data used by the auto exposure routine */

static int luminance_target = -1;
static int luminance_tolerance = 0;
static int luminance_std_target = -1;
static int luminance_std_tolerance = 0;
static int ae_mode = AE_ALL_AVG;

/* Calculate average pixel value for entire image */

static int
qcip_pixel_average (struct qcam *q, scanbuf * scan)
{
  int count = 0;
  int sum = 0;
  int pixels;
  int i;

  pixels = q->height / q->transfer_scale;
  pixels *= q->width / q->transfer_scale;

  for (i = 0; i < pixels; i++) {
    sum += scan[i];
    count++;
  }
  return (sum / count);
}

/* Calculate average pixel value for center of image */

static int
qcip_pixel_average_center (struct qcam *q, scanbuf * scan)
{
  int count = 0;
  int sum = 0;
  int height, width;
  int maxrow, maxcol;
  int i, j;

  /* actual image width & height after scaling */
  width = q->width / q->transfer_scale;
  height = q->height / q->transfer_scale;

  maxcol = width * 2 / 3;
  maxrow = height * 2 / 3;

  for (i = width / 3; i < maxcol; i++) {
    for (j = height / 3; j < maxrow; j++) {
      sum += scan[j * width + i];
      count++;
    }
  }
  return (sum / count);
}

int
qcip_set_luminance_target (struct qcam *q, int val)
{
  const int max_pixel_val = q->bpp == 6 ? 63 : 15;

  if ((val - luminance_tolerance) >= 0 &&
      (val + luminance_tolerance) <= max_pixel_val) {
    luminance_target = val;
    return QCIP_XPSR_OK;
  }
  return QCIP_XPSR_LUM_INVLD;
}

int
qcip_set_luminance_tolerance (struct qcam *q, int val)
{
  const int max_pixel_val = q->bpp == 6 ? 63 : 15;

  /* set target if it has not been explicitly set */
  if (luminance_target == -1) {
    luminance_target = q->bpp == 6 ? 32 : 8;
  }

  if ((luminance_target - val) >= 0 &&
      (luminance_target + val) <= max_pixel_val) {
    luminance_tolerance = val;
    return QCIP_XPSR_OK;
  }
  return QCIP_XPSR_LUM_INVLD;
}

int
qcip_set_luminance_std_target (struct qcam *q, int val)
{
  luminance_std_target = val;
  return QCIP_XPSR_OK;
}

int
qcip_set_luminance_std_tolerance (struct qcam *q, int val)
{
  luminance_std_tolerance = val;
  return QCIP_XPSR_OK;
}

int
qcip_set_autoexposure_mode (int val)
{
  ae_mode = val;
  return 0;
}

/* Calculate standard deviation of pixel value for entire image */

static int
qcip_luminance_std (struct qcam *q, scanbuf * scan, int avg)
{
  int count = 0;
  int sum = 0;
  int pixels;
  int i;

  pixels = q->height / q->transfer_scale;
  pixels *= q->width / q->transfer_scale;

  for (i = 0; i < pixels; i++) {
    if (scan[i] < avg) {
      sum += avg - scan[i];
    } else {
      sum += scan[i] - avg;
    }
    count++;
  }
  return (sum / count);
}


/* If necessary adjust the brightness in an attempt to achieve
 * a target average pixel value: 32 for 6 bpp, 8 for 4bpp.
 * This routine *will* modify the brightness value in preparation
 * for another scan unless the target average pixel values has
 * been reached. If the exposure is correct (yes, I realize that
 * this is subjective) QCIP_XPSR_OK will be returned, otherwise
 * return QCIP_XPSR_RSCN after adjusting the exposure.
 *
 * Caveat: If the new calculated brightness value is invalid,
 *         QCIP_XPSR_ERR will be returned.
 */

int
qcip_autoexposure (struct qcam *q, scanbuf * scan)
{
  int luminance_dif;
  int luminance_avg;
  int brightness_adj;
  int lum_min, lum_max;
  int lum_std, lum_std_min, lum_std_max;
  int ret = QCIP_XPSR_OK;

#ifdef DEBUG
  fprintf (stderr, "Brightness: %d  Contrast: %d\n",
      qc_getbrightness (q), qc_getcontrast (q));
#endif

  switch (ae_mode) {
    case AE_CTR_AVG:
      luminance_avg = qcip_pixel_average_center (q, scan);
      break;
    case AE_STD_AVG:
      luminance_avg = qcip_pixel_average (q, scan);
      lum_std = qcip_luminance_std (q, scan, luminance_avg);

      /* ==>> Contrast adjustment <<== */

      /* set target if it has not been explicitly set */
      if (luminance_std_target == -1) {
        luminance_std_target = q->bpp == 6 ? 10 : 2;
      }

      /* Adjust contrast to reach target luminance standard deviation */
      lum_std_min = luminance_std_target - luminance_std_tolerance;
      lum_std_max = luminance_std_target + luminance_std_tolerance;

      if (lum_std < lum_std_min || lum_std > lum_std_max) {
        ret = QCIP_XPSR_RSCN;
        if (qc_setcontrast (q,
                luminance_std_target - lum_std + qc_getcontrast (q))) {
          return QCIP_XPSR_ERR;
        }
      }
#ifdef DEBUG
      fprintf (stderr, "Luminance std/target/tolerance: %d/%d/%d\n",
          lum_std, luminance_std_target, luminance_std_tolerance);
#endif

      break;
    case AE_ALL_AVG:
    default:
      luminance_avg = qcip_pixel_average (q, scan);
      break;
  }

  /* ==>> Brightness adjustment <<== */

  /* set target if it has not been explicitly set */
  if (luminance_target == -1) {
    luminance_target = q->bpp == 6 ? 32 : 8;
  }

  lum_min = luminance_target - luminance_tolerance;
  lum_max = luminance_target + luminance_tolerance;

#ifdef DEBUG
  fprintf (stderr, "Luminance avg/target/tolerance: %d/%d/%d\n",
      luminance_avg, luminance_target, luminance_tolerance);
#endif

  /* check for luminance within target range */
  if (luminance_avg < lum_min || luminance_avg > lum_max) {
    ret = QCIP_XPSR_RSCN;
    /* we need to adjust the brighness, which way? */
    luminance_dif = luminance_target - luminance_avg;
    if (luminance_dif > 0) {
      brightness_adj = luminance_dif / 2 + 1;
    } else {
      brightness_adj = luminance_dif / 2 - 1;
    }

    /* Adjusted brightness is out of range ..
     * throw in the towel ... auto-exposure has failed!
     */
    if (qc_setbrightness (q, brightness_adj + qc_getbrightness (q))) {
      return QCIP_XPSR_ERR;
    }
  }

  return ret;
}
