/*
 *  codec.c - Codec utilities for the tests
 *
 *  Copyright (C) 2013 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#include "gst/vaapi/sysdeps.h"
#include <gst/vaapi/gstvaapidisplay.h>
#include "codec.h"

typedef struct
{
  const gchar *codec_str;
  GstVaapiCodec codec;
  const gchar *caps_str;
} CodecMap;

static const CodecMap g_codec_map[] = {
  {"h264", GST_VAAPI_CODEC_H264,
      "video/x-h264"},
  {"jpeg", GST_VAAPI_CODEC_JPEG,
      "image/jpeg"},
  {"mpeg2", GST_VAAPI_CODEC_MPEG2,
      "video/mpeg, mpegversion=2"},
  {"mpeg4", GST_VAAPI_CODEC_MPEG4,
      "video/mpeg, mpegversion=4"},
  {"wmv3", GST_VAAPI_CODEC_VC1,
      "video/x-wmv, wmvversion=3"},
  {"vc1", GST_VAAPI_CODEC_VC1,
      "video/x-wmv, wmvversion=3, format=(string)WVC1"},
  {NULL,}
};

static const CodecMap *
get_codec_map (GstVaapiCodec codec)
{
  const CodecMap *m;

  if (codec) {
    for (m = g_codec_map; m->codec_str != NULL; m++) {
      if (m->codec == codec)
        return m;
    }
  }
  return NULL;
}

const gchar *
string_from_codec (GstVaapiCodec codec)
{
  const CodecMap *const m = get_codec_map (codec);

  return m ? m->codec_str : NULL;
}

GstCaps *
caps_from_codec (GstVaapiCodec codec)
{
  const CodecMap *const m = get_codec_map (codec);

  return m ? gst_caps_from_string (m->caps_str) : NULL;
}

GstVaapiCodec
identify_codec_from_string (const gchar * codec_str)
{
  const CodecMap *m;

  if (codec_str) {
    for (m = g_codec_map; m->codec_str != NULL; m++) {
      if (g_ascii_strcasecmp (m->codec_str, codec_str) == 0)
        return m->codec;
    }
  }
  return 0;
}

typedef struct
{
  GMappedFile *file;
  guint8 *data;
  guint size;
  guint probability;
  GstCaps *caps;
  GstTypeFind type_find;
} CodecIdentifier;

static const guint8 *
codec_identifier_peek (gpointer data, gint64 offset, guint size)
{
  CodecIdentifier *const cip = data;

  if (offset >= 0 && offset + size <= cip->size)
    return cip->data + offset;
  if (offset < 0 && ((gint) cip->size + offset) >= 0)
    return &cip->data[cip->size + offset];
  return NULL;
}

static void
codec_identifier_suggest (gpointer data, guint probability, GstCaps * caps)
{
  CodecIdentifier *const cip = data;

  if (cip->probability < probability) {
    cip->probability = probability;
    gst_caps_replace (&cip->caps, caps);
  }
}

static void
codec_identifier_free (CodecIdentifier * cip)
{
  if (!cip)
    return;

  if (cip->file) {
    g_mapped_file_unref (cip->file);
    cip->file = NULL;
  }
  gst_caps_replace (&cip->caps, NULL);
  g_free (cip);
}

static CodecIdentifier *
codec_identifier_new (const gchar * filename)
{
  CodecIdentifier *cip;
  GstTypeFind *tfp;

  cip = g_new0 (CodecIdentifier, 1);
  if (!cip)
    return NULL;

  cip->file = g_mapped_file_new (filename, FALSE, NULL);
  if (!cip->file)
    goto error;

  cip->size = g_mapped_file_get_length (cip->file);
  cip->data = (guint8 *) g_mapped_file_get_contents (cip->file);
  if (!cip->data)
    goto error;

  tfp = &cip->type_find;
  tfp->peek = codec_identifier_peek;
  tfp->suggest = codec_identifier_suggest;
  tfp->data = cip;
  return cip;

error:
  codec_identifier_free (cip);
  return NULL;
}

GstVaapiCodec
identify_codec (const gchar * filename)
{
  CodecIdentifier *cip;
  GList *type_list, *l;
  guint32 codec = 0;

  cip = codec_identifier_new (filename);
  if (!cip)
    return 0;

  type_list = gst_type_find_factory_get_list ();
  for (l = type_list; l != NULL; l = l->next) {
    GstTypeFindFactory *const factory = GST_TYPE_FIND_FACTORY (l->data);
    gst_type_find_factory_call_function (factory, &cip->type_find);
  }
  g_list_free (type_list);

  if (cip->probability >= GST_TYPE_FIND_LIKELY)
    codec =
        gst_vaapi_profile_get_codec (gst_vaapi_profile_from_caps (cip->caps));

  codec_identifier_free (cip);
  return codec;
}
