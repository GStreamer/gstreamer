/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * filename:
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

#ifndef __GST_PARSE_H__
#define __GST_PARSE_H__

#include <gst/gstbin.h>

G_BEGIN_DECLS

#ifndef GST_DISABLE_PARSE

GQuark gst_parse_error_quark (void);
#define GST_PARSE_ERROR gst_parse_error_quark ()

typedef enum
{
  GST_PARSE_ERROR_SYNTAX,
  GST_PARSE_ERROR_NO_SUCH_ELEMENT,
  GST_PARSE_ERROR_NO_SUCH_PROPERTY,
  GST_PARSE_ERROR_LINK,
  GST_PARSE_ERROR_COULD_NOT_SET_PROPERTY,
  GST_PARSE_ERROR_EMPTY_BIN,
  GST_PARSE_ERROR_EMPTY
} GstParseError;


GstElement*	gst_parse_launch	(const gchar *pipeline_description, GError **error);
GstElement*	gst_parse_launchv	(const gchar **argv, GError **error);

#else /* GST_DISABLE_PARSE */

#if defined _GNUC_ && _GNUC_ >= 3
#pragma GCC poison gst_parse_launch
#pragma GCC poison gst_parse_launchv
#endif

#endif /* GST_DISABLE_PARSE */

G_END_DECLS

#endif /* __GST_PARSE_H__ */
