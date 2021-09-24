/* GStreamer
 * Copyright (C) 2013 Sebastian Rasmussen <sebras@hotmail.com>
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

#include <gst/check/gstcheck.h>

#include <rtsp-permissions.h>

GST_START_TEST (test_permissions)
{
  GstRTSPPermissions *perms;
  GstRTSPPermissions *copy;
  GstStructure *role_structure;

  perms = gst_rtsp_permissions_new ();
  fail_if (gst_rtsp_permissions_is_allowed (perms, "missing", "permission1"));
  gst_rtsp_permissions_unref (perms);

  perms = gst_rtsp_permissions_new ();
  gst_rtsp_permissions_add_role (perms, "user",
      "permission1", G_TYPE_BOOLEAN, TRUE,
      "permission2", G_TYPE_BOOLEAN, FALSE, NULL);
  fail_unless (gst_rtsp_permissions_is_allowed (perms, "user", "permission1"));
  fail_if (gst_rtsp_permissions_is_allowed (perms, "user", "permission2"));
  fail_if (gst_rtsp_permissions_is_allowed (perms, "user", "missing"));
  fail_if (gst_rtsp_permissions_is_allowed (perms, "missing", "permission1"));
  copy = GST_RTSP_PERMISSIONS (gst_mini_object_copy (GST_MINI_OBJECT (perms)));
  gst_rtsp_permissions_unref (perms);
  fail_unless (gst_rtsp_permissions_is_allowed (copy, "user", "permission1"));
  fail_if (gst_rtsp_permissions_is_allowed (copy, "user", "permission2"));
  gst_rtsp_permissions_unref (copy);

  perms = gst_rtsp_permissions_new ();
  gst_rtsp_permissions_add_role (perms, "admin",
      "permission1", G_TYPE_BOOLEAN, TRUE,
      "permission2", G_TYPE_BOOLEAN, TRUE, NULL);
  gst_rtsp_permissions_add_role (perms, "user",
      "permission1", G_TYPE_BOOLEAN, TRUE,
      "permission2", G_TYPE_BOOLEAN, FALSE, NULL);
  fail_unless (gst_rtsp_permissions_is_allowed (perms, "admin", "permission1"));
  fail_unless (gst_rtsp_permissions_is_allowed (perms, "admin", "permission2"));
  fail_unless (gst_rtsp_permissions_is_allowed (perms, "user", "permission1"));
  fail_if (gst_rtsp_permissions_is_allowed (perms, "user", "permission2"));
  gst_rtsp_permissions_unref (perms);

  perms = gst_rtsp_permissions_new ();
  gst_rtsp_permissions_add_role (perms, "user",
      "permission1", G_TYPE_BOOLEAN, TRUE,
      "permission2", G_TYPE_BOOLEAN, FALSE, NULL);
  fail_unless (gst_rtsp_permissions_is_allowed (perms, "user", "permission1"));
  fail_if (gst_rtsp_permissions_is_allowed (perms, "user", "permission2"));
  gst_rtsp_permissions_add_role (perms, "user",
      "permission1", G_TYPE_BOOLEAN, FALSE,
      "permission2", G_TYPE_BOOLEAN, TRUE, NULL);
  fail_if (gst_rtsp_permissions_is_allowed (perms, "user", "permission1"));
  fail_unless (gst_rtsp_permissions_is_allowed (perms, "user", "permission2"));
  gst_rtsp_permissions_unref (perms);

  perms = gst_rtsp_permissions_new ();
  gst_rtsp_permissions_add_role (perms, "admin",
      "permission1", G_TYPE_BOOLEAN, TRUE,
      "permission2", G_TYPE_BOOLEAN, TRUE, NULL);
  gst_rtsp_permissions_add_role (perms, "user",
      "permission1", G_TYPE_BOOLEAN, TRUE,
      "permission2", G_TYPE_BOOLEAN, FALSE, NULL);
  fail_unless (gst_rtsp_permissions_is_allowed (perms, "admin", "permission1"));
  fail_unless (gst_rtsp_permissions_is_allowed (perms, "admin", "permission2"));
  fail_unless (gst_rtsp_permissions_is_allowed (perms, "user", "permission1"));
  fail_if (gst_rtsp_permissions_is_allowed (perms, "user", "permission2"));
  gst_rtsp_permissions_remove_role (perms, "user");
  fail_unless (gst_rtsp_permissions_is_allowed (perms, "admin", "permission1"));
  fail_unless (gst_rtsp_permissions_is_allowed (perms, "admin", "permission2"));
  fail_if (gst_rtsp_permissions_is_allowed (perms, "user", "permission1"));
  fail_if (gst_rtsp_permissions_is_allowed (perms, "user", "permission2"));

  /* _add_permission_for_role() should overwrite existing or create new role */
  fail_unless (gst_rtsp_permissions_is_allowed (perms, "admin", "permission1"));
  gst_rtsp_permissions_add_permission_for_role (perms, "admin", "permission1",
      FALSE);
  fail_if (gst_rtsp_permissions_is_allowed (perms, "admin", "permission1"));

  fail_if (gst_rtsp_permissions_is_allowed (perms, "tester", "permission1"));
  gst_rtsp_permissions_add_permission_for_role (perms, "tester", "permission1",
      TRUE);
  fail_unless (gst_rtsp_permissions_is_allowed (perms, "tester",
          "permission1"));
  gst_rtsp_permissions_add_permission_for_role (perms, "tester", "permission1",
      FALSE);
  fail_if (gst_rtsp_permissions_is_allowed (perms, "tester", "permission1"));
  gst_rtsp_permissions_add_permission_for_role (perms, "tester", "permission2",
      TRUE);
  fail_unless (gst_rtsp_permissions_is_allowed (perms, "tester",
          "permission2"));
  fail_if (gst_rtsp_permissions_is_allowed (perms, "tester", "permission3"));

  gst_rtsp_permissions_add_role_empty (perms, "noone");
  fail_if (gst_rtsp_permissions_is_allowed (perms, "noone", "permission1"));

  role_structure = gst_structure_new ("tester", "permission1", G_TYPE_BOOLEAN,
      TRUE, NULL);
  gst_rtsp_permissions_add_role_from_structure (perms, role_structure);
  gst_structure_free (role_structure);
  fail_unless (gst_rtsp_permissions_is_allowed (perms, "tester",
          "permission1"));
  fail_if (gst_rtsp_permissions_is_allowed (perms, "tester", "permission2"));

  gst_rtsp_permissions_unref (perms);
}

GST_END_TEST;

static Suite *
rtsppermissions_suite (void)
{
  Suite *s = suite_create ("rtsppermissions");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_set_timeout (tc, 20);
  tcase_add_test (tc, test_permissions);

  return s;
}

GST_CHECK_MAIN (rtsppermissions);
