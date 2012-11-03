/* GStreamer Windows network source
 * Copyright (C) 2008 Ole André Vadla Ravnås <ole.andre.ravnas@tandberg.com>
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

#ifndef __GST_WIN_INET_SRC_H__
#define __GST_WIN_INET_SRC_H__

#include <windows.h>
#include <wininet.h>
#include <gst/base/gstpushsrc.h>

G_BEGIN_DECLS

#define GST_TYPE_WIN_INET_SRC \
  (gst_win_inet_src_get_type ())
#define GST_WIN_INET_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_WIN_INET_SRC, GstWinInetSrc))
#define GST_WIN_INET_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_WIN_INET_SRC, GstWinInetSrcClass))
#define GST_IS_WIN_INET_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_WIN_INET_SRC))
#define GST_IS_WIN_INET_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_WIN_INET_SRC))

typedef struct _GstWinInetSrc      GstWinInetSrc;
typedef struct _GstWinInetSrcClass GstWinInetSrcClass;

struct _GstWinInetSrc
{
  GstPushSrc push_src;

  /* property storage */
  gchar * location;
  gboolean poll_mode;
  gboolean iradio_mode;

  /* state */
  HINTERNET inet;
  HINTERNET url;
  guint64 cur_offset;
  GstCaps * icy_caps;
};

struct _GstWinInetSrcClass
{
  GstPushSrcClass parent_class;
};

GType gst_win_inet_src_get_type (void);

G_END_DECLS

#endif /* __GST_WIN_INET_SRC_H__ */

