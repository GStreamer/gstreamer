/* GStreamer taglib-based APEv2 muxer
 * Copyright (C) 2006 Christophe Fergeau <teuf@gnome.org>
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
 * Copyright (C) 2006 Sebastian Dröge <slomo@circular-chaos.org>
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

#ifndef GST_APEV2_MUX_H
#define GST_APEV2_MUX_H

#include <gst/tag/gsttagmux.h>

G_BEGIN_DECLS

#define GST_TYPE_APEV2_MUX (gst_apev2_mux_get_type())
G_DECLARE_FINAL_TYPE (GstApev2Mux, gst_apev2_mux, GST, APEV2_MUX, GstTagMux)

struct _GstApev2Mux {
  GstTagMux  tagmux;
};

G_END_DECLS

#endif /* GST_APEV2_MUX_H */
