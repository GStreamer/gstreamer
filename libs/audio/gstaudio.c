/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#include "gstaudio.h"

int
gst_audio_frame_byte_size (GstPad* pad)
{
/* calculate byte size of an audio frame
 * this should be moved closer to the gstreamer core
 * and be implemented for every mime type IMO
 * returns 0 if there's an error, or the byte size if everything's ok
 */

  int width = 0;
  int channels = 0;

  GstCaps *caps = NULL;

  /* get caps of pad */
  caps = GST_PAD_CAPS (pad);

  if (caps == NULL)
    /* ERROR: could not get caps of pad */
    return 0;

  width    = gst_caps_get_int (caps, "width");
  channels = gst_caps_get_int (caps, "channels");
  return (width / 8) * channels; 
}

long
gst_audio_frame_length (GstPad* pad, GstBuffer* buf)
/* calculate length of buffer in frames
 * this should be moved closer to the gstreamer core
 * and be implemented for every mime type IMO
 * returns 0 if there's an error, or the number of frames if everything's ok
 */
{
  int frame_byte_size = 0;

  frame_byte_size = gst_audio_frame_byte_size (pad);
  if (frame_byte_size == 0)
    /* error */
    return 0;
  /* FIXME: this function assumes the buffer size to be a whole multiple
   *  	    of the frame byte size
   */
  return GST_BUFFER_SIZE (buf) / frame_byte_size;
}

long
gst_audio_frame_rate (GstPad *pad)
/*
 * calculate frame rate (based on caps of pad)
 * returns 0 if failed, rate if success
 */
{
  GstCaps *caps = NULL;

  /* get caps of pad */
  caps = GST_PAD_CAPS (pad);

  if (caps == NULL)
    /* ERROR: could not get caps of pad */
    return 0;
  else
    return gst_caps_get_int (caps, "rate");
}

double 
gst_audio_length (GstPad* pad, GstBuffer* buf)
{
/* calculate length in seconds
 * of audio buffer buf
 * based on capabilities of pad
 */

  long bytes = 0;
  int width = 0;
  int channels = 0;
  long rate = 0L;

  double length;

  GstCaps *caps = NULL;

  /* get caps of pad */
  caps = GST_PAD_CAPS (pad);
  if (caps == NULL)
  {
    /* ERROR: could not get caps of pad */
    length = 0.0;
  }
  else
  {
    bytes = GST_BUFFER_SIZE (buf);
    width    = gst_caps_get_int (caps, "width");
    channels = gst_caps_get_int (caps, "channels");
    rate     = gst_caps_get_int (caps, "rate");

    length = (bytes * 8.0) / (double) (rate * channels * width);
  }
  return length;
}

long 
gst_audio_highest_sample_value (GstPad* pad)
/* calculate highest possible sample value
 * based on capabilities of pad
 */
{
  gboolean is_signed = FALSE;
  gint width = 0;
  GstCaps *caps = NULL;
  
  caps = GST_PAD_CAPS (pad);
    // FIXME : Please change this to a better warning method !
  if (caps == NULL)
    printf ("WARNING: gstaudio: could not get caps of pad !\n");
  width = gst_caps_get_int (caps, "width");
  is_signed = gst_caps_get_boolean (caps, "signed");
  if (is_signed) --width;
  /* example : 16 bit, signed : samples between -32768 and 32767 */
  return ((long) (1 << width));
}

gboolean 
gst_audio_is_buffer_framed (GstPad* pad, GstBuffer* buf)
/* check if the buffer size is a whole multiple of the frame size */
{
  if (GST_BUFFER_SIZE (buf) % gst_audio_frame_byte_size (pad) == 0)
    return TRUE;
  else
    return FALSE;
}
