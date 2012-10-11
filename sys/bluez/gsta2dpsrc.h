/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2012  Collabora Ltd.
 *
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef __GST_A2DP_SRC_H
#define __GST_A2DP_SRC_H

#include <gst/gst.h>

G_BEGIN_DECLS
#define GST_TYPE_A2DP_SRC \
	(gst_a2dp_src_get_type())
#define GST_A2DP_SRC(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_A2DP_SRC, GstA2dpSrc))
#define GST_A2DP_SRC_CLASS(klass) \
	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_A2DP_SRC, GstA2dpSrc))
#define GST_IS_A2DP_SRC(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_A2DP_SRC))
#define GST_IS_A2DP_SRC_CLASS(obj) \
	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_A2DP_SRC))
typedef struct _GstA2dpSrc GstA2dpSrc;
typedef struct _GstA2dpSrcClass GstA2dpSrcClass;

struct _GstA2dpSrcClass
{
  GstBinClass parentclass;
};

struct _GstA2dpSrc
{
  GstBin parent;

  GstElement *avdtpsrc;
  GstPad *srcpad;
};

gboolean gst_a2dp_src_plugin_init (GstPlugin * plugin);

G_END_DECLS
#endif
