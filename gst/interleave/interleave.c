/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *                    2007 Andy Wingo <wingo at pobox.com>
 *
 * interleave.c: interleave samples, based on gstsignalprocessor.c
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
 * SECTION:element-interleave
 *
 * <refsect2>
 * <para>
 * Merges separate mono inputs into one interleaved stream.
 * </para>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch-0.10 filesrc location=song.ogg ! decodebin ! audioconvert ! ladspa-gverb name=g ! interleave name=i ! audioconvert ! autoaudiosink g. ! i.
 * </programlisting>
 * Apply ladspa gverb to the music and merge separate left/right outputs into a
 * stereo stream for playback.
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>


#define GST_TYPE_INTERLEAVE            (gst_interleave_get_type())
#define GST_INTERLEAVE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_INTERLEAVE,GstInterleave))
#define GST_INTERLEAVE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_INTERLEAVE,GstInterleaveClass))
#define GST_INTERLEAVE_GET_CLASS(obj) \
        (G_TYPE_INSTANCE_GET_CLASS ((obj),GST_TYPE_INTERLEAVE,GstInterleaveClass))
#define GST_IS_INTERLEAVE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_INTERLEAVE))
#define GST_IS_INTERLEAVE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_INTERLEAVE))


typedef struct _GstInterleave GstInterleave;
typedef struct _GstInterleaveClass GstInterleaveClass;


struct _GstInterleave
{
  GstElement element;

  GstCaps *sinkcaps;
  guint channels;

  GstPad *src;

  GstActivateMode mode;

  guint pending_in;
};

struct _GstInterleaveClass
{
  GstElementClass parent_class;
};


GST_DEBUG_CATEGORY_STATIC (gst_interleave_debug);
#define GST_CAT_DEFAULT gst_interleave_debug


static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) 1, "
        "endianness = (int) BYTE_ORDER, " "width = (int) 32")
    );
static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, MAX ], "
        "endianness = (int) BYTE_ORDER, " "width = (int) 32")
    );


#define GST_TYPE_INTERLEAVE_PAD (gst_interleave_pad_get_type ())
#define GST_INTERLEAVE_PAD(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_INTERLEAVE_PAD,\
    GstInterleavePad))
typedef struct _GstInterleavePad GstInterleavePad;
typedef GstPadClass GstInterleavePadClass;

struct _GstInterleavePad
{
  GstPad parent;

  GstBuffer *pen;

  guint channel;

  /* these are only used for sink pads */
  guint samples_avail;
  gfloat *data;
};

static GType
gst_interleave_pad_get_type (void)
{
  static GType type = 0;

  if (!type) {
    static const GTypeInfo info = {
      sizeof (GstInterleavePadClass), NULL, NULL, NULL, NULL,
      NULL, sizeof (GstInterleavePad), 0, NULL
    };

    type = g_type_register_static (GST_TYPE_PAD, "GstInterleavePad", &info, 0);
  }
  return type;
}


GST_BOILERPLATE (GstInterleave, gst_interleave, GstElement, GST_TYPE_ELEMENT);


static gboolean gst_interleave_src_activate_pull (GstPad * pad,
    gboolean active);
static gboolean gst_interleave_sink_activate_push (GstPad * pad,
    gboolean active);
static GstPad *gst_interleave_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);

static GstFlowReturn gst_interleave_getrange (GstPad * pad,
    guint64 offset, guint length, GstBuffer ** buffer);
static GstFlowReturn gst_interleave_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_interleave_src_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_interleave_sink_setcaps (GstPad * pad, GstCaps * caps);
static GstCaps *gst_interleave_src_getcaps (GstPad * pad);


static const GstElementDetails details =
GST_ELEMENT_DETAILS ("Audio interleaver",
    "Filter/Converter/Audio",
    "Folds many mono channels into one interleaved audio stream",
    "Andy Wingo <wingo at pobox.com>");

static void
gst_interleave_base_init (gpointer g_class)
{
  GST_DEBUG_CATEGORY_INIT (gst_interleave_debug, "interleave", 0,
      "interleave element");

  gst_element_class_set_details (g_class, &details);

  gst_element_class_add_pad_template (g_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (g_class,
      gst_static_pad_template_get (&src_template));
}

static void
gst_interleave_class_init (GstInterleaveClass * klass)
{
  GstElementClass *gstelement_class;

  gstelement_class = GST_ELEMENT_CLASS (klass);

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_interleave_request_new_pad);
}

static void
gst_interleave_init (GstInterleave * self, GstInterleaveClass * klass)
{
  self->pending_in = 0;
  self->mode = GST_ACTIVATE_NONE;

  self->src = gst_pad_new_from_static_template (&src_template, "src");

  gst_pad_set_getrange_function (self->src,
      GST_DEBUG_FUNCPTR (gst_interleave_getrange));
  gst_pad_set_activatepull_function (self->src,
      GST_DEBUG_FUNCPTR (gst_interleave_src_activate_pull));
  gst_pad_set_setcaps_function (self->src,
      GST_DEBUG_FUNCPTR (gst_interleave_src_setcaps));
  gst_pad_set_getcaps_function (self->src,
      GST_DEBUG_FUNCPTR (gst_interleave_src_getcaps));

  gst_element_add_pad (GST_ELEMENT (self), self->src);
}

static GstPad *
gst_interleave_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name)
{
  GstInterleave *self = GST_INTERLEAVE (element);
  GstPad *new_pad;
  gchar *pad_name;

  pad_name = g_strdup_printf ("sink%d", self->channels);
  new_pad = g_object_new (GST_TYPE_INTERLEAVE_PAD, "name", pad_name,
      "direction", templ->direction, "template", templ, NULL);
  g_free (pad_name);
  GST_INTERLEAVE_PAD (new_pad)->channel = self->channels;
  ++self->channels;

  gst_pad_set_setcaps_function (new_pad,
      GST_DEBUG_FUNCPTR (gst_interleave_sink_setcaps));
  gst_pad_set_chain_function (new_pad,
      GST_DEBUG_FUNCPTR (gst_interleave_chain));
  gst_pad_set_activatepush_function (new_pad,
      GST_DEBUG_FUNCPTR (gst_interleave_sink_activate_push));

  self->pending_in++;

  GST_PAD_UNSET_FLUSHING (new_pad);
  gst_element_add_pad (element, new_pad);

  return new_pad;
}

static void
gst_interleave_unset_caps (GstInterleave * self)
{
  GstElement *elem;
  GList *sinks;

  elem = GST_ELEMENT (self);

  GST_INFO_OBJECT (self, "unset_caps()");

  for (sinks = elem->sinkpads; sinks; sinks = sinks->next)
    gst_pad_set_caps (GST_PAD (sinks->data), NULL);
}

static gboolean
gst_interleave_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstInterleave *self;

  self = GST_INTERLEAVE (gst_pad_get_parent (pad));

  if (self->sinkcaps && !gst_caps_is_equal (caps, self->sinkcaps))
    goto cannot_change_caps;

  if (self->mode == GST_ACTIVATE_PULL) {
    GstPad *peer;

    if ((peer = gst_pad_get_peer (pad))) {
      gboolean res = gst_pad_set_caps (peer, caps);

      gst_object_unref (peer);
      if (!res)
        goto peer_did_not_accept;
    }
  } else {
    GstCaps *srccaps;
    gboolean res;

    srccaps = gst_caps_copy (caps);
    gst_structure_set (gst_caps_get_structure (srccaps, 0), "channels",
        G_TYPE_INT, self->channels, NULL);

    res = gst_pad_set_caps (self->src, srccaps);
    gst_caps_unref (srccaps);

    if (!res)
      goto src_did_not_accept;
  }

  if (!self->sinkcaps)
    gst_caps_replace (&self->sinkcaps, caps);

  return TRUE;

cannot_change_caps:
  {
    GST_DEBUG_OBJECT (self, "caps of %" GST_PTR_FORMAT " already set, can't "
        "change", self->sinkcaps);
    return FALSE;
  }
peer_did_not_accept:
  {
    GST_DEBUG_OBJECT (self, "peer did not accept setcaps()");
    return FALSE;
  }
src_did_not_accept:
  {
    GST_DEBUG_OBJECT (self, "src did not accept setcaps()");
    return FALSE;
  }
}

static gboolean
gst_interleave_src_setcaps (GstPad * pad, GstCaps * caps)
{
  GstInterleave *self;
  gint channels;

  self = GST_INTERLEAVE (gst_pad_get_parent (pad));

  if (!gst_structure_get_int (gst_caps_get_structure (caps, 0), "channels",
          &channels))
    goto impossible;

  if (channels != self->channels)
    goto wrong_num_channels;

  if (self->mode == GST_ACTIVATE_PULL) {
    GstCaps *sinkcaps;
    GList *l;

    sinkcaps = gst_caps_copy (caps);
    gst_structure_set (gst_caps_get_structure (sinkcaps, 0), "channels",
        G_TYPE_INT, 1, NULL);

    for (l = GST_ELEMENT (self)->sinkpads; l; l = l->next)
      if (!gst_pad_set_caps (GST_PAD (l->data), sinkcaps))
        goto sinks_did_not_accept;

    gst_caps_unref (sinkcaps);
  }

  gst_object_unref (self);

  return TRUE;

impossible:
  {
    g_warning ("caps didn't have channels property, how is this possible");
    gst_object_unref (self);
    return FALSE;
  }
wrong_num_channels:
  {
    GST_INFO_OBJECT (self, "bad number of channels (%d != %d)",
        self->channels, channels);
    gst_object_unref (self);
    return FALSE;
  }
sinks_did_not_accept:
  {
    /* assume they already logged */
    gst_object_unref (self);
    return FALSE;
  }
}

static GstCaps *
gst_interleave_src_getcaps (GstPad * pad)
{
  GstInterleave *self;
  GList *l;
  GstCaps *ret;

  self = GST_INTERLEAVE (gst_pad_get_parent (pad));

  ret = gst_caps_copy (gst_pad_get_pad_template_caps (pad));

  for (l = GST_ELEMENT (self)->sinkpads; l; l = l->next) {
    GstCaps *sinkcaps, *oldcaps;

    oldcaps = ret;
    sinkcaps = gst_pad_get_caps (GST_PAD (l->data));
    ret = gst_caps_intersect (sinkcaps, oldcaps);
    gst_caps_unref (oldcaps);
    gst_caps_unref (sinkcaps);
  }

  if (self->channels)
    gst_structure_set (gst_caps_get_structure (ret, 0), "channels", G_TYPE_INT,
        self->channels, NULL);

  gst_object_unref (self);

  return ret;
}

static void
gst_interleave_update_inputs (GstInterleave * self, guint nprocessed)
{
  GstElement *elem = (GstElement *) self;
  GList *sinks;

  for (sinks = elem->sinkpads; sinks; sinks = sinks->next) {
    GstInterleavePad *sinkpad;

    sinkpad = (GstInterleavePad *) sinks->data;
    g_assert (sinkpad->samples_avail >= nprocessed);

    if (sinkpad->pen && sinkpad->samples_avail == nprocessed) {
      /* used up this buffer, unpen */
      gst_buffer_unref (sinkpad->pen);
      sinkpad->pen = NULL;
    }

    if (!sinkpad->pen) {
      /* this buffer was used up */
      self->pending_in++;
      sinkpad->data = NULL;
      sinkpad->samples_avail = 0;
    } else {
      /* advance ->data pointers and decrement ->samples_avail, unreffing buffer
         if no samples are left */
      sinkpad->samples_avail -= nprocessed;
      sinkpad->data += nprocessed;      /* gfloat* arithmetic */
    }
  }
}

static GstFlowReturn
gst_interleave_process (GstInterleave * self, guint nframes, GstBuffer ** buf)
{
  GstFlowReturn ret;
  GstElement *elem;
  GList *sinks;
  guint bufsize, i, j, channels;
  gfloat *in, *out;

  g_return_val_if_fail (self->pending_in == 0, GST_FLOW_ERROR);

  elem = GST_ELEMENT (self);

  /* determine the number of samples that we can process */
  for (sinks = elem->sinkpads; sinks; sinks = sinks->next) {
    GstInterleavePad *sinkpad = (GstInterleavePad *) sinks->data;

    g_assert (sinkpad->samples_avail > 0);
    nframes = MIN (nframes, sinkpad->samples_avail);
  }

  channels = self->channels;
  bufsize = nframes * channels * sizeof (gfloat);

  ret = gst_pad_alloc_buffer (GST_PAD (self->src), -1,
      bufsize, GST_PAD_CAPS (self->src), buf);

  if (ret != GST_FLOW_OK)
    goto alloc_buffer_failed;

  if (GST_BUFFER_SIZE (*buf) != bufsize)
    goto alloc_buffer_bad_size;

  gst_buffer_set_caps (*buf, GST_PAD_CAPS (self->src));

  /* do the thing */
  for (sinks = elem->sinkpads, i = 0; sinks; sinks = sinks->next, i++) {
    GstInterleavePad *sinkpad = (GstInterleavePad *) sinks->data;

    out = (gfloat *) GST_BUFFER_DATA (*buf);
    out += i;                   /* gfloat* arith */
    in = sinkpad->data;
    for (j = 0; j < nframes; j++)
      out[j * channels] = in[j];
  }

  gst_interleave_update_inputs (self, nframes);

  return ret;

alloc_buffer_failed:
  {
    GST_WARNING ("gst_pad_alloc_buffer() returned %d", ret);
    return ret;
  }
alloc_buffer_bad_size:
  {
    GST_WARNING ("called alloc_buffer() for %d bytes but got %d", bufsize,
        GST_BUFFER_SIZE (*buf));
    gst_buffer_unref (*buf);
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

static GstFlowReturn
gst_interleave_pen_buffer (GstInterleave * self, GstPad * pad,
    GstBuffer * buffer)
{
  GstInterleavePad *spad = (GstInterleavePad *) pad;

  if (spad->pen)
    goto had_buffer;

  /* keep the reference */
  spad->pen = buffer;
  spad->data = (gfloat *) GST_BUFFER_DATA (buffer);
  spad->samples_avail = GST_BUFFER_SIZE (buffer) / sizeof (float);

  g_assert (self->pending_in != 0);

  self->pending_in--;

  return GST_FLOW_OK;

  /* ERRORS */
had_buffer:
  {
    GST_WARNING ("Pad %s:%s already has penned buffer",
        GST_DEBUG_PAD_NAME (pad));
    gst_buffer_unref (buffer);
    return GST_FLOW_ERROR;
  }
}

static void
gst_interleave_flush (GstInterleave * self)
{
  GList *pads;

  GST_INFO_OBJECT (self, "flush()");

  for (pads = GST_ELEMENT (self)->sinkpads; pads; pads = pads->next) {
    GstInterleavePad *spad = (GstInterleavePad *) pads->data;

    if (spad->pen) {
      gst_buffer_unref (spad->pen);
      spad->pen = NULL;
      spad->data = NULL;
      spad->samples_avail = 0;
    }
  }

  self->pending_in = GST_ELEMENT (self)->numsinkpads;
}

static GstFlowReturn
gst_interleave_do_pulls (GstInterleave * self, guint nframes)
{
  GList *sinkpads;
  GstFlowReturn ret = GST_FLOW_OK;

  /* FIXME: not threadsafe atm */

  sinkpads = GST_ELEMENT (self)->sinkpads;

  for (; sinkpads; sinkpads = sinkpads->next) {
    GstInterleavePad *spad = (GstInterleavePad *) sinkpads->data;
    GstBuffer *buf;

    if (spad->pen) {
      g_warning ("Unexpectedly full buffer pen for pad %s:%s",
          GST_DEBUG_PAD_NAME (spad));
      continue;
    }

    ret =
        gst_pad_pull_range (GST_PAD (spad), -1, nframes * sizeof (gfloat),
        &buf);
    if (ret != GST_FLOW_OK)
      goto pull_failed;

    if (!buf)
      goto no_buffer;

    ret = gst_interleave_pen_buffer (self, GST_PAD (spad), buf);
    if (ret != GST_FLOW_OK)
      goto pull_failed;
  }

  return ret;

pull_failed:
  {
    gst_interleave_flush (self);
    return ret;
  }
no_buffer:
  {
    g_critical ("Pull failed to make a buffer!");
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_interleave_getrange (GstPad * pad, guint64 offset,
    guint length, GstBuffer ** buffer)
{
  GstInterleave *self;
  GstFlowReturn ret = GST_FLOW_ERROR;
  guint nframes;

  self = GST_INTERLEAVE (gst_pad_get_parent (pad));

  nframes = length / self->channels / sizeof (gfloat);

  ret = gst_interleave_do_pulls (self, nframes);

  if (ret == GST_FLOW_OK)
    ret = gst_interleave_process (self, nframes, buffer);

  GST_DEBUG_OBJECT (self, "returns %s", gst_flow_get_name (ret));

  gst_object_unref (self);

  return ret;
}

static GstFlowReturn
gst_interleave_chain (GstPad * pad, GstBuffer * buffer)
{
  GstFlowReturn ret;
  GstInterleave *self;

  self = GST_INTERLEAVE (gst_pad_get_parent (pad));

  ret = gst_interleave_pen_buffer (self, pad, buffer);
  if (ret != GST_FLOW_OK)
    goto pen_failed;

  if (self->pending_in == 0) {
    GstBuffer *out;

    ret = gst_interleave_process (self, G_MAXUINT, &out);
    if (ret != GST_FLOW_OK)
      goto process_failed;

    ret = gst_pad_push (self->src, out);
  }

done:
  gst_object_unref (self);
  return ret;

pen_failed:
  {
    GST_WARNING_OBJECT (self, "pen failed");
    goto done;
  }
process_failed:
  {
    GST_WARNING_OBJECT (self, "process failed");
    goto done;
  }
}

static gboolean
gst_interleave_sink_activate_push (GstPad * pad, gboolean active)
{
  gboolean result = TRUE;
  GstInterleave *self;

  self = GST_INTERLEAVE (gst_pad_get_parent (pad));

  if (active) {
    if (self->mode == GST_ACTIVATE_NONE) {
      self->mode = GST_ACTIVATE_PUSH;
      result = TRUE;
    } else if (self->mode == GST_ACTIVATE_PUSH) {
      result = TRUE;
    } else {
      g_warning ("foo");
      result = FALSE;
    }
  } else {
    if (self->mode == GST_ACTIVATE_NONE) {
      result = TRUE;
    } else if (self->mode == GST_ACTIVATE_PUSH) {
      self->mode = GST_ACTIVATE_NONE;
      result = TRUE;
    } else {
      g_warning ("foo");
      result = FALSE;
    }
  }

  GST_DEBUG_OBJECT (self, "result : %d", result);

  gst_object_unref (self);

  return result;
}

static gboolean
gst_interleave_src_activate_pull (GstPad * pad, gboolean active)
{
  gboolean result = TRUE;
  GstInterleave *self;

  self = GST_INTERLEAVE (gst_pad_get_parent (pad));

  if (active) {
    if (self->mode == GST_ACTIVATE_NONE) {
      GList *l;

      if (GST_ELEMENT (self)->sinkpads) {
        for (l = GST_ELEMENT (self)->sinkpads; l; l = l->next)
          result &= gst_pad_activate_pull (GST_PAD (l->data), active);
      } else {
        /* nobody has requested pads, seems i am operating in delayed-request
           push mode */
        result = FALSE;
      }
      if (result)
        self->mode = GST_ACTIVATE_PULL;
    } else if (self->mode == GST_ACTIVATE_PULL) {
      result = TRUE;
    } else {
      g_warning ("foo");
      result = FALSE;
    }
  } else {
    if (self->mode == GST_ACTIVATE_NONE) {
      result = TRUE;
    } else if (self->mode == GST_ACTIVATE_PULL) {
      GList *l;

      for (l = GST_ELEMENT (self)->sinkpads; l; l = l->next)
        result &= gst_pad_activate_pull (GST_PAD (l->data), active);
      if (result)
        self->mode = GST_ACTIVATE_NONE;
      result = TRUE;
    } else {
      g_warning ("foo");
      result = FALSE;
    }

    gst_interleave_unset_caps (self);
    gst_interleave_flush (self);
  }

  GST_DEBUG_OBJECT (self, "result : %d", result);

  gst_object_unref (self);

  return result;
}
