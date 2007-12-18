/* GStreamer AVI GAB2 subtitle parser
 * Copyright (C) <2007> Thijs Vermeir <thijsvermeir@gmail.com>
 * Copyright (C) <2007> Tim-Philipp Müller <tim centricular net>
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

/* FIXME: BOM detection and format conversion; validate UTF-8; handle seeks */

/* example of a subtitle chunk in an avi file
 * 00000000: 47 41 42 32 00 02 00 10 00 00 00 45 00 6e 00 67  GAB2.......E.n.g
 * 00000010: 00 6c 00 69 00 73 00 68 00 00 00 04 00 8e 00 00  .l.i.s.h........
 * 00000020: 00 ef bb bf 31 0d 0a 30 30 3a 30 30 3a 30 30 2c  ....1..00:00:00,
 * 00000030: 31 30 30 20 2d 2d 3e 20 30 30 3a 30 30 3a 30 32  100 --> 00:00:02
 * 00000040: 2c 30 30 30 0d 0a 3c 62 3e 41 6e 20 55 54 46 38  ,000..<b>An UTF8
 * 00000050: 20 53 75 62 74 69 74 6c 65 20 77 69 74 68 20 42   Subtitle with B
 * 00000060: 4f 4d 3c 2f 62 3e 0d 0a 0d 0a 32 0d 0a 30 30 3a  OM</b>....2..00:
 * 00000070: 30 30 3a 30 32 2c 31 30 30 20 2d 2d 3e 20 30 30  00:02,100 --> 00
 * 00000080: 3a 30 30 3a 30 34 2c 30 30 30 0d 0a 53 6f 6d 65  :00:04,000..Some
 * 00000090: 74 68 69 6e 67 20 6e 6f 6e 41 53 43 49 49 20 2d  thing nonASCII -
 * 000000a0: 20 c2 b5 c3 b6 c3 a4 c3 bc c3 9f 0d 0a 0d 0a      ..............
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstavisubtitle.h"

GST_DEBUG_CATEGORY_STATIC (avisubtitle_debug);
#define GST_CAT_DEFAULT avisubtitle_debug

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-subtitle-avi")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-subtitle")
    );

static GstFlowReturn gst_avi_subtitle_chain (GstPad * pad, GstBuffer * buffer);
static GstStateChangeReturn gst_avi_subtitle_change_state (GstElement * element,
    GstStateChange transition);

GST_BOILERPLATE (GstAviSubtitle, gst_avi_subtitle, GstElement,
    GST_TYPE_ELEMENT);

static GstBuffer *
gst_avi_subtitle_extract_utf8_file (GstBuffer * buffer, guint offset, guint len)
{
  guint8 *file = GST_BUFFER_DATA (buffer) + offset;

  if (file[0] == 0xEF && file[1] == 0xBB && file[2] == 0xBF) {
    /* UTF-8 */
    return gst_buffer_create_sub (buffer, offset + 3, len - 3);
  }
  /* TODO Check for:
   * 00 00 FE FF    UTF-32, big-endian
   * FF FE 00 00    UTF-32, little-endian
   * FE FF          UTF-16, big-endian
   * FF FE          UTF-16, little-endian
   */

  /* No BOM detected assuming UTF-8 */
  return gst_buffer_create_sub (buffer, offset, len);
}

static GstFlowReturn
gst_avi_subtitle_parse_gab2_chunk (GstAviSubtitle * sub, GstBuffer * buf)
{
  const guint8 *data;
  gchar *name_utf8;
  guint name_length;
  guint file_length;
  guint size;

  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  /* check the magic word "GAB2\0", and the next word must be 2 */
  if (size < 12 || memcmp (data, "GAB2\0\2\0", 5 + 2) != 0)
    goto wrong_magic_word;

  /* read 'name' of subtitle */
  name_length = GST_READ_UINT32_LE (data + 5 + 2);
  GST_LOG_OBJECT (sub, "length of name: %u", name_length);
  if (size <= 17 + name_length)
    goto wrong_name_length;

  name_utf8 = g_convert ((gchar *) data + 11, name_length, "UTF-8", "UTF-16LE",
      NULL, NULL, NULL);

  if (name_utf8) {
    /* FIXME: put in a taglist */
    GST_LOG_OBJECT (sub, "subtitle name: %s", name_utf8);
    g_free (name_utf8);
  }

  /* next word must be 4 */
  if (GST_READ_UINT16_LE (data + 11 + name_length) != 0x4)
    goto wrong_fixed_word_2;

  file_length = GST_READ_UINT32_LE (data + 13 + name_length);
  GST_LOG_OBJECT (sub, "length srt/ssa file: %u", file_length);

  if (size < (17 + name_length + file_length))
    goto wrong_total_length;

  /* store this, so we can send it again after a seek; note that we shouldn't
   * assume all the remaining data in the chunk is subtitle data, there may
   * be padding at the end for some reason, so only parse file_length bytes */
  sub->subfile =
      gst_avi_subtitle_extract_utf8_file (buf, 17 + name_length, file_length);

  return GST_FLOW_OK;

  /* ERRORS */
wrong_magic_word:
  {
    GST_ELEMENT_ERROR (sub, STREAM, DECODE, (NULL), ("Wrong magic word"));
    return GST_FLOW_ERROR;
  }
wrong_name_length:
  {
    GST_ELEMENT_ERROR (sub, STREAM, DECODE, (NULL),
        ("name doesn't fit in buffer (%d < %d)", size, 17 + name_length));
    return GST_FLOW_ERROR;
  }
wrong_fixed_word_2:
  {
    GST_ELEMENT_ERROR (sub, STREAM, DECODE, (NULL),
        ("wrong fixed word: expected %u, got %u", 4,
            GST_READ_UINT16_LE (data + 11 + name_length)));
    return GST_FLOW_ERROR;
  }
wrong_total_length:
  {
    GST_ELEMENT_ERROR (sub, STREAM, DECODE, (NULL),
        ("buffer size is wrong: need %d bytes, have %d bytes",
            17 + name_length + file_length, size));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_avi_subtitle_chain (GstPad * pad, GstBuffer * buffer)
{
  GstAviSubtitle *sub = GST_AVI_SUBTITLE (GST_PAD_PARENT (pad));
  GstFlowReturn ret;

  if (sub->subfile != NULL) {
    GST_WARNING_OBJECT (sub, "Got more buffers than expected, dropping");
    ret = GST_FLOW_UNEXPECTED;
    goto done;
  }

  /* we expect exactly one buffer with the whole srt/ssa file in it */
  ret = gst_avi_subtitle_parse_gab2_chunk (sub, buffer);
  if (ret != GST_FLOW_OK)
    goto done;

  /* now push the subtitle data downstream */
  ret = gst_pad_push (sub->src, gst_buffer_ref (sub->subfile));

done:

  gst_buffer_unref (buffer);
  return ret;
}

static void
gst_avi_subtitle_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (avisubtitle_debug, "avisubtitle", 0,
      "parse avi subtitle stream");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_details_simple (element_class,
      "Avi subtitle parser", "Codec/Demuxer", "Parse avi subtitle stream",
      "Thijs Vermeir <thijsvermeir@gmail.com>");
}

static void
gst_avi_subtitle_class_init (GstAviSubtitleClass * klass)
{
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_avi_subtitle_change_state);
}

static void
gst_avi_subtitle_init (GstAviSubtitle * self, GstAviSubtitleClass * klass)
{
  self->src = gst_pad_new_from_static_template (&src_template, "src");
  gst_element_add_pad (GST_ELEMENT (self), self->src);

  self->sink = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_chain_function (self->sink,
      GST_DEBUG_FUNCPTR (gst_avi_subtitle_chain));
  gst_element_add_pad (GST_ELEMENT (self), self->sink);
}

static GstStateChangeReturn
gst_avi_subtitle_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstAviSubtitle *sub = GST_AVI_SUBTITLE (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (sub->subfile) {
        gst_buffer_unref (sub->subfile);
        sub->subfile = NULL;
      }
      break;
    default:
      break;
  }

  return ret;
}
