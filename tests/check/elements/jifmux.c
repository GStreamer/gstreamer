/* GStreamer
 *
 * unit test for jifmux
 *
 * Copyright (C) <2010> Thiago Santos <thiago.sousa.santos@collabora.co.uk>
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
#  include <config.h>
#endif

#include <unistd.h>

#include <gst/check/gstcheck.h>

#ifdef HAVE_EXIF
#include <libexif/exif-data.h>
#include <libexif/exif-loader.h>

typedef struct
{
  gboolean result;
  const GstTagList *taglist;
  const gchar *gst_tag;
} ExifTagCheckData;

/* taken from the exif helper lib in -base */
/* Exif tag types */
#define EXIF_TYPE_BYTE       1
#define EXIF_TYPE_ASCII      2
#define EXIF_TYPE_SHORT      3
#define EXIF_TYPE_LONG       4
#define EXIF_TYPE_RATIONAL   5
#define EXIF_TYPE_UNDEFINED  7
#define EXIF_TYPE_SLONG      9
#define EXIF_TYPE_SRATIONAL 10

typedef struct _GstExifTagMatch
{
  const gchar *gst_tag;
  guint16 exif_tag;
  guint16 exif_type;
  guint16 complementary_tag;
} GstExifTagMatch;

static const GstExifTagMatch tag_map[] = {
  {GST_TAG_DESCRIPTION, 0x10E, EXIF_TYPE_ASCII, 0},
  {GST_TAG_DEVICE_MANUFACTURER, 0x10F, EXIF_TYPE_ASCII, 0},
  {GST_TAG_DEVICE_MODEL, 0x110, EXIF_TYPE_ASCII, 0},
  {GST_TAG_ARTIST, 0x13B, EXIF_TYPE_ASCII, 0},
  {GST_TAG_COPYRIGHT, 0x8298, EXIF_TYPE_ASCII, 0}
};

static gint
get_tag_id_from_gst_tag (const gchar * gst_tag)
{
  gint i;

  for (i = 0; i < G_N_ELEMENTS (tag_map); i++) {
    if (strcmp (gst_tag, tag_map[i].gst_tag) == 0)
      return i;
  }

  return -1;
}

static void
check_content (ExifContent * content, void *user_data)
{
  ExifTagCheckData *test_data = (ExifTagCheckData *) user_data;
  guint16 tagindex;
  ExifEntry *entry;

  tagindex = get_tag_id_from_gst_tag (test_data->gst_tag);

  fail_if (tagindex == -1);

  entry = exif_content_get_entry (content, tag_map[tagindex].exif_tag);
  if (entry == NULL)
    return;
  fail_unless (entry->format == tag_map[tagindex].exif_type);

  switch (entry->format) {
    case EXIF_TYPE_ASCII:{
      const gchar *str;
      gchar *taglist_str;

      str = (gchar *) entry->data;
      fail_unless (gst_tag_list_get_string (test_data->taglist,
              tag_map[tagindex].gst_tag, &taglist_str));

      fail_unless (strcmp (str, taglist_str) == 0);
      test_data->result = TRUE;
    }
      break;
    default:
      fail ();
  }
}

static void
libexif_check_tag_exists (const GstTagList * taglist, const gchar * gst_tag,
    gpointer data)
{
  ExifData *exif_data = (ExifData *) data;
  ExifTagCheckData test_data;

  test_data.result = FALSE;
  test_data.taglist = taglist;
  test_data.gst_tag = gst_tag;

  exif_data_foreach_content (exif_data, check_content, &test_data);

  fail_unless (test_data.result);
}

static void
generate_jif_file_with_tags (const gchar * tags, const gchar * filepath)
{
  GstElement *pipeline;
  GstBus *bus;
  GstMessage *msg;
  gchar *launchline;

  launchline = g_strdup_printf ("videotestsrc num-buffers=1 ! jpegenc ! "
      "taginject tags=%s ! jifmux ! filesink location=%s", tags, filepath);

  pipeline = gst_parse_launch (launchline, NULL);
  fail_unless (pipeline != NULL);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));

  fail_if (gst_element_set_state (pipeline, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE);

  msg = gst_bus_timed_pop_filtered (bus, GST_SECOND * 5, GST_MESSAGE_EOS |
      GST_MESSAGE_ERROR);
  fail_if (!msg || GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR);

  gst_message_unref (msg);
  gst_object_unref (bus);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
}

static void
libexif_check_tags (const gchar * tags, const gchar * filepath)
{
  GstTagList *taglist;
  ExifData *exif_data;

  taglist = gst_structure_from_string (tags, NULL);
  fail_unless (taglist != NULL);

  exif_data = exif_data_new_from_file (filepath);

  gst_tag_list_foreach (taglist, libexif_check_tag_exists, exif_data);

  exif_data_unref (exif_data);
  gst_tag_list_free (taglist);
}

GST_START_TEST (test_jifmux_tags)
{
  gchar *tmpfile;
  gchar *tmp;

  tmp = g_strdup_printf ("%s%d", "gst-check-xmp-test-", g_random_int ());
  tmpfile = g_build_filename (g_get_tmp_dir (), tmp, NULL);
  g_free (tmp);

#define EXIF_0IFD_TAGS "artist=testname,copyright=somenotice," \
    "device-manufacturer=myfavoritebrand,device-model=xpto," \
    "description=testtags"
  generate_jif_file_with_tags (EXIF_0IFD_TAGS, tmpfile);
  libexif_check_tags ("taglist," EXIF_0IFD_TAGS, tmpfile);

  g_free (tmpfile);
}

GST_END_TEST;
#endif

static Suite *
asfmux_suite (void)
{
  Suite *s = suite_create ("jifmux");
  TCase *tc_chain = tcase_create ("general");
#ifdef HAVE_EXIF
  tcase_add_test (tc_chain, test_jifmux_tags);
#else
  GST_WARNING ("Not running jifmux tags tests that depend on libexif");
#endif

  suite_add_tcase (s, tc_chain);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = asfmux_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
