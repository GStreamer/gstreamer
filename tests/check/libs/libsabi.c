/* GStreamer
 * Copyright (C) 2005 Wim Taymans <wim@fluendo.com>
 *
 * gstabi.c: Unit test for ABI compatibility
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <config.h>
#include <gst/check/gstcheck.h>

#include <gst/base/gstadapter.h>
#include <gst/base/gstbasesink.h>
#include <gst/base/gstbasesrc.h>
#include <gst/base/gstbasetransform.h>
#include <gst/base/gstcollectpads.h>
#include <gst/base/gstpushsrc.h>
#include <gst/base/gsttypefindhelper.h>
#include <gst/controller/gstcontroller.h>
#include <gst/net/gstnet.h>
#include <gst/net/gstnetclientclock.h>
#include <gst/net/gstnettimepacket.h>
#include <gst/net/gstnettimeprovider.h>


typedef struct
{
  char *name;
  int size;
  int abi_size;
}
Struct;

#ifdef HAVE_CPU_I386
#include "struct_i386.h"
#define HAVE_ABI_SIZES
#else
/* in case someone wants to generate a new arch */
#include "struct_i386.h"
#endif


GST_START_TEST (test_ABI)
{
#ifdef HAVE_ABI_SIZES
  gboolean ok = TRUE;
  gint i;

  for (i = 0; list[i].name; i++) {
    if (list[i].size != list[i].abi_size) {
      ok = FALSE;
      g_print ("sizeof(%s) is %d, expected %d\n",
          list[i].name, list[i].size, list[i].abi_size);
    }
  }
  fail_unless (ok, "failed ABI check");
#else
  g_print ("No structure size list was generated for this architecture\n");
  g_print ("ignoring\n");
#endif
}

GST_END_TEST;

Suite *
gstabi_suite (void)
{
  Suite *s = suite_create ("LibsABI");
  TCase *tc_chain = tcase_create ("size check");

  tcase_set_timeout (tc_chain, 0);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_ABI);
  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = gstabi_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
