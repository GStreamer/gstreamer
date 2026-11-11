/* GStreamer
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <glib/gstdio.h>
#include <gst/analytics/modelinfo.h>
#include <gst/check/gstcheck.h>

static GstAnalyticsModelInfo *
load_modelinfo (const gchar * contents)
{
  GstAnalyticsModelInfo *info;
  gchar *filename = NULL;
  gint fd;

  fd = g_file_open_tmp ("gst-modelinfo-XXXXXX", &filename, NULL);
  fail_unless (fd >= 0);
  fail_unless (g_close (fd, NULL));
  fail_unless (g_file_set_contents (filename, contents, -1, NULL));
  info = gst_analytics_modelinfo_load_from_file (filename);
  fail_unless_equals_int (g_remove (filename), 0);
  g_free (filename);

  return info;
}

/* *INDENT-OFF* */
static const gchar tensor_declarations[] =
"[modelinfo]\n"
"version=1.1\n"
"group-id=test\n"
"\n"
"[image]\n"
"dir=input\n"
"type=float32\n"
"dims=-1,3,224,224\n"
"caps=video/x-raw,format=RGB_F32LE\n"
"\n"
"[logits]\n"
"dir=output\n"
"type=float32\n"
"dims=1,1000\n"
"\n"
"[legacy]\n"
"type=float32\n"
"dims=1\n"
"caps=audio/x-raw,format=F32LE\n"
"\n"
"[tokens]\n"
"dir=input\n"
"type=int64\n"
"dims=1,128\n"
"caps=audio/x-raw,format=F32LE\n"
"\n"
"[invalid]\n"
"dir=unknown\n"
"type=unknown\n"
"dims=1,bad\n";
/* *INDENT-ON* */

GST_START_TEST (test_tensor_declarations)
{
  GstAnalyticsModelInfo *info;
  GstTensorDataType type;
  gchar **names;
  gsize num_dims;
  gint *dims;

  info = load_modelinfo (tensor_declarations);
  fail_unless (info);

  names = gst_analytics_modelinfo_get_tensor_names (info,
      MODELINFO_DIRECTION_INPUT);
  fail_unless_equals_int (g_strv_length (names), 2);
  fail_unless_equals_string (names[0], "image");
  fail_unless_equals_string (names[1], "tokens");
  g_strfreev (names);

  names = gst_analytics_modelinfo_get_tensor_names (info,
      MODELINFO_DIRECTION_OUTPUT);
  fail_unless_equals_int (g_strv_length (names), 1);
  fail_unless_equals_string (names[0], "logits");
  g_strfreev (names);

  names = gst_analytics_modelinfo_get_tensor_names (info,
      MODELINFO_DIRECTION_UNKNOWN);
  fail_unless_equals_int (g_strv_length (names), 3);
  fail_unless_equals_string (names[0], "image");
  fail_unless_equals_string (names[1], "logits");
  fail_unless_equals_string (names[2], "tokens");
  g_strfreev (names);

  fail_unless (gst_analytics_modelinfo_get_tensor_data_type (info, "tokens",
          &type));
  fail_unless_equals_int (type, GST_TENSOR_DATA_TYPE_INT64);
  fail_if (gst_analytics_modelinfo_get_tensor_data_type (info, "invalid",
          &type));
  fail_if (gst_analytics_modelinfo_get_tensor_data_type (info, "missing",
          &type));

  fail_unless (gst_analytics_modelinfo_get_tensor_dims (info, "image",
          &num_dims, &dims));
  fail_unless_equals_int (num_dims, 4);
  fail_unless_equals_int (dims[0], -1);
  fail_unless_equals_int (dims[1], 3);
  fail_unless_equals_int (dims[2], 224);
  fail_unless_equals_int (dims[3], 224);
  g_free (dims);

  fail_if (gst_analytics_modelinfo_get_tensor_dims (info, "invalid",
          &num_dims, &dims));
  fail_if (gst_analytics_modelinfo_get_tensor_dims (info, "missing",
          &num_dims, &dims));

  gst_analytics_modelinfo_free (info);
}

GST_END_TEST;

/* *INDENT-OFF* */
static const gchar unsupported_version[] =
"[modelinfo]\n"
"version=99.0\n";

static const gchar missing_input_caps[] =
"[modelinfo]\n"
"version=1.1\n"
"\n"
"[input]\n"
"dir=input\n"
"type=float32\n"
"dims=1\n";

static const gchar legacy_input_without_caps[] =
"[modelinfo]\n"
"version=1.0\n"
"\n"
"[input]\n"
"dir=input\n"
"type=float32\n"
"dims=1\n";
/* *INDENT-ON* */

GST_START_TEST (test_load_from_file_validation)
{
  GstAnalyticsModelInfo *info;
  gchar **names;

  info = load_modelinfo (unsupported_version);
  fail_unless (info == NULL);

  info = load_modelinfo (missing_input_caps);
  fail_unless (info == NULL);

  info = load_modelinfo (legacy_input_without_caps);
  fail_unless (info);
  names = gst_analytics_modelinfo_get_tensor_names (info,
      MODELINFO_DIRECTION_OUTPUT);
  fail_unless (names);
  fail_unless (names[0] == NULL);

  g_strfreev (names);
  gst_analytics_modelinfo_free (info);
}

GST_END_TEST;

static Suite *
analytics_modelinfo_suite (void)
{
  Suite *s = suite_create ("Analytics ModelInfo");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_add_test (tc, test_tensor_declarations);
  tcase_add_test (tc, test_load_from_file_validation);
  return s;
}

GST_CHECK_MAIN (analytics_modelinfo);
