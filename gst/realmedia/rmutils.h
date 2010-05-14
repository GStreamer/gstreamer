/* GStreamer RealMedia utility functions
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
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

#ifndef __GST_RM_UTILS_H__
#define __GST_RM_UTILS_H__

#include <gst/gst.h>

G_BEGIN_DECLS

typedef gchar * (*GstRmUtilsStringReadFunc) (const guint8 * data, guint datalen, guint * p_strlen);

gchar         *gst_rm_utils_read_string8  (const guint8 * data,
                                           guint          datalen,
                                           guint        * p_totallen);

gchar         *gst_rm_utils_read_string16 (const guint8 * data,
                                           guint          datalen,
                                           guint        * p_totallen);

GstTagList    *gst_rm_utils_read_tags     (const guint8            * data,
                                           guint                     datalen,
                                           GstRmUtilsStringReadFunc  func);

GstBuffer     *gst_rm_utils_descramble_dnet_buffer (GstBuffer * buf);
GstBuffer     *gst_rm_utils_descramble_sipr_buffer (GstBuffer * buf);

void gst_rm_utils_run_tests (void);


G_END_DECLS

#endif /* __GST_RM_UTILS_H__ */

