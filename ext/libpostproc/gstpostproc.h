/*
    Copyright (C) 2005 Edward Hervey (edward@fluendo.com)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef __GST_POSTPROC_H__
#define __GST_POSTPROC_H__

#ifdef HAVE_FFMPEG_UNINSTALLED
#include <avcodec.h>
#else
#include <ffmpeg/avcodec.h>
#endif

#include <gst/gst.h>

GST_DEBUG_CATEGORY_EXTERN (postproc_debug);
#define GST_CAT_DEFAULT postproc_debug

G_BEGIN_DECLS

extern gboolean gst_postproc_register (GstPlugin * plugin);

G_END_DECLS

#endif
