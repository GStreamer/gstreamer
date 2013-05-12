/* GStreamer
 *
 * gstofa.h
 *
 * Copyright (C) 2006 M. Derezynski
 * Copyright (C) 2008 Eric Buehl
 * Copyright (C) 2008 Sebastian Dröge <slomo@circular-chaos.org>
 *
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
 * Boston, MA  02110-1301 USA.
 */

#ifndef __GST_OFA_H__
#define __GST_OFA_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/audio/gstaudiofilter.h>
#include <gst/audio/audio.h>

G_BEGIN_DECLS

#define GST_TYPE_OFA \
  (gst_ofa_get_type())
#define GST_OFA(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OFA,GstOFA))
#define GST_OFA_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OFA,GstOFAClass))
#define GST_IS_OFA(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OFA))
#define GST_IS_OFA_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OFA))

#define GST_TAG_OFA_FINGERPRINT "ofa-fingerprint"

typedef struct _GstOFA GstOFA;
typedef struct _GstOFAClass GstOFAClass;


/**
 * GstOFA:
 *
 * Opaque #GstOFA data structure
 */

struct _GstOFA
{
  GstAudioFilter audiofilter;

  /*< private > */

  GstAdapter *adapter;
  char *fingerprint;
  gboolean record;
};

struct _GstOFAClass
{
  GstAudioFilterClass audiofilter_class;
};

GType gst_ofa_get_type (void);

G_END_DECLS

#endif /* __GST_OFA_H__ */
