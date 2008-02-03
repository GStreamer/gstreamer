/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *                    2005 Andy Wingo <wingo@fluendo.com>
 *                    2005 Thomas Vander Stichele <thomas at apestaart dot org>
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
 * Use Cases:
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
 *
 * <itemizedlist><title>Sub-class settable flags on GstBaseTransform</title>
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
 *
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
};

static GstElementClass *parent_class = NULL;

static void gst_base_transform_class_init (GstBaseTransformClass * klass);
static void gst_base_transform_init (GstBaseTransform * trans,
    GstBaseTransformClass * klass);
static GstFlowReturn gst_base_transform_prepare_output_buffer (GstBaseTransform
    * trans, GstBuffer * input, gint size, GstCaps * caps, GstBuffer ** buf);

GType
gst_base_transform_get_type (void)
{
  static GType base_transform_type = 0;

  if (!base_transform_type) {
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

    base_transform_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstBaseTransform", &base_transform_info, G_TYPE_FLAG_ABSTRACT);
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
    GstCaps * caps, guint * size);

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
static GstCaps *gst_base_transform_getcaps (GstPad * pad);
static gboolean gst_base_transform_setcaps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_base_transform_buffer_alloc (GstPad * pad,
    guint64 offset, guint size, GstCaps * caps, GstBuffer ** buf);

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

  g_type_class_add_private (klass, sizeof (GstBaseTransformPrivate));

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_base_transform_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_base_transform_get_property);

  g_object_class_install_property (gobject_class, PROP_QOS,
      g_param_spec_boolean ("qos", "QoS", "Handle Quality-of-Service events",
          DEFAULT_PROP_QOS, G_PARAM_READWRITE));

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_base_transform_finalize);

  klass->passthrough_on_same_caps = FALSE;
  klass->event = GST_DEBUG_FUNCPTR (gst_base_transform_sink_eventfunc);
  klass->src_event = GST_DEBUG_FUNCPTR (gst_base_transform_src_eventfunc);
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
  gst_pad_set_setcaps_function (trans->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_setcaps));
  gst_pad_set_event_function (trans->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_sink_event));
  gst_pad_set_chain_function (trans->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_chain));
  gst_pad_set_activatepush_function (trans->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_sink_activate_push));
  gst_pad_set_bufferalloc_function (trans->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_buffer_alloc));
  gst_element_add_pad (GST_ELEMENT (trans), trans->sinkpad);

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (bclass), "src");
  g_return_if_fail (pad_template != NULL);
  trans->srcpad = gst_pad_new_from_template (pad_template, "src");
  gst_pad_set_getcaps_function (trans->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_getcaps));
  gst_pad_set_setcaps_function (trans->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_setcaps));
  gst_pad_set_event_function (trans->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_src_event));
  gst_pad_set_checkgetrange_function (trans->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_check_get_range));
  gst_pad_set_getrange_function (trans->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_getrange));
  gst_pad_set_activatepull_function (trans->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_src_activate_pull));
  gst_element_add_pad (GST_ELEMENT (trans), trans->srcpad);

  trans->transform_lock = g_mutex_new ();
  trans->delay_configure = FALSE;
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

    if (bclass->transform_ip == NULL)
      trans->passthrough = TRUE;
  }
}

/* given @caps on the src or sink pad (given by @direction)
 * calculate the possible caps on the other pad.
 *
 * Returns new caps, unref after usage.
 */
static GstCaps *
gst_base_transform_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps)
{
  GstCaps *ret;
  GstBaseTransformClass *klass;

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
      temp = klass->transform_caps (trans, direction, caps);
      GST_DEBUG_OBJECT (trans, "  to: %" GST_PTR_FORMAT, temp);

      temp = gst_caps_make_writable (temp);
      gst_caps_append (ret, temp);
    } else {
      /* we send caps with just one structure to the transform
       * function as this is easier for the element */
      for (i = 0; i < gst_caps_get_size (caps); i++) {
        GstCaps *nth;

        nth = gst_caps_copy_nth (caps, i);
        GST_DEBUG_OBJECT (trans, "from[%d]: %" GST_PTR_FORMAT, i, nth);
        temp = klass->transform_caps (trans, direction, nth);
        gst_caps_unref (nth);
        GST_DEBUG_OBJECT (trans, "  to[%d]: %" GST_PTR_FORMAT, i, temp);

        temp = gst_caps_make_writable (temp);
        /* FIXME: here we need to only append those structures, that are not yet
         * in there
         * gst_caps_append (ret, temp);
         */
        gst_caps_merge (ret, temp);
      }
      GST_DEBUG_OBJECT (trans, "merged: (%d)", gst_caps_get_size (ret));
      /* now simplify caps
         gst_caps_do_simplify (ret);
         GST_DEBUG_OBJECT (trans, "simplified: (%d)", gst_caps_get_size (ret));
       */
    }
  } else {
    /* else use the identity transform */
    ret = gst_caps_ref (caps);
  }

  GST_DEBUG_OBJECT (trans, "to: (%d) %" GST_PTR_FORMAT, gst_caps_get_size (ret),
      ret);

  return ret;
}

static gboolean
gst_base_transform_transform_size (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps,
    guint size, GstCaps * othercaps, guint * othersize)
{
  guint inunitsize, outunitsize, units;
  GstBaseTransformClass *klass;
  gboolean ret;

  klass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  GST_DEBUG_OBJECT (trans, "asked to transform size %d for caps %"
      GST_PTR_FORMAT " to size for caps %" GST_PTR_FORMAT " in direction %s",
      size, caps, othercaps, direction == GST_PAD_SRC ? "SRC" : "SINK");

  /* if there is a custom transform function, use this */
  if (klass->transform_size) {
    ret = klass->transform_size (trans, direction, caps, size, othercaps,
        othersize);
  } else {
    if (!gst_base_transform_get_unit_size (trans, caps, &inunitsize))
      goto no_in_size;

    GST_DEBUG_OBJECT (trans, "input size %d, input unit size %d", size,
        inunitsize);

    if (inunitsize == 0 || (size % inunitsize != 0))
      goto no_multiple;

    units = size / inunitsize;
    if (!gst_base_transform_get_unit_size (trans, othercaps, &outunitsize))
      goto no_out_size;

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
    g_warning ("%s: size %u is not a multiple of unit size %u",
        GST_ELEMENT_NAME (trans), size, inunitsize);
    return FALSE;
  }
no_out_size:
  {
    GST_DEBUG_OBJECT (trans, "could not get out_size");
    g_warning ("%s: could not get out_size", GST_ELEMENT_NAME (trans));
    return FALSE;
  }
}

static GstCaps *
gst_base_transform_getcaps (GstPad * pad)
{
  GstBaseTransform *trans;
  GstPad *otherpad;
  GstCaps *caps;

  trans = GST_BASE_TRANSFORM (gst_pad_get_parent (pad));

  otherpad = (pad == trans->srcpad) ? trans->sinkpad : trans->srcpad;

  /* we can do what the peer can */
  caps = gst_pad_peer_get_caps (otherpad);
  if (caps) {
    GstCaps *temp;
    const GstCaps *templ;

    GST_DEBUG_OBJECT (pad, "peer caps  %" GST_PTR_FORMAT, caps);

    /* filtered against our padtemplate */
    templ = gst_pad_get_pad_template_caps (otherpad);
    GST_DEBUG_OBJECT (pad, "our template  %" GST_PTR_FORMAT, templ);
    temp = gst_caps_intersect (caps, templ);
    GST_DEBUG_OBJECT (pad, "intersected %" GST_PTR_FORMAT, temp);
    gst_caps_unref (caps);
    /* then see what we can transform this to */
    caps = gst_base_transform_transform_caps (trans,
        GST_PAD_DIRECTION (otherpad), temp);
    GST_DEBUG_OBJECT (pad, "transformed  %" GST_PTR_FORMAT, caps);
    gst_caps_unref (temp);
    if (caps == NULL)
      goto done;

    /* and filter against the template again */
    templ = gst_pad_get_pad_template_caps (pad);
    GST_DEBUG_OBJECT (pad, "our template  %" GST_PTR_FORMAT, templ);
    temp = gst_caps_intersect (caps, templ);
    GST_DEBUG_OBJECT (pad, "intersected %" GST_PTR_FORMAT, temp);
    gst_caps_unref (caps);
    /* this is what we can do */
    caps = temp;
  } else {
    /* no peer, our padtemplate is enough then */
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  }

done:
  GST_DEBUG_OBJECT (trans, "returning  %" GST_PTR_FORMAT, caps);

  gst_object_unref (trans);

  return caps;
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

  /* clear the cache */
  gst_caps_replace (&trans->cache_caps1, NULL);
  gst_caps_replace (&trans->cache_caps2, NULL);

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

  trans->negotiated = ret;

  return ret;
}

/* called when new caps arrive on the sink or source pad */
static gboolean
gst_base_transform_setcaps (GstPad * pad, GstCaps * caps)
{
  GstBaseTransform *trans;
  GstBaseTransformClass *klass;
  GstPad *otherpad, *otherpeer;
  GstCaps *othercaps = NULL;
  gboolean ret = TRUE;
  gboolean peer_checked = FALSE;

  trans = GST_BASE_TRANSFORM (gst_pad_get_parent (pad));
  klass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  otherpad = (pad == trans->srcpad) ? trans->sinkpad : trans->srcpad;
  otherpeer = gst_pad_get_peer (otherpad);

  /* if we get called recursively, we bail out now to avoid an
   * infinite loop. */
  if (GST_PAD_IS_IN_SETCAPS (otherpad))
    goto done;

  /* caps must be fixed here */
  if (!gst_caps_is_fixed (caps))
    goto unfixed_caps;

  /* see how we can transform the input caps. */
  othercaps = gst_base_transform_transform_caps (trans,
      GST_PAD_DIRECTION (pad), caps);

  /* The caps we can actually output is the intersection of the transformed
   * caps with the pad template for the pad */
  if (othercaps) {
    GstCaps *intersect;
    const GstCaps *templ_caps;

    templ_caps = gst_pad_get_pad_template_caps (otherpad);
    intersect = gst_caps_intersect (othercaps, templ_caps);

    gst_caps_unref (othercaps);
    othercaps = intersect;
  }

  /* check if transform is empty */
  if (!othercaps || gst_caps_is_empty (othercaps))
    goto no_transform;

  /* if the othercaps are not fixed, we need to fixate them, first attempt
   * is by attempting passthrough if the othercaps are a superset of caps. */
  if (!gst_caps_is_fixed (othercaps)) {
    GstCaps *temp;

    GST_DEBUG_OBJECT (trans,
        "transform returned non fixed  %" GST_PTR_FORMAT, othercaps);

    /* see if the target caps are a superset of the source caps, in this
     * case we can try to perform passthrough */
    temp = gst_caps_intersect (othercaps, caps);
    GST_DEBUG_OBJECT (trans, "intersect returned %" GST_PTR_FORMAT, temp);
    if (temp) {
      if (!gst_caps_is_empty (temp) && otherpeer) {
        GST_DEBUG_OBJECT (trans, "try passthrough with %" GST_PTR_FORMAT, caps);
        /* try passthrough. we know it's fixed, because caps is fixed */
        if (gst_pad_accept_caps (otherpeer, caps)) {
          GST_DEBUG_OBJECT (trans, "peer accepted %" GST_PTR_FORMAT, caps);
          /* peer accepted unmodified caps, we free the original non-fixed
           * caps and work with the passthrough caps */
          gst_caps_unref (othercaps);
          othercaps = gst_caps_ref (caps);
          /* mark that we checked othercaps with the peer, this
           * makes sure we don't call accept_caps again with these same
           * caps */
          peer_checked = TRUE;
        } else {
          GST_DEBUG_OBJECT (trans,
              "peer did not accept %" GST_PTR_FORMAT, caps);
        }
      }
      gst_caps_unref (temp);
    }
  }

  /* second attempt at fixation is done by intersecting with
   * the peer caps */
  if (!gst_caps_is_fixed (othercaps) && otherpeer) {
    /* intersect against what the peer can do */
    GstCaps *peercaps;
    GstCaps *intersect;

    GST_DEBUG_OBJECT (trans, "othercaps now %" GST_PTR_FORMAT, othercaps);

    peercaps = gst_pad_get_caps (otherpeer);
    intersect = gst_caps_intersect (peercaps, othercaps);
    gst_caps_unref (peercaps);
    gst_caps_unref (othercaps);
    othercaps = intersect;
    peer_checked = FALSE;

    GST_DEBUG_OBJECT (trans,
        "filtering against peer yields %" GST_PTR_FORMAT, othercaps);
  }

  if (gst_caps_is_empty (othercaps))
    goto no_transform_possible;

  /* third attempt at fixation, call the fixate vmethod and
   * ultimately call the pad fixate function. */
  if (!gst_caps_is_fixed (othercaps)) {
    GstCaps *temp;

    GST_DEBUG_OBJECT (trans,
        "trying to fixate %" GST_PTR_FORMAT " on pad %s:%s",
        othercaps, GST_DEBUG_PAD_NAME (otherpad));

    /* since we have no other way to fixate left, we might as well just take
     * the first of the caps list and fixate that */

    /* FIXME: when fixating using the vmethod, it might make sense to fixate
     * each of the caps; but Wim doesn't see a use case for that yet */
    temp = gst_caps_copy_nth (othercaps, 0);
    gst_caps_unref (othercaps);
    othercaps = temp;
    peer_checked = FALSE;

    if (klass->fixate_caps) {
      GST_DEBUG_OBJECT (trans, "trying to fixate %" GST_PTR_FORMAT
          " using caps %" GST_PTR_FORMAT
          " on pad %s:%s using fixate_caps vmethod", othercaps, caps,
          GST_DEBUG_PAD_NAME (otherpad));
      klass->fixate_caps (trans, GST_PAD_DIRECTION (pad), caps, othercaps);
    }
    /* if still not fixed, no other option but to let the default pad fixate
     * function do its job */
    if (!gst_caps_is_fixed (othercaps)) {
      GST_DEBUG_OBJECT (trans, "trying to fixate %" GST_PTR_FORMAT
          " on pad %s:%s using gst_pad_fixate_caps", othercaps,
          GST_DEBUG_PAD_NAME (otherpad));
      gst_pad_fixate_caps (otherpad, othercaps);
    }
    GST_DEBUG_OBJECT (trans, "after fixating %" GST_PTR_FORMAT, othercaps);
  }

  /* caps should be fixed now, if not we have to fail. */
  if (!gst_caps_is_fixed (othercaps))
    goto could_not_fixate;

  /* and peer should accept, don't check again if we already checked the
   * othercaps against the peer. */
  if (!peer_checked && otherpeer && !gst_pad_accept_caps (otherpeer, othercaps))
    goto peer_no_accept;

  GST_DEBUG_OBJECT (trans, "Input caps were %" GST_PTR_FORMAT
      ", and got final caps %" GST_PTR_FORMAT, caps, othercaps);

  trans->have_same_caps = gst_caps_is_equal (caps, othercaps);
  GST_DEBUG_OBJECT (trans, "have_same_caps: %d", trans->have_same_caps);

  /* see if we have to configure the element now */
  if (!trans->delay_configure) {
    GstCaps *incaps, *outcaps;

    /* make sure in and out caps are correct */
    if (pad == trans->sinkpad) {
      incaps = caps;
      outcaps = othercaps;
    } else {
      incaps = othercaps;
      outcaps = caps;
    }
    /* call configure now */
    if (!(ret = gst_base_transform_configure_caps (trans, incaps, outcaps)))
      goto failed_configure;
  } else {
    /* set pending configure, the configure will happen later with the
     * caps we set on the pads above. */
    trans->pending_configure = TRUE;
  }

  /* we know this will work, we implement the setcaps */
  gst_pad_set_caps (otherpad, othercaps);

  if (pad == trans->srcpad && trans->priv->pad_mode == GST_ACTIVATE_PULL) {
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

  gst_object_unref (trans);

  return ret;

  /* ERRORS */
unfixed_caps:
  {
    GST_DEBUG_OBJECT (trans, "caps are not fixed  %" GST_PTR_FORMAT, caps);
    ret = FALSE;
    goto done;
  }
no_transform:
  {
    GST_DEBUG_OBJECT (trans,
        "transform returned useless  %" GST_PTR_FORMAT, othercaps);
    ret = FALSE;
    goto done;
  }
no_transform_possible:
  {
    GST_DEBUG_OBJECT (trans,
        "transform could not transform %" GST_PTR_FORMAT
        " in anything we support", caps);
    ret = FALSE;
    goto done;
  }
could_not_fixate:
  {
    GST_ERROR_OBJECT (trans, "FAILED to fixate %" GST_PTR_FORMAT, othercaps);
    ret = FALSE;
    goto done;
  }
peer_no_accept:
  {
    GST_DEBUG_OBJECT (trans, "FAILED to get peer of %" GST_PTR_FORMAT
        " to accept %" GST_PTR_FORMAT, otherpad, othercaps);
    ret = FALSE;
    goto done;
  }
failed_configure:
  {
    GST_DEBUG_OBJECT (trans, "FAILED to configure caps %" GST_PTR_FORMAT
        " to accept %" GST_PTR_FORMAT, otherpad, othercaps);
    ret = FALSE;
    goto done;
  }
}

/* Allocate a buffer using gst_pad_alloc_buffer
 *
 * This function does not do renegotiation on the source pad
 *
 * The output buffer is always writable. outbuf can be equal to
 * inbuf, the caller should be prepared for this and perform 
 * appropriate refcounting.
 */
static GstFlowReturn
gst_base_transform_prepare_output_buffer (GstBaseTransform * trans,
    GstBuffer * in_buf, gint out_size, GstCaps * out_caps, GstBuffer ** out_buf)
{
  GstBaseTransformClass *bclass;
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean copy_inbuf = FALSE;

  bclass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  /* we cannot reconfigure the element yet as we are still processing
   * the old buffer. We will therefore delay the reconfiguration of the
   * element until we have processed this last buffer. */
  trans->delay_configure = TRUE;

  /* out_caps is the caps of the src pad gathered through the GST_PAD_CAPS 
     macro. If a set_caps occurs during this function this caps will become
     invalid. We want to keep them during preparation of the output buffer. */
  if (out_caps)
    gst_caps_ref (out_caps);

  /* see if the subclass wants to alloc a buffer */
  if (bclass->prepare_output_buffer) {
    ret =
        bclass->prepare_output_buffer (trans, in_buf, out_size, out_caps,
        out_buf);
    if (ret != GST_FLOW_OK)
      goto done;

    /* decrease refcount again if vmethod returned refcounted in_buf. This
     * is because we need to make sure that the buffer is writable for the
     * in_place transform. The docs of the vmethod say that you should return
     * a reffed inbuf, which is exactly what we don't want :), oh well.. */
    if (in_buf == *out_buf)
      gst_buffer_unref (in_buf);
  }

  /* See if we want to prepare the buffer for in place output */
  if (*out_buf == NULL && GST_BUFFER_SIZE (in_buf) == out_size
      && bclass->transform_ip) {
    if (gst_buffer_is_writable (in_buf)) {
      if (trans->have_same_caps) {
        /* Input buffer is already writable and caps are the same, return input as
         * output buffer. We don't take an additional ref since that would make the
         * output buffer not writable anymore. Caller should be prepared to deal
         * with proper refcounting of input/output buffers. */
        *out_buf = in_buf;
        GST_LOG_OBJECT (trans, "reuse input buffer");
      } else {
        /* Writable buffer, but need to change caps => subbuffer */
        *out_buf = gst_buffer_create_sub (in_buf, 0, GST_BUFFER_SIZE (in_buf));
        gst_caps_replace (&GST_BUFFER_CAPS (*out_buf), out_caps);
        GST_LOG_OBJECT (trans, "created sub-buffer of input buffer");
      }
      /* we are done now */
      goto done;
    } else {
      /* Make a writable buffer below and copy the data */
      copy_inbuf = TRUE;
      GST_LOG_OBJECT (trans, "need to copy input buffer to new output buffer");
    }
  }

  if (*out_buf == NULL) {
    /* Sub-class didn't already provide a buffer for us. Make one */
    ret = gst_pad_alloc_buffer (trans->srcpad,
        GST_BUFFER_OFFSET (in_buf), out_size, out_caps, out_buf);
    if (ret != GST_FLOW_OK || *out_buf == NULL)
      goto done;

    /* allocated buffer could be of different caps than what we requested */
    if (G_UNLIKELY (!gst_caps_is_equal (out_caps, GST_BUFFER_CAPS (*out_buf)))) {
      /* FIXME, it is possible we can reconfigure the transform with new caps at this
       * point but for now we just create a buffer ourselves */
      gst_buffer_unref (*out_buf);
      *out_buf = gst_buffer_new_and_alloc (out_size);
      gst_buffer_set_caps (*out_buf, out_caps);
    }
  }

  /* If the output buffer metadata is modifiable, copy timestamps and
   * buffer flags */
  if (*out_buf != in_buf && gst_buffer_is_metadata_writable (*out_buf)) {

    if (copy_inbuf && gst_buffer_is_writable (*out_buf))
      memcpy (GST_BUFFER_DATA (*out_buf), GST_BUFFER_DATA (in_buf), out_size);

    gst_buffer_copy_metadata (*out_buf, in_buf,
        GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS);

    /* Unset the GAP flag if the element is _not_ GAP aware. Otherwise
     * it might create an output buffer that does not contain neutral data
     * but still has the GAP flag on it! */
    if (!trans->priv->gap_aware)
      GST_BUFFER_FLAG_UNSET (*out_buf, GST_BUFFER_FLAG_GAP);
  }

done:
  if (out_caps)
    gst_caps_unref (out_caps);

  trans->delay_configure = FALSE;

  return ret;
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
    guint * size)
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

/* your upstream peer wants to send you a buffer
 * that buffer has the given offset, size and caps
 * you're requested to allocate a buffer
 */
static GstFlowReturn
gst_base_transform_buffer_alloc (GstPad * pad, guint64 offset, guint size,
    GstCaps * caps, GstBuffer ** buf)
{
  GstBaseTransform *trans;
  GstFlowReturn res;
  guint new_size;
  gboolean issinkcaps = TRUE;

  trans = GST_BASE_TRANSFORM (gst_pad_get_parent (pad));

  /* we cannot run this when we are transforming data and as such doing 
   * another negotiation in the transform method. */
  g_mutex_lock (trans->transform_lock);

  *buf = NULL;

  GST_DEBUG_OBJECT (trans, "allocating a buffer of size %d ...", size);
  if (offset == GST_BUFFER_OFFSET_NONE)
    GST_DEBUG_OBJECT (trans, "... and offset NONE");
  else
    GST_DEBUG_OBJECT (trans, "... and offset %" G_GUINT64_FORMAT, offset);

  /* if have_same_caps was previously set to TRUE we need to double check if it
   * hasn't changed */
  if (trans->have_same_caps) {
    GstCaps *sinkcaps;

    GST_OBJECT_LOCK (trans->sinkpad);
    sinkcaps = GST_PAD_CAPS (trans->sinkpad);
    issinkcaps = sinkcaps && (gst_caps_is_equal (sinkcaps, caps));
    GST_OBJECT_UNLOCK (trans->sinkpad);
  }

  /* before any buffers are pushed, have_same_caps is TRUE; allocating can trigger
   * a renegotiation and change that to FALSE */

  /* bilboed: This seems wrong, from all debug logs, have_same_caps is
   * initialized to FALSE */

  /* checking against trans->have_same_caps is not enough !! It should also
   *  check to see if the requested caps are equal to the sink caps */
  if (trans->have_same_caps && issinkcaps) {
    /* request a buffer with the same caps */
    GST_DEBUG_OBJECT (trans, "requesting buffer with same caps, size %d", size);

    res =
        gst_pad_alloc_buffer_and_set_caps (trans->srcpad, offset, size, caps,
        buf);
  } else {
    /* if we are configured, request a buffer with the src caps */
    GstCaps *srccaps;
    GstCaps *sinkcaps;
    gboolean configured;

    /* take lock, peek if the caps are ok */
    GST_OBJECT_LOCK (trans->sinkpad);
    sinkcaps = GST_PAD_CAPS (trans->sinkpad);
    configured = (sinkcaps == NULL || gst_caps_is_equal (sinkcaps, caps));
    GST_OBJECT_UNLOCK (trans->sinkpad);
    if (!configured)
      goto not_configured;

    /* take lock on srcpad to grab the caps, caps can change when pushing a
     * buffer. */
    GST_OBJECT_LOCK (trans->srcpad);
    if ((srccaps = GST_PAD_CAPS (trans->srcpad)))
      gst_caps_ref (srccaps);
    GST_OBJECT_UNLOCK (trans->srcpad);
    if (!srccaps)
      goto not_configured;

    GST_DEBUG_OBJECT (trans, "calling transform_size");
    if (!gst_base_transform_transform_size (trans,
            GST_PAD_DIRECTION (pad), caps, size, srccaps, &new_size)) {
      gst_caps_unref (srccaps);
      goto unknown_size;
    }

    res =
        gst_pad_alloc_buffer_and_set_caps (trans->srcpad, offset, new_size,
        srccaps, buf);

    gst_caps_unref (srccaps);
  }

  if (res == GST_FLOW_OK && !trans->have_same_caps) {
    /* note that we might have had same caps before, but calling the
       alloc_buffer caused setcaps to switch us out of in_place -- in any case
       the alloc_buffer served to transmit caps information but we can't use the
       buffer. fall through and allocate a buffer corresponding to our sink
       caps, if any */
    GstCaps *sinkcaps;
    GstCaps *srccaps;

    GST_OBJECT_LOCK (trans->sinkpad);
    if ((sinkcaps = GST_PAD_CAPS (trans->sinkpad)))
      gst_caps_ref (sinkcaps);
    GST_OBJECT_UNLOCK (trans->sinkpad);
    if (!sinkcaps)
      goto not_configured;

    GST_OBJECT_LOCK (trans->srcpad);
    if ((srccaps = GST_PAD_CAPS (trans->srcpad)))
      gst_caps_ref (srccaps);
    GST_OBJECT_UNLOCK (trans->srcpad);
    if (!srccaps) {
      gst_caps_unref (sinkcaps);
      goto not_configured;
    }

    if (!gst_base_transform_transform_size (trans,
            GST_PAD_DIRECTION (trans->srcpad), srccaps, GST_BUFFER_SIZE (*buf),
            sinkcaps, &new_size)) {
      gst_caps_unref (srccaps);
      gst_caps_unref (sinkcaps);
      goto unknown_size;
    }
    /* don't need the caps anymore now */
    gst_caps_unref (srccaps);

    gst_buffer_unref (*buf);

    *buf = gst_buffer_new_and_alloc (new_size);
    GST_BUFFER_OFFSET (*buf) = offset;
    /* set caps, gives away the ref */
    GST_BUFFER_CAPS (*buf) = sinkcaps;

    res = GST_FLOW_OK;
  }

done:
  g_mutex_unlock (trans->transform_lock);
  gst_object_unref (trans);

  return res;

not_configured:
  {
    /* let the default allocator handle it... */
    GST_DEBUG_OBJECT (trans, "not configured");
    gst_buffer_replace (buf, NULL);
    if (trans->passthrough) {
      /* ...by calling alloc_buffer without setting caps on the src pad, which
       * will force negotiation in the chain function. */
      res = gst_pad_alloc_buffer (trans->srcpad, offset, size, caps, buf);
    } else {
      /* ...by letting the default handler create a buffer */
      res = GST_FLOW_OK;
    }
    goto done;
  }
unknown_size:
  {
    /* let the default allocator handle it... */
    GST_DEBUG_OBJECT (trans, "unknown size");
    gst_buffer_replace (buf, NULL);
    if (trans->passthrough) {
      /* ...by calling alloc_buffer without setting caps on the src pad, which
       * will force negotiation in the chain function. */
      res = gst_pad_alloc_buffer (trans->srcpad, offset, size, caps, buf);
    } else {
      /* ...by letting the default handler create a buffer */
      res = GST_FLOW_OK;
    }
    goto done;
  }
}

static gboolean
gst_base_transform_sink_event (GstPad * pad, GstEvent * event)
{
  GstBaseTransform *trans;
  GstBaseTransformClass *bclass;
  gboolean ret = TRUE;
  gboolean forward = TRUE;

  trans = GST_BASE_TRANSFORM (gst_pad_get_parent (pad));
  bclass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  if (bclass->event)
    forward = bclass->event (trans, event);

  /* FIXME, do this in the default event handler so the subclass can do
   * something different. */
  if (forward)
    ret = gst_pad_push_event (trans->srcpad, event);

  gst_object_unref (trans);

  return ret;
}

static gboolean
gst_base_transform_sink_eventfunc (GstBaseTransform * trans, GstEvent * event)
{
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      break;
    case GST_EVENT_FLUSH_STOP:
      GST_OBJECT_LOCK (trans);
      /* reset QoS parameters */
      trans->priv->proportion = 1.0;
      trans->priv->earliest_time = -1;
      trans->priv->discont = FALSE;
      GST_OBJECT_UNLOCK (trans);
      /* we need new segment info after the flush. */
      trans->have_newsegment = FALSE;
      gst_segment_init (&trans->segment, GST_FORMAT_UNDEFINED);
      break;
    case GST_EVENT_EOS:
      break;
    case GST_EVENT_TAG:
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      GstFormat format;
      gdouble rate, arate;
      gint64 start, stop, time;
      gboolean update;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);

      trans->have_newsegment = TRUE;

      gst_segment_set_newsegment_full (&trans->segment, update, rate, arate,
          format, start, stop, time);

      if (format == GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (trans, "received TIME NEW_SEGMENT %" GST_TIME_FORMAT
            " -- %" GST_TIME_FORMAT ", time %" GST_TIME_FORMAT
            ", accum %" GST_TIME_FORMAT,
            GST_TIME_ARGS (trans->segment.start),
            GST_TIME_ARGS (trans->segment.stop),
            GST_TIME_ARGS (trans->segment.time),
            GST_TIME_ARGS (trans->segment.accum));
      } else {
        GST_DEBUG_OBJECT (trans, "received NEW_SEGMENT %" G_GINT64_FORMAT
            " -- %" G_GINT64_FORMAT ", time %" G_GINT64_FORMAT
            ", accum %" G_GINT64_FORMAT,
            trans->segment.start, trans->segment.stop,
            trans->segment.time, trans->segment.accum);
      }
      break;
    }
    default:
      break;
  }

  return TRUE;
}

static gboolean
gst_base_transform_src_event (GstPad * pad, GstEvent * event)
{
  GstBaseTransform *trans;
  GstBaseTransformClass *bclass;
  gboolean ret = TRUE;

  trans = GST_BASE_TRANSFORM (gst_pad_get_parent (pad));
  bclass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  if (bclass->src_event)
    ret = bclass->src_event (trans, event);

  gst_object_unref (trans);

  return ret;
}

static gboolean
gst_base_transform_src_eventfunc (GstBaseTransform * trans, GstEvent * event)
{
  gboolean ret;

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

      gst_event_parse_qos (event, &proportion, &diff, &timestamp);
      gst_base_transform_update_qos (trans, proportion, diff, timestamp);
      break;
    }
    default:
      break;
  }

  ret = gst_pad_push_event (trans->sinkpad, event);

  return ret;
}

static GstFlowReturn
gst_base_transform_handle_buffer (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer ** outbuf)
{
  GstBaseTransformClass *bclass;
  GstFlowReturn ret = GST_FLOW_OK;
  guint out_size;
  gboolean want_in_place;
  GstClockTime qostime;

  bclass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  if (GST_BUFFER_OFFSET_IS_VALID (inbuf))
    GST_LOG_OBJECT (trans, "handling buffer %p of size %d and offset %"
        G_GUINT64_FORMAT, inbuf, GST_BUFFER_SIZE (inbuf),
        GST_BUFFER_OFFSET (inbuf));
  else
    GST_LOG_OBJECT (trans, "handling buffer %p of size %d and offset NONE",
        inbuf, GST_BUFFER_SIZE (inbuf));

  /* Don't allow buffer handling before negotiation, except in passthrough mode
   * or if the class doesn't implement a set_caps function (in which case it doesn't
   * care about caps)
   */
  if (!trans->negotiated && !trans->passthrough && (bclass->set_caps != NULL))
    goto not_negotiated;

  /* Set discont flag so we can mark the outgoing buffer */
  if (GST_BUFFER_IS_DISCONT (inbuf)) {
    GST_LOG_OBJECT (trans, "got DISCONT buffer %p", inbuf);
    trans->priv->discont = TRUE;
  }

  /* can only do QoS if the segment is in TIME */
  if (trans->segment.format != GST_FORMAT_TIME)
    goto no_qos;

  qostime = gst_segment_to_running_time (&trans->segment, GST_FORMAT_TIME,
      GST_BUFFER_TIMESTAMP (inbuf));

  if (qostime != -1) {
    gboolean need_skip;
    GstClockTime earliest_time;

    GST_OBJECT_LOCK (trans);
    earliest_time = trans->priv->earliest_time;
    /* check for QoS, don't perform conversion for buffers
     * that are known to be late. */
    need_skip = trans->priv->qos_enabled &&
        earliest_time != -1 && qostime != -1 && qostime <= earliest_time;
    GST_OBJECT_UNLOCK (trans);

    if (need_skip) {
      GST_CAT_DEBUG_OBJECT (GST_CAT_QOS, trans, "skipping transform: qostime %"
          GST_TIME_FORMAT " <= %" GST_TIME_FORMAT,
          GST_TIME_ARGS (qostime), GST_TIME_ARGS (earliest_time));
      /* mark discont for next buffer */
      trans->priv->discont = TRUE;
      goto skip;
    }
  }

no_qos:
  if (trans->passthrough) {
    /* In passthrough mode, give transform_ip a look at the
     * buffer, without making it writable, or just push the
     * data through */
    GST_LOG_OBJECT (trans, "element is in passthrough mode");

    if (bclass->transform_ip)
      ret = bclass->transform_ip (trans, inbuf);

    *outbuf = inbuf;

    goto done;
  }

  want_in_place = (bclass->transform_ip != NULL) && trans->always_in_place;
  *outbuf = NULL;

  if (want_in_place) {
    /* If want_in_place is TRUE, we may need to prepare a new output buffer
     * Sub-classes can implement a prepare_output_buffer function as they
     * wish. */
    GST_LOG_OBJECT (trans, "doing inplace transform");

    ret = gst_base_transform_prepare_output_buffer (trans, inbuf,
        GST_BUFFER_SIZE (inbuf), GST_PAD_CAPS (trans->srcpad), outbuf);
    if (G_UNLIKELY (ret != GST_FLOW_OK))
      goto no_buffer;

    ret = bclass->transform_ip (trans, *outbuf);

  } else {
    GST_LOG_OBJECT (trans, "doing non-inplace transform");

    /* not transforming inplace, figure out the output size */
    if (trans->always_in_place) {
      out_size = GST_BUFFER_SIZE (inbuf);
    } else {
      if (!gst_base_transform_transform_size (trans,
              GST_PAD_DIRECTION (trans->sinkpad), GST_PAD_CAPS (trans->sinkpad),
              GST_BUFFER_SIZE (inbuf), GST_PAD_CAPS (trans->srcpad),
              &out_size)) {
        /* we have an error */
        goto no_size;
      }
    }

    /* no in place transform, get buffer, this might renegotiate. */
    ret = gst_base_transform_prepare_output_buffer (trans, inbuf, out_size,
        GST_PAD_CAPS (trans->srcpad), outbuf);
    if (ret != GST_FLOW_OK)
      goto no_buffer;

    if (bclass->transform)
      ret = bclass->transform (trans, inbuf, *outbuf);
    else
      ret = GST_FLOW_NOT_SUPPORTED;
  }

  /* if we got renegotiated we can configure now */
  if (trans->pending_configure) {
    gboolean success;

    success =
        gst_base_transform_configure_caps (trans,
        GST_PAD_CAPS (trans->sinkpad), GST_PAD_CAPS (trans->srcpad));

    trans->pending_configure = FALSE;

    if (!success)
      goto configure_failed;
  }

skip:
  /* only unref input buffer if we allocated a new outbuf buffer */
  if (*outbuf != inbuf)
    gst_buffer_unref (inbuf);

done:
  return ret;

  /* ERRORS */
not_negotiated:
  {
    gst_buffer_unref (inbuf);
    GST_ELEMENT_ERROR (trans, STREAM, NOT_IMPLEMENTED,
        ("not negotiated"), ("not negotiated"));
    return GST_FLOW_NOT_NEGOTIATED;
  }
no_size:
  {
    gst_buffer_unref (inbuf);
    GST_ELEMENT_ERROR (trans, STREAM, NOT_IMPLEMENTED,
        ("subclass did not specify output size"),
        ("subclass did not specify output size"));
    return GST_FLOW_ERROR;
  }
no_buffer:
  {
    gst_buffer_unref (inbuf);
    GST_DEBUG_OBJECT (trans, "could not get buffer from pool: %s",
        gst_flow_get_name (ret));
    return ret;
  }
configure_failed:
  {
    if (*outbuf != inbuf)
      gst_buffer_unref (inbuf);
    GST_DEBUG_OBJECT (trans, "could not negotiate");
    return GST_FLOW_NOT_NEGOTIATED;
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
  GstFlowReturn ret;
  GstBuffer *inbuf;

  trans = GST_BASE_TRANSFORM (gst_pad_get_parent (pad));

  ret = gst_pad_pull_range (trans->sinkpad, offset, length, &inbuf);
  if (ret == GST_FLOW_OK) {
    g_mutex_lock (trans->transform_lock);
    ret = gst_base_transform_handle_buffer (trans, inbuf, buffer);
    g_mutex_unlock (trans->transform_lock);
  }

  gst_object_unref (trans);

  return ret;
}

static GstFlowReturn
gst_base_transform_chain (GstPad * pad, GstBuffer * buffer)
{
  GstBaseTransform *trans;
  GstFlowReturn ret;
  GstClockTime last_stop = GST_CLOCK_TIME_NONE;
  GstBuffer *outbuf = NULL;

  trans = GST_BASE_TRANSFORM (GST_OBJECT_PARENT (pad));

  /* calculate end position of the incoming buffer */
  if (GST_BUFFER_TIMESTAMP (buffer) != GST_CLOCK_TIME_NONE) {
    if (GST_BUFFER_DURATION (buffer) != GST_CLOCK_TIME_NONE)
      last_stop = GST_BUFFER_TIMESTAMP (buffer) + GST_BUFFER_DURATION (buffer);
    else
      last_stop = GST_BUFFER_TIMESTAMP (buffer);
  }

  /* protect transform method and concurrent buffer alloc */
  g_mutex_lock (trans->transform_lock);
  ret = gst_base_transform_handle_buffer (trans, buffer, &outbuf);
  g_mutex_unlock (trans->transform_lock);

  /* outbuf can be NULL, this means a dropped buffer, if we have a buffer but
   * GST_BASE_TRANSFORM_FLOW_DROPPED we will not push either. */
  if (outbuf != NULL) {
    if ((ret == GST_FLOW_OK)) {
      /* Remember last stop position */
      if ((last_stop != GST_CLOCK_TIME_NONE) &&
          (trans->segment.format == GST_FORMAT_TIME))
        gst_segment_set_last_stop (&trans->segment, GST_FORMAT_TIME, last_stop);

      /* apply DISCONT flag if the buffer is not yet marked as such */
      if (trans->priv->discont) {
        if (!GST_BUFFER_IS_DISCONT (outbuf)) {
          outbuf = gst_buffer_make_metadata_writable (outbuf);
          GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
        }
        trans->priv->discont = FALSE;
      }
      ret = gst_pad_push (trans->srcpad, outbuf);
    } else
      gst_buffer_unref (outbuf);
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
    if (trans->priv->pad_mode == GST_ACTIVATE_NONE && bclass->start)
      result &= bclass->start (trans);

    GST_OBJECT_LOCK (trans);

    if (GST_PAD_CAPS (trans->sinkpad) && GST_PAD_CAPS (trans->srcpad))
      trans->have_same_caps =
          gst_caps_is_equal (GST_PAD_CAPS (trans->sinkpad),
          GST_PAD_CAPS (trans->srcpad)) || trans->passthrough;
    else
      trans->have_same_caps = trans->passthrough;
    GST_DEBUG_OBJECT (trans, "have_same_caps %d", trans->have_same_caps);
    trans->negotiated = FALSE;
    trans->have_newsegment = FALSE;
    gst_segment_init (&trans->segment, GST_FORMAT_UNDEFINED);
    trans->priv->proportion = 1.0;
    trans->priv->earliest_time = -1;
    trans->priv->discont = FALSE;

    GST_OBJECT_UNLOCK (trans);
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

  g_return_if_fail (trans != NULL);

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

  g_return_val_if_fail (trans != NULL, FALSE);

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
 *   <listitem>Always FALSE if ONLY transform_ip function is implemented.</listitem>
 * </itemizedlist>
 *
 * MT safe.
 */
void
gst_base_transform_set_in_place (GstBaseTransform * trans, gboolean in_place)
{
  GstBaseTransformClass *bclass;

  g_return_if_fail (trans != NULL);

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

  g_return_val_if_fail (trans != NULL, FALSE);

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
 * @timestamp: the timestamp of the buffer generating the QoS
 *
 * Set the QoS parameters in the transform.
 *
 * Since: 0.10.5
 *
 * MT safe.
 */
void
gst_base_transform_update_qos (GstBaseTransform * trans,
    gdouble proportion, GstClockTimeDiff diff, GstClockTime timestamp)
{

  g_return_if_fail (trans != NULL);

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
 * Since: 0.10.5
 *
 * MT safe.
 */
void
gst_base_transform_set_qos_enabled (GstBaseTransform * trans, gboolean enabled)
{
  g_return_if_fail (trans != NULL);

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
 * Since: 0.10.5
 *
 * MT safe.
 */
gboolean
gst_base_transform_is_qos_enabled (GstBaseTransform * trans)
{
  gboolean result;

  g_return_val_if_fail (trans != NULL, FALSE);

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
 * If @gap_aware is %FALSE (as it is by default) subclasses will never get
 * output buffers with the %GST_BUFFER_FLAG_GAP flag set.
 *
 * If set to %TRUE elements must handle output buffers with this flag set
 * correctly, i.e. they can assume that the buffer contains neutral data
 * but must unset the flag if the output is no neutral data.
 * Since: 0.10.16
 *
 * MT safe.
 */
void
gst_base_transform_set_gap_aware (GstBaseTransform * trans, gboolean gap_aware)
{
  g_return_if_fail (trans != NULL);

  GST_OBJECT_LOCK (trans);
  trans->priv->gap_aware = gap_aware;
  GST_DEBUG_OBJECT (trans, "set gap aware %d", trans->priv->gap_aware);
  GST_OBJECT_UNLOCK (trans);
}
