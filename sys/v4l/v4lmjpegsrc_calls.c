/* G-Streamer hardware MJPEG video source plugin
 * Copyright (C) 2001-2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

//#define DEBUG

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include "v4lmjpegsrc_calls.h"

/* On some systems MAP_FAILED seems to be missing */
#ifndef MAP_FAILED
#define MAP_FAILED ( (caddr_t) -1 )
#endif

char *input_name[] = { "Composite", "S-Video", "TV-Tuner", "Autodetect" };


/******************************************************
 * gst_v4lmjpegsrc_queue_frame():
 *   queue a frame for capturing
 * return value: TRUE on success, FALSE on error
 ******************************************************/

static gboolean
gst_v4lmjpegsrc_queue_frame (GstV4lMjpegSrc *v4lmjpegsrc,
                             gint           num)
{
#ifdef DEBUG
  fprintf(stderr, "V4LMJPEGSRC: gst_v4lmjpegsrc_queue_frame(), num = %d\n",
    num);
#endif

  if (ioctl(GST_V4LELEMENT(v4lmjpegsrc)->video_fd, MJPIOC_QBUF_CAPT, &num) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lmjpegsrc),
      "Error queueing a buffer (%d): %s",
      num, sys_errlist[errno]);
    return FALSE;
  }

  return TRUE;
}


/******************************************************
 * gst_v4lmjpegsrc_sync_next_frame():
 *   sync on the next frame for capturing
 * return value: TRUE on success, FALSE on error
 ******************************************************/

static gboolean
gst_v4lmjpegsrc_sync_next_frame (GstV4lMjpegSrc *v4lmjpegsrc,
                                 gint           *num)
{
#ifdef DEBUG
  fprintf(stderr, "V4LMJPEGSRC: gst_v4lmjpegsrc_sync_frame(), num = %d\n",
    num);
#endif

  if (ioctl(GST_V4LELEMENT(v4lmjpegsrc)->video_fd, MJPIOC_SYNC, &(v4lmjpegsrc->bsync)) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lmjpegsrc),
      "Error syncing on a buffer (%ld): %s",
      v4lmjpegsrc->bsync.frame, sys_errlist[errno]);
    return FALSE;
  }

  *num = v4lmjpegsrc->bsync.frame;

  return TRUE;
}


/******************************************************
 * gst_v4lmjpegsrc_set_input_norm():
 *   set input/norm (includes autodetection), norm is
 *   VIDEO_MODE_{PAL|NTSC|SECAM|AUTO}
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lmjpegsrc_set_input_norm (GstV4lMjpegSrc       *v4lmjpegsrc,
                                GstV4lMjpegInputType input,
                                gint                 norm)
{
  struct mjpeg_status bstat;

#ifdef DEBUG
  fprintf(stderr, "V4LMJPEGSRC: gst_v4lmjpegsrc_set_input_norm(), input = %d (%s), norm = %d (%s)\n",
    input, input_name[input], norm, norm_name[norm]);
#endif

  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lmjpegsrc));
  GST_V4L_CHECK_NOT_ACTIVE(GST_V4LELEMENT(v4lmjpegsrc));

  if (input == V4L_MJPEG_INPUT_AUTO)
  {
    int n;

    for (n=V4L_MJPEG_INPUT_COMPOSITE;n<V4L_MJPEG_INPUT_AUTO;n++)
    {
      gst_element_info(GST_ELEMENT(v4lmjpegsrc),
        "Trying %s as input...",
        input_name[n]);
      bstat.input = n;

      if (ioctl(GST_V4LELEMENT(v4lmjpegsrc)->video_fd, MJPIOC_G_STATUS, &bstat) < 0)
      {
        gst_element_error(GST_ELEMENT(v4lmjpegsrc),
          "Error getting device status: %s",
          sys_errlist[errno]);
        return FALSE;
      }

      if (bstat.signal)
      {
        input = bstat.input;
        if (norm == VIDEO_MODE_AUTO)
          norm = bstat.norm;
        gst_element_info(GST_ELEMENT(v4lmjpegsrc),
          "Signal found: on input %s, norm %s",
          input_name[bstat.input], norm_name[bstat.norm]);
        break;
      }
    }

    /* check */
    if (input == V4L_MJPEG_INPUT_AUTO || norm == VIDEO_MODE_AUTO)
    {
      gst_element_error(GST_ELEMENT(v4lmjpegsrc),
        "Unable to auto-detect an input");
      return FALSE;
    }

    /* save */
    GST_V4LELEMENT(v4lmjpegsrc)->channel = input;
    GST_V4LELEMENT(v4lmjpegsrc)->norm = norm;
  }
  else if (norm == VIDEO_MODE_AUTO && input)
  {
    bstat.input = input;

    if (ioctl(GST_V4LELEMENT(v4lmjpegsrc)->video_fd, MJPIOC_G_STATUS, &bstat) < 0)
    {
      gst_element_error(GST_ELEMENT(v4lmjpegsrc),
        "Error getting device status: %s",
        sys_errlist[errno]);
      return FALSE;
    }

    if (bstat.signal)
    {
      norm = bstat.norm;
      gst_element_info(GST_ELEMENT(v4lmjpegsrc),
        "Norm %s detected on input %s",
        norm_name[bstat.norm], input_name[input]);
      GST_V4LELEMENT(v4lmjpegsrc)->norm = norm;
    }
    else
    {
      gst_element_error(GST_ELEMENT(v4lmjpegsrc),
        "No signal found on input %s",
        input_name[input]);
      return FALSE;
    }
  }

  return gst_v4l_set_chan_norm(GST_V4LELEMENT(v4lmjpegsrc), input, norm);
}


/******************************************************
 * gst_v4lmjpegsrc_set_buffer():
 *   set buffer parameters (size/count)
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lmjpegsrc_set_buffer (GstV4lMjpegSrc *v4lmjpegsrc,
                            gint           numbufs,
                            gint           bufsize)
{
#ifdef DEBUG
  fprintf(stderr, "V4LMJPEGSRC: gst_v4lmjpegsrc_set_buffer(), numbufs = %d, bufsize = %d KB\n",
    numbufs, bufsize);
#endif

  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lmjpegsrc));
  GST_V4L_CHECK_NOT_ACTIVE(GST_V4LELEMENT(v4lmjpegsrc));

  v4lmjpegsrc->breq.size = bufsize * 1024;
  v4lmjpegsrc->breq.count = numbufs;

  return TRUE;
}


/******************************************************
 * gst_v4lmjpegsrc_set_capture():
 *   set capture parameters (simple)
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lmjpegsrc_set_capture (GstV4lMjpegSrc *v4lmjpegsrc,
                             gint           decimation,
                             gint           quality)
{
  int norm, input, mw;
  struct mjpeg_params bparm;

#ifdef DEBUG
  fprintf(stderr, "V4LMJPEGSRC: gst_v4lmjpegsrc_set_capture(), decimation = %d, quality = %d\n",
    decimation, quality);
#endif

  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lmjpegsrc));
  GST_V4L_CHECK_NOT_ACTIVE(GST_V4LELEMENT(v4lmjpegsrc));

  gst_v4l_get_chan_norm(GST_V4LELEMENT(v4lmjpegsrc), &input, &norm);

  /* Query params for capture */
  if (ioctl(GST_V4LELEMENT(v4lmjpegsrc)->video_fd, MJPIOC_G_PARAMS, &bparm) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lmjpegsrc),
      "Error getting video parameters: %s",
      sys_errlist[errno]);
    return FALSE;
  }

  bparm.decimation = decimation;
  bparm.quality = quality;
  bparm.norm = norm;
  bparm.input = input;
  bparm.APP_len = 0; /* no JPEG markers - TODO: this is definately not right for decimation==1 */

  mw = GST_V4LELEMENT(v4lmjpegsrc)->vcap.maxwidth;
  if (mw != 768 && mw != 640)
  {
    if (decimation == 1)
      mw = 720;
    else
      mw = 704;
  }
  v4lmjpegsrc->end_width = mw / decimation;
  v4lmjpegsrc->end_height = (norm==VIDEO_MODE_NTSC?480:576) / decimation;

  /* TODO: interlacing */

  /* Set params for capture */
  if (ioctl(GST_V4LELEMENT(v4lmjpegsrc)->video_fd, MJPIOC_S_PARAMS, &bparm) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lmjpegsrc),
      "Error setting video parameters: %s",
      sys_errlist[errno]);
    return FALSE;
  }

  return TRUE;
}


/******************************************************
 * gst_v4lmjpegsrc_set_capture_m():
 *   set capture parameters (advanced)
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean gst_v4lmjpegsrc_set_capture_m (GstV4lMjpegSrc *v4lmjpegsrc,
                                        gint           x_offset,
                                        gint           y_offset,
                                        gint           width,
                                        gint           height,
                                        gint           h_decimation,
                                        gint           v_decimation,
                                        gint           quality)
{
  gint norm, input;
  gint maxwidth;
  struct mjpeg_params bparm;

#ifdef DEBUG
  fprintf(stderr, "V4LMJPEGSRC: gst_v4lmjpegsrc_set_capture_m(), x_offset = %d, y_offset = %d, "
    "width = %d, height = %d, h_decimation = %d, v_decimation = %d, quality = %d\n",
    x_offset, y_offset, width, height, h_decimation, v_decimation, quality);
#endif

  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lmjpegsrc));
  GST_V4L_CHECK_NOT_ACTIVE(GST_V4LELEMENT(v4lmjpegsrc));

  gst_v4l_get_chan_norm(GST_V4LELEMENT(v4lmjpegsrc), &input, &norm);

  if (GST_V4LELEMENT(v4lmjpegsrc)->vcap.maxwidth != 768 &&
    GST_V4LELEMENT(v4lmjpegsrc)->vcap.maxwidth != 640)
    maxwidth = 720;
  else
    maxwidth = GST_V4LELEMENT(v4lmjpegsrc)->vcap.maxwidth;

  /* Query params for capture */
  if (ioctl(GST_V4LELEMENT(v4lmjpegsrc)->video_fd, MJPIOC_G_PARAMS, &bparm) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lmjpegsrc),
      "Error getting video parameters: %s",
      sys_errlist[errno]);
    return FALSE;
  }

  bparm.decimation = 0;
  bparm.quality = quality;
  bparm.norm = norm;
  bparm.input = input;
  bparm.APP_len = 0; /* no JPEG markers - TODO: this is definately not right for decimation==1 */

  if (width <= 0)
  {
    if (x_offset < 0) x_offset = 0;
    width = (maxwidth==720&&h_decimation!=1)?704:maxwidth - 2*x_offset;
  }
  else
  {
    if (x_offset < 0)
      x_offset = (maxwidth - width)/2;
  }

  if (height <= 0)
  {
    if (y_offset < 0) y_offset = 0;
    height = (norm==VIDEO_MODE_NTSC)?480:576 - 2*y_offset;
  }
  else
  {
    if (y_offset < 0)
      y_offset = ((norm==VIDEO_MODE_NTSC)?480:576 - height)/2;
  }

  if (width + x_offset > maxwidth)
  {
    gst_element_error(GST_ELEMENT(v4lmjpegsrc),
      "Image width+offset (%d) bigger than maximum (%d)",
      width + x_offset, maxwidth);
    return FALSE;
  }
  if ((width%(bparm.HorDcm*16))!=0) 
  {
    gst_element_error(GST_ELEMENT(v4lmjpegsrc),
      "Image width (%d) not multiple of %d (required for JPEG)",
      width, bparm.HorDcm*16);
    return FALSE;
  }
  if (height + y_offset > (norm==VIDEO_MODE_NTSC ? 480 : 576)) 
  {
    gst_element_error(GST_ELEMENT(v4lmjpegsrc),
      "Image height+offset (%d) bigger than maximum (%d)",
      height + y_offset, (norm==VIDEO_MODE_NTSC ? 480 : 576));
    return FALSE;
  }
  /* RJ: Image height must only be a multiple of 8, but geom_height
   * is double the field height
   */
  if ((height%(bparm.VerDcm*16))!=0) 
  {
    gst_element_error(GST_ELEMENT(v4lmjpegsrc),
      "Image height (%d) not multiple of %d (required for JPEG)",
      height, bparm.VerDcm*16);
    return FALSE;
  }

  bparm.img_x = x_offset;
  bparm.img_width = width;
  bparm.img_y = y_offset;
  bparm.img_height = height;
  bparm.HorDcm = h_decimation;
  bparm.VerDcm = (v_decimation==4) ? 2 : 1;
  bparm.TmpDcm = (v_decimation==1) ? 1 : 2;
  bparm.field_per_buff = (v_decimation==1) ? 2 : 1;

  v4lmjpegsrc->end_width = width / h_decimation;
  v4lmjpegsrc->end_width = height / v_decimation;

  /* TODO: interlacing */

  /* Set params for capture */
  if (ioctl(GST_V4LELEMENT(v4lmjpegsrc)->video_fd, MJPIOC_S_PARAMS, &bparm) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lmjpegsrc),
      "Error setting video parameters: %s",
      sys_errlist[errno]);
    return FALSE;
  }

  return TRUE;
}


/******************************************************
 * gst_v4lmjpegsrc_capture_init():
 *   initialize the capture system
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lmjpegsrc_capture_init (GstV4lMjpegSrc *v4lmjpegsrc)
{
#ifdef DEBUG
  fprintf(stderr, "V4LMJPEGSRC: gst_v4lmjpegsrc_capture_init()\n");
#endif

  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lmjpegsrc));
  GST_V4L_CHECK_NOT_ACTIVE(GST_V4LELEMENT(v4lmjpegsrc));

  /* Request buffers */
  if (ioctl(GST_V4LELEMENT(v4lmjpegsrc)->video_fd, MJPIOC_REQBUFS, &(v4lmjpegsrc->breq)) < 0)
  {
    gst_element_error(GST_ELEMENT(v4lmjpegsrc),
      "Error requesting video buffers: %s",
      sys_errlist[errno]);
    return FALSE;
  }

  gst_element_info(GST_ELEMENT(v4lmjpegsrc),
    "Got %ld buffers of size %ld KB",
    v4lmjpegsrc->breq.count, v4lmjpegsrc->breq.size/1024);

  /* Map the buffers */
  GST_V4LELEMENT(v4lmjpegsrc)->buffer = mmap(0,
    v4lmjpegsrc->breq.count * v4lmjpegsrc->breq.size, 
    PROT_READ, MAP_SHARED, GST_V4LELEMENT(v4lmjpegsrc)->video_fd, 0);
  if (GST_V4LELEMENT(v4lmjpegsrc)->buffer == MAP_FAILED)
  {
    gst_element_error(GST_ELEMENT(v4lmjpegsrc),
      "Error mapping video buffers: %s",
      sys_errlist[errno]);
    GST_V4LELEMENT(v4lmjpegsrc)->buffer = NULL;
    return FALSE;
  }

  return TRUE;
}


/******************************************************
 * gst_v4lmjpegsrc_capture_start():
 *   start streaming capture
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lmjpegsrc_capture_start (GstV4lMjpegSrc *v4lmjpegsrc)
{
  int n;

#ifdef DEBUG
  fprintf(stderr, "V4LMJPEGSRC: gst_v4lmjpegsrc_capture_start()\n");
#endif

  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lmjpegsrc));
  GST_V4L_CHECK_ACTIVE(GST_V4LELEMENT(v4lmjpegsrc));

  /* queue'ing the buffers starts streaming capture */
  for (n=0;n<v4lmjpegsrc->breq.count;n++)
    if (!gst_v4lmjpegsrc_queue_frame(v4lmjpegsrc, n))
      return FALSE;

  return TRUE;
}


/******************************************************
 * gst_v4lmjpegsrc_grab_frame():
 *   grab one frame during streaming capture
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lmjpegsrc_grab_frame (GstV4lMjpegSrc *v4lmjpegsrc,
                            gint           *num,
                            gint           *size)
{
#ifdef DEBUG
  fprintf(stderr, "V4LMJPEGSRC: gst_v4lmjpegsrc_grab_frame()\n");
#endif

  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lmjpegsrc));
  GST_V4L_CHECK_ACTIVE(GST_V4LELEMENT(v4lmjpegsrc));

  /* syncing on the buffer grabs it */
  if (!gst_v4lmjpegsrc_sync_next_frame(v4lmjpegsrc, num))
    return FALSE;

  *size = v4lmjpegsrc->bsync.length;

  return TRUE;
}


/******************************************************
 * gst_v4lmjpegsrc_get_buffer():
 *   get the memory address of a single buffer
 * return value: TRUE on success, FALSE on error
 ******************************************************/

guint8 *
gst_v4lmjpegsrc_get_buffer (GstV4lMjpegSrc *v4lmjpegsrc,
                            gint           num)
{
#ifdef DEBUG
  fprintf(stderr, "V4LMJPEGSRC: gst_v4lmjpegsrc_get_buffer(), num = %d\n",
    num);
#endif

  if (!GST_V4L_IS_ACTIVE(GST_V4LELEMENT(v4lmjpegsrc)) ||
      !GST_V4L_IS_OPEN(GST_V4LELEMENT(v4lmjpegsrc)))
    return NULL;

  if (num < 0 || num >= v4lmjpegsrc->breq.count)
    return NULL;

  return GST_V4LELEMENT(v4lmjpegsrc)->buffer+(v4lmjpegsrc->breq.size*num);
}


/******************************************************
 * gst_v4lmjpegsrc_requeue_frame():
 *   requeue a frame for capturing
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lmjpegsrc_requeue_frame (GstV4lMjpegSrc *v4lmjpegsrc,
                               gint           num)
{
#ifdef DEBUG
  fprintf(stderr, "V4LMJPEGSRC: gst_v4lmjpegsrc_requeue_frame(), num = %d\n",
    num);
#endif

  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lmjpegsrc));
  GST_V4L_CHECK_ACTIVE(GST_V4LELEMENT(v4lmjpegsrc));

  if (!gst_v4lmjpegsrc_queue_frame(v4lmjpegsrc, num))
    return FALSE;

  return TRUE;
}


/******************************************************
 * gst_v4lmjpegsrc_capture_stop():
 *   stop streaming capture
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lmjpegsrc_capture_stop (GstV4lMjpegSrc *v4lmjpegsrc)
{
#ifdef DEBUG
  fprintf(stderr, "V4LMJPEGSRC: gst_v4lmjpegsrc_capture_stop()\n");
#endif

  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lmjpegsrc));
  GST_V4L_CHECK_ACTIVE(GST_V4LELEMENT(v4lmjpegsrc));

  /* unqueue the buffers */
  if (!gst_v4lmjpegsrc_queue_frame(v4lmjpegsrc, -1))
    return FALSE;

  return TRUE;
}


/******************************************************
 * gst_v4lmjpegsrc_capture_deinit():
 *   deinitialize the capture system
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4lmjpegsrc_capture_deinit (GstV4lMjpegSrc *v4lmjpegsrc)
{
#ifdef DEBUG
  fprintf(stderr, "V4LMJPEGSRC: gst_v4lmjpegsrc_capture_deinit()\n");
#endif

  GST_V4L_CHECK_OPEN(GST_V4LELEMENT(v4lmjpegsrc));
  GST_V4L_CHECK_ACTIVE(GST_V4LELEMENT(v4lmjpegsrc));

  /* unmap the buffer */
  munmap(GST_V4LELEMENT(v4lmjpegsrc)->buffer, v4lmjpegsrc->breq.size * v4lmjpegsrc->breq.count);
  GST_V4LELEMENT(v4lmjpegsrc)->buffer = NULL;

  return TRUE;
}
