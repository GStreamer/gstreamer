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
 * @short_description: Base class for simple tranform filters
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
 *     passthrough_on_same_caps on an element that doesn't implement a transform_caps
 *     function is useful for elements that only inspect data (such as level)
 *   </para></listitem>
 *   </itemizedlist>
 *   <itemizedlist>
 *   <title>Example elements</title>
 *     <listitem>Level</listitem>
 *     <listitem>Videoscale, audioconvert, ffmpegcolorspace, audioresample in certain modes.</listitem>
 *   </itemizedlist>
 * </listitem>
 * <listitem>
 *   <itemizedlist><title>Modifications in-place - input buffer and output buffer are the same thing.</title>
 *   <listitem><para>
 *     The element must implement a transform_ip function.
 *   </para></listitem>
 *   <listitem><para>
 *     Output buffer size must <= input buffer size
 *   </para></listitem>
 *   <listitem><para>
 *     If the always_in_place flag is set, non-writable buffers will be copied and
 *     passed to the transform_ip function, otherwise a new buffer will be created
 *     and the transform function called.
 *   </para></listitem>
 *   <listitem><para>
 *     Incoming writable buffers will be passed to the transform_ip function immediately.
 *   </para></listitem>
 *   <listitem><para>
 *     only implementing transform_ip and not transform implies always_in_place =
 *     TRUE
 *   </para></listitem>
 *   </itemizedlist>
 *   <itemizedlist>
 *   <title>Example elements</title>
 *     <listitem>Volume</listitem>
 *     <listitem>Audioconvert in certain modes (signed/unsigned conversion)</listitem>
 *     <listitem>ffmpegcolorspace in certain modes (endianness swapping)</listitem>
 *   </itemizedlist>
 *  </listitem>
 * <listitem>
 *   <itemizedlist><title>Modifications only to the caps/metadata of a buffer</title>
 *   <listitem><para>
 *     The element does not require writable data, but non-writable buffers should
 *     be subbuffered so that the meta-information can be replaced.
 *   </para></listitem>
 *   <listitem><para>
 *     Elements wishing to operate in this mode should replace the
 *     prepare_output_buffer method to create subbuffers of the input buffer and
 *     set always_in_place to TRUE
 *   </para></listitem>
 *   </itemizedlist>
 *   <itemizedlist>
 *   <title>Example elements</title>
 *     <listitem>Capsfilter when setting caps on outgoing buffers that have none.</listitem>
 *     <listitem>identity when it is going to re-timestamp buffers by datarate.</listitem>
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
 *     <listitem>Videoscale, ffmpegcolorspace, audioconvert when doing scaling/conversions</listitem>
 *   </itemizedlist>
 * </listitem>
 * <listitem>
 *   <itemizedlist><title>Special output buffer allocations</title>
 *   <listitem><para>
 *     Elements which need to do special allocation of their output buffers other
 *     than what gst_buffer_pad_alloc allows should implement a
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
 *       Elements which are always in passthrough mode whenever the same caps has
 *       been negotiated on both pads can set the class variable
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

enum
{
  PROP_0,
};

static GstElementClass *parent_class = NULL;

static void gst_base_transform_base_init (gpointer g_class);
static void gst_base_transform_class_init (GstBaseTransformClass * klass);
static void gst_base_transform_init (GstBaseTransform * trans,
    GstBaseTransformClass * klass);
static GstFlowReturn gst_base_transform_prepare_output_buf (GstBaseTransform *
    trans, GstBuffer * input, gint size, GstCaps * caps, GstBuffer ** buf);

GType
gst_base_transform_get_type (void)
{
  static GType base_transform_type = 0;

  if (!base_transform_type) {
    static const GTypeInfo base_transform_info = {
      sizeof (GstBaseTransformClass),
      (GBaseInitFunc) gst_base_transform_base_init,
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
static gboolean gst_base_transform_get_unit_size (GstBaseTransform * trans,
    GstCaps * caps, guint * size);

static GstStateChangeReturn gst_base_transform_change_state (GstElement *
    element, GstStateChange transition);

static gboolean gst_base_transform_event (GstPad * pad, GstEvent * event);
static gboolean gst_base_transform_eventfunc (GstBaseTransform * trans,
    GstEvent * event);
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
gst_base_transform_base_init (gpointer g_class)
{
  GST_DEBUG_CATEGORY_INIT (gst_base_transform_debug, "basetransform", 0,
      "basetransform element");
}

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
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_base_transform_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_base_transform_get_property);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_base_transform_finalize);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_base_transform_change_state);

  klass->passthrough_on_same_caps = FALSE;
  klass->event = GST_DEBUG_FUNCPTR (gst_base_transform_eventfunc);
}

static void
gst_base_transform_init (GstBaseTransform * trans,
    GstBaseTransformClass * bclass)
{
  GstPadTemplate *pad_template;

  GST_DEBUG ("gst_base_transform_init");

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (bclass), "sink");
  g_return_if_fail (pad_template != NULL);
  trans->sinkpad = gst_pad_new_from_template (pad_template, "sink");
  gst_pad_set_getcaps_function (trans->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_getcaps));
  gst_pad_set_setcaps_function (trans->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_setcaps));
  gst_pad_set_event_function (trans->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_event));
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
  gst_pad_set_getrange_function (trans->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_getrange));
  gst_pad_set_activatepull_function (trans->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_transform_src_activate_pull));
  gst_element_add_pad (GST_ELEMENT (trans), trans->srcpad);

  trans->transform_lock = g_mutex_new ();
  trans->delay_configure = FALSE;
  trans->pending_configure = FALSE;
  trans->cache_caps1 = NULL;
  trans->cache_caps2 = NULL;

  trans->passthrough = FALSE;
  if (bclass->transform == NULL) {
    /* If no transform function, always_in_place is TRUE */
    GST_DEBUG_OBJECT (trans, "setting in_place TRUE");
    trans->always_in_place = TRUE;

    if (bclass->transform_ip == NULL)
      trans->passthrough = TRUE;
  }
}

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

    ret = gst_caps_new_empty ();

    if (gst_caps_is_any (caps)) {
      /* for any caps we still have to call the transform function */
      GST_DEBUG_OBJECT (trans, "from ANY:");
      temp = klass->transform_caps (trans, direction, caps);
      GST_DEBUG_OBJECT (trans, "  to: %" GST_PTR_FORMAT, temp);

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

        gst_caps_append (ret, temp);
      }
    }
  } else {
    /* else use the identity transform */
    ret = gst_caps_ref (caps);
  }

  GST_DEBUG_OBJECT (trans, "to:   %" GST_PTR_FORMAT, ret);

  return ret;
}

static gboolean
gst_base_transform_transform_size (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps,
    guint size, GstCaps * othercaps, guint * othersize)
{
  guint inunitsize, outunitsize, units;
  GstBaseTransformClass *klass;
  gint ret = -1;

  klass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  GST_DEBUG_OBJECT (trans, "asked to transform size %d for caps %"
      GST_PTR_FORMAT " to size for caps %" GST_PTR_FORMAT " in direction %s",
      size, caps, othercaps, direction == GST_PAD_SRC ? "SRC" : "SINK");

  /* if there is a custom transform function, use this */
  if (klass->transform_size) {
    ret = klass->transform_size (trans, direction, caps, size, othercaps,
        othersize);
  } else {
    gboolean got_in_unit_size, got_out_unit_size;

    got_in_unit_size = gst_base_transform_get_unit_size (trans, caps,
        &inunitsize);
    g_return_val_if_fail (got_in_unit_size == TRUE, FALSE);
    GST_DEBUG_OBJECT (trans, "input size %d, input unit size %d", size,
        inunitsize);
    g_return_val_if_fail (inunitsize != 0, FALSE);
    if (size % inunitsize != 0) {
      g_warning ("Size %u is not a multiple of unit size %u", size, inunitsize);
      return FALSE;
    }

    units = size / inunitsize;
    got_out_unit_size = gst_base_transform_get_unit_size (trans, othercaps,
        &outunitsize);
    g_return_val_if_fail (got_out_unit_size == TRUE, FALSE);

    *othersize = units * outunitsize;
    GST_DEBUG_OBJECT (trans, "transformed size to %d", *othersize);
  }

  return ret;
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
    /* then see what we can tranform this to */
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

  GST_DEBUG_OBJECT (trans, "got final caps %" GST_PTR_FORMAT, othercaps);

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

/* Allocate a buffer using gst_pad_alloc_buffer_and_set_caps.
 *
 * This function can trigger a renegotiation on the source pad when the
 * peer alloc_buffer function sets new caps. Since we currently are
 * processing a buffer on the sinkpad when this function is called, we cannot
 * reconfigure the transform with sinkcaps different from those of the current
 * buffer. FIXME, we currently don't check if the pluging can transform to the
 * new srcpad caps using the same sinkcaps, we alloc a proper outbuf buffer
 * ourselves instead.
 */
static GstFlowReturn
gst_base_transform_prepare_output_buf (GstBaseTransform * trans,
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
  }

  /* See if we want to prepare the buffer for in place output */
  if (*out_buf == NULL && GST_BUFFER_SIZE (in_buf) == out_size
      && bclass->transform_ip) {
    if (gst_buffer_is_writable (in_buf)) {
      if (trans->have_same_caps) {
        /* Input buffer is already writable and caps are the same, just ref and return it */
        *out_buf = in_buf;
        gst_buffer_ref (in_buf);
      } else {
        /* Writable buffer, but need to change caps => subbuffer */
        *out_buf = gst_buffer_create_sub (in_buf, 0, GST_BUFFER_SIZE (in_buf));
        gst_caps_replace (&GST_BUFFER_CAPS (*out_buf), out_caps);
      }
      goto done;
    } else {
      /* Make a writable buffer below and copy the data */
      copy_inbuf = TRUE;
    }
  }

  if (*out_buf == NULL) {
    /* Sub-class didn't already provide a buffer for us. Make one */
    ret =
        gst_pad_alloc_buffer_and_set_caps (trans->srcpad,
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
  if (*out_buf != in_buf && GST_MINI_OBJECT_REFCOUNT_VALUE (*out_buf) == 1) {

    if (copy_inbuf && gst_buffer_is_writable (*out_buf))
      memcpy (GST_BUFFER_DATA (*out_buf), GST_BUFFER_DATA (in_buf), out_size);

    gst_buffer_stamp (*out_buf, in_buf);
    GST_BUFFER_FLAGS (*out_buf) |= GST_BUFFER_FLAGS (in_buf) &
        (GST_BUFFER_FLAG_PREROLL | GST_BUFFER_FLAG_IN_CAPS |
        GST_BUFFER_FLAG_DELTA_UNIT);
  }

done:
  if (out_caps)
    gst_caps_unref (out_caps);
  trans->delay_configure = FALSE;

  return ret;
}

static gboolean
gst_base_transform_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    guint * size)
{
  gboolean res = FALSE;
  GstBaseTransformClass *bclass;

  /* see if we have the result cached */
  if (trans->cache_caps1 == caps) {
    *size = trans->cache_caps1_size;
    GST_DEBUG_OBJECT (trans, "get size returned cached 1 %d", *size);
    return TRUE;
  }
  if (trans->cache_caps2 == caps) {
    *size = trans->cache_caps2_size;
    GST_DEBUG_OBJECT (trans, "get size returned cached 2 %d", *size);
    return TRUE;
  }

  bclass = GST_BASE_TRANSFORM_GET_CLASS (trans);
  if (bclass->get_unit_size) {
    res = bclass->get_unit_size (trans, caps, size);
    GST_DEBUG_OBJECT (trans, "caps %" GST_PTR_FORMAT
        ") has unit size %d, result %s", caps, *size, res ? "TRUE" : "FALSE");

    if (res) {
      if (trans->cache_caps1 == NULL) {
        gst_caps_replace (&trans->cache_caps1, caps);
        trans->cache_caps1_size = *size;
      } else if (trans->cache_caps2 == NULL) {
        gst_caps_replace (&trans->cache_caps2, caps);
        trans->cache_caps2_size = *size;
      }
    }
  } else {
    GST_DEBUG ("Sub-class does not implement get_unit_size");
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

  trans = GST_BASE_TRANSFORM (gst_pad_get_parent (pad));

  /* we cannot run this when we are transforming data and as such doing 
   * another negotiation in the transform method. */
  g_mutex_lock (trans->transform_lock);

  *buf = NULL;

  GST_DEBUG_OBJECT (trans, "allocating a buffer of size %d ...", size, offset);
  if (offset == GST_BUFFER_OFFSET_NONE)
    GST_DEBUG_OBJECT (trans, "... and offset NONE");
  else
    GST_DEBUG_OBJECT (trans, "... and offset %" G_GUINT64_FORMAT, offset);

  /* before any buffers are pushed, have_same_caps is TRUE; allocating can trigger
   * a renegotiation and change that to FALSE */
  if (trans->have_same_caps) {
    /* request a buffer with the same caps */
    GST_DEBUG_OBJECT (trans, "requesting buffer with same caps, size %d", size);

    res =
        gst_pad_alloc_buffer_and_set_caps (trans->srcpad, offset, size, caps,
        buf);
  } else {
    /* if we are configured, request a buffer with the src caps */
    GstCaps *srccaps = gst_pad_get_negotiated_caps (trans->srcpad);
    GstCaps *sinkcaps = gst_pad_get_negotiated_caps (trans->sinkpad);

    if (!srccaps)
      goto not_configured;

    if (sinkcaps != NULL) {
      if (sinkcaps != caps || !gst_caps_is_equal (sinkcaps, caps)) {
        gst_caps_unref (sinkcaps);
        gst_caps_unref (srccaps);
        goto not_configured;
      }
      gst_caps_unref (sinkcaps);
    }

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
    GstCaps *sinkcaps = gst_pad_get_negotiated_caps (trans->sinkpad);
    GstCaps *srccaps = gst_pad_get_negotiated_caps (trans->srcpad);

    if (!sinkcaps)
      goto not_configured;

    if (!gst_base_transform_transform_size (trans,
            GST_PAD_DIRECTION (trans->srcpad), srccaps, GST_BUFFER_SIZE (*buf),
            sinkcaps, &new_size)) {
      gst_caps_unref (srccaps);
      gst_caps_unref (sinkcaps);
      goto unknown_size;
    }

    gst_buffer_unref (*buf);

    *buf = gst_buffer_new_and_alloc (new_size);
    gst_buffer_set_caps (*buf, sinkcaps);
    GST_BUFFER_OFFSET (*buf) = offset;
    res = GST_FLOW_OK;

    gst_caps_unref (srccaps);
    gst_caps_unref (sinkcaps);
  }
  g_mutex_unlock (trans->transform_lock);

  gst_object_unref (trans);

  return res;

not_configured:
  {
    /* let the default allocator handle it */
    GST_DEBUG_OBJECT (trans, "not configured");
    if (*buf) {
      gst_buffer_unref (*buf);
      *buf = NULL;
    }
    g_mutex_unlock (trans->transform_lock);
    gst_object_unref (trans);
    return GST_FLOW_OK;
  }
unknown_size:
  {
    /* let the default allocator handle it */
    GST_DEBUG_OBJECT (trans, "unknown size");
    if (*buf) {
      gst_buffer_unref (*buf);
      *buf = NULL;
    }
    g_mutex_unlock (trans->transform_lock);
    gst_object_unref (trans);
    return GST_FLOW_OK;
  }
}

static gboolean
gst_base_transform_event (GstPad * pad, GstEvent * event)
{
  GstBaseTransform *trans;
  GstBaseTransformClass *bclass;
  gboolean ret = TRUE;

  trans = GST_BASE_TRANSFORM (gst_pad_get_parent (pad));
  bclass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  if (bclass->event)
    ret = bclass->event (trans, event);

  /* FIXME, do this in the default event handler so the subclass can do
   * something different. */
  if (ret)
    ret = gst_pad_event_default (pad, event);

  gst_object_unref (trans);

  return ret;
}

static gboolean
gst_base_transform_eventfunc (GstBaseTransform * trans, GstEvent * event)
{
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      break;
    case GST_EVENT_FLUSH_STOP:
      /* we need new segment info after the flush. */
      gst_segment_init (&trans->segment, GST_FORMAT_UNDEFINED);
      break;
    case GST_EVENT_EOS:
      break;
    case GST_EVENT_TAG:
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      GstFormat format;
      gdouble rate;
      gint64 start, stop, time;
      gboolean update;

      gst_event_parse_new_segment (event, &update, &rate, &format, &start,
          &stop, &time);

      gst_segment_set_newsegment (&trans->segment, update, rate, format, start,
          stop, time);

      trans->have_newsegment = TRUE;

      if (format == GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (trans, "received NEW_SEGMENT %" GST_TIME_FORMAT
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

static GstFlowReturn
gst_base_transform_handle_buffer (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer ** outbuf)
{
  GstBaseTransformClass *bclass;
  GstFlowReturn ret = GST_FLOW_OK;
  guint out_size;
  gboolean want_in_place;

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

  if (trans->passthrough) {
    /* In passthrough mode, give transform_ip a look at the
     * buffer, without making it writable, or just push the
     * data through */
    GST_LOG_OBJECT (trans, "element is in passthrough mode");

    if (bclass->transform_ip)
      ret = bclass->transform_ip (trans, inbuf);

    *outbuf = inbuf;

    return ret;
  }

  want_in_place = (bclass->transform_ip != NULL) && trans->always_in_place;
  *outbuf = NULL;

  if (want_in_place) {
    /* If want_in_place is TRUE, we may need to prepare a new output buffer
     * Sub-classes can implement a prepare_output_buffer function as they
     * wish. */
    GST_LOG_OBJECT (trans, "doing inplace transform");

    ret = gst_base_transform_prepare_output_buf (trans, inbuf,
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
    ret = gst_base_transform_prepare_output_buf (trans, inbuf, out_size,
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
    GST_DEBUG_OBJECT (trans, "could not get buffer from pool");
    return ret;
  }
configure_failed:
  {
    gst_buffer_unref (inbuf);
    GST_DEBUG_OBJECT (trans, "could not negotiate");
    return GST_FLOW_NOT_NEGOTIATED;
  }
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
  GstBuffer *outbuf = NULL;

  trans = GST_BASE_TRANSFORM (gst_pad_get_parent (pad));

  /* protect transform method and concurrent buffer alloc */
  g_mutex_lock (trans->transform_lock);
  ret = gst_base_transform_handle_buffer (trans, buffer, &outbuf);
  g_mutex_unlock (trans->transform_lock);

  if (ret == GST_FLOW_OK) {
    ret = gst_pad_push (trans->srcpad, outbuf);
  } else if (outbuf != NULL)
    gst_buffer_unref (outbuf);

  gst_object_unref (trans);

  return ret;
}

static void
gst_base_transform_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBaseTransform *trans;

  trans = GST_BASE_TRANSFORM (object);

  switch (prop_id) {
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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_base_transform_sink_activate_push (GstPad * pad, gboolean active)
{
  gboolean result = TRUE;
  GstBaseTransform *trans;
  GstBaseTransformClass *bclass;

  trans = GST_BASE_TRANSFORM (gst_pad_get_parent (pad));
  bclass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  if (active) {
    if (bclass->start)
      result = bclass->start (trans);
  }
  gst_object_unref (trans);

  return result;
}

static gboolean
gst_base_transform_src_activate_pull (GstPad * pad, gboolean active)
{
  gboolean result = FALSE;
  GstBaseTransform *trans;
  GstBaseTransformClass *bclass;

  trans = GST_BASE_TRANSFORM (gst_pad_get_parent (pad));
  bclass = GST_BASE_TRANSFORM_GET_CLASS (trans);

  result = gst_pad_activate_pull (trans->sinkpad, active);

  if (active) {
    if (result && bclass->start)
      result &= bclass->start (trans);
  }
  gst_object_unref (trans);

  return result;
}

static GstStateChangeReturn
gst_base_transform_change_state (GstElement * element,
    GstStateChange transition)
{
  GstBaseTransform *trans;
  GstBaseTransformClass *bclass;
  GstStateChangeReturn result;

  trans = GST_BASE_TRANSFORM (element);
  bclass = GST_BASE_TRANSFORM_GET_CLASS (trans);


  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
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
      GST_OBJECT_UNLOCK (trans);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_caps_replace (&trans->cache_caps1, NULL);
      gst_caps_replace (&trans->cache_caps2, NULL);
    default:
      break;
  }

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (bclass->stop)
        result = bclass->stop (trans);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

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
