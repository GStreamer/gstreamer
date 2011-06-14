/*
 *  gstvaapiutils.c - VA-API utilities
 *
 *  gstreamer-vaapi (C) 2010 Splitted-Desktop Systems
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

#include "config.h"
#include "gstvaapicompat.h"
#include "gstvaapiutils.h"
#include "gstvaapisurface.h"
#include <stdio.h>
#include <stdarg.h>

#define DEBUG 1
#include "gstvaapidebug.h"

/* Check VA status for success or print out an error */
gboolean
vaapi_check_status(VAStatus status, const char *msg)
{
    if (status != VA_STATUS_SUCCESS) {
        GST_DEBUG("%s: %s", msg, vaErrorStr(status));
        return FALSE;
    }
    return TRUE;
}

/* Return a string representation of a FOURCC */
const char *string_of_FOURCC(guint32 fourcc)
{
    static int buf;
    static char str[2][5]; // XXX: 2 buffers should be enough for most purposes

    buf ^= 1;
    str[buf][0] = fourcc;
    str[buf][1] = fourcc >> 8;
    str[buf][2] = fourcc >> 16;
    str[buf][3] = fourcc >> 24;
    str[buf][4] = '\0';
    return str[buf];
}

/* Return a string representation of a VAProfile */
const char *string_of_VAProfile(VAProfile profile)
{
    switch (profile) {
#define PROFILE(profile) \
        case VAProfile##profile: return "VAProfile" #profile
        PROFILE(MPEG2Simple);
        PROFILE(MPEG2Main);
        PROFILE(MPEG4Simple);
        PROFILE(MPEG4AdvancedSimple);
        PROFILE(MPEG4Main);
        PROFILE(H264Baseline);
        PROFILE(H264Main);
        PROFILE(H264High);
        PROFILE(VC1Simple);
        PROFILE(VC1Main);
        PROFILE(VC1Advanced);
#undef PROFILE
    default: break;
    }
    return "<unknown>";
}

/* Return a string representation of a VAEntrypoint */
const char *string_of_VAEntrypoint(VAEntrypoint entrypoint)
{
    switch (entrypoint) {
#define ENTRYPOINT(entrypoint) \
        case VAEntrypoint##entrypoint: return "VAEntrypoint" #entrypoint
        ENTRYPOINT(VLD);
        ENTRYPOINT(IZZ);
        ENTRYPOINT(IDCT);
        ENTRYPOINT(MoComp);
        ENTRYPOINT(Deblocking);
#undef ENTRYPOINT
    default: break;
    }
    return "<unknown>";
}

/**
 * from_GstVaapiSurfaceRenderFlags:
 * @flags: the #GstVaapiSurfaceRenderFlags
 *
 * Converts #GstVaapiSurfaceRenderFlags to flags suitable for
 * vaPutSurface().
 */
guint
from_GstVaapiSurfaceRenderFlags(guint flags)
{
    guint va_fields = 0, va_csc = 0;

    if (flags & GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD)
        va_fields |= VA_TOP_FIELD;
    if (flags & GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD)
        va_fields |= VA_BOTTOM_FIELD;
    if ((va_fields ^ (VA_TOP_FIELD|VA_BOTTOM_FIELD)) == 0)
        va_fields  = VA_FRAME_PICTURE;

#ifdef VA_SRC_BT601
    if (flags & GST_VAAPI_COLOR_STANDARD_ITUR_BT_601)
        va_csc = VA_SRC_BT601;
#endif
#ifdef VA_SRC_BT709
    if (flags & GST_VAAPI_COLOR_STANDARD_ITUR_BT_709)
        va_csc = VA_SRC_BT709;
#endif

    return va_fields|va_csc;
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
to_GstVaapiSurfaceStatus(guint va_flags)
{
    guint flags;
    const guint va_flags_mask = (VASurfaceReady|
                                 VASurfaceRendering|
                                 VASurfaceDisplaying);

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
