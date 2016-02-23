/* GStreamer
 * Copyright (C) 1999 Erik Walthinsen <omega@cse.ogi.edu>
 *               2001 Steve Baker <stevebaker_org@yahoo.co.uk>
 *               2003 Andy Wingo <wingo at pobox.com>
 *               2016 Thibault Saunier <thibault.saunier@collabora.com>
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

#ifndef __GST_LV2_H__
#define __GST_LV2_H__

#include <lilv/lilv.h>
#include <gst/gst.h>

LilvWorld *world;
LilvNode *audio_class;
LilvNode *control_class;
LilvNode *input_class;
LilvNode *output_class;
LilvNode *integer_prop;
LilvNode *toggled_prop;
LilvNode *in_place_broken_pred;
LilvNode *group_pred;
LilvNode *has_role_pred;
LilvNode *lv2_symbol_pred;

LilvNode *center_role;
LilvNode *left_role;
LilvNode *right_role;
LilvNode *rear_center_role;
LilvNode *rear_left_role;
LilvNode *rear_right_role;
LilvNode *lfe_role;
LilvNode *center_left_role;
LilvNode *center_right_role;
LilvNode *side_left_role;
LilvNode *side_right_role;

GQuark descriptor_quark;

gboolean gst_lv2_filter_register_element (GstPlugin *plugin,
                                          const gchar *type_name,
                                          gpointer *lv2plugin);
#endif /* __GST_LV2_H__ */
