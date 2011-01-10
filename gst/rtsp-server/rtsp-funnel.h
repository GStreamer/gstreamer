/*
 * Farsight2 - Farsight Funnel element
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * rtsp-funnel.h: Simple Funnel element
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */


#ifndef __RTSP_FUNNEL_H__
#define __RTSP_FUNNEL_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define RTSP_TYPE_FUNNEL \
  (rtsp_funnel_get_type ())
#define RTSP_FUNNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),RTSP_TYPE_FUNNEL,RTSPFunnel))
#define RTSP_FUNNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),RTSP_TYPE_FUNNEL,RTSPFunnelClass))
#define RTSP_IS_FUNNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),RTSP_TYPE_FUNNEL))
#define RTSP_IS_FUNNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),RTSP_TYPE_FUNNEL))

typedef struct _RTSPFunnel          RTSPFunnel;
typedef struct _RTSPFunnelClass     RTSPFunnelClass;

/**
 * RTSPFunnel:
 *
 * Opaque #RTSPFunnel data structure.
 */
struct _RTSPFunnel {
  GstElement      element;

  /*< private >*/
  GstPad         *srcpad;

  gboolean has_segment;
};

struct _RTSPFunnelClass {
  GstElementClass parent_class;
};

GType   rtsp_funnel_get_type        (void);

G_END_DECLS

#endif /* __RTSP_FUNNEL_H__ */
