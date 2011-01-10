/*
 * Farsight2 - Farsight Funnel element
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * fs-funnel.h: Simple Funnel element
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


#ifndef __FS_FUNNEL_H__
#define __FS_FUNNEL_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define FS_TYPE_FUNNEL \
  (fs_funnel_get_type ())
#define FS_FUNNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),FS_TYPE_FUNNEL,FsFunnel))
#define FS_FUNNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),FS_TYPE_FUNNEL,FsFunnelClass))
#define FS_IS_FUNNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),FS_TYPE_FUNNEL))
#define FS_IS_FUNNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),FS_TYPE_FUNNEL))

typedef struct _FsFunnel          FsFunnel;
typedef struct _FsFunnelClass     FsFunnelClass;

/**
 * FsFunnel:
 *
 * Opaque #FsFunnel data structure.
 */
struct _FsFunnel {
  GstElement      element;

  /*< private >*/
  GstPad         *srcpad;

  gboolean has_segment;
};

struct _FsFunnelClass {
  GstElementClass parent_class;
};

GType   fs_funnel_get_type        (void);

G_END_DECLS

#endif /* __FS_FUNNEL_H__ */
