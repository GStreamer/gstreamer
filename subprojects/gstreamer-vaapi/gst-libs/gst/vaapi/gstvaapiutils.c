/*
 *  gstvaapiutils.c - VA-API utilities
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

#include "sysdeps.h"
#include "gstvaapicompat.h"
#include "gstvaapiutils.h"
#include "gstvaapibufferproxy.h"
#include "gstvaapifilter.h"
#include "gstvaapisubpicture.h"
#include "gstvaapisurface.h"
#include <stdio.h>
#include <stdarg.h>

#define DEBUG 1
#include "gstvaapidebug.h"

/* string case an enum */
#define STRCASEP(p, x)  STRCASE(G_PASTE(p, x))
#define STRCASE(x)      case x: return G_STRINGIFY(x)

/* string case a macro */
#define STRCASEM(p, x)  case G_PASTE(p, x): return G_STRINGIFY(x)

#if VA_CHECK_VERSION (0,40,0)
static gchar *
strip_msg (const char *message)
{
  gchar *msg;

  msg = g_strdup (message);
  if (!msg)
    return NULL;
  return g_strstrip (msg);
}

#if VA_CHECK_VERSION (1,0,0)
static void
gst_vaapi_err (void *data, const char *message)
{
  gchar *msg;

  msg = strip_msg (message);
  if (!msg)
    return;
  GST_ERROR ("%s", msg);
  g_free (msg);
}

static void
gst_vaapi_warning (void *data, const char *message)
{
  gchar *msg;

  msg = strip_msg (message);
  if (!msg)
    return;
  GST_WARNING ("%s", msg);
  g_free (msg);
}
#endif

static void
gst_vaapi_log (
#if VA_CHECK_VERSION (1,0,0)
    void *data,
#endif
    const char *message)
{
  gchar *msg;

  msg = strip_msg (message);
  if (!msg)
    return;
  GST_INFO ("%s", msg);
  g_free (msg);
}
#endif

gboolean
vaapi_initialize (VADisplay dpy)
{
  gint major_version, minor_version;
  VAStatus status;

#if VA_CHECK_VERSION (1,0,0)
  vaSetErrorCallback (dpy, gst_vaapi_warning, NULL);
  vaSetInfoCallback (dpy, gst_vaapi_log, NULL);
#elif VA_CHECK_VERSION (0,40,0)
  vaSetInfoCallback (gst_vaapi_log);
#endif

  status = vaInitialize (dpy, &major_version, &minor_version);

#if VA_CHECK_VERSION (1,0,0)
  vaSetErrorCallback (dpy, gst_vaapi_err, NULL);
#endif

  if (!vaapi_check_status (status, "vaInitialize()"))
    return FALSE;

  GST_INFO ("VA-API version %d.%d", major_version, minor_version);
  return TRUE;
}

/* Check VA status for success or print out an error */
gboolean
vaapi_check_status (VAStatus status, const gchar * msg)
{
  if (status != VA_STATUS_SUCCESS) {
    GST_DEBUG ("%s: %s", msg, vaErrorStr (status));
    return FALSE;
  }
  return TRUE;
}

/* Maps VA buffer */
gpointer
vaapi_map_buffer (VADisplay dpy, VABufferID buf_id)
{
  VAStatus status;
  gpointer data = NULL;

  status = vaMapBuffer (dpy, buf_id, &data);
  if (!vaapi_check_status (status, "vaMapBuffer()"))
    return NULL;
  return data;
}

/* Unmaps VA buffer */
void
vaapi_unmap_buffer (VADisplay dpy, VABufferID buf_id, gpointer * pbuf)
{
  VAStatus status;

  if (pbuf)
    *pbuf = NULL;

  status = vaUnmapBuffer (dpy, buf_id);
  if (!vaapi_check_status (status, "vaUnmapBuffer()"))
    return;
}

/* Creates and maps VA buffer */
gboolean
vaapi_create_buffer (VADisplay dpy, VAContextID ctx, int type, guint size,
    gconstpointer buf, VABufferID * buf_id_ptr, gpointer * mapped_data)
{
  return vaapi_create_n_elements_buffer (dpy, ctx, type, size, buf, buf_id_ptr,
      mapped_data, 1);
}

gboolean
vaapi_create_n_elements_buffer (VADisplay dpy, VAContextID ctx, int type,
    guint size, gconstpointer buf, VABufferID * buf_id_ptr,
    gpointer * mapped_data, int num_elements)
{
  VABufferID buf_id;
  VAStatus status;
  gpointer data = (gpointer) buf;

  status = vaCreateBuffer (dpy, ctx, type, size, num_elements, data, &buf_id);
  if (!vaapi_check_status (status, "vaCreateBuffer()"))
    return FALSE;

  if (mapped_data) {
    data = vaapi_map_buffer (dpy, buf_id);
    if (!data)
      goto error;
    *mapped_data = data;
  }

  *buf_id_ptr = buf_id;
  return TRUE;

  /* ERRORS */
error:
  {
    vaapi_destroy_buffer (dpy, &buf_id);
    return FALSE;
  }
}

/* Destroy VA buffer */
void
vaapi_destroy_buffer (VADisplay dpy, VABufferID * buf_id_ptr)
{
  if (!buf_id_ptr || *buf_id_ptr == VA_INVALID_ID)
    return;

  vaDestroyBuffer (dpy, *buf_id_ptr);
  *buf_id_ptr = VA_INVALID_ID;
}

/* Return a string representation of a VAProfile */
const gchar *
string_of_VAProfile (VAProfile profile)
{
  switch (profile) {
#define MAP(profile) \
        STRCASEP(VAProfile, profile)
      MAP (MPEG2Simple);
      MAP (MPEG2Main);
      MAP (MPEG4Simple);
      MAP (MPEG4AdvancedSimple);
      MAP (MPEG4Main);
      MAP (JPEGBaseline);
      MAP (H263Baseline);
      MAP (H264ConstrainedBaseline);
#if !VA_CHECK_VERSION(1,0,0)
      MAP (H264Baseline);
#endif
      MAP (H264Main);
      MAP (H264High);
      MAP (H264MultiviewHigh);
      MAP (H264StereoHigh);
#if VA_CHECK_VERSION(1,2,0)
      MAP (HEVCMain422_10);
      MAP (HEVCMain444);
      MAP (HEVCMain444_10);
      MAP (HEVCSccMain);
      MAP (HEVCSccMain10);
      MAP (HEVCSccMain444);
#endif
#if VA_CHECK_VERSION(1,8,0)
      MAP (HEVCSccMain444_10);
#endif
      MAP (HEVCMain);
      MAP (HEVCMain10);
      MAP (VC1Simple);
      MAP (VC1Main);
      MAP (VC1Advanced);
      MAP (VP8Version0_3);
      MAP (VP9Profile0);
      MAP (VP9Profile1);
      MAP (VP9Profile2);
      MAP (VP9Profile3);
#if VA_CHECK_VERSION(1,8,0)
      MAP (AV1Profile0);
      MAP (AV1Profile1);
#endif
#undef MAP
    default:
      break;
  }
  return "<unknown>";
}

/* Return a string representation of a VAEntrypoint */
const gchar *
string_of_VAEntrypoint (VAEntrypoint entrypoint)
{
  switch (entrypoint) {
#define MAP(entrypoint) \
        STRCASEP(VAEntrypoint, entrypoint)
      MAP (VLD);
      MAP (IZZ);
      MAP (IDCT);
      MAP (MoComp);
      MAP (Deblocking);
      MAP (EncSlice);
      MAP (EncPicture);
#if VA_CHECK_VERSION(0,39,1)
      MAP (EncSliceLP);
#endif
      MAP (VideoProc);
#if VA_CHECK_VERSION(1,0,0)
      MAP (FEI);
#endif
#undef MAP
    default:
      break;
  }
  return "<unknown>";
}

/* Return a string representation of a VADisplayAttributeType */
const gchar *
string_of_VADisplayAttributeType (VADisplayAttribType attribute_type)
{
  switch (attribute_type) {
#define MAP(attribute_type) \
        STRCASEP(VADisplayAttrib, attribute_type)
      MAP (Brightness);
      MAP (Contrast);
      MAP (Hue);
      MAP (Saturation);
      MAP (BackgroundColor);
      MAP (Rotation);
      MAP (OutofLoopDeblock);
      MAP (CSCMatrix);
      MAP (BlendColor);
      MAP (OverlayAutoPaintColorKey);
      MAP (OverlayColorKey);
      MAP (RenderMode);
      MAP (RenderDevice);
      MAP (RenderRect);
#undef MAP
    default:
      break;
  }
  return "<unknown>";
}

/* Return a string representation of a VA chroma format */
const gchar *
string_of_va_chroma_format (guint chroma_format)
{
  switch (chroma_format) {
#define MAP(value) \
        STRCASEM(VA_RT_FORMAT_, value)
      MAP (YUV420);
      MAP (YUV422);
      MAP (YUV444);
      MAP (YUV400);
      MAP (RGB16);
      MAP (RGB32);
      MAP (RGBP);
      MAP (YUV420_10BPP);
#if VA_CHECK_VERSION(1,2,0)
      MAP (YUV422_10);
      MAP (YUV444_10);
      MAP (YUV420_12);
      MAP (YUV422_12);
      MAP (YUV444_12);
      MAP (RGB32_10);
#endif
#undef MAP
    default:
      break;
  }
  return "<unknown>";
}

const gchar *
string_of_VARateControl (guint rate_control)
{
  switch (rate_control) {
    case VA_RC_NONE:
      return "None";
    case VA_RC_CQP:
      return "CQP";
    case VA_RC_CBR:
      return "CBR";
    case VA_RC_VCM:
      return "VCM";
    case VA_RC_VBR:
      return "VBR";
    case VA_RC_VBR_CONSTRAINED:
      return "VBR-Constrained";
#if VA_CHECK_VERSION(0,39,1)
    case VA_RC_MB:
      return "MB";
#endif
#if VA_CHECK_VERSION(1,1,0)
    case VA_RC_ICQ:
      return "VA_RC_ICQ";
#endif
#if VA_CHECK_VERSION(1,3,0)
    case VA_RC_QVBR:
      return "VA_RC_QVBR";
#endif
    default:
      break;
  }
  return "<unknown>";
}

/**
 * to_GstVaapiChromaType:
 * @va_rt_format: the value of VAConfigAttribRTFormat
 *
 * Converts the VA_RT_FORMAT_* to #GstVaapiChromaType
 *
 * Returns: the #GstVaapiChromaType associated to @va_rt_format or
 * zero.
 **/
guint
to_GstVaapiChromaType (guint va_rt_format)
{
  if (va_rt_format & VA_RT_FORMAT_YUV420)
    return GST_VAAPI_CHROMA_TYPE_YUV420;
  if (va_rt_format & VA_RT_FORMAT_YUV422)
    return GST_VAAPI_CHROMA_TYPE_YUV422;
  if (va_rt_format & VA_RT_FORMAT_YUV444)
    return GST_VAAPI_CHROMA_TYPE_YUV444;
  if (va_rt_format & VA_RT_FORMAT_YUV411)
    return GST_VAAPI_CHROMA_TYPE_YUV411;
  if (va_rt_format & VA_RT_FORMAT_YUV400)
    return GST_VAAPI_CHROMA_TYPE_YUV400;
  if (va_rt_format & VA_RT_FORMAT_RGB32)
    return GST_VAAPI_CHROMA_TYPE_RGB32;
  if (va_rt_format & VA_RT_FORMAT_RGB16)
    return GST_VAAPI_CHROMA_TYPE_RGB16;
  if (va_rt_format & VA_RT_FORMAT_RGBP)
    return GST_VAAPI_CHROMA_TYPE_RGBP;
  if (va_rt_format & VA_RT_FORMAT_YUV420_10BPP)
    return GST_VAAPI_CHROMA_TYPE_YUV420_10BPP;
#if VA_CHECK_VERSION(1,2,0)
  if (va_rt_format & VA_RT_FORMAT_YUV422_10)
    return GST_VAAPI_CHROMA_TYPE_YUV422_10BPP;
  if (va_rt_format & VA_RT_FORMAT_YUV444_10)
    return GST_VAAPI_CHROMA_TYPE_YUV444_10BPP;
  if (va_rt_format & VA_RT_FORMAT_YUV420_12)
    return GST_VAAPI_CHROMA_TYPE_YUV420_12BPP;
  if (va_rt_format & VA_RT_FORMAT_YUV422_12)
    return GST_VAAPI_CHROMA_TYPE_YUV422_12BPP;
  if (va_rt_format & VA_RT_FORMAT_YUV444_12)
    return GST_VAAPI_CHROMA_TYPE_YUV444_12BPP;
  if (va_rt_format & VA_RT_FORMAT_RGB32_10)
    return GST_VAAPI_CHROMA_TYPE_RGB32_10BPP;
#endif
  return 0;
}

/**
 * from_GstVaapiChromaType:
 * @chroma_type: the #GstVaapiChromaType
 *
 * Converts #GstVaapiChromaType to a chroma format suitable for
 * vaCreateSurfaces().
 */
guint
from_GstVaapiChromaType (guint chroma_type)
{
  guint format;

  switch (chroma_type) {
    case GST_VAAPI_CHROMA_TYPE_YUV420:
      format = VA_RT_FORMAT_YUV420;
      break;
    case GST_VAAPI_CHROMA_TYPE_YUV422:
      format = VA_RT_FORMAT_YUV422;
      break;
    case GST_VAAPI_CHROMA_TYPE_YUV444:
      format = VA_RT_FORMAT_YUV444;
      break;
    case GST_VAAPI_CHROMA_TYPE_YUV411:
      format = VA_RT_FORMAT_YUV411;
      break;
    case GST_VAAPI_CHROMA_TYPE_YUV400:
      format = VA_RT_FORMAT_YUV400;
      break;
    case GST_VAAPI_CHROMA_TYPE_RGB32:
      format = VA_RT_FORMAT_RGB32;
      break;
    case GST_VAAPI_CHROMA_TYPE_RGB16:
      format = VA_RT_FORMAT_RGB16;
      break;
    case GST_VAAPI_CHROMA_TYPE_RGBP:
      format = VA_RT_FORMAT_RGBP;
      break;
    case GST_VAAPI_CHROMA_TYPE_YUV420_10BPP:
      format = VA_RT_FORMAT_YUV420_10BPP;
      break;
#if VA_CHECK_VERSION(1,2,0)
    case GST_VAAPI_CHROMA_TYPE_YUV422_10BPP:
      format = VA_RT_FORMAT_YUV422_10;
      break;
    case GST_VAAPI_CHROMA_TYPE_YUV444_10BPP:
      format = VA_RT_FORMAT_YUV444_10;
      break;
    case GST_VAAPI_CHROMA_TYPE_YUV420_12BPP:
      format = VA_RT_FORMAT_YUV420_12;
      break;
    case GST_VAAPI_CHROMA_TYPE_YUV422_12BPP:
      format = VA_RT_FORMAT_YUV422_12;
      break;
    case GST_VAAPI_CHROMA_TYPE_YUV444_12BPP:
      format = VA_RT_FORMAT_YUV444_12;
      break;
    case GST_VAAPI_CHROMA_TYPE_RGB32_10BPP:
      format = VA_RT_FORMAT_RGB32_10;
      break;
#endif
    default:
      format = 0;
      break;
  }
  return format;
}

/**
 * from_GstVaapiSubpictureFlags:
 * @flags: the #GstVaapiSubpictureFlags
 *
 * Converts #GstVaapiSubpictureFlags to flags suitable for
 * vaAssociateSubpicture().
 */
guint
from_GstVaapiSubpictureFlags (guint flags)
{
  guint va_flags = 0;

  if (flags & GST_VAAPI_SUBPICTURE_FLAG_GLOBAL_ALPHA)
    va_flags |= VA_SUBPICTURE_GLOBAL_ALPHA;
#ifdef VA_SUBPICTURE_PREMULTIPLIED_ALPHA
  if (flags & GST_VAAPI_SUBPICTURE_FLAG_PREMULTIPLIED_ALPHA)
    flags |= VA_SUBPICTURE_PREMULTIPLIED_ALPHA;
#endif
  return va_flags;
}

/**
 * to_GstVaapiSubpictureFlags:
 * @flags: the #GstVaapiSubpictureFlags flags to translate
 *
 * Converts vaQuerySubpictureFormats() @flags to #GstVaapiSubpictureFlags
 * flags.
 *
 * Return value: the #GstVaapiSubpictureFlags flags
 */
guint
to_GstVaapiSubpictureFlags (guint va_flags)
{
  guint flags = 0;

  if (va_flags & VA_SUBPICTURE_GLOBAL_ALPHA)
    flags |= GST_VAAPI_SUBPICTURE_FLAG_GLOBAL_ALPHA;
#ifdef VA_SUBPICTURE_PREMULTIPLIED_ALPHA
  if (va_flags & VA_SUBPICTURE_PREMULTIPLIED_ALPHA)
    flags |= GST_VAAPI_SUBPICTURE_FLAG_PREMULTIPLIED_ALPHA;
#endif
  return flags;
}

/**
 * from_GstVideoOverlayFormatFlags:
 * @flags: the #GstVideoOverlayFormatFlags flags to translate
 *
 * Converts #GstVaapiSubpictureFlags to #GstVaapiSubpictureFlags.
 *
 * Return value: the #GstVaapiSubpictureFlags flags
 */
guint
from_GstVideoOverlayFormatFlags (guint ovl_flags)
{
  guint flags = 0;

  if (ovl_flags & GST_VIDEO_OVERLAY_FORMAT_FLAG_PREMULTIPLIED_ALPHA)
    flags |= GST_VAAPI_SUBPICTURE_FLAG_PREMULTIPLIED_ALPHA;
  if (ovl_flags & GST_VIDEO_OVERLAY_FORMAT_FLAG_GLOBAL_ALPHA)
    flags |= GST_VAAPI_SUBPICTURE_FLAG_GLOBAL_ALPHA;
  return flags;
}

/**
 * to_GstVideoOverlayFormatFlags:
 * @flags: the #GstVaapiSubpictureFlags flags to translate
 *
 * Converts #GstVaapiSubpictureFlags to #GstVideoOverlayFormatFlags.
 *
 * Return value: the #GstVideoOverlayFormatFlags flags
 */
guint
to_GstVideoOverlayFormatFlags (guint flags)
{
  guint ovl_flags = 0;

  if (flags & GST_VAAPI_SUBPICTURE_FLAG_PREMULTIPLIED_ALPHA)
    ovl_flags |= GST_VIDEO_OVERLAY_FORMAT_FLAG_PREMULTIPLIED_ALPHA;
  if (flags & GST_VAAPI_SUBPICTURE_FLAG_GLOBAL_ALPHA)
    ovl_flags |= GST_VIDEO_OVERLAY_FORMAT_FLAG_GLOBAL_ALPHA;
  return ovl_flags;
}

/**
 * from_GstVaapiSurfaceRenderFlags:
 * @flags: the #GstVaapiSurfaceRenderFlags
 *
 * Converts #GstVaapiSurfaceRenderFlags to flags suitable for
 * vaPutSurface().
 */
guint
from_GstVaapiSurfaceRenderFlags (guint flags)
{
  guint va_fields, va_csc;

  /* Picture structure */
  switch (flags & GST_VAAPI_PICTURE_STRUCTURE_MASK) {
    case GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD:
      va_fields = VA_TOP_FIELD;
      break;
    case GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD:
      va_fields = VA_BOTTOM_FIELD;
      break;
    default:
      va_fields = VA_FRAME_PICTURE;
      break;
  }

  /* Color standard */
  switch (flags & GST_VAAPI_COLOR_STANDARD_MASK) {
#ifdef VA_SRC_BT601
    case GST_VAAPI_COLOR_STANDARD_ITUR_BT_601:
      va_csc = VA_SRC_BT601;
      break;
#endif
#ifdef VA_SRC_BT709
    case GST_VAAPI_COLOR_STANDARD_ITUR_BT_709:
      va_csc = VA_SRC_BT709;
      break;
#endif
#ifdef VA_SRC_SMPTE_240
    case GST_VAAPI_COLOR_STANDARD_SMPTE_240M:
      va_csc = VA_SRC_SMPTE_240;
      break;
#endif
    default:
      va_csc = 0;
      break;
  }
  return va_fields | va_csc;
}

/**
 * to_GstVaapiSurfaceStatus:
 * @flags: the #GstVaapiSurfaceStatus flags to translate
 *
 * Converts vaQuerySurfaceStatus() @flags to #GstVaapiSurfaceStatus
 * flags.
 *
 * Return value: the #GstVaapiSurfaceStatus flags
 */
guint
to_GstVaapiSurfaceStatus (guint va_flags)
{
  guint flags;
  const guint va_flags_mask = (VASurfaceReady |
      VASurfaceRendering | VASurfaceDisplaying);

  /* Check for core status */
  switch (va_flags & va_flags_mask) {
    case VASurfaceReady:
      flags = GST_VAAPI_SURFACE_STATUS_IDLE;
      break;
    case VASurfaceRendering:
      flags = GST_VAAPI_SURFACE_STATUS_RENDERING;
      break;
    case VASurfaceDisplaying:
      flags = GST_VAAPI_SURFACE_STATUS_DISPLAYING;
      break;
    default:
      flags = 0;
      break;
  }

  /* Check for encoder status */
  if (va_flags & VASurfaceSkipped)
    flags |= GST_VAAPI_SURFACE_STATUS_SKIPPED;
  return flags;
}

/* Translate GstVaapiRotation value to VA-API rotation value */
guint
from_GstVaapiRotation (guint value)
{
  switch (value) {
    case GST_VAAPI_ROTATION_0:
      return VA_ROTATION_NONE;
    case GST_VAAPI_ROTATION_90:
      return VA_ROTATION_90;
    case GST_VAAPI_ROTATION_180:
      return VA_ROTATION_180;
    case GST_VAAPI_ROTATION_270:
      return VA_ROTATION_270;
  }
  GST_ERROR ("unsupported GstVaapiRotation value %d", value);
  return VA_ROTATION_NONE;
}

/* Translate VA-API rotation value to GstVaapiRotation value */
guint
to_GstVaapiRotation (guint value)
{
  switch (value) {
    case VA_ROTATION_NONE:
      return GST_VAAPI_ROTATION_0;
    case VA_ROTATION_90:
      return GST_VAAPI_ROTATION_90;
    case VA_ROTATION_180:
      return GST_VAAPI_ROTATION_180;
    case VA_ROTATION_270:
      return GST_VAAPI_ROTATION_270;
  }
  GST_ERROR ("unsupported VA-API rotation value %d", value);
  return GST_VAAPI_ROTATION_0;
}

guint
from_GstVaapiRateControl (guint value)
{
  switch (value) {
    case GST_VAAPI_RATECONTROL_NONE:
      return VA_RC_NONE;
    case GST_VAAPI_RATECONTROL_CQP:
      return VA_RC_CQP;
    case GST_VAAPI_RATECONTROL_CBR:
      return VA_RC_CBR;
    case GST_VAAPI_RATECONTROL_VCM:
      return VA_RC_VCM;
    case GST_VAAPI_RATECONTROL_VBR:
      return VA_RC_VBR;
    case GST_VAAPI_RATECONTROL_VBR_CONSTRAINED:
      return VA_RC_VBR_CONSTRAINED;
#if VA_CHECK_VERSION(0,39,1)
    case GST_VAAPI_RATECONTROL_MB:
      return VA_RC_MB;
#endif
#if VA_CHECK_VERSION(1,1,0)
    case GST_VAAPI_RATECONTROL_ICQ:
      return VA_RC_ICQ;
#endif
#if VA_CHECK_VERSION(1,3,0)
    case GST_VAAPI_RATECONTROL_QVBR:
      return VA_RC_QVBR;
#endif
  }
  GST_ERROR ("unsupported GstVaapiRateControl value %u", value);
  return VA_RC_NONE;
}

guint
to_GstVaapiRateControl (guint value)
{
  switch (value) {
    case VA_RC_NONE:
      return GST_VAAPI_RATECONTROL_NONE;
    case VA_RC_CQP:
      return GST_VAAPI_RATECONTROL_CQP;
    case VA_RC_CBR:
      return GST_VAAPI_RATECONTROL_CBR;
    case VA_RC_VCM:
      return GST_VAAPI_RATECONTROL_VCM;
    case VA_RC_VBR:
      return GST_VAAPI_RATECONTROL_VBR;
    case VA_RC_VBR_CONSTRAINED:
      return GST_VAAPI_RATECONTROL_VBR_CONSTRAINED;
#if VA_CHECK_VERSION(0,39,1)
    case VA_RC_MB:
      return GST_VAAPI_RATECONTROL_MB;
#endif
#if VA_CHECK_VERSION(1,1,0)
    case VA_RC_ICQ:
      return GST_VAAPI_RATECONTROL_ICQ;
#endif
#if VA_CHECK_VERSION(1,3,0)
    case VA_RC_QVBR:
      return GST_VAAPI_RATECONTROL_QVBR;
#endif

  }
  GST_ERROR ("unsupported VA-API Rate Control value %u", value);
  return GST_VAAPI_RATECONTROL_NONE;
}

/* VPP: translate GstVaapiDeinterlaceMethod to VA deinterlacing algorithm */
guint
from_GstVaapiDeinterlaceMethod (guint value)
{
  switch (value) {
    case GST_VAAPI_DEINTERLACE_METHOD_NONE:
      return 0;
    case GST_VAAPI_DEINTERLACE_METHOD_BOB:
      return VAProcDeinterlacingBob;
    case GST_VAAPI_DEINTERLACE_METHOD_WEAVE:
      return VAProcDeinterlacingWeave;
    case GST_VAAPI_DEINTERLACE_METHOD_MOTION_ADAPTIVE:
      return VAProcDeinterlacingMotionAdaptive;
    case GST_VAAPI_DEINTERLACE_METHOD_MOTION_COMPENSATED:
      return VAProcDeinterlacingMotionCompensated;
  }
  GST_ERROR ("unsupported GstVaapiDeinterlaceMethod value %d", value);
  return 0;
}

/* VPP: translate GstVaapiDeinterlaceFlags into VA deinterlacing flags */
guint
from_GstVaapiDeinterlaceFlags (guint flags)
{
  guint va_flags = 0;

  if (!(flags & GST_VAAPI_DEINTERLACE_FLAG_TFF))
    va_flags |= VA_DEINTERLACING_BOTTOM_FIELD_FIRST;

  if (flags & GST_VAAPI_DEINTERLACE_FLAG_ONEFIELD)
    va_flags |= VA_DEINTERLACING_ONE_FIELD;

  if (!(flags & GST_VAAPI_DEINTERLACE_FLAG_TOPFIELD))
    va_flags |= VA_DEINTERLACING_BOTTOM_FIELD;
  return va_flags;
}

/* VPP: translate GstVaapiScaleMethod into VA scaling flags */
guint
from_GstVaapiScaleMethod (guint value)
{
  guint va_flags;

  switch (value) {
    case GST_VAAPI_SCALE_METHOD_DEFAULT:
      va_flags = VA_FILTER_SCALING_DEFAULT;
      break;
    case GST_VAAPI_SCALE_METHOD_FAST:
      va_flags = VA_FILTER_SCALING_FAST;
      break;
    case GST_VAAPI_SCALE_METHOD_HQ:
      va_flags = VA_FILTER_SCALING_HQ;
      break;
    default:
      va_flags = 0;
      break;
  }
  return va_flags;
}

/* VPP: translate VA scaling flags into GstVaapiScale Method */
guint
to_GstVaapiScaleMethod (guint flags)
{
  GstVaapiScaleMethod method;

  switch (flags) {
    case VA_FILTER_SCALING_FAST:
      method = GST_VAAPI_SCALE_METHOD_FAST;
      break;
    case VA_FILTER_SCALING_HQ:
      method = GST_VAAPI_SCALE_METHOD_HQ;
      break;
    default:
      method = GST_VAAPI_SCALE_METHOD_DEFAULT;
      break;
  }
  return method;
}

/* VPP: translate GstVideoOrientationMethod into VA mirror/rotation flags */
void
from_GstVideoOrientationMethod (guint value, guint * va_mirror,
    guint * va_rotation)
{
  *va_mirror = 0;
  *va_rotation = 0;

  switch (value) {
#if VA_CHECK_VERSION(1,1,0)
    case GST_VIDEO_ORIENTATION_IDENTITY:
      *va_mirror = VA_MIRROR_NONE;
      *va_rotation = VA_ROTATION_NONE;
      break;
    case GST_VIDEO_ORIENTATION_HORIZ:
      *va_mirror = VA_MIRROR_HORIZONTAL;
      *va_rotation = VA_ROTATION_NONE;
      break;
    case GST_VIDEO_ORIENTATION_VERT:
      *va_mirror = VA_MIRROR_VERTICAL;
      *va_rotation = VA_ROTATION_NONE;
      break;
    case GST_VIDEO_ORIENTATION_90R:
      *va_mirror = VA_MIRROR_NONE;
      *va_rotation = VA_ROTATION_90;
      break;
    case GST_VIDEO_ORIENTATION_180:
      *va_mirror = VA_MIRROR_NONE;
      *va_rotation = VA_ROTATION_180;
      break;
    case GST_VIDEO_ORIENTATION_90L:
      *va_mirror = VA_MIRROR_NONE;
      *va_rotation = VA_ROTATION_270;
      break;
    case GST_VIDEO_ORIENTATION_UL_LR:
      *va_mirror = VA_MIRROR_HORIZONTAL;
      *va_rotation = VA_ROTATION_90;
      break;
    case GST_VIDEO_ORIENTATION_UR_LL:
      *va_mirror = VA_MIRROR_VERTICAL;
      *va_rotation = VA_ROTATION_90;
      break;
#endif
    default:
      break;
  }
}

/**
 * from_GstVaapiBufferMemoryType:
 * @type: a #GstVaapiBufferMemoryType
 *
 * Returns: the VA's memory type symbol
 **/
guint
from_GstVaapiBufferMemoryType (guint type)
{
  guint va_type;

  switch (type) {
#if VA_CHECK_VERSION(1,1,0)
    case GST_VAAPI_BUFFER_MEMORY_TYPE_DMA_BUF2:
      va_type = VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2;
      break;
#endif
    case GST_VAAPI_BUFFER_MEMORY_TYPE_DMA_BUF:
      va_type = VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME;
      break;
    case GST_VAAPI_BUFFER_MEMORY_TYPE_GEM_BUF:
      va_type = VA_SURFACE_ATTRIB_MEM_TYPE_KERNEL_DRM;
      break;
    case GST_VAAPI_BUFFER_MEMORY_TYPE_V4L2:
      va_type = VA_SURFACE_ATTRIB_MEM_TYPE_V4L2;
      break;
    case GST_VAAPI_BUFFER_MEMORY_TYPE_USER_PTR:
      va_type = VA_SURFACE_ATTRIB_MEM_TYPE_USER_PTR;
      break;
    default:
      va_type = 0;
      break;
  }
  return va_type;
}

/**
 * to_GstVaapiBufferMemoryType:
 * @va_type: a VA's memory type symbol
 *
 * It will return the first "supported" memory type from @va_type bit
 * flag.
 *
 * Returns: a #GstVaapiBufferMemoryType or 0 if unknown.
 **/
guint
to_GstVaapiBufferMemoryType (guint va_type)
{
#if VA_CHECK_VERSION(1,1,0)
  if ((va_type & VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2))
    return GST_VAAPI_BUFFER_MEMORY_TYPE_DMA_BUF2;
#endif
  if ((va_type & VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME))
    return GST_VAAPI_BUFFER_MEMORY_TYPE_DMA_BUF;
  if ((va_type & VA_SURFACE_ATTRIB_MEM_TYPE_KERNEL_DRM))
    return GST_VAAPI_BUFFER_MEMORY_TYPE_GEM_BUF;
  if ((va_type & VA_SURFACE_ATTRIB_MEM_TYPE_V4L2))
    return GST_VAAPI_BUFFER_MEMORY_TYPE_V4L2;
  if ((va_type & VA_SURFACE_ATTRIB_MEM_TYPE_USER_PTR))
    return GST_VAAPI_BUFFER_MEMORY_TYPE_USER_PTR;
  return 0;
}

/**
 * from_GstVideoColorimetry:
 * @colorimetry: a #GstVideoColorimetry type
 *
 * VPP: maps the #GstVideoColorimetry type to the VAProcColorStandardType.  If
 * @colorimetry is NULL or colorimetry->primaries are unknown, then returns
 * VAProcColorStandardNone.  If there is no 1:1 correlation, then returns
 * VAProcColorStandardExplicit.  Otherwise, the correlating
 * VAProcColorStandardType is returned.
 *
 * Returns: a VAProcColorStandardType.
 **/
guint
from_GstVideoColorimetry (const GstVideoColorimetry * const colorimetry)
{
#if VA_CHECK_VERSION(1,2,0)
  if (!colorimetry
      || colorimetry->primaries == GST_VIDEO_COLOR_PRIMARIES_UNKNOWN)
    return VAProcColorStandardNone;
  if (gst_video_colorimetry_matches (colorimetry, GST_VIDEO_COLORIMETRY_BT709))
    return VAProcColorStandardBT709;
  /* NOTE: VAProcColorStandardBT2020 in VAAPI is the same as
   * GST_VIDEO_COLORIMETRY_BT2020_10 in gstreamer. */
  if (gst_video_colorimetry_matches (colorimetry,
          GST_VIDEO_COLORIMETRY_BT2020_10) ||
      gst_video_colorimetry_matches (colorimetry, GST_VIDEO_COLORIMETRY_BT2020))
    return VAProcColorStandardBT2020;
  if (gst_video_colorimetry_matches (colorimetry, GST_VIDEO_COLORIMETRY_BT601))
    return VAProcColorStandardBT601;
  if (gst_video_colorimetry_matches (colorimetry,
          GST_VIDEO_COLORIMETRY_SMPTE240M))
    return VAProcColorStandardSMPTE240M;

  return VAProcColorStandardExplicit;
#else
  return VAProcColorStandardNone;
#endif
}

/**
 * from_GstVideoColorRange:
 * @value: a #GstVideoColorRange
 *
 * VPP: maps the #GstVideoColorRange to the VA value.
 *
 * Returns: the VA color range.
 **/
guint
from_GstVideoColorRange (const GstVideoColorRange value)
{
#if VA_CHECK_VERSION(1,2,0)
  switch (value) {
    case GST_VIDEO_COLOR_RANGE_0_255:
      return VA_SOURCE_RANGE_FULL;
    case GST_VIDEO_COLOR_RANGE_16_235:
      return VA_SOURCE_RANGE_REDUCED;
    default:
      return VA_SOURCE_RANGE_UNKNOWN;
  }
#else
  return 0;
#endif
}
