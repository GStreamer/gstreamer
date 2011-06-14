/*
 *  gstvapicompat.h - VA-API compatibility glue
 *
 *  gstreamer-vaapi (C) 2010-2011 Splitted-Desktop Systems
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

#ifndef GST_VAAPI_COMPAT_H
#define GST_VAAPI_COMPAT_H

#ifdef GST_VAAPI_USE_OLD_VAAPI_0_29
# include <va.h>
# include <va_x11.h>
#else
# include <va/va.h>
# if !VA_CHECK_VERSION(0,30,4)
#  include <va/va_x11.h>
# endif
#endif

#if USE_VAAPI_GLX
# include <va/va_glx.h>
#else
# define vaGetDisplayGLX(dpy) vaGetDisplay(dpy)
#endif

/* Check for VA version */
#ifndef VA_CHECK_VERSION
#define VA_MAJOR_VERSION 0
#define VA_MINOR_VERSION 29
#define VA_MICRO_VERSION 0
#define VA_SDS_VERSION   0
#define VA_CHECK_VERSION(major,minor,micro) \
        (VA_MAJOR_VERSION > (major) || \
         (VA_MAJOR_VERSION == (major) && VA_MINOR_VERSION > (minor)) || \
         (VA_MAJOR_VERSION == (major) && VA_MINOR_VERSION == (minor) && VA_MICRO_VERSION >= (micro)))
#endif

/* Check for VA/SDS version */
#ifndef VA_CHECK_VERSION_SDS
#define VA_CHECK_VERSION_SDS(major, minor, micro, sds)                  \
    (VA_CHECK_VERSION(major, minor, (micro)+1) ||                       \
     (VA_CHECK_VERSION(major, minor, micro) && VA_SDS_VERSION >= (sds)))
#endif

/* Compatibility glue with original VA-API 0.29 */
#ifdef GST_VAAPI_USE_OLD_VAAPI_0_29
typedef struct _VASliceParameterBufferBase {
    unsigned int slice_data_size;
    unsigned int slice_data_offset;
    unsigned int slice_data_flag;
} VASliceParameterBufferBase;
#endif

#ifndef VA_FOURCC
#define VA_FOURCC(ch0, ch1, ch2, ch3)           \
    ((guint32)(guint8)(ch0) |                   \
     ((guint32)(guint8)(ch1) << 8) |            \
     ((guint32)(guint8)(ch2) << 16) |           \
     ((guint32)(guint8)(ch3) << 24 ))
#endif

#ifndef VA_INVALID_ID
#define VA_INVALID_ID           0xffffffff
#endif
#ifndef VA_INVALID_SURFACE
#define VA_INVALID_SURFACE      VA_INVALID_ID
#endif

/* Compatibility glue with VA-API < 0.31 */
#if !VA_CHECK_VERSION(0,31,0)
#undef  vaSyncSurface
#define vaSyncSurface(dpy, s)   (vaSyncSurface)((dpy), VA_INVALID_ID, (s))
#undef  vaPutImage
#define vaPutImage              vaPutImage2
#undef  vaAssociateSubpicture
#define vaAssociateSubpicture   vaAssociateSubpicture2
#endif

#endif /* GST_VAAPI_COMPAT_H */
