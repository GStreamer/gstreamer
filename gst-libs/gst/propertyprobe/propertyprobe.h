/* GStreamer PropertyProbe
 * Copyright (C) 2003 David A. Schleef <ds@schleef.org>
 *
 * property_probe.h: property_probe interface design
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

#ifndef __GST_PROPERTY_PROBE_H__
#define __GST_PROPERTY_PROBE_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_PROPERTY_PROBE \
  (gst_property_probe_get_type ())
#define GST_PROPERTY_PROBE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_PROPERTY_PROBE, GstPropertyProbe))
#define GST_IS_PROPERTY_PROBE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_PROPERTY_PROBE))
#define GST_PROPERTY_PROBE_GET_IFACE(obj) \
  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), GST_TYPE_PROPERTY_PROBE, GstPropertyProbeInterface))

typedef struct _GstPropertyProbe GstPropertyProbe; /* dummy typedef */

typedef struct _GstPropertyProbeInterface {
  GTypeInterface klass;

  /* signals */
  void (*need_probe) (GstPropertyProbe *property_probe, const gchar *property);

  /* virtual functions */
  gchar ** (*get_list) (GstElement *property_probe);
  void (*probe_property) (GstElement *property_probe, const GParamSpec *property);
  gchar ** (*get_property_info) (GstElement *property_probe,
      const GParamSpec *property);
  gboolean (*is_probed) (GstElement *element, const GParamSpec *property);
  
  GST_CLASS_PADDING
} GstPropertyProbeInterface;

GType		gst_property_probe_get_type	(void);

/* virtual class function wrappers */
gchar ** gst_property_probe_get_list (GstElement *element);
void gst_property_probe_probe_property (GstElement *element,
    const gchar *property);
gchar ** gst_property_probe_get_property_info (GstElement *element,
    const gchar *property);
gboolean gst_property_probe_is_probed (GstElement *element,
    const gchar *property);

/* utility functions */
gchar ** gst_property_probe_get_possibilities (GstElement *element,
    const gchar *property);

G_END_DECLS

#endif /* __GST_PROPERTY_PROBE_H__ */
