/* GStreamer
 * Copyright (C) 2015 Sebastian Dröge <sebastian@centricular.com>
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

#include "gstrtputils.h"

typedef struct
{
  GstElement *element;
  GstBuffer *outbuf;
  GQuark copy_tag;
} CopyMetaData;

static gboolean
foreach_metadata_copy (GstBuffer * inbuf, GstMeta ** meta, gpointer user_data)
{
  CopyMetaData *data = user_data;
  GstElement *element = data->element;
  GstBuffer *outbuf = data->outbuf;
  GQuark copy_tag = data->copy_tag;
  const GstMetaInfo *info = (*meta)->info;
  const gchar *const *tags = gst_meta_api_type_get_tags (info->api);

  if (!tags || (copy_tag != 0 && g_strv_length ((gchar **) tags) == 1
          && gst_meta_api_type_has_tag (info->api, copy_tag))) {
    GstMetaTransformCopy copy_data = { FALSE, 0, -1 };
    GST_DEBUG_OBJECT (element, "copy metadata %s", g_type_name (info->api));
    /* simply copy then */
    info->transform_func (outbuf, *meta, inbuf,
        _gst_meta_transform_copy, &copy_data);
  } else {
    GST_DEBUG_OBJECT (element, "not copying metadata %s",
        g_type_name (info->api));
  }

  return TRUE;
}

/* TODO: Should probably make copy_tag an array at some point */
void
gst_rtp_copy_meta (GstElement * element, GstBuffer * outbuf, GstBuffer * inbuf,
    GQuark copy_tag)
{
  CopyMetaData data = { element, outbuf, copy_tag };

  gst_buffer_foreach_meta (inbuf, foreach_metadata_copy, &data);
}

typedef struct
{
  GstElement *element;
  GQuark keep_tag;
} DropMetaData;

static gboolean
foreach_metadata_drop (GstBuffer * inbuf, GstMeta ** meta, gpointer user_data)
{
  DropMetaData *data = user_data;
  GstElement *element = data->element;
  GQuark keep_tag = data->keep_tag;
  const GstMetaInfo *info = (*meta)->info;
  const gchar *const *tags = gst_meta_api_type_get_tags (info->api);

  if (!tags || (keep_tag != 0 && g_strv_length ((gchar **) tags) == 1
          && gst_meta_api_type_has_tag (info->api, keep_tag))) {
    GST_DEBUG_OBJECT (element, "keeping metadata %s", g_type_name (info->api));
  } else {
    GST_DEBUG_OBJECT (element, "dropping metadata %s", g_type_name (info->api));
    *meta = NULL;
  }

  return TRUE;
}

/* TODO: Should probably make keep_tag an array at some point */
void
gst_rtp_drop_meta (GstElement * element, GstBuffer * buf, GQuark keep_tag)
{
  DropMetaData data = { element, keep_tag };

  gst_buffer_foreach_meta (buf, foreach_metadata_drop, &data);
}
