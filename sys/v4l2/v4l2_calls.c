/* G-Streamer generic V4L2 element - generic V4L2 calls handling
 * Copyright (C) 2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include "v4l2_calls.h"

#define DEBUG(format, args...) \
	GST_DEBUG_ELEMENT(GST_CAT_PLUGIN_INFO, \
		GST_ELEMENT(v4l2element), \
		"V4L2: " format, ##args)


/******************************************************
 * gst_v4l2_get_capabilities():
 *   get the device's capturing capabilities
 * return value: TRUE on success, FALSE on error
 ******************************************************/

static gboolean
gst_v4l2_get_capabilities (GstV4l2Element *v4l2element)
{
	DEBUG("getting capabilities");
	GST_V4L2_CHECK_OPEN(v4l2element);

	if (ioctl(v4l2element->video_fd, VIDIOC_QUERYCAP, &(v4l2element->vcap)) < 0) {
		gst_element_error(GST_ELEMENT(v4l2element),
			"Error getting %s capabilities: %s",
			v4l2element->device, sys_errlist[errno]);
		return FALSE;
	}

	return TRUE;
}


/******************************************************
 * gst_v4l2_empty_lists() and gst_v4l2_fill_lists():
 *   fill/empty the lists of enumerations
 * return value: TRUE on success, FALSE on error
 ******************************************************/

static gboolean
gst_v4l2_fill_lists (GstV4l2Element *v4l2element)
{
	gint n;

	DEBUG("getting enumerations");
	GST_V4L2_CHECK_OPEN(v4l2element);

	/* create enumeration lists - let's start with format enumeration */
	for (n=0;;n++) {
		struct v4l2_fmtdesc format, *fmtptr;
		format.index = n;
		if (ioctl(v4l2element->video_fd, VIDIOC_ENUM_PIXFMT, &format) < 0) {
			if (errno == EINVAL)
				break; /* end of enumeration */
			else {
				gst_element_error(GST_ELEMENT(v4l2element),
					"Failed to get no. %d in pixelformat enumeration for %s: %s",
					n, v4l2element->device, sys_errlist[errno]);
				return FALSE;
			}
		}
		fmtptr = g_malloc(sizeof(format));
		memcpy(fmtptr, &format, sizeof(format));
		v4l2element->formats = g_list_append(v4l2element->formats, fmtptr);
	}

	/* and now, the inputs */
	for (n=0;;n++) {
		struct v4l2_input input, *inpptr;
		input.index = n;
		if (ioctl(v4l2element->video_fd, VIDIOC_ENUMINPUT, &input) < 0) {
			if (errno == EINVAL)
				break; /* end of enumeration */
			else {
				gst_element_error(GST_ELEMENT(v4l2element),
					"Failed to get no. %d in input enumeration for %s: %s",
					n, v4l2element->device, sys_errlist[errno]);
				return FALSE;
			}
		}
		inpptr = g_malloc(sizeof(input));
		memcpy(inpptr, &input, sizeof(input));
		v4l2element->inputs = g_list_append(v4l2element->inputs, inpptr);
	}

	/* outputs */
	for (n=0;;n++) {
		struct v4l2_output output, *outptr;
		output.index = n;
		if (ioctl(v4l2element->video_fd, VIDIOC_ENUMOUTPUT, &output) < 0) {
			if (errno == EINVAL)
				break; /* end of enumeration */
			else {
				gst_element_error(GST_ELEMENT(v4l2element),
					"Failed to get no. %d in output enumeration for %s: %s",
					n, v4l2element->device, sys_errlist[errno]);
				return FALSE;
			}
		}
		outptr = g_malloc(sizeof(output));
		memcpy(outptr, &output, sizeof(output));
		v4l2element->outputs = g_list_append(v4l2element->outputs, outptr);
	}

	/* norms... */
	for (n=0;;n++) {
		struct v4l2_enumstd standard, *stdptr;
		standard.index = n;
		if (ioctl(v4l2element->video_fd, VIDIOC_ENUMSTD, &standard) < 0) {
			if (errno == EINVAL)
				break; /* end of enumeration */
			else {
				gst_element_error(GST_ELEMENT(v4l2element),
					"Failed to get no. %d in norm enumeration for %s: %s",
					n, v4l2element->device, sys_errlist[errno]);
				return FALSE;
			}
		}
		stdptr = g_malloc(sizeof(standard));
		memcpy(stdptr, &standard, sizeof(standard));
		v4l2element->norms = g_list_append(v4l2element->norms, stdptr);
	}

	/* and lastly, controls+menus (if appropriate) */
	for (n=0;;n++) {
		struct v4l2_queryctrl control, *ctrlptr;
		GList *menus = NULL;
		control.id = n;
		if (ioctl(v4l2element->video_fd, VIDIOC_QUERYCTRL, &control) < 0) {
			if (errno == EINVAL)
				break; /* end of enumeration */
			else {
				gst_element_error(GST_ELEMENT(v4l2element),
					"Failed to get no. %d in control enumeration for %s: %s",
					n, v4l2element->device, sys_errlist[errno]);
				return FALSE;
			}
		}
		ctrlptr = g_malloc(sizeof(control));
		memcpy(ctrlptr, &control, sizeof(control));
		v4l2element->controls = g_list_append(v4l2element->controls, ctrlptr);
		if (control.type == V4L2_CTRL_TYPE_MENU) {
			struct v4l2_querymenu menu, *mptr;
			int i;
			menu.id = n;
			for (i=0;;i++) {
				menu.index = i;
				if (ioctl(v4l2element->video_fd, VIDIOC_QUERYMENU, &menu) < 0) {
					if (errno == EINVAL)
						break; /* end of enumeration */
					else {
						gst_element_error(GST_ELEMENT(v4l2element),
							"Failed to get no. %d in menu %d enumeration for %s: %s",
							i, n, v4l2element->device, sys_errlist[errno]);
						return FALSE;
					}
				}
				mptr = g_malloc(sizeof(menu));
				memcpy(mptr, &menu, sizeof(menu));
				menus = g_list_append(menus, mptr);
			}
		}
		v4l2element->menus = g_list_append(v4l2element->menus, menus);
	}

	return TRUE;
}


static void
gst_v4l2_empty_lists (GstV4l2Element *v4l2element)
{
	DEBUG("deleting enumerations");

	/* empty lists */
	while (g_list_length(v4l2element->inputs) > 0) {
		gpointer data = g_list_nth_data(v4l2element->inputs, 0);
		v4l2element->inputs = g_list_remove(v4l2element->inputs, data);
		g_free(data);
	}
	while (g_list_length(v4l2element->outputs) > 0) {
		gpointer data = g_list_nth_data(v4l2element->outputs, 0);
		v4l2element->outputs = g_list_remove(v4l2element->outputs, data);
		g_free(data);
	}
	while (g_list_length(v4l2element->norms) > 0) {
		gpointer data = g_list_nth_data(v4l2element->norms, 0);
		v4l2element->norms = g_list_remove(v4l2element->norms, data);
		g_free(data);
	}
	while (g_list_length(v4l2element->formats) > 0) {
		gpointer data = g_list_nth_data(v4l2element->formats, 0);
		v4l2element->formats = g_list_remove(v4l2element->formats, data);
		g_free(data);
	}
	while (g_list_length(v4l2element->controls) > 0) {
		gpointer data = g_list_nth_data(v4l2element->controls, 0);
		v4l2element->controls = g_list_remove(v4l2element->controls, data);
		g_free(data);
	}
	v4l2element->menus = g_list_remove_all(v4l2element->menus, NULL);
	while (g_list_length(v4l2element->menus) > 0) {
		GList *items = (GList *) g_list_nth_data(v4l2element->menus, 0);
		v4l2element->inputs = g_list_remove(v4l2element->inputs, items);
		while (g_list_length(items) > 0) {
			gpointer data = g_list_nth_data(v4l2element->menus, 0);
			items = g_list_remove(items, data);
			g_free(data);
		}
	}
}


/******************************************************
 * gst_v4l2_open():
 *   open the video device (v4l2element->device)
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2_open (GstV4l2Element *v4l2element)
{
	DEBUG("Trying to open device %s", v4l2element->device);
	GST_V4L2_CHECK_NOT_OPEN(v4l2element);
	GST_V4L2_CHECK_NOT_ACTIVE(v4l2element);

	/* be sure we have a device */
	if (!v4l2element->device)
		v4l2element->device = g_strdup("/dev/video");

	/* open the device */
	v4l2element->video_fd = open(v4l2element->device, O_RDWR);
	if (!GST_V4L2_IS_OPEN(v4l2element)) {
		gst_element_error(GST_ELEMENT(v4l2element),
			"Failed to open device %s: %s",
			v4l2element->device, sys_errlist[errno]);
		goto error;
	}

	/* get capabilities */
	if (!gst_v4l2_get_capabilities(v4l2element)) {
		goto error;
	}

	/* and get the video window */
	if (GST_V4L2_IS_OVERLAY(v4l2element)) {
		if (ioctl(v4l2element->video_fd, VIDIOC_G_WIN, &(v4l2element->vwin)) < 0) {
			gst_element_error(GST_ELEMENT(v4l2element),
				"Failed to get video window properties of %s: %s",
				v4l2element->device, sys_errlist[errno]);
			goto error;
		}
	}

	/* create enumerations */
	if (!gst_v4l2_fill_lists(v4l2element))
		goto error;

	gst_info("Opened device '%s' (%s) successfully\n",
		v4l2element->vcap.name, v4l2element->device);

	return TRUE;

error:
	if (GST_V4L2_IS_OPEN(v4l2element)) {
		/* close device */
		close(v4l2element->video_fd);
		v4l2element->video_fd = -1;
	}
	/* empty lists */
	gst_v4l2_empty_lists(v4l2element);

	return FALSE;
}


/******************************************************
 * gst_v4l2_close():
 *   close the video device (v4l2element->video_fd)
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2_close (GstV4l2Element *v4l2element)
{
	DEBUG("Trying to close %s", v4l2element->device);
	GST_V4L2_CHECK_OPEN(v4l2element);
	GST_V4L2_CHECK_NOT_ACTIVE(v4l2element);

	/* close device */
	close(v4l2element->video_fd);
	v4l2element->video_fd = -1;

	/* empty lists */
	gst_v4l2_empty_lists(v4l2element);

	return TRUE;
}


/******************************************************
 * gst_v4l2_get_norm()
 *   Get the norm of the current device
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2_get_norm (GstV4l2Element *v4l2element,
                   gint           *norm)
{
	struct v4l2_standard standard;
	gint n;

	DEBUG("getting norm");
	GST_V4L2_CHECK_OPEN(v4l2element);

	if (ioctl(v4l2element->video_fd, VIDIOC_G_STD, &standard) < 0) {
		gst_element_error(GST_ELEMENT(v4l2element),
			"Failed to get the current norm for device %s: %s",
			v4l2element->device, sys_errlist[errno]);
		return FALSE;
	}

	/* try to find out what norm number this actually is */
	for (n=0;n<g_list_length(v4l2element->norms);n++) {
		struct v4l2_enumstd *stdptr = (struct v4l2_enumstd *) g_list_nth_data(v4l2element->norms, n);
		if (!strcmp(stdptr->std.name, standard.name)) {
			*norm = n;
			return TRUE;
		}
	}

	gst_element_error(GST_ELEMENT(v4l2element),
		"Failed to find norm '%s' in our list of available norms for device %s",
		standard.name, v4l2element->device);
	return FALSE;
}


/******************************************************
 * gst_v4l2_set_norm()
 *   Set the norm of the current device
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2_set_norm (GstV4l2Element *v4l2element,
                   gint            norm)
{
	struct v4l2_enumstd *standard;

	DEBUG("trying to set norm to %d", norm);
	GST_V4L2_CHECK_OPEN(v4l2element);
	GST_V4L2_CHECK_NOT_ACTIVE(v4l2element);

	if (norm < 0 || norm >= g_list_length(v4l2element->norms)) {
		gst_element_error(GST_ELEMENT(v4l2element),
			"Invalid norm number %d (%d-%d)",
			norm, 0, g_list_length(v4l2element->norms));
		return FALSE;
	}

	standard = (struct v4l2_enumstd *) g_list_nth_data(v4l2element->norms, norm);

	if (ioctl(v4l2element->video_fd, VIDIOC_S_STD, &standard->std) < 0) {
		gst_element_error(GST_ELEMENT(v4l2element),
			"Failed to set norm '%s' (%d) for device %s: %s",
			standard->std.name, norm, v4l2element->device, sys_errlist[errno]);
		return FALSE;
	}

	return TRUE;
}


/******************************************************
 * gst_v4l2_get_norm_names()
 *   Get the list of available norms
 * return value: the list
 ******************************************************/

GList *
gst_v4l2_get_norm_names (GstV4l2Element *v4l2element)
{
	GList *names = NULL;
	gint n;

	DEBUG("getting a list of norm names");

	for (n=0;n<g_list_length(v4l2element->norms);n++) {
		struct v4l2_enumstd *standard = (struct v4l2_enumstd *) g_list_nth_data(v4l2element->norms, n);
		names = g_list_append(names, g_strdup(standard->std.name));
	}

	return names;
}


/******************************************************
 * gst_v4l2_get_input()
 *   Get the input of the current device
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2_get_input (GstV4l2Element *v4l2element,
                    gint           *input)
{
	gint n;

	DEBUG("trying to get input");
	GST_V4L2_CHECK_OPEN(v4l2element);

	if (ioctl(v4l2element->video_fd, VIDIOC_G_INPUT, &n) < 0) {
		gst_element_error(GST_ELEMENT(v4l2element),
			"Failed to get current input on device %s: %s",
			v4l2element->device, sys_errlist[errno]);
		return FALSE;
	}

	*input = n;

	return TRUE;
}


/******************************************************
 * gst_v4l2_set_input()
 *   Set the input of the current device
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2_set_input (GstV4l2Element *v4l2element,
                    gint            input)
{
	DEBUG("trying to set input to %d", input);
	GST_V4L2_CHECK_OPEN(v4l2element);
	GST_V4L2_CHECK_NOT_ACTIVE(v4l2element);

	if (input < 0 || input >= g_list_length(v4l2element->inputs)) {
		gst_element_error(GST_ELEMENT(v4l2element),
			"Invalid input number %d (%d-%d)",
			input, 0, g_list_length(v4l2element->inputs));
		return FALSE;
	}

	if (ioctl(v4l2element->video_fd, VIDIOC_S_INPUT, &input) < 0) {
		gst_element_error(GST_ELEMENT(v4l2element),
			"Failed to set input %d on device %s: %s",
			input, v4l2element->device, sys_errlist[errno]);
		return FALSE;
	}

	return TRUE;
}


/******************************************************
 * gst_v4l2_get_input_names()
 *   Get the list of available input channels
 * return value: the list
 ******************************************************/

GList *
gst_v4l2_get_input_names (GstV4l2Element *v4l2element)
{
	GList *names = NULL;
	gint n;

	DEBUG("getting a list of input names");

	for (n=0;n<g_list_length(v4l2element->inputs);n++) {
		struct v4l2_input *input = (struct v4l2_input *) g_list_nth_data(v4l2element->inputs, n);
		names = g_list_append(names, g_strdup(input->name));
	}

	return names;
}


/******************************************************
 * gst_v4l2_get_output()
 *   Get the output of the current device
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2_get_output (GstV4l2Element *v4l2element,
                     gint           *output)
{
	gint n;

	DEBUG("trying to get output");
	GST_V4L2_CHECK_OPEN(v4l2element);

	if (ioctl(v4l2element->video_fd, VIDIOC_G_OUTPUT, &n) < 0) {
		gst_element_error(GST_ELEMENT(v4l2element),
			"Failed to get current output on device %s: %s",
			v4l2element->device, sys_errlist[errno]);
		return FALSE;
	}

	*output = n;

	return TRUE;
}


/******************************************************
 * gst_v4l2_set_output()
 *   Set the output of the current device
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2_set_output (GstV4l2Element *v4l2element,
                     gint            output)
{
	DEBUG("trying to set output to %d", output);
	GST_V4L2_CHECK_OPEN(v4l2element);
	GST_V4L2_CHECK_NOT_ACTIVE(v4l2element);

	if (output < 0 || output >= g_list_length(v4l2element->outputs)) {
		gst_element_error(GST_ELEMENT(v4l2element),
			"Invalid output number %d (%d-%d)",
			output, 0, g_list_length(v4l2element->outputs));
		return FALSE;
	}

	if (ioctl(v4l2element->video_fd, VIDIOC_S_OUTPUT, &output) < 0) {
		gst_element_error(GST_ELEMENT(v4l2element),
			"Failed to set output %d on device %s: %s",
			output, v4l2element->device, sys_errlist[errno]);
		return FALSE;
	}

	return TRUE;
}


/******************************************************
 * gst_v4l2_get_output_names()
 *   Get the list of available output channels
 * return value: the list, or NULL on error
 ******************************************************/

GList *
gst_v4l2_get_output_names (GstV4l2Element *v4l2element)
{
	GList *names = NULL;
	gint n;

	DEBUG("getting a list of output names");

	for (n=0;n<g_list_length(v4l2element->outputs);n++) {
		struct v4l2_output *output = (struct v4l2_output *) g_list_nth_data(v4l2element->outputs, n);
		names = g_list_append(names, g_strdup(output->name));
	}

	return names;
}


/******************************************************
 * gst_v4l_has_tuner():
 *   Check whether the device has a tuner
 * return value: TRUE if it has a tuner, else FALSE
 ******************************************************/

gboolean
gst_v4l2_has_tuner (GstV4l2Element *v4l2element)
{
	gint input_num;
	struct v4l2_input *input;

	DEBUG("detecting whether device has a tuner");
	GST_V4L2_CHECK_OPEN(v4l2element);

	if (!gst_v4l2_get_input(v4l2element, &input_num))
		return FALSE;

	input = (struct v4l2_input *) g_list_nth_data(v4l2element->inputs, input_num);

	return (input->type == V4L2_INPUT_TYPE_TUNER &&
	        v4l2element->vcap.flags & V4L2_FLAG_TUNER);
}


/******************************************************
 * gst_v4l_get_frequency():
 *   get the current frequency
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2_get_frequency (GstV4l2Element *v4l2element,
                        gulong         *frequency)
{
	gint n;

	DEBUG("getting current tuner frequency");
	GST_V4L2_CHECK_OPEN(v4l2element);

	if (!gst_v4l2_has_tuner(v4l2element))
		return FALSE;

	if (ioctl(v4l2element->video_fd, VIDIOC_G_FREQ, &n) < 0) {
		gst_element_error(GST_ELEMENT(v4l2element),
			"Failed to get current tuner frequency for device %s: %s",
			v4l2element->device, sys_errlist[errno]);
		return FALSE;
	}

	*frequency = n;

	return TRUE;
}


/******************************************************
 * gst_v4l_set_frequency():
 *   set frequency
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2_set_frequency (GstV4l2Element *v4l2element,
                        gulong          frequency)
{
	gint n = frequency;

	DEBUG("setting current tuner frequency to %lu", frequency);
	GST_V4L2_CHECK_OPEN(v4l2element);
	GST_V4L2_CHECK_NOT_ACTIVE(v4l2element);

	if (!gst_v4l2_has_tuner(v4l2element))
		return FALSE;

	if (ioctl(v4l2element->video_fd, VIDIOC_G_FREQ, &n) < 0) {
		gst_element_error(GST_ELEMENT(v4l2element),
			"Failed to set tuner frequency to %lu for device %s: %s",
			frequency, v4l2element->device, sys_errlist[errno]);
		return FALSE;
	}

	return TRUE;
}


/******************************************************
 * gst_v4l_signal_strength():
 *   get the strength of the signal on the current input
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2_signal_strength (GstV4l2Element *v4l2element,
                          gulong         *signal_strength)
{
	struct v4l2_tuner tuner;

	DEBUG("trying to get signal strength");
	GST_V4L2_CHECK_OPEN(v4l2element);

	if (ioctl(v4l2element->video_fd, VIDIOC_G_TUNER, &tuner) < 0) {
		gst_element_error(GST_ELEMENT(v4l2element),
			"Failed to set signal strength for device %s: %s",
			v4l2element->device, sys_errlist[errno]);
		return FALSE;
	}

	*signal_strength = tuner.signal;

	return TRUE;
}


/******************************************************
 * gst_v4l_has_audio():
 *   Check whether the device has audio capabilities
 * return value: TRUE if it has a tuner, else FALSE
 ******************************************************/

gboolean
gst_v4l2_has_audio (GstV4l2Element *v4l2element)
{
	gint input_num;
	struct v4l2_input *input;

	DEBUG("detecting whether device has audio");
	GST_V4L2_CHECK_OPEN(v4l2element);

	if (!gst_v4l2_get_input(v4l2element, &input_num))
		return FALSE;

	input = (struct v4l2_input *) g_list_nth_data(v4l2element->inputs, input_num);

	return (input->capability & V4L2_INPUT_CAP_AUDIO);
}


/******************************************************
 * gst_v4l_get_attributes():
 *   get a list of attributes available on this device
 * return value: the list
 ******************************************************/

GList *
gst_v4l2_get_attributes	(GstV4l2Element *v4l2element)
{
	gint i;
	GList *list = NULL;

	DEBUG("getting a list of available attributes");

	for (i=0;i<g_list_length(v4l2element->controls);i++) {
		struct v4l2_queryctrl *control = (struct v4l2_queryctrl *) g_list_nth_data(v4l2element->controls, i);
		GstV4l2Attribute* attribute = g_malloc(sizeof(GstV4l2Attribute));
		attribute->name = g_strdup(control->name);
		attribute->index = i;
		attribute->list_items = NULL;
		switch (control->type) {
			case V4L2_CTRL_TYPE_INTEGER:
				attribute->val_type = GST_V4L2_ATTRIBUTE_VALUE_TYPE_INT;
				break;
			case V4L2_CTRL_TYPE_BOOLEAN:
				attribute->val_type = GST_V4L2_ATTRIBUTE_VALUE_TYPE_BOOLEAN;
				break;
			case V4L2_CTRL_TYPE_MENU: {
				/* list items */
				gint n;
				GList *menus = (GList *) g_list_nth_data(v4l2element->menus, i);
				for (n=0;n<g_list_length(menus);n++) {
					struct v4l2_querymenu *menu = g_list_nth_data(menus, n);
					attribute->list_items = g_list_append(attribute->list_items, g_strdup(menu->name));
				}
				attribute->val_type = GST_V4L2_ATTRIBUTE_VALUE_TYPE_LIST;
				break; }
			case V4L2_CTRL_TYPE_BUTTON:
				attribute->val_type = GST_V4L2_ATTRIBUTE_VALUE_TYPE_BUTTON;
				break;
		}
		switch (control->category) {
			case V4L2_CTRL_CAT_VIDEO:
				attribute->type = GST_V4L2_ATTRIBUTE_TYPE_VIDEO;
				break;
			case V4L2_CTRL_CAT_AUDIO:
				attribute->type = GST_V4L2_ATTRIBUTE_TYPE_AUDIO;
				break;
			case V4L2_CTRL_CAT_EFFECT:
				attribute->type = GST_V4L2_ATTRIBUTE_TYPE_EFFECT;
				break;
		}
		gst_v4l2_get_attribute(v4l2element, i, &attribute->value);
		attribute->min = control->minimum;
		attribute->max = control->maximum;
	}

	return list;
}


/******************************************************
 * gst_v4l_get_attribute():
 *   try to get the value of one specific attribute
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2_get_attribute	(GstV4l2Element *v4l2element,
                         gint            attribute_num,
                         gint           *value)
{
	struct v4l2_control control;

	DEBUG("getting value of attribute %d", attribute_num);
	GST_V4L2_CHECK_OPEN(v4l2element);

	if (attribute_num < 0 || attribute_num >= g_list_length(v4l2element->controls)) {
		gst_element_error(GST_ELEMENT(v4l2element),
			"Invalid control ID %d", attribute_num);
		return FALSE;
	}

	control.id = attribute_num;

	if (ioctl(v4l2element->video_fd, VIDIOC_G_CTRL, &control) < 0) {
		gst_element_error(GST_ELEMENT(v4l2element),
			"Failed to get value for control %d on device %s: %s",
			attribute_num, v4l2element->device, sys_errlist[errno]);
		return FALSE;
	}

	*value = control.value;

	return TRUE;
}


/******************************************************
 * gst_v4l_set_attribute():
 *   try to set the value of one specific attribute
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2_set_attribute	(GstV4l2Element *v4l2element,
                         gint            attribute_num,
                         gint            value)
{
	struct v4l2_control control;

	DEBUG("setting value of attribute %d to %d", attribute_num, value);
	GST_V4L2_CHECK_OPEN(v4l2element);

	if (attribute_num < 0 || attribute_num >= g_list_length(v4l2element->controls)) {
		gst_element_error(GST_ELEMENT(v4l2element),
			"Invalid control ID %d", attribute_num);
		return FALSE;
	}

	control.id = attribute_num;
	control.value = value;

	if (ioctl(v4l2element->video_fd, VIDIOC_S_CTRL, &control) < 0) {
		gst_element_error(GST_ELEMENT(v4l2element),
			"Failed to set value %d for control %d on device %s: %s",
			value, attribute_num, v4l2element->device, sys_errlist[errno]);
		return FALSE;
	}

	return TRUE;
}
    
