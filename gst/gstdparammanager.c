/* GStreamer
 * Copyright (C) 2001 Steve Baker <stevebaker_org@yahoo.co.uk>
 *
 * gstdparammanager.c: Dynamic Parameter group functionality
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "gst_private.h"

#include "gstdparammanager.h"
#include <gst/gstelement.h>


static void gst_dpman_base_class_init (GstDParamManagerClass *klass);
static void gst_dpman_class_init (GstDParamManagerClass *klass);
static void gst_dpman_init (GstDParamManager *dpman);
static void gst_dpman_state_change (GstElement *element, gint state, GstDParamManager *dpman);
static void gst_dpman_caps_changed (GstPad *pad, GstCaps *caps, GstDParamManager *dpman);
static guint gst_dpman_first_countdown_synchronous(GstDParamManager *dpman, guint frames, gint64 timestamp);
static guint gst_dpman_first_countdown_noop(GstDParamManager *dpman, guint frames, gint64 timestamp);
static guint gst_dpman_countdown_noop(GstDParamManager *dpman, guint frame_count);

GType
gst_dpman_get_type (void)
{
	static GType dpman_type = 0;

	if (!dpman_type) {
		static const GTypeInfo dpman_info = {
			sizeof(GstDParamManagerClass),
			(GBaseInitFunc)gst_dpman_base_class_init,
			NULL,
			(GClassInitFunc)gst_dpman_class_init,
			NULL,
			NULL,
			sizeof(GstDParamManager),
			0,
			(GInstanceInitFunc)gst_dpman_init,
		};
		dpman_type = g_type_register_static(GST_TYPE_OBJECT, "GstDParamManager", &dpman_info, 0);
	}
	return dpman_type;
}

static void
gst_dpman_base_class_init (GstDParamManagerClass *klass)
{
	GObjectClass *gobject_class;

	gobject_class = (GObjectClass*) klass;

}

static void
gst_dpman_class_init (GstDParamManagerClass *klass)
{
	GstObjectClass *gstobject_class;
	GObjectClass *gobject_class;

	gstobject_class = (GstObjectClass*) klass;
	gobject_class = (GObjectClass*) klass;

	klass->modes = g_hash_table_new(g_str_hash,g_str_equal);

	gst_dpman_register_mode (klass, "synchronous", 
	                       gst_dpman_first_countdown_synchronous, gst_dpman_countdown_noop, NULL, NULL);
	gst_dpman_register_mode (klass, "asynchronous", 
	                       gst_dpman_first_countdown_noop, gst_dpman_countdown_noop, NULL, NULL);
	gst_dpman_register_mode (klass, "disabled", 
	                       gst_dpman_first_countdown_noop, gst_dpman_countdown_noop, NULL, NULL);

}

static void
gst_dpman_init (GstDParamManager *dpman)
{
	GST_DPMAN_DPARAMS(dpman) = g_hash_table_new(g_str_hash,g_str_equal);
	GST_DPMAN_DPARAMS_LIST(dpman) = NULL;
	GST_DPMAN_NAME(dpman) = NULL;
	GST_DPMAN_PARENT(dpman) = NULL;
	GST_DPMAN_MODE_NAME(dpman) = NULL;
	GST_DPMAN_MODE(dpman) = NULL;
	GST_DPMAN_MODE_DATA(dpman) = NULL;
	GST_DPMAN_RATE(dpman) = 0;
}

/**
 * gst_dpman_new:
 * @name: name of the GstDParamManager instance
 * @parent: element which created this instance
 *
 * Returns: a new instance of GstDParamManager
 */
GstDParamManager* 
gst_dpman_new (gchar *name, GstElement *parent)
{
	GstDParamManager *dpman;
	
	g_return_val_if_fail (name != NULL, NULL);

	dpman = gtk_type_new (gst_dpman_get_type ());
	gst_object_set_name (GST_OBJECT (dpman), name);
	gst_dpman_set_parent(dpman, parent);

	gst_dpman_set_mode(dpman, "disabled");

	return dpman;
}

/**
 * gst_dpman_add_required_dparam:
 * @dpman: GstDParamManager instance
 * @dparam_name: a parameter name unique to this GstDParamManager
 * @type: the GValue type that this parameter will store
 * @update_func: callback to update the element with the new value
 * @update_data: will be included in the call to update_func
 *
 * Returns: true if it was successfully added
 */
gboolean 
gst_dpman_add_required_dparam (GstDParamManager *dpman, 
                                gchar *dparam_name, 
                                GType type, 
                                GstDpmUpdateFunction update_func, 
                                gpointer update_data)
{
	GstDParamWrapper* dpwrap;

	g_return_val_if_fail (dpman != NULL, FALSE);
	g_return_val_if_fail (GST_IS_DPMAN (dpman), FALSE);
	g_return_val_if_fail (dparam_name != NULL, FALSE);
	g_return_val_if_fail (update_func != NULL, FALSE);

	g_return_val_if_fail(g_hash_table_lookup(GST_DPMAN_DPARAMS(dpman), dparam_name) == NULL, FALSE);

	GST_DEBUG(GST_CAT_PARAMS,"adding required dparam: %s\n", dparam_name);
	
	dpwrap = g_new0(GstDParamWrapper,1);
	dpwrap->dparam_name = dparam_name;
	dpwrap->update_func = update_func;
	dpwrap->update_data = update_data;
	dpwrap->value = g_new0(GValue,1);
	g_value_init(dpwrap->value, type);

	g_hash_table_insert(GST_DPMAN_DPARAMS(dpman), dparam_name, dpwrap);
	GST_DPMAN_DPARAMS_LIST(dpman) = g_slist_append(GST_DPMAN_DPARAMS_LIST(dpman), dpwrap);
	
	return TRUE;	
}

/**
 * gst_dpman_remove_required_dparam:
 * @dpman: GstDParamManager instance
 * @dparam_name: the name of an existing parameter
 *
 */
void 
gst_dpman_remove_required_dparam (GstDParamManager *dpman, gchar *dparam_name)
{
	GstDParamWrapper* dpwrap;

	g_return_if_fail (dpman != NULL);
	g_return_if_fail (GST_IS_DPMAN (dpman));
	g_return_if_fail (dparam_name != NULL);

	dpwrap = g_hash_table_lookup(GST_DPMAN_DPARAMS(dpman), dparam_name);

	g_return_if_fail(dpwrap != NULL);
	g_return_if_fail(dpwrap->dparam == NULL);

	GST_DEBUG(GST_CAT_PARAMS, "removing required dparam: %s\n", dparam_name);
	
	g_hash_table_remove(GST_DPMAN_DPARAMS(dpman), dparam_name);
	GST_DPMAN_DPARAMS_LIST(dpman) = g_slist_remove(GST_DPMAN_DPARAMS_LIST(dpman), dpwrap);

	g_free(dpwrap->value);
	g_free(dpwrap);
}

/**
 * gst_dpman_attach_dparam:
 * @dpman: GstDParamManager instance
 * @dparam_name: a name previously added with gst_dpman_add_required_dparam
 * @dparam: GstDParam instance to attach
 *
 * Returns: true if it was successfully attached
 */
gboolean 
gst_dpman_attach_dparam (GstDParamManager *dpman, gchar *dparam_name, GstDParam *dparam)
{
	GstDParamWrapper* dpwrap;

	g_return_val_if_fail (dpman != NULL, FALSE);
	g_return_val_if_fail (GST_IS_DPMAN (dpman), FALSE);
	g_return_val_if_fail (dparam_name != NULL, FALSE);
	g_return_val_if_fail (dparam != NULL, FALSE);
	g_return_val_if_fail (GST_IS_DPARAM (dparam), FALSE);
	g_return_val_if_fail (dparam != NULL, FALSE);

	dpwrap = g_hash_table_lookup(GST_DPMAN_DPARAMS(dpman), dparam_name);

	g_return_val_if_fail(dpwrap != NULL, FALSE);
	g_return_val_if_fail(dpwrap->value != NULL, FALSE);

	GST_DPARAM_VALUE(dparam) = dpwrap->value;
	dpwrap->dparam = dparam;
	gst_dparam_set_parent (dparam, GST_OBJECT(dpman));
	GST_DPARAM_NAME(dparam)	= dparam_name;

	return TRUE;
}

/**
 * gst_dpman_dettach_dparam:
 * @dpman: GstDParamManager instance
 * @dparam_name: the name of a parameter with a previously attached GstDParam
 *
 */
void 
gst_dpman_dettach_dparam (GstDParamManager *dpman, gchar *dparam_name)
{
	GstDParamWrapper* dpwrap;

	g_return_if_fail (dpman != NULL);
	g_return_if_fail (GST_IS_DPMAN (dpman));
	g_return_if_fail (dparam_name != NULL);
	
	dpwrap = g_hash_table_lookup(GST_DPMAN_DPARAMS(dpman), dparam_name);

	g_return_if_fail(dpwrap);
	
	GST_DPARAM_VALUE(dpwrap->dparam) = NULL;
	GST_DPARAM_NAME(dpwrap->dparam)	= NULL;
	gst_object_unparent (GST_OBJECT(dpwrap->dparam));

	dpwrap->dparam = NULL;
	
}

/**
 * gst_dpman_get_dparam:
 * @dpman: GstDParamManager instance
 * @name: the name of an existing dparam instance
 *
 * Returns: the dparam with the given name - or NULL otherwise
 */
GstDParam *
gst_dpman_get_dparam (GstDParamManager *dpman, gchar *name)
{
	GstDParamWrapper* dpwrap;

	g_return_val_if_fail (dpman != NULL, NULL);
	g_return_val_if_fail (GST_IS_DPMAN (dpman), NULL);
	g_return_val_if_fail (name != NULL, NULL);
	
	dpwrap = g_hash_table_lookup(GST_DPMAN_DPARAMS(dpman), name);
	g_return_val_if_fail (dpwrap != NULL, NULL);
	
	return dpwrap->dparam;
}

/**
 * gst_dpman_register_mode
 * @klass: GstDParamManagerClass class instance
 * @modename: the unique name of the new mode
 * @firstcountdownfunc: the function which will be called before each buffer is processed
 * @countdownfunc: the function which may be called throughout the processing of a buffer
 * @setupfunc: the function which initialises the mode when activated
 * @teardownfunc: the function which frees any resources the mode uses
 *
 */
void
gst_dpman_register_mode (GstDParamManagerClass *klass,
                         gchar *modename, 
                         GstDpmModeFirstCountdownFunction firstcountdownfunc,
                         GstDpmModeCountdownFunction countdownfunc,
                         GstDpmModeSetupFunction setupfunc,
                         GstDpmModeTeardownFunction teardownfunc)
{
	GstDpmMode *mode;

	g_return_if_fail (klass != NULL);
	g_return_if_fail (modename != NULL);
	g_return_if_fail (GST_IS_DPMAN_CLASS (klass));
	
	mode = g_new0(GstDpmMode,1);

	mode->firstcountdownfunc = firstcountdownfunc;
	mode->countdownfunc = countdownfunc;
	mode->setupfunc = setupfunc;
	mode->teardownfunc = teardownfunc;
	
	g_hash_table_insert(klass->modes, modename, mode);
	GST_DEBUG(GST_CAT_PARAMS, "mode '%s' registered\n", modename);
}

/**
 * gst_dpman_set_mode
 * @dpman: GstDParamManager instance
 * @modename: the name of the mode to use
 *
 * Returns: TRUE if the mode was set, FALSE otherwise
 */
gboolean
gst_dpman_set_mode(GstDParamManager *dpman, gchar *modename)
{
	GstDpmMode *mode=NULL;
	GstDParamManagerClass *oclass;
	
	g_return_val_if_fail (dpman != NULL, FALSE);
	g_return_val_if_fail (GST_IS_DPMAN (dpman), FALSE);
	g_return_val_if_fail (modename != NULL, FALSE);

	oclass = (GstDParamManagerClass*)(G_OBJECT_GET_CLASS(dpman));
	
	mode = g_hash_table_lookup(oclass->modes, modename);
	g_return_val_if_fail (mode != NULL, FALSE);
	GST_DEBUG(GST_CAT_PARAMS, "setting mode to %s\n", modename);
	if (GST_DPMAN_MODE(dpman) && GST_DPMAN_TEARDOWNFUNC(dpman)){
		GST_DPMAN_TEARDOWNFUNC(dpman)(dpman);
	}
	
	GST_DPMAN_MODE(dpman) = mode;

	if (GST_DPMAN_SETUPFUNC(dpman)){
		GST_DPMAN_SETUPFUNC(dpman)(dpman);
	}
	
	return TRUE;
}

/**
 * gst_dpman_set_parent
 * @dpman: GstDParamManager instance
 * @parent: the element that this GstDParamManager belongs to
 *
 */
void
gst_dpman_set_parent (GstDParamManager *dpman, GstElement *parent)
{
	g_return_if_fail (dpman != NULL);
	g_return_if_fail (GST_IS_DPMAN (dpman));
	g_return_if_fail (parent != NULL);
	g_return_if_fail (GST_IS_ELEMENT (parent));

	gst_object_set_parent (GST_OBJECT (dpman), GST_OBJECT(parent));
	g_signal_connect(G_OBJECT(parent), "state_change", 
	                 G_CALLBACK (gst_dpman_state_change), dpman);
}

/**
 * gst_dpman_set_rate_change_pad
 * @dpman: GstDParamManager instance
 * @pad: the pad which may have a "rate" caps property
 *
 */
void
gst_dpman_set_rate_change_pad(GstDParamManager *dpman, GstPad *pad)
{
	g_return_if_fail (dpman != NULL);
	g_return_if_fail (GST_IS_DPMAN (dpman));
	g_return_if_fail (pad != NULL);
	g_return_if_fail (GST_IS_PAD (pad));

	g_signal_connect(G_OBJECT(pad), "caps_changed", 
	                 G_CALLBACK (gst_dpman_caps_changed), dpman);
}

static void 
gst_dpman_state_change (GstElement *element, gint state, GstDParamManager *dpman)
{
	GSList *dwraps;
	GstDParam *dparam;
	GstDParamWrapper *dpwrap;
	
	if (state == GST_STATE_PLAYING) return;
	GST_DEBUG(GST_CAT_PARAMS, "initialising params\n");
	
	g_return_if_fail (dpman != NULL);
	g_return_if_fail (GST_IS_DPMAN (dpman));

	// force all params to be updated
	dwraps = GST_DPMAN_DPARAMS_LIST(dpman);
	while (dwraps){
		dpwrap = (GstDParamWrapper*)dwraps->data;
		dparam = dpwrap->dparam;
		
		if (dparam){
			GST_DPARAM_READY_FOR_UPDATE(dparam) = TRUE;
		}
		dwraps = g_slist_next(dwraps);
	}
}

static void
gst_dpman_caps_changed (GstPad *pad, GstCaps *caps, GstDParamManager *dpman)
{
	g_return_if_fail (caps != NULL);
	g_return_if_fail (dpman != NULL);
	g_return_if_fail (GST_IS_DPMAN (dpman));
	
	GST_DPMAN_RATE(dpman) = gst_caps_get_int (caps, "rate");
	
	GST_DEBUG(GST_CAT_PARAMS, "got caps change %d\n", GST_DPMAN_RATE(dpman));
}

static guint 
gst_dpman_first_countdown_synchronous(GstDParamManager *dpman, guint frames, gint64 timestamp)
{
	GSList *dwraps;
	GstDParam *dparam;
	GstDParamWrapper *dpwrap;

	g_return_val_if_fail (dpman != NULL, frames);
	g_return_val_if_fail (GST_IS_DPMAN (dpman), frames);

	// now check whether any passive dparams are ready for an update
	dwraps = GST_DPMAN_DPARAMS_LIST(dpman);
	while (dwraps){
		dpwrap = (GstDParamWrapper*)dwraps->data;
		dparam = dpwrap->dparam;
		
		if (dparam && (GST_DPARAM_READY_FOR_UPDATE(dparam) || 
		              (GST_DPARAM_NEXT_UPDATE_TIMESTAMP(dparam) < timestamp))){
			GST_DPARAM_DO_UPDATE(dparam, timestamp);
			GST_DPMAN_DO_UPDATE(dpwrap);
		}
		dwraps = g_slist_next(dwraps);
	}
	
	return frames;
}

static guint 
gst_dpman_first_countdown_noop(GstDParamManager *dpman, guint frames, gint64 timestamp)
{
	return frames;
}

static guint 
gst_dpman_countdown_noop(GstDParamManager *dpman, guint frame_count)
{
	return 0;
}

