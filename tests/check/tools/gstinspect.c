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

GST_START_TEST (test_exists)
{
#define ARGV_LEN (G_N_ELEMENTS (argv) - 1)

  {
    const gchar *argv[] = { "gst-inspect-1.0", "--exists", "foo", NULL };

    fail_unless_equals_int (gst_inspect_main (ARGV_LEN, (gchar **) argv), 1);
  }
  {
    const gchar *argv[] = { "gst-inspect-1.0", "--exists", "bin", NULL };

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

    fail_unless_equals_int (gst_inspect_main (ARGV_LEN, (gchar **) argv), 1);
  }
  {
    const gchar *argv[] = { "gst-inspect-1.0", "--exists",
      "--atleast-version=2.0.0", "bin", NULL
    };

    fail_unless_equals_int (gst_inspect_main (ARGV_LEN, (gchar **) argv), 1);
  }
  {
    const gchar *argv[] = { "gst-inspect-1.0", "--exists",
      "--atleast-version=1.44", "bin", NULL
    };

    fail_unless_equals_int (gst_inspect_main (ARGV_LEN, (gchar **) argv), 1);
  }
  {
    const gchar *argv[] = { "gst-inspect-1.0", "--exists",
      "--atleast-version=1.60.4", "bin", NULL
    };

    fail_unless_equals_int (gst_inspect_main (ARGV_LEN, (gchar **) argv), 1);
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
