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


#include <stdlib.h>
#include <string.h>

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstpushsrc.h"
#include "gsttypefindhelper.h"
#include <gst/gstmarshal.h>

GST_DEBUG_CATEGORY_STATIC (gst_pushsrc_debug);
#define GST_CAT_DEFAULT gst_pushsrc_debug

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

static void gst_pushsrc_base_init (gpointer g_class);
static void gst_pushsrc_class_init (GstPushSrcClass * klass);
static void gst_pushsrc_init (GstPushSrc * src, gpointer g_class);

GType
gst_pushsrc_get_type (void)
{
  static GType pushsrc_type = 0;

  if (!pushsrc_type) {
    static const GTypeInfo pushsrc_info = {
      sizeof (GstPushSrcClass),
      (GBaseInitFunc) gst_pushsrc_base_init,
      NULL,
      (GClassInitFunc) gst_pushsrc_class_init,
      NULL,
      NULL,
      sizeof (GstPushSrc),
      0,
      (GInstanceInitFunc) gst_pushsrc_init,
    };

    pushsrc_type = g_type_register_static (GST_TYPE_BASE_SRC,
        "GstPushSrc", &pushsrc_info, G_TYPE_FLAG_ABSTRACT);
  }
  return pushsrc_type;
}

#if 0
static const GstEventMask *gst_pushsrc_get_event_mask (GstPad * pad);
#endif

static GstFlowReturn gst_pushsrc_create (GstBaseSrc * bsrc, guint64 offset,
    guint length, GstBuffer ** ret);

static void
gst_pushsrc_base_init (gpointer g_class)
{
  GST_DEBUG_CATEGORY_INIT (gst_pushsrc_debug, "pushsrc", 0, "pushsrc element");
}

static void
gst_pushsrc_class_init (GstPushSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_BASE_SRC);

  gstbasesrc_class->create = GST_DEBUG_FUNCPTR (gst_pushsrc_create);
}

static void
gst_pushsrc_init (GstPushSrc * pushsrc, gpointer g_class)
{
}

static GstFlowReturn
gst_pushsrc_create (GstBaseSrc * bsrc, guint64 offset, guint length,
    GstBuffer ** ret)
{
  GstFlowReturn fret;
  GstPushSrc *src;
  GstPushSrcClass *pclass;

  src = GST_PUSHSRC (bsrc);
  pclass = GST_PUSHSRC_GET_CLASS (src);
  if (pclass->create)
    fret = pclass->create (src, ret);
  else
    fret = GST_FLOW_ERROR;

  return fret;
}
