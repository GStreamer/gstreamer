/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstpad.c: Pads for linking elements together
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

#include "gst_private.h"

#include "gstpad.h"
#include "gstmarshal.h"
#include "gstutils.h"
#include "gstelement.h"
#include "gstbin.h"
#include "gstscheduler.h"
#include "gstevent.h"
#include "gstinfo.h"
#include "gsterror.h"
#include "gstvalue.h"

#define GST_CAT_DEFAULT GST_CAT_PADS


enum
{
  TEMPL_PAD_CREATED,
  /* FILL ME */
  TEMPL_LAST_SIGNAL
};

static GstObject *padtemplate_parent_class = NULL;
static guint gst_pad_template_signals[TEMPL_LAST_SIGNAL] = { 0 };

GType _gst_pad_type = 0;

/***** Start with the base GstPad class *****/
static void gst_pad_class_init (GstPadClass * klass);
static void gst_pad_init (GstPad * pad);
static void gst_pad_dispose (GObject * object);

static void gst_pad_set_pad_template (GstPad * pad, GstPadTemplate * templ);
static GstCaps *_gst_pad_default_fixate_func (GstPad * pad,
    const GstCaps * caps);

static gboolean gst_pad_link_try (GstPadLink * link);
static void gst_pad_link_free (GstPadLink * link);

#ifndef GST_DISABLE_LOADSAVE
static xmlNodePtr gst_pad_save_thyself (GstObject * object, xmlNodePtr parent);
#endif

static GstObject *pad_parent_class = NULL;

GType
gst_pad_get_type (void)
{
  if (!_gst_pad_type) {
    static const GTypeInfo pad_info = {
      sizeof (GstPadClass), NULL, NULL,
      (GClassInitFunc) gst_pad_class_init, NULL, NULL,
      sizeof (GstPad),
      0,
      (GInstanceInitFunc) gst_pad_init, NULL
    };

    _gst_pad_type = g_type_register_static (GST_TYPE_OBJECT, "GstPad",
        &pad_info, 0);
  }
  return _gst_pad_type;
}

static void
gst_pad_class_init (GstPadClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  pad_parent_class = g_type_class_ref (GST_TYPE_OBJECT);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_pad_dispose);
}

static void
gst_pad_init (GstPad * pad)
{
  /* all structs are initialized to NULL by glib */
}
static void
gst_pad_dispose (GObject * object)
{
  GstPad *pad = GST_PAD (object);

  gst_pad_set_pad_template (pad, NULL);

  G_OBJECT_CLASS (pad_parent_class)->dispose (object);
}



/***** Then do the Real Pad *****/
/* Pad signals and args */
enum
{
  REAL_LINKED,
  REAL_UNLINKED,
  REAL_FIXATE,
  /* FILL ME */
  REAL_LAST_SIGNAL
};

enum
{
  REAL_ARG_0,
  REAL_ARG_CAPS,
  REAL_ARG_ACTIVE
      /* FILL ME */
};

static void gst_real_pad_class_init (GstRealPadClass * klass);
static void gst_real_pad_init (GstRealPad * pad);
static void gst_real_pad_dispose (GObject * object);

static gboolean _gst_real_pad_fixate_accumulator (GSignalInvocationHint * ihint,
    GValue * return_accu, const GValue * handler_return, gpointer dummy);
static void gst_real_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_real_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

GType _gst_real_pad_type = 0;

static GstPad *real_pad_parent_class = NULL;
static guint gst_real_pad_signals[REAL_LAST_SIGNAL] = { 0 };

GType
gst_real_pad_get_type (void)
{
  if (!_gst_real_pad_type) {
    static const GTypeInfo pad_info = {
      sizeof (GstRealPadClass), NULL, NULL,
      (GClassInitFunc) gst_real_pad_class_init, NULL, NULL,
      sizeof (GstRealPad),
      0,
      (GInstanceInitFunc) gst_real_pad_init, NULL
    };

    _gst_real_pad_type = g_type_register_static (GST_TYPE_PAD, "GstRealPad",
        &pad_info, 0);
  }
  return _gst_real_pad_type;
}

static void
gst_real_pad_class_init (GstRealPadClass * klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;

  gobject_class = (GObjectClass *) klass;
  gstobject_class = (GstObjectClass *) klass;

  real_pad_parent_class = g_type_class_ref (GST_TYPE_PAD);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_real_pad_dispose);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_real_pad_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_real_pad_get_property);

  gst_real_pad_signals[REAL_LINKED] =
      g_signal_new ("linked", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstRealPadClass, linked), NULL, NULL,
      gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1, GST_TYPE_PAD);
  gst_real_pad_signals[REAL_UNLINKED] =
      g_signal_new ("unlinked", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstRealPadClass, unlinked), NULL, NULL,
      gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1, GST_TYPE_PAD);
  gst_real_pad_signals[REAL_FIXATE] =
      g_signal_new ("fixate", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstRealPadClass, appfixatefunc),
      _gst_real_pad_fixate_accumulator, NULL,
      gst_marshal_BOXED__BOXED, GST_TYPE_CAPS, 1,
      GST_TYPE_CAPS | G_SIGNAL_TYPE_STATIC_SCOPE);

/*  gtk_object_add_arg_type ("GstRealPad::active", G_TYPE_BOOLEAN, */
/*                           GTK_ARG_READWRITE, REAL_ARG_ACTIVE); */
  g_object_class_install_property (G_OBJECT_CLASS (klass), REAL_ARG_ACTIVE,
      g_param_spec_boolean ("active", "Active", "Whether the pad is active.",
          TRUE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), REAL_ARG_CAPS,
      g_param_spec_boxed ("caps", "Caps", "The capabilities of the pad",
          GST_TYPE_CAPS, G_PARAM_READABLE));

#ifndef GST_DISABLE_LOADSAVE
  gstobject_class->save_thyself = GST_DEBUG_FUNCPTR (gst_pad_save_thyself);
#endif
  gstobject_class->path_string_separator = ".";
}

static gboolean
_gst_real_pad_fixate_accumulator (GSignalInvocationHint * ihint,
    GValue * return_accu, const GValue * handler_return, gpointer dummy)
{
  if (gst_value_get_caps (handler_return)) {
    g_value_copy (handler_return, return_accu);
    /* stop emission if something was returned */
    return FALSE;
  }
  return TRUE;
}

static void
gst_real_pad_init (GstRealPad * pad)
{
  pad->direction = GST_PAD_UNKNOWN;
  pad->peer = NULL;

  pad->chainfunc = NULL;
  pad->getfunc = NULL;

  pad->chainhandler = NULL;
  pad->gethandler = NULL;

  pad->ghostpads = NULL;
  pad->caps = NULL;

  pad->linkfunc = NULL;
  pad->getcapsfunc = NULL;

  pad->eventfunc = gst_pad_event_default;
  pad->convertfunc = gst_pad_convert_default;
  pad->queryfunc = gst_pad_query_default;
  pad->intlinkfunc = gst_pad_get_internal_links_default;

  pad->eventmaskfunc = gst_pad_get_event_masks_default;
  pad->formatsfunc = gst_pad_get_formats_default;
  pad->querytypefunc = gst_pad_get_query_types_default;

  GST_FLAG_SET (pad, GST_PAD_DISABLED);
  GST_FLAG_UNSET (pad, GST_PAD_NEGOTIATING);

  gst_probe_dispatcher_init (&pad->probedisp);
}

static void
gst_real_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  g_return_if_fail (GST_IS_PAD (object));

  switch (prop_id) {
    case REAL_ARG_ACTIVE:
      gst_pad_set_active (GST_PAD (object), g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_real_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  g_return_if_fail (GST_IS_PAD (object));

  switch (prop_id) {
    case REAL_ARG_ACTIVE:
      g_value_set_boolean (value, !GST_FLAG_IS_SET (object, GST_PAD_DISABLED));
      break;
    case REAL_ARG_CAPS:
      g_value_set_boxed (value, GST_PAD_CAPS (GST_REAL_PAD (object)));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* FIXME-0.9: Replace these custom functions with proper inheritance via _init
   functions and object properties */
/**
 * gst_pad_custom_new:
 * @type: the #Gtype of the pad.
 * @name: the name of the new pad.
 * @direction: the #GstPadDirection of the pad.
 *
 * Creates a new pad with the given name and type in the given direction.
 * If name is NULL, a guaranteed unique name (across all pads) 
 * will be assigned.
 *
 * Returns: a new #GstPad, or NULL in case of an error.
 */
GstPad *
gst_pad_custom_new (GType type, const gchar * name, GstPadDirection direction)
{
  GstRealPad *pad;

  g_return_val_if_fail (direction != GST_PAD_UNKNOWN, NULL);

  pad = g_object_new (type, NULL);
  gst_object_set_name (GST_OBJECT (pad), name);
  GST_RPAD_DIRECTION (pad) = direction;

  return GST_PAD (pad);
}

/**
 * gst_pad_new:
 * @name: the name of the new pad.
 * @direction: the #GstPadDirection of the pad.
 *
 * Creates a new real pad with the given name in the given direction.
 * If name is NULL, a guaranteed unique name (across all pads) 
 * will be assigned.
 *
 * Returns: a new #GstPad, or NULL in case of an error.
 */
GstPad *
gst_pad_new (const gchar * name, GstPadDirection direction)
{
  return gst_pad_custom_new (gst_real_pad_get_type (), name, direction);
}

/**
 * gst_pad_custom_new_from_template:
 * @type: the custom #GType of the pad.
 * @templ: the #GstPadTemplate to instantiate from.
 * @name: the name of the new pad.
 *
 * Creates a new custom pad with the given name from the given template.
 * If name is NULL, a guaranteed unique name (across all pads) 
 * will be assigned.
 *
 * Returns: a new #GstPad, or NULL in case of an error.
 */
GstPad *
gst_pad_custom_new_from_template (GType type, GstPadTemplate * templ,
    const gchar * name)
{
  GstPad *pad;

  g_return_val_if_fail (GST_IS_PAD_TEMPLATE (templ), NULL);

  pad = gst_pad_custom_new (type, name, templ->direction);
  gst_pad_set_pad_template (pad, templ);

  return pad;
}

/**
 * gst_pad_new_from_template:
 * @templ: the pad template to use
 * @name: the name of the element
 *
 * Creates a new real pad with the given name from the given template.
 * If name is NULL, a guaranteed unique name (across all pads) 
 * will be assigned.
 *
 * Returns: a new #GstPad, or NULL in case of an error.
 */
GstPad *
gst_pad_new_from_template (GstPadTemplate * templ, const gchar * name)
{
  return gst_pad_custom_new_from_template (gst_real_pad_get_type (),
      templ, name);
}

/* FIXME 0.9: GST_PAD_UNKNOWN needs to die! */
/**
 * gst_pad_get_direction:
 * @pad: a #GstPad to get the direction of.
 *
 * Gets the direction of the pad.
 *
 * Returns: the #GstPadDirection of the pad.
 */
GstPadDirection
gst_pad_get_direction (GstPad * pad)
{
  g_return_val_if_fail (GST_IS_PAD (pad), GST_PAD_UNKNOWN);

  if (GST_IS_REAL_PAD (pad))
    return GST_PAD_DIRECTION (pad);
  else
    return GST_PAD_UNKNOWN;
}

/**
 * gst_pad_set_active:
 * @pad: the #GstPad to activate or deactivate.
 * @active: TRUE to activate the pad.
 *
 * Activates or deactivates the given pad.
 */
void
gst_pad_set_active (GstPad * pad, gboolean active)
{
  GstRealPad *realpad;
  gboolean old;

  g_return_if_fail (GST_IS_PAD (pad));

  old = GST_PAD_IS_ACTIVE (pad);

  if (old == active)
    return;

  realpad = GST_PAD_REALIZE (pad);

  if (active) {
    GST_CAT_DEBUG (GST_CAT_PADS, "activating pad %s:%s",
        GST_DEBUG_PAD_NAME (realpad));
    GST_FLAG_UNSET (realpad, GST_PAD_DISABLED);
  } else {
    GST_CAT_DEBUG (GST_CAT_PADS, "de-activating pad %s:%s",
        GST_DEBUG_PAD_NAME (realpad));
    GST_FLAG_SET (realpad, GST_PAD_DISABLED);
  }

  g_object_notify (G_OBJECT (realpad), "active");
}

/**
 * gst_pad_is_active:
 * @pad: the #GstPad to query
 *
 * Query if a pad is active
 *
 * Returns: TRUE if the pad is active.
 */
gboolean
gst_pad_is_active (GstPad * pad)
{
  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  return !GST_FLAG_IS_SET (pad, GST_PAD_DISABLED);
}

/**
 * gst_pad_set_name:
 * @pad: a #GstPad to set the name of.
 * @name: the name of the pad.
 *
 * Sets the name of a pad.  If name is NULL, then a guaranteed unique
 * name will be assigned.
 */
void
gst_pad_set_name (GstPad * pad, const gchar * name)
{
  g_return_if_fail (GST_IS_PAD (pad));

  gst_object_set_name (GST_OBJECT (pad), name);
}

/* FIXME 0.9: This function must die */
/**
 * gst_pad_get_name:
 * @pad: a #GstPad to get the name of.
 *
 * Gets the name of a pad.
 *
 * Returns: the name of the pad.  This is not a newly allocated pointer
 * so you must not free it.
 */
const gchar *
gst_pad_get_name (GstPad * pad)
{
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  return GST_OBJECT_NAME (pad);
}

/**
 * gst_pad_set_chain_function:
 * @pad: a real sink #GstPad.
 * @chain: the #GstPadChainFunction to set.
 *
 * Sets the given chain function for the pad. The chain function is called to
 * process a #GstData input buffer.
 */
void
gst_pad_set_chain_function (GstPad * pad, GstPadChainFunction chain)
{
  g_return_if_fail (GST_IS_REAL_PAD (pad));
  g_return_if_fail (GST_RPAD_DIRECTION (pad) == GST_PAD_SINK);

  GST_RPAD_CHAINFUNC (pad) = chain;
  GST_CAT_DEBUG (GST_CAT_PADS, "chainfunc for %s:%s set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (chain));
}

/**
 * gst_pad_set_get_function:
 * @pad: a real source #GstPad.
 * @get: the #GstPadGetFunction to set.
 *
 * Sets the given get function for the pad. The get function is called to
 * produce a new #GstData to start the processing pipeline. Get functions cannot
 * return %NULL.
 */
void
gst_pad_set_get_function (GstPad * pad, GstPadGetFunction get)
{
  g_return_if_fail (GST_IS_REAL_PAD (pad));
  g_return_if_fail (GST_RPAD_DIRECTION (pad) == GST_PAD_SRC);

  GST_RPAD_GETFUNC (pad) = get;

  GST_CAT_DEBUG (GST_CAT_PADS, "getfunc for %s:%s  set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (get));
}

/**
 * gst_pad_set_event_function:
 * @pad: a real source #GstPad.
 * @event: the #GstPadEventFunction to set.
 *
 * Sets the given event handler for the pad.
 */
void
gst_pad_set_event_function (GstPad * pad, GstPadEventFunction event)
{
  g_return_if_fail (GST_IS_REAL_PAD (pad));
  g_return_if_fail (GST_RPAD_DIRECTION (pad) == GST_PAD_SRC);

  GST_RPAD_EVENTFUNC (pad) = event;

  GST_CAT_DEBUG (GST_CAT_PADS, "eventfunc for %s:%s  set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (event));
}

/**
 * gst_pad_set_event_mask_function:
 * @pad: a real #GstPad of either direction.
 * @mask_func: the #GstPadEventMaskFunction to set.
 *
 * Sets the given event mask function for the pad.
 */
void
gst_pad_set_event_mask_function (GstPad * pad,
    GstPadEventMaskFunction mask_func)
{
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_EVENTMASKFUNC (pad) = mask_func;

  GST_CAT_DEBUG (GST_CAT_PADS, "eventmaskfunc for %s:%s  set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (mask_func));
}

/**
 * gst_pad_get_event_masks:
 * @pad: a #GstPad.
 *
 * Gets the array of eventmasks from the given pad.
 *
 * Returns: a zero-terminated array of #GstEventMask, or NULL if the pad does
 * not have an event mask function.
 */
const GstEventMask *
gst_pad_get_event_masks (GstPad * pad)
{
  GstRealPad *rpad;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  rpad = GST_PAD_REALIZE (pad);

  g_return_val_if_fail (rpad, FALSE);

  if (GST_RPAD_EVENTMASKFUNC (rpad))
    return GST_RPAD_EVENTMASKFUNC (rpad) (GST_PAD (pad));

  return NULL;
}

static gboolean
gst_pad_get_event_masks_dispatcher (GstPad * pad, const GstEventMask ** data)
{
  *data = gst_pad_get_event_masks (pad);

  return TRUE;
}

/**
 * gst_pad_get_event_masks_default:
 * @pad: a #GstPad.
 *
 * Invokes the default event masks dispatcher on the pad.
 *
 * Returns: a zero-terminated array of #GstEventMask, or NULL if none of the
 * internally-linked pads have an event mask function.
 */
const GstEventMask *
gst_pad_get_event_masks_default (GstPad * pad)
{
  GstEventMask *result = NULL;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  gst_pad_dispatcher (pad, (GstPadDispatcherFunction)
      gst_pad_get_event_masks_dispatcher, &result);

  return result;
}

/**
 * gst_pad_set_convert_function:
 * @pad: a real #GstPad of either direction.
 * @convert: the #GstPadConvertFunction to set.
 *
 * Sets the given convert function for the pad.
 */
void
gst_pad_set_convert_function (GstPad * pad, GstPadConvertFunction convert)
{
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_CONVERTFUNC (pad) = convert;

  GST_CAT_DEBUG (GST_CAT_PADS, "convertfunc for %s:%s  set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (convert));
}

/**
 * gst_pad_set_query_function:
 * @pad: a real #GstPad of either direction.
 * @query: the #GstPadQueryFunction to set.
 *
 * Set the given query function for the pad.
 */
void
gst_pad_set_query_function (GstPad * pad, GstPadQueryFunction query)
{
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_QUERYFUNC (pad) = query;

  GST_CAT_DEBUG (GST_CAT_PADS, "queryfunc for %s:%s  set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (query));
}

/**
 * gst_pad_set_query_type_function:
 * @pad: a real #GstPad of either direction.
 * @type_func: the #GstPadQueryTypeFunction to set.
 *
 * Set the given query type function for the pad.
 */
void
gst_pad_set_query_type_function (GstPad * pad,
    GstPadQueryTypeFunction type_func)
{
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_QUERYTYPEFUNC (pad) = type_func;

  GST_CAT_DEBUG (GST_CAT_PADS, "querytypefunc for %s:%s  set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (type_func));
}

/**
 * gst_pad_get_query_types:
 * @pad: a #GstPad.
 *
 * Get an array of supported queries that can be performed
 * on this pad.
 *
 * Returns: a zero-terminated array of #GstQueryType.
 */
const GstQueryType *
gst_pad_get_query_types (GstPad * pad)
{
  GstRealPad *rpad;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  rpad = GST_PAD_REALIZE (pad);

  g_return_val_if_fail (rpad, NULL);

  if (GST_RPAD_QUERYTYPEFUNC (rpad))
    return GST_RPAD_QUERYTYPEFUNC (rpad) (GST_PAD (pad));

  return NULL;
}

static gboolean
gst_pad_get_query_types_dispatcher (GstPad * pad, const GstQueryType ** data)
{
  *data = gst_pad_get_query_types (pad);

  return TRUE;
}

/**
 * gst_pad_get_query_types_default:
 * @pad: a #GstPad.
 *
 * Invoke the default dispatcher for the query types on
 * the pad.
 *
 * Returns: an zero-terminated array of #GstQueryType, or NULL if none of the
 * internally-linked pads has a query types function.
 */
const GstQueryType *
gst_pad_get_query_types_default (GstPad * pad)
{
  GstQueryType *result = NULL;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  gst_pad_dispatcher (pad, (GstPadDispatcherFunction)
      gst_pad_get_query_types_dispatcher, &result);

  return result;
}

/**
 * gst_pad_set_internal_link_function:
 * @pad: a real #GstPad of either direction.
 * @intlink: the #GstPadIntLinkFunction to set.
 *
 * Sets the given internal link function for the pad.
 */
void
gst_pad_set_internal_link_function (GstPad * pad, GstPadIntLinkFunction intlink)
{
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_INTLINKFUNC (pad) = intlink;
  GST_CAT_DEBUG (GST_CAT_PADS, "internal link for %s:%s  set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (intlink));
}

/**
 * gst_pad_set_formats_function:
 * @pad: a real #GstPad of either direction.
 * @formats: the #GstPadFormatsFunction to set.
 *
 * Sets the given formats function for the pad.
 */
void
gst_pad_set_formats_function (GstPad * pad, GstPadFormatsFunction formats)
{
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_FORMATSFUNC (pad) = formats;
  GST_CAT_DEBUG (GST_CAT_PADS, "formats function for %s:%s  set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (formats));
}

/**
 * gst_pad_set_link_function:
 * @pad: a real #GstPad.
 * @link: the #GstPadLinkFunction to set.
 * 
 * Sets the given link function for the pad. It will be called when the pad is
 * linked or relinked with caps. The caps passed to the link function are
 * guaranteed to be fixed. This means that you can assume that the caps is not
 * ANY or EMPTY, and that there is exactly one structure in the caps, and that
 * all the fields in the structure are fixed.
 * 
 * The return value GST_PAD_LINK_OK should be used when the caps are acceptable,
 * and you've extracted all the necessary information from the caps and set the
 * element's internal state appropriately.
 * 
 * The return value GST_PAD_LINK_REFUSED should be used when the caps are
 * unacceptable for whatever reason.
 * 
 * The return value GST_PAD_LINK_DELAYED should be used when the element is in a
 * state where it can't determine whether the caps are acceptable or not. This
 * is often used if the element needs to open a device or process data before
 * determining acceptable caps.
 * 
 * @link must not call gst_caps_try_set_caps() on the pad that was specified as
 * a parameter, although it may (and often should) call gst_caps_try_set_caps()
 * on other pads.
 */
void
gst_pad_set_link_function (GstPad * pad, GstPadLinkFunction link)
{
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_LINKFUNC (pad) = link;
  GST_CAT_DEBUG (GST_CAT_PADS, "linkfunc for %s:%s set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (link));
}

/**
 * gst_pad_set_unlink_function:
 * @pad: a real #GstPad.
 * @unlink: the #GstPadUnlinkFunction to set.
 *
 * Sets the given unlink function for the pad. It will be called
 * when the pad is unlinked.
 */
void
gst_pad_set_unlink_function (GstPad * pad, GstPadUnlinkFunction unlink)
{
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_UNLINKFUNC (pad) = unlink;
  GST_CAT_DEBUG (GST_CAT_PADS, "unlinkfunc for %s:%s set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (unlink));
}

/**
 * gst_pad_set_fixate_function:
 * @pad: a real #GstPad.
 * @fixate: the #GstPadFixateFunction to set.
 *
 * Sets the given fixate function for the pad. Its job is to narrow down the
 * possible caps for a connection. Fixate functions are called with a const
 * caps, and should return a caps that is a strict subset of the given caps.
 * That is, @fixate should create a caps that is "more fixed" than previously,
 * but it does not have to return fixed caps. If @fixate can't provide more
 * fixed caps, it should return %NULL.
 * 
 * Note that @fixate will only be called after the "fixate" signal is emitted,
 * and only if the caps are still non-fixed.
 */
void
gst_pad_set_fixate_function (GstPad * pad, GstPadFixateFunction fixate)
{
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_FIXATEFUNC (pad) = fixate;
  GST_CAT_DEBUG (GST_CAT_PADS, "fixatefunc for %s:%s set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (fixate));
}

/**
 * gst_pad_set_getcaps_function:
 * @pad: a real #GstPad.
 * @getcaps: the #GstPadGetCapsFunction to set.
 * 
 * Sets the given getcaps function for the pad. @getcaps should return the
 * allowable caps for a pad in the context of the element's state, its link to
 * other elements, and the devices or files it has opened. These caps must be a
 * subset of the pad template caps. In the NULL state with no links, @getcaps
 * should ideally return the same caps as the pad template. In rare
 * circumstances, an object property can affect the caps returned by @getcaps,
 * but this is discouraged.
 *
 * You do not need to call this function if @pad's allowed caps are always the
 * same as the pad template caps.
 *
 * For most filters, the caps returned by @getcaps is directly affected by the
 * allowed caps on other pads. For demuxers and decoders, the caps returned by
 * the srcpad's getcaps function is directly related to the stream data. Again,
 * @getcaps should return the most specific caps it reasonably can, since this
 * helps with autoplugging. However, the returned caps should not depend on the
 * stream type currently negotiated for @pad.
 *
 * Note that the return value from @getcaps is owned by the caller.
 */
void
gst_pad_set_getcaps_function (GstPad * pad, GstPadGetCapsFunction getcaps)
{
  g_return_if_fail (GST_IS_REAL_PAD (pad));

  GST_RPAD_GETCAPSFUNC (pad) = getcaps;
  GST_CAT_DEBUG (GST_CAT_PADS, "getcapsfunc for %s:%s set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (getcaps));
}

/**
 * gst_pad_set_bufferalloc_function:
 * @pad: a real sink #GstPad.
 * @bufalloc: the #GstPadBufferAllocFunction to set.
 *
 * Sets the given bufferalloc function for the pad. Note that the
 * bufferalloc function can only be set on sinkpads.
 */
void
gst_pad_set_bufferalloc_function (GstPad * pad,
    GstPadBufferAllocFunction bufalloc)
{
  g_return_if_fail (GST_IS_REAL_PAD (pad));
  g_return_if_fail (GST_PAD_IS_SINK (pad));

  GST_RPAD_BUFFERALLOCFUNC (pad) = bufalloc;
  GST_CAT_DEBUG (GST_CAT_PADS, "bufferallocfunc for %s:%s set to %s",
      GST_DEBUG_PAD_NAME (pad), GST_DEBUG_FUNCPTR_NAME (bufalloc));
}

/* FIXME 0.9: Do we actually want to allow the case where src and sink are
   switched? */
/**
 * gst_pad_unlink:
 * @srcpad: the source #GstPad to unlink.
 * @sinkpad: the sink #GstPad to unlink.
 *
 * Unlinks the source pad from the sink pad. Will emit the "unlinked" signal on
 * both pads.
 */
void
gst_pad_unlink (GstPad * srcpad, GstPad * sinkpad)
{
  GstRealPad *realsrc, *realsink;
  GstScheduler *src_sched, *sink_sched;

  g_return_if_fail (GST_IS_PAD (srcpad));
  g_return_if_fail (GST_IS_PAD (sinkpad));

  GST_CAT_INFO (GST_CAT_ELEMENT_PADS, "unlinking %s:%s(%p) and %s:%s(%p)",
      GST_DEBUG_PAD_NAME (srcpad), srcpad,
      GST_DEBUG_PAD_NAME (sinkpad), sinkpad);

  realsrc = GST_PAD_REALIZE (srcpad);
  realsink = GST_PAD_REALIZE (sinkpad);

  g_return_if_fail (GST_RPAD_PEER (realsrc) != NULL);
  g_return_if_fail (GST_RPAD_PEER (realsink) == realsrc);

  if ((GST_RPAD_DIRECTION (realsrc) == GST_PAD_SINK) &&
      (GST_RPAD_DIRECTION (realsink) == GST_PAD_SRC)) {
    GstRealPad *temppad;

    temppad = realsrc;
    realsrc = realsink;
    realsink = temppad;
  }
  g_return_if_fail ((GST_RPAD_DIRECTION (realsrc) == GST_PAD_SRC) &&
      (GST_RPAD_DIRECTION (realsink) == GST_PAD_SINK));

  if (GST_RPAD_UNLINKFUNC (realsrc)) {
    GST_RPAD_UNLINKFUNC (realsrc) (GST_PAD (realsrc));
  }
  if (GST_RPAD_UNLINKFUNC (realsink)) {
    GST_RPAD_UNLINKFUNC (realsink) (GST_PAD (realsink));
  }

  /* get the schedulers before we unlink */
  src_sched = gst_pad_get_scheduler (GST_PAD (realsrc));
  sink_sched = gst_pad_get_scheduler (GST_PAD (realsink));

  if (GST_RPAD_LINK (realsrc))
    gst_pad_link_free (GST_RPAD_LINK (realsrc));

  /* first clear peers */
  GST_RPAD_PEER (realsrc) = NULL;
  GST_RPAD_PEER (realsink) = NULL;
  GST_RPAD_LINK (realsrc) = NULL;
  GST_RPAD_LINK (realsink) = NULL;

  /* now tell the scheduler */
  if (src_sched && src_sched == sink_sched) {
    gst_scheduler_pad_unlink (src_sched, GST_PAD (realsrc), GST_PAD (realsink));
  }

  /* hold a reference, as they can go away in the signal handlers */
  gst_object_ref (GST_OBJECT (realsrc));
  gst_object_ref (GST_OBJECT (realsink));

  /* fire off a signal to each of the pads telling them 
   * that they've been unlinked */
  g_signal_emit (G_OBJECT (realsrc), gst_real_pad_signals[REAL_UNLINKED],
      0, realsink);
  g_signal_emit (G_OBJECT (realsink), gst_real_pad_signals[REAL_UNLINKED],
      0, realsrc);

  GST_CAT_INFO (GST_CAT_ELEMENT_PADS, "unlinked %s:%s and %s:%s",
      GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));

  gst_object_unref (GST_OBJECT (realsrc));
  gst_object_unref (GST_OBJECT (realsink));
}

/**
 * gst_pad_is_linked:
 * @pad: pad to check
 *
 * Checks if a @pad is linked to another pad or not.
 *
 * Returns: TRUE if the pad is linked, FALSE otherwise.
 */
gboolean
gst_pad_is_linked (GstPad * pad)
{
  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  return GST_PAD_PEER (pad) != NULL;
}

struct _GstPadLink
{
  GType type;

  gboolean bla;
  gboolean srcnotify;
  gboolean sinknotify;

  GstPad *srcpad;
  GstPad *sinkpad;

  GstCaps *srccaps;
  GstCaps *sinkcaps;
  GstCaps *filtercaps;
  GstCaps *caps;

  GstPadFixateFunction app_fixate;
};

static gboolean
gst_pad_check_schedulers (GstRealPad * realsrc, GstRealPad * realsink)
{
  GstScheduler *src_sched, *sink_sched;
  gint num_decoupled = 0;

  src_sched = gst_pad_get_scheduler (GST_PAD (realsrc));
  sink_sched = gst_pad_get_scheduler (GST_PAD (realsink));

  if (src_sched && sink_sched) {
    if (GST_FLAG_IS_SET (GST_PAD_PARENT (realsrc), GST_ELEMENT_DECOUPLED))
      num_decoupled++;
    if (GST_FLAG_IS_SET (GST_PAD_PARENT (realsink), GST_ELEMENT_DECOUPLED))
      num_decoupled++;

    if (src_sched != sink_sched && num_decoupled != 1) {
      return FALSE;
    }
  }
  return TRUE;
}

#define GST_PAD_LINK_SRC(pad) ((GST_PAD_IS_SRC (pad)) ? (pad) : GST_PAD_PEER (pad))
#define GST_PAD_LINK_SINK(pad) ((GST_PAD_IS_SINK (pad)) ? (pad) : GST_PAD_PEER (pad))

static GstPadLink *
gst_pad_link_new (void)
{
  GstPadLink *link;

  link = g_new0 (GstPadLink, 1);
  link->sinknotify = TRUE;
  link->srcnotify = TRUE;
  return link;
}

static void
gst_pad_link_free (GstPadLink * link)
{
  if (link->srccaps)
    gst_caps_free (link->srccaps);
  if (link->sinkcaps)
    gst_caps_free (link->sinkcaps);
  if (link->filtercaps)
    gst_caps_free (link->filtercaps);
  if (link->caps)
    gst_caps_free (link->caps);
#ifdef USE_POISONING
  memset (link, 0xff, sizeof (*link));
#endif
  g_free (link);
}

static void
gst_pad_link_intersect (GstPadLink * link)
{
  GstCaps *pad_intersection;

  if (link->caps)
    gst_caps_free (link->caps);

  GST_DEBUG ("intersecting link from %s:%s to %s:%s",
      GST_DEBUG_PAD_NAME (link->srcpad), GST_DEBUG_PAD_NAME (link->sinkpad));
  GST_DEBUG ("... srccaps %" GST_PTR_FORMAT, link->srccaps);
  GST_DEBUG ("... sinkcaps %" GST_PTR_FORMAT, link->sinkcaps);
  GST_DEBUG ("... filtercaps %" GST_PTR_FORMAT, link->filtercaps);

  pad_intersection = gst_caps_intersect (link->srccaps, link->sinkcaps);

  if (link->filtercaps) {
    GST_DEBUG ("unfiltered intersection %" GST_PTR_FORMAT, pad_intersection);
    link->caps = gst_caps_intersect (pad_intersection, link->filtercaps);
    gst_caps_free (pad_intersection);
  } else {
    link->caps = pad_intersection;
  }

  GST_DEBUG ("intersection %" GST_PTR_FORMAT, link->caps);
}

static gboolean
gst_pad_link_ready_for_negotiation (GstPadLink * link)
{
  GstElement *parent;

  parent = GST_PAD_PARENT (link->srcpad);
  if (!parent || GST_STATE (parent) < GST_STATE_READY) {
    GST_DEBUG ("parent %s of pad %s:%s is not READY",
        GST_ELEMENT_NAME (parent), GST_DEBUG_PAD_NAME (link->srcpad));
    return FALSE;
  }
  parent = GST_PAD_PARENT (link->sinkpad);
  if (!parent || GST_STATE (parent) < GST_STATE_READY) {
    GST_DEBUG ("parent %s of pad %s:%s is not READY",
        GST_ELEMENT_NAME (parent), GST_DEBUG_PAD_NAME (link->sinkpad));
    return FALSE;
  }

  return TRUE;
}

static void
gst_pad_link_fixate (GstPadLink * link)
{
  GstCaps *caps;
  GstCaps *newcaps;

  caps = link->caps;

  g_return_if_fail (caps != NULL);
  g_return_if_fail (!gst_caps_is_empty (caps));

  GST_DEBUG ("trying to fixate caps %" GST_PTR_FORMAT, caps);

  while (!gst_caps_is_fixed (caps)) {
    int i;

    for (i = 0; i < 5; i++) {
      newcaps = NULL;
      switch (i) {
        case 0:
          g_signal_emit (G_OBJECT (link->srcpad),
              gst_real_pad_signals[REAL_FIXATE], 0, caps, &newcaps);
          GST_DEBUG ("app srcpad signal fixated to %" GST_PTR_FORMAT, newcaps);
          break;
        case 1:
          g_signal_emit (G_OBJECT (link->sinkpad),
              gst_real_pad_signals[REAL_FIXATE], 0, caps, &newcaps);
          GST_DEBUG ("app sinkpad signal fixated to %" GST_PTR_FORMAT, newcaps);
          break;
        case 2:
          if (GST_RPAD_FIXATEFUNC (link->srcpad)) {
            newcaps =
                GST_RPAD_FIXATEFUNC (link->srcpad) (GST_PAD (link->srcpad),
                caps);
            GST_DEBUG ("srcpad %s:%s fixated to %" GST_PTR_FORMAT,
                GST_DEBUG_PAD_NAME (link->srcpad), newcaps);
          } else
            GST_DEBUG ("srcpad %s:%s doesn't have a fixate function",
                GST_DEBUG_PAD_NAME (link->srcpad));

          break;
        case 3:
          if (GST_RPAD_FIXATEFUNC (link->sinkpad)) {
            newcaps =
                GST_RPAD_FIXATEFUNC (link->sinkpad) (GST_PAD (link->sinkpad),
                caps);
            GST_DEBUG ("sinkpad %s:%s fixated to %" GST_PTR_FORMAT,
                GST_DEBUG_PAD_NAME (link->sinkpad), newcaps);
          } else
            GST_DEBUG ("sinkpad %s:%s doesn't have a fixate function",
                GST_DEBUG_PAD_NAME (link->sinkpad));
          break;
        case 4:
          newcaps = _gst_pad_default_fixate_func (GST_PAD (link->srcpad), caps);
          GST_DEBUG ("core fixated to %" GST_PTR_FORMAT, newcaps);
          break;
      }
      if (newcaps) {
        gst_caps_free (caps);
        caps = newcaps;
        break;
      }
    }
  }

  link->caps = caps;
}

static GstPadLinkReturn
gst_pad_link_call_link_functions (GstPadLink * link)
{
  gboolean negotiating;
  GstPadLinkReturn res;

  if (link->srcnotify && GST_RPAD_LINKFUNC (link->srcpad)) {
    GST_DEBUG ("calling link function on pad %s:%s",
        GST_DEBUG_PAD_NAME (link->srcpad));

    negotiating = GST_FLAG_IS_SET (link->srcpad, GST_PAD_NEGOTIATING);

    /* set the NEGOTIATING flag if not already done */
    if (!negotiating)
      GST_FLAG_SET (link->srcpad, GST_PAD_NEGOTIATING);

    /* call the link function */
    res = GST_RPAD_LINKFUNC (link->srcpad) (GST_PAD (link->srcpad), link->caps);

    /* unset again after negotiating only if we set it  */
    if (!negotiating)
      GST_FLAG_UNSET (link->srcpad, GST_PAD_NEGOTIATING);

    GST_DEBUG ("got reply %d from link function on pad %s:%s",
        res, GST_DEBUG_PAD_NAME (link->srcpad));

    if (GST_PAD_LINK_FAILED (res)) {
      GST_CAT_INFO (GST_CAT_CAPS, "pad %s:%s doesn't accept caps",
          GST_DEBUG_PAD_NAME (link->srcpad));
      return res;
    }
  }

  if (link->sinknotify && GST_RPAD_LINKFUNC (link->sinkpad)) {
    GST_DEBUG ("calling link function on pad %s:%s",
        GST_DEBUG_PAD_NAME (link->sinkpad));

    negotiating = GST_FLAG_IS_SET (link->sinkpad, GST_PAD_NEGOTIATING);

    /* set the NEGOTIATING flag if not already done */
    if (!negotiating)
      GST_FLAG_SET (link->sinkpad, GST_PAD_NEGOTIATING);

    /* call the link function */
    res = GST_RPAD_LINKFUNC (link->sinkpad) (GST_PAD (link->sinkpad),
        link->caps);

    /* unset again after negotiating only if we set it  */
    if (!negotiating)
      GST_FLAG_UNSET (link->sinkpad, GST_PAD_NEGOTIATING);

    GST_DEBUG ("got reply %d from link function on pad %s:%s",
        res, GST_DEBUG_PAD_NAME (link->sinkpad));

    if (GST_PAD_LINK_FAILED (res)) {
      GST_CAT_INFO (GST_CAT_CAPS, "pad %s:%s doesn't accept caps",
          GST_DEBUG_PAD_NAME (link->sinkpad));
      return res;
    }
  }

  return GST_PAD_LINK_OK;
}

static GstPadLinkReturn
gst_pad_link_negotiate (GstPadLink * link)
{
  GST_DEBUG ("negotiating link from pad %s:%s to pad %s:%s",
      GST_DEBUG_PAD_NAME (link->srcpad), GST_DEBUG_PAD_NAME (link->sinkpad));

  if (!gst_pad_link_ready_for_negotiation (link)) {
    return GST_PAD_LINK_DELAYED;
  }

  gst_pad_link_intersect (link);
  if (gst_caps_is_empty (link->caps))
    return GST_PAD_LINK_REFUSED;

  gst_pad_link_fixate (link);
  if (gst_caps_is_empty (link->caps))
    return GST_PAD_LINK_REFUSED;

  return gst_pad_link_call_link_functions (link);
}

/**
 * gst_pad_link_try:
 * @link: link to try
 *
 * Tries to (re)link the pads with the given link. The function takes ownership
 * of the supplied link. If the function returns FALSE and an old link existed,
 * that link can be assumed to work unchanged.
 *
 * Returns: TRUE if the link succeeded, FALSE if not.
 */
static gboolean
gst_pad_link_try (GstPadLink * link)
{
  GstPad *srcpad, *sinkpad;
  GstPadLink *oldlink;
  GstPadLinkReturn ret;

  /* we use assertions here, because this function is static */
  g_assert (link);
  srcpad = link->srcpad;
  g_assert (srcpad);
  sinkpad = link->sinkpad;
  g_assert (sinkpad);
  oldlink = GST_RPAD_LINK (srcpad);
  g_assert (oldlink == GST_RPAD_LINK (sinkpad));

  ret = gst_pad_link_negotiate (link);
  if (GST_PAD_LINK_FAILED (ret) && oldlink && oldlink->caps) {
    oldlink->srcnotify = link->srcnotify;
    oldlink->sinknotify = link->sinknotify;
    if (GST_PAD_LINK_FAILED (gst_pad_link_call_link_functions (oldlink))) {
      g_warning ("pads don't accept old caps. We assume they did though");
    }
  }
  if (ret == GST_PAD_LINK_REFUSED) {
    gst_pad_link_free (link);
    return ret;
  }
  if (ret == GST_PAD_LINK_DELAYED) {
    gst_caps_replace (&link->caps, NULL);
  }

  GST_RPAD_PEER (srcpad) = GST_REAL_PAD (link->sinkpad);
  GST_RPAD_PEER (sinkpad) = GST_REAL_PAD (link->srcpad);
  if (oldlink)
    gst_pad_link_free (oldlink);
  GST_RPAD_LINK (srcpad) = link;
  GST_RPAD_LINK (sinkpad) = link;
  if (ret == GST_PAD_LINK_OK) {
    g_object_notify (G_OBJECT (srcpad), "caps");
    g_object_notify (G_OBJECT (sinkpad), "caps");
  }

  return ret;
}

/**
 * gst_pad_renegotiate:
 * @pad: a #GstPad
 *
 * Initiate caps negotiation on @pad. @pad must be linked.
 *
 * If @pad's parent is not at least in #GST_STATE_READY, returns
 * #GST_PAD_LINK_DELAYED.
 *
 * Otherwise caps are retrieved from both @pad and its peer by calling their
 * getcaps functions. They are then intersected, returning #GST_PAD_LINK_FAIL if
 * there is no intersection.
 *
 * The intersection is fixated if necessary, and then the link functions of @pad
 * and its peer are called.
 *
 * Returns: The return value of @pad's link function (see
 * gst_pad_set_link_function()), or #GST_PAD_LINK_OK if there is no link
 * function.
 *
 * The macros GST_PAD_LINK_SUCCESSFUL() and GST_PAD_LINK_FAILED() should be used
 * when you just need success/failure information.
 */
GstPadLinkReturn
gst_pad_renegotiate (GstPad * pad)
{
  GstPadLink *link;

  g_return_val_if_fail (GST_IS_PAD (pad), GST_PAD_LINK_REFUSED);
  g_return_val_if_fail (GST_PAD_LINK_SRC (pad), GST_PAD_LINK_REFUSED);
  g_return_val_if_fail (GST_PAD_LINK_SINK (pad), GST_PAD_LINK_REFUSED);

  link = gst_pad_link_new ();

  link->srcpad = GST_PAD_LINK_SRC (pad);
  link->sinkpad = GST_PAD_LINK_SINK (pad);

  if (!gst_pad_link_ready_for_negotiation (link)) {
    gst_pad_link_free (link);
    return GST_PAD_LINK_DELAYED;
  }

  if (GST_REAL_PAD (pad)->link->filtercaps) {
    link->filtercaps = gst_caps_copy (GST_REAL_PAD (pad)->link->filtercaps);
  }
  link->srccaps = gst_pad_get_caps (link->srcpad);
  link->sinkcaps = gst_pad_get_caps (link->sinkpad);

  return gst_pad_link_try (link);
}

/**
 * gst_pad_try_set_caps:
 * @pad: a #GstPad
 * @caps: #GstCaps to set on @pad
 *
 * Try to set the caps on @pad. @caps must be fixed. If @pad is unlinked,
 * returns #GST_PAD_LINK_OK without doing anything. Otherwise, start caps
 * negotiation on @pad.
 *
 * Returns: The return value of @pad's link function (see
 * gst_pad_set_link_function()), or #GST_PAD_LINK_OK if there is no link
 * function.
 *
 * The macros GST_PAD_LINK_SUCCESSFUL() and GST_PAD_LINK_FAILED() should be used
 * when you just need success/failure information.
 */
GstPadLinkReturn
gst_pad_try_set_caps (GstPad * pad, const GstCaps * caps)
{
  GstPadLink *link;
  GstPadLink *oldlink;
  GstPadLinkReturn ret;

  g_return_val_if_fail (GST_IS_REAL_PAD (pad), GST_PAD_LINK_REFUSED);
  g_return_val_if_fail (!GST_PAD_IS_NEGOTIATING (pad), GST_PAD_LINK_REFUSED);

  /* setting non-fixed caps on a pad is not allowed */
  if (!gst_caps_is_fixed (caps)) {
    GST_CAT_INFO (GST_CAT_CAPS,
        "trying to set unfixed caps on pad %s:%s, not allowed",
        GST_DEBUG_PAD_NAME (pad));
    g_warning ("trying to set non fixed caps on pad %s:%s, not allowed",
        GST_DEBUG_PAD_NAME (pad));

    GST_DEBUG ("unfixed caps %" GST_PTR_FORMAT, caps);
    return GST_PAD_LINK_REFUSED;
  }

  /* we allow setting caps on non-linked pads.  It's ignored */
  if (!GST_PAD_PEER (pad)) {
    return GST_PAD_LINK_OK;
  }

  g_return_val_if_fail (GST_PAD_LINK_SRC (pad), GST_PAD_LINK_REFUSED);
  g_return_val_if_fail (GST_PAD_LINK_SINK (pad), GST_PAD_LINK_REFUSED);

  /* if the desired caps are already there, it's trivially ok */
  if (GST_PAD_CAPS (pad) && gst_caps_is_equal_fixed (caps, GST_PAD_CAPS (pad))) {
    return GST_PAD_LINK_OK;
  }

  link = gst_pad_link_new ();

  link->srcpad = GST_PAD_LINK_SRC (pad);
  link->sinkpad = GST_PAD_LINK_SINK (pad);

  if (!gst_pad_link_ready_for_negotiation (link)) {
    gst_pad_link_free (link);
    return GST_PAD_LINK_DELAYED;
  }

  oldlink = GST_REAL_PAD (pad)->link;
  if (oldlink && oldlink->filtercaps) {
    link->filtercaps = gst_caps_copy (oldlink->filtercaps);
  }
  if (link->srcpad == pad) {
    link->srccaps = gst_caps_copy (caps);
    link->sinkcaps = gst_pad_get_caps (link->sinkpad);
    link->srcnotify = FALSE;
  } else {
    link->srccaps = gst_pad_get_caps (link->srcpad);
    link->sinkcaps = gst_caps_copy (caps);
    link->sinknotify = FALSE;
  }

  ret = gst_pad_link_try (link);

  return ret;
}

/**
 * gst_pad_try_set_caps_nonfixed:
 * @pad: a real #GstPad
 * @caps: #GstCaps to set on @pad
 *
 * Like gst_pad_try_set_caps(), but allows non-fixed caps.
 *
 * Returns: a #GstPadLinkReturn, like gst_pad_try_set_caps().
 */
GstPadLinkReturn
gst_pad_try_set_caps_nonfixed (GstPad * pad, const GstCaps * caps)
{
  GstPadLink *link;
  GstPadLink *oldlink;
  GstPadLinkReturn ret;

  g_return_val_if_fail (GST_IS_REAL_PAD (pad), GST_PAD_LINK_REFUSED);
  g_return_val_if_fail (!GST_PAD_IS_NEGOTIATING (pad), GST_PAD_LINK_REFUSED);

  /* we allow setting caps on non-linked pads.  It's ignored */
  if (!GST_PAD_PEER (pad)) {
    return GST_PAD_LINK_OK;
  }

  g_return_val_if_fail (GST_PAD_LINK_SRC (pad), GST_PAD_LINK_REFUSED);
  g_return_val_if_fail (GST_PAD_LINK_SINK (pad), GST_PAD_LINK_REFUSED);

  /* if the link is already negotiated and the caps are compatible
   * with what we're setting, it's trivially OK. */
  if (GST_PAD_CAPS (pad)) {
    GstCaps *intersection;

    intersection = gst_caps_intersect (caps, GST_PAD_CAPS (pad));
    if (!gst_caps_is_empty (intersection)) {
      gst_caps_free (intersection);
      return GST_PAD_LINK_OK;
    }
    gst_caps_free (intersection);
  }

  link = gst_pad_link_new ();

  link->srcpad = GST_PAD_LINK_SRC (pad);
  link->sinkpad = GST_PAD_LINK_SINK (pad);

  if (!gst_pad_link_ready_for_negotiation (link)) {
    gst_pad_link_free (link);
    return GST_PAD_LINK_DELAYED;
  }

  oldlink = GST_REAL_PAD (pad)->link;
  if (oldlink && oldlink->filtercaps) {
    link->filtercaps = gst_caps_copy (oldlink->filtercaps);
  }
  if (link->srcpad == pad) {
    link->srccaps = gst_caps_copy (caps);
    link->sinkcaps = gst_pad_get_caps (link->sinkpad);
    link->srcnotify = FALSE;
  } else {
    link->srccaps = gst_pad_get_caps (link->srcpad);
    link->sinkcaps = gst_caps_copy (caps);
    link->sinknotify = FALSE;
  }

  ret = gst_pad_link_try (link);

  return ret;
}

/**
 * gst_pad_can_link_filtered:
 * @srcpad: the source #GstPad to link.
 * @sinkpad: the sink #GstPad to link.
 * @filtercaps: the filter #GstCaps.
 *
 * Checks if the source pad and the sink pad can be linked when constrained
 * by the given filter caps. Both @srcpad and @sinkpad must be unlinked.
 *
 * Returns: TRUE if the pads can be linked, FALSE otherwise.
 */
gboolean
gst_pad_can_link_filtered (GstPad * srcpad, GstPad * sinkpad,
    const GstCaps * filtercaps)
{
  GstRealPad *realsrc, *realsink;
  GstPadLink *link;

  /* FIXME This function is gross.  It's almost a direct copy of
   * gst_pad_link_filtered().  Any decent programmer would attempt
   * to merge the two functions, which I will do some day. --ds
   */

  /* generic checks */
  g_return_val_if_fail (srcpad != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PAD (srcpad), FALSE);
  g_return_val_if_fail (sinkpad != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PAD (sinkpad), FALSE);

  GST_CAT_INFO (GST_CAT_PADS, "trying to link %s:%s and %s:%s",
      GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));

  /* now we need to deal with the real/ghost stuff */
  realsrc = GST_PAD_REALIZE (srcpad);
  realsink = GST_PAD_REALIZE (sinkpad);

  if ((GST_PAD (realsrc) != srcpad) || (GST_PAD (realsink) != sinkpad)) {
    GST_CAT_INFO (GST_CAT_PADS, "*actually* linking %s:%s and %s:%s",
        GST_DEBUG_PAD_NAME (realsrc), GST_DEBUG_PAD_NAME (realsink));
  }
  /* FIXME: shouldn't we convert this to g_return_val_if_fail? */
  if (GST_RPAD_PEER (realsrc) != NULL) {
    GST_CAT_INFO (GST_CAT_PADS, "Real source pad %s:%s has a peer, failed",
        GST_DEBUG_PAD_NAME (realsrc));
    return FALSE;
  }
  if (GST_RPAD_PEER (realsink) != NULL) {
    GST_CAT_INFO (GST_CAT_PADS, "Real sink pad %s:%s has a peer, failed",
        GST_DEBUG_PAD_NAME (realsink));
    return FALSE;
  }
  if (GST_PAD_PARENT (realsrc) == NULL) {
    GST_CAT_INFO (GST_CAT_PADS, "Real src pad %s:%s has no parent, failed",
        GST_DEBUG_PAD_NAME (realsrc));
    return FALSE;
  }
  if (GST_PAD_PARENT (realsink) == NULL) {
    GST_CAT_INFO (GST_CAT_PADS, "Real sink pad %s:%s has no parent, failed",
        GST_DEBUG_PAD_NAME (realsrc));
    return FALSE;
  }

  if (!gst_pad_check_schedulers (realsrc, realsink)) {
    g_warning ("linking pads with different scheds requires "
        "exactly one decoupled element (such as queue)");
    return FALSE;
  }

  g_return_val_if_fail (realsrc != NULL, GST_PAD_LINK_REFUSED);
  g_return_val_if_fail (realsink != NULL, GST_PAD_LINK_REFUSED);

  link = gst_pad_link_new ();

  if (GST_RPAD_DIRECTION (realsrc) == GST_PAD_SRC) {
    link->srcpad = GST_PAD (realsrc);
    link->sinkpad = GST_PAD (realsink);
  } else {
    link->srcpad = GST_PAD (realsink);
    link->sinkpad = GST_PAD (realsrc);
  }

  if (GST_RPAD_DIRECTION (link->srcpad) != GST_PAD_SRC) {
    GST_CAT_INFO (GST_CAT_PADS,
        "Real src pad %s:%s is not a source pad, failed",
        GST_DEBUG_PAD_NAME (link->srcpad));
    gst_pad_link_free (link);
    return FALSE;
  }
  if (GST_RPAD_DIRECTION (link->sinkpad) != GST_PAD_SINK) {
    GST_CAT_INFO (GST_CAT_PADS, "Real sink pad %s:%s is not a sink pad, failed",
        GST_DEBUG_PAD_NAME (link->sinkpad));
    gst_pad_link_free (link);
    return FALSE;
  }

  link->srccaps = gst_pad_get_caps (link->srcpad);
  link->sinkcaps = gst_pad_get_caps (link->sinkpad);
  if (filtercaps)
    link->filtercaps = gst_caps_copy (filtercaps);

  gst_pad_link_intersect (link);
  if (gst_caps_is_empty (link->caps)) {
    gst_pad_link_free (link);
    return FALSE;
  }

  gst_pad_link_free (link);
  return TRUE;
}

/**
 * gst_pad_can_link:
 * @srcpad: the source #GstPad to link.
 * @sinkpad: the sink #GstPad to link.
 *
 * Checks if the source pad and the sink pad can be linked.
 *
 * Returns: TRUE if the pads can be linked, FALSE otherwise.
 */
gboolean
gst_pad_can_link (GstPad * srcpad, GstPad * sinkpad)
{
  return gst_pad_can_link_filtered (srcpad, sinkpad, NULL);
}

/**
 * gst_pad_link_filtered:
 * @srcpad: the source #GstPad to link.
 * @sinkpad: the sink #GstPad to link.
 * @filtercaps: the filter #GstCaps.
 *
 * Links the source pad and the sink pad, constrained
 * by the given filter caps.
 *
 * Returns: TRUE if the pads have been linked, FALSE otherwise.
 */
gboolean
gst_pad_link_filtered (GstPad * srcpad, GstPad * sinkpad,
    const GstCaps * filtercaps)
{
  GstRealPad *realsrc, *realsink;
  GstScheduler *src_sched, *sink_sched;
  GstPadLink *link;

  /* generic checks */
  g_return_val_if_fail (srcpad != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PAD (srcpad), FALSE);
  g_return_val_if_fail (sinkpad != NULL, FALSE);
  g_return_val_if_fail (GST_IS_PAD (sinkpad), FALSE);

  GST_CAT_INFO (GST_CAT_PADS, "trying to link %s:%s and %s:%s",
      GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));

  /* now we need to deal with the real/ghost stuff */
  realsrc = GST_PAD_REALIZE (srcpad);
  realsink = GST_PAD_REALIZE (sinkpad);

  if ((GST_PAD (realsrc) != srcpad) || (GST_PAD (realsink) != sinkpad)) {
    GST_CAT_INFO (GST_CAT_PADS, "*actually* linking %s:%s and %s:%s",
        GST_DEBUG_PAD_NAME (realsrc), GST_DEBUG_PAD_NAME (realsink));
  }
  /* FIXME: shouldn't we convert this to g_return_val_if_fail? */
  if (GST_RPAD_PEER (realsrc) != NULL) {
    GST_CAT_INFO (GST_CAT_PADS, "Real source pad %s:%s has a peer, failed",
        GST_DEBUG_PAD_NAME (realsrc));
    return FALSE;
  }
  if (GST_RPAD_PEER (realsink) != NULL) {
    GST_CAT_INFO (GST_CAT_PADS, "Real sink pad %s:%s has a peer, failed",
        GST_DEBUG_PAD_NAME (realsink));
    return FALSE;
  }
  if (GST_PAD_PARENT (realsrc) == NULL) {
    GST_CAT_INFO (GST_CAT_PADS, "Real src pad %s:%s has no parent, failed",
        GST_DEBUG_PAD_NAME (realsrc));
    return FALSE;
  }
  if (GST_PAD_PARENT (realsink) == NULL) {
    GST_CAT_INFO (GST_CAT_PADS, "Real sink pad %s:%s has no parent, failed",
        GST_DEBUG_PAD_NAME (realsrc));
    return FALSE;
  }

  if (!gst_pad_check_schedulers (realsrc, realsink)) {
    g_warning ("linking pads with different scheds requires "
        "exactly one decoupled element (such as queue)");
    return FALSE;
  }

  g_return_val_if_fail (realsrc != NULL, GST_PAD_LINK_REFUSED);
  g_return_val_if_fail (realsink != NULL, GST_PAD_LINK_REFUSED);

  link = gst_pad_link_new ();

  if (GST_RPAD_DIRECTION (realsrc) == GST_PAD_SRC) {
    link->srcpad = GST_PAD (realsrc);
    link->sinkpad = GST_PAD (realsink);
  } else {
    link->srcpad = GST_PAD (realsink);
    link->sinkpad = GST_PAD (realsrc);
  }

  if (GST_RPAD_DIRECTION (link->srcpad) != GST_PAD_SRC) {
    GST_CAT_INFO (GST_CAT_PADS,
        "Real src pad %s:%s is not a source pad, failed",
        GST_DEBUG_PAD_NAME (link->srcpad));
    gst_pad_link_free (link);
    return FALSE;
  }
  if (GST_RPAD_DIRECTION (link->sinkpad) != GST_PAD_SINK) {
    GST_CAT_INFO (GST_CAT_PADS, "Real sink pad %s:%s is not a sink pad, failed",
        GST_DEBUG_PAD_NAME (link->sinkpad));
    gst_pad_link_free (link);
    return FALSE;
  }

  link->srccaps = gst_pad_get_caps (link->srcpad);
  link->sinkcaps = gst_pad_get_caps (link->sinkpad);
  if (filtercaps)
    link->filtercaps = gst_caps_copy (filtercaps);
  if (gst_pad_link_try (link) == GST_PAD_LINK_REFUSED)
    return FALSE;

  /* fire off a signal to each of the pads telling them 
   * that they've been linked */
  g_signal_emit (G_OBJECT (link->srcpad), gst_real_pad_signals[REAL_LINKED],
      0, link->sinkpad);
  g_signal_emit (G_OBJECT (link->sinkpad), gst_real_pad_signals[REAL_LINKED],
      0, link->srcpad);

  src_sched = gst_pad_get_scheduler (GST_PAD (link->srcpad));
  sink_sched = gst_pad_get_scheduler (GST_PAD (link->sinkpad));

  /* now tell the scheduler */
  if (src_sched && src_sched == sink_sched) {
    gst_scheduler_pad_link (src_sched,
        GST_PAD (link->srcpad), GST_PAD (link->sinkpad));
  } else {
    GST_CAT_INFO (GST_CAT_PADS,
        "not telling link to scheduler %s:%s and %s:%s, %p %p",
        GST_DEBUG_PAD_NAME (link->srcpad), GST_DEBUG_PAD_NAME (link->sinkpad),
        src_sched, sink_sched);
  }

  GST_CAT_INFO (GST_CAT_PADS, "linked %s:%s and %s:%s, successful",
      GST_DEBUG_PAD_NAME (link->srcpad), GST_DEBUG_PAD_NAME (link->sinkpad));

  return TRUE;
}

/**
 * gst_pad_link:
 * @srcpad: the source #GstPad to link.
 * @sinkpad: the sink #GstPad to link.
 *
 * Links the source pad to the sink pad.
 *
 * Returns: TRUE if the pad could be linked, FALSE otherwise.
 */
gboolean
gst_pad_link (GstPad * srcpad, GstPad * sinkpad)
{
  return gst_pad_link_filtered (srcpad, sinkpad, NULL);
}

/* FIXME 0.9: Remove this */
/**
 * gst_pad_set_parent:
 * @pad: a #GstPad to set the parent of.
 * @parent: the new parent #GstElement.
 *
 * Sets the parent object of a pad. Deprecated, use gst_object_set_parent()
 * instead.
 */
void
gst_pad_set_parent (GstPad * pad, GstElement * parent)
{
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (GST_PAD_PARENT (pad) == NULL);
  g_return_if_fail (GST_IS_ELEMENT (parent));

  gst_object_set_parent (GST_OBJECT (pad), GST_OBJECT (parent));
}

/* FIXME 0.9: Remove this */
/**
 * gst_pad_get_parent:
 * @pad: the #GstPad to get the parent of.
 *
 * Gets the parent object of this pad. Deprecated, use gst_object_get_parent()
 * instead.
 *
 * Returns: the parent #GstElement.
 */
GstElement *
gst_pad_get_parent (GstPad * pad)
{
  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  return GST_PAD_PARENT (pad);
}

static void
gst_pad_set_pad_template (GstPad * pad, GstPadTemplate * templ)
{
  /* this function would need checks if it weren't static */

  gst_object_replace ((GstObject **) & pad->padtemplate, (GstObject *) templ);

  if (templ) {
    gst_object_sink (GST_OBJECT (templ));
    g_signal_emit (G_OBJECT (templ),
        gst_pad_template_signals[TEMPL_PAD_CREATED], 0, pad);
  }
}

/**
 * gst_pad_get_pad_template:
 * @pad: a #GstPad.
 *
 * Gets the template for @pad.
 *
 * Returns: the #GstPadTemplate from which this pad was instantiated, or %NULL
 * if this pad has no template.
 */
GstPadTemplate *
gst_pad_get_pad_template (GstPad * pad)
{
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  return GST_PAD_PAD_TEMPLATE (pad);
}


/**
 * gst_pad_get_scheduler:
 * @pad: a #GstPad to get the scheduler of.
 *
 * Gets the scheduler of the pad. Since the pad does not
 * have a scheduler of its own, the scheduler of the parent
 * is taken. For decoupled pads, the scheduler of the peer
 * parent is taken.
 *
 * Returns: the #GstScheduler of the pad, or %NULL if there is no parent or the
 * parent is not yet in a managing bin.
 */
GstScheduler *
gst_pad_get_scheduler (GstPad * pad)
{
  GstScheduler *scheduler = NULL;
  GstElement *parent;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  parent = gst_pad_get_parent (pad);
  if (parent) {
    if (GST_FLAG_IS_SET (parent, GST_ELEMENT_DECOUPLED)) {
      GstRealPad *peer = GST_RPAD_PEER (pad);

      if (peer) {
        scheduler =
            gst_element_get_scheduler (gst_pad_get_parent (GST_PAD (peer)));
      }
    } else {
      scheduler = gst_element_get_scheduler (parent);
    }
  }

  return scheduler;
}

/**
 * gst_pad_get_real_parent:
 * @pad: a #GstPad to get the real parent of.
 *
 * Gets the real parent object of this pad. If the pad
 * is a ghost pad, the actual owner of the real pad is
 * returned, as opposed to #gst_pad_get_parent().
 *
 * Returns: the parent #GstElement.
 */
GstElement *
gst_pad_get_real_parent (GstPad * pad)
{
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  return GST_PAD_PARENT (GST_PAD (GST_PAD_REALIZE (pad)));
}

/* FIXME 0.9: Make static. */
/**
 * gst_pad_add_ghost_pad:
 * @pad: a #GstPad to attach the ghost pad to.
 * @ghostpad: the ghost #GstPad to to the pad.
 *
 * Adds a ghost pad to a pad. Private function, will be removed from the API in
 * 0.9.
 */
void
gst_pad_add_ghost_pad (GstPad * pad, GstPad * ghostpad)
{
  GstRealPad *realpad;

  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (GST_IS_GHOST_PAD (ghostpad));

  /* if we're ghosting a ghost pad, drill down to find the real pad */
  realpad = (GstRealPad *) pad;
  while (GST_IS_GHOST_PAD (realpad))
    realpad = GST_GPAD_REALPAD (realpad);
  g_return_if_fail (GST_IS_REAL_PAD (realpad));

  /* will ref the pad template */
  GST_GPAD_REALPAD (ghostpad) = realpad;
  realpad->ghostpads = g_list_prepend (realpad->ghostpads, ghostpad);
  gst_pad_set_pad_template (GST_PAD (ghostpad), GST_PAD_PAD_TEMPLATE (pad));
}

/* FIXME 0.9: Make static. */
/**
 * gst_pad_remove_ghost_pad:
 * @pad: a #GstPad to remove the ghost pad from.
 * @ghostpad: the ghost #GstPad to remove from the pad.
 *
 * Removes a ghost pad from a pad. Private, will be removed from the API in 0.9.
 */
void
gst_pad_remove_ghost_pad (GstPad * pad, GstPad * ghostpad)
{
  GstRealPad *realpad;

  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (GST_IS_GHOST_PAD (ghostpad));
  realpad = GST_PAD_REALIZE (pad);
  g_return_if_fail (GST_GPAD_REALPAD (ghostpad) == realpad);

  gst_pad_set_pad_template (GST_PAD (ghostpad), NULL);
  realpad->ghostpads = g_list_remove (realpad->ghostpads, ghostpad);
  GST_GPAD_REALPAD (ghostpad) = NULL;
}

/**
 * gst_pad_get_ghost_pad_list:
 * @pad: a #GstPad to get the ghost pads of.
 *
 * Gets the ghost pads of this pad.
 *
 * Returns: a #GList of ghost pads.
 */
GList *
gst_pad_get_ghost_pad_list (GstPad * pad)
{
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  return GST_PAD_REALIZE (pad)->ghostpads;
}

static gboolean
_gst_pad_default_fixate_foreach (GQuark field_id, GValue * value, gpointer s)
{
  GstStructure *structure = (GstStructure *) s;
  GType type = G_VALUE_TYPE (value);

  if (G_TYPE_IS_FUNDAMENTAL (type) || type == GST_TYPE_FOURCC)
    return TRUE;

  if (type == GST_TYPE_INT_RANGE) {
    gst_structure_set (structure, g_quark_to_string (field_id),
        G_TYPE_INT, gst_value_get_int_range_min (value), NULL);
    return FALSE;
  }
  if (type == GST_TYPE_DOUBLE_RANGE) {
    gst_structure_set (structure, g_quark_to_string (field_id),
        G_TYPE_DOUBLE, gst_value_get_double_range_min (value), NULL);
    return FALSE;
  }
  if (type == GST_TYPE_LIST) {
    gst_structure_set_value (structure, g_quark_to_string (field_id),
        gst_value_list_get_value (value, 0));
    return FALSE;
  }

  g_critical ("don't know how to fixate type %s", g_type_name (type));
  return TRUE;
}

static GstCaps *
_gst_pad_default_fixate_func (GstPad * pad, const GstCaps * caps)
{
  static GstStaticCaps octetcaps = GST_STATIC_CAPS ("application/octet-stream");
  GstStructure *structure;
  GstCaps *newcaps;

  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (caps != NULL, NULL);
  g_return_val_if_fail (!gst_caps_is_empty (caps), NULL);

  if (gst_caps_is_any (caps)) {
    return gst_caps_copy (gst_static_caps_get (&octetcaps));
  }

  if (caps->structs->len > 1) {
    return gst_caps_new_full (gst_structure_copy (gst_caps_get_structure (caps,
                0)), NULL);
  }

  newcaps = gst_caps_copy (caps);
  structure = gst_caps_get_structure (newcaps, 0);
  gst_structure_foreach (structure, _gst_pad_default_fixate_foreach, structure);

  return newcaps;
}

/**
 * gst_pad_perform_negotiate:
 * @srcpad: the source #GstPad.
 * @sinkpad: the sink #GstPad.
 *
 * Tries to negotiate the pads. See gst_pad_renegotiate() for a brief
 * description of caps negotiation.
 *
 * Returns: TRUE if the pads were succesfully negotiated, FALSE otherwise.
 */
gboolean
gst_pad_perform_negotiate (GstPad * srcpad, GstPad * sinkpad)
{
  return GST_PAD_LINK_SUCCESSFUL (gst_pad_renegotiate (srcpad));
}

static void
gst_pad_link_unnegotiate (GstPadLink * link)
{
  g_return_if_fail (link != NULL);

  if (link->caps) {
    gst_caps_free (link->caps);
    link->caps = NULL;
    if (GST_RPAD_LINK (link->srcpad) != link) {
      g_warning ("unnegotiating unset link");
    } else {
      g_object_notify (G_OBJECT (link->srcpad), "caps");
    }
    if (GST_RPAD_LINK (link->sinkpad) != link) {
      g_warning ("unnegotiating unset link");
    } else {
      g_object_notify (G_OBJECT (link->sinkpad), "caps");
    }
  }
}

/**
 * gst_pad_unnegotiate:
 * @pad: pad to unnegotiate
 *
 * "Unnegotiates" a pad. The currently negotiated caps are cleared and the pad 
 * needs renegotiation.
 */
void
gst_pad_unnegotiate (GstPad * pad)
{
  GstPadLink *link;

  g_return_if_fail (GST_IS_PAD (pad));

  link = GST_RPAD_LINK (GST_PAD_REALIZE (pad));
  if (link)
    gst_pad_link_unnegotiate (link);
}

/* returning NULL indicates that the arguments are invalid */
static GstPadLink *
gst_pad_link_prepare (GstPad * srcpad, GstPad * sinkpad,
    const GstCaps * filtercaps)
{
  GstRealPad *realsrc, *realsink;
  GstPadLink *link;

  g_return_val_if_fail (GST_IS_PAD (srcpad), NULL);
  g_return_val_if_fail (GST_IS_PAD (sinkpad), NULL);

  realsrc = GST_PAD_REALIZE (srcpad);
  realsink = GST_PAD_REALIZE (sinkpad);

  if ((GST_PAD (realsrc) != srcpad) || (GST_PAD (realsink) != sinkpad)) {
    GST_CAT_DEBUG (GST_CAT_PADS, "*actually* linking %s:%s and %s:%s",
        GST_DEBUG_PAD_NAME (realsrc), GST_DEBUG_PAD_NAME (realsink));
  }

  g_return_val_if_fail (GST_RPAD_PEER (realsrc) == NULL, NULL);
  g_return_val_if_fail (GST_RPAD_PEER (realsink) == NULL, NULL);
  g_return_val_if_fail (GST_PAD_PARENT (realsrc) != NULL, NULL);
  g_return_val_if_fail (GST_PAD_PARENT (realsink) != NULL, NULL);

  if (!gst_pad_check_schedulers (realsrc, realsink)) {
    g_warning ("linking pads with different scheds requires "
        "exactly one decoupled element (such as queue)");
    return NULL;
  }

  if (GST_RPAD_DIRECTION (realsrc) == GST_RPAD_DIRECTION (realsink)) {
    g_warning ("%s:%s and %s:%s are both %s pads, failed",
        GST_DEBUG_PAD_NAME (realsrc), GST_DEBUG_PAD_NAME (realsink),
        GST_RPAD_DIRECTION (realsrc) == GST_PAD_SRC ? "src" : "sink");
    return NULL;
  }

  link = gst_pad_link_new ();

  if (GST_RPAD_DIRECTION (realsrc) == GST_PAD_SRC) {
    link->srcpad = GST_PAD (realsrc);
    link->sinkpad = GST_PAD (realsink);
  } else {
    link->srcpad = GST_PAD (realsink);
    link->sinkpad = GST_PAD (realsrc);
  }

  link->srccaps = gst_pad_get_caps (link->srcpad);
  link->sinkcaps = gst_pad_get_caps (link->sinkpad);
  if (filtercaps)
    link->filtercaps = gst_caps_copy (filtercaps);

  return link;
}

/**
 * gst_pad_try_relink_filtered:
 * @srcpad: the source #GstPad to relink.
 * @sinkpad: the sink #GstPad to relink.
 * @filtercaps: the #GstPad to use as a filter in the relink.
 *
 * Tries to relink the given source and sink pad, constrained by the given
 * capabilities.
 *
 * Returns: TRUE if the pads were succesfully renegotiated, FALSE otherwise.
 */
gboolean
gst_pad_try_relink_filtered (GstPad * srcpad, GstPad * sinkpad,
    const GstCaps * filtercaps)
{
  GstPadLink *link;

  GST_INFO ("trying to relink %" GST_PTR_FORMAT " and %" GST_PTR_FORMAT
      " with filtercaps %" GST_PTR_FORMAT, srcpad, sinkpad);

  link = gst_pad_link_prepare (srcpad, sinkpad, filtercaps);
  if (!link)
    return FALSE;

  if (GST_RPAD_PEER (link->srcpad) != (GstRealPad *) link->sinkpad) {
    g_warning ("Pads %s:%s and %s:%s were never linked",
        GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));
    gst_pad_link_free (link);
    return FALSE;
  }

  if (GST_PAD_LINK_FAILED (gst_pad_link_try (link)))
    return FALSE;

  return TRUE;
}

/**
 * gst_pad_relink_filtered:
 * @srcpad: the source #GstPad to relink.
 * @sinkpad: the sink #GstPad to relink.
 * @filtercaps: the #GstPad to use as a filter in the relink.
 *
 * Relinks the given source and sink pad, constrained by the given
 * capabilities.  If the relink fails, the pads are unlinked
 * and FALSE is returned.
 *
 * Returns: TRUE if the pads were succesfully relinked, FALSE otherwise.
 */
gboolean
gst_pad_relink_filtered (GstPad * srcpad, GstPad * sinkpad,
    const GstCaps * filtercaps)
{
  if (gst_pad_try_relink_filtered (srcpad, sinkpad, filtercaps))
    return TRUE;

  gst_pad_unlink (srcpad, sinkpad);
  return FALSE;
}

/**
 * gst_pad_proxy_getcaps:
 * @pad: a #GstPad to proxy.
 *
 * Calls gst_pad_get_allowed_caps() for every other pad belonging to the
 * same element as @pad, and returns the intersection of the results.
 *
 * This function is useful as a default getcaps function for an element
 * that can handle any stream format, but requires all its pads to have
 * the same caps.  Two such elements are tee and aggregator.
 *
 * Returns: the intersection of the other pads' allowed caps.
 */
GstCaps *
gst_pad_proxy_getcaps (GstPad * pad)
{
  GstElement *element;
  const GList *pads;
  GstCaps *caps;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  GST_DEBUG ("proxying getcaps for %s:%s", GST_DEBUG_PAD_NAME (pad));

  element = gst_pad_get_parent (pad);

  pads = gst_element_get_pad_list (element);

  caps = gst_caps_new_any ();
  while (pads) {
    GstPad *otherpad = GST_PAD (pads->data);
    GstCaps *temp;

    if (otherpad != pad) {
      GstCaps *allowed = gst_pad_get_allowed_caps (otherpad);

      temp = gst_caps_intersect (caps, allowed);
      gst_caps_free (caps);
      gst_caps_free (allowed);
      caps = temp;
    }

    pads = g_list_next (pads);
  }

  return caps;
}

/**
 * gst_pad_proxy_pad_link:
 * @pad: a #GstPad to proxy from
 * @caps: the #GstCaps to link with
 *
 * Calls gst_pad_try_set_caps() for every other pad belonging to the
 * same element as @pad.  If gst_pad_try_set_caps() fails on any pad,
 * the proxy link fails. May be used only during negotiation.
 *
 * Returns: GST_PAD_LINK_OK if sucessful
 */
GstPadLinkReturn
gst_pad_proxy_pad_link (GstPad * pad, const GstCaps * caps)
{
  GstElement *element;
  const GList *pads;
  GstPadLinkReturn ret;

  g_return_val_if_fail (GST_IS_PAD (pad), GST_PAD_LINK_REFUSED);
  g_return_val_if_fail (caps != NULL, GST_PAD_LINK_REFUSED);

  GST_DEBUG ("proxying pad link for %s:%s", GST_DEBUG_PAD_NAME (pad));

  element = gst_pad_get_parent (pad);

  pads = gst_element_get_pad_list (element);

  while (pads) {
    GstPad *otherpad = GST_PAD (pads->data);

    if (otherpad != pad) {
      ret = gst_pad_try_set_caps (otherpad, caps);
      if (GST_PAD_LINK_FAILED (ret)) {
        return ret;
      }
    }
    pads = g_list_next (pads);
  }

  return GST_PAD_LINK_OK;
}

/**
 * gst_pad_proxy_fixate:
 * @pad: a #GstPad to proxy.
 * @caps: the #GstCaps to fixate
 *
 * Implements a default fixate function based on the caps set on the other
 * pads in the element.  This function should only be used if every pad
 * has the same pad template caps.
 *
 * Returns: a fixated caps, or NULL if caps cannot be fixed
 */
GstCaps *
gst_pad_proxy_fixate (GstPad * pad, const GstCaps * caps)
{
  GstElement *element;
  const GList *pads;
  const GstCaps *othercaps;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);
  g_return_val_if_fail (caps != NULL, NULL);

  GST_DEBUG ("proxying fixate for %s:%s\n", GST_DEBUG_PAD_NAME (pad));

  element = gst_pad_get_parent (pad);

  pads = gst_element_get_pad_list (element);

  while (pads) {
    GstPad *otherpad = GST_PAD (pads->data);

    /* FIXME check that each pad has the same pad template caps */

    if (otherpad != pad) {
      othercaps = gst_pad_get_negotiated_caps (otherpad);

      if (othercaps) {
        GstCaps *icaps;

        icaps = gst_caps_intersect (othercaps, caps);
        if (!gst_caps_is_empty (icaps)) {
          return icaps;
        } else {
          gst_caps_free (icaps);
        }
      }
    }
    pads = g_list_next (pads);
  }

  return NULL;
}

/**
 * gst_pad_set_explicit_caps:
 * @pad: a #GstPad to set the explicit caps of
 * @caps: the #GstCaps to set
 *
 * If a pad has been told to use explicit caps, this function is used
 * to set the explicit caps.  If @caps is NULL, the explicit caps are
 * unset.
 *
 * This function calls gst_pad_try_set_caps() on the pad.  If that
 * call fails, GST_ELEMENT_ERROR() is called to indicate a negotiation
 * failure.
 * 
 * Returns: TRUE if the caps were set correctly, otherwise FALSE
 */
gboolean
gst_pad_set_explicit_caps (GstPad * pad, const GstCaps * caps)
{
  GstPadLinkReturn link_ret;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  GST_CAT_DEBUG (GST_CAT_PADS,
      "setting explicit caps on %s:%s to %" GST_PTR_FORMAT,
      GST_DEBUG_PAD_NAME (pad), caps);

  if (caps == NULL) {
    GST_CAT_DEBUG (GST_CAT_PADS, "caps is NULL");
    gst_caps_replace (&GST_RPAD_EXPLICIT_CAPS (pad), NULL);
    return TRUE;
  }

  gst_caps_replace (&GST_RPAD_EXPLICIT_CAPS (pad), gst_caps_copy (caps));

  if (!GST_PAD_IS_LINKED (pad)) {
    GST_CAT_DEBUG (GST_CAT_PADS, "pad is not linked");
    return TRUE;
  }
  link_ret = gst_pad_try_set_caps (pad, caps);
  if (link_ret == GST_PAD_LINK_REFUSED) {
    gchar *caps_str = gst_caps_to_string (caps);

    GST_ELEMENT_ERROR (gst_pad_get_parent (pad), CORE, PAD, (NULL),
        ("failed to negotiate (try_set_caps with \"%s\" returned REFUSED)",
            caps_str));
    g_free (caps_str);
    return FALSE;
  }

  return TRUE;
}

static GstCaps *
gst_pad_explicit_getcaps (GstPad * pad)
{
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  if (GST_RPAD_EXPLICIT_CAPS (pad) == NULL) {
    const GstCaps *caps = gst_pad_get_pad_template_caps (pad);

    return gst_caps_copy (caps);
  }
  return gst_caps_copy (GST_RPAD_EXPLICIT_CAPS (pad));
}

static GstPadLinkReturn
gst_pad_explicit_link (GstPad * pad, const GstCaps * caps)
{
  g_return_val_if_fail (GST_IS_PAD (pad), GST_PAD_LINK_REFUSED);
  g_return_val_if_fail (caps != NULL, GST_PAD_LINK_REFUSED);

  if (GST_RPAD_EXPLICIT_CAPS (pad) == NULL) {
    return GST_PAD_LINK_DELAYED;
  }

  return GST_PAD_LINK_OK;
}

/**
 * gst_pad_use_explicit_caps:
 * @pad: a #GstPad to set to use explicit caps
 *
 * This function handles negotiation for pads that need to be set
 * to particular caps under complete control of the element, based
 * on some state in the element.  This is often the case with
 * decoders and other elements whose caps is determined by the data
 * stream.
 *
 * WARNING: This function is a hack and will be replaced with something
 * better in gstreamer-0.9.
 */
void
gst_pad_use_explicit_caps (GstPad * pad)
{
  g_return_if_fail (GST_IS_PAD (pad));

  gst_pad_set_getcaps_function (pad, gst_pad_explicit_getcaps);
  gst_pad_set_link_function (pad, gst_pad_explicit_link);
  gst_caps_replace (&GST_RPAD_EXPLICIT_CAPS (pad), NULL);
}

/**
 * gst_pad_proxy_link:
 * @pad: a #GstPad to proxy to.
 * @caps: the #GstCaps to use in proxying.
 *
 * Proxies the link function to the specified pad.
 *
 * Returns: TRUE if the peer pad accepted the caps, FALSE otherwise.
 */
GstPadLinkReturn
gst_pad_proxy_link (GstPad * pad, const GstCaps * caps)
{
  return gst_pad_try_set_caps (pad, caps);
}

/**
 * gst_pad_is_negotiated:
 * @pad: a #GstPad to get the negotiation status of
 *
 * Returns: TRUE if the pad has successfully negotiated caps.
 */
gboolean
gst_pad_is_negotiated (GstPad * pad)
{
  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  if (!GST_PAD_REALIZE (pad))
    return FALSE;
  if (!GST_RPAD_LINK (pad))
    return FALSE;

  return (GST_RPAD_LINK (pad)->caps != NULL);
}

/**
 * gst_pad_get_negotiated_caps:
 * @pad: a #GstPad to get the negotiated capabilites of
 *
 * Gets the currently negotiated caps of a pad.
 *
 * Returns: the currently negotiated caps of a pad, or NULL if the pad isn't
 *	    negotiated.
 */
G_CONST_RETURN GstCaps *
gst_pad_get_negotiated_caps (GstPad * pad)
{
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  if (!GST_PAD_REALIZE (pad))
    return NULL;
  if (!GST_RPAD_LINK (pad))
    return NULL;

  return GST_RPAD_LINK (pad)->caps;
}

/**
 * gst_pad_get_caps:
 * @pad: a  #GstPad to get the capabilities of.
 *
 * Gets the capabilities of this pad.
 *
 * Returns: the #GstCaps of this pad. This function returns a new caps, so use 
 * gst_caps_free to get rid of it.
 */
GstCaps *
gst_pad_get_caps (GstPad * pad)
{
  GstRealPad *realpad;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  realpad = GST_PAD_REALIZE (pad);

  GST_CAT_DEBUG (GST_CAT_CAPS, "get pad caps of %s:%s (%p)",
      GST_DEBUG_PAD_NAME (realpad), realpad);

  if (GST_RPAD_GETCAPSFUNC (realpad)) {
    GstCaps *caps;

    GST_CAT_DEBUG (GST_CAT_CAPS, "using pad getcaps function");
    caps = GST_RPAD_GETCAPSFUNC (realpad) (GST_PAD (realpad));

    if (caps == NULL) {
      g_critical ("pad %s:%s returned NULL caps from getcaps function\n",
          GST_ELEMENT_NAME (GST_PAD_PARENT (GST_PAD (realpad))),
          GST_PAD_NAME (realpad));
      caps = gst_caps_new_any ();
    }

    return caps;
  } else if (GST_PAD_PAD_TEMPLATE (realpad)) {
    GstPadTemplate *templ = GST_PAD_PAD_TEMPLATE (realpad);
    const GstCaps *caps;

    caps = GST_PAD_TEMPLATE_CAPS (templ);
    GST_CAT_DEBUG (GST_CAT_CAPS,
        "using pad template %p with caps %" GST_PTR_FORMAT, templ, caps);

#if 0
    /* FIXME we should enable something like this someday, but this is
     * a bit buggy */
    if (!gst_caps_is_fixed (caps)) {
      g_warning
          ("pad %s:%s (%p) has no getcaps function and the pad template returns non-fixed caps.  Element is probably broken.\n",
          GST_DEBUG_PAD_NAME (realpad), realpad);
    }
#endif

    return gst_caps_copy (GST_PAD_TEMPLATE_CAPS (templ));
  }
  GST_CAT_DEBUG (GST_CAT_CAPS, "pad has no caps");

#if 0
  /* FIXME enable */
  g_warning ("pad %s:%s (%p) has no pad template\n",
      GST_DEBUG_PAD_NAME (realpad), realpad);
#endif

  return gst_caps_new_any ();
}

/**
 * gst_pad_get_pad_template_caps:
 * @pad: a #GstPad to get the template capabilities from.
 *
 * Gets the capabilities for @pad's template.
 *
 * Returns: the #GstCaps of this pad template. If you intend to keep a reference
 * on the caps, make a copy (see gst_caps_copy ()).
 */
const GstCaps *
gst_pad_get_pad_template_caps (GstPad * pad)
{
  static GstStaticCaps anycaps = GST_STATIC_CAPS ("ANY");

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  if (GST_PAD_PAD_TEMPLATE (pad))
    return GST_PAD_TEMPLATE_CAPS (GST_PAD_PAD_TEMPLATE (pad));

#if 0
  /* FIXME this should be enabled some day */
  /* wingo: why? mail the list during 0.9 when you find this :) */
  g_warning ("pad %s:%s (%p) has no pad template\n",
      GST_DEBUG_PAD_NAME (realpad), realpad);
#endif

  return gst_static_caps_get (&anycaps);
}

/* FIXME 0.9: This function should probably die, or at least be renamed to
 * get_caps_by_format. */
/**
 * gst_pad_template_get_caps_by_name:
 * @templ: a #GstPadTemplate to get the capabilities of.
 * @name: the name of the capability to get.
 *
 * Gets the capability with the given name from @templ.
 *
 * Returns: the #GstCaps of this pad template, or NULL if not found. If you
 * intend to keep a reference on the caps, make a copy (see gst_caps_copy ()).
 */
const GstCaps *
gst_pad_template_get_caps_by_name (GstPadTemplate * templ, const gchar * name)
{
  GstCaps *caps;

  g_return_val_if_fail (templ != NULL, NULL);

  caps = GST_PAD_TEMPLATE_CAPS (templ);
  if (!caps)
    return NULL;

  /* FIXME */
  //return gst_caps_copy (gst_caps_get_by_name (caps, name));
  return NULL;
}

/* FIXME 0.9: What good is this if it only works for already-negotiated pads? */
/**
 * gst_pad_check_compatibility:
 * @srcpad: the source #GstPad to check.
 * @sinkpad: the sink #GstPad to check against.
 *
 * Checks if two pads have compatible capabilities. If neither one has yet been
 * negotiated, returns TRUE for no good reason.
 *
 * Returns: TRUE if they are compatible or if the capabilities could not be
 * checked, FALSE if the capabilities are not compatible.
 */
gboolean
gst_pad_check_compatibility (GstPad * srcpad, GstPad * sinkpad)
{
  g_return_val_if_fail (GST_IS_PAD (srcpad), FALSE);
  g_return_val_if_fail (GST_IS_PAD (sinkpad), FALSE);

  if (GST_PAD_CAPS (srcpad) && GST_PAD_CAPS (sinkpad)) {
    if (!gst_caps_is_always_compatible (GST_PAD_CAPS (srcpad),
            GST_PAD_CAPS (sinkpad))) {
      return FALSE;
    } else {
      return TRUE;
    }
  } else {
    GST_CAT_DEBUG (GST_CAT_PADS,
        "could not check capabilities of pads (%s:%s) and (%s:%s) %p %p",
        GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad),
        GST_PAD_CAPS (srcpad), GST_PAD_CAPS (sinkpad));
    return TRUE;
  }
}

/**
 * gst_pad_get_peer:
 * @pad: a #GstPad to get the peer of.
 *
 * Gets the peer of @pad.
 *
 * Returns: the peer #GstPad.
 */
GstPad *
gst_pad_get_peer (GstPad * pad)
{
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  return GST_PAD (GST_PAD_PEER (pad));
}

/**
 * gst_pad_get_allowed_caps:
 * @pad: a real #GstPad.
 *
 * Gets the capabilities of the allowed media types that can flow through @pad.
 * The caller must free the resulting caps.
 *
 * Returns: the allowed #GstCaps of the pad link.  Free the caps when
 * you no longer need it.
 */
GstCaps *
gst_pad_get_allowed_caps (GstPad * pad)
{
  const GstCaps *mycaps;
  GstCaps *caps;
  GstCaps *peercaps;
  GstCaps *icaps;
  GstPadLink *link;

  g_return_val_if_fail (GST_IS_REAL_PAD (pad), NULL);

  GST_CAT_DEBUG (GST_CAT_PROPERTIES, "%s:%s: getting allowed caps",
      GST_DEBUG_PAD_NAME (pad));

  mycaps = gst_pad_get_pad_template_caps (pad);
  if (GST_RPAD_PEER (pad) == NULL) {
    GST_CAT_DEBUG (GST_CAT_PROPERTIES, "%s:%s: no peer, returning template",
        GST_DEBUG_PAD_NAME (pad));
    return gst_caps_copy (mycaps);
  }

  peercaps = gst_pad_get_caps (GST_PAD_PEER (pad));
  caps = gst_caps_intersect (mycaps, peercaps);
  gst_caps_free (peercaps);

  link = GST_RPAD_LINK (pad);
  if (link->filtercaps) {
    icaps = gst_caps_intersect (caps, link->filtercaps);
    gst_caps_free (caps);
    GST_CAT_DEBUG (GST_CAT_PROPERTIES,
        "%s:%s: returning filtered intersection with peer",
        GST_DEBUG_PAD_NAME (pad));
    return icaps;
  } else {
    GST_CAT_DEBUG (GST_CAT_PROPERTIES,
        "%s:%s: returning unfiltered intersection with peer",
        GST_DEBUG_PAD_NAME (pad));
    return caps;
  }
}

/**
 * gst_pad_caps_change_notify:
 * @pad: a #GstPad
 *
 * Called to indicate that the return value of @pad's getcaps function may have
 * changed, and that a renegotiation is suggested.
 */
void
gst_pad_caps_change_notify (GstPad * pad)
{
}

/**
 * gst_pad_recover_caps_error:
 * @pad: a #GstPad that had a failed capsnego
 * @allowed: possible caps for the link
 *
 * Attempt to recover from a failed caps negotiation. This function
 * is typically called by a plugin that exhausted its list of caps
 * and wants the application to resolve the issue. The application
 * should connect to the pad's caps_nego_failed signal and should
 * resolve the issue by connecting another element for example.
 *
 * Returns: TRUE when the issue was resolved, dumps detailed information
 * on the console and returns FALSE otherwise.
 */
gboolean
gst_pad_recover_caps_error (GstPad * pad, const GstCaps * allowed)
{
  /* FIXME */
  return FALSE;
}

/**
 * gst_pad_alloc_buffer:
 * @pad: a source #GstPad
 * @offset: the offset of the new buffer in the stream
 * @size: the size of the new buffer
 *
 * Allocates a new, empty buffer optimized to push to pad @pad.  This
 * function only works if @pad is a source pad.
 *
 * Returns: a new, empty #GstBuffer, or NULL if there is an error
 */
GstBuffer *
gst_pad_alloc_buffer (GstPad * pad, guint64 offset, gint size)
{
  GstRealPad *peer;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);
  g_return_val_if_fail (GST_PAD_IS_SRC (pad), NULL);

  peer = GST_RPAD_PEER (pad);

  if (!peer)
    return gst_buffer_new_and_alloc (size);

  GST_CAT_DEBUG (GST_CAT_BUFFER, "(%s:%s): getting buffer",
      GST_DEBUG_PAD_NAME (pad));

  if (peer->bufferallocfunc) {
    GST_CAT_DEBUG (GST_CAT_PADS,
        "calling bufferallocfunc &%s (@%p) of peer pad %s:%s",
        GST_DEBUG_FUNCPTR_NAME (peer->bufferallocfunc),
        &peer->bufferallocfunc, GST_DEBUG_PAD_NAME (((GstPad *) peer)));
    return (peer->bufferallocfunc) (GST_PAD (peer), offset, size);
  } else {
    return gst_buffer_new_and_alloc (size);
  }
}

static void
gst_real_pad_dispose (GObject * object)
{
  GstPad *pad = GST_PAD (object);

  /* No linked pad can ever be disposed.
   * It has to have a parent to be linked 
   * and a parent would hold a reference */
  /* FIXME: what about if g_object_dispose is explicitly called on the pad? Is
     that legal? otherwise we could assert GST_OBJECT_PARENT (pad) == NULL as
     well... */
  g_assert (GST_PAD_PEER (pad) == NULL);

  GST_CAT_DEBUG (GST_CAT_REFCOUNTING, "dispose %s:%s",
      GST_DEBUG_PAD_NAME (pad));

  /* we destroy the ghostpads, because they are nothing without the real pad */
  if (GST_REAL_PAD (pad)->ghostpads) {
    GList *orig, *ghostpads;

    orig = ghostpads = g_list_copy (GST_REAL_PAD (pad)->ghostpads);

    while (ghostpads) {
      GstPad *ghostpad = GST_PAD (ghostpads->data);

      if (GST_IS_ELEMENT (GST_OBJECT_PARENT (ghostpad))) {
        GstElement *parent = GST_ELEMENT (GST_OBJECT_PARENT (ghostpad));

        GST_CAT_DEBUG (GST_CAT_REFCOUNTING,
            "removing ghost pad from element '%s'", GST_OBJECT_NAME (parent));
        gst_element_remove_pad (parent, ghostpad);
      } else {
        /* handle the case where we have some floating ghost pad that was never
           added to an element */
        g_object_set (ghostpad, "real-pad", NULL, NULL);
      }
      ghostpads = g_list_next (ghostpads);
    }
    g_list_free (orig);
    /* as the ghost pads are removed, they remove themselves from ->ghostpads.
       So it should be empty now. Let's assert that. */
    g_assert (GST_REAL_PAD (pad)->ghostpads == NULL);
  }

  if (GST_IS_ELEMENT (GST_OBJECT_PARENT (pad))) {
    GST_CAT_DEBUG (GST_CAT_REFCOUNTING, "removing pad from element '%s'",
        GST_OBJECT_NAME (GST_OBJECT (GST_ELEMENT (GST_OBJECT_PARENT (pad)))));

    gst_element_remove_pad (GST_ELEMENT (GST_OBJECT_PARENT (pad)), pad);
  }

  if (GST_RPAD_EXPLICIT_CAPS (pad)) {
    GST_ERROR_OBJECT (pad, "still explicit caps %" GST_PTR_FORMAT " set",
        GST_RPAD_EXPLICIT_CAPS (pad));
    g_warning ("pad %p has still explicit caps set", pad);
    gst_caps_replace (&GST_RPAD_EXPLICIT_CAPS (pad), NULL);
  }
  G_OBJECT_CLASS (real_pad_parent_class)->dispose (object);
}


#ifndef GST_DISABLE_LOADSAVE
/* FIXME: why isn't this on a GstElement ? */
/**
 * gst_pad_load_and_link:
 * @self: an #xmlNodePtr to read the description from.
 * @parent: the #GstObject element that owns the pad.
 *
 * Reads the pad definition from the XML node and links the given pad
 * in the element to a pad of an element up in the hierarchy.
 */
void
gst_pad_load_and_link (xmlNodePtr self, GstObject * parent)
{
  xmlNodePtr field = self->xmlChildrenNode;
  GstPad *pad = NULL, *targetpad;
  gchar *peer = NULL;
  gchar **split;
  GstElement *target;
  GstObject *grandparent;

  while (field) {
    if (!strcmp (field->name, "name")) {
      pad = gst_element_get_pad (GST_ELEMENT (parent),
          xmlNodeGetContent (field));
    } else if (!strcmp (field->name, "peer")) {
      peer = xmlNodeGetContent (field);
    }
    field = field->next;
  }
  g_return_if_fail (pad != NULL);

  if (peer == NULL)
    return;

  split = g_strsplit (peer, ".", 2);

  if (split[0] == NULL || split[1] == NULL) {
    GST_CAT_DEBUG (GST_CAT_XML,
        "Could not parse peer '%s' for pad %s:%s, leaving unlinked",
        peer, GST_DEBUG_PAD_NAME (pad));
    return;
  }

  g_return_if_fail (split[0] != NULL);
  g_return_if_fail (split[1] != NULL);

  grandparent = gst_object_get_parent (parent);

  if (grandparent && GST_IS_BIN (grandparent)) {
    target = gst_bin_get_by_name_recurse_up (GST_BIN (grandparent), split[0]);
  } else
    goto cleanup;

  if (target == NULL)
    goto cleanup;

  targetpad = gst_element_get_pad (target, split[1]);

  if (targetpad == NULL)
    goto cleanup;

  gst_pad_link (pad, targetpad);

cleanup:
  g_strfreev (split);
}

/**
 * gst_pad_save_thyself:
 * @pad: a #GstPad to save.
 * @parent: the parent #xmlNodePtr to save the description in.
 *
 * Saves the pad into an xml representation.
 *
 * Returns: the #xmlNodePtr representation of the pad.
 */
static xmlNodePtr
gst_pad_save_thyself (GstObject * object, xmlNodePtr parent)
{
  GstRealPad *realpad;
  GstPad *peer;

  g_return_val_if_fail (GST_IS_REAL_PAD (object), NULL);

  realpad = GST_REAL_PAD (object);

  xmlNewChild (parent, NULL, "name", GST_PAD_NAME (realpad));
  if (GST_RPAD_PEER (realpad) != NULL) {
    gchar *content;

    peer = GST_PAD (GST_RPAD_PEER (realpad));
    /* first check to see if the peer's parent's parent is the same */
    /* we just save it off */
    content = g_strdup_printf ("%s.%s",
        GST_OBJECT_NAME (GST_PAD_PARENT (peer)), GST_PAD_NAME (peer));
    xmlNewChild (parent, NULL, "peer", content);
    g_free (content);
  } else
    xmlNewChild (parent, NULL, "peer", "");

  return parent;
}

/* FIXME: shouldn't it be gst_pad_ghost_* ?
 * dunno -- wingo 7 feb 2004
 */
/**
 * gst_ghost_pad_save_thyself:
 * @pad: a ghost #GstPad to save.
 * @parent: the parent #xmlNodePtr to save the description in.
 *
 * Saves the ghost pad into an xml representation.
 *
 * Returns: the #xmlNodePtr representation of the pad.
 */
xmlNodePtr
gst_ghost_pad_save_thyself (GstPad * pad, xmlNodePtr parent)
{
  xmlNodePtr self;

  g_return_val_if_fail (GST_IS_GHOST_PAD (pad), NULL);

  self = xmlNewChild (parent, NULL, "ghostpad", NULL);
  xmlNewChild (self, NULL, "name", GST_PAD_NAME (pad));
  xmlNewChild (self, NULL, "parent", GST_OBJECT_NAME (GST_PAD_PARENT (pad)));

  /* FIXME FIXME FIXME! */

  return self;
}
#endif /* GST_DISABLE_LOADSAVE */

/**
 * gst_pad_push:
 * @pad: a source #GstPad.
 * @data: the #GstData to push.
 *
 * Pushes a buffer or an event to the peer of @pad. @pad must be linked. May
 * only be called by @pad's parent.
 */
void
gst_pad_push (GstPad * pad, GstData * data)
{
  GstRealPad *peer;

  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (GST_PAD_DIRECTION (pad) == GST_PAD_SRC);
  g_return_if_fail (data != NULL);

  if (!gst_probe_dispatcher_dispatch (&(GST_REAL_PAD (pad)->probedisp), &data))
    return;

  if (!GST_PAD_IS_LINKED (pad)) {
    GST_CAT_LOG_OBJECT (GST_CAT_DATAFLOW, pad,
        "not pushing data %p as pad is unconnected", data);
    gst_data_unref (data);
    return;
  }

  GST_CAT_LOG_OBJECT (GST_CAT_DATAFLOW, pad, "pushing");
  peer = GST_RPAD_PEER (pad);

  if (!peer) {
    g_warning ("push on pad %s:%s but it is unlinked",
        GST_DEBUG_PAD_NAME (pad));
  } else {
    if (!GST_IS_EVENT (data) && !GST_PAD_IS_ACTIVE (peer)) {
      g_warning ("push on peer of pad %s:%s but peer is not active",
          GST_DEBUG_PAD_NAME (pad));
      return;
    }

    if (peer->chainhandler) {
      if (data) {
        GST_CAT_LOG_OBJECT (GST_CAT_DATAFLOW, pad,
            "calling chainhandler &%s of peer pad %s:%s",
            GST_DEBUG_FUNCPTR_NAME (peer->chainhandler),
            GST_DEBUG_PAD_NAME (GST_PAD (peer)));
        if (!gst_probe_dispatcher_dispatch (&peer->probedisp, &data))
          return;

        (peer->chainhandler) (GST_PAD (peer), data);
        return;
      } else {
        g_warning ("trying to push a NULL buffer on pad %s:%s",
            GST_DEBUG_PAD_NAME (peer));
        return;
      }
    } else {
      g_warning ("internal error: push on pad %s:%s but it has no chainhandler",
          GST_DEBUG_PAD_NAME (peer));
    }
  }
  /* clean up the mess here */
  if (data != NULL)
    gst_data_unref (data);
}

/**
 * gst_pad_pull:
 * @pad: a sink #GstPad.
 *
 * Pulls an event or a buffer from the peer pad. May only be called by @pad's
 * parent.
 *
 * Returns: a new #GstData from the peer pad.
 */
GstData *
gst_pad_pull (GstPad * pad)
{
  GstRealPad *peer;

  GST_CAT_LOG_OBJECT (GST_CAT_DATAFLOW, pad, "pulling");

  g_return_val_if_fail (GST_PAD_DIRECTION (pad) == GST_PAD_SINK,
      GST_DATA (gst_event_new (GST_EVENT_INTERRUPT)));

  peer = GST_RPAD_PEER (pad);

  if (!peer) {
    GST_ELEMENT_ERROR (GST_PAD_PARENT (pad), CORE, PAD, (NULL),
        ("pull on pad %s:%s but it was unlinked", GST_DEBUG_PAD_NAME (pad)));
  } else {
  restart:
    if (peer->gethandler) {
      GstData *data;

      GST_CAT_LOG_OBJECT (GST_CAT_DATAFLOW, pad,
          "calling gethandler %s of peer pad %s:%s",
          GST_DEBUG_FUNCPTR_NAME (peer->gethandler), GST_DEBUG_PAD_NAME (peer));

      data = (peer->gethandler) (GST_PAD (peer));

      if (data) {
        if (!gst_probe_dispatcher_dispatch (&peer->probedisp, &data))
          goto restart;
        return data;
      }

      /* no null buffers allowed */
      GST_ELEMENT_ERROR (GST_PAD_PARENT (pad), CORE, PAD, (NULL),
          ("NULL buffer during pull on %s:%s", GST_DEBUG_PAD_NAME (pad)));
    } else {
      GST_ELEMENT_ERROR (GST_PAD_PARENT (pad), CORE, PAD, (NULL),
          ("pull on pad %s:%s but the peer pad %s:%s has no gethandler",
              GST_DEBUG_PAD_NAME (pad), GST_DEBUG_PAD_NAME (peer)));
    }
  }
  return GST_DATA (gst_event_new (GST_EVENT_INTERRUPT));
}

GstData *
gst_pad_collect_array (GstScheduler * scheduler, GstPad ** selected,
    GstPad ** padlist)
{
  GstSchedulerClass *klass = GST_SCHEDULER_GET_CLASS (scheduler);

  if (!GST_FLAG_IS_SET (scheduler, GST_SCHEDULER_FLAG_NEW_API) ||
      !klass->pad_select) {
    /* better randomness? */
    if (selected)
      *selected = padlist[0];
    return gst_pad_pull (padlist[0]);
  } else {
    GstPad *select;

    return klass->pad_select (scheduler, selected ? selected : &select,
        padlist);
  }
}

/**
 * gst_pad_collectv:
 * @selected: set to the pad the buffer comes from if not NULL
 * @padlist: a #GList of sink pads.
 *
 * Waits for a buffer on any of the list of pads. Each #GstPad in @padlist must
 * belong to the same element and be owned by the caller.
 *
 * Returns: the #GstData that was available
 */
GstData *
gst_pad_collectv (GstPad ** selected, const GList * padlist)
{
  /* need to use alloca here because we must not leak data */
  GstPad **pads;
  GstPad *test;
  GstElement *element = NULL;
  int i = 0;

  g_return_val_if_fail (padlist != NULL, NULL);
  pads = g_alloca (sizeof (gpointer) * (g_list_length ((GList *) padlist) + 1));
  for (; padlist; padlist = g_list_next (padlist)) {
    test = GST_PAD (padlist->data);
    g_return_val_if_fail (GST_IS_PAD (test), NULL);
    g_return_val_if_fail (GST_PAD_IS_SINK (test), NULL);
    if (element) {
      g_return_val_if_fail (element == gst_pad_get_parent (test), NULL);
    } else {
      element = gst_pad_get_parent (test);
    }
    pads[i++] = test;
  }
  pads[i] = NULL;

  return gst_pad_collect_array (GST_SCHEDULER (element), selected, pads);
}

/**
 * gst_pad_collect:
 * @selected: set to the pad the buffer comes from if not NULL
 * @pad: first pad
 * @...: more sink pads.
 *
 * Waits for a buffer on the given set of pads.
 *
 * Returns: the #GstData that was available.
 */
GstData *
gst_pad_collect (GstPad ** selected, GstPad * pad, ...)
{
  GstData *result;
  va_list var_args;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  va_start (var_args, pad);

  result = gst_pad_collect_valist (selected, pad, var_args);

  va_end (var_args);

  return result;
}

/**
 * gst_pad_collect_valist:
 * @selected: set to the pad the buffer comes from if not NULL
 * @pad: first pad
 * @...: more sink pads.
 *
 * Waits for a buffer on the given set of pads.
 *
 * Returns: the #GstData that was available.
 */
GstData *
gst_pad_collect_valist (GstPad ** selected, GstPad * pad, va_list var_args)
{
  GstPad **padlist;
  GstElement *element = NULL;
  gint i = 0, maxlength;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  element = gst_pad_get_parent (pad);
  maxlength = element->numsinkpads;
  /* can we make this list a bit smaller than this upper limit? */
  padlist = g_alloca (sizeof (gpointer) * (maxlength + 1));
  while (pad) {
    g_return_val_if_fail (i < maxlength, NULL);
    g_return_val_if_fail (element == gst_pad_get_parent (pad), NULL);
    padlist[i++] = pad;
    pad = va_arg (var_args, GstPad *);
  }
  return gst_pad_collect_array (GST_SCHEDULER (element), selected, padlist);
}

/**
 * gst_pad_selectv:
 * @padlist: a #GList of sink pads.
 *
 * Waits for a buffer on any of the list of pads. Each #GstPad in @padlist must
 * be owned by the calling code.
 *
 * Returns: the #GstPad that has a buffer available. 
 * Use #gst_pad_pull() to get the buffer.
 */
GstPad *
gst_pad_selectv (GList * padlist)
{
  return NULL;
}

/**
 * gst_pad_select_valist:
 * @pad: a first #GstPad to perform the select on.
 * @varargs: A va_list of more pads to select on.
 *
 * Waits for a buffer on the given set of pads.
 *
 * Returns: the #GstPad that has a buffer available.
 * Use #gst_pad_pull() to get the buffer.
 */
GstPad *
gst_pad_select_valist (GstPad * pad, va_list var_args)
{
  GstPad *result;
  GList *padlist = NULL;

  if (pad == NULL)
    return NULL;

  while (pad) {
    padlist = g_list_prepend (padlist, pad);
    pad = va_arg (var_args, GstPad *);
  }
  result = gst_pad_selectv (padlist);
  g_list_free (padlist);

  return result;
}

/**
 * gst_pad_select:
 * @pad: a first sink #GstPad to perform the select on.
 * @...: A NULL-terminated list of more pads to select on.
 *
 * Waits for a buffer on the given set of pads.
 *
 * Returns: the #GstPad that has a buffer available.
 * Use #gst_pad_pull() to get the buffer.
 */
GstPad *
gst_pad_select (GstPad * pad, ...)
{
  GstPad *result;
  va_list var_args;

  if (pad == NULL)
    return NULL;

  va_start (var_args, pad);

  result = gst_pad_select_valist (pad, var_args);

  va_end (var_args);

  return result;
}

/************************************************************************
 *
 * templates
 *
 */
static void gst_pad_template_class_init (GstPadTemplateClass * klass);
static void gst_pad_template_init (GstPadTemplate * templ);
static void gst_pad_template_dispose (GObject * object);

GType
gst_pad_template_get_type (void)
{
  static GType padtemplate_type = 0;

  if (!padtemplate_type) {
    static const GTypeInfo padtemplate_info = {
      sizeof (GstPadTemplateClass), NULL, NULL,
      (GClassInitFunc) gst_pad_template_class_init, NULL, NULL,
      sizeof (GstPadTemplate),
      0,
      (GInstanceInitFunc) gst_pad_template_init, NULL
    };

    padtemplate_type =
        g_type_register_static (GST_TYPE_OBJECT, "GstPadTemplate",
        &padtemplate_info, 0);
  }
  return padtemplate_type;
}

static void
gst_pad_template_class_init (GstPadTemplateClass * klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;

  gobject_class = (GObjectClass *) klass;
  gstobject_class = (GstObjectClass *) klass;

  padtemplate_parent_class = g_type_class_ref (GST_TYPE_OBJECT);

  gst_pad_template_signals[TEMPL_PAD_CREATED] =
      g_signal_new ("pad-created", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstPadTemplateClass, pad_created),
      NULL, NULL, gst_marshal_VOID__OBJECT, G_TYPE_NONE, 1, GST_TYPE_PAD);

  gobject_class->dispose = gst_pad_template_dispose;

  gstobject_class->path_string_separator = "*";
}

static void
gst_pad_template_init (GstPadTemplate * templ)
{
}

static void
gst_pad_template_dispose (GObject * object)
{
  GstPadTemplate *templ = GST_PAD_TEMPLATE (object);

  g_free (GST_PAD_TEMPLATE_NAME_TEMPLATE (templ));
  if (GST_PAD_TEMPLATE_CAPS (templ)) {
    gst_caps_free (GST_PAD_TEMPLATE_CAPS (templ));
  }

  G_OBJECT_CLASS (padtemplate_parent_class)->dispose (object);
}

/* ALWAYS padtemplates cannot have conversion specifications, it doesn't make
 * sense.
 * SOMETIMES padtemplates can do whatever they want, they are provided by the
 * element.
 * REQUEST padtemplates can be reverse-parsed (the user asks for 'sink1', the
 * 'sink%d' template is automatically selected), so we need to restrict their
 * naming.
 */
static gboolean
name_is_valid (const gchar * name, GstPadPresence presence)
{
  const gchar *str;

  if (presence == GST_PAD_ALWAYS) {
    if (strchr (name, '%')) {
      g_warning ("invalid name template %s: conversion specifications are not"
          " allowed for GST_PAD_ALWAYS padtemplates", name);
      return FALSE;
    }
  } else if (presence == GST_PAD_REQUEST) {
    if ((str = strchr (name, '%')) && strchr (str + 1, '%')) {
      g_warning ("invalid name template %s: only one conversion specification"
          " allowed in GST_PAD_REQUEST padtemplate", name);
      return FALSE;
    }
    if (str && (*(str + 1) != 's' && *(str + 1) != 'd')) {
      g_warning ("invalid name template %s: conversion specification must be of"
          " type '%%d' or '%%s' for GST_PAD_REQUEST padtemplate", name);
      return FALSE;
    }
    if (str && (*(str + 2) != '\0')) {
      g_warning ("invalid name template %s: conversion specification must"
          " appear at the end of the GST_PAD_REQUEST padtemplate name", name);
      return FALSE;
    }
  }

  return TRUE;
}

/**
 * gst_static_pad_template_get:
 * @pad_template: the static pad template
 *
 * Converts a #GstStaticPadTemplate into a #GstPadTemplate.
 *
 * Returns: a new #GstPadTemplate.
 */
GstPadTemplate *
gst_static_pad_template_get (GstStaticPadTemplate * pad_template)
{
  GstPadTemplate *new;

  if (!name_is_valid (pad_template->name_template, pad_template->presence))
    return NULL;

  new = g_object_new (gst_pad_template_get_type (),
      "name", pad_template->name_template, NULL);

  GST_PAD_TEMPLATE_NAME_TEMPLATE (new) = g_strdup (pad_template->name_template);
  GST_PAD_TEMPLATE_DIRECTION (new) = pad_template->direction;
  GST_PAD_TEMPLATE_PRESENCE (new) = pad_template->presence;

  GST_PAD_TEMPLATE_CAPS (new) =
      gst_caps_copy (gst_static_caps_get (&pad_template->static_caps));

  return new;
}

/**
 * gst_pad_template_new:
 * @name_template: the name template.
 * @direction: the #GstPadDirection of the template.
 * @presence: the #GstPadPresence of the pad.
 * @caps: a #GstCaps set for the template. The caps are taken ownership of.
 *
 * Creates a new pad template with a name according to the given template
 * and with the given arguments. This functions takes ownership of the provided
 * caps, so be sure to not use them afterwards.
 *
 * Returns: a new #GstPadTemplate.
 */
GstPadTemplate *
gst_pad_template_new (const gchar * name_template,
    GstPadDirection direction, GstPadPresence presence, GstCaps * caps)
{
  GstPadTemplate *new;

  g_return_val_if_fail (name_template != NULL, NULL);

  if (!name_is_valid (name_template, presence))
    return NULL;

#if 0
#ifdef USE_POISONING
  if (caps) {
    GstCaps *newcaps = gst_caps_copy (caps);

    gst_caps_free (caps);
    caps = newcaps;
  }
#endif
#endif
  new = g_object_new (gst_pad_template_get_type (),
      "name", name_template, NULL);

  GST_PAD_TEMPLATE_NAME_TEMPLATE (new) = g_strdup (name_template);
  GST_PAD_TEMPLATE_DIRECTION (new) = direction;
  GST_PAD_TEMPLATE_PRESENCE (new) = presence;
  GST_PAD_TEMPLATE_CAPS (new) = caps;

  return new;
}

/**
 * gst_pad_template_get_caps:
 * @templ: a #GstPadTemplate to get capabilities of.
 *
 * Gets the capabilities of the pad template.
 *
 * Returns: the #GstCaps of the pad template. If you need to keep a reference to
 * the caps, make a copy (see gst_caps_copy ()).
 */
const GstCaps *
gst_pad_template_get_caps (GstPadTemplate * templ)
{
  g_return_val_if_fail (GST_IS_PAD_TEMPLATE (templ), NULL);

  return GST_PAD_TEMPLATE_CAPS (templ);
}

/**
 * gst_pad_set_element_private:
 * @pad: the #GstPad to set the private data of.
 * @priv: The private data to attach to the pad.
 *
 * Set the given private data gpointer on the pad. 
 * This function can only be used by the element that owns the pad.
 */
void
gst_pad_set_element_private (GstPad * pad, gpointer priv)
{
  pad->element_private = priv;
}

/**
 * gst_pad_get_element_private:
 * @pad: the #GstPad to get the private data of.
 *
 * Gets the private data of a pad.
 *
 * Returns: a #gpointer to the private data.
 */
gpointer
gst_pad_get_element_private (GstPad * pad)
{
  return pad->element_private;
}


/***** ghost pads *****/
GType _gst_ghost_pad_type = 0;

static void gst_ghost_pad_class_init (GstGhostPadClass * klass);
static void gst_ghost_pad_init (GstGhostPad * pad);
static void gst_ghost_pad_dispose (GObject * object);
static void gst_ghost_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_ghost_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static GstPad *ghost_pad_parent_class = NULL;

/* static guint gst_ghost_pad_signals[LAST_SIGNAL] = { 0 }; */
enum
{
  GPAD_ARG_0,
  GPAD_ARG_REAL_PAD
      /* fill me */
};

GType
gst_ghost_pad_get_type (void)
{
  if (!_gst_ghost_pad_type) {
    static const GTypeInfo pad_info = {
      sizeof (GstGhostPadClass), NULL, NULL,
      (GClassInitFunc) gst_ghost_pad_class_init, NULL, NULL,
      sizeof (GstGhostPad),
      0,
      (GInstanceInitFunc) gst_ghost_pad_init,
      NULL
    };

    _gst_ghost_pad_type = g_type_register_static (GST_TYPE_PAD, "GstGhostPad",
        &pad_info, 0);
  }
  return _gst_ghost_pad_type;
}

static void
gst_ghost_pad_class_init (GstGhostPadClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  ghost_pad_parent_class = g_type_class_ref (GST_TYPE_PAD);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_ghost_pad_dispose);
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_ghost_pad_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_ghost_pad_get_property);

  g_object_class_install_property (gobject_class, GPAD_ARG_REAL_PAD,
      g_param_spec_object ("real-pad", "Real pad",
          "The real pad for the ghost pad", GST_TYPE_PAD, G_PARAM_READWRITE));
}

static void
gst_ghost_pad_init (GstGhostPad * pad)
{
  /* zeroed by glib */
}

static void
gst_ghost_pad_dispose (GObject * object)
{
  g_object_set (object, "real-pad", NULL, NULL);

  G_OBJECT_CLASS (ghost_pad_parent_class)->dispose (object);
}

static void
gst_ghost_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPad *ghostpad = (GstPad *) object;
  GstPad *oldrealpad = (GstPad *) GST_GPAD_REALPAD (ghostpad);
  GstPad *realpad = NULL;

  switch (prop_id) {
    case GPAD_ARG_REAL_PAD:
      realpad = g_value_get_object (value);

      if (oldrealpad) {
        if (realpad == oldrealpad)
          return;
        else
          gst_pad_remove_ghost_pad (oldrealpad, ghostpad);
      }

      if (realpad)
        gst_pad_add_ghost_pad (realpad, ghostpad);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ghost_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    case GPAD_ARG_REAL_PAD:
      g_value_set_object (value, GST_GPAD_REALPAD (object));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 * gst_ghost_pad_new:
 * @name: the name of the new ghost pad.
 * @pad: the #GstPad to create a ghost pad for.
 *
 * Creates a new ghost pad associated with @pad, and named @name. If @name is
 * %NULL, a guaranteed unique name (across all ghost pads) will be assigned.
 *
 * Returns: a new ghost #GstPad, or %NULL in case of an error.
 */
GstPad *
gst_ghost_pad_new (const gchar * name, GstPad * pad)
{
  GstPad *gpad;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  gpad = g_object_new (GST_TYPE_GHOST_PAD, "name", name, "real-pad", pad, NULL);

  GST_CAT_DEBUG (GST_CAT_PADS, "created ghost pad \"%s\" for pad %s:%s",
      GST_OBJECT_NAME (gpad), GST_DEBUG_PAD_NAME (pad));

  return gpad;
}

/**
 * gst_pad_get_internal_links_default:
 * @pad: the #GstPad to get the internal links of.
 *
 * Gets a list of pads to which the given pad is linked to
 * inside of the parent element.
 * This is the default handler, and thus returns a list of all of the
 * pads inside the parent element with opposite direction.
 * The caller must free this list after use.
 *
 * Returns: a newly allocated #GList of pads.
 */
GList *
gst_pad_get_internal_links_default (GstPad * pad)
{
  GList *res = NULL;
  GstElement *parent;
  GList *parent_pads;
  GstPadDirection direction;
  GstRealPad *rpad;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);

  rpad = GST_PAD_REALIZE (pad);
  direction = rpad->direction;

  parent = GST_PAD_PARENT (rpad);
  parent_pads = parent->pads;

  while (parent_pads) {
    GstRealPad *parent_pad = GST_PAD_REALIZE (parent_pads->data);

    if (parent_pad->direction != direction) {
      res = g_list_prepend (res, parent_pad);
    }

    parent_pads = g_list_next (parent_pads);
  }

  return res;
}

/**
 * gst_pad_get_internal_links:
 * @pad: the #GstPad to get the internal links of.
 *
 * Gets a list of pads to which the given pad is linked to
 * inside of the parent element.
 * The caller must free this list after use.
 *
 * Returns: a newly allocated #GList of pads.
 */
GList *
gst_pad_get_internal_links (GstPad * pad)
{
  GList *res = NULL;
  GstRealPad *rpad;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  rpad = GST_PAD_REALIZE (pad);

  if (GST_RPAD_INTLINKFUNC (rpad))
    res = GST_RPAD_INTLINKFUNC (rpad) (GST_PAD (rpad));

  return res;
}


static gboolean
gst_pad_event_default_dispatch (GstPad * pad, GstElement * element,
    GstEvent * event)
{
  GList *orig, *pads;

  GST_INFO_OBJECT (pad, "Sending event %p to all internally linked pads",
      event);

  orig = pads = gst_pad_get_internal_links (pad);

  while (pads) {
    GstPad *eventpad = GST_PAD (pads->data);

    pads = g_list_next (pads);

    /* for all of the internally-linked pads that are actually linked */
    if (GST_PAD_IS_LINKED (eventpad)) {
      if (GST_PAD_DIRECTION (eventpad) == GST_PAD_SRC) {
        /* increase the refcount */
        gst_event_ref (event);
        gst_pad_push (eventpad, GST_DATA (event));
      } else {
        GstPad *peerpad = GST_PAD (GST_RPAD_PEER (eventpad));

        /* we only send the event on one pad, multi-sinkpad elements 
         * should implement a handler */
        g_list_free (orig);
        return gst_pad_send_event (peerpad, event);
      }
    }
  }
  gst_event_unref (event);
  g_list_free (orig);
  return (GST_PAD_DIRECTION (pad) == GST_PAD_SINK);
}

/**
 * gst_pad_event_default:
 * @pad: a #GstPad to call the default event handler on.
 * @event: the #GstEvent to handle.
 *
 * Invokes the default event handler for the given pad. End-of-stream and
 * discontinuity events are handled specially, and then the event is sent to all
 * pads internally linked to @pad. Note that if there are many possible sink
 * pads that are internally linked to @pad, only one will be sent an event.
 * Multi-sinkpad elements should implement custom event handlers.
 *
 * Returns: TRUE if the event was sent succesfully.
 */
gboolean
gst_pad_event_default (GstPad * pad, GstEvent * event)
{
  GstElement *element;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  element = GST_PAD_PARENT (pad);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      gst_pad_event_default_dispatch (pad, element, event);
      gst_element_set_eos (element);
      break;
    case GST_EVENT_DISCONTINUOUS:
    {
      guint64 time;

      if (gst_element_requires_clock (element) && element->clock) {
        if (gst_event_discont_get_value (event, GST_FORMAT_TIME, &time)) {
          gst_element_set_time (element, time);
        } else {
          GstFormat format = GST_FORMAT_TIME;
          guint i;

          for (i = 0; i < event->event_data.discont.noffsets; i++) {
            if (gst_pad_convert (pad,
                    event->event_data.discont.offsets[i].format,
                    event->event_data.discont.offsets[i].value, &format,
                    &time)) {
              gst_element_set_time (element, time);
            } else if (i == event->event_data.discont.noffsets) {
              g_warning
                  ("can't adjust clock to new time when time not provided");
            }
          }
        }
      }
    }
    default:
      return gst_pad_event_default_dispatch (pad, element, event);
  }
  return TRUE;
}

/**
 * gst_pad_dispatcher:
 * @pad: a #GstPad to dispatch.
 * @dispatch: the #GstDispatcherFunction to call.
 * @data: gpointer user data passed to the dispatcher function.
 *
 * Invokes the given dispatcher function on all pads that are 
 * internally linked to the given pad. 
 * The GstPadDispatcherFunction should return TRUE when no further pads 
 * need to be processed.
 *
 * Returns: TRUE if one of the dispatcher functions returned TRUE.
 */
gboolean
gst_pad_dispatcher (GstPad * pad, GstPadDispatcherFunction dispatch,
    gpointer data)
{
  gboolean res = FALSE;
  GList *int_pads, *orig;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  g_return_val_if_fail (dispatch != NULL, FALSE);

  orig = int_pads = gst_pad_get_internal_links (pad);

  while (int_pads) {
    GstRealPad *int_rpad = GST_PAD_REALIZE (int_pads->data);
    GstRealPad *int_peer = GST_RPAD_PEER (int_rpad);

    if (int_peer) {
      res = dispatch (GST_PAD (int_peer), data);
      if (res)
        break;
    }
    int_pads = g_list_next (int_pads);
  }

  g_list_free (orig);

  return res;
}

/**
 * gst_pad_send_event:
 * @pad: a #GstPad to send the event to.
 * @event: the #GstEvent to send to the pad.
 *
 * Sends the event to the pad.
 *
 * Returns: TRUE if the event was handled.
 */
gboolean
gst_pad_send_event (GstPad * pad, GstEvent * event)
{
  gboolean success = FALSE;
  GstRealPad *rpad;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  g_return_val_if_fail (event != NULL, FALSE);

  rpad = GST_PAD_REALIZE (pad);

  if (GST_EVENT_SRC (event) == NULL)
    GST_EVENT_SRC (event) = gst_object_ref (GST_OBJECT (rpad));

  GST_CAT_DEBUG (GST_CAT_EVENT, "have event %d on pad %s:%s",
      GST_EVENT_TYPE (event), GST_DEBUG_PAD_NAME (rpad));

  if (GST_RPAD_EVENTHANDLER (rpad))
    success = GST_RPAD_EVENTHANDLER (rpad) (GST_PAD (rpad), event);
  else {
    g_warning ("pad %s:%s has no event handler", GST_DEBUG_PAD_NAME (rpad));
    gst_event_unref (event);
  }

  return success;
}

typedef struct
{
  GstFormat src_format;
  gint64 src_value;
  GstFormat *dest_format;
  gint64 *dest_value;
}
GstPadConvertData;

static gboolean
gst_pad_convert_dispatcher (GstPad * pad, GstPadConvertData * data)
{
  return gst_pad_convert (pad, data->src_format, data->src_value,
      data->dest_format, data->dest_value);
}

/**
 * gst_pad_convert_default:
 * @pad: a #GstPad to invoke the default converter on.
 * @src_format: the source #GstFormat.
 * @src_value: the source value.
 * @dest_format: a pointer to the destination #GstFormat.
 * @dest_value: a pointer to the destination value.
 *
 * Invokes the default converter on a pad. 
 * This will forward the call to the pad obtained 
 * using the internal link of
 * the element.
 *
 * Returns: TRUE if the conversion could be performed.
 */
gboolean
gst_pad_convert_default (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  GstPadConvertData data;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  g_return_val_if_fail (dest_format != NULL, FALSE);
  g_return_val_if_fail (dest_value != NULL, FALSE);

  data.src_format = src_format;
  data.src_value = src_value;
  data.dest_format = dest_format;
  data.dest_value = dest_value;

  return gst_pad_dispatcher (pad, (GstPadDispatcherFunction)
      gst_pad_convert_dispatcher, &data);
}

/**
 * gst_pad_convert:
 * @pad: a #GstPad to invoke the default converter on.
 * @src_format: the source #GstFormat.
 * @src_value: the source value.
 * @dest_format: a pointer to the destination #GstFormat.
 * @dest_value: a pointer to the destination value.
 *
 * Invokes a conversion on the pad.
 *
 * Returns: TRUE if the conversion could be performed.
 */
gboolean
gst_pad_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  GstRealPad *rpad;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  g_return_val_if_fail (dest_format != NULL, FALSE);
  g_return_val_if_fail (dest_value != NULL, FALSE);

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  rpad = GST_PAD_REALIZE (pad);

  if (GST_RPAD_CONVERTFUNC (rpad)) {
    return GST_RPAD_CONVERTFUNC (rpad) (GST_PAD (rpad), src_format,
        src_value, dest_format, dest_value);
  }

  return FALSE;
}

typedef struct
{
  GstQueryType type;
  GstFormat *format;
  gint64 *value;
}
GstPadQueryData;

static gboolean
gst_pad_query_dispatcher (GstPad * pad, GstPadQueryData * data)
{
  return gst_pad_query (pad, data->type, data->format, data->value);
}

/**
 * gst_pad_query_default:
 * @pad: a #GstPad to invoke the default query on.
 * @type: the #GstQueryType of the query to perform.
 * @format: a pointer to the #GstFormat of the result.
 * @value: a pointer to the result.
 *
 * Invokes the default query function on a pad. 
 *
 * Returns: TRUE if the query could be performed.
 */
gboolean
gst_pad_query_default (GstPad * pad, GstQueryType type,
    GstFormat * format, gint64 * value)
{
  GstPadQueryData data;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  g_return_val_if_fail (format != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  data.type = type;
  data.format = format;
  data.value = value;

  return gst_pad_dispatcher (pad, (GstPadDispatcherFunction)
      gst_pad_query_dispatcher, &data);
}

/**
 * gst_pad_query:
 * @pad: a #GstPad to invoke the default query on.
 * @type: the #GstQueryType of the query to perform.
 * @format: a pointer to the #GstFormat asked for.
 *          On return contains the #GstFormat used.
 * @value: a pointer to the result.
 *
 * Queries a pad for one of the available properties. The format will be
 * adjusted to the actual format used when specifying formats such as 
 * GST_FORMAT_DEFAULT.
 * FIXME: Tell if the format can be adjusted when specifying a definite format.
 *
 * Returns: TRUE if the query could be performed.
 */
gboolean
gst_pad_query (GstPad * pad, GstQueryType type,
    GstFormat * format, gint64 * value)
{
  GstRealPad *rpad;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  g_return_val_if_fail (format != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  rpad = GST_PAD_REALIZE (pad);

  g_return_val_if_fail (rpad, FALSE);

  if (GST_RPAD_QUERYFUNC (rpad))
    return GST_RPAD_QUERYFUNC (rpad) (GST_PAD (pad), type, format, value);

  return FALSE;
}

static gboolean
gst_pad_get_formats_dispatcher (GstPad * pad, const GstFormat ** data)
{
  *data = gst_pad_get_formats (pad);

  return TRUE;
}

/**
 * gst_pad_get_formats_default:
 * @pad: a #GstPad to query
 *
 * Invoke the default format dispatcher for the pad.
 *
 * Returns: An array of GstFormats ended with a 0 value.
 */
const GstFormat *
gst_pad_get_formats_default (GstPad * pad)
{
  GstFormat *result = NULL;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  gst_pad_dispatcher (pad, (GstPadDispatcherFunction)
      gst_pad_get_formats_dispatcher, &result);

  return result;
}

/**
 * gst_pad_get_formats:
 * @pad: a #GstPad to query
 *
 * Gets the list of supported formats from the pad.
 *
 * Returns: An array of GstFormats ended with a 0 value.
 */
const GstFormat *
gst_pad_get_formats (GstPad * pad)
{
  GstRealPad *rpad;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  rpad = GST_PAD_REALIZE (pad);

  if (GST_RPAD_FORMATSFUNC (rpad))
    return GST_RPAD_FORMATSFUNC (rpad) (GST_PAD (pad));

  return NULL;
}
