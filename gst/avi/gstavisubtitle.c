/* GStreamer
 * Copyright (C) <2007> Thijs Vermeir <thijsvermeir@gmail.com>
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

 /* example of a subtitle chunk in an avi file
  * 00000000 (0x8051700): 47 41 42 32 00 02 00 10 00 00 00 45 00 6e 00 67  GAB2.......E.n.g
  * 00000010 (0x8051710): 00 6c 00 69 00 73 00 68 00 00 00 04 00 8e 00 00  .l.i.s.h........
  * 00000020 (0x8051720): 00 ef bb bf 31 0d 0a 30 30 3a 30 30 3a 30 30 2c  ....1..00:00:00,
  * 00000030 (0x8051730): 31 30 30 20 2d 2d 3e 20 30 30 3a 30 30 3a 30 32  100 --> 00:00:02
  * 00000040 (0x8051740): 2c 30 30 30 0d 0a 3c 62 3e 41 6e 20 55 54 46 38  ,000..<b>An UTF8
  * 00000050 (0x8051750): 20 53 75 62 74 69 74 6c 65 20 77 69 74 68 20 42   Subtitle with B
  * 00000060 (0x8051760): 4f 4d 3c 2f 62 3e 0d 0a 0d 0a 32 0d 0a 30 30 3a  OM</b>....2..00:
  * 00000070 (0x8051770): 30 30 3a 30 32 2c 31 30 30 20 2d 2d 3e 20 30 30  00:02,100 --> 00
  * 00000080 (0x8051780): 3a 30 30 3a 30 34 2c 30 30 30 0d 0a 53 6f 6d 65  :00:04,000..Some
  * 00000090 (0x8051790): 74 68 69 6e 67 20 6e 6f 6e 41 53 43 49 49 20 2d  thing nonASCII -
  * 000000a0 (0x80517a0): 20 c2 b5 c3 b6 c3 a4 c3 bc c3 9f 0d 0a 0d 0a      ..............
  */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstavisubtitle.h"

GST_DEBUG_CATEGORY_STATIC (avisubtitle_debug);
#define GST_CAT_DEFAULT avisubtitle_debug
GST_DEBUG_CATEGORY_EXTERN (GST_CAT_EVENT);

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

GST_BOILERPLATE (GstAviSubtitle, gst_avi_subtitle, GstElement,
    GST_TYPE_ELEMENT);

static GstBuffer *
gst_avi_subtitle_utf8_file (GstBuffer * buffer, guint offset)
{
  guint8 *file = GST_BUFFER_DATA (buffer) + offset;

  if (file[0] == 0xEF && file[1] == 0xBB && file[2] == 0xBF) {
    /* UTF-8 */
    return gst_buffer_create_sub (buffer, offset + 3,
        GST_BUFFER_SIZE (buffer) - offset - 3);
  }
  /* TODO Check for:
   * 00 00 FE FF    UTF-32, big-endian
   * FF FE 00 00    UTF-32, little-endian
   * FE FF          UTF-16, big-endian
   * FF FE          UTF-16, little-endian
   */

  /* No BOM detected assuming UTF-8 */
  return gst_buffer_create_sub (buffer, offset,
      GST_BUFFER_SIZE (buffer) - offset);
}

static GstFlowReturn
gst_avi_subtitle_chain (GstPad * pad, GstBuffer * buffer)
{
  guint name_length, file_length;
  gunichar2 *name;

  // gchar* name_utf8;
  GstFlowReturn ret;
  GstAviSubtitle *avisubtitle = GST_AVI_SUBTITLE (GST_PAD_PARENT (pad));

  /* we expext only one buffer packet with the whole srt/ssa file in it */

  /* check the magic word "GAB2\0" */
  if (GST_BUFFER_SIZE (buffer) <= 11
      || memcmp (GST_BUFFER_DATA (buffer), "GAB2\0", 5) != 0)
    goto wrong_magic_word;

  /* next word must be 2 */
  if (GST_READ_UINT16_LE (GST_BUFFER_DATA (buffer) + 5) != 0x2)
    goto wrong_fixed_word_1;

  name_length = GST_READ_UINT32_LE (GST_BUFFER_DATA (buffer) + 7);
  GST_LOG ("length of name: %d", name_length);
  if (GST_BUFFER_SIZE (buffer) <= 17 + name_length)
    goto wrong_length_1;

  name = (gunichar2 *) & (GST_BUFFER_DATA (buffer)[11]);
  // FIXME Take care for endianess in UTF-16
  // name_utf8 = g_utf16_to_utf8( name, name_length, NULL, NULL, NULL);
  // GST_LOG("avi subtitle name: %s", name_utf8);
  // g_free (name_utf8);

  /* next word must be 4 */
  if (GST_READ_UINT16_LE (GST_BUFFER_DATA (buffer) + 11 + name_length) != 0x4)
    goto wrong_fixed_word_2;

  file_length =
      GST_READ_UINT32_LE (GST_BUFFER_DATA (buffer) + 13 + name_length);
  GST_LOG ("length srt/ssa file: %d", file_length);

  if (GST_BUFFER_SIZE (buffer) != 17 + name_length + file_length)
    goto wrong_total_length;

  /* push the file over the src pad */
  ret =
      gst_pad_push (avisubtitle->src, gst_avi_subtitle_utf8_file (buffer,
          17 + name_length));
  gst_buffer_unref (buffer);

  return ret;

  /* all the errors */
wrong_magic_word:
  GST_ELEMENT_ERROR (avisubtitle, STREAM, DECODE, NULL, ("Wrong magic word"));
  gst_buffer_unref (buffer);
  return GST_FLOW_ERROR;

wrong_fixed_word_1:
  GST_ELEMENT_ERROR (avisubtitle, STREAM, DECODE, NULL,
      ("wrong fixed word: expected %d found %d", 2,
          GST_READ_UINT16_LE (GST_BUFFER_DATA (buffer) + 5)));
  gst_buffer_unref (buffer);
  return GST_FLOW_ERROR;

wrong_length_1:
  GST_ELEMENT_ERROR (avisubtitle, STREAM, DECODE, NULL,
      ("length of the buffer is too small (%d < %d)", GST_BUFFER_SIZE (buffer),
          17 + name_length));
  gst_buffer_unref (buffer);
  return GST_FLOW_ERROR;

wrong_fixed_word_2:
  GST_ELEMENT_ERROR (avisubtitle, STREAM, DECODE, NULL,
      ("wrong fixed word: expected %d found %d", 4,
          GST_READ_UINT16_LE (GST_BUFFER_DATA (buffer) + 11 + name_length)));
  gst_buffer_unref (buffer);
  return GST_FLOW_ERROR;

wrong_total_length:
  GST_ELEMENT_ERROR (avisubtitle, STREAM, DECODE, NULL,
      ("buffer size is wrong: need %d bytes, have %d bytes",
          17 + name_length + file_length, GST_BUFFER_SIZE (buffer)));
  gst_buffer_unref (buffer);
  return GST_FLOW_ERROR;
}

static void
gst_avi_subtitle_base_init (gpointer klass)
{
  static const GstElementDetails gst_avi_demux_details =
      GST_ELEMENT_DETAILS ("Avi subtitle parser",
      "Codec/Demuxer",
      "Parse avi subtitle stream",
      "Thijs Vermeir <thijsvermeir@gmail.com>");
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  /* add the pad templates to the element */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  /* set the element details */
  gst_element_class_set_details (element_class, &gst_avi_demux_details);
}

static void
gst_avi_subtitle_class_init (GstAviSubtitleClass * klass)
{
  GST_DEBUG_CATEGORY_INIT (avisubtitle_debug, "avisubtitle", 0,
      "parse avi subtitle stream");
}

static void
gst_avi_subtitle_init (GstAviSubtitle * self, GstAviSubtitleClass * klass)
{
  self->src = gst_pad_new_from_static_template (&src_template, "src");
  gst_element_add_pad (GST_ELEMENT (self), self->src);

  self->sink = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (self), self->sink);
  gst_pad_set_chain_function (self->sink, gst_avi_subtitle_chain);
}
