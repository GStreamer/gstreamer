/* qcam-lib.c -- Library for programming with the Connectix QuickCam.
 * See the included documentation for usage instructions and details
 * of the protocol involved. */


/* Version 0.5, August 4, 1996 */
/* Version 0.7, August 27, 1996 */
/* Version 0.9, November 17, 1996 */


/******************************************************************

Copyright (C) 1996 by Scott Laird

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL SCOTT LAIRD BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

******************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <unistd.h>

#include <assert.h>

#include "qcam.h"
#include "qcam-os.h"
#include "qcam-os.c"

/* Prototypes for static functions.  Externally visible functions
 * should be prototyped in qcam.h */

static int qc_waithand (const struct qcam *q, int val);
static int qc_command (const struct qcam *q, int command);
static int qc_readparam (const struct qcam *q);
static int qc_setscanmode (struct qcam *q);
static int qc_readbytes (const struct qcam *q, char buffer[]);

/* The next several functions are used for controlling the qcam
 * structure.  They aren't used inside this library, but they should
 * provide a clean interface for external programs.*/

/* Gets/sets the brightness. */

int
qc_getbrightness (const struct qcam *q)
{
  return q->brightness;
}

int
qc_setbrightness (struct qcam *q, int val)
{
  if (val >= 0 && val <= 255) {
    q->brightness = val;
    return 0;
  }
  return 1;
}


/* Gets/sets the contrast */

int
qc_getcontrast (const struct qcam *q)
{
  return q->contrast;
}

int
qc_setcontrast (struct qcam *q, int val)
{
  if (val >= 0 && val <= 255) {
    q->contrast = val;
    return 0;
  }
  return 1;
}


/* Gets/sets the white balance */

int
qc_getwhitebal (const struct qcam *q)
{
  return q->whitebal;
}

int
qc_setwhitebal (struct qcam *q, int val)
{
  if (val >= 0 && val <= 255) {
    q->whitebal = val;
    return 0;
  }
  return 1;
}


/* Gets/sets the resolution */

void
qc_getresolution (const struct qcam *q, int *x, int *y)
{
  *x = q->width;
  *y = q->height;
}

int
qc_setresolution (struct qcam *q, int x, int y)
{
  if (x >= 0 && x <= 336 && y >= 0 && y <= 243) {
    q->width = x;
    q->height = y;
    return 0;
  }
  return 1;
}

int
qc_getheight (const struct qcam *q)
{
  return q->height;
}

int
qc_setheight (struct qcam *q, int y)
{
  if (y >= 0 && y <= 243) {
    q->height = y;
    return 0;
  }
  return 1;
}

int
qc_getwidth (const struct qcam *q)
{
  return q->width;
}

int
qc_setwidth (struct qcam *q, int x)
{
  if (x >= 0 && x <= 336) {
    q->width = x;
    return 0;
  }
  return 1;
}

/* Gets/sets the bit depth */

int
qc_getbitdepth (const struct qcam *q)
{
  return q->bpp;
}

int
qc_setbitdepth (struct qcam *q, int val)
{
  if (val == 4 || val == 6) {
    q->bpp = val;
    return qc_setscanmode (q);
  }
  return 1;
}

int
qc_gettop (const struct qcam *q)
{
  return q->top;
}

int
qc_settop (struct qcam *q, int val)
{
  if (val >= 1 && val <= 243) {
    q->top = val;
    return 0;
  }
  return 1;
}

int
qc_getleft (const struct qcam *q)
{
  return q->left;
}

int
qc_setleft (struct qcam *q, int val)
{
  if (val % 2 == 0 && val >= 2 && val <= 336) {
    q->left = val;
    return 0;
  }
  return 1;
}

int
qc_gettransfer_scale (const struct qcam *q)
{
  return q->transfer_scale;
}

int
qc_settransfer_scale (struct qcam *q, int val)
{
  if (val == 1 || val == 2 || val == 4) {
    q->transfer_scale = val;
    return qc_setscanmode (q);
  }
  return 1;
}

int
qc_calibrate (struct qcam *q)
/* bugfix by Hanno Mueller hmueller@kabel.de, Mai 21 96 */
/* The white balance is an individiual value for each */
/* quickcam. Run calibration once, write the value down */
/* and put it in your qcam.conf file. You won't need to */
/* recalibrate your camera again. */
{
  int value;

#ifdef DEBUG
  int count = 0;
#endif

  qc_command (q, 27);           /* AutoAdjustOffset */
  qc_command (q, 0);            /* Dummy Parameter, ignored by the camera */

  /* GetOffset (33) will read 255 until autocalibration */
  /* is finished. After that, a value of 1-254 will be */
  /* returned. */

  do {
    qc_command (q, 33);
    value = qc_readparam (q);
#ifdef DEBUG
    count++;
#endif
  } while (value == 0xff);

  q->whitebal = value;

#ifdef DEBUG
  fprintf (stderr, "%d loops to calibrate\n", count);
  fprintf (stderr, "Calibrated to %d\n", value);
#endif

  return value;
}

int
qc_forceunidir (struct qcam *q)
{
  q->port_mode = (q->port_mode & ~QC_FORCE_MASK) | QC_FORCE_UNIDIR;
  return 0;
}


/* Initialize the QuickCam driver control structure.  This is where
 * defaults are set for people who don't have a config file.*/
struct qcam *
qc_init (void)
{
  struct qcam *q;

  q = malloc (sizeof (struct qcam));

  q->port = 0;                  /* Port 0 == Autoprobe */
  q->port_mode = (QC_ANY | QC_NOTSET);
  q->width = 160;
  q->height = 120;
  q->bpp = 4;
  q->transfer_scale = 2;
  q->contrast = 104;
  q->brightness = 150;
  q->whitebal = 150;
  q->top = 1;
  q->left = 14;
  q->mode = -1;
  q->fd = -1;                   /* added initialization of fd member
                                 * BTW, there doesn't seem to be a place to close this fd...
                                 * I think we need a qc_free function.
                                 * - Dave Plonka (plonka@carroll1.cc.edu)
                                 */

  return q;
}


/* qc_open enables access to the port specified in q->port.  It takes
 * care of locking and enabling I/O port access by calling the
 * appropriate routines.
 *
 * Returns 0 for success, 1 for opening error, 2 for locking error,
 * and 3 for qcam not found */

int
qc_open (struct qcam *q)
{
  if (q->port == 0)
    if (qc_probe (q)) {
      fprintf (stderr, "Qcam not found\n");
      return 3;
    }

  if (qc_lock (q)) {
    fprintf (stderr, "Cannot lock qcam.\n");
    return 2;
  }

  if (enable_ports (q)) {
    fprintf (stderr, "Cannot open QuickCam -- permission denied.");
    return 1;
  } else {
    return 0;
  }
}


/* qc_close closes and unlocks the driver.  You *need* to call this,
 * or lockfiles will be left behind and everything will be screwed. */

int
qc_close (struct qcam *q)
{
  qc_unlock (q);

  disable_ports (q);
  return 0;
}


/* qc_command is probably a bit of a misnomer -- it's used to send
 * bytes *to* the camera.  Generally, these bytes are either commands
 * or arguments to commands, so the name fits, but it still bugs me a
 * bit.  See the documentation for a list of commands. */

static int
qc_command (const struct qcam *q, int command)
{
  int n1, n2;
  int cmd;

  write_lpdata (q, command);
  write_lpcontrol (q, 6);

  n1 = qc_waithand (q, 1);

  write_lpcontrol (q, 0xe);
  n2 = qc_waithand (q, 0);

  cmd = (n1 & 0xf0) | ((n2 & 0xf0) >> 4);
#ifdef DEBUG
  if (cmd != command) {
    fprintf (stderr, "Command 0x%02x sent, 0x%02x echoed", command, cmd);
    n2 = read_lpstatus (q);
    cmd = (n1 & 0xf0) | ((n2 & 0xf0) >> 4);
    if (cmd != command)
      fprintf (stderr, " (re-read does not help)\n");
    else
      fprintf (stderr, " (fixed on re-read)\n");
  }
#endif
  return cmd;
}

static int
qc_readparam (const struct qcam *q)
{
  int n1, n2;
  int cmd;

  write_lpcontrol (q, 6);
  n1 = qc_waithand (q, 1);

  write_lpcontrol (q, 0xe);
  n2 = qc_waithand (q, 0);

  cmd = (n1 & 0xf0) | ((n2 & 0xf0) >> 4);
  return cmd;
}

/* qc_waithand busy-waits for a handshake signal from the QuickCam.
 * Almost all communication with the camera requires handshaking. */

static int
qc_waithand (const struct qcam *q, int val)
{
  int status;

  if (val)
    while (!((status = read_lpstatus (q)) & 8));
  else
    while (((status = read_lpstatus (q)) & 8));

  return status;
}

/* Waithand2 is used when the qcam is in bidirectional mode, and the
 * handshaking signal is CamRdy2 (bit 0 of data reg) instead of CamRdy1
 * (bit 3 of status register).  It also returns the last value read,
 * since this data is useful. */

static unsigned int
qc_waithand2 (const struct qcam *q, int val)
{
  unsigned int status;

  do {
    status = read_lpdata (q);
  } while ((status & 1) != val);

  return status;
}


/* Try to detect a QuickCam.  It appears to flash the upper 4 bits of
   the status register at 5-10 Hz.  This is only used in the autoprobe
   code.  Be aware that this isn't the way Connectix detects the
   camera (they send a reset and try to handshake), but this should be
   almost completely safe, while their method screws up my printer if
   I plug it in before the camera. */

int
qc_detect (const struct qcam *q)
{
  int reg, lastreg;
  int count = 0;
  int i;

  lastreg = reg = read_lpstatus (q) & 0xf0;

  for (i = 0; i < 30; i++) {
    reg = read_lpstatus (q) & 0xf0;
    if (reg != lastreg)
      count++;
    lastreg = reg;
    usleep (10000);
  }

  /* Be liberal in what you accept...  */

  if (count > 3 && count < 15)
    return 1;                   /* found */
  else
    return 0;                   /* not found */
}


/* Reset the QuickCam.  This uses the same sequence the Windows
 * QuickPic program uses.  Someone with a bi-directional port should
 * check that bi-directional mode is detected right, and then
 * implement bi-directional mode in qc_readbyte(). */

void
qc_reset (struct qcam *q)
{
  switch (q->port_mode & QC_FORCE_MASK) {
    case QC_FORCE_UNIDIR:
      q->port_mode = (q->port_mode & ~QC_MODE_MASK) | QC_UNIDIR;
      break;

    case QC_FORCE_BIDIR:
      q->port_mode = (q->port_mode & ~QC_MODE_MASK) | QC_BIDIR;
      break;

    case QC_ANY:
      write_lpcontrol (q, 0x20);
      write_lpdata (q, 0x75);

      if (read_lpdata (q) != 0x75) {
        q->port_mode = (q->port_mode & ~QC_MODE_MASK) | QC_BIDIR;
      } else {
        q->port_mode = (q->port_mode & ~QC_MODE_MASK) | QC_UNIDIR;
      }
      break;

    case QC_FORCE_SERIAL:
    default:
      fprintf (stderr, "Illegal port_mode %x\n", q->port_mode);
      break;
  }

  /* usleep(250); */
  write_lpcontrol (q, 0xb);
  usleep (250);
  write_lpcontrol (q, 0xe);
  (void) qc_setscanmode (q);    /* in case port_mode changed */
}


/* Decide which scan mode to use.  There's no real requirement that
 * the scanmode match the resolution in q->height and q-> width -- the
 * camera takes the picture at the resolution specified in the
 * "scanmode" and then returns the image at the resolution specified
 * with the resolution commands.  If the scan is bigger than the
 * requested resolution, the upper-left hand corner of the scan is
 * returned.  If the scan is smaller, then the rest of the image
 * returned contains garbage. */

static int
qc_setscanmode (struct qcam *q)
{
  switch (q->transfer_scale) {
    case 1:
      q->mode = 0;
      break;
    case 2:
      q->mode = 4;
      break;
    case 4:
      q->mode = 8;
      break;
    default:
      return 1;
  }

  switch (q->bpp) {
    case 4:
      break;
    case 6:
      q->mode += 2;
      break;
    default:
      fprintf (stderr, "Error: Unsupported bit depth\n");
      return 1;
  }

  switch (q->port_mode & QC_MODE_MASK) {
    case QC_BIDIR:
      q->mode += 1;
      break;
    case QC_NOTSET:
    case QC_UNIDIR:
      break;
    default:
      return 1;
  }
  return 0;
}


/* Reset the QuickCam and program for brightness, contrast,
 * white-balance, and resolution. */

void
qc_set (struct qcam *q)
{
  int val;
  int val2;

  qc_reset (q);

  /* Set the brightness.  Yes, this is repetitive, but it works.
   * Shorter versions seem to fail subtly.  Feel free to try :-). */
  /* I think the problem was in qc_command, not here -- bls */
  qc_command (q, 0xb);
  qc_command (q, q->brightness);

  val = q->height / q->transfer_scale;
  qc_command (q, 0x11);
  qc_command (q, val);
  if ((q->port_mode & QC_MODE_MASK) == QC_UNIDIR && q->bpp == 6) {
    /* The normal "transfers per line" calculation doesn't seem to work
       as expected here (and yet it works fine in qc_scan).  No idea
       why this case is the odd man out.  Fortunately, Laird's original
       working version gives me a good way to guess at working values.
       -- bls */
    val = q->width;
    val2 = q->transfer_scale * 4;
  } else {
    val = q->width * q->bpp;
    val2 = (((q->port_mode & QC_MODE_MASK) == QC_BIDIR) ? 24 : 8) *
        q->transfer_scale;
  }
  val = (val + val2 - 1) / val2;
  qc_command (q, 0x13);
  qc_command (q, val);

  /* I still don't know what these do! */
  /* They're setting top and left -- bls */
  qc_command (q, 0xd);
  qc_command (q, q->top);
  qc_command (q, 0xf);
  qc_command (q, q->left / 2);

  qc_command (q, 0x19);
  qc_command (q, q->contrast);
  qc_command (q, 0x1f);
  qc_command (q, q->whitebal);
}


/* Qc_readbytes reads some bytes from the QC and puts them in
   the supplied buffer.  It returns the number of bytes read,
   or -1 on error. */

static int __inline__
qc_readbytes (const struct qcam *q, char buffer[])
{
  int ret;
  unsigned int hi, lo;
  unsigned int hi2, lo2;
  static unsigned int saved_bits;
  static int state = 0;

  if (buffer == NULL) {
    state = 0;
    return 0;
  }

  switch (q->port_mode & QC_MODE_MASK) {
    case QC_BIDIR:             /* Bi-directional Port */
      write_lpcontrol (q, 0x26);
      lo = (qc_waithand2 (q, 1) >> 1);
      hi = (read_lpstatus (q) >> 3) & 0x1f;
      write_lpcontrol (q, 0x2e);
      lo2 = (qc_waithand2 (q, 0) >> 1);
      hi2 = (read_lpstatus (q) >> 3) & 0x1f;
      switch (q->bpp) {
        case 4:
          buffer[0] = lo & 0xf;
          buffer[1] = ((lo & 0x70) >> 4) | ((hi & 1) << 3);
          buffer[2] = (hi & 0x1e) >> 1;
          buffer[3] = lo2 & 0xf;
          buffer[4] = ((lo2 & 0x70) >> 4) | ((hi2 & 1) << 3);
          buffer[5] = (hi2 & 0x1e) >> 1;
          ret = 6;
          break;
        case 6:
          buffer[0] = lo & 0x3f;
          buffer[1] = ((lo & 0x40) >> 6) | (hi << 1);
          buffer[2] = lo2 & 0x3f;
          buffer[3] = ((lo2 & 0x40) >> 6) | (hi2 << 1);
          ret = 4;
          break;
        default:
          fprintf (stderr, "Bad bidir pixel depth %d\n", q->bpp);
          ret = -1;
          break;
      }
      break;

    case QC_UNIDIR:            /* Unidirectional Port */
      write_lpcontrol (q, 6);
      lo = (qc_waithand (q, 1) & 0xf0) >> 4;
      write_lpcontrol (q, 0xe);
      hi = (qc_waithand (q, 0) & 0xf0) >> 4;

      switch (q->bpp) {
        case 4:
          buffer[0] = lo;
          buffer[1] = hi;
          ret = 2;
          break;
        case 6:
          switch (state) {
            case 0:
              buffer[0] = (lo << 2) | ((hi & 0xc) >> 2);
              saved_bits = (hi & 3) << 4;
              state = 1;
              ret = 1;
              break;
            case 1:
              buffer[0] = lo | saved_bits;
              saved_bits = hi << 2;
              state = 2;
              ret = 1;
              break;
            case 2:
              buffer[0] = ((lo & 0xc) >> 2) | saved_bits;
              buffer[1] = ((lo & 3) << 4) | hi;
              state = 0;
              ret = 2;
              break;
            default:
              fprintf (stderr, "Unidir 6-bit state %d?\n", state);
              ret = -1;
              break;
          }
          break;
        default:
          fprintf (stderr, "Bad unidir pixel depth %d\n", q->bpp);
          ret = -1;
          break;
      }
      break;
    case QC_SERIAL:            /* Serial Interface.  Just in case. */
    default:
      fprintf (stderr, "Mode %x not supported\n", q->port_mode);
      ret = -1;
      break;
  }
  return ret;
}

/* Read a scan from the QC.  This takes the qcam structure and
 * requests a scan from the camera.  It sends the correct instructions
 * to the camera and then reads back the correct number of bytes.  In
 * previous versions of this routine the return structure contained
 * the raw output from the camera, and there was a 'qc_convertscan'
 * function that converted that to a useful format.  In version 0.3 I
 * rolled qc_convertscan into qc_scan and now I only return the
 * converted scan.  The format is just an one-dimensional array of
 * characters, one for each pixel, with 0=black up to n=white, where
 * n=2^(bit depth)-1.  Ask me for more details if you don't understand
 * this. */

scanbuf *
qc_scan (const struct qcam * q)
{
  unsigned char *ret;
  int i, j, k;
  int bytes;
  int linestotrans, transperline;
  int divisor;
  int pixels_per_line;
  int pixels_read;
  char buffer[6];
  char invert;

  if (q->mode != -1) {
    qc_command (q, 0x7);
    qc_command (q, q->mode);
  } else {
    struct qcam bogus_cam;

    /* We're going through these odd hoops to retain the "const"
       qualification on q.  We can't do a qc_setscanmode directly on q,
       so we copy it, do a setscanmode on that, and pass in the newly
       computed mode. -- bls 11/21/96
     */

#ifdef DEBUG
    fprintf (stderr, "Warning!  qc->mode not set!\n");
#endif
    bogus_cam = *q;
    (void) qc_setscanmode (&bogus_cam);
    qc_command (q, 0x7);
    qc_command (q, bogus_cam.mode);
  }

  if ((q->port_mode & QC_MODE_MASK) == QC_BIDIR) {
    write_lpcontrol (q, 0x2e);  /* turn port around */
    write_lpcontrol (q, 0x26);
    (void) qc_waithand (q, 1);
    write_lpcontrol (q, 0x2e);
    (void) qc_waithand (q, 0);
  }

  /* strange -- should be 15:63 below, but 4bpp is odd */
  invert = (q->bpp == 4) ? 16 : 63;

  linestotrans = q->height / q->transfer_scale;
  pixels_per_line = q->width / q->transfer_scale;
  transperline = q->width * q->bpp;
  divisor = (((q->port_mode & QC_MODE_MASK) == QC_BIDIR) ? 24 : 8) *
      q->transfer_scale;
  transperline = (transperline + divisor - 1) / divisor;

  ret = malloc (linestotrans * pixels_per_line);
  assert (ret);

#ifdef DEBUG
  fprintf (stderr, "%s %d bpp\n%d lines of %d transfers each\n",
      ((q->port_mode & QC_MODE_MASK) == QC_BIDIR) ? "Bidir" : "Unidir",
      q->bpp, linestotrans, transperline);
#endif

  for (i = 0; i < linestotrans; i++) {
    for (pixels_read = j = 0; j < transperline; j++) {
      bytes = qc_readbytes (q, buffer);
      assert (bytes > 0);
      for (k = 0; k < bytes && (pixels_read + k) < pixels_per_line; k++) {
        assert (buffer[k] <= invert);
        assert (buffer[k] >= 0);
        if (buffer[k] == 0 && invert == 16) {
          /* 4bpp is odd (again) -- inverter is 16, not 15, but output
             must be 0-15 -- bls */
          buffer[k] = 16;
        }
        ret[i * pixels_per_line + pixels_read + k] = invert - buffer[k];
      }
      pixels_read += bytes;
    }
    (void) qc_readbytes (q, 0); /* reset state machine */
  }

  if ((q->port_mode & QC_MODE_MASK) == QC_BIDIR) {
    write_lpcontrol (q, 2);
    write_lpcontrol (q, 6);
    usleep (3);
    write_lpcontrol (q, 0xe);
  }

  return ret;
}


void
qc_dump (const struct qcam *q, char *fname)
{
  FILE *fp;
  time_t t;

  if ((fp = fopen (fname, "w")) == 0) {
    fprintf (stderr, "Error: cannot open %s\n", fname);
    return;
  }

  fprintf (fp, "# Version 0.9\n");
  time (&t);
  fprintf (fp, "# Created %s", ctime (&t));
  fprintf (fp, "Width %d\nHeight %d\n", q->width, q->height);
  fprintf (fp, "Top %d\nLeft %d\n", q->top, q->left);
  fprintf (fp, "Bpp %d\nContrast %d\n", q->bpp, q->contrast);
  fprintf (fp, "Brightness %d\nWhitebal %d\n", q->brightness, q->whitebal);
  fprintf (fp, "Port 0x%x\nScale %d\n", q->port, q->transfer_scale);
  fclose (fp);
}
