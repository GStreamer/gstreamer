/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstosssrc.h: 
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


#ifndef __GST_OSSSRC_H__
#define __GST_OSSSRC_H__


#include <gst/gst.h>
#include "gstosselement.h"

G_BEGIN_DECLS
#define GST_TYPE_OSSSRC \
  (gst_osssrc_get_type())
#define GST_OSSSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OSSSRC,GstOssSrc))
#define GST_OSSSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OSSSRC,GstOssSrcClass))
#define GST_IS_OSSSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OSSSRC))
#define GST_IS_OSSSRC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OSSSRC))
    typedef enum
{
  GST_OSSSRC_OPEN = GST_ELEMENT_FLAG_LAST,

  GST_OSSSRC_FLAG_LAST = GST_ELEMENT_FLAG_LAST + 2,
} GstOssSrcFlags;

typedef struct _GstOssSrc GstOssSrc;
typedef struct _GstOssSrcClass GstOssSrcClass;

struct _GstOssSrc
{
  GstOssElement element;

  /* pads */
  GstPad *srcpad;

  gboolean need_eos;		/* Do we need to emit an EOS? */

  /* blocking */
  gulong curoffset;
  gulong buffersize;

  /* clocks */
  GstClock *provided_clock, *clock;
};

struct _GstOssSrcClass
{
  GstOssElementClass parent_class;
};

GType gst_osssrc_get_type (void);

G_END_DECLS
#endif /* __GST_OSSSRC_H__ */
