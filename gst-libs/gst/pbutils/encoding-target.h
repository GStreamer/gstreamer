/* GStreamer encoding profile registry
 * Copyright (C) 2010 Edward Hervey <edward.hervey@collabora.co.uk>
 *           (C) 2010 Nokia Corporation
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

#ifndef __GST_PROFILE_REGISTRY_H__
#define __GST_PROFILE_REGISTRY_H__

#include <gst/pbutils/encoding-profile.h>

G_BEGIN_DECLS


/* FIXME/UNKNOWNS
 *
 * Should encoding categories be well-known strings/quarks ?
 *
 */

#define GST_ENCODING_CATEGORY_DEVICE		"device"
#define GST_ENCODING_CATEGORY_ONLINE_SERVICE	"online-service"
#define GST_ENCODING_CATEGORY_STORAGE_EDITING   "storage-editing"
#define GST_ENCODING_CATEGORY_CAPTURE		"capture"

/**
 * GstEncodingTarget:
 *
 * Collection of #GstEncodingProfile for a specific target or use-case.
 *
 * Since: 0.10.32
 */
#define GST_TYPE_ENCODING_TARGET			\
  (gst_encoding_target_get_type ())
#define GST_ENCODING_TARGET(obj)			\
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ENCODING_TARGET, GstEncodingTarget))
#define GST_IS_ENCODING_TARGET(obj)			\
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ENCODING_TARGET))

typedef struct _GstEncodingTarget GstEncodingTarget;
typedef GstMiniObjectClass GstEncodingTargetClass;

GType gst_encoding_target_get_type (void);

/**
 * gst_encoding_target_unref:
 * @target: a #GstEncodingTarget
 *
 * Decreases the reference count of the @target, possibly freeing it.
 *
 * Since: 0.10.32
 */
#define gst_encoding_target_unref(target) \
  (gst_mini_object_unref ((GstMiniObject*) target))

/**
 * gst_encoding_target_ref:
 * @target: a #GstEncodingTarget
 *
 * Increases the reference count of the @target.
 *
 * Since: 0.10.32
 */
#define gst_encoding_target_ref(target) \
  (gst_mini_object_ref ((GstMiniObject*) target))

GstEncodingTarget *
gst_encoding_target_new (const gchar *name, const gchar *category,
			 const gchar *description, const GList *profiles);
const gchar *gst_encoding_target_get_name (GstEncodingTarget *target);
const gchar *gst_encoding_target_get_category (GstEncodingTarget *target);
const gchar *gst_encoding_target_get_description (GstEncodingTarget *target);
const GList *gst_encoding_target_get_profiles (GstEncodingTarget *target);

gboolean
gst_encoding_target_add_profile (GstEncodingTarget *target, GstEncodingProfile *profile);

gboolean gst_encoding_target_save (GstEncodingTarget *target,
				   GError **error);
gboolean gst_encoding_target_save_to (GstEncodingTarget *target,
				      const gchar *path,
				      GError **error);
GstEncodingTarget *gst_encoding_target_load (const gchar *name,
					     GError **error);
GstEncodingTarget *gst_encoding_target_load_from (const gchar *path,
						  GError **error);

G_END_DECLS

#endif	/* __GST_PROFILE_REGISTRY_H__ */
