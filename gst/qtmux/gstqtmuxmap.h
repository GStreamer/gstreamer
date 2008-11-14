/* Quicktime muxer plugin for GStreamer
 * Copyright (C) 2008 Thiago Sousa Santos <thiagoss@embedded.ufcg.edu.br>
 * Copyright (C) 2008 Mark Nauwelaerts <mnauw@users.sf.net>
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

#ifndef __GST_QT_MUX_MAP_H__
#define __GST_QT_MUX_MAP_H__

#include "atoms.h"

#include <glib.h>
#include <gst/gst.h>

typedef enum _GstQTMuxFormat
{
  GST_QT_MUX_FORMAT_NONE = 0,
  GST_QT_MUX_FORMAT_QT,
  GST_QT_MUX_FORMAT_MP4,
  GST_QT_MUX_FORMAT_3GP,
  GST_QT_MUX_FORMAT_MJ2
} GstQTMuxFormat;

typedef struct _GstQTMuxFormatProp
{
  GstQTMuxFormat format;
  gchar *name;
  gchar *long_name;
  gchar *type_name;
  GstStaticCaps src_caps;
  GstStaticCaps video_sink_caps;
  GstStaticCaps audio_sink_caps;
} GstQTMuxFormatProp;

extern GstQTMuxFormatProp gst_qt_mux_format_list[];

void            gst_qt_mux_map_format_to_header      (GstQTMuxFormat format, GstBuffer ** _prefix,
                                                      guint32 * _major, guint32 * verson,
                                                      GList ** _compatible, AtomMOOV * moov);

AtomsTreeFlavor gst_qt_mux_map_format_to_flavor      (GstQTMuxFormat format);

#endif /* __GST_QT_MUX_MAP_H__ */
