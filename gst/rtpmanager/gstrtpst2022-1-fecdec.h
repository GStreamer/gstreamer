/* GStreamer
 * Copyright (C) <2020> Mathieu Duponchelle <mathieu@centricular.com>
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

#ifndef __GST_RTPST_2022_1_FECDEC_H__
#define __GST_RTPST_2022_1_FECDEC_H__

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstRTPST_2022_1_FecDecClass GstRTPST_2022_1_FecDecClass;
typedef struct _GstRTPST_2022_1_FecDec GstRTPST_2022_1_FecDec;

#define GST_TYPE_RTPST_2022_1_FECDEC (gst_rtpst_2022_1_fecdec_get_type())
#define GST_RTPST_2022_1_FECDEC_CAST(obj) ((GstRTPST_2022_1_FecDec *)(obj))

GType gst_rtpst_2022_1_fecdec_get_type (void);

GST_ELEMENT_REGISTER_DECLARE (rtpst2022_1_fecdec);

G_END_DECLS

#endif /* __GST_RTPST_2022_1_FECDEC_H__ */
