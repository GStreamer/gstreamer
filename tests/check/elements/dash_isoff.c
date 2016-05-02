#include "../../ext/dash/gstisoff.c"
#undef GST_CAT_DEFAULT

#include <gst/check/gstcheck.h>
#include <gst/base/base.h>

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

static Suite *
dash_isoff_suite (void)
{
  Suite *s = suite_create ("dash-isoff");
  TCase *tc_isoff_box = tcase_create ("isoff-box-parsing");

  tcase_add_test (tc_isoff_box, dash_isoff_box_header_minimal);
  tcase_add_test (tc_isoff_box, dash_isoff_box_header_long_size);
  tcase_add_test (tc_isoff_box, dash_isoff_box_header_uuid_type);
  tcase_add_test (tc_isoff_box, dash_isoff_box_header_uuid_type_long_size);

  suite_add_tcase (s, tc_isoff_box);

  return s;
}

GST_CHECK_MAIN (dash_isoff);
