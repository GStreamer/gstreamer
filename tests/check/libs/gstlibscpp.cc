/* GStreamer
 * Copyright (C) 2011 Tim-Philipp Müller <tim centricular net>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/gst.h>
#include <gst/check/gstcheck.h>

#include <gst/base/gstadapter.h>
#include <gst/base/gstbasesink.h>
#include <gst/base/gstbasesrc.h>
#include <gst/base/gstbasetransform.h>
#include <gst/base/gstbitreader.h>
#include <gst/base/gstbytereader.h>
#include <gst/base/gstbytewriter.h>
#include <gst/base/gstcollectpads.h>
#include <gst/base/gstdataqueue.h>
#include <gst/base/gstpushsrc.h>
#include <gst/base/gsttypefindhelper.h>

#include <gst/controller/gstcontroller.h>
#include <gst/controller/gstcontrollerprivate.h>
#include <gst/controller/gstcontrolsource.h>
#include <gst/controller/gstinterpolationcontrolsource.h>
#include <gst/controller/gstinterpolationcontrolsourceprivate.h>
#include <gst/controller/gstlfocontrolsource.h>
#include <gst/controller/gstlfocontrolsourceprivate.h>

#include <gst/dataprotocol/dataprotocol.h>

#include <gst/net/gstnetclientclock.h>
#include <gst/net/gstnet.h>
#include <gst/net/gstnettimepacket.h>
#include <gst/net/gstnettimeprovider.h>

/* we mostly just want to make sure that our library headers don't
 * contain anything a C++ compiler might not like */
GST_START_TEST (test_nothing)
{
  gst_init (NULL, NULL);
}

GST_END_TEST;

static Suite *
libscpp_suite (void)
{
  Suite *s = suite_create ("GstLibsCpp");
  TCase *tc_chain = tcase_create ("C++ libs header tests");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_nothing);

  return s;
}

GST_CHECK_MAIN (libscpp);
