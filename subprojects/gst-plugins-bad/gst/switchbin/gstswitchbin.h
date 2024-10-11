/* switchbin
 * Copyright (C) 2016  Carlos Rafael Giani
 *
 * gstswitchbin.h: Header for GstSwitchBin object
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


#ifndef GSTSWITCHBIN_H___
#define GSTSWITCHBIN_H___

#include <gst/gst.h>


G_BEGIN_DECLS


typedef struct _GstSwitchBin GstSwitchBin;
typedef struct _GstSwitchBinClass GstSwitchBinClass;
typedef struct _GstSwitchBinPath GstSwitchBinPath;


#define GST_TYPE_SWITCH_BIN             (gst_switch_bin_get_type())
#define GST_SWITCH_BIN(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_SWITCH_BIN, GstSwitchBin))
#define GST_SWITCH_BIN_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_SWITCH_BIN, GstSwitchBinClass))
#define GST_SWITCH_BIN_CAST(obj)        ((GstSwitchBin *)(obj))
#define GST_IS_SWITCH_BIN(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_SWITCH_BIN))
#define GST_IS_SWITCH_BIN_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_SWITCH_BIN))


struct _GstSwitchBin
{
	GstBin parent;

	GMutex path_mutex;

	GstSwitchBinPath **paths;
	GstSwitchBinPath *current_path;
	gboolean path_changed;

	guint num_paths;

	GstElement *input_identity;
	GstPad *sinkpad, *srcpad;
	gulong blocking_probe_id;

	GstCaps *last_caps;
};


struct _GstSwitchBinClass
{
	GstBinClass parent_class;
};


struct _GstSwitchBinPath
{
	GstObject parent;

	GstElement *element;
	GstCaps *caps;
	GstSwitchBin *bin;
};


GType gst_switch_bin_get_type(void);
GType gst_switch_bin_path_get_type(void);
GST_ELEMENT_REGISTER_DECLARE (switchbin);

G_END_DECLS


#endif
