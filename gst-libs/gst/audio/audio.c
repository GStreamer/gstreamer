/* GStreamer
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "audio.h"

#include <gst/gststructure.h>

int
gst_audio_frame_byte_size (GstPad * pad)
{
/* calculate byte size of an audio frame
 * this should be moved closer to the gstreamer core
 * and be implemented for every mime type IMO
 * returns -1 if there's an error (to avoid division by zero), 
 * or the byte size if everything's ok
 */

  int width = 0;
  int channels = 0;
  const GstCaps *caps = NULL;
  GstStructure *structure;

  /* get caps of pad */
  caps = GST_PAD_CAPS (pad);

  if (caps == NULL) {
    /* ERROR: could not get caps of pad */
    g_warning ("gstaudio: could not get caps of pad %s:%s\n",
        GST_ELEMENT_NAME (gst_pad_get_parent (pad)), GST_PAD_NAME (pad));
    return 0;
  }

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "channels", &channels);
  return (width / 8) * channels;
}

long
gst_audio_frame_length (GstPad * pad, GstBuffer * buf)
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
   *        of the frame byte size
   */
  return GST_BUFFER_SIZE (buf) / frame_byte_size;
}

long
gst_audio_frame_rate (GstPad * pad)
/*
 * calculate frame rate (based on caps of pad)
 * returns 0 if failed, rate if success
 */
{
  const GstCaps *caps = NULL;
  gint rate;
  GstStructure *structure;

  /* get caps of pad */
  caps = GST_PAD_CAPS (pad);

  if (caps == NULL) {
    /* ERROR: could not get caps of pad */
    g_warning ("gstaudio: could not get caps of pad %s:%s\n",
        GST_ELEMENT_NAME (gst_pad_get_parent (pad)), GST_PAD_NAME (pad));
    return 0;
  } else {
    structure = gst_caps_get_structure (caps, 0);
    gst_structure_get_int (structure, "rate", &rate);
    return rate;
  }
}

double
gst_audio_length (GstPad * pad, GstBuffer * buf)
{
/* calculate length in seconds
 * of audio buffer buf
 * based on capabilities of pad
 */

  long bytes = 0;
  int width = 0;
  int channels = 0;
  int rate = 0;

  double length;

  const GstCaps *caps = NULL;
  GstStructure *structure;

  g_assert (GST_IS_BUFFER (buf));
  /* get caps of pad */
  caps = GST_PAD_CAPS (pad);
  if (caps == NULL) {
    /* ERROR: could not get caps of pad */
    g_warning ("gstaudio: could not get caps of pad %s:%s\n",
        GST_ELEMENT_NAME (gst_pad_get_parent (pad)), GST_PAD_NAME (pad));
    length = 0.0;
  } else {
    structure = gst_caps_get_structure (caps, 0);
    bytes = GST_BUFFER_SIZE (buf);
    gst_structure_get_int (structure, "width", &width);
    gst_structure_get_int (structure, "channels", &channels);
    gst_structure_get_int (structure, "rate", &rate);

    g_assert (bytes != 0);
    g_assert (width != 0);
    g_assert (channels != 0);
    g_assert (rate != 0);
    length = (bytes * 8.0) / (double) (rate * channels * width);
  }
  /* g_print ("DEBUG: audio: returning length of %f\n", length); */
  return length;
}

long
gst_audio_highest_sample_value (GstPad * pad)
/* calculate highest possible sample value
 * based on capabilities of pad
 */
{
  gboolean is_signed = FALSE;
  gint width = 0;
  const GstCaps *caps = NULL;
  GstStructure *structure;

  caps = GST_PAD_CAPS (pad);
  if (caps == NULL) {
    g_warning ("gstaudio: could not get caps of pad %s:%s\n",
        GST_ELEMENT_NAME (gst_pad_get_parent (pad)), GST_PAD_NAME (pad));
  }

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_boolean (structure, "signed", &is_signed);

  if (is_signed)
    --width;
  /* example : 16 bit, signed : samples between -32768 and 32767 */
  return ((long) (1 << width));
}

gboolean
gst_audio_is_buffer_framed (GstPad * pad, GstBuffer * buf)
/* check if the buffer size is a whole multiple of the frame size */
{
  if (GST_BUFFER_SIZE (buf) % gst_audio_frame_byte_size (pad) == 0)
    return TRUE;
  else
    return FALSE;
}

/* _getcaps helper functions
 * sets structure fields to default for audio type
 * flag determines which structure fields to set to default
 * keep these functions in sync with the templates in audio.h
 */

/* private helper function
 * sets a list on the structure
 * pass in structure, fieldname for the list, type of the list values,
 * number of list values, and each of the values, terminating with NULL
 */
static void
_gst_audio_structure_set_list (GstStructure * structure,
    const gchar * fieldname, GType type, int number, ...)
{
  va_list varargs;
  GValue value = { 0 };
  GArray *array;
  int j;

  g_return_if_fail (structure != NULL);

  g_value_init (&value, GST_TYPE_LIST);
  array = g_value_peek_pointer (&value);

  va_start (varargs, number);

  for (j = 0; j < number; ++j) {
    int i;
    gboolean b;

    GValue list_value = { 0 };

    switch (type) {
      case G_TYPE_INT:
        i = va_arg (varargs, int);

        g_value_init (&list_value, G_TYPE_INT);
        g_value_set_int (&list_value, i);
        break;
      case G_TYPE_BOOLEAN:
        b = va_arg (varargs, gboolean);
        g_value_init (&list_value, G_TYPE_BOOLEAN);
        g_value_set_boolean (&list_value, b);
        break;
      default:
        g_warning
            ("_gst_audio_structure_set_list: LIST of given type not implemented.");
    }
    g_array_append_val (array, list_value);

  }
  gst_structure_set_value (structure, fieldname, &value);
  va_end (varargs);
}

void
gst_audio_structure_set_int (GstStructure * structure, GstAudioFieldFlag flag)
{
  if (flag & GST_AUDIO_FIELD_RATE)
    gst_structure_set (structure, "rate", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        NULL);
  if (flag & GST_AUDIO_FIELD_CHANNELS)
    gst_structure_set (structure, "channels", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        NULL);
  if (flag & GST_AUDIO_FIELD_ENDIANNESS)
    _gst_audio_structure_set_list (structure, "endianness", G_TYPE_INT, 2,
        G_LITTLE_ENDIAN, G_BIG_ENDIAN, NULL);
  if (flag & GST_AUDIO_FIELD_WIDTH)
    _gst_audio_structure_set_list (structure, "width", G_TYPE_INT, 3, 8, 16, 32,
        NULL);
  if (flag & GST_AUDIO_FIELD_DEPTH)
    gst_structure_set (structure, "depth", GST_TYPE_INT_RANGE, 1, 32, NULL);
  if (flag & GST_AUDIO_FIELD_SIGNED)
    _gst_audio_structure_set_list (structure, "signed", G_TYPE_BOOLEAN, 2, TRUE,
        FALSE, NULL);
  if (flag & GST_AUDIO_FIELD_BUFFER_FRAMES)
    gst_structure_set (structure, "buffer-frames", GST_TYPE_INT_RANGE, 1,
        G_MAXINT, NULL);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "gstaudio",
    "Support services for audio plugins",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN);
