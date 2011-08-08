/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *               2000,2004 Wim Taymans <wim@fluendo.com>
 *
 * gstelement.h: Header for GstElement
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

#ifndef __GST_ELEMENT_DETAILS_H__
#define __GST_ELEMENT_DETAILS_H__

G_BEGIN_DECLS

static inline void
__gst_element_details_clear (GstElementDetails * dp)
{
  g_free (dp->longname);
  g_free (dp->klass);
  g_free (dp->description);
  g_free (dp->author);
  memset (dp, 0, sizeof (GstElementDetails));
}

static inline void
__gst_element_details_copy (GstElementDetails * dest,
    const GstElementDetails * src)
{
  g_free (dest->longname);
  dest->longname = g_strdup (src->longname);

  g_free (dest->klass);
  dest->klass = g_strdup (src->klass);

  g_free (dest->description);
  dest->description = g_strdup (src->description);

  g_free (dest->author);
  dest->author = g_strdup (src->author);
}

G_END_DECLS

#endif /* __GST_ELEMENT_DETAILS_H__ */
