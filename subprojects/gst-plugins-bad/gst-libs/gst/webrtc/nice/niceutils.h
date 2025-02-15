/* GStreamer
 * Copyright (C) 2022 Sherrill Lin <lshuying@amazon.com>
 * Copyright (C) 2022 Philippe Normand <philn@igalia.com>
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

#pragma once

#include <glib.h>
#include "nice.h"

gchar *gst_webrtc_nice_get_candidate_server_url(GstWebRTCNice * ice, NiceCandidate * cand);
const gchar *gst_webrtc_nice_get_candidate_relay_protocol(GstUri * turn_server);
void gst_webrtc_nice_fill_local_candidate_credentials(NiceAgent * agent, NiceCandidate * cand);
void gst_webrtc_nice_fill_remote_candidate_credentials(GstWebRTCNice  *nice, NiceCandidate * cand);
