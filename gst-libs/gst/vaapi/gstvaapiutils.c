/*
 *  gstvaapiutils.c - VA-API utilities
 *
 *  gstreamer-vaapi (C) 2010 Splitted-Desktop Systems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include "gstvaapiutils.h"
#include <stdio.h>
#include <stdarg.h>

/* Debug output */
void vaapi_dprintf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    fprintf(stdout, "[GstVaapi] ");
    vfprintf(stdout, format, args);
    va_end(args);
}

/* Check VA status for success or print out an error */
int vaapi_check_status(VAStatus status, const char *msg)
{
    if (status != VA_STATUS_SUCCESS) {
        vaapi_dprintf("%s: %s\n", msg, vaErrorStr(status));
        return 0;
    }
    return 1;
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
