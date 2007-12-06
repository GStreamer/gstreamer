/* GStreamer
 * Copyright (C) <2007> Wim Taymans <wim.taymans@gmail.com>
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

#ifndef __GST_PLAY_ENUM_H__
#define __GST_PLAY_ENUM_H__

#include <gst/gst.h>

G_BEGIN_DECLS

/**
 * GstAutoplugSelectResult:
 * @GST_AUTOPLUG_SELECT_TRY: try to autoplug the current factory
 * @GST_AUTOPLUG_SELECT_EXPOSE: expose the pad as a raw stream
 * @GST_AUTOPLUG_SELECT_SKIP: skip the current factory
 *
 * return values for the autoplug-select signal.
 */
typedef enum {
  GST_AUTOPLUG_SELECT_TRY,
  GST_AUTOPLUG_SELECT_EXPOSE,
  GST_AUTOPLUG_SELECT_SKIP
} GstAutoplugSelectResult;

#define GST_TYPE_AUTOPLUG_SELECT_RESULT (gst_autoplug_select_result_get_type())
GType gst_autoplug_select_result_get_type (void);

G_END_DECLS

#endif /* __GST_PLAY_ENUM_H__ */
