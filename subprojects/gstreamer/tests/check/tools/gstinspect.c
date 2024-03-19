/* GStreamer gst-inspect unit test
 * Copyright (C) 2012 Tim-Philipp Müller <tim centricular net>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/* FIXME 2.0: suppress warnings for deprecated API such as GValueArray
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include <config.h>
#include <gst/check/gstcheck.h>

static int gst_inspect_main (int argc, char **argv);

#define main gst_inspect_main
#include "../../tools/gst-inspect.c"
#undef main

// A plugin whose version does not match the gstreamer major/minor
// see https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/6191
#define TEST_PLUGIN_VERSION "0.1.0"
#define TEST_ELEMENT_NAME "local_test_bin"
static gboolean
test_plugin_init (G_GNUC_UNUSED GstPlugin * plugin)
{
  gst_element_register (plugin, TEST_ELEMENT_NAME, GST_RANK_NONE, GST_TYPE_BIN);
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR,
    test_plugin, "Test Plugin", test_plugin_init, TEST_PLUGIN_VERSION,
    "LGPL", "gsttestplugin", "testing");

GST_START_TEST (test_exists)
{
#define ARGV_LEN (G_N_ELEMENTS (argv) - 1)

  gst_plugin_test_plugin_register ();

  {
    const gchar *argv[] = { "gst-inspect-1.0", "--exists", "foo", NULL };

    fail_unless_equals_int (gst_inspect_main (ARGV_LEN, (gchar **) argv), 1);
  }
  {
    const gchar *argv[] = { "gst-inspect-1.0", "--exists", "bin", NULL };

    fail_unless_equals_int (gst_inspect_main (ARGV_LEN, (gchar **) argv), 0);
  }
  {
    // --exists should work even if the plugin's version does not equal
    // the gstreamer version (i.e., the --atleast-version check is not
    // implicitly enforced when not present).
    const gchar *argv[] = { "gst-inspect-1.0", "--exists",
      TEST_ELEMENT_NAME, NULL
    };

    fail_unless_equals_int (gst_inspect_main (ARGV_LEN, (gchar **) argv), 0);
  }
  {
    const gchar *argv[] = { "gst-inspect-1.0", "--exists",
      "--atleast-version=" VERSION, "bin", NULL
    };

    fail_unless_equals_int (gst_inspect_main (ARGV_LEN, (gchar **) argv), 0);
  }
  {
    const gchar *argv[] = { "gst-inspect-1.0", "--exists",
      "--atleast-version=1.0", "bin", NULL
    };

    fail_unless_equals_int (gst_inspect_main (ARGV_LEN, (gchar **) argv), 0);
  }
  {
    const gchar *argv[] = { "gst-inspect-1.0", "--exists",
      "--atleast-version=1.0.0", "bin", NULL
    };

    fail_unless_equals_int (gst_inspect_main (ARGV_LEN, (gchar **) argv), 0);
  }
  {
    const gchar *argv[] = { "gst-inspect-1.0", "--exists",
      "--atleast-version=1.2.0", "bin", NULL
    };

    fail_unless_equals_int (gst_inspect_main (ARGV_LEN, (gchar **) argv), 0);
  }
  {
    const gchar *argv[] = { "gst-inspect-1.0", "--exists",
      "--atleast-version=2.0", "bin", NULL
    };

    fail_unless_equals_int (gst_inspect_main (ARGV_LEN, (gchar **) argv), 2);
  }
  {
    const gchar *argv[] = { "gst-inspect-1.0", "--exists",
      "--atleast-version=2.0.0", "bin", NULL
    };

    fail_unless_equals_int (gst_inspect_main (ARGV_LEN, (gchar **) argv), 2);
  }
  {
    const gchar *argv[] = { "gst-inspect-1.0", "--exists",
      "--atleast-version=1.44", "bin", NULL
    };

    fail_unless_equals_int (gst_inspect_main (ARGV_LEN, (gchar **) argv), 2);
  }
  {
    const gchar *argv[] = { "gst-inspect-1.0", "--exists",
      "--atleast-version=1.60.4", "bin", NULL
    };

    fail_unless_equals_int (gst_inspect_main (ARGV_LEN, (gchar **) argv), 2);
  }
  {
    // The 'atleast-version' supplied here will not match the test plugin's
    // version, above, so the test case should return "2" because the test
    // plugin's 0.1.0 will not meet the minimum version specified by the arg.
    gchar *atleast = g_strdup_printf ("--atleast-version=%d.%d",
        GST_VERSION_MAJOR, GST_VERSION_MINOR);
    const gchar *argv[] = { "gst-inspect-1.0", "--exists",
      atleast, TEST_ELEMENT_NAME, NULL
    };

    fail_unless_equals_int (gst_inspect_main (ARGV_LEN, (gchar **) argv), 2);
    g_free (atleast);
  }
  {
    /* check for plugin should fail like this */
    const gchar *argv[] = { "gst-inspect-1.0", "--exists",
      "--atleast-version=1.0", "coreelements", NULL
    };

    fail_unless_equals_int (gst_inspect_main (ARGV_LEN, (gchar **) argv), 1);
  }
}

GST_END_TEST;

static Suite *
gstabi_suite (void)
{
  Suite *s = suite_create ("gst-inspect");
  TCase *tc_chain = tcase_create ("gst-inspect");

  tcase_set_timeout (tc_chain, 0);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_exists);
  return s;
}

GST_CHECK_MAIN (gstabi);
