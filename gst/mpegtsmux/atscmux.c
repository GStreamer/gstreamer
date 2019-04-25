/* ATSC Transport Stream muxer
 * Copyright (C) 2019 Mathieu Duponchelle <mathieu@centricular.com>
 *
 * atscmux.c:
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

#include "atscmux.h"

GST_DEBUG_CATEGORY (atscmux_debug);
#define GST_CAT_DEFAULT atscmux_debug

G_DEFINE_TYPE (ATSCMux, atscmux, GST_TYPE_MPEG_TSMUX)
#define parent_class atscmux_parent_class
     static TsMuxStream *atscmux_create_new_stream (guint16 new_pid,
    TsMuxStreamType stream_type, MpegTsMux * mpegtsmux)
{
  return tsmux_stream_new (new_pid, stream_type);
}

static TsMux *
atscmux_create_ts_mux (MpegTsMux * mpegtsmux)
{
  TsMux *ret = ((MpegTsMuxClass *) parent_class)->create_ts_mux (mpegtsmux);

  tsmux_set_new_stream_func (ret,
      (TsMuxNewStreamFunc) atscmux_create_new_stream, mpegtsmux);

  return ret;
}

static void
atscmux_class_init (ATSCMuxClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  MpegTsMuxClass *mpegtsmux_class = (MpegTsMuxClass *) klass;

  GST_DEBUG_CATEGORY_INIT (atscmux_debug, "atscmux", 0, "ATSC muxer");

  gst_element_class_set_static_metadata (gstelement_class,
      "ATSC Transport Stream Muxer", "Codec/Muxer",
      "Multiplexes media streams into an ATSC-compliant Transport Stream",
      "Mathieu Duponchelle <mathieu@centricular.com>");

  mpegtsmux_class->create_ts_mux = atscmux_create_ts_mux;
}

static void
atscmux_init (ATSCMux * mux)
{
}
