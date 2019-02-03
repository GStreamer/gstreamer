#include <gst/check/gstcheck.h>
#include <glib/gstdio.h>
#include <gst/validate/validate.h>

GST_START_TEST (test_expression_parser)
{
  GstClockTime start;
  GstValidateRunner *runner = gst_validate_runner_new ();
  GstValidateActionType *set_vars = gst_validate_get_action_type ("set-vars");
  GstValidateActionType *seek_type = gst_validate_get_action_type ("seek");
  GstValidateScenario *scenario =
      g_object_new (GST_TYPE_VALIDATE_SCENARIO, "validate-runner",
      runner, NULL);
  GstValidateAction *action;

  fail_unless (seek_type);

  action = gst_validate_action_new (scenario, set_vars,
      gst_structure_from_string
      ("set-vars, a=(string)\"50\", b=(string)\"70\", default_flags=flush",
          NULL), FALSE);
  fail_unless_equals_int (gst_validate_execute_action (set_vars, action),
      GST_VALIDATE_EXECUTE_ACTION_OK);
  gst_validate_action_unref (action);

  action = gst_validate_action_new (scenario, seek_type,
      gst_structure_from_string
      ("seek, start=\"min($(a), $(b))\", flags=\"$(default_flags)\"", NULL),
      FALSE);
  fail_unless (action);

  fail_unless (seek_type->prepare (action));
  fail_unless (gst_validate_action_get_clocktime (scenario, action, "start",
          &start));
  fail_unless_equals_uint64 (start, 50 * GST_SECOND);
  gst_validate_action_unref (action);

  gst_object_unref (runner);
}

GST_END_TEST;

static Suite *
gst_validate_suite (void)
{
  Suite *s = suite_create ("registry");
  TCase *tc_chain = tcase_create ("registry");
  suite_add_tcase (s, tc_chain);

  if (atexit (gst_validate_deinit) != 0) {
    GST_ERROR ("failed to set gst_validate_deinit as exit function");
  }

  g_setenv ("GST_VALIDATE_REPORTING_DETAILS", "all", TRUE);
  gst_validate_init ();
  tcase_add_test (tc_chain, test_expression_parser);
  gst_validate_deinit ();

  return s;
}

GST_CHECK_MAIN (gst_validate);
