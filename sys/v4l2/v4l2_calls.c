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
#include "gstv4l2tuner.h"
#include "gstv4l2xoverlay.h"
#include "gstv4l2colorbalance.h"

#include "gstv4l2src.h"

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
	const GList *pads =
		gst_element_get_pad_list (GST_ELEMENT (v4l2element));
	GstPadDirection dir = GST_PAD_UNKNOWN;

	DEBUG("getting enumerations");
	GST_V4L2_CHECK_OPEN(v4l2element);

	/* sinks have outputs, all others have inputs */
	if (pads && g_list_length ((GList *) pads) == 1)
		dir = GST_PAD_DIRECTION (GST_PAD (pads->data));

	if (dir != GST_PAD_SINK) {
	/* and now, the inputs */
		for (n=0;;n++) {
			struct v4l2_input input;
			GstV4l2TunerChannel *v4l2channel;
			GstTunerChannel *channel;

			input.index = n;
			if (ioctl(v4l2element->video_fd, VIDIOC_ENUMINPUT,
				  &input) < 0) {
				if (errno == EINVAL)
					break; /* end of enumeration */
				else {
					gst_element_error(GST_ELEMENT(v4l2element),
						"Failed to get no. %d in input enumeration for %s: %s",
						n, v4l2element->device,
						g_strerror(errno));
					return FALSE;
				}
			}

			v4l2channel =
				g_object_new(GST_TYPE_V4L2_TUNER_CHANNEL, NULL);
			channel = GST_TUNER_CHANNEL(v4l2channel);
			channel->label = g_strdup(input.name);
			channel->flags = GST_TUNER_CHANNEL_INPUT;
			v4l2channel->index = n;
			if (input.type == V4L2_INPUT_TYPE_TUNER) {
				struct v4l2_tuner vtun;

				v4l2channel->tuner = input.tuner;
				channel->flags |= GST_TUNER_CHANNEL_FREQUENCY;

				vtun.index = input.tuner;
				if (ioctl(v4l2element->video_fd, VIDIOC_G_TUNER,
					  &vtun) < 0) {
					gst_element_error(GST_ELEMENT(v4l2element),
						"Failed to get tuner %d settings on %s: %s",
						input.tuner,
						v4l2element->device,
						g_strerror(errno));
					g_object_unref(G_OBJECT(channel));
					return FALSE;
				}
				channel->min_frequency = vtun.rangelow;
				channel->max_frequency = vtun.rangehigh;
				channel->min_signal = 0;
				channel->max_signal = 0xffff;
			}
			if (input.audioset) {
				/* we take the first. We don't care for
				 * the others for now */
				while (!(input.audioset &
				         (1<<v4l2channel->audio)))
					v4l2channel->audio++;
				channel->flags |= GST_TUNER_CHANNEL_AUDIO;
			}

			v4l2element->channels =
				g_list_append(v4l2element->channels,
					      (gpointer) channel);
		}
	} else {
		/* outputs */
		for (n=0;;n++) {
			struct v4l2_output output;
			GstV4l2TunerChannel *v4l2channel;
			GstTunerChannel *channel;

			output.index = n;
			if (ioctl(v4l2element->video_fd, VIDIOC_ENUMOUTPUT,
				  &output) < 0) {
				if (errno == EINVAL)
					break; /* end of enumeration */
				else {
					gst_element_error(GST_ELEMENT(v4l2element),
						"Failed to get no. %d in output enumeration for %s: %s",
						n, v4l2element->device,
						g_strerror(errno));
					return FALSE;
				}
			}

			v4l2channel = g_object_new(GST_TYPE_V4L2_TUNER_CHANNEL, NULL);
			channel = GST_TUNER_CHANNEL(v4l2channel);
			channel->label = g_strdup(output.name);
			channel->flags = GST_TUNER_CHANNEL_OUTPUT;
			v4l2channel->index = n;
			if (output.audioset) {
				/* we take the first. We don't care for
				 * the others for now */
				while (!(output.audioset &
				         (1<<v4l2channel->audio)))
					v4l2channel->audio++;
				channel->flags |= GST_TUNER_CHANNEL_AUDIO;
			}

			v4l2element->channels =
				g_list_append(v4l2element->channels,
					      (gpointer) channel);
		}
	}

	/* norms... */
	for (n=0;;n++) {
		struct v4l2_standard standard;
		GstV4l2TunerNorm *v4l2norm;
		GstTunerNorm *norm;

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

		v4l2norm = g_object_new(GST_TYPE_V4L2_TUNER_NORM, NULL);
		norm = GST_TUNER_NORM (v4l2norm);
		norm->label = g_strdup(standard.name);
		norm->fps = (gfloat) standard.frameperiod.denominator /
				standard.frameperiod.numerator;
		v4l2norm->index = standard.id;

		v4l2element->norms = g_list_append(v4l2element->norms,
						   (gpointer) norm);
	}

	/* and lastly, controls+menus (if appropriate) */
	for (n=V4L2_CID_BASE;;n++) {
		struct v4l2_queryctrl control;
		GstV4l2ColorBalanceChannel *v4l2channel;
		GstColorBalanceChannel *channel;

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

		switch (n) {
			case V4L2_CID_BRIGHTNESS:
			case V4L2_CID_CONTRAST:
			case V4L2_CID_SATURATION:
			case V4L2_CID_HUE:
			case V4L2_CID_BLACK_LEVEL:
			case V4L2_CID_AUTO_WHITE_BALANCE:
			case V4L2_CID_DO_WHITE_BALANCE:
			case V4L2_CID_RED_BALANCE:
			case V4L2_CID_BLUE_BALANCE:
			case V4L2_CID_GAMMA:
			case V4L2_CID_EXPOSURE:
			case V4L2_CID_AUTOGAIN:
			case V4L2_CID_GAIN:
				/* we only handle these for now */
				break;
			default:
				DEBUG("ControlID %s (%d) unhandled, FIXME",
				      control.name, n);
				control.id++;
				break;
		}
		if (n != control.id)
			continue;

		v4l2channel = g_object_new(GST_TYPE_V4L2_COLOR_BALANCE_CHANNEL,
					   NULL);
		channel = GST_COLOR_BALANCE_CHANNEL(v4l2channel);
		channel->label = g_strdup(control.name);
		v4l2channel->index = n;

#if 0
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
#endif

		switch (control.type) {
			case V4L2_CTRL_TYPE_INTEGER:
				channel->min_value = control.minimum;
				channel->max_value = control.maximum;
				break;
			case V4L2_CTRL_TYPE_BOOLEAN:
				channel->min_value = FALSE;
				channel->max_value = TRUE;
				break;
			default:
				channel->min_value =
					channel->max_value = 0;
				break;
		}

		v4l2element->colors = g_list_append(v4l2element->colors,
						    (gpointer) channel);
	}

	return TRUE;
}


static void
gst_v4l2_empty_lists (GstV4l2Element *v4l2element)
{
	DEBUG("deleting enumerations");

	g_list_foreach (v4l2element->channels, (GFunc) g_object_unref, NULL);
	g_list_free (v4l2element->channels);
	v4l2element->channels = NULL;

	g_list_foreach (v4l2element->norms, (GFunc) g_object_unref, NULL);
	g_list_free (v4l2element->norms);
	v4l2element->norms = NULL;

	g_list_foreach (v4l2element->colors, (GFunc) g_object_unref, NULL);
	g_list_free (v4l2element->colors);
	v4l2element->colors = NULL;
}

/* FIXME: move this stuff to gstv4l2tuner.c? */

static void
gst_v4l2_set_defaults (GstV4l2Element *v4l2element)
{
  GstTunerNorm *norm = NULL;
  GstTunerChannel *channel = NULL;
  GstTuner *tuner = GST_TUNER (v4l2element);
  
  if (v4l2element->norm)
    norm = gst_tuner_find_norm_by_name (tuner, v4l2element->norm);
  if (norm) {
    gst_tuner_set_norm (tuner, norm);
  } else {
    norm = GST_TUNER_NORM (gst_tuner_get_norm (GST_TUNER (v4l2element)));
    v4l2element->norm = g_strdup (norm->label);
    gst_tuner_norm_changed (tuner, norm);
    g_object_notify (G_OBJECT (v4l2element), "norm"); 
  }
  
  if (v4l2element->channel) 
    channel = gst_tuner_find_channel_by_name (tuner, v4l2element->channel);
  if (channel) {
    gst_tuner_set_channel (tuner, channel);
  } else {
    channel = GST_TUNER_CHANNEL (gst_tuner_get_channel (GST_TUNER (v4l2element)));
    v4l2element->channel = g_strdup (channel->label);
    gst_tuner_channel_changed (tuner, channel);
    g_object_notify (G_OBJECT (v4l2element), "channel"); 
  }
  if (v4l2element->frequency != 0) {
    gst_tuner_set_frequency (tuner, channel, v4l2element->frequency);
  } else {
    v4l2element->frequency = gst_tuner_get_frequency (tuner, channel);
    if (v4l2element->frequency == 0) {
      /* guess */
      gst_tuner_set_frequency (tuner, channel, 1000);
    } else {
      g_object_notify (G_OBJECT (v4l2element), "frequency");
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
			v4l2element->device, g_strerror(errno));
		goto error;
	}

	/* get capabilities */
	if (!gst_v4l2_get_capabilities(v4l2element)) {
		goto error;
	}

	/* do we need to be a capture device? */
	if (GST_IS_V4L2SRC(v4l2element) &&
	    !(v4l2element->vcap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		gst_element_error(GST_ELEMENT(v4l2element),
				  "Not a capture device (0x%x)",
				  v4l2element->vcap.capabilities);
		goto error;
	}

	/* create enumerations */
	if (!gst_v4l2_fill_lists(v4l2element))
		goto error;

	/* set defaults */
	gst_v4l2_set_defaults (v4l2element);

	GST_INFO_OBJECT (v4l2element, "Opened device '%s' (%s) successfully\n",
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
                   v4l2_std_id    *norm)
{
	DEBUG("getting norm");
	GST_V4L2_CHECK_OPEN(v4l2element);

	if (ioctl(v4l2element->video_fd, VIDIOC_G_STD, norm) < 0) {
		gst_element_error(GST_ELEMENT(v4l2element),
			"Failed to get the current norm for device %s: %s",
			v4l2element->device, g_strerror(errno));
		return FALSE;
	}

	return TRUE;
}


/******************************************************
 * gst_v4l2_set_norm()
 *   Set the norm of the current device
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2_set_norm (GstV4l2Element *v4l2element,
                   v4l2_std_id     norm)
{
	DEBUG("trying to set norm to %llx", norm);
	GST_V4L2_CHECK_OPEN(v4l2element);
	GST_V4L2_CHECK_NOT_ACTIVE(v4l2element);

	if (ioctl(v4l2element->video_fd, VIDIOC_S_STD, &norm) < 0) {
		gst_element_error(GST_ELEMENT(v4l2element),
			"Failed to set norm 0x%llx for device %s: %s",
			norm, v4l2element->device, g_strerror(errno));
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

	if (ioctl(v4l2element->video_fd, VIDIOC_S_OUTPUT, &output) < 0) {
		gst_element_error(GST_ELEMENT(v4l2element),
			"Failed to set output %d on device %s: %s",
			output, v4l2element->device, g_strerror(errno));
		return FALSE;
	}

	return TRUE;
}


/******************************************************
 * gst_v4l2_get_frequency():
 *   get the current frequency
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2_get_frequency (GstV4l2Element *v4l2element,
			gint            tunernum,
                        gulong         *frequency)
{
	struct v4l2_frequency freq;

	DEBUG("getting current tuner frequency");
	GST_V4L2_CHECK_OPEN(v4l2element);

	freq.tuner = tunernum;
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
			gint            tunernum,
                        gulong          frequency)
{
	struct v4l2_frequency freq;

	DEBUG("setting current tuner frequency to %lu", frequency);
	GST_V4L2_CHECK_OPEN(v4l2element);
	GST_V4L2_CHECK_NOT_ACTIVE(v4l2element);

	freq.tuner = tunernum;
	/* fill in type - ignore error */
	ioctl(v4l2element->video_fd, VIDIOC_G_FREQUENCY, &freq);
	freq.frequency = frequency;

	if (ioctl(v4l2element->video_fd, VIDIOC_S_FREQUENCY, &freq) < 0) {
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
			  gint            tunernum,
                          gulong         *signal_strength)
{
	struct v4l2_tuner tuner;

	DEBUG("trying to get signal strength");
	GST_V4L2_CHECK_OPEN(v4l2element);

	tuner.index = tunernum;
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
 * gst_v4l2_get_attribute():
 *   try to get the value of one specific attribute
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2_get_attribute	(GstV4l2Element *v4l2element,
                         int             attribute_num,
                         int            *value)
{
	struct v4l2_control control;

	GST_V4L2_CHECK_OPEN(v4l2element);

	DEBUG("getting value of attribute %d", attribute_num);

	control.id = attribute_num;

	if (ioctl(v4l2element->video_fd, VIDIOC_G_CTRL, &control) < 0) {
		gst_element_error(GST_ELEMENT(v4l2element),
			"Failed to get value for control %d on device %s: %s",
			attribute_num, v4l2element->device, g_strerror(errno));
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
gst_v4l2_set_attribute	(GstV4l2Element *v4l2element,
                         int             attribute_num,
                         const int       value)
{
	struct v4l2_control control;

	GST_V4L2_CHECK_OPEN(v4l2element);

	DEBUG("setting value of attribute %d to %d", attribute_num, value);

	control.id = attribute_num;
	control.value = value;

	if (ioctl(v4l2element->video_fd, VIDIOC_S_CTRL, &control) < 0) {
		gst_element_error(GST_ELEMENT(v4l2element),
			"Failed to set value %d for control %d on device %s: %s",
			value, attribute_num, v4l2element->device, g_strerror(errno));
		return FALSE;
	}

	return TRUE;
}
    
