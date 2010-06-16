/* GStreamer
 *
 * unit tests for the tag support library
 *
 * Copyright (C) 2006-2009 Tim-Philipp Müller <tim centricular net>
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
#include "config.h"
#endif

#include <gst/check/gstcheck.h>

#include <gst/tag/tag.h>
#include <gst/base/gstbytewriter.h>
#include <string.h>

GST_START_TEST (test_parse_extended_comment)
{
  gchar *key, *val, *lang;

  /* first check the g_return_val_if_fail conditions */
  ASSERT_CRITICAL (gst_tag_parse_extended_comment (NULL, NULL, NULL, NULL,
          FALSE));
  ASSERT_CRITICAL (gst_tag_parse_extended_comment ("\377\000", NULL, NULL, NULL,
          FALSE));

  key = val = lang = NULL;
  fail_unless (gst_tag_parse_extended_comment ("a=b", &key, &lang, &val,
          FALSE) == TRUE);
  fail_unless (key != NULL);
  fail_unless (lang == NULL);
  fail_unless (val != NULL);
  fail_unless_equals_string (key, "a");
  fail_unless_equals_string (val, "b");
  g_free (key);
  g_free (lang);
  g_free (val);

  key = val = lang = NULL;
  fail_unless (gst_tag_parse_extended_comment ("a[l]=b", &key, &lang, &val,
          FALSE) == TRUE);
  fail_unless (key != NULL);
  fail_unless (lang != NULL);
  fail_unless (val != NULL);
  fail_unless_equals_string (key, "a");
  fail_unless_equals_string (lang, "l");
  fail_unless_equals_string (val, "b");
  g_free (key);
  g_free (lang);
  g_free (val);

  key = val = lang = NULL;
  fail_unless (gst_tag_parse_extended_comment ("foo=bar", &key, &lang, &val,
          FALSE) == TRUE);
  fail_unless (key != NULL);
  fail_unless (lang == NULL);
  fail_unless (val != NULL);
  fail_unless_equals_string (key, "foo");
  fail_unless_equals_string (val, "bar");
  g_free (key);
  g_free (lang);
  g_free (val);

  key = val = lang = NULL;
  fail_unless (gst_tag_parse_extended_comment ("foo[fr]=bar", &key, &lang, &val,
          FALSE) == TRUE);
  fail_unless (key != NULL);
  fail_unless (lang != NULL);
  fail_unless (val != NULL);
  fail_unless_equals_string (key, "foo");
  fail_unless_equals_string (lang, "fr");
  fail_unless_equals_string (val, "bar");
  g_free (key);
  g_free (lang);
  g_free (val);

  key = val = lang = NULL;
  fail_unless (gst_tag_parse_extended_comment ("foo=[fr]bar", &key, &lang, &val,
          FALSE) == TRUE);
  fail_unless (key != NULL);
  fail_unless (lang == NULL);
  fail_unless (val != NULL);
  fail_unless_equals_string (key, "foo");
  fail_unless_equals_string (val, "[fr]bar");
  g_free (key);
  g_free (lang);
  g_free (val);

  /* test NULL for output locations */
  fail_unless (gst_tag_parse_extended_comment ("foo[fr]=bar", NULL, NULL, NULL,
          FALSE) == TRUE);

  /* test strict mode (key must be specified) */
  fail_unless (gst_tag_parse_extended_comment ("foo[fr]=bar", NULL, NULL, NULL,
          TRUE) == TRUE);
  fail_unless (gst_tag_parse_extended_comment ("foo=bar", NULL, NULL, NULL,
          TRUE) == TRUE);
  fail_unless (gst_tag_parse_extended_comment ("foobar", NULL, NULL, NULL,
          TRUE) == FALSE);

  /* test non-strict mode (if there's no key, that's fine too) */
  fail_unless (gst_tag_parse_extended_comment ("foobar", NULL, NULL, NULL,
          FALSE) == TRUE);
  fail_unless (gst_tag_parse_extended_comment ("[fr]bar", NULL, NULL, NULL,
          FALSE) == TRUE);

  key = val = lang = NULL;
  fail_unless (gst_tag_parse_extended_comment ("[fr]bar", &key, &lang, &val,
          FALSE) == TRUE);
  fail_unless (key == NULL);
  fail_unless (lang == NULL);
  fail_unless (val != NULL);
  fail_unless_equals_string (val, "[fr]bar");
  g_free (key);
  g_free (lang);
  g_free (val);
}

GST_END_TEST;

#define ASSERT_TAG_LIST_HAS_STRING(list,field,string)                      \
  {                                                                        \
    gboolean got_match = FALSE;                                            \
    guint i, size;                                                         \
                                                                           \
    fail_unless (gst_tag_list_get_tag_size (list,field) > 0);              \
    size = gst_tag_list_get_tag_size (list,field);                         \
    for (i = 0; i < size; ++i) {                                           \
      gchar *___s = NULL;                                                  \
                                                                           \
      fail_unless (gst_tag_list_get_string_index (list, field, i, &___s)); \
      fail_unless (___s != NULL);                                          \
      if (g_str_equal (___s, string)) {                                    \
        got_match = TRUE;                                                  \
        g_free (___s);                                                     \
        break;                                                             \
      }                                                                    \
      g_free (___s);                                                       \
    }                                                                      \
    fail_unless (got_match);                                               \
  }

#define ASSERT_TAG_LIST_HAS_UINT(list,field,num)                           \
  {                                                                        \
    guint ___n;                                                            \
                                                                           \
    fail_unless (gst_tag_list_get_tag_size (list,field) > 0);              \
    fail_unless (gst_tag_list_get_tag_size (list,field) == 1);             \
    fail_unless (gst_tag_list_get_uint_index (list, field, 0, &___n));     \
    fail_unless_equals_int (___n, num);                                    \
  }

#define MATCH_DOUBLE(p1, p2) ((p1 < p2 + 1e-6) && (p2 < p1 + 1e-6))
#define ASSERT_TAG_LIST_HAS_DOUBLE(list,field,d)                           \
  {                                                                        \
    gdouble ___d;                                                          \
                                                                           \
    fail_unless (gst_tag_list_get_tag_size (list,field) > 0);              \
    fail_unless (gst_tag_list_get_tag_size (list,field) == 1);             \
    fail_unless (gst_tag_list_get_double_index (list, field, 0, &___d));   \
    fail_unless (MATCH_DOUBLE (d, ___d),                                   \
        "%f does not match expected %f", ___d, d);                         \
  }

GST_START_TEST (test_muscibrainz_tag_registration)
{
  GstTagList *list;

  gst_tag_register_musicbrainz_tags ();

  list = gst_tag_list_new ();

  /* musicbrainz tags aren't registered yet */
  gst_vorbis_tag_add (list, "MUSICBRAINZ_TRACKID", "123456");
  gst_vorbis_tag_add (list, "MUSICBRAINZ_ARTISTID", "234567");
  gst_vorbis_tag_add (list, "MUSICBRAINZ_ALBUMID", "345678");
  gst_vorbis_tag_add (list, "MUSICBRAINZ_ALBUMARTISTID", "4567890");
  gst_vorbis_tag_add (list, "MUSICBRAINZ_TRMID", "5678901");
  /* MUSICBRAINZ_SORTNAME = GST_TAG_ARTIST_SORTNAME now */
  gst_vorbis_tag_add (list, "MUSICBRAINZ_SORTNAME", "Five, 678901");

  ASSERT_TAG_LIST_HAS_STRING (list, GST_TAG_MUSICBRAINZ_TRACKID, "123456");
  ASSERT_TAG_LIST_HAS_STRING (list, GST_TAG_MUSICBRAINZ_ARTISTID, "234567");
  ASSERT_TAG_LIST_HAS_STRING (list, GST_TAG_MUSICBRAINZ_ALBUMID, "345678");
  ASSERT_TAG_LIST_HAS_STRING (list, GST_TAG_MUSICBRAINZ_ALBUMARTISTID,
      "4567890");
  ASSERT_TAG_LIST_HAS_STRING (list, GST_TAG_MUSICBRAINZ_TRMID, "5678901");
  ASSERT_TAG_LIST_HAS_STRING (list, GST_TAG_ARTIST_SORTNAME, "Five, 678901");

  gst_tag_list_free (list);
}

GST_END_TEST;

/* is there an easier way to compare two structures / tagslists? */
static gboolean
taglists_are_equal (const GstTagList * list_1, const GstTagList * list_2)
{
  GstCaps *c_list_1 = gst_caps_new_empty ();
  GstCaps *c_list_2 = gst_caps_new_empty ();
  gboolean ret;

  gst_caps_append_structure (c_list_1,
      gst_structure_copy ((GstStructure *) list_1));
  gst_caps_append_structure (c_list_2,
      gst_structure_copy ((GstStructure *) list_2));

  ret = gst_caps_is_equal (c_list_2, c_list_1);

  gst_caps_unref (c_list_1);
  gst_caps_unref (c_list_2);

  return ret;
}

GST_START_TEST (test_vorbis_tags)
{
  GstTagList *list;

  list = gst_tag_list_new ();

  /* NULL pointers aren't allowed */
  ASSERT_CRITICAL (gst_vorbis_tag_add (NULL, "key", "value"));
  ASSERT_CRITICAL (gst_vorbis_tag_add (list, NULL, "value"));
  ASSERT_CRITICAL (gst_vorbis_tag_add (list, "key", NULL));

  /* must be UTF-8 */
  ASSERT_CRITICAL (gst_vorbis_tag_add (list, "key", "v\377lue"));
  ASSERT_CRITICAL (gst_vorbis_tag_add (list, "k\377y", "value"));

  /* key can't have a '=' in it */
  ASSERT_CRITICAL (gst_vorbis_tag_add (list, "k=y", "value"));
  ASSERT_CRITICAL (gst_vorbis_tag_add (list, "key=", "value"));

  /* should be allowed in values though */
  gst_vorbis_tag_add (list, "keeey", "va=ue");

  /* add some tags */
  gst_vorbis_tag_add (list, "TITLE", "Too");
  gst_vorbis_tag_add (list, "ALBUM", "Aoo");
  gst_vorbis_tag_add (list, "ARTIST", "Alboo");
  gst_vorbis_tag_add (list, "PERFORMER", "Perfoo");
  gst_vorbis_tag_add (list, "COPYRIGHT", "Copyfoo");
  gst_vorbis_tag_add (list, "DESCRIPTION", "Descoo");
  gst_vorbis_tag_add (list, "LICENSE", "Licoo");
  gst_vorbis_tag_add (list, "LICENSE",
      "http://creativecommons.org/licenses/by/3.0/");
  gst_vorbis_tag_add (list, "LOCATION", "Bristol, UK");
  gst_vorbis_tag_add (list, "ORGANIZATION", "Orgoo");
  gst_vorbis_tag_add (list, "GENRE", "Goo");
  gst_vorbis_tag_add (list, "CONTACT", "Coo");
  gst_vorbis_tag_add (list, "COMMENT", "Stroodle is good");
  gst_vorbis_tag_add (list, "COMMENT", "Peroxysulfid stroodles the brain");

  gst_vorbis_tag_add (list, "TRACKNUMBER", "5");
  gst_vorbis_tag_add (list, "TRACKTOTAL", "77");
  gst_vorbis_tag_add (list, "DISCNUMBER", "1");
  gst_vorbis_tag_add (list, "DISCTOTAL", "2");
  gst_vorbis_tag_add (list, "DATE", "1954-12-31");

  ASSERT_TAG_LIST_HAS_STRING (list, GST_TAG_TITLE, "Too");
  ASSERT_TAG_LIST_HAS_STRING (list, GST_TAG_ALBUM, "Aoo");
  ASSERT_TAG_LIST_HAS_STRING (list, GST_TAG_ARTIST, "Alboo");
  ASSERT_TAG_LIST_HAS_STRING (list, GST_TAG_PERFORMER, "Perfoo");
  ASSERT_TAG_LIST_HAS_STRING (list, GST_TAG_COPYRIGHT, "Copyfoo");
  ASSERT_TAG_LIST_HAS_STRING (list, GST_TAG_DESCRIPTION, "Descoo");
  ASSERT_TAG_LIST_HAS_STRING (list, GST_TAG_LICENSE, "Licoo");
  ASSERT_TAG_LIST_HAS_STRING (list, GST_TAG_LICENSE_URI,
      "http://creativecommons.org/licenses/by/3.0/");
  ASSERT_TAG_LIST_HAS_STRING (list, GST_TAG_GEO_LOCATION_NAME, "Bristol, UK");
  ASSERT_TAG_LIST_HAS_STRING (list, GST_TAG_ORGANIZATION, "Orgoo");
  ASSERT_TAG_LIST_HAS_STRING (list, GST_TAG_GENRE, "Goo");
  ASSERT_TAG_LIST_HAS_STRING (list, GST_TAG_CONTACT, "Coo");
  ASSERT_TAG_LIST_HAS_STRING (list, GST_TAG_COMMENT,
      "Peroxysulfid stroodles the brain");
  ASSERT_TAG_LIST_HAS_STRING (list, GST_TAG_COMMENT, "Stroodle is good");
  ASSERT_TAG_LIST_HAS_UINT (list, GST_TAG_TRACK_NUMBER, 5);
  ASSERT_TAG_LIST_HAS_UINT (list, GST_TAG_TRACK_COUNT, 77);
  ASSERT_TAG_LIST_HAS_UINT (list, GST_TAG_ALBUM_VOLUME_NUMBER, 1);
  ASSERT_TAG_LIST_HAS_UINT (list, GST_TAG_ALBUM_VOLUME_COUNT, 2);

  {
    GDate *date = NULL;

    fail_unless (gst_tag_list_get_date (list, GST_TAG_DATE, &date));
    fail_unless (date != NULL);
    fail_unless (g_date_get_day (date) == 31);
    fail_unless (g_date_get_month (date) == G_DATE_DECEMBER);
    fail_unless (g_date_get_year (date) == 1954);

    g_date_free (date);
  }

  /* unknown vorbis comments should go into a GST_TAG_EXTENDED_COMMENT */
  gst_vorbis_tag_add (list, "CoEdSub_ID", "98172AF-973-10-B");
  ASSERT_TAG_LIST_HAS_STRING (list, GST_TAG_EXTENDED_COMMENT,
      "CoEdSub_ID=98172AF-973-10-B");
  gst_vorbis_tag_add (list, "RuBuWuHash", "1337BA42F91");
  ASSERT_TAG_LIST_HAS_STRING (list, GST_TAG_EXTENDED_COMMENT,
      "RuBuWuHash=1337BA42F91");

  gst_vorbis_tag_add (list, "REPLAYGAIN_REFERENCE_LOUDNESS", "89.");
  ASSERT_TAG_LIST_HAS_DOUBLE (list, GST_TAG_REFERENCE_LEVEL, 89.);
  gst_vorbis_tag_add (list, "REPLAYGAIN_TRACK_GAIN", "+12.36");
  ASSERT_TAG_LIST_HAS_DOUBLE (list, GST_TAG_TRACK_GAIN, +12.36);
  gst_vorbis_tag_add (list, "REPLAYGAIN_TRACK_PEAK", "0.96349");
  ASSERT_TAG_LIST_HAS_DOUBLE (list, GST_TAG_TRACK_PEAK, 0.96349);
  gst_vorbis_tag_add (list, "REPLAYGAIN_ALBUM_GAIN", "+10.12");
  ASSERT_TAG_LIST_HAS_DOUBLE (list, GST_TAG_ALBUM_GAIN, +10.12);
  /* now check that we can parse floating point numbers with any separator
   * (',' or '.') regardless of the current locale */
  gst_vorbis_tag_add (list, "REPLAYGAIN_ALBUM_PEAK", "0,98107");
  ASSERT_TAG_LIST_HAS_DOUBLE (list, GST_TAG_ALBUM_PEAK, 0.98107);
  gst_vorbis_tag_add (list, "LICENSE", "http://foo.com/license-1.html");

  /* make sure we can convert back and forth without loss */
  {
    GstTagList *new_list, *even_newer_list;
    GstBuffer *buf, *buf2;
    gchar *vendor_id = NULL;

    buf = gst_tag_list_to_vorbiscomment_buffer (list,
        (const guint8 *) "\003vorbis", 7, "libgstunittest");
    fail_unless (buf != NULL);
    new_list = gst_tag_list_from_vorbiscomment_buffer (buf,
        (const guint8 *) "\003vorbis", 7, &vendor_id);
    fail_unless (new_list != NULL);
    fail_unless (vendor_id != NULL);
    g_free (vendor_id);
    vendor_id = NULL;

    GST_LOG ("new_list = %" GST_PTR_FORMAT, new_list);
    fail_unless (taglists_are_equal (list, new_list));

    buf2 = gst_tag_list_to_vorbiscomment_buffer (new_list,
        (const guint8 *) "\003vorbis", 7, "libgstunittest");
    fail_unless (buf2 != NULL);
    even_newer_list = gst_tag_list_from_vorbiscomment_buffer (buf2,
        (const guint8 *) "\003vorbis", 7, &vendor_id);
    fail_unless (even_newer_list != NULL);
    fail_unless (vendor_id != NULL);
    g_free (vendor_id);
    vendor_id = NULL;

    GST_LOG ("even_newer_list = %" GST_PTR_FORMAT, even_newer_list);
    fail_unless (taglists_are_equal (new_list, even_newer_list));

    gst_tag_list_free (new_list);
    gst_tag_list_free (even_newer_list);
    gst_buffer_unref (buf);
    gst_buffer_unref (buf2);
  }

  /* there can only be one language per taglist ... */
  gst_tag_list_free (list);
  list = gst_tag_list_new ();
  gst_vorbis_tag_add (list, "LANGUAGE", "fr");
  ASSERT_TAG_LIST_HAS_STRING (list, GST_TAG_LANGUAGE_CODE, "fr");

  gst_tag_list_free (list);
  list = gst_tag_list_new ();
  gst_vorbis_tag_add (list, "LANGUAGE", "[fr]");
  ASSERT_TAG_LIST_HAS_STRING (list, GST_TAG_LANGUAGE_CODE, "fr");

  gst_tag_list_free (list);
  list = gst_tag_list_new ();
  gst_vorbis_tag_add (list, "LANGUAGE", "French [fr]");
  ASSERT_TAG_LIST_HAS_STRING (list, GST_TAG_LANGUAGE_CODE, "fr");

  gst_tag_list_free (list);
  list = gst_tag_list_new ();
  gst_vorbis_tag_add (list, "LANGUAGE", "[eng] English");
  ASSERT_TAG_LIST_HAS_STRING (list, GST_TAG_LANGUAGE_CODE, "eng");

  gst_tag_list_free (list);
  list = gst_tag_list_new ();
  gst_vorbis_tag_add (list, "LANGUAGE", "eng");
  ASSERT_TAG_LIST_HAS_STRING (list, GST_TAG_LANGUAGE_CODE, "eng");

  gst_tag_list_free (list);
  list = gst_tag_list_new ();
  gst_vorbis_tag_add (list, "LANGUAGE", "[eng]");
  ASSERT_TAG_LIST_HAS_STRING (list, GST_TAG_LANGUAGE_CODE, "eng");

  /* free-form *sigh* */
  gst_tag_list_free (list);
  list = gst_tag_list_new ();
  gst_vorbis_tag_add (list, "LANGUAGE", "English");
  ASSERT_TAG_LIST_HAS_STRING (list, GST_TAG_LANGUAGE_CODE, "English");

  /* now, while we still have a taglist, test _to_vorbiscomment_buffer() */
  {
    GstBuffer *buf1, *buf2;

    ASSERT_CRITICAL (gst_tag_list_to_vorbiscomment_buffer (NULL,
            (const guint8 *) "x", 1, "x"));

    buf1 = gst_tag_list_to_vorbiscomment_buffer (list, NULL, 0, NULL);
    fail_unless (buf1 != NULL);

    buf2 = gst_tag_list_to_vorbiscomment_buffer (list,
        (const guint8 *) "foo", 3, NULL);
    fail_unless (buf2 != NULL);

    fail_unless (memcmp (GST_BUFFER_DATA (buf1), GST_BUFFER_DATA (buf2) + 3,
            GST_BUFFER_SIZE (buf1)) == 0);

    gst_buffer_unref (buf1);
    gst_buffer_unref (buf2);
  }

  gst_tag_list_free (list);

  /* make sure gst_tag_list_from_vorbiscomment_buffer() works with an
   * empty ID (for Speex) */
  {
    const guint8 speex_comments_buf1[] = { 0x03, 0x00, 0x00, 0x00, 'f', 'o',
      'o', 0x00, 0x00, 0x00, 0x00
    };
    GstBuffer *buf;
    gchar *vendor = NULL;

    buf = gst_buffer_new ();
    GST_BUFFER_DATA (buf) = (guint8 *) speex_comments_buf1;
    GST_BUFFER_SIZE (buf) = sizeof (speex_comments_buf1);

    /* make sure it doesn't memcmp over the end of the buffer */
    fail_unless (gst_tag_list_from_vorbiscomment_buffer (buf,
            (const guint8 *) "averylongstringbrownfoxjumpoverthefence", 39,
            &vendor) == NULL);
    fail_unless (vendor == NULL);

    /* make sure it bails out if the ID doesn't match */
    fail_unless (gst_tag_list_from_vorbiscomment_buffer (buf,
            (guint8 *) "short", 4, &vendor) == NULL);
    fail_unless (vendor == NULL);

    /* now read properly */
    list = gst_tag_list_from_vorbiscomment_buffer (buf, NULL, 0, &vendor);
    fail_unless (vendor != NULL);
    fail_unless_equals_string (vendor, "foo");
    fail_unless (list != NULL);
    fail_unless (gst_structure_n_fields ((GstStructure *) list) == 0);
    g_free (vendor);
    gst_tag_list_free (list);

    /* now again without vendor */
    list = gst_tag_list_from_vorbiscomment_buffer (buf, NULL, 0, NULL);
    fail_unless (list != NULL);
    fail_unless (gst_structure_n_fields ((GstStructure *) list) == 0);
    gst_tag_list_free (list);

    gst_buffer_unref (buf);
  }

  /* the same with an ID */
  {
    const guint8 vorbis_comments_buf[] = { 0x03, 'v', 'o', 'r', 'b', 'i', 's',
      0x03, 0x00, 0x00, 0x00, 'f', 'o', 'o', 0x01, 0x00, 0x00, 0x00,
      strlen ("ARTIST=foo bar"), 0x00, 0x00, 0x00, 'A', 'R', 'T', 'I', 'S',
      'T', '=', 'f', 'o', 'o', ' ', 'b', 'a', 'r'
    };
    GstBuffer *buf;
    gchar *vendor = NULL;

    buf = gst_buffer_new ();
    GST_BUFFER_DATA (buf) = (guint8 *) vorbis_comments_buf;
    GST_BUFFER_SIZE (buf) = sizeof (vorbis_comments_buf);

    /* make sure it doesn't memcmp over the end of the buffer */
    fail_unless (gst_tag_list_from_vorbiscomment_buffer (buf,
            (const guint8 *) "averylongstringbrownfoxjumpoverthefence", 39,
            &vendor) == NULL);
    fail_unless (vendor == NULL);

    /* make sure it bails out if the ID doesn't match */
    fail_unless (gst_tag_list_from_vorbiscomment_buffer (buf,
            (guint8 *) "short", 4, &vendor) == NULL);
    fail_unless (vendor == NULL);

    /* now read properly */
    list = gst_tag_list_from_vorbiscomment_buffer (buf,
        (guint8 *) "\003vorbis", 7, &vendor);
    fail_unless (vendor != NULL);
    fail_unless_equals_string (vendor, "foo");
    fail_unless (list != NULL);
    fail_unless (gst_structure_n_fields ((GstStructure *) list) == 1);
    ASSERT_TAG_LIST_HAS_STRING (list, GST_TAG_ARTIST, "foo bar");
    g_free (vendor);
    gst_tag_list_free (list);

    /* now again without vendor */
    list = gst_tag_list_from_vorbiscomment_buffer (buf,
        (guint8 *) "\003vorbis", 7, NULL);
    fail_unless (list != NULL);
    fail_unless (gst_structure_n_fields ((GstStructure *) list) == 1);
    ASSERT_TAG_LIST_HAS_STRING (list, GST_TAG_ARTIST, "foo bar");
    gst_tag_list_free (list);

    gst_buffer_unref (buf);
  }

  /* check date with time */
  {
    GDate *date = NULL;

    list = gst_tag_list_new ();
    gst_vorbis_tag_add (list, "DATE", "2006-09-25 22:02:38");

    fail_unless (gst_tag_list_get_date_index (list, GST_TAG_DATE, 0, &date));
    fail_unless (date != NULL);
    fail_unless (g_date_get_day (date) == 25);
    fail_unless (g_date_get_month (date) == G_DATE_SEPTEMBER);
    fail_unless (g_date_get_year (date) == 2006);

    g_date_free (date);
    gst_tag_list_free (list);
  }

  /* check date with month/day of 00-00 */
  {
    GDate *date = NULL;

    list = gst_tag_list_new ();
    gst_vorbis_tag_add (list, "DATE", "1992-00-00");

    fail_unless (gst_tag_list_get_date_index (list, GST_TAG_DATE, 0, &date));
    fail_unless (date != NULL);
    fail_unless (g_date_get_year (date) == 1992);

    g_date_free (date);
    gst_tag_list_free (list);
  }

  /* check date with valid month, but day of 00 */
  {
    GDate *date = NULL;

    list = gst_tag_list_new ();
    gst_vorbis_tag_add (list, "DATE", "1992-05-00");

    fail_unless (gst_tag_list_get_date_index (list, GST_TAG_DATE, 0, &date));
    fail_unless (date != NULL);
    fail_unless (g_date_get_year (date) == 1992);
    fail_unless (g_date_get_month (date) == G_DATE_MAY);

    g_date_free (date);
    gst_tag_list_free (list);
  }
}

GST_END_TEST;

GST_START_TEST (test_id3_tags)
{
  guint i;

  fail_unless (gst_tag_id3_genre_count () > 0);

  for (i = 0; i < gst_tag_id3_genre_count (); ++i) {
    const gchar *genre;

    genre = gst_tag_id3_genre_get (i);
    fail_unless (genre != NULL);
  }

  {
    /* TODO: GstTagList *gst_tag_list_new_from_id3v1 (const guint8 *data) */
  }

  /* gst_tag_from_id3_tag */
  fail_unless (gst_tag_from_id3_tag ("TALB") != NULL);
  ASSERT_CRITICAL (gst_tag_from_id3_tag (NULL));
  fail_unless (gst_tag_from_id3_tag ("R2D2") == NULL);
  fail_unless_equals_string (gst_tag_from_id3_tag ("WCOP"),
      GST_TAG_COPYRIGHT_URI);

  /* gst_tag_from_id3_user_tag */
  ASSERT_CRITICAL (gst_tag_from_id3_user_tag (NULL, "foo"));
  ASSERT_CRITICAL (gst_tag_from_id3_user_tag ("foo", NULL));
  fail_unless (gst_tag_from_id3_user_tag ("R2D2", "R2D2") == NULL);

  /* gst_tag_to_id3_tag */
  ASSERT_CRITICAL (gst_tag_to_id3_tag (NULL));
  fail_unless (gst_tag_to_id3_tag ("R2D2") == NULL);
  fail_unless (gst_tag_to_id3_tag (GST_TAG_ARTIST) != NULL);
  fail_unless_equals_string (gst_tag_to_id3_tag (GST_TAG_COPYRIGHT_URI),
      "WCOP");

  fail_unless (GST_TYPE_TAG_IMAGE_TYPE != 0);
  fail_unless (g_type_name (GST_TYPE_TAG_IMAGE_TYPE) != NULL);
}

GST_END_TEST;


GST_START_TEST (test_id3v1_utf8_tag)
{
  const guint8 id3v1[128] = {
    /* marker */
    'T', 'A', 'G',
    /* title (30 bytes) */
    'D', 0xc3, 0xad, 'v', 'k', 'a', ' ', 's',
    ' ', 'p', 'e', 'r', 'l', 'a', 'm', 'i',
    ' ', 'v', 'e', ' ', 'v', 'l', 'a', 's',
    'e', 'c', 'h', 0, 0, 0,
    /* artist (30 bytes) */
    'A', 'l', 'e', 0xc5, 0xa1, ' ', 'B', 'r', 'i', 'c', 'h', 't', 'a',
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* album (30 bytes) */
    'B', 'e', 's', 't', ' ', 'o', 'f', ' ', '(', 'P', 'r', 'o', 's', 't',
    0xc4, 0x9b, ' ', 0xc3, 0xba, 0xc5, 0xbe, 'a', 's', 'n', 0xc3, 0xbd, ')',
    0, 0, 0,
    /* year (4 bytes) */
    '2', '0', '0', '0',
    /* comment (28 bytes) */
    '-', '-', '-', ' ', 0xc4, 0x8d, 'e', 's', 'k', 0xc3, 0xa9, ' ', 'p',
    0xc3, 0xad, 's', 'n', 'i', 0xc4, 0x8d, 'k', 'y', ' ', '-', '-', '-',
    0, 0,
    /* track number */
    0, 0,
    /* genre */
    0x11
  };
  GstTagList *tags;
  GDate *d;
  gchar *s;

  /* set this, to make sure UTF-8 strings are really interpreted properly
   * as UTF-8, regardless of the locale set */
  g_setenv ("GST_ID3V1_TAG_ENCODING", "WINDOWS-1250", TRUE);

  tags = gst_tag_list_new_from_id3v1 (id3v1);
  fail_unless (tags != NULL);

  GST_LOG ("Got tags: %" GST_PTR_FORMAT, tags);

  s = NULL;
  fail_unless (gst_tag_list_get_string (tags, GST_TAG_TITLE, &s));
  fail_unless (s != NULL);
  fail_unless_equals_string (s, "Dívka s perlami ve vlasech");
  g_free (s);

  s = NULL;
  fail_unless (gst_tag_list_get_string (tags, GST_TAG_ARTIST, &s));
  fail_unless (s != NULL);
  fail_unless_equals_string (s, "Aleš Brichta");
  g_free (s);

  s = NULL;
  fail_unless (gst_tag_list_get_string (tags, GST_TAG_ALBUM, &s));
  fail_unless (s != NULL);
  fail_unless_equals_string (s, "Best of (Prostě úžasný)");
  g_free (s);

  d = NULL;
  fail_unless (gst_tag_list_get_date (tags, GST_TAG_DATE, &d));
  fail_unless (d != NULL);
  fail_unless_equals_int (g_date_get_year (d), 2000);
  g_date_free (d);
  d = NULL;

  gst_tag_list_free (tags);

  g_unsetenv ("GST_ID3V1_TAG_ENCODING");
}

GST_END_TEST;

GST_START_TEST (test_language_utils)
{
  gchar **lang_codes, **c;

#define ASSERT_STRINGS_EQUAL fail_unless_equals_string

  lang_codes = gst_tag_get_language_codes ();
  fail_unless (lang_codes != NULL);
  fail_unless (*lang_codes != NULL);

  for (c = lang_codes; c != NULL && *c != NULL; ++c) {
    const gchar *lang_name, *c1, *c2t, *c2b;

    lang_name = gst_tag_get_language_name (*c);
    fail_unless (lang_name != NULL);
    fail_unless (g_utf8_validate (lang_name, -1, NULL));

    c1 = gst_tag_get_language_code_iso_639_1 (*c);
    fail_unless (c1 != NULL);
    fail_unless (g_utf8_validate (c1, -1, NULL));

    c2t = gst_tag_get_language_code_iso_639_2T (*c);
    fail_unless (c2t != NULL);
    fail_unless (g_utf8_validate (c2t, -1, NULL));

    c2b = gst_tag_get_language_code_iso_639_2B (*c);
    fail_unless (c2b != NULL);
    fail_unless (g_utf8_validate (c2b, -1, NULL));

    ASSERT_STRINGS_EQUAL (gst_tag_get_language_code_iso_639_1 (*c), *c);
    ASSERT_STRINGS_EQUAL (gst_tag_get_language_code_iso_639_1 (c2t), *c);
    ASSERT_STRINGS_EQUAL (gst_tag_get_language_code_iso_639_1 (c2b), *c);

    GST_DEBUG ("[%s] %s %s %s : %s\n", *c, c1, c2t, c2b, lang_name);

  }
  g_strfreev (lang_codes);

  fail_unless (gst_tag_get_language_name ("de") != NULL);
  fail_unless (gst_tag_get_language_name ("deu") != NULL);
  fail_unless (gst_tag_get_language_name ("ger") != NULL);
  fail_unless_equals_string (gst_tag_get_language_name ("deu"),
      gst_tag_get_language_name ("ger"));
  fail_unless_equals_string (gst_tag_get_language_name ("de"),
      gst_tag_get_language_name ("ger"));
  fail_unless (gst_tag_get_language_name ("de") !=
      gst_tag_get_language_name ("fr"));

  ASSERT_STRINGS_EQUAL (gst_tag_get_language_code ("deu"), "de");
  ASSERT_STRINGS_EQUAL (gst_tag_get_language_code ("de"), "de");
  ASSERT_STRINGS_EQUAL (gst_tag_get_language_code ("ger"), "de");

  ASSERT_STRINGS_EQUAL (gst_tag_get_language_code_iso_639_1 ("deu"), "de");
  ASSERT_STRINGS_EQUAL (gst_tag_get_language_code_iso_639_1 ("de"), "de");
  ASSERT_STRINGS_EQUAL (gst_tag_get_language_code_iso_639_1 ("ger"), "de");

  ASSERT_STRINGS_EQUAL (gst_tag_get_language_code_iso_639_2T ("de"), "deu");
  ASSERT_STRINGS_EQUAL (gst_tag_get_language_code_iso_639_2T ("deu"), "deu");
  ASSERT_STRINGS_EQUAL (gst_tag_get_language_code_iso_639_2T ("ger"), "deu");

  ASSERT_STRINGS_EQUAL (gst_tag_get_language_code_iso_639_2B ("de"), "ger");
  ASSERT_STRINGS_EQUAL (gst_tag_get_language_code_iso_639_2B ("deu"), "ger");
  ASSERT_STRINGS_EQUAL (gst_tag_get_language_code_iso_639_2B ("ger"), "ger");
}

GST_END_TEST;

GST_START_TEST (test_xmp_formatting)
{
  GstTagList *list;
  GstBuffer *buf;
  const gchar *text;
  guint len;

  /* test data */
  list = gst_tag_list_new_full (GST_TAG_TITLE, "test title",
      GST_TAG_DESCRIPTION, "test decription",
      GST_TAG_KEYWORDS, "keyword1", GST_TAG_KEYWORDS, "keyword2", NULL);

  buf = gst_tag_list_to_xmp_buffer (list, FALSE);
  fail_unless (buf != NULL);

  text = (const gchar *) GST_BUFFER_DATA (buf);
  len = GST_BUFFER_SIZE (buf);

  /* check the content */
  fail_unless (g_strrstr_len (text, len, "<?xpacket begin") == text);
  fail_unless (g_strrstr_len (text, len, ">test title<") != NULL);
  fail_unless (g_strrstr_len (text, len, ">test decription<") != NULL);
  fail_unless (g_strrstr_len (text, len, ">keyword1<") != NULL);
  fail_unless (g_strrstr_len (text, len, ">keyword2<") != NULL);
  fail_unless (g_strrstr_len (text, len, "<?xpacket end") != NULL);

  gst_buffer_unref (buf);
  gst_tag_list_free (list);
}

GST_END_TEST;


GST_START_TEST (test_xmp_parsing)
{
  GstTagList *list;
  GstBuffer *buf;
  guint i, result_size;
  gchar *text;
  const gchar *xmp_header =
      "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>"
      "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" x:xmptk=\"GStreamer\">"
      "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\">";
  const gchar *xmp_footer =
      "</rdf:RDF>" "</x:xmpmeta>" "<?xpacket end=\"r\"?>\n";
  struct
  {
    const gchar *xmp_data;
    gint result_size;
    gint result_test;
  } test_data[] = {
    {
    "", -1, -1}, {
    "<rdf:Description rdf:about=\"\" />", 0, -1}, {
    "<rdf:Description rdf:about=\"\"></rdf:Description>", 0, -1}, {
    "<rdf:Description    rdf:about=\"\"    ></rdf:Description>", 0, -1}, {
    "<rdf:Description rdf:about=\"\"><dc:description>test</dc:description></rdf:Description>",
          1, 0}, {
    "<rdf:Description rdf:about=\"\" dc:description=\"test\"></rdf:Description>",
          1, 0}, {
    NULL, -1, -1}
  };

  /* test data */
  buf = gst_buffer_new ();

  i = 0;
  while (test_data[i].xmp_data) {
    GST_DEBUG ("trying test-data %u", i);

    text = g_strconcat (xmp_header, test_data[i].xmp_data, xmp_footer, NULL);
    GST_BUFFER_DATA (buf) = (guint8 *) text;
    GST_BUFFER_SIZE (buf) = strlen (text) + 1;


    list = gst_tag_list_from_xmp_buffer (buf);
    if (test_data[i].result_size >= 0) {
      fail_unless (list != NULL);

      result_size = gst_structure_n_fields ((GstStructure *) list);
      fail_unless (result_size == test_data[i].result_size);

      /* check the taglist content */
      switch (test_data[i].result_test) {
        case 0:
          ASSERT_TAG_LIST_HAS_STRING (list, "description", "test");
          break;
        default:
          break;
      }
    }
    if (list)
      gst_tag_list_free (list);

    g_free (text);
    i++;
  }

  gst_buffer_unref (buf);
}

GST_END_TEST;


static void
tag_list_equals (GstTagList * taglist, GstTagList * taglist2)
{
  const gchar *name_sent, *name_recv;
  const GValue *value_sent, *value_recv;
  gboolean found;
  gint comparison;
  gint n_recv;
  gint n_sent;
  gint i, j;

  /* verify tags */
  fail_unless (taglist2 != NULL);
  n_recv = gst_structure_n_fields (taglist2);
  n_sent = gst_structure_n_fields (taglist);
  fail_unless (n_recv == n_sent);

  /* FIXME: compare taglist values */
  for (i = 0; i < n_sent; i++) {
    name_sent = gst_structure_nth_field_name (taglist, i);
    value_sent = gst_structure_get_value (taglist, name_sent);
    found = FALSE;
    for (j = 0; j < n_recv; j++) {
      name_recv = gst_structure_nth_field_name (taglist2, j);
      if (!strcmp (name_sent, name_recv)) {
        value_recv = gst_structure_get_value (taglist2, name_recv);
        comparison = gst_value_compare (value_sent, value_recv);
        if (comparison != GST_VALUE_EQUAL) {
          gchar *vs = g_strdup_value_contents (value_sent);
          gchar *vr = g_strdup_value_contents (value_recv);
          GST_DEBUG ("sent = %s:'%s', recv = %s:'%s'",
              G_VALUE_TYPE_NAME (value_sent), vs,
              G_VALUE_TYPE_NAME (value_recv), vr);
          g_free (vs);
          g_free (vr);
        }
        if (comparison != GST_VALUE_EQUAL &&
            G_VALUE_HOLDS (value_sent, G_TYPE_DOUBLE)) {
          gdouble vs;
          gdouble vr;

          /* add some tolerance for doubles */
          vs = g_value_get_double (value_sent);
          vr = g_value_get_double (value_recv);
          if (vr >= vs - 0.001 && vr <= vs + 0.001)
            comparison = GST_VALUE_EQUAL;
        }
        fail_unless (comparison == GST_VALUE_EQUAL,
            "tag item %s has been received with different type or value",
            name_sent);
        found = TRUE;
        break;
      }
    }
    fail_unless (found, "tag item %s is lost", name_sent);
  }
}

static void
do_xmp_tag_serialization_deserialization (const gchar * gsttag, GValue * value)
{
  GstTagList *taglist = gst_tag_list_new ();
  GstTagList *taglist2;
  GstBuffer *buf;

  gst_tag_list_add_value (taglist, GST_TAG_MERGE_REPLACE, gsttag, value);

  buf = gst_tag_list_to_xmp_buffer (taglist, TRUE);
  taglist2 = gst_tag_list_from_xmp_buffer (buf);

  tag_list_equals (taglist, taglist2);

  gst_buffer_unref (buf);
  gst_tag_list_free (taglist);
  gst_tag_list_free (taglist2);
}

GST_START_TEST (test_xmp_tags_serialization_deserialization)
{
  GValue value = { 0 };
  GDate *date;

  g_value_init (&value, G_TYPE_STRING);
  g_value_set_static_string (&value, "my string");
  do_xmp_tag_serialization_deserialization (GST_TAG_ARTIST, &value);
  do_xmp_tag_serialization_deserialization (GST_TAG_COPYRIGHT, &value);
  do_xmp_tag_serialization_deserialization (GST_TAG_DESCRIPTION, &value);
  do_xmp_tag_serialization_deserialization (GST_TAG_KEYWORDS, &value);
  do_xmp_tag_serialization_deserialization (GST_TAG_TITLE, &value);
  do_xmp_tag_serialization_deserialization (GST_TAG_VIDEO_CODEC, &value);
  do_xmp_tag_serialization_deserialization (GST_TAG_GEO_LOCATION_COUNTRY,
      &value);
  do_xmp_tag_serialization_deserialization (GST_TAG_GEO_LOCATION_CITY, &value);
  do_xmp_tag_serialization_deserialization (GST_TAG_GEO_LOCATION_SUBLOCATION,
      &value);
  do_xmp_tag_serialization_deserialization (GST_TAG_DEVICE_MANUFACTURER,
      &value);
  do_xmp_tag_serialization_deserialization (GST_TAG_DEVICE_MODEL, &value);

  g_value_set_static_string (&value, "rotate-0");
  do_xmp_tag_serialization_deserialization (GST_TAG_IMAGE_ORIENTATION, &value);
  g_value_set_static_string (&value, "flip-rotate-0");
  do_xmp_tag_serialization_deserialization (GST_TAG_IMAGE_ORIENTATION, &value);
  g_value_set_static_string (&value, "rotate-180");
  do_xmp_tag_serialization_deserialization (GST_TAG_IMAGE_ORIENTATION, &value);
  g_value_set_static_string (&value, "flip-rotate-180");
  do_xmp_tag_serialization_deserialization (GST_TAG_IMAGE_ORIENTATION, &value);
  g_value_set_static_string (&value, "flip-rotate-270");
  do_xmp_tag_serialization_deserialization (GST_TAG_IMAGE_ORIENTATION, &value);
  g_value_set_static_string (&value, "rotate-90");
  do_xmp_tag_serialization_deserialization (GST_TAG_IMAGE_ORIENTATION, &value);
  g_value_set_static_string (&value, "flip-rotate-90");
  do_xmp_tag_serialization_deserialization (GST_TAG_IMAGE_ORIENTATION, &value);
  g_value_set_static_string (&value, "rotate-270");
  do_xmp_tag_serialization_deserialization (GST_TAG_IMAGE_ORIENTATION, &value);

  g_value_unset (&value);
  g_value_init (&value, G_TYPE_DOUBLE);

  g_value_set_double (&value, 0.0);
  do_xmp_tag_serialization_deserialization (GST_TAG_GEO_LOCATION_LATITUDE,
      &value);
  do_xmp_tag_serialization_deserialization (GST_TAG_GEO_LOCATION_LONGITUDE,
      &value);
  g_value_set_double (&value, 10.5);
  do_xmp_tag_serialization_deserialization (GST_TAG_GEO_LOCATION_LATITUDE,
      &value);
  do_xmp_tag_serialization_deserialization (GST_TAG_GEO_LOCATION_LONGITUDE,
      &value);
  g_value_set_double (&value, -32.375);
  do_xmp_tag_serialization_deserialization (GST_TAG_GEO_LOCATION_LATITUDE,
      &value);
  do_xmp_tag_serialization_deserialization (GST_TAG_GEO_LOCATION_LONGITUDE,
      &value);

  g_value_set_double (&value, 0);
  do_xmp_tag_serialization_deserialization (GST_TAG_GEO_LOCATION_ELEVATION,
      &value);
  g_value_set_double (&value, 100);
  do_xmp_tag_serialization_deserialization (GST_TAG_GEO_LOCATION_ELEVATION,
      &value);
  g_value_set_double (&value, 500.25);
  do_xmp_tag_serialization_deserialization (GST_TAG_GEO_LOCATION_ELEVATION,
      &value);
  g_value_set_double (&value, -12.75);
  do_xmp_tag_serialization_deserialization (GST_TAG_GEO_LOCATION_ELEVATION,
      &value);

  g_value_set_double (&value, 0.0);
  do_xmp_tag_serialization_deserialization (GST_TAG_GEO_LOCATION_MOVEMENT_SPEED,
      &value);
  g_value_set_double (&value, 10.0);
  do_xmp_tag_serialization_deserialization (GST_TAG_GEO_LOCATION_MOVEMENT_SPEED,
      &value);
  g_value_set_double (&value, 786.125);
  do_xmp_tag_serialization_deserialization (GST_TAG_GEO_LOCATION_MOVEMENT_SPEED,
      &value);
  g_value_set_double (&value, -2.5);
  do_xmp_tag_serialization_deserialization (GST_TAG_GEO_LOCATION_MOVEMENT_SPEED,
      &value);

  g_value_set_double (&value, 0.0);
  do_xmp_tag_serialization_deserialization
      (GST_TAG_GEO_LOCATION_MOVEMENT_DIRECTION, &value);
  g_value_set_double (&value, 180.0);
  do_xmp_tag_serialization_deserialization
      (GST_TAG_GEO_LOCATION_MOVEMENT_DIRECTION, &value);
  g_value_set_double (&value, 359.99);
  do_xmp_tag_serialization_deserialization
      (GST_TAG_GEO_LOCATION_MOVEMENT_DIRECTION, &value);

  g_value_set_double (&value, 0.0);
  do_xmp_tag_serialization_deserialization
      (GST_TAG_GEO_LOCATION_CAPTURE_DIRECTION, &value);
  g_value_set_double (&value, 90.0);
  do_xmp_tag_serialization_deserialization
      (GST_TAG_GEO_LOCATION_CAPTURE_DIRECTION, &value);
  g_value_set_double (&value, 359.99);
  do_xmp_tag_serialization_deserialization
      (GST_TAG_GEO_LOCATION_CAPTURE_DIRECTION, &value);
  g_value_unset (&value);

  g_value_init (&value, GST_TYPE_DATE);
  date = g_date_new_dmy (22, 3, 2010);
  gst_value_set_date (&value, date);
  g_date_free (date);
  do_xmp_tag_serialization_deserialization (GST_TAG_DATE, &value);
  g_value_unset (&value);

  g_value_init (&value, G_TYPE_UINT);
  g_value_set_uint (&value, 0);
  do_xmp_tag_serialization_deserialization (GST_TAG_USER_RATING, &value);
  g_value_set_uint (&value, 100);
  do_xmp_tag_serialization_deserialization (GST_TAG_USER_RATING, &value);
  g_value_set_uint (&value, 22);
  do_xmp_tag_serialization_deserialization (GST_TAG_USER_RATING, &value);
  g_value_unset (&value);
}

GST_END_TEST;

GST_START_TEST (test_exif_parsing)
{
  GstTagList *taglist;
  GstBuffer *buf;
  GstByteWriter writer;
  const gchar *str;

  gst_byte_writer_init (&writer);

  /* write the IFD */
  /* 1 entry */
  gst_byte_writer_put_uint16_le (&writer, 1);

  /* copyright tag */
  /* tag id */
  gst_byte_writer_put_uint16_le (&writer, 0x8298);
  /* tag type */
  gst_byte_writer_put_uint16_le (&writer, 0x2);
  /* count */
  gst_byte_writer_put_uint32_le (&writer, strlen ("my copyright") + 1);
  /* offset */
  gst_byte_writer_put_uint32_le (&writer, 8 + 14);

  /* data */
  gst_byte_writer_put_string (&writer, "my copyright");

  buf = gst_byte_writer_reset_and_get_buffer (&writer);

  taglist = gst_tag_list_from_exif_buffer (buf, G_LITTLE_ENDIAN, 8);

  fail_unless (gst_structure_n_fields (taglist) == 1);
  fail_unless (gst_structure_has_field_typed (taglist, GST_TAG_COPYRIGHT,
          G_TYPE_STRING));
  str = gst_structure_get_string (taglist, GST_TAG_COPYRIGHT);
  fail_unless (strcmp (str, "my copyright") == 0);

  gst_tag_list_free (taglist);
  gst_buffer_unref (buf);
}

GST_END_TEST;


static void
do_exif_tag_serialization_deserialization (const gchar * gsttag, GValue * value)
{
  GstTagList *taglist = gst_tag_list_new ();
  GstTagList *taglist2;
  GstBuffer *buf;

  gst_tag_list_add_value (taglist, GST_TAG_MERGE_REPLACE, gsttag, value);

  /* LE */
  buf = gst_tag_list_to_exif_buffer (taglist, G_LITTLE_ENDIAN, 0);
  taglist2 = gst_tag_list_from_exif_buffer (buf, G_LITTLE_ENDIAN, 0);
  gst_buffer_unref (buf);

  tag_list_equals (taglist, taglist2);
  gst_tag_list_free (taglist2);

  /* BE */
  buf = gst_tag_list_to_exif_buffer (taglist, G_BIG_ENDIAN, 0);
  taglist2 = gst_tag_list_from_exif_buffer (buf, G_BIG_ENDIAN, 0);
  gst_buffer_unref (buf);

  tag_list_equals (taglist, taglist2);
  gst_tag_list_free (taglist2);

  /* APP1 */
  buf = gst_tag_list_to_exif_buffer_with_tiff_header (taglist);
  taglist2 = gst_tag_list_from_exif_buffer_with_tiff_header (buf);
  gst_buffer_unref (buf);

  tag_list_equals (taglist, taglist2);
  gst_tag_list_free (taglist2);

  gst_tag_list_free (taglist);
}

GST_START_TEST (test_exif_tags_serialization_deserialization)
{
  GValue value = { 0 };

  g_value_init (&value, G_TYPE_STRING);
  g_value_set_static_string (&value, "my string");
  do_exif_tag_serialization_deserialization (GST_TAG_COPYRIGHT, &value);
  g_value_set_static_string (&value, "ty");
  do_exif_tag_serialization_deserialization (GST_TAG_ARTIST, &value);

  /* image orientation tests */
  g_value_set_static_string (&value, "rotate-0");
  do_exif_tag_serialization_deserialization (GST_TAG_IMAGE_ORIENTATION, &value);
  g_value_set_static_string (&value, "flip-rotate-0");
  do_exif_tag_serialization_deserialization (GST_TAG_IMAGE_ORIENTATION, &value);
  g_value_set_static_string (&value, "rotate-180");
  do_exif_tag_serialization_deserialization (GST_TAG_IMAGE_ORIENTATION, &value);
  g_value_set_static_string (&value, "flip-rotate-180");
  do_exif_tag_serialization_deserialization (GST_TAG_IMAGE_ORIENTATION, &value);
  g_value_set_static_string (&value, "flip-rotate-270");
  do_exif_tag_serialization_deserialization (GST_TAG_IMAGE_ORIENTATION, &value);
  g_value_set_static_string (&value, "rotate-90");
  do_exif_tag_serialization_deserialization (GST_TAG_IMAGE_ORIENTATION, &value);
  g_value_set_static_string (&value, "flip-rotate-90");
  do_exif_tag_serialization_deserialization (GST_TAG_IMAGE_ORIENTATION, &value);
  g_value_set_static_string (&value, "rotate-270");
  do_exif_tag_serialization_deserialization (GST_TAG_IMAGE_ORIENTATION, &value);
  g_value_unset (&value);

  g_value_init (&value, G_TYPE_DOUBLE);
  g_value_set_double (&value, 30.5);
  do_exif_tag_serialization_deserialization (GST_TAG_GEO_LOCATION_LATITUDE,
      &value);
  g_value_set_double (&value, -12.125);
  do_exif_tag_serialization_deserialization (GST_TAG_GEO_LOCATION_LATITUDE,
      &value);
  g_value_set_double (&value, 0);
  do_exif_tag_serialization_deserialization (GST_TAG_GEO_LOCATION_LONGITUDE,
      &value);
  g_value_set_double (&value, 65.0);
  do_exif_tag_serialization_deserialization (GST_TAG_GEO_LOCATION_LONGITUDE,
      &value);
  g_value_set_double (&value, -0.75);
  do_exif_tag_serialization_deserialization (GST_TAG_GEO_LOCATION_LONGITUDE,
      &value);

  g_value_set_double (&value, 0.0);
  do_exif_tag_serialization_deserialization
      (GST_TAG_GEO_LOCATION_CAPTURE_DIRECTION, &value);
  g_value_set_double (&value, 180.5);
  do_exif_tag_serialization_deserialization
      (GST_TAG_GEO_LOCATION_CAPTURE_DIRECTION, &value);
  g_value_set_double (&value, 0.12345);
  do_exif_tag_serialization_deserialization
      (GST_TAG_GEO_LOCATION_MOVEMENT_DIRECTION, &value);
  g_value_set_double (&value, 359.9);
  do_exif_tag_serialization_deserialization
      (GST_TAG_GEO_LOCATION_MOVEMENT_DIRECTION, &value);

  g_value_set_double (&value, 0.0);
  do_exif_tag_serialization_deserialization
      (GST_TAG_GEO_LOCATION_ELEVATION, &value);
  g_value_set_double (&value, 321.456);
  do_exif_tag_serialization_deserialization
      (GST_TAG_GEO_LOCATION_ELEVATION, &value);
  g_value_set_double (&value, -12.56);
  do_exif_tag_serialization_deserialization
      (GST_TAG_GEO_LOCATION_ELEVATION, &value);

  g_value_set_double (&value, 0);
  do_exif_tag_serialization_deserialization
      (GST_TAG_GEO_LOCATION_MOVEMENT_SPEED, &value);
  g_value_set_double (&value, 100 / 3.6);
  do_exif_tag_serialization_deserialization
      (GST_TAG_GEO_LOCATION_MOVEMENT_SPEED, &value);
  g_value_unset (&value);
}

GST_END_TEST;

static Suite *
tag_suite (void)
{
  Suite *s = suite_create ("tag support library");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_muscibrainz_tag_registration);
  tcase_add_test (tc_chain, test_parse_extended_comment);
  tcase_add_test (tc_chain, test_vorbis_tags);
  tcase_add_test (tc_chain, test_id3_tags);
  tcase_add_test (tc_chain, test_id3v1_utf8_tag);
  tcase_add_test (tc_chain, test_language_utils);
  tcase_add_test (tc_chain, test_xmp_formatting);
  tcase_add_test (tc_chain, test_xmp_parsing);
  tcase_add_test (tc_chain, test_xmp_tags_serialization_deserialization);
  tcase_add_test (tc_chain, test_exif_parsing);
  tcase_add_test (tc_chain, test_exif_tags_serialization_deserialization);
  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = tag_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
