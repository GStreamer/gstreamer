/*
 * Copyright (C) 2010 Ole André Vadla Ravnås <oleavr@soundrop.com>
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

#include "mioapi.h"

#include "dynapi-internal.h"

#define MIO_FRAMEWORK_PATH "/System/Library/PrivateFrameworks/" \
    "CoreMediaIOServices.framework/CoreMediaIOServices"

GType gst_mio_api_get_type (void);

G_DEFINE_TYPE (GstMIOApi, gst_mio_api, GST_TYPE_DYN_API);

static void
gst_mio_api_init (GstMIOApi * self)
{
}

static void
gst_mio_api_class_init (GstMIOApiClass * klass)
{
}

#define SYM_SPEC(name) GST_DYN_SYM_SPEC (GstMIOApi, name)

GstMIOApi *
gst_mio_api_obtain (GError ** error)
{
  static const GstDynSymSpec symbols[] = {
    SYM_SPEC (TundraGraphCreate),
    SYM_SPEC (TundraGraphRelease),
    SYM_SPEC (TundraGraphCreateNode),
    SYM_SPEC (TundraGraphGetNodeInfo),
    SYM_SPEC (TundraGraphSetProperty),
    SYM_SPEC (TundraGraphConnectNodeInput),
    SYM_SPEC (TundraGraphInitialize),
    SYM_SPEC (TundraGraphUninitialize),
    SYM_SPEC (TundraGraphStart),
    SYM_SPEC (TundraGraphStop),

    SYM_SPEC (TundraObjectGetPropertyDataSize),
    SYM_SPEC (TundraObjectGetPropertyData),
    SYM_SPEC (TundraObjectIsPropertySettable),
    SYM_SPEC (TundraObjectSetPropertyData),

    SYM_SPEC (kTundraSampleBufferAttachmentKey_SequenceNumber),
    SYM_SPEC (kTundraSampleBufferAttachmentKey_HostTime),

    {NULL, 0},
  };

  return _gst_dyn_api_new (gst_mio_api_get_type (), MIO_FRAMEWORK_PATH, symbols,
      error);
}

gpointer
gst_mio_object_get_pointer (gint obj, TundraTargetSpec * pspec, GstMIOApi * mio)
{
  gpointer ptr;
  guint sz;
  TundraStatus status;

  sz = sizeof (ptr);
  status = mio->TundraObjectGetPropertyData (obj, pspec, 0, NULL, &sz, &ptr);
  if (status != kTundraSuccess)
    goto error;

  return ptr;

error:
  return NULL;
}

gchar *
gst_mio_object_get_string (gint obj, TundraTargetSpec * pspec, GstMIOApi * mio)
{
  gchar *result = NULL;
  CFStringRef str;
  guint size;
  TundraStatus status;
  CFRange range;

  size = sizeof (str);
  status = mio->TundraObjectGetPropertyData (obj, pspec, 0, NULL, &size, &str);
  if (status != kTundraSuccess)
    goto error;

  range.location = 0;
  range.length = CFStringGetLength (str);
  result = g_malloc0 (range.length + 1);
  CFStringGetBytes (str, range, kCFStringEncodingUTF8, 0, FALSE,
      (UInt8 *) result, range.length, NULL);
  CFRelease (str);

  return result;

error:
  return NULL;
}

guint32
gst_mio_object_get_uint32 (gint obj, TundraTargetSpec * pspec, GstMIOApi * mio)
{
  guint32 val;
  guint size;
  TundraStatus status;

  size = sizeof (val);
  status = mio->TundraObjectGetPropertyData (obj, pspec, 0, NULL, &size, &val);
  if (status != kTundraSuccess)
    goto error;

  return val;

error:
  return 0;
}

GArray *
gst_mio_object_get_array (gint obj, TundraTargetSpec * pspec,
    guint element_size, GstMIOApi * mio)
{
  return gst_mio_object_get_array_full (obj, pspec, 0, NULL, element_size, mio);
}

GArray *
gst_mio_object_get_array_full (gint obj, TundraTargetSpec * pspec,
    guint ctx_size, gpointer ctx, guint element_size, GstMIOApi * mio)
{
  GArray *arr = NULL;
  guint size, num_elements;
  TundraStatus status;

  status = mio->TundraObjectGetPropertyDataSize (obj, pspec, ctx_size, ctx,
      &size);
  if (status != kTundraSuccess)
    goto error;
  else if (size % element_size != 0)
    goto error;

  num_elements = size / element_size;
  arr = g_array_sized_new (FALSE, TRUE, element_size, num_elements);
  g_array_set_size (arr, num_elements);

  status = mio->TundraObjectGetPropertyData (obj, pspec, ctx_size, ctx,
      &size, arr->data);
  if (status != kTundraSuccess)
    goto error;

  return arr;

error:
  if (arr != NULL)
    g_array_free (arr, TRUE);
  return NULL;
}

gchar *
gst_mio_object_get_fourcc (gint obj, TundraTargetSpec * pspec, GstMIOApi * mio)
{
  guint32 fcc;
  guint size;
  TundraStatus status;

  size = sizeof (fcc);
  status = mio->TundraObjectGetPropertyData (obj, pspec, 0, NULL, &size, &fcc);
  if (status != kTundraSuccess)
    goto error;

  return gst_mio_fourcc_to_string (fcc);

error:
  return NULL;
}

gpointer
gst_mio_object_get_raw (gint obj, TundraTargetSpec * pspec, guint * size,
    GstMIOApi * mio)
{
  gpointer data = NULL;
  guint sz;
  TundraStatus status;

  status = mio->TundraObjectGetPropertyDataSize (obj, pspec, 0, NULL, &sz);
  if (status != kTundraSuccess)
    goto error;

  data = g_malloc0 (sz);

  status = mio->TundraObjectGetPropertyData (obj, pspec, 0, NULL, &sz, data);
  if (status != kTundraSuccess)
    goto error;

  if (size != NULL)
    *size = sz;
  return data;

error:
  g_free (data);
  if (size != NULL)
    *size = 0;
  return NULL;
}

gchar *
gst_mio_fourcc_to_string (guint32 fcc)
{
  gchar *result;

  result = g_malloc0 (5);
  result[0] = (fcc >> 24) & 0xff;
  result[1] = (fcc >> 16) & 0xff;
  result[2] = (fcc >> 8) & 0xff;
  result[3] = (fcc >> 0) & 0xff;

  return result;
}
