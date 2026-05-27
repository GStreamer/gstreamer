/* GStreamer Android AAsset Source
 * Copyright (C) 2026 Dominique Leroux <dominique.p.leroux@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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

#ifndef _GST_AASSET_SRC_H__
#define _GST_AASSET_SRC_H__

#include <gst/base/gstbasesrc.h>

G_BEGIN_DECLS

typedef struct AAsset AAsset;
typedef struct AAssetManager AAssetManager;

#define GST_TYPE_AASSET_SRC (gst_aasset_src_get_type())
#define GST_AASSET_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_AASSET_SRC, GstAAssetSrc))
#define GST_AASSET_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_AASSET_SRC, GstAAssetSrcClass))
#define GST_IS_AASSET_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_AASSET_SRC))
#define GST_IS_AASSET_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_AASSET_SRC))

typedef struct _GstAAssetSrc GstAAssetSrc;
typedef struct _GstAAssetSrcClass GstAAssetSrcClass;

struct _GstAAssetSrc
{
  GstBaseSrc parent;

  gchar *location;

  gpointer asset_manager_ref;
  AAssetManager *asset_manager;
  AAsset *asset;
  guint64 size;
  gboolean flushing;
};

struct _GstAAssetSrcClass
{
  GstBaseSrcClass parent_class;
};

GType gst_aasset_src_get_type (void);

GST_ELEMENT_REGISTER_DECLARE (aassetsrc);

G_END_DECLS

#endif
