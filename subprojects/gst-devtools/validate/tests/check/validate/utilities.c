#include <gst/check/gstcheck.h>
#include <gst/validate/validate.h>
#include <gst/validate/gst-validate-utils.h>
#include "gst/gststructure.h"

GST_START_TEST (test_resolve_variables)
{
  GstStructure *expected, *variables =
      gst_structure_from_string
      ("vars, a=(string)1, b=(string)2, c=the_c_value", NULL);
  GstStructure *struct_with_vars =
      gst_structure_from_string ("test, n=\"$(a)/$(b)\"", NULL);

  gst_validate_structure_resolve_variables (NULL, struct_with_vars, variables,
      0);
  fail_unless_equals_string (gst_structure_get_string (struct_with_vars, "n"),
      "1/2");
  gst_structure_free (struct_with_vars);

  struct_with_vars =
      gst_structure_from_string
      ("test, sub_field=[sub, sub_field=\"$(a)\", subsub_field=[subsub, b_field=\"$(b)\", subsubsub_field=[subsubsub, subsubsubsub_field=\"$(c)\"]]]",
      NULL);

  gst_validate_structure_resolve_variables (NULL, struct_with_vars, variables,
      0);

  expected =
      gst_structure_from_string
      ("test, sub_field=[sub, sub_field=(string)1, subsub_field=[subsub, b_field=(string)2, subsubsub_field=[subsubsub, subsubsubsub_field=the_c_value]]]",
      NULL);
  fail_unless (gst_structure_is_equal (struct_with_vars, expected),
      "\nReplaced: `%s`\n!=\nExpected: `%s`",
      gst_structure_serialize_full (struct_with_vars, GST_SERIALIZE_FLAG_NONE),
      gst_structure_serialize_full (expected, GST_SERIALIZE_FLAG_NONE));

  gst_structure_free (variables);
  gst_structure_free (struct_with_vars);
  gst_structure_free (expected);
}

GST_END_TEST;

static Suite *
gst_validate_suite (void)
{
  Suite *s = suite_create ("utilities");
  TCase *tc_chain = tcase_create ("utilities");
  suite_add_tcase (s, tc_chain);

  if (atexit (gst_validate_deinit) != 0) {
    GST_ERROR ("failed to set gst_validate_deinit as exit function");
  }

  gst_validate_init ();
  tcase_add_test (tc_chain, test_resolve_variables);
  gst_validate_deinit ();

  return s;
}

GST_CHECK_MAIN (gst_validate);
