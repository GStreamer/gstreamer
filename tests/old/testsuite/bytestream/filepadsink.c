/* GStreamer
 * Copyright (C) 2004 Benjamin Otte <otte@gnome.org>
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
#include <gst/gst.h>
#include <gst/bytestream/filepad.h>
#include <stdio.h>
#include <string.h>

#define GST_TYPE_FP_SINK \
  (gst_fp_sink_get_type())
#define GST_FP_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FP_SINK,GstFpSink))
#define GST_FP_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FP_SINK,GstFpSinkClass))
#define GST_IS_FP_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FP_SINK))
#define GST_IS_FP_SINK_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FP_SINK))


typedef struct _GstFpSink GstFpSink;
typedef struct _GstFpSinkClass GstFpSinkClass;

struct _GstFpSink
{
  GstElement element;
  /* pads */
  GstFilePad *sinkpad;

  /* fd */
  FILE *stream;
  guint state;
};

struct _GstFpSinkClass
{
  GstElementClass parent_class;
};

GST_BOILERPLATE (GstFpSink, gst_fp_sink, GstElement, GST_TYPE_ELEMENT);

static void do_tests (GstFilePad * pad);


static void
gst_fp_sink_base_init (gpointer g_class)
{
}

static void
gst_fp_sink_class_init (GstFpSinkClass * klass)
{
}

static GstStaticPadTemplate template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

static void
gst_fp_sink_init (GstFpSink * fp)
{
  GST_FLAG_SET (fp, GST_ELEMENT_EVENT_AWARE);

  fp->sinkpad =
      GST_FILE_PAD (gst_file_pad_new (gst_static_pad_template_get (&template),
          "src"));
  gst_file_pad_set_iterate_function (fp->sinkpad, do_tests);
  gst_element_add_pad (GST_ELEMENT (fp), GST_PAD (fp->sinkpad));
}

#define THE_CHECK(result) G_STMT_START{ \
  gint64 pos = gst_file_pad_tell(fp->sinkpad); \
  if (pos >= 0) \
    g_assert (pos == ftell (fp->stream)); \
  g_print ("%s (%"G_GINT64_FORMAT")\n", result ? "OK" : "no", pos); \
  return result; \
}G_STMT_END
#define FAIL THE_CHECK(FALSE)
#define SUCCESS THE_CHECK(TRUE)

static gboolean
fp_read (GstFpSink * fp, guint size)
{
  guint8 buf[size], buf2[size];
  gint64 amount;

  g_print ("reading %u bytes...", size);
  amount = gst_file_pad_read (fp->sinkpad, buf, size);
  if (amount == -EAGAIN)
    FAIL;
  g_assert (amount == size);
  amount = fread (buf2, 1, amount, fp->stream);
  g_assert (amount == size);
  if (memcmp (buf, buf2, amount) != 0)
    g_assert_not_reached ();
  fp->state++;
  SUCCESS;
}

static gboolean
fp_try_read (GstFpSink * fp, guint size)
{
  guint8 buf[size], buf2[size];
  gint64 amount;
  size_t amount2;

  g_print ("reading %u bytes...", size);
  amount = gst_file_pad_try_read (fp->sinkpad, buf, size);
  if (amount == -EAGAIN)
    FAIL;
  g_assert (amount > 0);
  amount2 = fread (buf2, 1, amount, fp->stream);
  g_assert (amount == amount2);
  if (memcmp (buf, buf2, amount) != 0)
    g_assert_not_reached ();
  fp->state++;
  SUCCESS;
}

static gboolean
fp_seek (GstFpSink * fp, gint64 pos, GstSeekType whence)
{
  int seek_type = whence == GST_SEEK_METHOD_SET ? SEEK_SET :
      whence == GST_SEEK_METHOD_CUR ? SEEK_CUR : SEEK_END;

  g_print ("seeking to %s %" G_GINT64_FORMAT " bytes...",
      whence == GST_SEEK_METHOD_SET ? "" : whence ==
      GST_SEEK_METHOD_CUR ? "+-" : "-", pos);
  if (gst_file_pad_seek (fp->sinkpad, pos, whence) != 0)
    g_assert_not_reached ();
  if (fseek (fp->stream, pos, seek_type) != 0)
    g_assert_not_reached ();
  fp->state++;
  SUCCESS;
}

static gboolean
fp_eof (GstFpSink * fp)
{
  guint8 buf;

  g_print ("checking for EOF...");
  if (!gst_file_pad_eof (fp->sinkpad))
    FAIL;
  if (fread (&buf, 1, 1, fp->stream) != 0)
    g_assert_not_reached ();
  fp->state++;
  SUCCESS;
}

#define MIN_SIZE 10050
#define MAX_SIZE 1000000
static void
do_tests (GstFilePad * pad)
{
  GstFpSink *fp = GST_FP_SINK (gst_pad_get_parent (GST_PAD (pad)));

  while (TRUE) {
    switch (fp->state) {
      case 0:
        if (!fp_try_read (fp, 50))
          return;
        break;
      case 1:
        if (!fp_try_read (fp, MAX_SIZE))        /* more than file size */
          return;
        break;
      case 2:
        if (!fp_seek (fp, 0, GST_SEEK_METHOD_SET))
          return;
        break;
      case 3:
        if (!fp_read (fp, 50))
          return;
        break;
      case 4:
        if (!fp_read (fp, MIN_SIZE - 50))       /* bigger than 1 buffer */
          return;
        break;
      case 5:
        if (!fp_seek (fp, -200, GST_SEEK_METHOD_CUR))
          return;
        break;
      case 6:
        if (!fp_read (fp, 50))
          return;
        break;
      case 7:
        if (!fp_seek (fp, 50, GST_SEEK_METHOD_CUR))
          return;
        break;
      case 8:
        if (!fp_read (fp, 50))
          return;
        break;
      case 9:
        if (!fp_seek (fp, MIN_SIZE - 50, GST_SEEK_METHOD_SET))
          return;
        break;
      case 10:
        if (!fp_read (fp, 50))
          return;
        break;
      case 11:
        if (!fp_seek (fp, 0, GST_SEEK_METHOD_END))
          return;
        break;
      case 12:
        if (!fp_eof (fp))
          return;
        gst_element_set_eos (GST_ELEMENT (fp));
        return;
      default:
        g_assert_not_reached ();
    }
  }
}

#ifndef THE_FILE
#  define THE_FILE "../../configure.ac"
#endif
gint
main (gint argc, gchar ** argv)
{
  GstElement *pipeline, *src, *sink;
  long size;

  gst_init (&argc, &argv);
  gst_library_load ("bytestream");

  pipeline = gst_element_factory_make ("pipeline", NULL);
  g_assert (pipeline);
  src = gst_element_factory_make ("filesrc", NULL);
  g_assert (src);
  sink = g_object_new (GST_TYPE_FP_SINK, NULL);
  gst_object_set_name (GST_OBJECT (sink), "sink");
  gst_bin_add_many (GST_BIN (pipeline), src, sink, NULL);
  if (!gst_element_link (src, sink))
    g_assert_not_reached ();
  g_object_set (src, "location", THE_FILE, NULL);
  GST_FP_SINK (sink)->stream = fopen (THE_FILE, "r");
  g_assert (GST_FP_SINK (sink)->stream);
  /* check correct file sizes */
  if (fseek (GST_FP_SINK (sink)->stream, 0, SEEK_END) != 0)
    g_assert_not_reached ();
  size = ftell (GST_FP_SINK (sink)->stream);
  if (fseek (GST_FP_SINK (sink)->stream, 0, SEEK_SET) != 0)
    g_assert_not_reached ();
  g_assert (size >= MIN_SIZE);
  g_assert (size <= MAX_SIZE);

  if (gst_element_set_state (pipeline, GST_STATE_PLAYING) != GST_STATE_SUCCESS)
    g_assert_not_reached ();
  while (gst_bin_iterate (GST_BIN (pipeline)));

  g_assert (GST_FP_SINK (sink)->state == 13);
  g_object_unref (pipeline);
  pipeline = NULL;
  return 0;
}
