/******************************************************************

Copyright (C) 1996 by Brian Scearce

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
and/or distribute copies of the Software, and to permit persons to whom
the Software is furnished to do so, subject to the following conditions:

1. The above copyright notice and this permission notice shall be
   included in all copies or substantial portions of the Software.

2. Redistribution for profit requires the express, written permission of
   the author.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL BRIAN SCEARCE BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.
******************************************************************/

/** Fixdark
	Routine to repair dark current artifacts in qcam output.
	Basic idea: the Qcam CCD suffers from "dark current";
	that is, some of the CCD pixels will leak current under
	long exposures, even if they're in the dark, and this
	shows up as ugly speckling on images taken in low light.

	Fortunately, the leaky pixels are the same from shot to
	shot.  So, we can figure out which pixels are leaky by
	taking some establishing shots in the dark, and try to
	fix those pixels on subsequent shots.  The dark
	establishing shots need only be done once per camera.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "qcam.h"
#define MAX_LOOPS 10
#define FNAME "qcam.darkfile"

static unsigned char master_darkmask1[MAX_HEIGHT][MAX_WIDTH];
static unsigned char master_darkmask2[MAX_HEIGHT / 2 + 1][MAX_WIDTH / 2 + 1];
static unsigned char master_darkmask4[MAX_HEIGHT / 4 + 1][MAX_WIDTH / 4 + 1];

/*
int
read_darkmask()
{
  int x, y;
  int min_bright;
  char darkfile[BUFSIZ], *p;
  FILE *fp;

  strcpy(darkfile, CONFIG_FILE);
  if ( (p = strrchr(darkfile, '/'))) {
    strcpy(p+1, FNAME);
  } else {
    strcpy(darkfile, FNAME);
  }

  if (!(fp = fopen(darkfile, "r"))) {
#ifdef DEBUG
    fprintf(stderr, "Can't open darkfile %s\n", darkfile);
#endif
    return 0;
  }

  if (fread(master_darkmask1, sizeof(unsigned char), MAX_WIDTH*MAX_HEIGHT, fp) !=
	MAX_WIDTH*MAX_HEIGHT) {
#ifdef DEBUG
    fprintf(stderr, "Error reading darkfile\n");
#endif
    return 0;
  }

  for (y = 0; y < MAX_HEIGHT; y += 2) {
    for (x = 0; x < MAX_WIDTH; x += 2) {
      min_bright = master_darkmask1[y][x];
      if (y < MAX_HEIGHT-1 && master_darkmask1[y+1][x] < min_bright)
	min_bright = master_darkmask1[y+1][x];
      if (x < MAX_WIDTH-1 && master_darkmask1[y][x+1] < min_bright)
	min_bright = master_darkmask1[y][x+1];
      if (y < MAX_HEIGHT-1 && x < MAX_WIDTH-1 && master_darkmask1[y+1][x+1] < min_bright)
	min_bright = master_darkmask1[y+1][x+1];
      master_darkmask2[y/2][x/2] = min_bright;
	assert(y/2 < MAX_HEIGHT/2+1);
	assert(x/2 < MAX_WIDTH/2+1);
    }
  }

  for (y = 0; y < MAX_HEIGHT/2; y += 2) {
    for (x = 0; x < MAX_WIDTH/2; x += 2) {
      min_bright = master_darkmask2[y][x];
      if (y < MAX_HEIGHT/2-1 && master_darkmask2[y+1][x] < min_bright)
	min_bright = master_darkmask2[y+1][x];
      if (x < MAX_WIDTH/2-1 && master_darkmask2[y][x+1] < min_bright)
	min_bright = master_darkmask2[y][x+1];
      if (y < MAX_HEIGHT/2-1 && x < MAX_WIDTH-1 && master_darkmask2[y+1][x+1] < min_bright)
	min_bright = master_darkmask2[y+1][x+1];
      master_darkmask4[y/2][x/2] = min_bright;
	assert(y/2 < MAX_HEIGHT/4+1);
	assert(x/2 < MAX_WIDTH/4+1);
    }
  }

  fclose(fp);
  return 1;
}
*/


/** fixdark
		We first record a list of bad leaky pixels, by making a
		number of exposures in the dark.  master_darkmask holds
		this information.  It's a map of the CCD.
		master_darkmask[y][x] == val means that the pixel is
		unreliable for brightnesses of "val" and above.

		We go over the image.  If a pixel is bad, look at the
		adjacent four pixels, average the ones that have good
		values, and use that instead.
*/

int
fixdark (const struct qcam *q, scanbuf * scan)
{
  static int init = 0;
  static int smallest_dm = 255;
  unsigned char darkmask[MAX_HEIGHT][MAX_WIDTH];
  unsigned char new_image[MAX_HEIGHT][MAX_WIDTH];
  int width, height;
  int max_width, max_height;
  int x, y;
  int ccd_x, ccd_y;
  int pixelcount, pixeltotal;
  int again, loopcount = 0;
  int val;
  int brightness = q->brightness;
  int scale = q->transfer_scale;

  if (!init) {
    if (!read_darkmask ())
      return 0;
    for (y = 0; y < MAX_HEIGHT; y++)
      for (x = 0; x < MAX_HEIGHT; x++)
        if (master_darkmask1[y][x] < smallest_dm) {
          smallest_dm = master_darkmask1[y][x];
#ifdef DEBUG
          fprintf (stderr, "Smallest mask is %d at (%d, %d)\n",
              smallest_dm, x, y);
#endif
        }
    init = 1;
  }

  if (brightness < smallest_dm) {
#ifdef DEBUG
    fprintf (stderr,
        "Brightness %d (dark current starts at %d), no fixup needed\n",
        brightness, smallest_dm);
#endif
    return 1;
  }

  width = q->width / scale;
  height = q->height / scale;

  max_height = MAX_HEIGHT / scale;
  max_width = MAX_WIDTH / scale;
  for (y = 0; y < max_height; y++)
    for (x = 0; x < max_width; x++)
      if (scale == 1) {
        darkmask[y][x] = master_darkmask1[y][x];
      } else if (scale == 2) {
        darkmask[y][x] = master_darkmask2[y][x];
      } else if (scale == 4) {
        darkmask[y][x] = master_darkmask4[y][x];
      } else {
#ifdef DEBUG
        fprintf (stderr, "Bad transfer_scale in darkmask assignment!\n");
#endif
        return 0;
      }

  do {
    again = 0;
    ccd_y = (q->top - 1) / scale;
    for (y = 0; y < height; y++, ccd_y++) {
      ccd_x = q->left - 1;
      ccd_x /= 2;
      ccd_x *= 2;
      ccd_x /= scale;
      for (x = 0; x < width; x++, ccd_x++) {
        val = scan[y * width + x];
        if (brightness < darkmask[ccd_y][ccd_x]) {      /* good pixel */
          new_image[y][x] = val;
        } else {                /* bad pixel */
          /* look at nearby pixels, average the good values */
          pixelcount = 0;
          pixeltotal = 0;
          if (x > 0) {          /* left */
            if (brightness < darkmask[ccd_y][ccd_x - 1]) {
              pixelcount++;
              pixeltotal += scan[y * width + x - 1];
            }
          }
          if (x < width - 1) {  /* right */
            if (brightness < darkmask[ccd_y][ccd_x + 1]) {
              pixelcount++;
              pixeltotal += scan[y * width + x + 1];
            }
          }
          if (y > 0) {          /* above */
            if (brightness < darkmask[ccd_y - 1][ccd_x]) {
              pixelcount++;
              pixeltotal += scan[(y - 1) * width + x];
            }
          }
          if (y < height - 1) { /* below */
            if (brightness < darkmask[ccd_y + 1][ccd_x]) {
              pixelcount++;
              pixeltotal += scan[(y + 1) * width + x];
            }
          }

          if (pixelcount == 0) {        /* no valid neighbors! */
            again = 1;
          } else {
            new_image[y][x] = pixeltotal / pixelcount;
            /* mark this pixel as valid, so we don't loop forever */
            darkmask[ccd_y][ccd_x] = 255;
          }
        }
      }
    }

    for (y = 0; y < height; y++)
      for (x = 0; x < width; x++)
        scan[y * width + x] = new_image[y][x];

  } while (loopcount++ < MAX_LOOPS && again);
#ifdef DEBUG
  fprintf (stderr, "Darkmask fix took %d loop%s\n",
      loopcount, (loopcount == 1) ? "" : "s");
#endif
  return 1;
}
