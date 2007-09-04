/* Copyright (C) 2003 Johan Dahlin <johan@gnome.org>
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


#ifndef __GST_NSFDEC_H__
#define __GST_NSFDEC_H__

#include <stdlib.h>

#include <gst/gst.h>

#include "types.h"
#include "nsf.h"

G_BEGIN_DECLS

#define GST_TYPE_NSFDEC \
  (gst_nsfdec_get_type())
#define GST_NSFDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_NSFDEC,GstNsfDec))
#define GST_NSFDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_NSFDEC,GstNsfDec))
#define GST_IS_NSFDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_NSFDEC))
#define GST_IS_NSFDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_NSFDEC))

typedef struct _GstNsfDec GstNsfDec;
typedef struct _GstNsfDecClass GstNsfDecClass;

enum
{
  NSF_STATE_NEED_TUNE = 1,
  NSF_STATE_LOAD_TUNE = 2,
  NSF_STATE_PLAY_TUNE = 3
};

struct _GstNsfDec {
  GstElement 	 element;

  /* pads */
  GstPad 	*sinkpad, 
  		*srcpad;

  gint 		 state;
  GstBuffer	*tune_buffer;
  guint64 	 total_bytes;

  /* properties */
  gint 		 tune_number;
  gint 		 filter;

  nsf_t         *nsf;
  gulong	 blocksize;

  int            frequency;
  int            bits;
  gboolean       stereo;
  int            channels;
  int            bps;

  GstTagList    *taglist;
};

struct _GstNsfDecClass {
  GstElementClass parent_class;
};

GType gst_nsfdec_get_type (void);
	
G_END_DECLS

#endif /* __GST_NSFDEC_H__ */
