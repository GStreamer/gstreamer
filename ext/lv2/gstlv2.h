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

#include "gstlv2utils.h"

extern LilvWorld *world;
extern LilvNode *atom_class;
extern LilvNode *audio_class;
extern LilvNode *control_class;
extern LilvNode *cv_class;
extern LilvNode *event_class;
extern LilvNode *input_class;
extern LilvNode *output_class;
extern LilvNode *preset_class;
extern LilvNode *state_iface;
extern LilvNode *state_uri;

extern LilvNode *integer_prop;
extern LilvNode *toggled_prop;
extern LilvNode *designation_pred;
extern LilvNode *in_place_broken_pred;
extern LilvNode *optional_pred;
extern LilvNode *group_pred;
extern LilvNode *supports_event_pred;
extern LilvNode *label_pred;

extern LilvNode *center_role;
extern LilvNode *left_role;
extern LilvNode *right_role;
extern LilvNode *rear_center_role;
extern LilvNode *rear_left_role;
extern LilvNode *rear_right_role;
extern LilvNode *lfe_role;
extern LilvNode *center_left_role;
extern LilvNode *center_right_role;
extern LilvNode *side_left_role;
extern LilvNode *side_right_role;

extern GstStructure *lv2_meta_all;

void gst_lv2_filter_register_element (GstPlugin *plugin,
                                      GstStructure * lv2_meta);
void gst_lv2_source_register_element (GstPlugin *plugin,
                                      GstStructure * lv2_meta);
#endif /* __GST_LV2_H__ */
