#include "../../ext/dash/gstisoff.c"
#undef GST_CAT_DEFAULT

#include <gst/check/gstcheck.h>
#include <gst/base/base.h>

GST_DEBUG_CATEGORY (gst_dash_demux_debug);

#include "dash_isoff.h"

GST_START_TEST (dash_isoff_box_header_minimal)
{
  /* INDENT-OFF */
  static const guint8 data[] = {
    16, 32, 64, 128,
    't', 'e', 's', 't'
  };
  /* INDENT-ON */
  GstByteReader reader = GST_BYTE_READER_INIT (data, sizeof (data));
  guint32 type;
  guint8 extended_type[16];
  guint header_size;
  guint64 size;

  fail_unless (gst_isoff_parse_box_header (&reader, &type, extended_type,
          &header_size, &size));
  fail_unless (type == GST_MAKE_FOURCC ('t', 'e', 's', 't'));
  fail_unless_equals_int (header_size, 8);
  fail_unless_equals_uint64 (size, 0x10204080);
}

GST_END_TEST;

GST_START_TEST (dash_isoff_box_header_long_size)
{
  /* INDENT-OFF */
  static const guint8 data[] = {
    0, 0, 0, 1,
    't', 'e', 's', 't',
    1, 2, 4, 8, 16, 32, 64, 128
  };
  /* INDENT-ON */
  GstByteReader reader = GST_BYTE_READER_INIT (data, sizeof (data));
  guint32 type;
  guint8 extended_type[16];
  guint header_size;
  guint64 size;

  fail_unless (gst_isoff_parse_box_header (&reader, &type, extended_type,
          &header_size, &size));
  fail_unless (type == GST_MAKE_FOURCC ('t', 'e', 's', 't'));
  fail_unless_equals_int (header_size, 16);
  fail_unless_equals_uint64 (size, G_GUINT64_CONSTANT (0x0102040810204080));
}

GST_END_TEST;

GST_START_TEST (dash_isoff_box_header_uuid_type)
{
  /* INDENT-OFF */
  static const guint8 data[] = {
    16, 32, 64, 128,
    'u', 'u', 'i', 'd',
    'a', 'b', 'c', 'd',
    'e', 'f', 'g', 'h',
    'i', 'j', 'k', 'l',
    'm', 'n', 'o', 'p'
  };
  /* INDENT-ON */
  GstByteReader reader = GST_BYTE_READER_INIT (data, sizeof (data));
  guint32 type;
  guint8 extended_type[16];
  guint header_size;
  guint64 size;

  fail_unless (gst_isoff_parse_box_header (&reader, &type, extended_type,
          &header_size, &size));
  fail_unless (type == GST_MAKE_FOURCC ('u', 'u', 'i', 'd'));
  fail_unless_equals_int (header_size, 24);
  fail_unless_equals_uint64 (size, 0x10204080);
  fail_unless (memcmp (data + 8, extended_type, 16) == 0);
}

GST_END_TEST;

GST_START_TEST (dash_isoff_box_header_uuid_type_long_size)
{
  /* INDENT-OFF */
  static const guint8 data[] = {
    0, 0, 0, 1,
    'u', 'u', 'i', 'd',
    1, 2, 4, 8, 16, 32, 64, 128,
    'a', 'b', 'c', 'd',
    'e', 'f', 'g', 'h',
    'i', 'j', 'k', 'l',
    'm', 'n', 'o', 'p'
  };
  /* INDENT-ON */
  GstByteReader reader = GST_BYTE_READER_INIT (data, sizeof (data));
  guint32 type;
  guint8 extended_type[16];
  guint header_size;
  guint64 size;

  fail_unless (gst_isoff_parse_box_header (&reader, &type, extended_type,
          &header_size, &size));
  fail_unless (type == GST_MAKE_FOURCC ('u', 'u', 'i', 'd'));
  fail_unless_equals_int (header_size, 32);
  fail_unless_equals_uint64 (size, G_GUINT64_CONSTANT (0x0102040810204080));
  fail_unless (memcmp (data + 16, extended_type, 16) == 0);
}

GST_END_TEST;

GST_START_TEST (dash_isoff_moof_parse)
{
  /* INDENT-ON */
  GstByteReader reader = GST_BYTE_READER_INIT (moof1, sizeof (moof1));
  guint32 type;
  guint8 extended_type[16];
  guint header_size;
  guint64 size;
  GstMoofBox *moof;
  GstTrafBox *traf;
  GstTrunBox *trun;
  guint i;

  fail_unless (gst_isoff_parse_box_header (&reader, &type, extended_type,
          &header_size, &size));
  fail_unless (type == GST_MAKE_FOURCC ('m', 'o', 'o', 'f'));
  fail_unless_equals_int (header_size, 8);
  fail_unless_equals_uint64 (size, sizeof (moof1));

  moof = gst_isoff_moof_box_parse (&reader);
  fail_unless (moof != NULL);

  fail_unless_equals_int (moof->mfhd.sequence_number, 1);
  fail_unless_equals_int (moof->traf->len, 1);

  traf = &g_array_index (moof->traf, GstTrafBox, 0);
  fail_unless_equals_int (traf->tfhd.version, 0);
  fail_unless_equals_int (traf->tfhd.flags,
      GST_TFHD_FLAGS_DEFAULT_SAMPLE_DURATION_PRESENT);
  fail_unless_equals_int (traf->tfhd.track_id, 1);
  fail_unless_equals_uint64 (traf->tfhd.base_data_offset, 0);
  fail_unless_equals_int (traf->tfhd.sample_description_index, 0);
  fail_unless_equals_int (traf->tfhd.default_sample_duration, 8);
  fail_unless_equals_int (traf->tfhd.default_sample_size, 0);
  fail_unless_equals_int (traf->tfhd.default_sample_flags, 0);

  fail_unless_equals_int (traf->trun->len, 1);
  trun = &g_array_index (traf->trun, GstTrunBox, 0);

  fail_unless_equals_int (trun->version, 1);
  fail_unless_equals_int (trun->flags,
      GST_TRUN_FLAGS_SAMPLE_COMPOSITION_TIME_OFFSETS_PRESENT |
      GST_TRUN_FLAGS_SAMPLE_FLAGS_PRESENT | GST_TRUN_FLAGS_SAMPLE_SIZE_PRESENT |
      GST_TRUN_FLAGS_DATA_OFFSET_PRESENT);
  fail_unless_equals_int (trun->sample_count, 96);
  fail_unless_equals_int (trun->data_offset, size + header_size);
  fail_unless_equals_int (trun->first_sample_flags, 0);

  fail_unless_equals_int (trun->samples->len, 96);

  for (i = 0; i < 96; i++) {
    GstTrunSample *sample = &g_array_index (trun->samples, GstTrunSample, i);

    fail_unless_equals_int (sample->sample_duration, 0);
    if (i == 0) {
      /* sample_depends_on = 2 => I-frame */
      /* sample_is_non_sync_sample = 0 */
      fail_unless_equals_int (sample->sample_flags, 0x02000000);
    } else {
      /* sample_depends_on = 1 => non-I-frame */
      /* sample_is_non_sync_sample = 1 */
      fail_unless_equals_int (sample->sample_flags, 0x01010000);
    }

    /* sample size and CTO is changing for each sample */
  }

  gst_isoff_moof_box_free (moof);
}

GST_END_TEST;

static Suite *
dash_isoff_suite (void)
{
  Suite *s = suite_create ("dash-isoff");
  TCase *tc_isoff_box = tcase_create ("isoff-box-parsing");
  TCase *tc_moof = tcase_create ("moof");

  GST_DEBUG_CATEGORY_INIT (gst_dash_demux_debug, "gst_dash_demux_debug", 0,
      "mpeg dash tests");

  tcase_add_test (tc_isoff_box, dash_isoff_box_header_minimal);
  tcase_add_test (tc_isoff_box, dash_isoff_box_header_long_size);
  tcase_add_test (tc_isoff_box, dash_isoff_box_header_uuid_type);
  tcase_add_test (tc_isoff_box, dash_isoff_box_header_uuid_type_long_size);

  suite_add_tcase (s, tc_isoff_box);


  tcase_add_test (tc_moof, dash_isoff_moof_parse);
  suite_add_tcase (s, tc_moof);

  return s;
}

GST_CHECK_MAIN (dash_isoff);
