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

#include "dparammanager.h"
#include <gst/gstelement.h>
#include <gst/gstmarshal.h>
#include <gst/gstinfo.h>

GST_DEBUG_CATEGORY_EXTERN(_gst_control_debug);

static GHashTable *_element_registry = NULL;
static gboolean _gst_dpman_init_done = FALSE;

enum {
  NEW_REQUIRED_DPARAM,
  LAST_SIGNAL
};

static void gst_dpman_class_init (GstDParamManagerClass *klass);
static void gst_dpman_init (GstDParamManager *dpman);
static void gst_dpman_dispose (GObject *object);
static GstDParamWrapper* gst_dpman_new_wrapper(GstDParamManager *dpman, GParamSpec *param_spec, gchar *unit_name, GstDPMUpdateMethod update_method);
static GstDParamWrapper* gst_dpman_get_wrapper(GstDParamManager *dpman, gchar *dparam_name);
static void gst_dpman_state_change (GstElement *element, gint old_state, gint new_state, GstDParamManager *dpman);
static gboolean gst_dpman_preprocess_synchronous(GstDParamManager *dpman, guint frames, gint64 timestamp);
static gboolean gst_dpman_preprocess_asynchronous(GstDParamManager *dpman, guint frames, gint64 timestamp);
static gboolean gst_dpman_process_asynchronous(GstDParamManager *dpman, guint frame_count);
static gboolean gst_dpman_preprocess_noop(GstDParamManager *dpman, guint frames, gint64 timestamp);
static gboolean gst_dpman_process_noop(GstDParamManager *dpman, guint frame_count);
static void gst_dpman_setup_synchronous(GstDParamManager *dpman);
static void gst_dpman_setup_asynchronous(GstDParamManager *dpman);
static void gst_dpman_setup_disabled(GstDParamManager *dpman);
static void gst_dpman_teardown_synchronous(GstDParamManager *dpman);
static void gst_dpman_teardown_asynchronous(GstDParamManager *dpman);
static void gst_dpman_teardown_disabled(GstDParamManager *dpman);

static GObjectClass *parent_class;
static guint gst_dpman_signals[LAST_SIGNAL] = { 0 };

void 
_gst_dpman_initialize()
{
	if (_gst_dpman_init_done) return;
	
	_gst_dpman_init_done = TRUE;
	_element_registry = g_hash_table_new(NULL,NULL);
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
	                       gst_dpman_preprocess_synchronous, gst_dpman_process_noop, 
			       gst_dpman_setup_synchronous, gst_dpman_teardown_synchronous);
	gst_dpman_register_mode (klass, "asynchronous", 
	                       gst_dpman_preprocess_asynchronous, gst_dpman_process_asynchronous, 
			       gst_dpman_setup_asynchronous, gst_dpman_teardown_asynchronous);
	gst_dpman_register_mode (klass, "disabled", 
	                       gst_dpman_preprocess_noop, gst_dpman_process_noop, 
			       gst_dpman_setup_disabled, gst_dpman_teardown_disabled);


	gst_dpman_signals[NEW_REQUIRED_DPARAM] =
		g_signal_new ("new_required_dparam", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (GstDParamManagerClass, new_required_dparam), NULL, NULL,
		              gst_marshal_VOID__STRING, G_TYPE_NONE, 1,
		              G_TYPE_STRING);
		                              
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
 * @update_func: callback to update the element with the new value
 * @update_data: will be included in the call to update_func
 *
 * Returns: true if it was successfully added
 */
gboolean 
gst_dpman_add_required_dparam_callback (GstDParamManager *dpman, 
                                        GParamSpec *param_spec,
                                        gchar *unit_name,
                                        GstDPMUpdateFunction update_func, 
                                        gpointer update_data)
{
	GstDParamWrapper* dpwrap;

	g_return_val_if_fail (dpman != NULL, FALSE);
	g_return_val_if_fail (GST_IS_DPMAN (dpman), FALSE);
	g_return_val_if_fail (update_func != NULL, FALSE);

	dpwrap = gst_dpman_new_wrapper(dpman, param_spec, unit_name, GST_DPMAN_CALLBACK);

	g_return_val_if_fail (dpwrap != NULL, FALSE);

	GST_DEBUG ("adding required callback dparam '%s'", g_param_spec_get_name(param_spec));

	dpwrap->update_func = update_func;
	dpwrap->update_data = update_data;
	
	g_signal_emit (G_OBJECT (dpman), gst_dpman_signals[NEW_REQUIRED_DPARAM], 0, g_param_spec_get_name(param_spec));

	return TRUE;	
}

/**
 * gst_dpman_add_required_dparam_direct:
 * @dpman: GstDParamManager instance
 * @update_data: pointer to the member to be updated
 *
 * Returns: true if it was successfully added
 */
gboolean 
gst_dpman_add_required_dparam_direct (GstDParamManager *dpman, 
                                      GParamSpec *param_spec,
                                      gchar *unit_name,
                                      gpointer update_data)
{
	GstDParamWrapper* dpwrap;

	g_return_val_if_fail (dpman != NULL, FALSE);
	g_return_val_if_fail (GST_IS_DPMAN (dpman), FALSE);
	g_return_val_if_fail (update_data != NULL, FALSE);

	dpwrap = gst_dpman_new_wrapper(dpman, param_spec, unit_name, GST_DPMAN_DIRECT);

	g_return_val_if_fail (dpwrap != NULL, FALSE);

	GST_DEBUG ("adding required direct dparam '%s'", g_param_spec_get_name(param_spec));

	dpwrap->update_data = update_data;

	g_signal_emit (G_OBJECT (dpman), gst_dpman_signals[NEW_REQUIRED_DPARAM], 0, g_param_spec_get_name(param_spec));

	return TRUE;	
}

/**
 * gst_dpman_add_required_dparam_array:
 * @dpman: GstDParamManager instance
 * @dparam_name: a parameter name unique to this GstDParamManager
 * @update_data: pointer to where the array will be stored
 *
 * Returns: true if it was successfully added
 */
gboolean 
gst_dpman_add_required_dparam_array (GstDParamManager *dpman, 
                                     GParamSpec *param_spec,
                                     gchar *unit_name,
                                     gpointer update_data)
{
	GstDParamWrapper* dpwrap;

	g_return_val_if_fail (dpman != NULL, FALSE);
	g_return_val_if_fail (GST_IS_DPMAN (dpman), FALSE);
	g_return_val_if_fail (update_data != NULL, FALSE);

	dpwrap = gst_dpman_new_wrapper(dpman, param_spec, unit_name, GST_DPMAN_ARRAY);

	g_return_val_if_fail (dpwrap != NULL, FALSE);

	GST_DEBUG ("adding required array dparam '%s'", g_param_spec_get_name(param_spec));

	dpwrap->update_data = update_data;

	g_signal_emit (G_OBJECT (dpman), gst_dpman_signals[NEW_REQUIRED_DPARAM], 0, g_param_spec_get_name(param_spec));

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

	GST_DEBUG ("removing required dparam: %s", dparam_name);
	
	g_hash_table_remove(GST_DPMAN_DPARAMS(dpman), dparam_name);
	GST_DPMAN_DPARAMS_LIST(dpman) = g_list_remove(GST_DPMAN_DPARAMS_LIST(dpman), dpwrap);

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
	g_return_val_if_fail (G_PARAM_SPEC_VALUE_TYPE (dpwrap->param_spec) == dparam->type, FALSE); 

	dpwrap->dparam = dparam;
	gst_dparam_attach(dparam, dpman, dpwrap->param_spec, dpwrap->unit_name);

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

GParamSpec**
gst_dpman_list_dparam_specs(GstDParamManager *dpman)
{
	GstDParamWrapper* dpwrap;
	GList *dwraps;
	GParamSpec** param_specs;
	guint x = 0;

	g_return_val_if_fail (dpman != NULL, NULL);
	g_return_val_if_fail (GST_IS_DPMAN (dpman), NULL);
	
	dwraps = GST_DPMAN_DPARAMS_LIST(dpman);

	param_specs = g_new0(GParamSpec*, g_list_length(dwraps) + 1);
	
	while (dwraps){
		dpwrap = (GstDParamWrapper*)dwraps->data;
		param_specs[x++] = dpwrap->param_spec;
		dwraps = g_list_next(dwraps);
	}
	return param_specs;
}

GParamSpec*
gst_dpman_get_param_spec (GstDParamManager *dpman, gchar *dparam_name)
{
	GstDParamWrapper* dpwrap;

	g_return_val_if_fail (dpman != NULL, NULL);
	g_return_val_if_fail (GST_IS_DPMAN (dpman), NULL);
	g_return_val_if_fail (dparam_name != NULL, NULL);

	dpwrap = gst_dpman_get_wrapper(dpman, dparam_name);
	return dpwrap->param_spec;
}

void
gst_dpman_set_rate (GstDParamManager *dpman, gint rate)
{
	g_return_if_fail (GST_IS_DPMAN (dpman));
	GST_DPMAN_RATE(dpman) = rate;
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
		GST_DEBUG ("mode %s already set", modename);
		return TRUE;
	}
	
	GST_DEBUG ("setting mode to %s", modename);
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
 * gst_dpman_bypass_dparam:
 * @dpman: GstDParamManager instance
 * @dparam_name: the name of dparam
 *
 * If a dparam is attached to this dparam_name, it will be detached
 * and a warning will be issued. This should be called in the _set_property
 * function of an element if the value it changes is also changed by a dparam.
 * 
 */
void
gst_dpman_bypass_dparam(GstDParamManager *dpman, gchar *dparam_name)
{
	GstDParamWrapper* dpwrap;
	
	g_return_if_fail (dpman != NULL);
	g_return_if_fail (GST_IS_DPMAN (dpman));
	g_return_if_fail (dparam_name != NULL);
		
	dpwrap = gst_dpman_get_wrapper(dpman, dparam_name);
	g_return_if_fail (dpwrap != NULL);
	
	if (dpwrap->dparam != NULL){
		g_warning("Bypassing attached dparam '%s'. It will be detached", dparam_name);
		gst_dpman_detach_dparam(dpman, dparam_name);
	}
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
gst_dpman_new_wrapper(GstDParamManager *dpman, 
                      GParamSpec *param_spec, 
                      gchar *unit_name, 
                      GstDPMUpdateMethod update_method)
{
	GstDParamWrapper* dpwrap;
	gchar *dparam_name;

	g_return_val_if_fail (dpman != NULL, NULL);
	g_return_val_if_fail (GST_IS_DPMAN (dpman), NULL);
	g_return_val_if_fail (param_spec != NULL, NULL);
	g_return_val_if_fail (gst_unitconv_unit_exists(unit_name), NULL);

	dparam_name = g_strdup(g_param_spec_get_name(param_spec));
	g_return_val_if_fail(gst_dpman_get_wrapper(dpman, dparam_name) == NULL, NULL);

	dpwrap = g_new0(GstDParamWrapper,1);
	dpwrap->update_method = update_method;
	dpwrap->value = g_new0(GValue,1);
	g_value_init(dpwrap->value, G_PARAM_SPEC_VALUE_TYPE(param_spec));
	dpwrap->param_spec = param_spec;
	dpwrap->unit_name = unit_name;
	
	g_hash_table_insert(GST_DPMAN_DPARAMS(dpman), dparam_name, dpwrap);
	GST_DPMAN_DPARAMS_LIST(dpman) = g_list_append(GST_DPMAN_DPARAMS_LIST(dpman), dpwrap);
	
	return dpwrap;	
}


static void 
gst_dpman_state_change (GstElement *element, gint old_state, gint new_state, GstDParamManager *dpman)
{
	GList *dwraps;
	GstDParam *dparam;
	GstDParamWrapper *dpwrap;

	g_return_if_fail (dpman != NULL);
	g_return_if_fail (GST_IS_DPMAN (dpman));
	
	if (new_state == GST_STATE_PLAYING){
		GST_DEBUG ("initialising params");

			
		/* force all params to be updated */
		dwraps = GST_DPMAN_DPARAMS_LIST(dpman);
		while (dwraps){
			dpwrap = (GstDParamWrapper*)dwraps->data;
			dparam = dpwrap->dparam;
			
			if (dparam){
				GST_DPARAM_READY_FOR_UPDATE(dparam) = TRUE;
				GST_DPARAM_NEXT_UPDATE_TIMESTAMP(dparam) = 0LL;
			}
			/* some dparams treat the first update after the pipeline starts differently */
			dpwrap->update_info = GST_DPARAM_UPDATE_FIRST;
			dwraps = g_list_next(dwraps);
		}
	}
}

static inline void
gst_dpman_inline_direct_update(GValue *value, gpointer data){
	switch (G_VALUE_TYPE(value)){
		case G_TYPE_INT:
			*(gint*)data = g_value_get_int(value);
			break;
		case G_TYPE_INT64:
			*(gint64*)data = g_value_get_int64(value);
			break;
		case G_TYPE_FLOAT:
			*(gfloat*)data = g_value_get_float(value);
			break;
                case G_TYPE_DOUBLE:
			*(double*)data = g_value_get_double(value);
			break;
		default:
			break;
	}
}

static gboolean 
gst_dpman_preprocess_synchronous(GstDParamManager *dpman, guint frames, gint64 timestamp)
{
	GList *dwraps;
	GstDParamWrapper *dpwrap;

	g_return_val_if_fail (GST_IS_DPMAN (dpman), FALSE);

	/* this basically means don't call GST_DPMAN_PREPROCESS at all */
	dpman->next_update_frame = frames;
	dpman->frames_to_process = frames;

	/* now check whether any dparams are ready for an update */
	dwraps = GST_DPMAN_DPARAMS_LIST(dpman);
	while (dwraps){
		dpwrap = (GstDParamWrapper*)dwraps->data;
		
		if (dpwrap->dparam && 
		    GST_DPARAM_READY_FOR_UPDATE(dpwrap->dparam) && 
		    GST_DPARAM_NEXT_UPDATE_TIMESTAMP(dpwrap->dparam) <= timestamp){

			switch (dpwrap->update_method) {
				
				/* direct method - set the value directly in the struct of the element */
				case GST_DPMAN_DIRECT:
					GST_DPARAM_DO_UPDATE(dpwrap->dparam, timestamp, dpwrap->value, dpwrap->update_info);
					GST_DEBUG ("doing direct update");

					gst_dpman_inline_direct_update(dpwrap->value, dpwrap->update_data);
					break;

				/* callback method - call the element's callback so it can do what it likes */
				case GST_DPMAN_CALLBACK:
					GST_DPARAM_DO_UPDATE(dpwrap->dparam, timestamp, dpwrap->value, dpwrap->update_info);
					GST_DEBUG ("doing callback update");
					
					GST_DPMAN_CALLBACK_UPDATE(dpwrap, dpwrap->value);
					break;

				case GST_DPMAN_ARRAY:
					/* FIXME do array method checking here */
					break;
				default:
					break;
			}

			if (dpwrap->update_info == GST_DPARAM_UPDATE_FIRST){
				/* it is not the first update anymore */
				dpwrap->update_info = GST_DPARAM_UPDATE_NORMAL;
			}
		}
		dwraps = g_list_next(dwraps);
	}


	return FALSE;
}

static gint 
gst_dpman_dpwrap_compare (const GstDParamWrapper *a, const GstDParamWrapper *b)
{
	if (a->next_update_frame > b->next_update_frame) return 1;
	return (a->next_update_frame < b->next_update_frame) ? -1 : 0;
}

static gboolean 
gst_dpman_preprocess_asynchronous(GstDParamManager *dpman, guint frames, gint64 timestamp)
{
	GList *dwraps;
	GstDParamWrapper *dpwrap;
	gint64 current_time;
	gboolean updates_pending;

	g_return_val_if_fail (GST_IS_DPMAN (dpman), FALSE);


	if (GST_DPMAN_RATE(dpman) == 0){
		g_warning("The element hasn't given GstDParamManager a frame rate");
		return FALSE;
	}
	dpman->rate_ratio = (guint)(1000000000LL / (gint64)GST_DPMAN_RATE(dpman));

	dpman->time_buffer_starts = timestamp;
	dpman->time_buffer_ends = timestamp + ((gint64)frames * (gint64)dpman->rate_ratio);
	dpman->num_frames = frames;

	updates_pending = FALSE;

	/* now check whether any dparams are ready for an update */
	dwraps = GST_DPMAN_DPARAMS_LIST(dpman);
	while (dwraps){
		dpwrap = (GstDParamWrapper*)dwraps->data;
		
		dpwrap->next_update_frame = frames;

		if (dpwrap->dparam && 
		    GST_DPARAM_READY_FOR_UPDATE(dpwrap->dparam)){

			current_time = GST_DPARAM_NEXT_UPDATE_TIMESTAMP(dpwrap->dparam);
			if (current_time > dpman->time_buffer_ends){
				/* not due for an update in this buffer */
				dwraps = g_list_next(dwraps);
				continue;
			}
			if (current_time < timestamp){
				current_time = timestamp;
			}

			if (current_time == timestamp){
				/* we are overdue for an update. lets do it now */

				GST_DPARAM_DO_UPDATE(dpwrap->dparam, current_time, dpwrap->value, dpwrap->update_info);

				if (dpwrap->update_info == GST_DPARAM_UPDATE_FIRST){
					/* it is not the first update anymore */
					dpwrap->update_info = GST_DPARAM_UPDATE_NORMAL;
				}

				switch (dpwrap->update_method) {

					/* direct method - set the value directly in the struct of the element */
					case GST_DPMAN_DIRECT:
						GST_DEBUG ("doing direct update");
						gst_dpman_inline_direct_update(dpwrap->value, dpwrap->update_data);
						break;

					/* callback method - call the element's callback so it can do what it likes */
					case GST_DPMAN_CALLBACK:
						GST_DEBUG ("doing callback update");
						GST_DPMAN_CALLBACK_UPDATE(dpwrap, dpwrap->value);
						break;
					default:
						break;
				}

				current_time = GST_DPARAM_NEXT_UPDATE_TIMESTAMP(dpwrap->dparam);

				if (!GST_DPARAM_READY_FOR_UPDATE(dpwrap->dparam) || 
				    current_time > dpman->time_buffer_ends){
					/* not due for an update in this buffer */
					dwraps = g_list_next(dwraps);
					continue;
				}
			}

			dpwrap->next_update_frame = (guint)(current_time - timestamp) / dpman->rate_ratio;
			updates_pending = TRUE;

			GST_DEBUG ("timestamp start: %"
				  G_GINT64_FORMAT " end: %"
				  G_GINT64_FORMAT " current: %"
				  G_GINT64_FORMAT, 
			          timestamp, dpman->time_buffer_ends, current_time);

		}
		dwraps = g_list_next(dwraps);
	}
	if (updates_pending){
		GST_DPMAN_DPARAMS_LIST(dpman) = g_list_sort(GST_DPMAN_DPARAMS_LIST(dpman), (GCompareFunc)gst_dpman_dpwrap_compare); 
		dwraps = GST_DPMAN_DPARAMS_LIST(dpman);
		dpwrap = (GstDParamWrapper*)dwraps->data;

		dpman->next_update_frame = dpwrap->next_update_frame;
		dpman->frames_to_process = dpman->next_update_frame;

		GST_DEBUG ("next update frame %u, frames to process %u", dpman->next_update_frame, dpman->frames_to_process);
		return TRUE;
	}
	
	dpman->next_update_frame = frames;
	dpman->frames_to_process = frames;
	return FALSE;
}

static gboolean 
gst_dpman_process_asynchronous(GstDParamManager *dpman, guint frame_count)
{
	GstDParamWrapper *dpwrap;
	GList *dwraps;
	gint64 current_time;
	gboolean needs_resort = FALSE;

	dwraps = GST_DPMAN_DPARAMS_LIST(dpman);
	dpwrap = (GstDParamWrapper*)dwraps->data;

	GST_DEBUG ("in gst_dpman_process_asynchronous");

	if (frame_count >= dpman->num_frames){
		g_warning("there is no more buffer to process");
		dpman->next_update_frame = dpman->num_frames;
		dpman->frames_to_process = 0;
		return FALSE;
	}

	if (frame_count != dpwrap->next_update_frame){
		g_warning("frame count %u does not match update frame %u", 
		          frame_count, dpwrap->next_update_frame);
	}

	while (dpwrap){

		current_time = GST_DPARAM_NEXT_UPDATE_TIMESTAMP(dpwrap->dparam);
		GST_DPARAM_DO_UPDATE(dpwrap->dparam, current_time, dpwrap->value, dpwrap->update_info);
		switch (dpwrap->update_method) {

			/* direct method - set the value directly in the struct of the element */
			case GST_DPMAN_DIRECT:
				GST_DEBUG ("doing direct update");
				gst_dpman_inline_direct_update(dpwrap->value, dpwrap->update_data);
				break;

			/* callback method - call the element's callback so it can do what it likes */
			case GST_DPMAN_CALLBACK:
				GST_DEBUG ("doing callback update");
				GST_DPMAN_CALLBACK_UPDATE(dpwrap, dpwrap->value);
				break;
			default:
				break;
		}

		dpwrap->next_update_frame = dpman->num_frames;
		needs_resort = TRUE;

		if(GST_DPARAM_READY_FOR_UPDATE(dpwrap->dparam)){
			current_time = GST_DPARAM_NEXT_UPDATE_TIMESTAMP(dpwrap->dparam);
			if (current_time <= dpman->time_buffer_ends){
				dpwrap->next_update_frame = (guint)(current_time - dpman->time_buffer_starts) / dpman->rate_ratio;
			}
		}

		if ((dwraps = g_list_next(dwraps))){
			dpwrap = (GstDParamWrapper*)dwraps->data;
			if (frame_count == dpwrap->next_update_frame){
				continue;
			}
		}
		dpwrap = NULL;
	}

	if (needs_resort && g_list_length(GST_DPMAN_DPARAMS_LIST(dpman)) > 1){
		GST_DPMAN_DPARAMS_LIST(dpman) = g_list_sort(GST_DPMAN_DPARAMS_LIST(dpman), (GCompareFunc)gst_dpman_dpwrap_compare); 
	}
	
	dwraps = GST_DPMAN_DPARAMS_LIST(dpman);
	dpwrap = (GstDParamWrapper*)dwraps->data;

	if (dpwrap->next_update_frame == dpman->num_frames){
		dpman->next_update_frame = dpman->num_frames;
		dpman->frames_to_process = dpman->num_frames - frame_count;
		GST_DEBUG ("no more updates, frames to process %u", dpman->frames_to_process);
	}
	else {
		dpman->next_update_frame = dpwrap->next_update_frame;
		dpman->frames_to_process = dpman->next_update_frame - frame_count;
		GST_DEBUG ("next update frame %u, frames to process %u", dpman->next_update_frame, dpman->frames_to_process);
	}

	return TRUE;
}

static gboolean 
gst_dpman_preprocess_noop(GstDParamManager *dpman, guint frames, gint64 timestamp)
{
	dpman->next_update_frame = frames;
	dpman->frames_to_process = frames;
	return FALSE;
}

static gboolean 
gst_dpman_process_noop(GstDParamManager *dpman, guint frame_count)
{
	g_warning("gst_dpman_process_noop should never be called - something might be wrong with your processing loop");
	return FALSE;
}

static void 
gst_dpman_setup_synchronous(GstDParamManager *dpman){
	g_return_if_fail (GST_IS_DPMAN (dpman));

}

static void 
gst_dpman_setup_asynchronous(GstDParamManager *dpman){
	g_return_if_fail (GST_IS_DPMAN (dpman));

}

static void 
gst_dpman_setup_disabled(GstDParamManager *dpman){
	g_return_if_fail (GST_IS_DPMAN (dpman));

}

static void 
gst_dpman_teardown_synchronous(GstDParamManager *dpman){
	g_return_if_fail (GST_IS_DPMAN (dpman));

}

static void 
gst_dpman_teardown_asynchronous(GstDParamManager *dpman){
	g_return_if_fail (GST_IS_DPMAN (dpman));
	
}

static void 
gst_dpman_teardown_disabled(GstDParamManager *dpman){
	g_return_if_fail (GST_IS_DPMAN (dpman));

}
