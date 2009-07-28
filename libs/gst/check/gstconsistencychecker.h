/* GStreamer
 *
 * unit testing helper lib
 *
 * Copyright (C) 2009 Edward Hervey <bilboed@bilboed.com>
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

#ifndef __GST_CONSISTENCY_CHECKER_H__
#define __GST_CONSISTENCY_CHECKER_H__

#include <gst/check/gstcheck.h>

G_BEGIN_DECLS

/**
 * GstStreamConsistency:
 *
 * Opaque consistency checker handle.
 *
 * Since: 0.10.24
 */
typedef struct _GstStreamConsistency GstStreamConsistency;


GstStreamConsistency * gst_consistency_checker_new   (GstPad * pad);

void                   gst_consistency_checker_reset (GstStreamConsistency * consist);

void                   gst_consistency_checker_free  (GstStreamConsistency * consist);

G_END_DECLS

#endif /* __GST_CONSISTENCY_CHECKER_H__ */
