/* GStreamer H.263 Parser
 * Copyright (C) <2010> Arun Raghavan <arun.raghavan@collabora.co.uk>
 * Copyright (C) <2010> Edward Hervey <edward.hervey@collabora.co.uk>
 * Copyright (C) <2010> Collabora Multimedia
 * Copyright (C) <2010> Nokia Corporation
 *
 * Some bits C-c,C-v'ed and s/4/3 from h264parse:
 *           (C) 2005 Michal Benes <michal.benes@itonis.tv>
 *           (C) 2008 Wim Taymans <wim.taymans@gmail.com>
 *           (C) 2009 Mark Nauwelaerts <mnauw users sf net>
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

#ifndef __GST_H263_PARSE_H__
#define __GST_H263_PARSE_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include "gstbaseparse.h"

GST_DEBUG_CATEGORY_EXTERN (h263_parse_debug);
#define GST_CAT_DEFAULT h263_parse_debug

G_BEGIN_DECLS

typedef enum
{
  PARSING = 0,
  GOT_HEADER,
  PASSTHROUGH
} H263ParseState;

typedef struct _H263Params H263Params;

#define GST_TYPE_H263PARSE \
  (gst_h263_parse_get_type())
#define GST_H263PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_H263PARSE,GstH263Parse))
#define GST_H263PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_H263PARSE,GstH263ParseClass))
#define GST_IS_H263PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_H263PARSE))
#define GST_IS_H263PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_H263PARSE))
GType gst_h263_parse_get_type (void);

typedef struct _GstH263Parse GstH263Parse;
typedef struct _GstH263ParseClass GstH263ParseClass;

struct _GstH263Parse
{
  GstBaseParse baseparse;

  gint psc_pos;
  guint last_pos;

  gint profile, level;
  guint bitrate;

  H263ParseState state;
};

struct _GstH263ParseClass
{
  GstBaseParseClass parent_class;
};

gboolean gst_h263_parse_is_delta_unit (H263Params * params);
GstFlowReturn gst_h263_parse_get_params (GstH263Parse * h263parse,
    GstBuffer * buffer, H263Params ** params_p, gboolean fast);
void gst_h263_parse_get_framerate (GstCaps * caps, H263Params * params,
    gint * num, gint * denom);
void gst_h263_parse_set_src_caps (GstH263Parse * h263parse,
    H263Params * params);
gint gst_h263_parse_get_profile (H263Params * params);
gint gst_h263_parse_get_level (H263Params * params, gint profile,
    guint bitrate, gint fps_num, gint fps_denom);

G_END_DECLS
#endif
