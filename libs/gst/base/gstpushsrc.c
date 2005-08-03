/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *               2000,2005 Wim Taymans <wim@fluendo.com>
 *
 * gstpushsrc.c:
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

/**
 * SECTION:gstpushsrc
 * @short_description: Base class for push based source elements
 * @see_also: #GstBaseTransformc, #GstBaseSink
 *
 * This class is mostly usefull for elements that cannot do
 * random access, or at least very slowly. The source usually
 * prefers to push out a fixed size buffer.
 *
 * Classes extending this base class will usually be scheduled
 * in a push based mode. It the peer accepts to operate without
 * offsets and withing the limits of the allowed block size, this
 * class can operate in getrange based mode automatically.
 *
 * The subclass should extend the methods from the baseclass in
 * addition to the create method.
 *
 * Seeking, flushing, scheduling and sync is all handled by this
 * base class.
 */

#include <stdlib.h>
#include <string.h>

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstpushsrc.h"
#include "gsttypefindhelper.h"
#include <gst/gstmarshal.h>

GST_DEBUG_CATEGORY_STATIC (gst_push_src_debug);
#define GST_CAT_DEFAULT gst_push_src_debug

/* PushSrc signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
};

static GstElementClass *parent_class = NULL;

static void gst_push_src_base_init (gpointer g_class);
static void gst_push_src_class_init (GstPushSrcClass * klass);
static void gst_push_src_init (GstPushSrc * src, gpointer g_class);

GType
gst_push_src_get_type (void)
{
  static GType push_src_type = 0;

  if (!push_src_type) {
    static const GTypeInfo push_src_info = {
      sizeof (GstPushSrcClass),
      (GBaseInitFunc) gst_push_src_base_init,
      NULL,
      (GClassInitFunc) gst_push_src_class_init,
      NULL,
      NULL,
      sizeof (GstPushSrc),
      0,
      (GInstanceInitFunc) gst_push_src_init,
    };

    push_src_type = g_type_register_static (GST_TYPE_BASE_SRC,
        "GstPushSrc", &push_src_info, G_TYPE_FLAG_ABSTRACT);
  }
  return push_src_type;
}

#if 0
static const GstEventMask *gst_push_src_get_event_mask (GstPad * pad);
#endif

static GstFlowReturn gst_push_src_create (GstBaseSrc * bsrc, guint64 offset,
    guint length, GstBuffer ** ret);

static void
gst_push_src_base_init (gpointer g_class)
{
  GST_DEBUG_CATEGORY_INIT (gst_push_src_debug, "pushsrc", 0, "pushsrc element");
}

static void
gst_push_src_class_init (GstPushSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_BASE_SRC);

  gstbasesrc_class->create = GST_DEBUG_FUNCPTR (gst_push_src_create);
}

static void
gst_push_src_init (GstPushSrc * pushsrc, gpointer g_class)
{
}

static GstFlowReturn
gst_push_src_create (GstBaseSrc * bsrc, guint64 offset, guint length,
    GstBuffer ** ret)
{
  GstFlowReturn fret;
  GstPushSrc *src;
  GstPushSrcClass *pclass;

  src = GST_PUSH_SRC (bsrc);
  pclass = GST_PUSH_SRC_GET_CLASS (src);
  if (pclass->create)
    fret = pclass->create (src, ret);
  else
    fret = GST_FLOW_ERROR;

  return fret;
}
