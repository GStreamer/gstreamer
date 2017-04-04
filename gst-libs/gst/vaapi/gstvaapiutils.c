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
#include "gstvaapisurface.h"
#include "gstvaapisubpicture.h"
#include "gstvaapifilter.h"
#include <stdio.h>
#include <stdarg.h>

#if USE_VA_VPP
# include <va/va_vpp.h>
#endif

#define DEBUG 1
#include "gstvaapidebug.h"

#define CONCAT(a, b)    CONCAT_(a, b)
#define CONCAT_(a, b)   a##b
#define STRINGIFY(x)    STRINGIFY_(x)
#define STRINGIFY_(x)   #x
#define STRCASEP(p, x)  STRCASE(CONCAT(p, x))
#define STRCASE(x)      case x: return STRINGIFY(x)

#if VA_CHECK_VERSION (0,40,0)
static void
gst_vaapi_log (const char *message)
{
  gchar *msg;

  msg = g_strdup (message);
  if (!msg)
    return;
  g_strchomp (msg);
  GST_INFO ("%s", msg);
  g_free (msg);
}
#endif

gboolean
vaapi_initialize (VADisplay dpy)
{
  gint major_version, minor_version;
  VAStatus status;

#if VA_CHECK_VERSION (0,40,0)
  vaSetInfoCallback (gst_vaapi_log);
#endif

  status = vaInitialize (dpy, &major_version, &minor_version);
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
  VABufferID buf_id;
  VAStatus status;
  gpointer data = (gpointer) buf;

  status = vaCreateBuffer (dpy, ctx, type, size, 1, data, &buf_id);
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
#if VA_CHECK_VERSION(0,32,0)
      MAP (JPEGBaseline);
      MAP (H263Baseline);
      MAP (H264ConstrainedBaseline);
#endif
      MAP (H264Baseline);
      MAP (H264Main);
      MAP (H264High);
#if VA_CHECK_VERSION(0,35,2)
      MAP (H264MultiviewHigh);
      MAP (H264StereoHigh);
#endif
#if VA_CHECK_VERSION(0,37,1)
      MAP (HEVCMain);
      MAP (HEVCMain10);
#endif
      MAP (VC1Simple);
      MAP (VC1Main);
      MAP (VC1Advanced);
#if VA_CHECK_VERSION(0,35,0)
      MAP (VP8Version0_3);
#endif
#if VA_CHECK_VERSION(0,37,0)
      MAP (VP9Profile0);
#endif
#if VA_CHECK_VERSION(0,39,0)
      MAP (VP9Profile1);
      MAP (VP9Profile2);
      MAP (VP9Profile3);
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
#if !VA_CHECK_VERSION(0,34,0)
      MAP (DirectSurface);
#endif
      MAP (Rotation);
      MAP (OutofLoopDeblock);
#if VA_CHECK_VERSION(0,31,1) && !VA_CHECK_VERSION(0,34,0)
      MAP (BLEBlackMode);
      MAP (BLEWhiteMode);
      MAP (BlueStretch);
      MAP (SkinColorCorrection);
#endif
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
        STRCASEP(VA_RT_FORMAT_, value)
      MAP (YUV420);
      MAP (YUV422);
      MAP (YUV444);
#if VA_CHECK_VERSION(0,34,0)
      MAP (YUV400);
      MAP (RGB16);
      MAP (RGB32);
      MAP (RGBP);
#endif
#if VA_CHECK_VERSION(0,38,1)
      MAP (YUV420_10BPP);
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
#ifdef VA_RC_CQP
    case VA_RC_CQP:
      return "CQP";
#endif
    case VA_RC_CBR:
      return "CBR";
    case VA_RC_VCM:
      return "VCM";
    case VA_RC_VBR:
      return "VBR";
#ifdef VA_RC_VBR_CONSTRAINED
    case VA_RC_VBR_CONSTRAINED:
      return "VBR-Constrained";
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
  guint chroma_type;

  switch (va_rt_format) {
    case VA_RT_FORMAT_YUV420:
      chroma_type = GST_VAAPI_CHROMA_TYPE_YUV420;
      break;
    case VA_RT_FORMAT_YUV422:
      chroma_type = GST_VAAPI_CHROMA_TYPE_YUV422;
      break;
    case VA_RT_FORMAT_YUV444:
      chroma_type = GST_VAAPI_CHROMA_TYPE_YUV444;
      break;
#if VA_CHECK_VERSION(0,34,0)
    case VA_RT_FORMAT_YUV411:
      chroma_type = GST_VAAPI_CHROMA_TYPE_YUV411;
      break;
    case VA_RT_FORMAT_YUV400:
      chroma_type = GST_VAAPI_CHROMA_TYPE_YUV400;
      break;
    case VA_RT_FORMAT_RGB32:
      chroma_type = GST_VAAPI_CHROMA_TYPE_RGB32;
      break;
    case VA_RT_FORMAT_RGB16:
      chroma_type = GST_VAAPI_CHROMA_TYPE_RGB16;
      break;
#endif
#if VA_CHECK_VERSION(0,38,1)
    case VA_RT_FORMAT_YUV420_10BPP:
      chroma_type = GST_VAAPI_CHROMA_TYPE_YUV420_10BPP;
      break;
#endif
    default:
      chroma_type = 0;
      break;
  }
  return chroma_type;
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
#if VA_CHECK_VERSION(0,34,0)
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
#endif
#if VA_CHECK_VERSION(0,38,1)
    case GST_VAAPI_CHROMA_TYPE_YUV420_10BPP:
      format = VA_RT_FORMAT_YUV420_10BPP;
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
#if VA_CHECK_VERSION(0,30,0)
  if (va_flags & VASurfaceSkipped)
    flags |= GST_VAAPI_SURFACE_STATUS_SKIPPED;
#endif
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
#ifdef VA_RC_CQP
    case GST_VAAPI_RATECONTROL_CQP:
      return VA_RC_CQP;
#endif
    case GST_VAAPI_RATECONTROL_CBR:
      return VA_RC_CBR;
    case GST_VAAPI_RATECONTROL_VCM:
      return VA_RC_VCM;
    case GST_VAAPI_RATECONTROL_VBR:
      return VA_RC_VBR;
#ifdef VA_RC_VBR_CONSTRAINED
    case GST_VAAPI_RATECONTROL_VBR_CONSTRAINED:
      return VA_RC_VBR_CONSTRAINED;
#endif
#ifdef VA_RC_MB
    case GST_VAAPI_RATECONTROL_MB:
      return VA_RC_MB;
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
#ifdef VA_RC_CQP
    case VA_RC_CQP:
      return GST_VAAPI_RATECONTROL_CQP;
#endif
    case VA_RC_CBR:
      return GST_VAAPI_RATECONTROL_CBR;
    case VA_RC_VCM:
      return GST_VAAPI_RATECONTROL_VCM;
    case VA_RC_VBR:
      return GST_VAAPI_RATECONTROL_VBR;
#ifdef VA_RC_VBR_CONSTRAINED
    case VA_RC_VBR_CONSTRAINED:
      return GST_VAAPI_RATECONTROL_VBR_CONSTRAINED;
#endif
#ifdef VA_RC_MB
    case VA_RC_MB:
      return GST_VAAPI_RATECONTROL_MB;
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
#if USE_VA_VPP
    case GST_VAAPI_DEINTERLACE_METHOD_BOB:
      return VAProcDeinterlacingBob;
    case GST_VAAPI_DEINTERLACE_METHOD_WEAVE:
      return VAProcDeinterlacingWeave;
    case GST_VAAPI_DEINTERLACE_METHOD_MOTION_ADAPTIVE:
      return VAProcDeinterlacingMotionAdaptive;
    case GST_VAAPI_DEINTERLACE_METHOD_MOTION_COMPENSATED:
      return VAProcDeinterlacingMotionCompensated;
#endif
  }
  GST_ERROR ("unsupported GstVaapiDeinterlaceMethod value %d", value);
  return 0;
}

/* VPP: translate GstVaapiDeinterlaceFlags into VA deinterlacing flags */
guint
from_GstVaapiDeinterlaceFlags (guint flags)
{
  guint va_flags = 0;

#if USE_VA_VPP
  if (!(flags & GST_VAAPI_DEINTERLACE_FLAG_TFF))
    va_flags |= VA_DEINTERLACING_BOTTOM_FIELD_FIRST;

  if (flags & GST_VAAPI_DEINTERLACE_FLAG_ONEFIELD)
    va_flags |= VA_DEINTERLACING_ONE_FIELD;

  if (!(flags & GST_VAAPI_DEINTERLACE_FLAG_TOPFIELD))
    va_flags |= VA_DEINTERLACING_BOTTOM_FIELD;
#endif
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
