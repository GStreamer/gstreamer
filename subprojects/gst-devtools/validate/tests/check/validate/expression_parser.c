#include <gst/check/gstcheck.h>
#include <glib/gstdio.h>
#include <gst/validate/validate.h>
#include <gst/validate/gst-validate-utils.h>

static int
get_var (const gchar * name, double *value, gpointer udata)
{
  *value = (double) GPOINTER_TO_INT (udata);

  return 1;
}

GST_START_TEST (test_expression_parser)
{
  fail_unless_equals_float (gst_validate_utils_parse_expression ("10 / 2", NULL,
          NULL, NULL), 5.0);

  fail_unless_equals_float (gst_validate_utils_parse_expression ("10 / 0.5",
          NULL, NULL, NULL), 20);

  fail_unless_equals_float (gst_validate_utils_parse_expression
      ("max(100, (10 / 0.1))", NULL, NULL, NULL), 100);

  fail_unless_equals_float (gst_validate_utils_parse_expression
      ("min(10, (duration - 0.1) / 0.1)", get_var, GINT_TO_POINTER (1), NULL),
      9);
}

GST_END_TEST;

static void
setup (void)
{
  gst_validate_init ();
}

static void
teardown (void)
{
  gst_validate_deinit ();
}

static Suite *
gst_validate_suite (void)
{
  Suite *s = suite_create ("registry");
  TCase *tc_chain = tcase_create ("registry");
  suite_add_tcase (s, tc_chain);
  tcase_add_checked_fixture (tc_chain, setup, teardown);

  tcase_add_test (tc_chain, test_expression_parser);
  g_setenv ("GST_VALIDATE_REPORTING_DETAILS", "all", TRUE);

  return s;
}

GST_CHECK_MAIN (gst_validate);
