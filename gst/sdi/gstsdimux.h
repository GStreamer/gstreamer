/* GStreamer
 * Copyright (C) 2010 REAL_NAME <EMAIL_ADDRESS>
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

#ifndef _GST_SDI_MUX_H_
#define _GST_SDI_MUX_H_

#include <gst/gst.h>
#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_SDI_MUX   (gst_sdi_mux_get_type())
#define GST_SDI_MUX(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SDI_MUX,GstSdiMux))
#define GST_SDI_MUX_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SDI_MUX,GstSdiMuxClass))
#define GST_IS_SDI_MUX(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SDI_MUX))
#define GST_IS_SDI_MUX_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SDI_MUX))

typedef struct _GstSdiMux GstSdiMux;
typedef struct _GstSdiMuxClass GstSdiMuxClass;

struct _GstSdiMux
{
  GstElement base_sdimux;

  GstPad *srcpad;
  GstPad *sinkpad;
};

struct _GstSdiMuxClass
{
  GstElementClass base_sdimux_class;
};

GType gst_sdi_mux_get_type (void);

G_END_DECLS

#endif
