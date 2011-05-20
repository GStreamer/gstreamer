/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *                    2005 Andy Wingo <wingo@fluendo.com>
 *                    2005 Thomas Vander Stichele <thomas at apestaart dot org>
 *                    2008 Wim Taymans <wim.taymans@gmail.com>
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
 * SECTION:gstbasetransform
 * @short_description: Base class for simple transform filters
 * @see_also: #GstBaseSrc, #GstBaseSink
 *
 * This base class is for filter elements that process data.
 *
 * It provides for:
 * <itemizedlist>
 *   <listitem><para>one sinkpad and one srcpad</para></listitem>
 *   <listitem><para>
 *      Possible formats on sink and source pad implemented
 *      with custom transform_caps function. By default uses
 *      same format on sink and source.
 *   </para></listitem>
 *   <listitem><para>Handles state changes</para></listitem>
 *   <listitem><para>Does flushing</para></listitem>
 *   <listitem><para>Push mode</para></listitem>
 *   <listitem><para>
 *       Pull mode if the sub-class transform can operate on arbitrary data
 *    </para></listitem>
 * </itemizedlist>
 *
 * <refsect2>
 * <title>Use Cases</title>
 * <para>
 * <orderedlist>
 * <listitem>
 *   <itemizedlist><title>Passthrough mode</title>
 *   <listitem><para>
 *     Element has no interest in modifying the buffer. It may want to inspect it,
 *     in which case the element should have a transform_ip function. If there
 *     is no transform_ip function in passthrough mode, the buffer is pushed
 *     intact.
 *   </para></listitem>
 *   <listitem><para>
 *     On the GstBaseTransformClass is the passthrough_on_same_caps variable
 *     which will automatically set/unset passthrough based on whether the
 *     element negotiates the same caps on both pads.
 *   </para></listitem>
 *   <listitem><para>
 *     passthrough_on_same_caps on an element that doesn't implement a
 *     transform_caps function is useful for elements that only inspect data
 *     (such as level)
 *   </para></listitem>
 *   </itemizedlist>
 *   <itemizedlist>
 *   <title>Example elements</title>
 *     <listitem>Level</listitem>
 *     <listitem>Videoscale, audioconvert, ffmpegcolorspace, audioresample in
 *     certain modes.</listitem>
 *   </itemizedlist>
 * </listitem>
 * <listitem>
 *   <itemizedlist>
 *     <title>Modifications in-place - input buffer and output buffer are the
 *     same thing.</title>
 *   <listitem><para>
 *     The element must implement a transform_ip function.
 *   </para></listitem>
 *   <listitem><para>
 *     Output buffer size must <= input buffer size
 *   </para></listitem>
 *   <listitem><para>
 *     If the always_in_place flag is set, non-writable buffers will be copied
 *     and passed to the transform_ip function, otherwise a new buffer will be
 *     created and the transform function called.
 *   </para></listitem>
 *   <listitem><para>
 *     Incoming writable buffers will be passed to the transform_ip function
 *     immediately.  </para></listitem>
 *   <listitem><para>
 *     only implementing transform_ip and not transform implies always_in_place
 *     = TRUE
 *   </para></listitem>
 *   </itemizedlist>
 *   <itemizedlist>
 *   <title>Example elements</title>
 *     <listitem>Volume</listitem>
 *     <listitem>Audioconvert in certain modes (signed/unsigned
 *     conversion)</listitem>
 *     <listitem>ffmpegcolorspace in certain modes (endianness
 *     swapping)</listitem>
 *   </itemizedlist>
 *  </listitem>
 * <listitem>
 *   <itemizedlist>
 *   <title>Modifications only to the caps/metadata of a buffer</title>
 *   <listitem><para>
 *     The element does not require writable data, but non-writable buffers
 *     should be subbuffered so that the meta-information can be replaced.
 *   </para></listitem>
 *   <listitem><para>
 *     Elements wishing to operate in this mode should replace the
 *     prepare_output_buffer method to create subbuffers of the input buffer
 *     and set always_in_place to TRUE
 *   </para></listitem>
 *   </itemizedlist>
 *   <itemizedlist>
 *   <title>Example elements</title>
 *     <listitem>Capsfilter when setting caps on outgoing buffers that have
 *     none.</listitem>
 *     <listitem>identity when it is going to re-timestamp buffers by
 *     datarate.</listitem>
 *   </itemizedlist>
 * </listitem>
 * <listitem>
 *   <itemizedlist><title>Normal mode</title>
 *   <listitem><para>
 *     always_in_place flag is not set, or there is no transform_ip function
 *   </para></listitem>
 *   <listitem><para>
 *     Element will receive an input buffer and output buffer to operate on.
 *   </para></listitem>
 *   <listitem><para>
 *     Output buffer is allocated by calling the prepare_output_buffer function.
 *   </para></listitem>
 *   </itemizedlist>
 *   <itemizedlist>
 *   <title>Example elements</title>
 *     <listitem>Videoscale, ffmpegcolorspace, audioconvert when doing
 *     scaling/conversions</listitem>
 *   </itemizedlist>
 * </listitem>
 * <listitem>
 *   <itemizedlist><title>Special output buffer allocations</title>
 *   <listitem><para>
 *     Elements which need to do special allocation of their output buffers
 *     other than what gst_buffer_pad_alloc allows should implement a
 *     prepare_output_buffer method, which calls the parent implementation and
 *     passes the newly allocated buffer.
 *   </para></listitem>
 *   </itemizedlist>
 *   <itemizedlist>
 *   <title>Example elements</title>
 *     <listitem>efence</listitem>
 *   </itemizedlist>
 * </listitem>
 * </orderedlist>
 * </para>
 * </refsect2>
 * <refsect2>
 * <title>Sub-class settable flags on GstBaseTransform</title>
 * <para>
 * <itemizedlist>
 * <listitem><para>
 *   <itemizedlist><title>passthrough</title>
 *     <listitem><para>
 *       Implies that in the current configuration, the sub-class is not
 *       interested in modifying the buffers.
 *     </para></listitem>
 *     <listitem><para>
 *       Elements which are always in passthrough mode whenever the same caps
 *       has been negotiated on both pads can set the class variable
 *       passthrough_on_same_caps to have this behaviour automatically.
 *     </para></listitem>
 *   </itemizedlist>
 * </para></listitem>
 * <listitem><para>
 *   <itemizedlist><title>always_in_place</title>
 *     <listitem><para>
 *       Determines whether a non-writable buffer will be copied before passing
 *       to the transform_ip function.
 *     </para></listitem>
 *     <listitem><para>
 *       Implied TRUE if no transform function is implemented.
 *     </para></listitem>
 *     <listitem><para>
 *       Implied FALSE if ONLY transform function is implemented.
 *     </para></listitem>
 *   </itemizedlist>
 * </para></listitem>
 * </itemizedlist>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include "../../../gst/gst_private.h"
#include "../../../gst/gst-i18n-lib.h"
#include "gstbasetransform.h"
#include <gst/gstmarshal.h>

GST_DEBUG_CATEGORY_STATIC (gst_base_transform_debug);
#define GST_CAT_DEFAULT gst_base_transform_debug

/* BaseTransform signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_PROP_QOS	FALSE

enum
{
  PROP_0,
  PROP_QOS
};

#define GST_BASE_TRANSFORM_GET_PRIVATE(obj)  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_BASE_TRANSFORM, GstBaseTransformPrivate))

struct _GstBaseTransformPrivate
{
  /* QoS *//* with LOCK */
  gboolean qos_enabled;
  gdouble proportion;
  GstClockTime earliest_time;
  /* previous buffer had a discont */
  gboolean discont;

  GstActivateMode pad_mode;

  gboolean gap_aware;

  gboolean reconfigure;

  /* QoS stats */
  guint64 processed;
  guint64 dropped;

  GstClockTime position_out;

  GstBufferPool *srcpool;
};

static GstElementClass *parent_class = NULL;

static void gst_base_transform_class_init (GstBaseTransformClass * klass);
static void gst_base_transform_init (GstBaseTransform * trans,
    GstBaseTransformClass * klass);
static GstFlowReturn gst_base_transform_prepare_output_buffer (GstBaseTransform
    * trans, GstBuffer * input, GstBuffer ** buf);

GType
gst_base_transform_get_type (void)
{
  static volatile gsize base_transform_type = 0;

  if (g_once_init_enter (&base_transform_type)) {
    GType _type;
    static const GTypeInfo base_transform_info = {
      sizeof (GstBaseTransformClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_base_transform_class_init,
      NULL,
      NULL,
      sizeof (GstBaseTransform),
      0,
      (GInstanceInitFunc) gst_base_transform_init,
    };

    _type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstBaseTransform", &base_transform_info, G_TYPE_FLAG_ABSTRACT);
    g_once_init_leave (&base_transform_type, _type);
  }
  return base_transform_type;
}

static void gst_base_transform_finalize (GObject * object);
static void gst_base_transform_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_base_transform_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_base_transform_src_activate_pull (GstPad * pad,
    gboolean active);
static gboolean gst_base_transform_sink_activate_push (GstPad * pad,
    gboolean active);
static gboolean gst_base_transform_activate (GstBaseTransform * trans,
    gboolean active);
static gboolean gst_base_transform_get_unit_size (GstBaseTransform * trans,
    GstCaps * caps, gsize * size);

static gboolean gst_base_transform_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_base_transform_src_eventfunc (GstBaseTransform * trans,
    GstEvent * event);
static gboolean gst_base_transform_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_base_transform_sink_eventfunc (GstBaseTransform * trans,
    GstEvent * event);
static gboolean gst_base_transform_check_get_range (GstPad * pad);
static GstFlowReturn gst_base_transform_getrange (GstPad * pad, guint64 offset,
    guint length, GstBuffer ** buffer);
static GstFlowReturn gst_base_transform_chain (GstPad * pad,
    GstBuffer * buffer);
static GstCaps *gst_base_transform_getcaps (GstPad * pad, GstCaps * filter);
static gboolean gst_base_transform_acceptcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_base_transform_acceptcaps_default (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps);
static gboolean gst_base_transform_setcaps (GstBaseTransform * trans,
    GstPad * pad, GstCaps * caps);
static gboolean gst_base_transform_query (GstPad * pad, GstQuery * query);
static const GstQueryType *gst_base_transform_query_type (GstPad * pad);

/* static guint gst_base_transform_signals[LAST_SIGNAL] = { 0 }; */

static void
gst_base_transform_finalize (GObject * object)
{
  GstBaseTransform *trans;

  trans = GST_BASE_TRANSFORM (object);

  g_mutex_free (trans->transform_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_base_transform_class_init (GstBaseTransformClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_base_transform_debug, "basetransform", 0,
      "basetransform element");

  GST_DEBUG ("gst_base_transform_class_init");

  g_type_class_add_private (klass, sizeof (GstBaseTransformPrivate));

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_base_transform_set_property;
  gobject_class->get_property = gst_base_transform_get_property;

  g_object_class_install_property (gobject_class, PROP_QOS,
      g_param_spec_boolean ("qos", "QoS", "Handle Quality-of-Service events",
          DEFAULT_PROP_QOS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gobject_class->finalize = gst_base_transform_finalize;

  klass->passthrough_on_same_caps = FALSE;
  klass->event = GST_DEBUG_FUNCPTR (gst_base_transform_sink_eventfunc);
  klass->src_event = GST_DEBUG_FUNCPTR (gst_base_transform_src_eventfunc);
  klass->accept_caps =
      GST_DEBUG_FUNCPTR (gst_base_transform_acceptcaps_default);
}

static void
gst_base_transform_init (GstBaseTransform * trans,
    GstBaseTransformClass * bclass)
{
  GstPadTemplate *pad_template;

  GST_DEBUG ("gst_base_transform_init");

  trans->priv = GST_BASE_TRANSFORM_GET_PRIVATE (trans);

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (bclass), "sink");
  g_return_if_fail (pad_template != NULL);
  trans->sinkpad = gst_pad_new_from_template (pad_template, "sink");
  gst_pad_set_getcaps_function (trans->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_getcaps));
  gst_pad_set_acceptcaps_function (trans->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_acceptcaps));
  gst_pad_set_event_function (trans->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_sink_event));
  gst_pad_set_chain_function (trans->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_chain));
  gst_pad_set_activatepush_function (trans->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_sink_activate_push));
  gst_pad_set_query_function (trans->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_query));
  gst_pad_set_query_type_function (trans->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_query_type));
  gst_element_add_pad (GST_ELEMENT (trans), trans->sinkpad);

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (bclass), "src");
  g_return_if_fail (pad_template != NULL);
  trans->srcpad = gst_pad_new_from_template (pad_template, "src");
  gst_pad_set_getcaps_function (trans->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_getcaps));
  gst_pad_set_acceptcaps_function (trans->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_acceptcaps));
  gst_pad_set_event_function (trans->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_src_event));
  gst_pad_set_checkgetrange_function (trans->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_check_get_range));
  gst_pad_set_getrange_function (trans->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_getrange));
  gst_pad_set_activatepull_function (trans->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_src_activate_pull));
  gst_pad_set_query_function (trans->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_query));
  gst_pad_set_query_type_function (trans->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_query_type));
  gst_element_add_pad (GST_ELEMENT (trans), trans->srcpad);

  trans->transform_lock = g_mutex_new ();
  trans->pending_configure = FALSE;
  trans->priv->qos_enabled = DEFAULT_PROP_QOS;
  trans->cache_caps1 = NULL;
  trans->cache_caps2 = NULL;
  trans->priv->pad_mode = GST_ACTIVATE_NONE;
  trans->priv->gap_aware = FALSE;

  trans->passthrough = FALSE;
  if (bclass->transform == NULL) {
    /* If no transform function, always_in_place is TRUE */
    GST_DEBUG_OBJECT (trans, "setting in_place TRUE");
    trans->always_in_place = TRUE;

    if (bclass->transform_ip == NULL) {
      GST_DEBUG_OBJECT (trans, "setting passthrough TRUE");
      trans->passthrough = TRUE;
    }
  }

  trans->priv->processed = 0;
  trans->priv->dropped = 0;
}

/* given @caps on the src or sink pad (given by @direction)
 * calculate the possible caps on the other pad.
 *
 * Returns new caps, unref after usage.
 */
static GstCaps *
gst_base_transform_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *ret;
  GstBaseTransformClass *klass;

  if (caps == NULL)
    return NULL;

  klass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  /* if there is a custom transform function, use this */
  if (klass->transform_caps) {
    GstCaps *temp;
    gint i;

    /* start with empty caps */
    ret = gst_caps_new_empty ();
    GST_DEBUG_OBJECT (trans, "transform caps (direction = %d)", direction);

    if (gst_caps_is_any (caps)) {
      /* for any caps we still have to call the transform function */
      GST_DEBUG_OBJECT (trans, "from: ANY");
      temp = klass->transform_caps (trans, direction, caps, filter);
      GST_DEBUG_OBJECT (trans, "  to: %" GST_PTR_FORMAT, temp);
      temp = gst_caps_make_writable (temp);
      gst_caps_append (ret, temp);
    } else {
      gint n = gst_caps_get_size (caps);
      /* we send caps with just one structure to the transform
       * function as this is easier for the element */
      for (i = 0; i < n; i++) {
        GstCaps *nth;

        nth = gst_caps_copy_nth (caps, i);
        GST_LOG_OBJECT (trans, "from[%d]: %" GST_PTR_FORMAT, i, nth);
        temp = klass->transform_caps (trans, direction, nth, filter);
        gst_caps_unref (nth);

        GST_LOG_OBJECT (trans, "  to[%d]: %" GST_PTR_FORMAT, i, temp);

        temp = gst_caps_make_writable (temp);

        /* here we need to only append those structures, that are not yet
         * in there, we use the merge function for this */
        gst_caps_merge (ret, temp);

        GST_LOG_OBJECT (trans, "  merged[%d]: %" GST_PTR_FORMAT, i, ret);
      }
      GST_LOG_OBJECT (trans, "merged: (%d)", gst_caps_get_size (ret));
      /* FIXME: we can't do much simplification here because we don't really want to
       * change the caps order
       gst_caps_do_simplify (ret);
       GST_DEBUG_OBJECT (trans, "simplified: (%d)", gst_caps_get_size (ret));
       */
    }

#ifndef G_DISABLE_ASSERT
    if (filter) {
      if (!gst_caps_is_subset (ret, filter)) {
        GST_ERROR_OBJECT (trans,
            "transform_caps returned caps %" GST_PTR_FORMAT
            " which are not a real subset of the filter caps %"
            GST_PTR_FORMAT, ret, filter);
        g_warning ("%s: transform_caps returned caps which are not a real "
            "subset of the filter caps", GST_ELEMENT_NAME (trans));

        temp = gst_caps_intersect_full (filter, ret, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref (ret);
        ret = temp;
      }
    }
#endif
  } else {
    GST_DEBUG_OBJECT (trans, "identity from: %" GST_PTR_FORMAT, caps);
    /* no transform function, use the identity transform */
    if (filter) {
      ret = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    } else {
      ret = gst_caps_ref (caps);
    }
  }

  GST_DEBUG_OBJECT (trans, "to: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (ret),
      ret);

  return ret;
}

/* transform a buffer of @size with @caps on the pad with @direction to
 * the size of a buffer with @othercaps and store the result in @othersize
 *
 * We have two ways of doing this:
 *  1) use a custom transform size function, this is for complicated custom
 *     cases with no fixed unit_size.
 *  2) use the unit_size functions where there is a relationship between the
 *     caps and the size of a buffer.
 */
static gboolean
gst_base_transform_transform_size (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps,
    gsize size, GstCaps * othercaps, gsize * othersize)
{
  gsize inunitsize, outunitsize, units;
  GstBaseTransformClass *klass;
  gboolean ret;

  klass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  GST_DEBUG_OBJECT (trans, "asked to transform size %d for caps %"
      GST_PTR_FORMAT " to size for caps %" GST_PTR_FORMAT " in direction %s",
      size, caps, othercaps, direction == GST_PAD_SRC ? "SRC" : "SINK");

  if (klass->transform_size) {
    /* if there is a custom transform function, use this */
    ret = klass->transform_size (trans, direction, caps, size, othercaps,
        othersize);
  } else if (klass->get_unit_size == NULL) {
    /* if there is no transform_size and no unit_size, it means the
     * element does not modify the size of a buffer */
    *othersize = size;
    ret = TRUE;
  } else {
    /* there is no transform_size function, we have to use the unit_size
     * functions. This method assumes there is a fixed unit_size associated with
     * each caps. We provide the same amount of units on both sides. */
    if (!gst_base_transform_get_unit_size (trans, caps, &inunitsize))
      goto no_in_size;

    GST_DEBUG_OBJECT (trans, "input size %d, input unit size %d", size,
        inunitsize);

    /* input size must be a multiple of the unit_size of the input caps */
    if (inunitsize == 0 || (size % inunitsize != 0))
      goto no_multiple;

    /* get the amount of units */
    units = size / inunitsize;

    /* now get the unit size of the output */
    if (!gst_base_transform_get_unit_size (trans, othercaps, &outunitsize))
      goto no_out_size;

    /* the output size is the unit_size times the amount of units on the
     * input */
    *othersize = units * outunitsize;
    GST_DEBUG_OBJECT (trans, "transformed size to %d", *othersize);

    ret = TRUE;
  }
  return ret;

  /* ERRORS */
no_in_size:
  {
    GST_DEBUG_OBJECT (trans, "could not get in_size");
    g_warning ("%s: could not get in_size", GST_ELEMENT_NAME (trans));
    return FALSE;
  }
no_multiple:
  {
    GST_DEBUG_OBJECT (trans, "Size %u is not a multiple of unit size %u", size,
        inunitsize);
    g_warning ("%s: size %" G_GSIZE_FORMAT " is not a multiple of unit size %"
        G_GSIZE_FORMAT, GST_ELEMENT_NAME (trans), size, inunitsize);
    return FALSE;
  }
no_out_size:
  {
    GST_DEBUG_OBJECT (trans, "could not get out_size");
    g_warning ("%s: could not get out_size", GST_ELEMENT_NAME (trans));
    return FALSE;
  }
}

/* get the caps that can be handled by @pad. We perform:
 *
 *  - take the caps of peer of otherpad,
 *  - filter against the padtemplate of otherpad, 
 *  - calculate all transforms of remaining caps
 *  - filter against template of @pad
 *
 * If there is no peer, we simply return the caps of the padtemplate of pad.
 */
static GstCaps *
gst_base_transform_getcaps (GstPad * pad, GstCaps * filter)
{
  GstBaseTransform *trans;
  GstPad *otherpad;
  GstCaps *peercaps, *caps, *peerfilter = NULL;

  trans = GST_BASE_TRANSFORM (gst_pad_get_parent (pad));

  otherpad = (pad == trans->srcpad) ? trans->sinkpad : trans->srcpad;

  /* we can do what the peer can */
  if (filter) {
    GstCaps *temp, *templ;

    GST_DEBUG_OBJECT (pad, "filter caps  %" GST_PTR_FORMAT, filter);

    /* filtered against our padtemplate on the other side */
    templ = gst_pad_get_pad_template_caps (pad);
    GST_DEBUG_OBJECT (pad, "our template  %" GST_PTR_FORMAT, templ);
    temp = gst_caps_intersect_full (filter, templ, GST_CAPS_INTERSECT_FIRST);
    GST_DEBUG_OBJECT (pad, "intersected %" GST_PTR_FORMAT, temp);
    gst_caps_unref (templ);

    /* then see what we can transform this to */
    peerfilter = gst_base_transform_transform_caps (trans,
        GST_PAD_DIRECTION (pad), temp, NULL);
    GST_DEBUG_OBJECT (pad, "transformed  %" GST_PTR_FORMAT, peerfilter);
    gst_caps_unref (temp);

    /* and filter against the template of this pad */
    templ = gst_pad_get_pad_template_caps (otherpad);
    GST_DEBUG_OBJECT (pad, "our template  %" GST_PTR_FORMAT, templ);
    /* We keep the caps sorted like the returned caps */
    temp =
        gst_caps_intersect_full (peerfilter, templ, GST_CAPS_INTERSECT_FIRST);
    GST_DEBUG_OBJECT (pad, "intersected %" GST_PTR_FORMAT, temp);
    gst_caps_unref (peerfilter);
    gst_caps_unref (templ);
    peerfilter = temp;
  }

  peercaps = gst_pad_peer_get_caps (otherpad, peerfilter);

  if (peerfilter)
    gst_caps_unref (peerfilter);

  if (peercaps) {
    GstCaps *temp, *templ;

    GST_DEBUG_OBJECT (pad, "peer caps  %" GST_PTR_FORMAT, peercaps);

    /* filtered against our padtemplate on the other side */
    templ = gst_pad_get_pad_template_caps (otherpad);
    GST_DEBUG_OBJECT (pad, "our template  %" GST_PTR_FORMAT, templ);
    temp = gst_caps_intersect_full (peercaps, templ, GST_CAPS_INTERSECT_FIRST);
    GST_DEBUG_OBJECT (pad, "intersected %" GST_PTR_FORMAT, temp);
    gst_caps_unref (templ);

    /* then see what we can transform this to */
    caps = gst_base_transform_transform_caps (trans,
        GST_PAD_DIRECTION (otherpad), temp, filter);
    GST_DEBUG_OBJECT (pad, "transformed  %" GST_PTR_FORMAT, caps);
    gst_caps_unref (temp);
    if (caps == NULL)
      goto done;

    /* and filter against the template of this pad */
    templ = gst_pad_get_pad_template_caps (pad);
    GST_DEBUG_OBJECT (pad, "our template  %" GST_PTR_FORMAT, templ);
    /* We keep the caps sorted like the returned caps */
    temp = gst_caps_intersect_full (caps, templ, GST_CAPS_INTERSECT_FIRST);
    GST_DEBUG_OBJECT (pad, "intersected %" GST_PTR_FORMAT, temp);
    gst_caps_unref (caps);
    gst_caps_unref (templ);
    caps = temp;

    /* Now try if we can put the untransformed downstream caps first */
    temp = gst_caps_intersect_full (peercaps, caps, GST_CAPS_INTERSECT_FIRST);
    if (!gst_caps_is_empty (temp)) {
      gst_caps_merge (temp, caps);
      caps = temp;
    } else {
      gst_caps_unref (temp);
    }
  } else {
    /* no peer or the peer can do anything, our padtemplate is enough then */
    caps = gst_pad_get_pad_template_caps (pad);

    if (filter) {
      GstCaps *temp;

      temp = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
      gst_caps_unref (caps);
      caps = temp;
    }
  }

done:
  GST_DEBUG_OBJECT (trans, "returning  %" GST_PTR_FORMAT, caps);

  if (peercaps)
    gst_caps_unref (peercaps);

  gst_object_unref (trans);

  return caps;
}

static gboolean
gst_base_transform_do_bufferpool (GstBaseTransform * trans, GstCaps * outcaps)
{
  GstQuery *query;
  GstBufferPool *pool = NULL, *oldpool;
  guint size, min, max, prefix, alignment;

  /* there are these possibilities:
   *
   * 1) we negotiated passthrough, we can proxy the bufferpool directly.
   * 2) 
   */

  /* clear old pool */
  oldpool = trans->priv->srcpool;
  if (oldpool) {
    gst_buffer_pool_set_active (oldpool, FALSE);
    gst_object_unref (oldpool);
    trans->priv->srcpool = oldpool = NULL;
  }

  if (trans->passthrough) {
    /* we are in passthrough, the input buffer is never copied and always passed
     * along. We never allocate an output buffer on the srcpad. What we do is
     * let the upstream element decide if it wants to use a bufferpool and
     * then we will proxy the downstream pool */
    GST_DEBUG_OBJECT (trans, "we're passthough, delay bufferpool");
    return TRUE;
  }

  /* not passthrough, we need to allocate */
  /* find a pool for the negotiated caps now */
  query = gst_query_new_allocation (outcaps, TRUE);

  if (gst_pad_peer_query (trans->srcpad, query))
    goto query_failed;

  /* we got configuration from our peer, parse them */
  gst_query_parse_allocation_params (query, &size, &min, &max, &prefix,
      &alignment, &pool);
  gst_query_unref (query);

  if (pool == NULL) {
    GstStructure *config;

    /* we did not get a pool, make one ourselves then */
    pool = gst_buffer_pool_new ();

    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_set (config, outcaps, size, min, max, prefix, 0,
        alignment);
    gst_buffer_pool_set_config (pool, config);
  }

  /* activate */
  if (!gst_buffer_pool_set_active (pool, TRUE))
    goto activate_failed;

  /* and store */
  trans->priv->srcpool = pool;

  return TRUE;

  /* ERRORS */
query_failed:
  {
    GST_DEBUG_OBJECT (trans, "allocation query failed");
    gst_query_unref (query);
    return FALSE;
  }
activate_failed:
  {
    GST_ERROR_OBJECT (trans, "failed to activate bufferpool.");
    gst_object_unref (pool);
    return FALSE;
  }
}

/* function triggered when the in and out caps are negotiated and need
 * to be configured in the subclass. */
static gboolean
gst_base_transform_configure_caps (GstBaseTransform * trans, GstCaps * in,
    GstCaps * out)
{
  gboolean ret = TRUE;
  GstBaseTransformClass *klass;

  klass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  GST_DEBUG_OBJECT (trans, "in caps:  %" GST_PTR_FORMAT, in);
  GST_DEBUG_OBJECT (trans, "out caps: %" GST_PTR_FORMAT, out);

  /* clear the cache */
  gst_caps_replace (&trans->cache_caps1, NULL);
  gst_caps_replace (&trans->cache_caps2, NULL);

  /* figure out same caps state */
  trans->have_same_caps = gst_caps_is_equal (in, out);
  GST_DEBUG_OBJECT (trans, "have_same_caps: %d", trans->have_same_caps);

  /* If we've a transform_ip method and same input/output caps, set in_place
   * by default. If for some reason the sub-class prefers using a transform
   * function, it can clear the in place flag in the set_caps */
  gst_base_transform_set_in_place (trans,
      klass->transform_ip && trans->have_same_caps);

  /* Set the passthrough if the class wants passthrough_on_same_caps
   * and we have the same caps on each pad */
  if (klass->passthrough_on_same_caps)
    gst_base_transform_set_passthrough (trans, trans->have_same_caps);

  /* now configure the element with the caps */
  if (klass->set_caps) {
    GST_DEBUG_OBJECT (trans, "Calling set_caps method to setup caps");
    ret = klass->set_caps (trans, in, out);
  }

  if (ret) {
    /* try to get a pool when needed */
    gst_base_transform_do_bufferpool (trans, out);
  }

  trans->negotiated = ret;

  return ret;
}

/* given a fixed @caps on @pad, create the best possible caps for the
 * other pad.
 * @caps must be fixed when calling this function.
 *
 * This function calls the transform caps vmethod of the basetransform to figure
 * out the possible target formats. It then tries to select the best format from
 * this list by:
 *
 * - attempt passthrough if the target caps is a superset of the input caps
 * - fixating by using peer caps
 * - fixating with transform fixate function
 * - fixating with pad fixate functions.
 *
 * this function returns a caps that can be transformed into and is accepted by
 * the peer element.
 */
static GstCaps *
gst_base_transform_find_transform (GstBaseTransform * trans, GstPad * pad,
    GstCaps * caps)
{
  GstBaseTransformClass *klass;
  GstPad *otherpad, *otherpeer;
  GstCaps *othercaps;
  gboolean peer_checked = FALSE;
  gboolean is_fixed;

  /* caps must be fixed here, this is a programming error if it's not */
  g_return_val_if_fail (gst_caps_is_fixed (caps), NULL);

  klass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  otherpad = (pad == trans->srcpad) ? trans->sinkpad : trans->srcpad;
  otherpeer = gst_pad_get_peer (otherpad);

  /* see how we can transform the input caps. We need to do this even for
   * passthrough because it might be possible that this element cannot support
   * passthrough at all. */
  othercaps = gst_base_transform_transform_caps (trans,
      GST_PAD_DIRECTION (pad), caps, NULL);

  /* The caps we can actually output is the intersection of the transformed
   * caps with the pad template for the pad */
  if (othercaps) {
    GstCaps *intersect, *templ_caps;

    templ_caps = gst_pad_get_pad_template_caps (otherpad);
    GST_DEBUG_OBJECT (trans,
        "intersecting against padtemplate %" GST_PTR_FORMAT, templ_caps);

    intersect =
        gst_caps_intersect_full (othercaps, templ_caps,
        GST_CAPS_INTERSECT_FIRST);

    gst_caps_unref (othercaps);
    gst_caps_unref (templ_caps);
    othercaps = intersect;
  }

  /* check if transform is empty */
  if (!othercaps || gst_caps_is_empty (othercaps))
    goto no_transform;

  /* if the othercaps are not fixed, we need to fixate them, first attempt
   * is by attempting passthrough if the othercaps are a superset of caps. */
  /* FIXME. maybe the caps is not fixed because it has multiple structures of
   * fixed caps */
  is_fixed = gst_caps_is_fixed (othercaps);
  if (!is_fixed) {
    GST_DEBUG_OBJECT (trans,
        "transform returned non fixed  %" GST_PTR_FORMAT, othercaps);

    /* Now let's see what the peer suggests based on our transformed caps */
    if (otherpeer) {
      GstCaps *peercaps, *intersection, *templ_caps;

      GST_DEBUG_OBJECT (trans,
          "Checking peer caps with filter %" GST_PTR_FORMAT, othercaps);

      peercaps = gst_pad_get_caps (otherpeer, othercaps);
      GST_DEBUG_OBJECT (trans, "Resulted in %" GST_PTR_FORMAT, peercaps);

      templ_caps = gst_pad_get_pad_template_caps (otherpad);

      GST_DEBUG_OBJECT (trans,
          "Intersecting with template caps %" GST_PTR_FORMAT, templ_caps);

      intersection =
          gst_caps_intersect_full (peercaps, templ_caps,
          GST_CAPS_INTERSECT_FIRST);
      GST_DEBUG_OBJECT (trans, "Intersection: %" GST_PTR_FORMAT, intersection);
      gst_caps_unref (peercaps);
      gst_caps_unref (templ_caps);
      peercaps = intersection;

      GST_DEBUG_OBJECT (trans,
          "Intersecting with transformed caps %" GST_PTR_FORMAT, othercaps);
      intersection =
          gst_caps_intersect_full (peercaps, othercaps,
          GST_CAPS_INTERSECT_FIRST);
      GST_DEBUG_OBJECT (trans, "Intersection: %" GST_PTR_FORMAT, intersection);
      gst_caps_unref (peercaps);
      gst_caps_unref (othercaps);
      othercaps = intersection;
      is_fixed = gst_caps_is_fixed (othercaps);
      peer_checked = TRUE;
    } else {
      GST_DEBUG_OBJECT (trans, "no peer, doing passthrough");
      gst_caps_unref (othercaps);
      othercaps = gst_caps_ref (caps);
      is_fixed = TRUE;
    }
  }
  if (gst_caps_is_empty (othercaps))
    goto no_transform_possible;

  /* second attempt at fixation, call the fixate vmethod and
   * ultimately call the pad fixate function. */
  if (!is_fixed) {
    GST_DEBUG_OBJECT (trans,
        "trying to fixate %" GST_PTR_FORMAT " on pad %s:%s",
        othercaps, GST_DEBUG_PAD_NAME (otherpad));

    /* since we have no other way to fixate left, we might as well just take
     * the first of the caps list and fixate that */

    /* FIXME: when fixating using the vmethod, it might make sense to fixate
     * each of the caps; but Wim doesn't see a use case for that yet */
    gst_caps_truncate (othercaps);
    peer_checked = FALSE;

    if (klass->fixate_caps) {
      GST_DEBUG_OBJECT (trans, "trying to fixate %" GST_PTR_FORMAT
          " using caps %" GST_PTR_FORMAT
          " on pad %s:%s using fixate_caps vmethod", othercaps, caps,
          GST_DEBUG_PAD_NAME (otherpad));
      klass->fixate_caps (trans, GST_PAD_DIRECTION (pad), caps, othercaps);
      is_fixed = gst_caps_is_fixed (othercaps);
    }
    /* if still not fixed, no other option but to let the default pad fixate
     * function do its job */
    if (!is_fixed) {
      GST_DEBUG_OBJECT (trans, "trying to fixate %" GST_PTR_FORMAT
          " on pad %s:%s using gst_pad_fixate_caps", othercaps,
          GST_DEBUG_PAD_NAME (otherpad));
      gst_pad_fixate_caps (otherpad, othercaps);
      is_fixed = gst_caps_is_fixed (othercaps);
    }
    GST_DEBUG_OBJECT (trans, "after fixating %" GST_PTR_FORMAT, othercaps);
  } else {
    GST_DEBUG ("caps are fixed");
    /* else caps are fixed but the subclass may want to add fields */
    if (klass->fixate_caps) {
      othercaps = gst_caps_make_writable (othercaps);

      GST_DEBUG_OBJECT (trans, "doing fixate %" GST_PTR_FORMAT
          " using caps %" GST_PTR_FORMAT
          " on pad %s:%s using fixate_caps vmethod", othercaps, caps,
          GST_DEBUG_PAD_NAME (otherpad));

      klass->fixate_caps (trans, GST_PAD_DIRECTION (pad), caps, othercaps);
      is_fixed = gst_caps_is_fixed (othercaps);
    }
  }

  /* caps should be fixed now, if not we have to fail. */
  if (!is_fixed)
    goto could_not_fixate;

  /* and peer should accept, don't check again if we already checked the
   * othercaps against the peer. */
  if (!peer_checked && otherpeer && !gst_pad_accept_caps (otherpeer, othercaps))
    goto peer_no_accept;

  GST_DEBUG_OBJECT (trans, "Input caps were %" GST_PTR_FORMAT
      ", and got final caps %" GST_PTR_FORMAT, caps, othercaps);

  if (otherpeer)
    gst_object_unref (otherpeer);

  return othercaps;

  /* ERRORS */
no_transform:
  {
    GST_DEBUG_OBJECT (trans,
        "transform returned useless  %" GST_PTR_FORMAT, othercaps);
    goto error_cleanup;
  }
no_transform_possible:
  {
    GST_DEBUG_OBJECT (trans,
        "transform could not transform %" GST_PTR_FORMAT
        " in anything we support", caps);
    goto error_cleanup;
  }
could_not_fixate:
  {
    GST_DEBUG_OBJECT (trans, "FAILED to fixate %" GST_PTR_FORMAT, othercaps);
    goto error_cleanup;
  }
peer_no_accept:
  {
    GST_DEBUG_OBJECT (trans, "FAILED to get peer of %" GST_PTR_FORMAT
        " to accept %" GST_PTR_FORMAT, otherpad, othercaps);
    goto error_cleanup;
  }
error_cleanup:
  {
    if (otherpeer)
      gst_object_unref (otherpeer);
    if (othercaps)
      gst_caps_unref (othercaps);
    return NULL;
  }
}

static gboolean
gst_base_transform_acceptcaps_default (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps)
{
#if 0
  GstPad *otherpad;
  GstCaps *othercaps = NULL;
#endif
  gboolean ret = TRUE;

#if 0
  otherpad = (pad == trans->srcpad) ? trans->sinkpad : trans->srcpad;

  /* we need fixed caps for the check, fall back to the default implementation
   * if we don't */
  if (!gst_caps_is_fixed (caps))
#endif
  {
    GstCaps *allowed;

    GST_DEBUG_OBJECT (trans, "non fixed accept caps %" GST_PTR_FORMAT, caps);

    /* get all the formats we can handle on this pad */
    if (direction == GST_PAD_SRC)
      allowed = gst_pad_get_caps (trans->srcpad, NULL);
    else
      allowed = gst_pad_get_caps (trans->sinkpad, NULL);

    if (!allowed) {
      GST_DEBUG_OBJECT (trans, "gst_pad_get_caps() failed");
      goto no_transform_possible;
    }

    GST_DEBUG_OBJECT (trans, "allowed caps %" GST_PTR_FORMAT, allowed);

    /* intersect with the requested format */
    ret = gst_caps_can_intersect (allowed, caps);
    gst_caps_unref (allowed);

    if (!ret)
      goto no_transform_possible;
  }
#if 0
  else {
    GST_DEBUG_OBJECT (pad, "accept caps %" GST_PTR_FORMAT, caps);

    /* find best possible caps for the other pad as a way to see if we can
     * transform this caps. */
    othercaps = gst_base_transform_find_transform (trans, pad, caps, FALSE);
    if (!othercaps || gst_caps_is_empty (othercaps))
      goto no_transform_possible;

    GST_DEBUG_OBJECT (pad, "we can transform to %" GST_PTR_FORMAT, othercaps);
  }
#endif

done:
#if 0
  /* We know it's always NULL since we never use it */
  if (othercaps)
    gst_caps_unref (othercaps);
#endif

  return ret;

  /* ERRORS */
no_transform_possible:
  {
    GST_DEBUG_OBJECT (trans,
        "transform could not transform %" GST_PTR_FORMAT
        " in anything we support", caps);
    ret = FALSE;
    goto done;
  }
}

static gboolean
gst_base_transform_acceptcaps (GstPad * pad, GstCaps * caps)
{
  gboolean ret = TRUE;
  GstBaseTransform *trans;
  GstBaseTransformClass *bclass;

  trans = GST_BASE_TRANSFORM (gst_pad_get_parent (pad));
  bclass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  if (bclass->accept_caps)
    ret = bclass->accept_caps (trans, GST_PAD_DIRECTION (pad), caps);

  gst_object_unref (trans);

  return ret;
}

/* called when new caps arrive on the sink or source pad,
 * We try to find the best caps for the other side using our _find_transform()
 * function. If there are caps, we configure the transform for this new
 * transformation.
 *
 * FIXME, this function is currently commutative but this should not really be
 * because we never set caps starting from the srcpad.
 */
static gboolean
gst_base_transform_setcaps (GstBaseTransform * trans, GstPad * pad,
    GstCaps * caps)
{
  GstPad *otherpad, *otherpeer;
  GstCaps *othercaps = NULL;
  gboolean ret = TRUE;
  GstCaps *incaps, *outcaps;

  otherpad = (pad == trans->srcpad) ? trans->sinkpad : trans->srcpad;
  otherpeer = gst_pad_get_peer (otherpad);

  /* if we get called recursively, we bail out now to avoid an
   * infinite loop. */
  if (GST_PAD_IS_IN_SETCAPS (otherpad))
    goto done;

  GST_DEBUG_OBJECT (pad, "have new caps %p %" GST_PTR_FORMAT, caps, caps);

  /* find best possible caps for the other pad */
  othercaps = gst_base_transform_find_transform (trans, pad, caps);
  if (!othercaps || gst_caps_is_empty (othercaps))
    goto no_transform_possible;

  /* configure the element now */
  /* make sure in and out caps are correct */
  if (pad == trans->sinkpad) {
    incaps = caps;
    outcaps = othercaps;
  } else {
    incaps = othercaps;
    outcaps = caps;
  }

  /* if we have the same caps, we can optimize and reuse the input caps */
  if (gst_caps_is_equal (incaps, outcaps)) {
    GST_INFO_OBJECT (trans, "reuse caps");
    gst_caps_unref (othercaps);
    outcaps = othercaps = gst_caps_ref (incaps);
  }

  /* call configure now */
  if (!(ret = gst_base_transform_configure_caps (trans, incaps, outcaps)))
    goto failed_configure;

  /* we know this will work, we implement the setcaps */
  gst_pad_set_caps (otherpad, othercaps);

  if (pad == trans->srcpad && trans->priv->pad_mode == GST_ACTIVATE_PULL) {
    /* FIXME hm? */
    ret &= gst_pad_set_caps (otherpeer, othercaps);
    if (!ret) {
      GST_INFO_OBJECT (trans, "otherpeer setcaps(%" GST_PTR_FORMAT ") failed",
          othercaps);
    }
  }

done:
  if (otherpeer)
    gst_object_unref (otherpeer);
  if (othercaps)
    gst_caps_unref (othercaps);

  trans->negotiated = ret;

  return ret;

  /* ERRORS */
no_transform_possible:
  {
    GST_WARNING_OBJECT (trans,
        "transform could not transform %" GST_PTR_FORMAT
        " in anything we support", caps);
    ret = FALSE;
    goto done;
  }
failed_configure:
  {
    GST_WARNING_OBJECT (trans, "FAILED to configure caps %" GST_PTR_FORMAT
        " to accept %" GST_PTR_FORMAT, otherpad, othercaps);
    ret = FALSE;
    goto done;
  }
}

static gboolean
gst_base_transform_query (GstPad * pad, GstQuery * query)
{
  gboolean ret = FALSE;
  GstBaseTransform *trans;
  GstPad *otherpad;

  trans = GST_BASE_TRANSFORM (gst_pad_get_parent (pad));
  if (G_UNLIKELY (trans == NULL))
    return FALSE;
  otherpad = (pad == trans->srcpad) ? trans->sinkpad : trans->srcpad;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ALLOCATION:
    {
      gboolean passthrough;

      /* can only be done on the sinkpad */
      if (!GST_PAD_IS_SINK (pad))
        goto done;

      GST_BASE_TRANSFORM_LOCK (trans);
      passthrough = trans->passthrough;
      GST_BASE_TRANSFORM_UNLOCK (trans);

      if (passthrough)
        ret = gst_pad_peer_query (otherpad, query);
      else
        ret = FALSE;
      break;
    }
    case GST_QUERY_POSITION:
    {
      GstFormat format;

      gst_query_parse_position (query, &format, NULL);
      if (format == GST_FORMAT_TIME && trans->segment.format == GST_FORMAT_TIME) {
        gint64 pos;
        ret = TRUE;

        if ((pad == trans->sinkpad)
            || (trans->priv->position_out == GST_CLOCK_TIME_NONE)) {
          pos =
              gst_segment_to_stream_time (&trans->segment, GST_FORMAT_TIME,
              trans->segment.position);
        } else {
          pos = gst_segment_to_stream_time (&trans->segment, GST_FORMAT_TIME,
              trans->priv->position_out);
        }
        gst_query_set_position (query, format, pos);
      } else {
        ret = gst_pad_peer_query (otherpad, query);
      }
      break;
    }
    default:
      ret = gst_pad_peer_query (otherpad, query);
      break;
  }

done:
  gst_object_unref (trans);
  return ret;
}

static const GstQueryType *
gst_base_transform_query_type (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_NONE
  };

  return types;
}

/* Allocate a buffer using gst_pad_alloc_buffer
 *
 * This function can do renegotiation on the source pad
 *
 * The output buffer is always writable. outbuf can be equal to
 * inbuf, the caller should be prepared for this and perform
 * appropriate refcounting.
 */
static GstFlowReturn
gst_base_transform_prepare_output_buffer (GstBaseTransform * trans,
    GstBuffer * in_buf, GstBuffer ** out_buf)
{
  GstBaseTransformClass *bclass;
  GstBaseTransformPrivate *priv;
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean copymeta;
  gsize insize, outsize;
  GstCaps *incaps = NULL, *outcaps = NULL;

  bclass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  priv = trans->priv;

  *out_buf = NULL;

  insize = gst_buffer_get_size (in_buf);

  /* figure out how to allocate a buffer based on the current configuration */
  if (trans->passthrough) {
    GST_DEBUG_OBJECT (trans, "doing passthrough alloc");
    /* passthrough, the output size is the same as the input size. */
    outsize = insize;
  } else {
    gboolean want_in_place = (bclass->transform_ip != NULL)
        && trans->always_in_place;

    if (want_in_place) {
      GST_DEBUG_OBJECT (trans, "doing inplace alloc");
      /* we alloc a buffer of the same size as the input */
      outsize = insize;
    } else {
      incaps = gst_pad_get_current_caps (trans->sinkpad);
      outcaps = gst_pad_get_current_caps (trans->srcpad);

      GST_DEBUG_OBJECT (trans, "getting output size for copy transform");
      /* copy transform, figure out the output size */
      if (!gst_base_transform_transform_size (trans,
              GST_PAD_SINK, incaps, insize, outcaps, &outsize)) {
        goto unknown_size;
      }
    }
  }

  if (bclass->prepare_output_buffer) {
    if (outcaps == NULL)
      outcaps = gst_pad_get_current_caps (trans->srcpad);

    GST_DEBUG_OBJECT (trans,
        "calling prepare buffer with caps %p %" GST_PTR_FORMAT, outcaps,
        outcaps);
    ret =
        bclass->prepare_output_buffer (trans, in_buf, outsize, outcaps,
        out_buf);

    /* get a new ref to the srcpad caps, the prepare_output_buffer function can
     * update the pad caps if it wants */
    if (outcaps)
      gst_caps_unref (outcaps);
    outcaps = gst_pad_get_current_caps (trans->srcpad);

    /* FIXME 0.11:
     * decrease refcount again if vmethod returned refcounted in_buf. This
     * is because we need to make sure that the buffer is writable for the
     * in_place transform. The docs of the vmethod say that you should return
     * a reffed inbuf, which is exactly what we don't want :), oh well.. */
    if (in_buf == *out_buf)
      gst_buffer_unref (in_buf);
  }

  if (ret != GST_FLOW_OK)
    goto alloc_failed;

  if (*out_buf == NULL) {
    if (trans->passthrough) {
      GST_DEBUG_OBJECT (trans, "Avoiding pad alloc");
      *out_buf = in_buf;
    } else if (trans->priv->srcpool) {
      GST_DEBUG_OBJECT (trans, "using pool alloc");
      ret =
          gst_buffer_pool_acquire_buffer (trans->priv->srcpool, out_buf, NULL);
    } else {
      GST_DEBUG_OBJECT (trans, "doing alloc of size %u", outsize);
      *out_buf = gst_buffer_new_and_alloc (outsize);
    }
  }

  if (ret != GST_FLOW_OK)
    goto alloc_failed;

  /* must always have a buffer by now */
  if (*out_buf == NULL)
    goto no_buffer;

  if (trans->passthrough && in_buf != *out_buf) {
    /* we are asked to perform a passthrough transform but the input and
     * output buffers are different. We have to discard the output buffer and
     * reuse the input buffer. */
    GST_DEBUG_OBJECT (trans, "passthrough but different buffers");
    gst_buffer_unref (*out_buf);
    *out_buf = in_buf;
  }
  GST_DEBUG_OBJECT (trans, "using allocated buffer in %p, out %p", in_buf,
      *out_buf);

  /* if we have different buffers, check if the metadata is ok */
  copymeta = FALSE;
  if (*out_buf != in_buf) {
    guint mask;

    mask = GST_BUFFER_FLAG_PREROLL | GST_BUFFER_FLAG_IN_CAPS |
        GST_BUFFER_FLAG_DELTA_UNIT | GST_BUFFER_FLAG_DISCONT |
        GST_BUFFER_FLAG_GAP | GST_BUFFER_FLAG_MEDIA1 |
        GST_BUFFER_FLAG_MEDIA2 | GST_BUFFER_FLAG_MEDIA3;
    /* see if the flags and timestamps match */
    copymeta =
        (GST_MINI_OBJECT_FLAGS (*out_buf) & mask) ==
        (GST_MINI_OBJECT_FLAGS (in_buf) & mask);
    copymeta |=
        GST_BUFFER_TIMESTAMP (*out_buf) != GST_BUFFER_TIMESTAMP (in_buf) ||
        GST_BUFFER_DURATION (*out_buf) != GST_BUFFER_DURATION (in_buf) ||
        GST_BUFFER_OFFSET (*out_buf) != GST_BUFFER_OFFSET (in_buf) ||
        GST_BUFFER_OFFSET_END (*out_buf) != GST_BUFFER_OFFSET_END (in_buf);
  }

  /* we need to modify the metadata when the element is not gap aware,
   * passthrough is not used and the gap flag is set */
  copymeta |= !trans->priv->gap_aware && !trans->passthrough
      && (GST_MINI_OBJECT_FLAGS (*out_buf) & GST_BUFFER_FLAG_GAP);

  if (copymeta) {
    GST_DEBUG_OBJECT (trans, "copymeta %d", copymeta);
    if (!gst_buffer_is_writable (*out_buf)) {
      GST_DEBUG_OBJECT (trans, "buffer %p not writable", *out_buf);
      if (in_buf == *out_buf)
        *out_buf = gst_buffer_copy (in_buf);
      else
        *out_buf = gst_buffer_make_writable (*out_buf);
    }
    /* when we get here, the metadata should be writable */
    if (copymeta)
      gst_buffer_copy_into (*out_buf, in_buf,
          GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);
    /* clear the GAP flag when the subclass does not understand it */
    if (!trans->priv->gap_aware)
      GST_BUFFER_FLAG_UNSET (*out_buf, GST_BUFFER_FLAG_GAP);
  }

done:
  if (incaps)
    gst_caps_unref (incaps);
  if (outcaps)
    gst_caps_unref (outcaps);

  return ret;

  /* ERRORS */
alloc_failed:
  {
    GST_WARNING_OBJECT (trans, "pad-alloc failed: %s", gst_flow_get_name (ret));
    goto done;
  }
no_buffer:
  {
    GST_ELEMENT_ERROR (trans, STREAM, NOT_IMPLEMENTED,
        ("Sub-class failed to provide an output buffer"), (NULL));
    ret = GST_FLOW_ERROR;
    goto done;
  }
unknown_size:
  {
    GST_ERROR_OBJECT (trans, "unknown output size");
    ret = GST_FLOW_ERROR;
    goto done;
  }
}

/* Given @caps calcultate the size of one unit.
 *
 * For video caps, this is the size of one frame (and thus one buffer).
 * For audio caps, this is the size of one sample.
 *
 * These values are cached since they do not change and the calculation
 * potentially involves parsing caps and other expensive stuff.
 *
 * We have two cache locations to store the size, one for the source caps
 * and one for the sink caps.
 *
 * this function returns FALSE if no size could be calculated.
 */
static gboolean
gst_base_transform_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    gsize * size)
{
  gboolean res = FALSE;
  GstBaseTransformClass *bclass;

  /* see if we have the result cached */
  if (trans->cache_caps1 == caps) {
    *size = trans->cache_caps1_size;
    GST_DEBUG_OBJECT (trans, "returned %d from first cache", *size);
    return TRUE;
  }
  if (trans->cache_caps2 == caps) {
    *size = trans->cache_caps2_size;
    GST_DEBUG_OBJECT (trans, "returned %d from second cached", *size);
    return TRUE;
  }

  bclass = GST_BASE_TRANSFORM_GET_CLASS (trans);
  if (bclass->get_unit_size) {
    res = bclass->get_unit_size (trans, caps, size);
    GST_DEBUG_OBJECT (trans, "caps %" GST_PTR_FORMAT
        ") has unit size %d, result %s", caps, *size, res ? "TRUE" : "FALSE");

    if (res) {
      /* and cache the values */
      if (trans->cache_caps1 == NULL) {
        gst_caps_replace (&trans->cache_caps1, caps);
        trans->cache_caps1_size = *size;
        GST_DEBUG_OBJECT (trans, "caching %d in first cache", *size);
      } else if (trans->cache_caps2 == NULL) {
        gst_caps_replace (&trans->cache_caps2, caps);
        trans->cache_caps2_size = *size;
        GST_DEBUG_OBJECT (trans, "caching %d in second cache", *size);
      } else {
        GST_DEBUG_OBJECT (trans, "no free spot to cache unit_size");
      }
    }
  } else {
    GST_DEBUG_OBJECT (trans, "Sub-class does not implement get_unit_size");
  }
  return res;
}

static gboolean
gst_base_transform_sink_event (GstPad * pad, GstEvent * event)
{
  GstBaseTransform *trans;
  GstBaseTransformClass *bclass;
  gboolean ret = TRUE;
  gboolean forward = TRUE;

  trans = GST_BASE_TRANSFORM (gst_pad_get_parent (pad));
  if (G_UNLIKELY (trans == NULL)) {
    gst_event_unref (event);
    return FALSE;
  }
  bclass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  if (bclass->event)
    forward = bclass->event (trans, event);

  /* FIXME, do this in the default event handler so the subclass can do
   * something different. */
  if (forward)
    ret = gst_pad_push_event (trans->srcpad, event);
  else
    gst_event_unref (event);

  gst_object_unref (trans);

  return ret;
}

static gboolean
gst_base_transform_sink_eventfunc (GstBaseTransform * trans, GstEvent * event)
{
  gboolean forward = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      break;
    case GST_EVENT_FLUSH_STOP:
      GST_OBJECT_LOCK (trans);
      /* reset QoS parameters */
      trans->priv->proportion = 1.0;
      trans->priv->earliest_time = -1;
      trans->priv->discont = FALSE;
      trans->priv->processed = 0;
      trans->priv->dropped = 0;
      GST_OBJECT_UNLOCK (trans);
      /* we need new segment info after the flush. */
      trans->have_segment = FALSE;
      gst_segment_init (&trans->segment, GST_FORMAT_UNDEFINED);
      trans->priv->position_out = GST_CLOCK_TIME_NONE;
      break;
    case GST_EVENT_EOS:
      break;
    case GST_EVENT_TAG:
      break;
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      gst_base_transform_setcaps (trans, trans->sinkpad, caps);

      forward = FALSE;
      break;
    }
    case GST_EVENT_SEGMENT:
    {
      gst_event_copy_segment (event, &trans->segment);
      trans->have_segment = TRUE;

      GST_DEBUG_OBJECT (trans, "received SEGMENT %" GST_SEGMENT_FORMAT,
          &trans->segment);
      break;
    }
    default:
      break;
  }

  return forward;
}

static gboolean
gst_base_transform_src_event (GstPad * pad, GstEvent * event)
{
  GstBaseTransform *trans;
  GstBaseTransformClass *bclass;
  gboolean ret = TRUE;

  trans = GST_BASE_TRANSFORM (gst_pad_get_parent (pad));
  if (G_UNLIKELY (trans == NULL)) {
    gst_event_unref (event);
    return FALSE;
  }

  bclass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  if (bclass->src_event)
    ret = bclass->src_event (trans, event);
  else
    gst_event_unref (event);

  gst_object_unref (trans);

  return ret;
}

static gboolean
gst_base_transform_src_eventfunc (GstBaseTransform * trans, GstEvent * event)
{
  gboolean ret;

  GST_DEBUG_OBJECT (trans, "handling event %p %" GST_PTR_FORMAT, event, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      break;
    case GST_EVENT_NAVIGATION:
      break;
    case GST_EVENT_QOS:
    {
      gdouble proportion;
      GstClockTimeDiff diff;
      GstClockTime timestamp;

      gst_event_parse_qos (event, NULL, &proportion, &diff, &timestamp);
      gst_base_transform_update_qos (trans, proportion, diff, timestamp);
      break;
    }
    default:
      break;
  }

  ret = gst_pad_push_event (trans->sinkpad, event);

  return ret;
}

/* perform a transform on @inbuf and put the result in @outbuf.
 *
 * This function is common to the push and pull-based operations.
 *
 * This function takes ownership of @inbuf */
static GstFlowReturn
gst_base_transform_handle_buffer (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer ** outbuf)
{
  GstBaseTransformClass *bclass;
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean want_in_place;
  GstClockTime running_time;
  GstClockTime timestamp;
  gsize insize;
  gboolean reconfigure;

  bclass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  GST_OBJECT_LOCK (trans->sinkpad);
  reconfigure = GST_PAD_NEEDS_RECONFIGURE (trans->srcpad)
      || trans->priv->reconfigure;
  GST_OBJECT_FLAG_UNSET (trans->srcpad, GST_PAD_NEED_RECONFIGURE);
  trans->priv->reconfigure = FALSE;
  GST_OBJECT_UNLOCK (trans->sinkpad);

  if (G_UNLIKELY (reconfigure)) {
    GstCaps *incaps;

    GST_DEBUG_OBJECT (trans, "we had a pending reconfigure");

    incaps = gst_pad_get_current_caps (trans->sinkpad);
    if (incaps == NULL)
      goto no_reconfigure;

    /* if we need to reconfigure we pretend a buffer with new caps arrived. This
     * will reconfigure the transform with the new output format. We can only
     * do this if the buffer actually has caps. */
    if (!gst_base_transform_setcaps (trans, trans->sinkpad, incaps)) {
      gst_caps_unref (incaps);
      goto not_negotiated;
    }
    gst_caps_unref (incaps);
  }

no_reconfigure:
  insize = gst_buffer_get_size (inbuf);

  if (GST_BUFFER_OFFSET_IS_VALID (inbuf))
    GST_DEBUG_OBJECT (trans,
        "handling buffer %p of size %" G_GSIZE_FORMAT " and offset %"
        G_GUINT64_FORMAT, inbuf, insize, GST_BUFFER_OFFSET (inbuf));
  else
    GST_DEBUG_OBJECT (trans,
        "handling buffer %p of size %" G_GSIZE_FORMAT " and offset NONE", inbuf,
        insize);

  /* Don't allow buffer handling before negotiation, except in passthrough mode
   * or if the class doesn't implement a set_caps function (in which case it doesn't
   * care about caps)
   */
  if (!trans->negotiated && !trans->passthrough && (bclass->set_caps != NULL))
    goto not_negotiated;

  /* Set discont flag so we can mark the outgoing buffer */
  if (GST_BUFFER_IS_DISCONT (inbuf)) {
    GST_DEBUG_OBJECT (trans, "got DISCONT buffer %p", inbuf);
    trans->priv->discont = TRUE;
  }

  /* can only do QoS if the segment is in TIME */
  if (trans->segment.format != GST_FORMAT_TIME)
    goto no_qos;

  /* QOS is done on the running time of the buffer, get it now */
  timestamp = GST_BUFFER_TIMESTAMP (inbuf);
  running_time = gst_segment_to_running_time (&trans->segment, GST_FORMAT_TIME,
      timestamp);

  if (running_time != -1) {
    gboolean need_skip;
    GstClockTime earliest_time;
    gdouble proportion;

    /* lock for getting the QoS parameters that are set (in a different thread)
     * with the QOS events */
    GST_OBJECT_LOCK (trans);
    earliest_time = trans->priv->earliest_time;
    proportion = trans->priv->proportion;
    /* check for QoS, don't perform conversion for buffers
     * that are known to be late. */
    need_skip = trans->priv->qos_enabled &&
        earliest_time != -1 && running_time <= earliest_time;
    GST_OBJECT_UNLOCK (trans);

    if (need_skip) {
      GstMessage *qos_msg;
      GstClockTime duration;
      guint64 stream_time;
      gint64 jitter;

      GST_CAT_DEBUG_OBJECT (GST_CAT_QOS, trans, "skipping transform: qostime %"
          GST_TIME_FORMAT " <= %" GST_TIME_FORMAT,
          GST_TIME_ARGS (running_time), GST_TIME_ARGS (earliest_time));

      trans->priv->dropped++;

      duration = GST_BUFFER_DURATION (inbuf);
      stream_time =
          gst_segment_to_stream_time (&trans->segment, GST_FORMAT_TIME,
          timestamp);
      jitter = GST_CLOCK_DIFF (running_time, earliest_time);

      qos_msg =
          gst_message_new_qos (GST_OBJECT_CAST (trans), FALSE, running_time,
          stream_time, timestamp, duration);
      gst_message_set_qos_values (qos_msg, jitter, proportion, 1000000);
      gst_message_set_qos_stats (qos_msg, GST_FORMAT_BUFFERS,
          trans->priv->processed, trans->priv->dropped);
      gst_element_post_message (GST_ELEMENT_CAST (trans), qos_msg);

      /* mark discont for next buffer */
      trans->priv->discont = TRUE;
      goto skip;
    }
  }

no_qos:

  /* first try to allocate an output buffer based on the currently negotiated
   * format. outbuf will contain a buffer suitable for doing the configured
   * transform after this function. */
  ret = gst_base_transform_prepare_output_buffer (trans, inbuf, outbuf);
  if (G_UNLIKELY (ret != GST_FLOW_OK))
    goto no_buffer;

  /* now perform the needed transform */
  if (trans->passthrough) {
    /* In passthrough mode, give transform_ip a look at the
     * buffer, without making it writable, or just push the
     * data through */
    if (bclass->transform_ip) {
      GST_DEBUG_OBJECT (trans, "doing passthrough transform");
      ret = bclass->transform_ip (trans, *outbuf);
    } else {
      GST_DEBUG_OBJECT (trans, "element is in passthrough");
    }
  } else {
    want_in_place = (bclass->transform_ip != NULL) && trans->always_in_place;

    if (want_in_place) {
      GST_DEBUG_OBJECT (trans, "doing inplace transform");

      if (inbuf != *outbuf) {
        guint8 *indata, *outdata;
        gsize insize, outsize;

        /* Different buffer. The data can still be the same when we are dealing
         * with subbuffers of the same buffer. Note that because of the FIXME in
         * prepare_output_buffer() we have decreased the refcounts of inbuf and
         * outbuf to keep them writable */
        indata = gst_buffer_map (inbuf, &insize, NULL, GST_MAP_READ);
        outdata = gst_buffer_map (*outbuf, &outsize, NULL, GST_MAP_WRITE);

        if (indata != outdata)
          memcpy (outdata, indata, insize);

        gst_buffer_unmap (inbuf, indata, insize);
        gst_buffer_unmap (*outbuf, outdata, outsize);
      }
      ret = bclass->transform_ip (trans, *outbuf);
    } else {
      GST_DEBUG_OBJECT (trans, "doing non-inplace transform");

      if (bclass->transform)
        ret = bclass->transform (trans, inbuf, *outbuf);
      else
        ret = GST_FLOW_NOT_SUPPORTED;
    }
  }

skip:
  /* only unref input buffer if we allocated a new outbuf buffer */
  if (*outbuf != inbuf)
    gst_buffer_unref (inbuf);

  return ret;

  /* ERRORS */
not_negotiated:
  {
    gst_buffer_unref (inbuf);
    GST_ELEMENT_ERROR (trans, STREAM, NOT_IMPLEMENTED,
        ("not negotiated"), ("not negotiated"));
    return GST_FLOW_NOT_NEGOTIATED;
  }
no_buffer:
  {
    gst_buffer_unref (inbuf);
    *outbuf = NULL;
    GST_WARNING_OBJECT (trans, "could not get buffer from pool: %s",
        gst_flow_get_name (ret));
    return ret;
  }
}

static gboolean
gst_base_transform_check_get_range (GstPad * pad)
{
  GstBaseTransform *trans;
  gboolean ret;

  trans = GST_BASE_TRANSFORM (gst_pad_get_parent (pad));

  ret = gst_pad_check_pull_range (trans->sinkpad);

  gst_object_unref (trans);

  return ret;
}

/* FIXME, getrange is broken, need to pull range from the other
 * end based on the transform_size result.
 */
static GstFlowReturn
gst_base_transform_getrange (GstPad * pad, guint64 offset,
    guint length, GstBuffer ** buffer)
{
  GstBaseTransform *trans;
  GstBaseTransformClass *klass;
  GstFlowReturn ret;
  GstBuffer *inbuf;

  trans = GST_BASE_TRANSFORM (gst_pad_get_parent (pad));

  ret = gst_pad_pull_range (trans->sinkpad, offset, length, &inbuf);
  if (G_UNLIKELY (ret != GST_FLOW_OK))
    goto pull_error;

  klass = GST_BASE_TRANSFORM_GET_CLASS (trans);
  if (klass->before_transform)
    klass->before_transform (trans, inbuf);

  GST_BASE_TRANSFORM_LOCK (trans);
  ret = gst_base_transform_handle_buffer (trans, inbuf, buffer);
  GST_BASE_TRANSFORM_UNLOCK (trans);

done:
  gst_object_unref (trans);

  return ret;

  /* ERRORS */
pull_error:
  {
    GST_DEBUG_OBJECT (trans, "failed to pull a buffer: %s",
        gst_flow_get_name (ret));
    goto done;
  }
}

static GstFlowReturn
gst_base_transform_chain (GstPad * pad, GstBuffer * buffer)
{
  GstBaseTransform *trans;
  GstBaseTransformClass *klass;
  GstFlowReturn ret;
  GstClockTime position = GST_CLOCK_TIME_NONE;
  GstClockTime timestamp, duration;
  GstBuffer *outbuf = NULL;

  trans = GST_BASE_TRANSFORM (GST_OBJECT_PARENT (pad));

  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  duration = GST_BUFFER_DURATION (buffer);

  /* calculate end position of the incoming buffer */
  if (timestamp != GST_CLOCK_TIME_NONE) {
    if (duration != GST_CLOCK_TIME_NONE)
      position = timestamp + duration;
    else
      position = timestamp;
  }

  klass = GST_BASE_TRANSFORM_GET_CLASS (trans);
  if (klass->before_transform)
    klass->before_transform (trans, buffer);

  /* protect transform method and concurrent buffer alloc */
  GST_BASE_TRANSFORM_LOCK (trans);
  ret = gst_base_transform_handle_buffer (trans, buffer, &outbuf);
  GST_BASE_TRANSFORM_UNLOCK (trans);

  /* outbuf can be NULL, this means a dropped buffer, if we have a buffer but
   * GST_BASE_TRANSFORM_FLOW_DROPPED we will not push either. */
  if (outbuf != NULL) {
    if ((ret == GST_FLOW_OK)) {
      GstClockTime position_out = GST_CLOCK_TIME_NONE;

      /* Remember last stop position */
      if (position != GST_CLOCK_TIME_NONE &&
          trans->segment.format == GST_FORMAT_TIME)
        trans->segment.position = position;

      if (GST_BUFFER_TIMESTAMP_IS_VALID (outbuf)) {
        position_out = GST_BUFFER_TIMESTAMP (outbuf);
        if (GST_BUFFER_DURATION_IS_VALID (outbuf))
          position_out += GST_BUFFER_DURATION (outbuf);
      } else if (position != GST_CLOCK_TIME_NONE) {
        position_out = position;
      }
      if (position_out != GST_CLOCK_TIME_NONE
          && trans->segment.format == GST_FORMAT_TIME)
        trans->priv->position_out = position_out;

      /* apply DISCONT flag if the buffer is not yet marked as such */
      if (trans->priv->discont) {
        if (!GST_BUFFER_IS_DISCONT (outbuf)) {
          outbuf = gst_buffer_make_writable (outbuf);
          GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
        }
        trans->priv->discont = FALSE;
      }
      trans->priv->processed++;
      ret = gst_pad_push (trans->srcpad, outbuf);
    } else {
      gst_buffer_unref (outbuf);
    }
  }

  /* convert internal flow to OK and mark discont for the next buffer. */
  if (ret == GST_BASE_TRANSFORM_FLOW_DROPPED) {
    trans->priv->discont = TRUE;
    ret = GST_FLOW_OK;
  }

  return ret;
}

static void
gst_base_transform_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBaseTransform *trans;

  trans = GST_BASE_TRANSFORM (object);

  switch (prop_id) {
    case PROP_QOS:
      gst_base_transform_set_qos_enabled (trans, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_base_transform_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstBaseTransform *trans;

  trans = GST_BASE_TRANSFORM (object);

  switch (prop_id) {
    case PROP_QOS:
      g_value_set_boolean (value, gst_base_transform_is_qos_enabled (trans));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* not a vmethod of anything, just an internal method */
static gboolean
gst_base_transform_activate (GstBaseTransform * trans, gboolean active)
{
  GstBaseTransformClass *bclass;
  gboolean result = TRUE;

  bclass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  if (active) {
    GstCaps *incaps, *outcaps;

    if (trans->priv->pad_mode == GST_ACTIVATE_NONE && bclass->start)
      result &= bclass->start (trans);

    incaps = gst_pad_get_current_caps (trans->sinkpad);
    outcaps = gst_pad_get_current_caps (trans->srcpad);

    GST_OBJECT_LOCK (trans);
    if (incaps && outcaps)
      trans->have_same_caps =
          gst_caps_is_equal (incaps, outcaps) || trans->passthrough;
    else
      trans->have_same_caps = trans->passthrough;
    GST_DEBUG_OBJECT (trans, "have_same_caps %d", trans->have_same_caps);
    trans->negotiated = FALSE;
    trans->have_segment = FALSE;
    gst_segment_init (&trans->segment, GST_FORMAT_UNDEFINED);
    trans->priv->position_out = GST_CLOCK_TIME_NONE;
    trans->priv->proportion = 1.0;
    trans->priv->earliest_time = -1;
    trans->priv->discont = FALSE;
    trans->priv->processed = 0;
    trans->priv->dropped = 0;
    GST_OBJECT_UNLOCK (trans);

    if (incaps)
      gst_caps_unref (incaps);
    if (outcaps)
      gst_caps_unref (outcaps);
  } else {
    /* We must make sure streaming has finished before resetting things
     * and calling the ::stop vfunc */
    GST_PAD_STREAM_LOCK (trans->sinkpad);
    GST_PAD_STREAM_UNLOCK (trans->sinkpad);

    trans->have_same_caps = FALSE;
    /* We can only reset the passthrough mode if the instance told us to 
       handle it in configure_caps */
    if (bclass->passthrough_on_same_caps) {
      gst_base_transform_set_passthrough (trans, FALSE);
    }
    gst_caps_replace (&trans->cache_caps1, NULL);
    gst_caps_replace (&trans->cache_caps2, NULL);

    if (trans->priv->pad_mode != GST_ACTIVATE_NONE && bclass->stop)
      result &= bclass->stop (trans);
  }

  return result;
}

static gboolean
gst_base_transform_sink_activate_push (GstPad * pad, gboolean active)
{
  gboolean result = TRUE;
  GstBaseTransform *trans;

  trans = GST_BASE_TRANSFORM (gst_pad_get_parent (pad));

  result = gst_base_transform_activate (trans, active);

  if (result)
    trans->priv->pad_mode = active ? GST_ACTIVATE_PUSH : GST_ACTIVATE_NONE;

  gst_object_unref (trans);

  return result;
}

static gboolean
gst_base_transform_src_activate_pull (GstPad * pad, gboolean active)
{
  gboolean result = FALSE;
  GstBaseTransform *trans;

  trans = GST_BASE_TRANSFORM (gst_pad_get_parent (pad));

  result = gst_pad_activate_pull (trans->sinkpad, active);

  if (result)
    result &= gst_base_transform_activate (trans, active);

  if (result)
    trans->priv->pad_mode = active ? GST_ACTIVATE_PULL : GST_ACTIVATE_NONE;

  gst_object_unref (trans);

  return result;
}

/**
 * gst_base_transform_set_passthrough:
 * @trans: the #GstBaseTransform to set
 * @passthrough: boolean indicating passthrough mode.
 *
 * Set passthrough mode for this filter by default. This is mostly
 * useful for filters that do not care about negotiation.
 *
 * Always TRUE for filters which don't implement either a transform
 * or transform_ip method.
 *
 * MT safe.
 */
void
gst_base_transform_set_passthrough (GstBaseTransform * trans,
    gboolean passthrough)
{
  GstBaseTransformClass *bclass;

  g_return_if_fail (GST_IS_BASE_TRANSFORM (trans));

  bclass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  GST_OBJECT_LOCK (trans);
  if (passthrough == FALSE) {
    if (bclass->transform_ip || bclass->transform)
      trans->passthrough = FALSE;
  } else {
    trans->passthrough = TRUE;
  }

  GST_DEBUG_OBJECT (trans, "set passthrough %d", trans->passthrough);
  GST_OBJECT_UNLOCK (trans);
}

/**
 * gst_base_transform_is_passthrough:
 * @trans: the #GstBaseTransform to query
 *
 * See if @trans is configured as a passthrough transform.
 *
 * Returns: TRUE is the transform is configured in passthrough mode.
 *
 * MT safe.
 */
gboolean
gst_base_transform_is_passthrough (GstBaseTransform * trans)
{
  gboolean result;

  g_return_val_if_fail (GST_IS_BASE_TRANSFORM (trans), FALSE);

  GST_OBJECT_LOCK (trans);
  result = trans->passthrough;
  GST_OBJECT_UNLOCK (trans);

  return result;
}

/**
 * gst_base_transform_set_in_place:
 * @trans: the #GstBaseTransform to modify
 * @in_place: Boolean value indicating that we would like to operate
 * on in_place buffers.
 *
 * Determines whether a non-writable buffer will be copied before passing
 * to the transform_ip function.
 * <itemizedlist>
 *   <listitem>Always TRUE if no transform function is implemented.</listitem>
 *   <listitem>Always FALSE if ONLY transform function is implemented.</listitem>
 * </itemizedlist>
 *
 * MT safe.
 */
void
gst_base_transform_set_in_place (GstBaseTransform * trans, gboolean in_place)
{
  GstBaseTransformClass *bclass;

  g_return_if_fail (GST_IS_BASE_TRANSFORM (trans));

  bclass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  GST_OBJECT_LOCK (trans);

  if (in_place) {
    if (bclass->transform_ip) {
      GST_DEBUG_OBJECT (trans, "setting in_place TRUE");
      trans->always_in_place = TRUE;
    }
  } else {
    if (bclass->transform) {
      GST_DEBUG_OBJECT (trans, "setting in_place FALSE");
      trans->always_in_place = FALSE;
    }
  }

  GST_OBJECT_UNLOCK (trans);
}

/**
 * gst_base_transform_is_in_place:
 * @trans: the #GstBaseTransform to query
 *
 * See if @trans is configured as a in_place transform.
 *
 * Returns: TRUE is the transform is configured in in_place mode.
 *
 * MT safe.
 */
gboolean
gst_base_transform_is_in_place (GstBaseTransform * trans)
{
  gboolean result;

  g_return_val_if_fail (GST_IS_BASE_TRANSFORM (trans), FALSE);

  GST_OBJECT_LOCK (trans);
  result = trans->always_in_place;
  GST_OBJECT_UNLOCK (trans);

  return result;
}

/**
 * gst_base_transform_update_qos:
 * @trans: a #GstBaseTransform
 * @proportion: the proportion
 * @diff: the diff against the clock
 * @timestamp: the timestamp of the buffer generating the QoS expressed in
 * running_time.
 *
 * Set the QoS parameters in the transform. This function is called internally
 * when a QOS event is received but subclasses can provide custom information
 * when needed.
 *
 * MT safe.
 *
 * Since: 0.10.5
 */
void
gst_base_transform_update_qos (GstBaseTransform * trans,
    gdouble proportion, GstClockTimeDiff diff, GstClockTime timestamp)
{

  g_return_if_fail (GST_IS_BASE_TRANSFORM (trans));

  GST_CAT_DEBUG_OBJECT (GST_CAT_QOS, trans,
      "qos: proportion: %lf, diff %" G_GINT64_FORMAT ", timestamp %"
      GST_TIME_FORMAT, proportion, diff, GST_TIME_ARGS (timestamp));

  GST_OBJECT_LOCK (trans);
  trans->priv->proportion = proportion;
  trans->priv->earliest_time = timestamp + diff;
  GST_OBJECT_UNLOCK (trans);
}

/**
 * gst_base_transform_set_qos_enabled:
 * @trans: a #GstBaseTransform
 * @enabled: new state
 *
 * Enable or disable QoS handling in the transform.
 *
 * MT safe.
 *
 * Since: 0.10.5
 */
void
gst_base_transform_set_qos_enabled (GstBaseTransform * trans, gboolean enabled)
{
  g_return_if_fail (GST_IS_BASE_TRANSFORM (trans));

  GST_CAT_DEBUG_OBJECT (GST_CAT_QOS, trans, "enabled: %d", enabled);

  GST_OBJECT_LOCK (trans);
  trans->priv->qos_enabled = enabled;
  GST_OBJECT_UNLOCK (trans);
}

/**
 * gst_base_transform_is_qos_enabled:
 * @trans: a #GstBaseTransform
 *
 * Queries if the transform will handle QoS.
 *
 * Returns: TRUE if QoS is enabled.
 *
 * MT safe.
 *
 * Since: 0.10.5
 */
gboolean
gst_base_transform_is_qos_enabled (GstBaseTransform * trans)
{
  gboolean result;

  g_return_val_if_fail (GST_IS_BASE_TRANSFORM (trans), FALSE);

  GST_OBJECT_LOCK (trans);
  result = trans->priv->qos_enabled;
  GST_OBJECT_UNLOCK (trans);

  return result;
}

/**
 * gst_base_transform_set_gap_aware:
 * @trans: a #GstBaseTransform
 * @gap_aware: New state
 *
 * If @gap_aware is %FALSE (the default), output buffers will have the
 * %GST_BUFFER_FLAG_GAP flag unset.
 *
 * If set to %TRUE, the element must handle output buffers with this flag set
 * correctly, i.e. it can assume that the buffer contains neutral data but must
 * unset the flag if the output is no neutral data.
 *
 * MT safe.
 *
 * Since: 0.10.16
 */
void
gst_base_transform_set_gap_aware (GstBaseTransform * trans, gboolean gap_aware)
{
  g_return_if_fail (GST_IS_BASE_TRANSFORM (trans));

  GST_OBJECT_LOCK (trans);
  trans->priv->gap_aware = gap_aware;
  GST_DEBUG_OBJECT (trans, "set gap aware %d", trans->priv->gap_aware);
  GST_OBJECT_UNLOCK (trans);
}

/**
 * gst_base_transform_suggest:
 * @trans: a #GstBaseTransform
 * @caps: (transfer none): caps to suggest
 * @size: buffer size to suggest
 *
 * Instructs @trans to suggest new @caps upstream. A copy of @caps will be
 * taken.
 *
 * Since: 0.10.21
 */
void
gst_base_transform_suggest (GstBaseTransform * trans, GstCaps * caps,
    gsize size)
{
  g_return_if_fail (GST_IS_BASE_TRANSFORM (trans));

  /* push the renegotiate event */
  if (!gst_pad_push_event (GST_BASE_TRANSFORM_SINK_PAD (trans),
          gst_event_new_reconfigure ()))
    GST_DEBUG_OBJECT (trans, "Renegotiate event wasn't handled");
}

/**
 * gst_base_transform_reconfigure:
 * @trans: a #GstBaseTransform
 *
 * Instructs @trans to renegotiate a new downstream transform on the next
 * buffer. This function is typically called after properties on the transform
 * were set that influence the output format.
 *
 * Since: 0.10.21
 */
void
gst_base_transform_reconfigure (GstBaseTransform * trans)
{
  g_return_if_fail (GST_IS_BASE_TRANSFORM (trans));

  GST_OBJECT_LOCK (trans);
  GST_DEBUG_OBJECT (trans, "marking reconfigure");
  trans->priv->reconfigure = TRUE;
  GST_OBJECT_UNLOCK (trans);
}
