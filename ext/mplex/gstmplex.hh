/* GStreamer mplex (mjpegtools) wrapper
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * gstmplex.hh: gstreamer mplex wrapper
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

#ifndef __GST_MPLEX_H__
#define __GST_MPLEX_H__

#include <gst/gst.h>
#include <multiplexor.hpp>
#include "gstmplexjob.hh"

G_BEGIN_DECLS

#define GST_TYPE_MPLEX \
  (gst_mplex_get_type ())
#define GST_MPLEX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MPLEX, GstMplex))
#define GST_MPLEX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MPLEX, GstMplex))
#define GST_IS_MPLEX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MPLEX))
#define GST_IS_MPLEX_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MPLEX))

typedef struct _GstMplex {
  GstElement parent;

  /* pads */
  GstPad *sinkpad, *srcpad;
  guint num_apads, num_vpads;

  /* options wrapper */
  GstMplexJob *job;

  /* general muxing object (contains rest) */
  Multiplexor *mux;
} GstMplex;

typedef struct _GstMplexClass {
  GstElementClass parent;
} GstMplexClass;

GType    gst_mplex_get_type    (void);

G_END_DECLS

#endif /* __GST_MPLEX_H__ */
