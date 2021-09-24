#include <gst/check/gstcheck.h>
#include <gst/validate/validate.h>
#include <gst/validate/gst-validate-utils.h>

GST_START_TEST (test_resolve_variables)
{
  GstStructure *s1 =
      gst_structure_from_string ("vars, a=(string)1, b=(string)2", NULL);
  GstStructure *s2 = gst_structure_from_string ("test, n=\"$(a)/$(b)\"", NULL);

  gst_validate_structure_resolve_variables (NULL, s2, s1, 0);
  fail_unless_equals_string (gst_structure_get_string (s2, "n"), "1/2");
  gst_structure_free (s1);
  gst_structure_free (s2);
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
