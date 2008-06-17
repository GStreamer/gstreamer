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

/*
 *
 *   This is a temporary copy of GstBaseSrc/GstPushSrc for the resin
 *   DVD components, to work around a deadlock with source elements that
 *   send seeks to themselves. 
 *
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include "rsnpushsrc.h"
#include <gst/gstmarshal.h>

GST_DEBUG_CATEGORY_STATIC (rsn_push_src_debug);
#define GST_CAT_DEFAULT rsn_push_src_debug

#define _do_init(type) \
    GST_DEBUG_CATEGORY_INIT (rsn_push_src_debug, "pushsrc", 0, \
        "pushsrc element");

GST_BOILERPLATE_FULL (RsnPushSrc, rsn_push_src, RsnBaseSrc, RSN_TYPE_BASE_SRC,
    _do_init);

static gboolean rsn_push_src_check_get_range (RsnBaseSrc * src);
static GstFlowReturn rsn_push_src_create (RsnBaseSrc * bsrc, guint64 offset,
    guint length, GstBuffer ** ret);

static void
rsn_push_src_base_init (gpointer g_class)
{
  /* nop */
}

static void
rsn_push_src_class_init (RsnPushSrcClass * klass)
{
  RsnBaseSrcClass *gstbasesrc_class = (RsnBaseSrcClass *) klass;

  gstbasesrc_class->create = GST_DEBUG_FUNCPTR (rsn_push_src_create);
  gstbasesrc_class->check_get_range =
      GST_DEBUG_FUNCPTR (rsn_push_src_check_get_range);
}

static void
rsn_push_src_init (RsnPushSrc * pushsrc, RsnPushSrcClass * klass)
{
  /* nop */
}

static gboolean
rsn_push_src_check_get_range (RsnBaseSrc * src)
{
  /* a pushsrc can by default never operate in pull mode override
   * if you want something different. */
  return FALSE;
}

static GstFlowReturn
rsn_push_src_create (RsnBaseSrc * bsrc, guint64 offset, guint length,
    GstBuffer ** ret)
{
  GstFlowReturn fret;
  RsnPushSrc *src;
  RsnPushSrcClass *pclass;

  src = GST_PUSH_SRC (bsrc);
  pclass = GST_PUSH_SRC_GET_CLASS (src);
  if (pclass->create)
    fret = pclass->create (src, ret);
  else
    fret = GST_FLOW_ERROR;

  return fret;
}
