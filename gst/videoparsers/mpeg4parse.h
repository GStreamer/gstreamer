/* GStreamer MPEG4-2 video Parser
 * Copyright (C) <2008> Mindfruit B.V.
 *   @author Sjoerd Simons <sjoerd@luon.net>
 * Copyright (C) <2007> Julien Moutte <julien@fluendo.com>
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

#ifndef __GST_MPEG4_PARAMS_H__
#define __GST_MPEG4_PARAMS_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define MPEG4_VIDEO_OBJECT_STARTCODE_MIN      0x00
#define MPEG4_VIDEO_OBJECT_STARTCODE_MAX      0x1F
#define MPEG4_VOS_STARTCODE                   0xB0
#define MPEG4_VOS_ENDCODE                     0xB1
#define MPEG4_USER_DATA_STARTCODE             0xB2
#define MPEG4_GOP_STARTCODE                   0xB3
#define MPEG4_VISUAL_OBJECT_STARTCODE         0xB5
#define MPEG4_VOP_STARTCODE                   0xB6

#define MPEG4_START_MARKER                    0x000001
#define MPEG4_VISUAL_OBJECT_STARTCODE_MARKER  \
  ((MPEG4_START_MARKER << 8) + MPEG4_VISUAL_OBJECT_STARTCODE)
#define MPEG4_VOS_STARTCODE_MARKER            \
  ((MPEG4_START_MARKER << 8) + MPEG4_VOS_STARTCODE)
#define MPEG4_USER_DATA_STARTCODE_MARKER      \
  ((MPEG4_START_MARKER << 8) + MPEG4_USER_DATA_STARTCODE)


typedef struct _MPEG4Params MPEG4Params;

struct _MPEG4Params
{
  gint  profile;

  gint  width, height;
  gint  aspect_ratio_width, aspect_ratio_height;
  gint  time_increment_resolution;
  gint  fixed_time_increment;
};

GstFlowReturn gst_mpeg4_params_parse_config (MPEG4Params * params,
                                            const guint8 * data, guint size);

G_END_DECLS
#endif
