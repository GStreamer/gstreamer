/*
 *  gstvaapicontext.h - VA context abstraction (private)
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2011-2014 Intel Corporation
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

#ifndef GST_VAAPI_CONTEXT_H
#define GST_VAAPI_CONTEXT_H

#include "gstvaapiprofile.h"
#include "gstvaapidisplay.h"
#include "gstvaapisurface.h"
#include "gstvaapiutils_core.h"
#include "gstvaapivideopool.h"

G_BEGIN_DECLS

#define GST_VAAPI_CONTEXT(obj) \
  ((GstVaapiContext *) (obj))

typedef struct _GstVaapiConfigInfoEncoder GstVaapiConfigInfoEncoder;
typedef struct _GstVaapiContextInfo GstVaapiContextInfo;
typedef struct _GstVaapiContext GstVaapiContext;

/**
 * GstVaapiContextUsage:
 * @GST_VAAPI_CONTEXT_MODE_DECODE: context used for decoding.
 * @GST_VAAPI_CONTEXT_MODE_ENCODE: context used for encoding.
 * @GST_VAAPI_CONTEXT_MODE_VPP: context used for video processing.
 *
 * The set of supported VA context usages.
 */
typedef enum {
  GST_VAAPI_CONTEXT_USAGE_DECODE = 1,
  GST_VAAPI_CONTEXT_USAGE_ENCODE,
  GST_VAAPI_CONTEXT_USAGE_VPP,
} GstVaapiContextUsage;

/**
 * GstVaapiConfigInfoEncoder:
 * @rc_mode: rate-control mode (#GstVaapiRateControl).
 * @packed_headers: notify encoder that packed headers are submitted (mask).
 * @roi_capability: if encoder supports regions-of-interest.
 * @roi_num_supported: The number of regions-of-interest supported.
 *
 * Extra configuration for encoding.
 */
struct _GstVaapiConfigInfoEncoder
{
  GstVaapiRateControl rc_mode;
  guint packed_headers;
  gboolean roi_capability;
  guint roi_num_supported;
};

/**
 * GstVaapiContextInfo:
 *
 * Structure holding VA context info like encoded size, decoder
 * profile and entry-point to use, and maximum number of reference
 * frames reported by the bitstream.
 */
struct _GstVaapiContextInfo
{
  GstVaapiContextUsage usage;
  GstVaapiProfile profile;
  GstVaapiEntrypoint entrypoint;
  GstVaapiChromaType chroma_type;
  guint width;
  guint height;
  guint ref_frames;
  union _GstVaapiConfigInfo {
    GstVaapiConfigInfoEncoder encoder;
  } config;
};

/**
 * GstVaapiContext:
 *
 * A VA context wrapper.
 */
struct _GstVaapiContext
{
  /*< private >*/
  gint ref_count;
  GstVaapiDisplay *display;
  GstVaapiID object_id;

  /*< public >*/
  GstVaapiContextInfo info;
  VAProfile va_profile;
  VAEntrypoint va_entrypoint;
  VAConfigID va_config;
  GPtrArray *surfaces;
  GstVaapiVideoPool *surfaces_pool;
  gboolean reset_on_resize;
  GstVaapiConfigSurfaceAttributes *attribs;
  GstVideoFormat preferred_format;
};

#define GST_VAAPI_CONTEXT_ID(context)        (((GstVaapiContext *)(context))->object_id)
#define GST_VAAPI_CONTEXT_DISPLAY(context)   (((GstVaapiContext *)(context))->display)

G_GNUC_INTERNAL
GstVaapiContext *
gst_vaapi_context_new (GstVaapiDisplay * display,
    const GstVaapiContextInfo * cip);

G_GNUC_INTERNAL
gboolean
gst_vaapi_context_reset (GstVaapiContext * context,
    const GstVaapiContextInfo * new_cip);

G_GNUC_INTERNAL
GstVaapiID
gst_vaapi_context_get_id (GstVaapiContext * context);

G_GNUC_INTERNAL
GstVaapiSurfaceProxy *
gst_vaapi_context_get_surface_proxy (GstVaapiContext * context);

G_GNUC_INTERNAL
guint
gst_vaapi_context_get_surface_count (GstVaapiContext * context);

G_GNUC_INTERNAL
void
gst_vaapi_context_reset_on_resize (GstVaapiContext * context,
    gboolean reset_on_resize);

G_GNUC_INTERNAL
GArray *
gst_vaapi_context_get_surface_formats (GstVaapiContext * context);

G_GNUC_INTERNAL
gboolean
gst_vaapi_context_get_surface_attributes (GstVaapiContext * context,
    GstVaapiConfigSurfaceAttributes * out_attribs);

G_GNUC_INTERNAL
GstVaapiContext *
gst_vaapi_context_ref (GstVaapiContext * context);

G_GNUC_INTERNAL
void
gst_vaapi_context_unref (GstVaapiContext * context);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstVaapiContext, gst_vaapi_context_unref)

G_END_DECLS

#endif /* GST_VAAPI_CONTEXT_H */
