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
#include <unistd.h>
#include "v4l2_calls.h"

#define DEBUG(format, args...) \
	GST_DEBUG_OBJECT (\
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
			v4l2element->device, g_strerror(errno));
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
					n, v4l2element->device, g_strerror(errno));
				return FALSE;
			}
		}
		inpptr = g_malloc(sizeof(input));
		memcpy(inpptr, &input, sizeof(input));
		v4l2element->inputs = g_list_append(v4l2element->inputs, inpptr);

		v4l2element->input_names = g_list_append(v4l2element->input_names, inpptr->name);
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
					n, v4l2element->device, g_strerror(errno));
				return FALSE;
			}
		}
		outptr = g_malloc(sizeof(output));
		memcpy(outptr, &output, sizeof(output));
		v4l2element->outputs = g_list_append(v4l2element->outputs, outptr);

		v4l2element->output_names = g_list_append(v4l2element->output_names, outptr->name);
	}

	/* norms... */
	for (n=0;;n++) {
		struct v4l2_standard standard, *stdptr;
		standard.index = n;
		if (ioctl(v4l2element->video_fd, VIDIOC_ENUMSTD, &standard) < 0) {
			if (errno == EINVAL)
				break; /* end of enumeration */
			else {
				gst_element_error(GST_ELEMENT(v4l2element),
					"Failed to get no. %d in norm enumeration for %s: %s",
					n, v4l2element->device, g_strerror(errno));
				return FALSE;
			}
		}
		stdptr = g_malloc(sizeof(standard));
		memcpy(stdptr, &standard, sizeof(standard));
		v4l2element->norms = g_list_append(v4l2element->norms, stdptr);

		v4l2element->norm_names = g_list_append(v4l2element->norm_names, stdptr->name);
	}

	/* and lastly, controls+menus (if appropriate) */
	for (n=V4L2_CID_BASE;;n++) {
		struct v4l2_queryctrl control, *ctrlptr;
		GList *menus = NULL;
		GParamSpec *spec = NULL;
		/* hacky... */
		if (n == V4L2_CID_LASTP1)
			n = V4L2_CID_PRIVATE_BASE;
		control.id = n;
		if (ioctl(v4l2element->video_fd, VIDIOC_QUERYCTRL, &control) < 0) {
			if (errno == EINVAL) {
				if (n < V4L2_CID_PRIVATE_BASE)
					continue;
				else
					break;
			} else {
				gst_element_error(GST_ELEMENT(v4l2element),
					"Failed to get no. %d in control enumeration for %s: %s",
					n, v4l2element->device, g_strerror(errno));
				return FALSE;
			}
		}
		if (control.flags & V4L2_CTRL_FLAG_DISABLED)
			continue;
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
							i, n, v4l2element->device, g_strerror(errno));
						return FALSE;
					}
				}
				mptr = g_malloc(sizeof(menu));
				memcpy(mptr, &menu, sizeof(menu));
				menus = g_list_append(menus, mptr);
			}
		}
		v4l2element->menus = g_list_append(v4l2element->menus, menus);

		switch (control.type) {
			case V4L2_CTRL_TYPE_INTEGER:
				spec = g_param_spec_int(ctrlptr->name, ctrlptr->name,
							ctrlptr->name, ctrlptr->minimum, ctrlptr->maximum,
							ctrlptr->default_value, G_PARAM_READWRITE);
				break;
			case V4L2_CTRL_TYPE_BOOLEAN:
				spec = g_param_spec_boolean(ctrlptr->name, ctrlptr->name,
							ctrlptr->name, ctrlptr->default_value,
							G_PARAM_READWRITE);
				break;
			case V4L2_CTRL_TYPE_MENU:
				/* hacky... we abuse pointer for 'no value' */
				spec = g_param_spec_pointer(ctrlptr->name, ctrlptr->name,
							ctrlptr->name, G_PARAM_WRITABLE);
				break;
			case V4L2_CTRL_TYPE_BUTTON:
				/* help?!? */
				spec = NULL;
				break;
		}

		v4l2element->control_specs = g_list_append(v4l2element->control_specs, spec);
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
	g_list_free(v4l2element->input_names);
	v4l2element->input_names = NULL;
	while (g_list_length(v4l2element->outputs) > 0) {
		gpointer data = g_list_nth_data(v4l2element->outputs, 0);
		v4l2element->outputs = g_list_remove(v4l2element->outputs, data);
		g_free(data);
	}
	g_list_free(v4l2element->output_names);
	v4l2element->output_names = NULL;
	while (g_list_length(v4l2element->norms) > 0) {
		gpointer data = g_list_nth_data(v4l2element->norms, 0);
		v4l2element->norms = g_list_remove(v4l2element->norms, data);
		g_free(data);
	}
	g_list_free(v4l2element->norm_names);
	v4l2element->norm_names = NULL;
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
	while (g_list_length(v4l2element->control_specs) > 0) {
		gpointer data = g_list_nth_data(v4l2element->control_specs, 0);
		v4l2element->control_specs = g_list_remove(v4l2element->control_specs, data);
		g_param_spec_unref(G_PARAM_SPEC(data));
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
			v4l2element->device, g_strerror(errno));
		goto error;
	}

	/* get capabilities */
	if (!gst_v4l2_get_capabilities(v4l2element)) {
		goto error;
	}

	/* create enumerations */
	if (!gst_v4l2_fill_lists(v4l2element))
		goto error;

	gst_info("Opened device '%s' (%s) successfully\n",
		v4l2element->vcap.card, v4l2element->device);

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
	v4l2_std_id std_id;
	gint n;

	DEBUG("getting norm");
	GST_V4L2_CHECK_OPEN(v4l2element);

	if (ioctl(v4l2element->video_fd, VIDIOC_G_STD, &std_id) < 0) {
		gst_element_error(GST_ELEMENT(v4l2element),
			"Failed to get the current norm for device %s: %s",
			v4l2element->device, g_strerror(errno));
		return FALSE;
	}

	/* try to find out what norm number this actually is */
	for (n=0;n<g_list_length(v4l2element->norms);n++) {
		struct v4l2_standard *stdptr = (struct v4l2_standard *) g_list_nth_data(v4l2element->norms, n);
		if (stdptr->id == std_id) {
			*norm = n;
			return TRUE;
		}
	}

	gst_element_error(GST_ELEMENT(v4l2element),
		"Failed to find norm '%llu' in our list of available norms for device %s",
		std_id, v4l2element->device);
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
	struct v4l2_standard *standard;

	DEBUG("trying to set norm to %d", norm);
	GST_V4L2_CHECK_OPEN(v4l2element);
	GST_V4L2_CHECK_NOT_ACTIVE(v4l2element);

	if (norm < 0 || norm >= g_list_length(v4l2element->norms)) {
		gst_element_error(GST_ELEMENT(v4l2element),
			"Invalid norm number %d (%d-%d)",
			norm, 0, g_list_length(v4l2element->norms));
		return FALSE;
	}

	standard = (struct v4l2_standard *) g_list_nth_data(v4l2element->norms, norm);

	if (ioctl(v4l2element->video_fd, VIDIOC_S_STD, &standard->id) < 0) {
		gst_element_error(GST_ELEMENT(v4l2element),
			"Failed to set norm '%s' (%llu) for device %s: %s",
			standard->name, standard->id, v4l2element->device, g_strerror(errno));
		return FALSE;
	}

	return TRUE;
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
			v4l2element->device, g_strerror(errno));
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
			input, v4l2element->device, g_strerror(errno));
		return FALSE;
	}

	return TRUE;
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
			v4l2element->device, g_strerror(errno));
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
			output, v4l2element->device, g_strerror(errno));
		return FALSE;
	}

	return TRUE;
}


/******************************************************
 * gst_v4l2_has_tuner():
 *   Check whether the device has a tuner
 * return value: TRUE if it has a tuner, else FALSE
 ******************************************************/

gint
gst_v4l2_has_tuner (GstV4l2Element *v4l2element,
                    gint           *tuner_num)
{
	gint input_num;
	struct v4l2_input *input;

	DEBUG("detecting whether device has a tuner");
	GST_V4L2_CHECK_OPEN(v4l2element);

	if (!gst_v4l2_get_input(v4l2element, &input_num))
		return FALSE;

	input = (struct v4l2_input *) g_list_nth_data(v4l2element->inputs, input_num);

	if (input->type == V4L2_INPUT_TYPE_TUNER &&
	    v4l2element->vcap.capabilities & V4L2_CAP_TUNER) {
		*tuner_num = input->tuner;
		return TRUE;
	}
	return FALSE;
}


/******************************************************
 * gst_v4l2_get_frequency():
 *   get the current frequency
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2_get_frequency (GstV4l2Element *v4l2element,
                        gulong         *frequency)
{
	struct v4l2_frequency freq;

	DEBUG("getting current tuner frequency");
	GST_V4L2_CHECK_OPEN(v4l2element);

	if (!gst_v4l2_has_tuner(v4l2element, &freq.tuner))
		return FALSE;

	freq.type = 0;

	if (ioctl(v4l2element->video_fd, VIDIOC_G_FREQUENCY, &freq) < 0) {
		gst_element_error(GST_ELEMENT(v4l2element),
			"Failed to get current tuner frequency for device %s: %s",
			v4l2element->device, g_strerror(errno));
		return FALSE;
	}

	*frequency = freq.frequency;

	return TRUE;
}


/******************************************************
 * gst_v4l2_set_frequency():
 *   set frequency
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2_set_frequency (GstV4l2Element *v4l2element,
                        gulong          frequency)
{
	struct v4l2_frequency freq;

	DEBUG("setting current tuner frequency to %lu", frequency);
	GST_V4L2_CHECK_OPEN(v4l2element);
	GST_V4L2_CHECK_NOT_ACTIVE(v4l2element);

	if (!gst_v4l2_has_tuner(v4l2element, &freq.tuner))
		return FALSE;

	freq.frequency = frequency;
	freq.type = 0;

	if (ioctl(v4l2element->video_fd, VIDIOC_G_FREQUENCY, &freq) < 0) {
		gst_element_error(GST_ELEMENT(v4l2element),
			"Failed to set tuner frequency to %lu for device %s: %s",
			frequency, v4l2element->device, g_strerror(errno));
		return FALSE;
	}

	return TRUE;
}


/******************************************************
 * gst_v4l2_signal_strength():
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
			"Failed to get signal strength for device %s: %s",
			v4l2element->device, g_strerror(errno));
		return FALSE;
	}

	*signal_strength = tuner.signal;

	return TRUE;
}


/******************************************************
 * gst_v4l2_has_audio():
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

	return (input->audioset != 0);
}


/******************************************************
 * gst_v4l2_control_name_to_num():
 *   convert name to num (-1 if nothing)
 ******************************************************/

static gint
gst_v4l2_control_name_to_num (GstV4l2Element *v4l2element,
                              const gchar    *name)
{
	GList *item;

	for (item = v4l2element->controls; item != NULL; item = item->next) {
		struct v4l2_queryctrl *ctrl = item->data;
		if (!strcmp(ctrl->name, name))
			return ctrl->id;
	}

	return -1;
}


/******************************************************
 * gst_v4l2_get_attribute():
 *   try to get the value of one specific attribute
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2_get_attribute	(GstElement  *element,
                         const gchar *name,
                         int         *value)
{
	struct v4l2_control control;
	GstV4l2Element *v4l2element;
	gint attribute_num = -1;

	g_return_val_if_fail(element != NULL && name != NULL && value != NULL, FALSE);
	g_return_val_if_fail(GST_IS_V4L2ELEMENT(element), FALSE);
	v4l2element = GST_V4L2ELEMENT(element);

	DEBUG("getting value of attribute %d", attribute_num);
	GST_V4L2_CHECK_OPEN(v4l2element);

	attribute_num = gst_v4l2_control_name_to_num(v4l2element, name);

	if (attribute_num < 0) {
		gst_element_error(GST_ELEMENT(v4l2element),
			"Invalid control %s", name);
		return FALSE;
	}

	control.id = attribute_num;

	if (ioctl(v4l2element->video_fd, VIDIOC_G_CTRL, &control) < 0) {
		gst_element_error(GST_ELEMENT(v4l2element),
			"Failed to get value for control %s (%d) on device %s: %s",
			name, attribute_num, v4l2element->device, g_strerror(errno));
		return FALSE;
	}

	*value = control.value;

	return TRUE;
}


/******************************************************
 * gst_v4l2_set_attribute():
 *   try to set the value of one specific attribute
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2_set_attribute	(GstElement  *element,
                         const gchar *name,
                         const int    value)
{
	struct v4l2_control control;
	GstV4l2Element *v4l2element;
	gint attribute_num = -1;

	g_return_val_if_fail(element != NULL && name != NULL, FALSE);
	g_return_val_if_fail(GST_IS_V4L2ELEMENT(element), FALSE);
	v4l2element = GST_V4L2ELEMENT(element);

	DEBUG("setting value of attribute %d to %d", attribute_num, value);
	GST_V4L2_CHECK_OPEN(v4l2element);

	attribute_num = gst_v4l2_control_name_to_num(v4l2element, name);

	if (attribute_num < 0) {
		gst_element_error(GST_ELEMENT(v4l2element),
			"Invalid control %s", name);
		return FALSE;
	}

	control.id = attribute_num;
	control.value = value;

	if (ioctl(v4l2element->video_fd, VIDIOC_S_CTRL, &control) < 0) {
		gst_element_error(GST_ELEMENT(v4l2element),
			"Failed to set value %d for control %s (%d) on device %s: %s",
			value, name, attribute_num, v4l2element->device, g_strerror(errno));
		return FALSE;
	}

	return TRUE;
}
    
