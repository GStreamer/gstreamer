/* GStreamer
 * Copyright (C) <2002> David A. Schleef <ds@schleef.org>
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#ifndef __GST_SUBPARSE_H__
#define __GST_SUBPARSE_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_SUBPARSE \
  (gst_subparse_get_type ())
#define GST_SUBPARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_SUBPARSE, GstSubparse))
#define GST_SUBPARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_SUBPARSE, GstSubparse))
#define GST_IS_SUBPARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_SUBPARSE))
#define GST_IS_SUBPARSE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_SUBPARSE))

typedef struct _GstSubparse GstSubparse;
typedef struct _GstSubparseClass GstSubparseClass;

typedef void    (* GstSubparseInit)   (GstSubparse *self);
typedef gchar * (* GstSubparseParser) (GstSubparse *self,
				       guint64     *out_start_time,
				       guint64     *out_end_time,
				       gboolean     after_seek);

struct _GstSubparse {
  GstElement element;

  GstPad *sinkpad,*srcpad;

  GString *textbuf;
  struct {
    GstSubparseInit deinit;
    GstSubparseParser parse;
    gint type;
  } parser;
  gboolean parser_detected;

  union {
    struct {
      int      state;
      GString *buf;
      guint64  time1, time2;
    } subrip;
    struct {
      int state;
      GString *buf;
      guint64 time;
    } mpsub;
  } state;

  /* seek */
  guint64 seek_time;
  gboolean flush;
};

struct _GstSubparseClass {
  GstElementClass parent_class;
};

GType gst_subparse_get_type (void);

G_END_DECLS

#endif /* __GST_SUBPARSE_H__ */
