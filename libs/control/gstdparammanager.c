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

#include "gstdparammanager.h"
#include <gst/gstelement.h>
#include <gst/gstinfo.h>

static GHashTable *_element_registry;

static void gst_dpman_class_init (GstDParamManagerClass *klass);
static void gst_dpman_init (GstDParamManager *dpman);
static void gst_dpman_dispose (GObject *object);
static GstDParamWrapper* gst_dpman_new_wrapper(GstDParamManager *dpman, gchar *dparam_name, GType type, GstDPMUpdateMethod update_method);
static GstDParamWrapper* gst_dpman_get_wrapper(GstDParamManager *dpman, gchar *dparam_name);
static void gst_dpman_state_change (GstElement *element, gint state, GstDParamManager *dpman);
static void gst_dpman_caps_changed (GstPad *pad, GstCaps *caps, GstDParamManager *dpman);
static guint gst_dpman_preprocess_synchronous(GstDParamManager *dpman, guint frames, gint64 timestamp);
static guint gst_dpman_preprocess_noop(GstDParamManager *dpman, guint frames, gint64 timestamp);
static guint gst_dpman_process_noop(GstDParamManager *dpman, guint frame_count);

static GObjectClass *parent_class;

void 
_gst_dpman_initialize()
{
}

GType
gst_dpman_get_type (void)
{
	static GType dpman_type = 0;

	if (!dpman_type) {
		static const GTypeInfo dpman_info = {
			sizeof(GstDParamManagerClass),
			NULL,
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
gst_dpman_class_init (GstDParamManagerClass *klass)
{
	GstObjectClass *gstobject_class;
	GObjectClass *gobject_class;

	parent_class = g_type_class_peek_parent (klass);

	gstobject_class = (GstObjectClass*) klass;
	gobject_class = (GObjectClass*) klass;
	gobject_class->dispose = gst_dpman_dispose;

	klass->modes = g_hash_table_new(g_str_hash,g_str_equal);

	gst_dpman_register_mode (klass, "synchronous", 
	                       gst_dpman_preprocess_synchronous, gst_dpman_process_noop, NULL, NULL);
	gst_dpman_register_mode (klass, "asynchronous", 
	                       gst_dpman_preprocess_noop, gst_dpman_process_noop, NULL, NULL);
	gst_dpman_register_mode (klass, "disabled", 
	                       gst_dpman_preprocess_noop, gst_dpman_process_noop, NULL, NULL);

	_element_registry = g_hash_table_new(NULL,NULL);
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

	dpman = g_object_new (gst_dpman_get_type (), NULL);
	gst_object_set_name (GST_OBJECT (dpman), name);
	gst_dpman_set_parent(dpman, parent);

	gst_dpman_set_mode(dpman, "disabled");

	return dpman;
}


static void
gst_dpman_dispose (GObject *object)
{
/*	GstDParamManager *dpman = GST_DPMAN(object); */

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

/**
 * gst_dpman_add_required_dparam_callback:
 * @dpman: GstDParamManager instance
 * @dparam_name: a parameter name unique to this GstDParamManager
 * @type: the GValue type that this parameter will store
 * @update_func: callback to update the element with the new value
 * @update_data: will be included in the call to update_func
 *
 * Returns: true if it was successfully added
 */
gboolean 
gst_dpman_add_required_dparam_callback (GstDParamManager *dpman, 
                                        gchar *dparam_name, 
                                        GType type, 
                                        GstDPMUpdateFunction update_func, 
                                        gpointer update_data)
{
	GstDParamWrapper* dpwrap;

	g_return_val_if_fail (dpman != NULL, FALSE);
	g_return_val_if_fail (GST_IS_DPMAN (dpman), FALSE);
	g_return_val_if_fail (update_func != NULL, FALSE);

	dpwrap = gst_dpman_new_wrapper(dpman, dparam_name, type, GST_DPMAN_CALLBACK);

	g_return_val_if_fail (dpwrap != NULL, FALSE);

	GST_DEBUG(GST_CAT_PARAMS,"adding required callback dparam '%s' of type %s\n", dparam_name, g_type_name(type));

	dpwrap->update_func = update_func;
	dpwrap->update_data = update_data;

	return TRUE;	
}

/**
 * gst_dpman_add_required_dparam_direct:
 * @dpman: GstDParamManager instance
 * @dparam_name: a parameter name unique to this GstDParamManager
 * @type: the GValue type that this parameter will store
 * @update_data: pointer to the member to be updated
 *
 * Returns: true if it was successfully added
 */
gboolean 
gst_dpman_add_required_dparam_direct (GstDParamManager *dpman, 
                                        gchar *dparam_name, 
                                        GType type, 
                                        gpointer update_data)
{
	GstDParamWrapper* dpwrap;

	g_return_val_if_fail (dpman != NULL, FALSE);
	g_return_val_if_fail (GST_IS_DPMAN (dpman), FALSE);
	g_return_val_if_fail (update_data != NULL, FALSE);

	dpwrap = gst_dpman_new_wrapper(dpman, dparam_name, type, GST_DPMAN_DIRECT);

	g_return_val_if_fail (dpwrap != NULL, FALSE);

	GST_DEBUG(GST_CAT_PARAMS,"adding required direct dparam '%s' of type %s\n", dparam_name, g_type_name(type));

	dpwrap->update_data = update_data;

	return TRUE;	
}

/**
 * gst_dpman_add_required_dparam_array:
 * @dpman: GstDParamManager instance
 * @dparam_name: a parameter name unique to this GstDParamManager
 * @type: the GValue type that this parameter will store
 * @update_data: pointer to where the array will be stored
 *
 * Returns: true if it was successfully added
 */
gboolean 
gst_dpman_add_required_dparam_array (GstDParamManager *dpman, 
                                        gchar *dparam_name, 
                                        GType type, 
                                        gpointer update_data)
{
	GstDParamWrapper* dpwrap;

	g_return_val_if_fail (dpman != NULL, FALSE);
	g_return_val_if_fail (GST_IS_DPMAN (dpman), FALSE);
	g_return_val_if_fail (update_data != NULL, FALSE);

	dpwrap = gst_dpman_new_wrapper(dpman, dparam_name, type, GST_DPMAN_ARRAY);

	g_return_val_if_fail (dpwrap != NULL, FALSE);

	GST_DEBUG(GST_CAT_PARAMS,"adding required array dparam '%s' of type %s\n", dparam_name, g_type_name(type));

	dpwrap->update_data = update_data;

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

	dpwrap = gst_dpman_get_wrapper(dpman, dparam_name);
	
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

	dpwrap = gst_dpman_get_wrapper(dpman, dparam_name);

	g_return_val_if_fail(dpwrap != NULL, FALSE);
	g_return_val_if_fail(dpwrap->value != NULL, FALSE);

	dpwrap->dparam = dparam;
	gst_dparam_attach(dparam, dpman, dpwrap->value, dpwrap->spec);

	return TRUE;
}

/**
 * gst_dpman_detach_dparam:
 * @dpman: GstDParamManager instance
 * @dparam_name: the name of a parameter with a previously attached GstDParam
 *
 */
void 
gst_dpman_detach_dparam (GstDParamManager *dpman, gchar *dparam_name)
{
	GstDParamWrapper* dpwrap;

	g_return_if_fail (dpman != NULL);
	g_return_if_fail (GST_IS_DPMAN (dpman));
	g_return_if_fail (dparam_name != NULL);
	
	dpwrap = gst_dpman_get_wrapper(dpman, dparam_name);

	g_return_if_fail(dpwrap);
	
	gst_dparam_detach(dpwrap->dparam);
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
 * gst_dpman_get_dparam_type:
 * @dpman: GstDParamManager instance
 * @name: the name of dparam
 *
 * Returns: the type that this dparam requires/uses
 */
GType
gst_dpman_get_dparam_type (GstDParamManager *dpman, gchar *name)
{
	GstDParamWrapper* dpwrap;

	g_return_val_if_fail (dpman != NULL, 0);
	g_return_val_if_fail (GST_IS_DPMAN (dpman), 0);
	g_return_val_if_fail (name != NULL, 0);
	
	dpwrap = g_hash_table_lookup(GST_DPMAN_DPARAMS(dpman), name);
	g_return_val_if_fail (dpwrap != NULL, 0);
	
	return G_VALUE_TYPE(dpwrap->value);
}

GstDParamSpec**
gst_dpman_list_dparam_specs(GstDParamManager *dpman)
{
	GstDParamWrapper* dpwrap;
	GSList *dpwraps;
	GstDParamSpec** dparam_specs;
	guint x = 0;

	g_return_val_if_fail (dpman != NULL, NULL);
	g_return_val_if_fail (GST_IS_DPMAN (dpman), NULL);
	
	dpwraps = GST_DPMAN_DPARAMS_LIST(dpman);

	dparam_specs = g_new0(GstDParamSpec*, g_slist_length(dpwraps) + 1);
	
	while (dpwraps){
		dpwrap = (GstDParamWrapper*)dpwraps->data;
		dparam_specs[x++] = dpwrap->spec;
		dpwraps = g_slist_next(dpwraps);
	}
	return dparam_specs;
}

GstDParamSpec*
gst_dpman_get_dparam_spec (GstDParamManager *dpman, gchar *dparam_name)
{
	GstDParamWrapper* dpwrap;

	g_return_val_if_fail (dpman != NULL, NULL);
	g_return_val_if_fail (GST_IS_DPMAN (dpman), NULL);
	g_return_val_if_fail (dparam_name != NULL, NULL);

	dpwrap = gst_dpman_get_wrapper(dpman, dparam_name);
	return dpwrap->spec;
}

void
gst_dpman_dparam_spec_has_changed (GstDParamManager *dpman, gchar *dparam_name)
{
	
}

/**
 * gst_dpman_register_mode
 * @klass: GstDParamManagerClass class instance
 * @modename: the unique name of the new mode
 * @preprocessfunc: the function which will be called before each buffer is processed
 * @processfunc: the function which may be called throughout the processing of a buffer
 * @setupfunc: the function which initialises the mode when activated
 * @teardownfunc: the function which frees any resources the mode uses
 *
 */
void
gst_dpman_register_mode (GstDParamManagerClass *klass,
                         gchar *modename, 
                         GstDPMModePreProcessFunction preprocessfunc,
                         GstDPMModeProcessFunction processfunc,
                         GstDPMModeSetupFunction setupfunc,
                         GstDPMModeTeardownFunction teardownfunc)
{
	GstDPMMode *mode;

	g_return_if_fail (klass != NULL);
	g_return_if_fail (modename != NULL);
	g_return_if_fail (GST_IS_DPMAN_CLASS (klass));
	
	mode = g_new0(GstDPMMode,1);

	mode->preprocessfunc = preprocessfunc;
	mode->processfunc = processfunc;
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
	GstDPMMode *mode=NULL;
	GstDParamManagerClass *oclass;
	
	g_return_val_if_fail (dpman != NULL, FALSE);
	g_return_val_if_fail (GST_IS_DPMAN (dpman), FALSE);
	g_return_val_if_fail (modename != NULL, FALSE);

	oclass = (GstDParamManagerClass*)(G_OBJECT_GET_CLASS(dpman));
	
	mode = g_hash_table_lookup(oclass->modes, modename);
	g_return_val_if_fail (mode != NULL, FALSE);
	
	if (GST_DPMAN_MODE(dpman) == mode) {
		GST_DEBUG(GST_CAT_PARAMS, "mode %s already set\n", modename);
		return TRUE;
	}
	
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

	g_hash_table_insert(_element_registry, parent, dpman);
	gst_object_set_parent (GST_OBJECT (dpman), GST_OBJECT(parent));
	g_signal_connect(G_OBJECT(parent), "state_change", 
	                 G_CALLBACK (gst_dpman_state_change), dpman);
}

/**
 * gst_dpman_get_manager
 * @parent: the element that the desired GstDParamManager belongs to
 *
 * Returns: the GstDParamManager which belongs to this element or NULL
 * if it doesn't exist
 */
GstDParamManager *
gst_dpman_get_manager (GstElement *parent)
{
	GstDParamManager *dpman;
	g_return_val_if_fail (parent != NULL, NULL);
	g_return_val_if_fail (GST_IS_ELEMENT (parent), NULL);
	
	dpman = (GstDParamManager*)g_hash_table_lookup(_element_registry, parent);
	return dpman;
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

static GstDParamWrapper* 
gst_dpman_get_wrapper(GstDParamManager *dpman, gchar *dparam_name)
{
	g_return_val_if_fail (dpman != NULL, NULL);
	g_return_val_if_fail (GST_IS_DPMAN (dpman), NULL);
	g_return_val_if_fail (dparam_name != NULL, NULL);
	
	return g_hash_table_lookup(GST_DPMAN_DPARAMS(dpman), dparam_name);
}

static GstDParamWrapper* 
gst_dpman_new_wrapper(GstDParamManager *dpman, gchar *dparam_name, GType type, GstDPMUpdateMethod update_method)
{
	GstDParamWrapper* dpwrap;

	g_return_val_if_fail (dpman != NULL, NULL);
	g_return_val_if_fail (GST_IS_DPMAN (dpman), NULL);
	g_return_val_if_fail (dparam_name != NULL, NULL);

	g_return_val_if_fail(gst_dpman_get_wrapper(dpman, dparam_name) == NULL, NULL);

	dpwrap = g_new0(GstDParamWrapper,1);
	dpwrap->update_method = update_method;
	dpwrap->value = g_new0(GValue,1);
	g_value_init(dpwrap->value, type);
	
	dpwrap->spec = g_new0(GstDParamSpec,1);
	dpwrap->spec->dparam_name = dparam_name;
	dpwrap->spec->min_val = g_new0(GValue,1);
	dpwrap->spec->max_val = g_new0(GValue,1);
	dpwrap->spec->default_val = g_new0(GValue,1);
	g_value_init(dpwrap->spec->min_val, type);
	g_value_init(dpwrap->spec->max_val, type);
	g_value_init(dpwrap->spec->default_val, type);

	g_hash_table_insert(GST_DPMAN_DPARAMS(dpman), dparam_name, dpwrap);
	GST_DPMAN_DPARAMS_LIST(dpman) = g_slist_append(GST_DPMAN_DPARAMS_LIST(dpman), dpwrap);
	
	return dpwrap;	
}


static void 
gst_dpman_state_change (GstElement *element, gint state, GstDParamManager *dpman)
{
	GSList *dwraps;
	GstDParam *dparam;
	GstDParamWrapper *dpwrap;

	g_return_if_fail (dpman != NULL);
	g_return_if_fail (GST_IS_DPMAN (dpman));
	
	if (state == GST_STATE_PLAYING){
		GST_DEBUG(GST_CAT_PARAMS, "initialising params\n");
			
		// force all params to be updated
		dwraps = GST_DPMAN_DPARAMS_LIST(dpman);
		while (dwraps){
			dpwrap = (GstDParamWrapper*)dwraps->data;
			dparam = dpwrap->dparam;
			
			if (dparam){
				GST_DPARAM_READY_FOR_UPDATE(dparam) = TRUE;
				if (dparam->spec){
					g_value_copy(dparam->spec->default_val, GST_DPARAM_VALUE(dparam));
				}
			}
			dwraps = g_slist_next(dwraps);
		}
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
gst_dpman_preprocess_synchronous(GstDParamManager *dpman, guint frames, gint64 timestamp)
{
	GSList *dwraps;
   	GstDParam *dparam;
	GstDParamWrapper *dpwrap;
	guint x;

	g_return_val_if_fail (dpman != NULL, frames);
	g_return_val_if_fail (GST_IS_DPMAN (dpman), frames);

	// now check whether any passive dparams are ready for an update
	dwraps = GST_DPMAN_DPARAMS_LIST(dpman);
	while (dwraps){
		dpwrap = (GstDParamWrapper*)dwraps->data;
		dparam = dpwrap->dparam;

		if (dparam && (GST_DPARAM_READY_FOR_UPDATE(dparam) && 
		              (GST_DPARAM_NEXT_UPDATE_TIMESTAMP(dparam) <= timestamp))){
		              	
		    // this will make dpwrap->value contain the latest value.
		    // now we just need to get it to the element
			GST_DPARAM_DO_UPDATE(dparam, timestamp);
			
			switch (dpwrap->update_method) {
				
				// direct method - set the value directly in the struct of the element
				case GST_DPMAN_DIRECT:
					GST_DEBUG(GST_CAT_PARAMS, "doing direct update\n");
					switch (G_VALUE_TYPE(dpwrap->value)){
						case G_TYPE_CHAR:
							*(gchar*)dpwrap->update_data = g_value_get_char(dpwrap->value);
							break;
						case G_TYPE_UCHAR:
							*(guchar*)dpwrap->update_data = g_value_get_uchar(dpwrap->value);
							break;
						case G_TYPE_BOOLEAN:
							*(gboolean*)dpwrap->update_data = g_value_get_boolean(dpwrap->value);
							break;
						case G_TYPE_INT:
							*(gint*)dpwrap->update_data = g_value_get_int(dpwrap->value);
							break;
						case G_TYPE_UINT:
							*(guint*)dpwrap->update_data = g_value_get_uint(dpwrap->value);
							break;
						case G_TYPE_LONG:
							*(glong*)dpwrap->update_data = g_value_get_long(dpwrap->value);
							break;
						case G_TYPE_ULONG:
							*(gulong*)dpwrap->update_data = g_value_get_ulong(dpwrap->value);
							break;
						case G_TYPE_FLOAT:
							*(gfloat*)dpwrap->update_data = g_value_get_float(dpwrap->value);
							break;
						case G_TYPE_DOUBLE:
							*(gdouble*)dpwrap->update_data = g_value_get_double(dpwrap->value);
							break;
						case G_TYPE_POINTER:
							*(gpointer*)dpwrap->update_data = g_value_get_pointer(dpwrap->value);
							break;
						default:
							break;
					}
					break;

				// callback method - call the element's callback so it can do what it likes
				case GST_DPMAN_CALLBACK:
					GST_DEBUG(GST_CAT_PARAMS, "doing callback update\n");
					GST_DPMAN_DO_UPDATE(dpwrap);
					break;
					
				// array method - generate an array of the right size 
				// with each value being the same (in synchronous update mode)
				case GST_DPMAN_ARRAY:
					GST_DEBUG(GST_CAT_PARAMS, "doing array update\n");
					switch (G_VALUE_TYPE(dpwrap->value)){
						case G_TYPE_CHAR:
							(gchar*)dpwrap->update_data = g_new(gchar, frames);
							*(gchar*)dpwrap->update_data = g_value_get_char(dpwrap->value);
							for (x = 1 ; x < frames ; x++)
								((gchar*)dpwrap->update_data)[x] = *(gchar*)dpwrap->update_data;
							break;
						case G_TYPE_UCHAR:
							(guchar*)dpwrap->update_data = g_new(guchar, frames);
							*(guchar*)dpwrap->update_data = g_value_get_uchar(dpwrap->value);
							for (x = 1 ; x < frames ; x++)
								((guchar*)dpwrap->update_data)[x] = *(guchar*)dpwrap->update_data;
							break;
						case G_TYPE_BOOLEAN:
							(gboolean*)dpwrap->update_data = g_new(gboolean, frames);
							*(gboolean*)dpwrap->update_data = g_value_get_boolean(dpwrap->value);
							for (x = 1 ; x < frames ; x++)
								((gboolean*)dpwrap->update_data)[x] = *(gboolean*)dpwrap->update_data;
							break;
						case G_TYPE_INT:
							(gint*)dpwrap->update_data = g_new(gint, frames);
							*(gint*)dpwrap->update_data = g_value_get_int(dpwrap->value);
							for (x = 1 ; x < frames ; x++)
								((gint*)dpwrap->update_data)[x] = *(gint*)dpwrap->update_data;
							break;
						case G_TYPE_UINT:
							(guint*)dpwrap->update_data = g_new(guint, frames);
							*(guint*)dpwrap->update_data = g_value_get_uint(dpwrap->value);
							for (x = 1 ; x < frames ; x++)
								((guint*)dpwrap->update_data)[x] = *(guint*)dpwrap->update_data;
							break;
						case G_TYPE_LONG:
							(glong*)dpwrap->update_data = g_new(glong, frames);
							*(glong*)dpwrap->update_data = g_value_get_long(dpwrap->value);
							for (x = 1 ; x < frames ; x++)
								((glong*)dpwrap->update_data)[x] = *(glong*)dpwrap->update_data;
							break;
						case G_TYPE_ULONG:
							(gulong*)dpwrap->update_data = g_new(gulong, frames);
							*(gulong*)dpwrap->update_data = g_value_get_ulong(dpwrap->value);
							for (x = 1 ; x < frames ; x++)
								((gulong*)dpwrap->update_data)[x] = *(gulong*)dpwrap->update_data;
							break;
						case G_TYPE_FLOAT:
							(gfloat*)dpwrap->update_data = g_new(gfloat, frames);
							*(gfloat*)dpwrap->update_data = g_value_get_float(dpwrap->value);
							for (x = 1 ; x < frames ; x++)
								((gfloat*)dpwrap->update_data)[x] = *(gfloat*)dpwrap->update_data;
							break;
						case G_TYPE_DOUBLE:
							(gdouble*)dpwrap->update_data = g_new(gdouble, frames);
							*(gdouble*)dpwrap->update_data = g_value_get_double(dpwrap->value);
							for (x = 1 ; x < frames ; x++)
								((gdouble*)dpwrap->update_data)[x] = *(gdouble*)dpwrap->update_data;
							break;
						case G_TYPE_POINTER:
							(gpointer*)dpwrap->update_data = g_new(gpointer, frames);
							*(gpointer*)dpwrap->update_data = g_value_get_pointer(dpwrap->value);
							for (x = 1 ; x < frames ; x++)
								((gpointer*)dpwrap->update_data)[x] = *(gpointer*)dpwrap->update_data;
							break;
						default:
							break;
					}
					break;
				default:
					break;
			}
		}
		dwraps = g_slist_next(dwraps);
	}
	return frames;
}

static guint 
gst_dpman_preprocess_noop(GstDParamManager *dpman, guint frames, gint64 timestamp)
{
	return frames;
}

static guint 
gst_dpman_process_noop(GstDParamManager *dpman, guint frame_count)
{
	return 0;
}

