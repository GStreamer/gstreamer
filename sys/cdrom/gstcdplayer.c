/* gstcdplay
 * Copyright (c) 2002 Charles Schmidt <cbschmid@uiuc.edu> 
 
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

#include "gstcdplayer.h"
#include "gstcdplayer_ioctl.h"

/* props */
enum {
	ARG_0,
	ARG_DEVICE,
	ARG_NUM_TRACKS,
	ARG_START_TRACK,
};

/* signals */
enum {
	LAST_SIGNAL,
};

static void cdplayer_class_init(CDPlayerClass *klass);
static void cdplayer_init(CDPlayer *cdp);
static void cdplayer_set_property(GObject *object,guint prop_id,const GValue *value,GParamSpec *spec);
static void cdplayer_get_property(GObject *object,guint prop_id,GValue *value,GParamSpec *spec);
static void cdplayer_dispose(GObject *object);
static GstElementStateReturn cdplayer_change_state(GstElement *element);

static gboolean plugin_init(GModule *module,GstPlugin *plugin);



static GstElementClass *parent_class;
/* static guint cdplayer_signals[LAST_SIGNAL] = {0}; */


static GstElementDetails cdplayer_details = {
	"CD Player",
	"Generic/Bin",
	"Play CD audio through the CD Drive",
	VERSION,
	"Charles Schmidt <cbschmid@uiuc.edu>",
	"(C) 2002",
};



GType cdplayer_get_type(void)
{
	static GType cdplayer_type = 0;

	if (!cdplayer_type) {
		static const GTypeInfo cdplayer_info = {
			sizeof(CDPlayerClass),
			NULL,
			NULL,
			(GClassInitFunc)cdplayer_class_init,
			NULL,
			NULL,
			sizeof(CDPlayer),
			0,
			(GInstanceInitFunc)cdplayer_init,
			NULL
		};

		cdplayer_type = g_type_register_static(GST_TYPE_BIN,"CDPlayer",&cdplayer_info,0);
	}

	return cdplayer_type;
}

static void cdplayer_class_init(CDPlayerClass *klass)
{
	GObjectClass *gobject_klass;
	GstElementClass *gstelement_klass;

	gobject_klass = (GObjectClass *)klass;
	gstelement_klass = (GstElementClass *)klass;

	parent_class = g_type_class_ref(gst_bin_get_type());

	gobject_klass->dispose = GST_DEBUG_FUNCPTR(cdplayer_dispose);

	gstelement_klass->change_state = GST_DEBUG_FUNCPTR(cdplayer_change_state);

	g_object_class_install_property(gobject_klass,ARG_DEVICE,g_param_spec_string("device","device","CDROM device",NULL,G_PARAM_READWRITE));
	g_object_class_install_property(gobject_klass,ARG_NUM_TRACKS,g_param_spec_int("num_tracks","num_tracks","Number of Tracks",G_MININT,G_MAXINT,0,G_PARAM_READABLE));
	g_object_class_install_property(gobject_klass,ARG_START_TRACK,g_param_spec_int("start_track","start_track","Track to start playback on",1,CDPLAYER_MAX_TRACKS-1,1,G_PARAM_READABLE));

	gobject_klass->set_property = cdplayer_set_property;
	gobject_klass->get_property = cdplayer_get_property;

	return;
}

static void cdplayer_init(CDPlayer *cdp)
{
	GstScheduler *scheduler;

	cdp->device = g_strdup("/dev/cdrom");
	cdp->num_tracks = -1;
	cdp->start_track = 1;

	cdp->paused = FALSE;

	GST_FLAG_SET(cdp,GST_BIN_SELF_SCHEDULABLE);

	scheduler = gst_scheduler_factory_make(NULL,GST_ELEMENT(cdp));
	g_return_if_fail(scheduler != NULL);

	gst_scheduler_setup(scheduler);

	return;
}

static void cdplayer_set_property(GObject *object,guint prop_id,const GValue *value,GParamSpec *spec)
{
	CDPlayer *cdp;

	g_return_if_fail(GST_IS_CDPLAYER(object));

	cdp = CDPLAYER(object);

	switch (prop_id) {
		case ARG_DEVICE:
// FIXME prolly should uhh.. stop it or something
			if (cdp->device) {
				g_free(cdp->device);
			} 
			
			cdp->device = g_strdup(g_value_get_string(value));
			break;
		case ARG_START_TRACK:
// FIXME prolly should uhh.. restart play, i guess... or something whatever
			cdp->start_track = g_value_get_int(value);
			break;
		default:
			break;
	}

	return;
}


static void cdplayer_get_property(GObject *object,guint prop_id,GValue *value,GParamSpec *spec)
{
	CDPlayer *cdp;

	g_return_if_fail(GST_IS_CDPLAYER(object));

	cdp = CDPLAYER(object);

	switch (prop_id) {
		case ARG_DEVICE:
			g_value_set_string(value,cdp->device);
			break;
		case ARG_NUM_TRACKS:
			g_value_set_int(value,cdp->num_tracks);
			break;
		case ARG_START_TRACK:
			g_value_set_int(value,cdp->start_track);
			break;
		default:
			break;
	}

	return;
}

static void cdplayer_dispose(GObject *object)
{
	CDPlayer *cdp;

	g_return_if_fail(GST_IS_CDPLAYER(object));

	cdp = CDPLAYER(object);

	G_OBJECT_CLASS(parent_class)->dispose(object);

	g_free(cdp->device);

	if (GST_ELEMENT_SCHED(cdp)) {
		gst_scheduler_reset(GST_ELEMENT_SCHED(cdp));
		gst_object_unref(GST_OBJECT(GST_ELEMENT_SCHED(cdp)));
		GST_ELEMENT_SCHED(cdp) = NULL;
	}

	return;
}

static GstElementStateReturn cdplayer_change_state(GstElement *element)
{
	CDPlayer *cdp;
	GstElementState state = GST_STATE(element);
	GstElementState pending = GST_STATE_PENDING(element);

	g_return_val_if_fail(GST_IS_CDPLAYER(element),GST_STATE_FAILURE);

	cdp = CDPLAYER(element);

	switch (pending) {
		case GST_STATE_READY:
			if (state != GST_STATE_PAUSED) {
				if (cd_init(CDPLAYER_CD(cdp),cdp->device) == FALSE) {
					return GST_STATE_FAILURE;
				}
				cdp->num_tracks = cdp->cd.num_tracks;
			}
			break;
		case GST_STATE_PAUSED:
			/* ready->paused is not useful */
			if (state != GST_STATE_READY) {
				if (cd_pause(CDPLAYER_CD(cdp)) == FALSE) {
					return GST_STATE_FAILURE;
				}

				cdp->paused = TRUE;
			}

			break;
		case GST_STATE_PLAYING:
			if (cdp->paused == TRUE) {
				if (cd_resume(CDPLAYER_CD(cdp)) == FALSE) {
					return GST_STATE_FAILURE;
				}

				cdp->paused = FALSE;
			} else {
				if (cd_start(CDPLAYER_CD(cdp),cdp->start_track) == FALSE) {
					return GST_STATE_FAILURE;
				}
			}

			break;
		case GST_STATE_NULL:
			/* stop & close fd */
			if (cd_stop(CDPLAYER_CD(cdp)) == FALSE || cd_close(CDPLAYER_CD(cdp)) == FALSE) {
				return GST_STATE_FAILURE;
			}

			break;
		default:
			break;
	}

	GST_STATE(element) = GST_STATE_PENDING(element);
	GST_STATE_PENDING(element) = GST_STATE_VOID_PENDING;

	if (GST_ELEMENT_CLASS(parent_class)->change_state) {
		return GST_ELEMENT_CLASS(parent_class)->change_state(element);
	}

	return GST_STATE_SUCCESS;
}


static gboolean plugin_init(GModule *module,GstPlugin *plugin)
{
	GstElementFactory *factory;

	factory = gst_element_factory_new("cdplayer",GST_TYPE_CDPLAYER,&cdplayer_details);
	g_return_val_if_fail(factory != NULL,FALSE);
	gst_plugin_add_feature(plugin,GST_PLUGIN_FEATURE(factory));

	gst_plugin_set_longname(plugin,"CD Player");
	
	return TRUE;

}

GstPluginDesc plugin_desc = {
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "cdplayer",
    plugin_init
};

	

