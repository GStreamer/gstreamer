/* GStreamer xvid decoder plugin
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_XVID_H__
#define __GST_XVID_H__

#include <gst/gst.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern gchar *	gst_xvid_error (int errorcode);
extern gboolean	gst_xvid_init  (void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GST_XVID_H__ */
