/* GStreamer
 * Copyright (c) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (c) 2006 Jürg Billeter <j@bitron.ch>
 * Copyright (c) 2007 Jan Schmidt <thaytan@noraisin.net>
 * Copyright (c) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstswitchsrc.h"

GST_DEBUG_CATEGORY_STATIC (switch_debug);
#define GST_CAT_DEFAULT switch_debug

static void gst_switch_src_dispose (GObject * object);
static GstStateChangeReturn
gst_switch_src_change_state (GstElement * element, GstStateChange transition);

GST_BOILERPLATE (GstSwitchSrc, gst_switch_src, GstBin, GST_TYPE_BIN);

static void
gst_switch_src_base_init (gpointer klass)
{
  GST_DEBUG_CATEGORY_INIT (switch_debug, "switchsrc", 0, "switchsrc element");
}

static void
gst_switch_src_class_init (GstSwitchSrcClass * klass)
{
  GObjectClass *oklass = G_OBJECT_CLASS (klass);
  GstElementClass *eklass = GST_ELEMENT_CLASS (klass);
  static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
      GST_PAD_SRC,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS_ANY);
  GstPadTemplate *child_pad_templ;

  oklass->dispose = gst_switch_src_dispose;
  eklass->change_state = gst_switch_src_change_state;

  /* Provide a default pad template if the child didn't */
  child_pad_templ = gst_element_class_get_pad_template (eklass, "src");
  if (child_pad_templ == NULL) {
    gst_element_class_add_pad_template (eklass,
        gst_static_pad_template_get (&src_template));
  }
}

static gboolean
gst_switch_src_reset (GstSwitchSrc * src)
{
  /* this will install fakesrc if no other child has been set,
   * otherwise we rely on the subclass to know when to unset its
   * custom kid */
  if (src->kid == NULL) {
    return gst_switch_src_set_child (src, NULL);
  }

  return TRUE;
}

static void
gst_switch_src_init (GstSwitchSrc * src, GstSwitchSrcClass * g_class)
{
  GstElementClass *eklass = GST_ELEMENT_GET_CLASS (src);
  GstPadTemplate *templ;

  templ = gst_element_class_get_pad_template (eklass, "src");
  src->pad = gst_ghost_pad_new_no_target_from_template ("src", templ);
  gst_element_add_pad (GST_ELEMENT (src), src->pad);

  gst_switch_src_reset (src);

  GST_OBJECT_FLAG_SET (src, GST_ELEMENT_IS_SOURCE);
}

static void
gst_switch_src_dispose (GObject * object)
{
  GstSwitchSrc *src = GST_SWITCH_SRC (object);
  GstObject *new_kid, *kid;

  GST_OBJECT_LOCK (src);
  new_kid = GST_OBJECT_CAST (src->new_kid);
  src->new_kid = NULL;

  kid = GST_OBJECT_CAST (src->kid);
  src->kid = NULL;
  GST_OBJECT_UNLOCK (src);

  gst_object_replace (&new_kid, NULL);
  gst_object_replace (&kid, NULL);

  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

static gboolean
gst_switch_src_commit_new_kid (GstSwitchSrc * src)
{
  GstPad *targetpad;
  GstState kid_state;
  GstElement *new_kid, *old_kid;
  gboolean is_fakesrc = FALSE;
  GstBus *bus;

  /* need locking around member accesses */
  GST_OBJECT_LOCK (src);
  /* If we're currently changing state, set the child to the next state
   * we're transitioning too, rather than our current state which is 
   * about to change */
  if (GST_STATE_NEXT (src) != GST_STATE_VOID_PENDING)
    kid_state = GST_STATE_NEXT (src);
  else
    kid_state = GST_STATE (src);

  new_kid = src->new_kid ? gst_object_ref (src->new_kid) : NULL;
  src->new_kid = NULL;
  GST_OBJECT_UNLOCK (src);

  /* Fakesrc by default if NULL is passed as the new child */
  if (new_kid == NULL) {
    GST_DEBUG_OBJECT (src, "Replacing kid with fakesrc");
    new_kid = gst_element_factory_make ("fakesrc", "testsrc");
    if (new_kid == NULL) {
      GST_ERROR_OBJECT (src, "Failed to create fakesrc");
      return FALSE;
    }
    /* Add a reference, as it would if the element came from src->new_kid */
    gst_object_ref (new_kid);
    is_fakesrc = TRUE;
  } else {
    GST_DEBUG_OBJECT (src, "Setting new kid");
  }

  /* set temporary bus of our own to catch error messages from the child
   * (could we just set our own bus on it, or would the state change messages
   * from the not-yet-added element confuse the state change algorithm? Let's
   * play it safe for now) */
  bus = gst_bus_new ();
  gst_element_set_bus (new_kid, bus);
  gst_object_unref (bus);

  if (gst_element_set_state (new_kid, kid_state) == GST_STATE_CHANGE_FAILURE) {
    GstMessage *msg;

    /* check if child posted an error message and if so re-post it on our bus
     * so that the application gets to see a decent error and not our generic
     * fallback error message which is completely indecipherable to the user */
    msg = gst_bus_pop_filtered (GST_ELEMENT_BUS (new_kid), GST_MESSAGE_ERROR);
    if (msg) {
      GST_INFO_OBJECT (src, "Forwarding kid error: %" GST_PTR_FORMAT, msg);
      gst_element_post_message (GST_ELEMENT (src), msg);
    }
    GST_ELEMENT_ERROR (src, CORE, STATE_CHANGE, (NULL),
        ("Failed to set state on new child."));
    gst_element_set_bus (new_kid, NULL);
    gst_object_unref (new_kid);
    return FALSE;
  }
  gst_element_set_bus (new_kid, NULL);
  gst_bin_add (GST_BIN (src), new_kid);

  /* Now, replace the existing child */
  GST_OBJECT_LOCK (src);
  old_kid = src->kid;
  src->kid = new_kid;
  /* Mark whether a custom kid or fakesrc has been installed */
  src->have_kid = !is_fakesrc;
  GST_OBJECT_UNLOCK (src);

  /* kill old element */
  if (old_kid) {
    GST_DEBUG_OBJECT (src, "Removing old kid %" GST_PTR_FORMAT, old_kid);
    gst_element_set_state (old_kid, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (src), old_kid);
    gst_object_unref (old_kid);
    /* Don't lose the SOURCE flag */
    GST_OBJECT_FLAG_SET (src, GST_ELEMENT_IS_SOURCE);
  }

  /* re-attach ghostpad */
  GST_DEBUG_OBJECT (src, "Creating new ghostpad");
  targetpad = gst_element_get_static_pad (src->kid, "src");
  gst_ghost_pad_set_target (GST_GHOST_PAD (src->pad), targetpad);
  gst_object_unref (targetpad);
  GST_DEBUG_OBJECT (src, "done changing child of switchsrc");

  return TRUE;
}

gboolean
gst_switch_src_set_child (GstSwitchSrc * src, GstElement * new_kid)
{
  GstState cur, next;
  GstElement **p_kid;

  /* Nothing to do if clearing the child and we've already installed fakesrc */
  if (new_kid == NULL && src->kid != NULL && src->have_kid == FALSE)
    return TRUE;

  /* Store the new kid to be committed later */
  GST_OBJECT_LOCK (src);
  cur = GST_STATE (src);
  next = GST_STATE_NEXT (src);
  p_kid = &src->new_kid;
  gst_object_replace ((GstObject **) p_kid, (GstObject *) new_kid);
  GST_OBJECT_UNLOCK (src);
  if (new_kid)
    gst_object_unref (new_kid);

  /* Sometime, it would be lovely to allow src changes even when
   * already running */
  /* FIXME: Block the pad and replace the kid when it completes */
  if (cur > GST_STATE_READY || next == GST_STATE_PAUSED) {
    GST_DEBUG_OBJECT (src,
        "Switch-src is already running. Ignoring change of child.");
    gst_object_unref (new_kid);
    return TRUE;
  }

  return gst_switch_src_commit_new_kid (src);
}

static GstStateChangeReturn
gst_switch_src_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstSwitchSrc *src = GST_SWITCH_SRC (element);

  ret = GST_CALL_PARENT_WITH_DEFAULT (GST_ELEMENT_CLASS, change_state,
      (element, transition), GST_STATE_CHANGE_SUCCESS);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (!gst_switch_src_reset (src))
        ret = GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  return ret;
}
