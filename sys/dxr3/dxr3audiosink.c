/* GStreamer
 * Copyright (C) 2003 Martin Soto <martinsoto@users.sourceforge.net>
 *
 * dxr3audiosink.c: Audio sink for em8300 based DVD cards.
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
#include "config.h"
#endif
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <linux/soundcard.h>
#include <linux/em8300.h>

#include <gst/gst-i18n-plugin.h>
#include <gst/gst.h>

#include "dxr3audiosink.h"
#include "dxr3marshal.h"
#include "dxr3common.h"

/* Our only supported AC3 byte rate. */
#define AC3_BYTE_RATE 48000

/* Determines the amount of time to play the given number of bytes of
   the original AC3 stream.  The result is expressed as MPEG2. */
#define TIME_FOR_BYTES(bytes) (((bytes) * 90) / 48)


/* ElementFactory information. */
static GstElementDetails dxr3audiosink_details = {
  "dxr3/Hollywood+ mpeg decoder board audio plugin",
  "Audio/Sink",
  "Feeds audio to Sigma Designs em8300 based boards",
  "Martin Soto <martinsoto@users.sourceforge.net>"
};


/* Dxr3AudioSink signals and args */
enum
{
  SIGNAL_FLUSHED,
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_DIGITAL_PCM
};

static GstStaticPadTemplate dxr3audiosink_pcm_sink_factory =
GST_STATIC_PAD_TEMPLATE ("pcm_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
	"endianness = (int) BYTE_ORDER, "
	"signed = (boolean) TRUE, "
	"width = (int) 16, "
	"depth = (int) 16, "
	"rate = (int) { 32000, 44100, 48000, 66000 }, " "channels = (int) 2")
    );

static GstStaticPadTemplate dxr3audiosink_ac3_sink_factory =
GST_STATIC_PAD_TEMPLATE ("ac3_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-ac3"
	/* no parameters needed, we don't need a parsed stream */
    )
    );


static void dxr3audiosink_class_init (Dxr3AudioSinkClass * klass);
static void dxr3audiosink_base_init (Dxr3AudioSinkClass * klass);
static void dxr3audiosink_init (Dxr3AudioSink * sink);

static void dxr3audiosink_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void dxr3audiosink_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean dxr3audiosink_open (Dxr3AudioSink * sink);
static gboolean dxr3audiosink_set_mode_pcm (Dxr3AudioSink * sink);
static gboolean dxr3audiosink_set_mode_ac3 (Dxr3AudioSink * sink);
static void dxr3audiosink_close (Dxr3AudioSink * sink);
static void dxr3audiosink_set_clock (GstElement * element, GstClock * clock);

static GstPadLinkReturn dxr3audiosink_pcm_sinklink (GstPad * pad,
    const GstCaps * caps);
static void dxr3audiosink_set_scr (Dxr3AudioSink * sink, guint32 scr);

static gboolean dxr3audiosink_handle_event (GstPad * pad, GstEvent * event);
static void dxr3audiosink_chain_pcm (GstPad * pad, GstData * buf);
static void dxr3audiosink_chain_ac3 (GstPad * pad, GstData * buf);

/* static void	dxr3audiosink_wait		(Dxr3AudioSink *sink, */
/*                                                  GstClockTime time); */
/* static int	dxr3audiosink_mvcommand		(Dxr3AudioSink *sink, */
/*                                                  int command); */

static GstElementStateReturn dxr3audiosink_change_state (GstElement * element);

static void dxr3audiosink_flushed (Dxr3AudioSink * sink);

static GstElementClass *parent_class = NULL;
static guint dxr3audiosink_signals[LAST_SIGNAL] = { 0 };


extern GType
dxr3audiosink_get_type (void)
{
  static GType dxr3audiosink_type = 0;

  if (!dxr3audiosink_type) {
    static const GTypeInfo dxr3audiosink_info = {
      sizeof (Dxr3AudioSinkClass),
      (GBaseInitFunc) dxr3audiosink_base_init,
      NULL,
      (GClassInitFunc) dxr3audiosink_class_init,
      NULL,
      NULL,
      sizeof (Dxr3AudioSink),
      0,
      (GInstanceInitFunc) dxr3audiosink_init,
    };
    dxr3audiosink_type = g_type_register_static (GST_TYPE_ELEMENT,
	"Dxr3AudioSink", &dxr3audiosink_info, 0);
  }

  return dxr3audiosink_type;
}


static void
dxr3audiosink_base_init (Dxr3AudioSinkClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&dxr3audiosink_pcm_sink_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&dxr3audiosink_ac3_sink_factory));
  gst_element_class_set_details (element_class, &dxr3audiosink_details);
}

static void
dxr3audiosink_class_init (Dxr3AudioSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  dxr3audiosink_signals[SIGNAL_FLUSHED] =
      g_signal_new ("flushed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (Dxr3AudioSinkClass, flushed),
      NULL, NULL, dxr3_marshal_VOID__VOID, G_TYPE_NONE, 0);

  klass->flushed = dxr3audiosink_flushed;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DIGITAL_PCM,
      g_param_spec_boolean ("digital-pcm", "Digital PCM",
	  "Use the digital output for PCM sound", FALSE, G_PARAM_READWRITE));

  gobject_class->set_property = dxr3audiosink_set_property;
  gobject_class->get_property = dxr3audiosink_get_property;

  gstelement_class->change_state = dxr3audiosink_change_state;
  gstelement_class->set_clock = dxr3audiosink_set_clock;
}


static void
dxr3audiosink_init (Dxr3AudioSink * sink)
{
  GstPadTemplate *temp;

  /* Create the PCM pad. */
  temp = gst_static_pad_template_get (&dxr3audiosink_pcm_sink_factory);
  sink->pcm_sinkpad = gst_pad_new_from_template (temp, "pcm_sink");
  gst_pad_set_chain_function (sink->pcm_sinkpad, dxr3audiosink_chain_pcm);
  gst_pad_set_link_function (sink->pcm_sinkpad, dxr3audiosink_pcm_sinklink);
  gst_element_add_pad (GST_ELEMENT (sink), sink->pcm_sinkpad);

  /* Create the AC3 pad. */
  temp = gst_static_pad_template_get (&dxr3audiosink_ac3_sink_factory);
  sink->ac3_sinkpad = gst_pad_new_from_template (temp, "ac3_sink");
  gst_pad_set_chain_function (sink->ac3_sinkpad, dxr3audiosink_chain_ac3);
  gst_element_add_pad (GST_ELEMENT (sink), sink->ac3_sinkpad);

  GST_FLAG_SET (GST_ELEMENT (sink), GST_ELEMENT_EVENT_AWARE);

  sink->card_number = 0;

  sink->audio_filename = NULL;
  sink->audio_fd = -1;

  sink->control_filename = NULL;
  sink->control_fd = -1;

  /* Since we don't know any better, we set the initial scr to 0. */
  sink->scr = 0;

  /* Initially don't use digital output. */
  sink->digital_pcm = FALSE;

  /* Initially there's no padder. */
  sink->padder = NULL;

  sink->mode = DXR3AUDIOSINK_MODE_NONE;
}


static void
dxr3audiosink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  Dxr3AudioSink *sink;

  /* it's not null if we got it, but it might not be ours */
  sink = DXR3AUDIOSINK (object);

  switch (prop_id) {
    case ARG_DIGITAL_PCM:
      sink->digital_pcm = g_value_get_boolean (value);
      /* Refresh the setup of the device. */
      if (sink->mode == DXR3AUDIOSINK_MODE_PCM) {
	dxr3audiosink_set_mode_pcm (sink);
      }
      g_object_notify (G_OBJECT (sink), "digital-pcm");
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
dxr3audiosink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  Dxr3AudioSink *sink;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_DXR3AUDIOSINK (object));

  sink = DXR3AUDIOSINK (object);

  switch (prop_id) {
    case ARG_DIGITAL_PCM:
      g_value_set_boolean (value, sink->digital_pcm);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static gboolean
dxr3audiosink_open (Dxr3AudioSink * sink)
{
  g_return_val_if_fail (!GST_FLAG_IS_SET (sink, DXR3AUDIOSINK_OPEN), FALSE);

  /* Compute the name of the audio device file. */
  sink->audio_filename = g_strdup_printf ("/dev/em8300_ma-%d",
      sink->card_number);

  sink->audio_fd = open (sink->audio_filename, O_WRONLY);
  if (sink->audio_fd < 0) {
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE,
	(_("Could not open audio device \"%s\" for writing."),
	    sink->audio_filename), GST_ERROR_SYSTEM);
    return FALSE;
  }

  /* Open the control device. */
  sink->control_filename = g_strdup_printf ("/dev/em8300-%d",
      sink->card_number);

  sink->control_fd = open (sink->control_filename, O_WRONLY);
  if (sink->control_fd < 0) {
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE,
	(_("Could not open control device \"%s\" for writing."),
	    sink->control_filename), GST_ERROR_SYSTEM);
    return FALSE;
  }

  GST_FLAG_SET (sink, DXR3AUDIOSINK_OPEN);

  dxr3audiosink_set_mode_pcm (sink);

  return TRUE;
}


/**
 * dxr3audiosink_set_mode:
 * @sink: The sink element to operate on.
 *
 * Set the operation mode of the element to PCM.
 */
static gboolean
dxr3audiosink_set_mode_pcm (Dxr3AudioSink * sink)
{
  int tmp, oss_mode, audiomode;

  if (sink->audio_fd == -1 || sink->control_fd == -1) {
    return FALSE;
  }

  /* Set the audio device mode. */
  oss_mode = (G_BYTE_ORDER == G_BIG_ENDIAN ? AFMT_S16_BE : AFMT_S16_LE);
  tmp = oss_mode;
  if (ioctl (sink->audio_fd, SNDCTL_DSP_SETFMT, &tmp) < 0 || tmp != oss_mode) {
    GST_ELEMENT_ERROR (sink, RESOURCE, SETTINGS,
	(_("Could not configure audio device \"%s\"."), sink->audio_filename),
	GST_ERROR_SYSTEM);
    return FALSE;
  }

  /* Set the card's general audio output mode. */
  audiomode = sink->digital_pcm ?
      EM8300_AUDIOMODE_DIGITALPCM : EM8300_AUDIOMODE_ANALOG;
  ioctl (sink->control_fd, EM8300_IOCTL_SET_AUDIOMODE, &audiomode);

  /* Set the sampling rate. */
  tmp = sink->rate;
  if (ioctl (sink->audio_fd, SNDCTL_DSP_SPEED, &tmp) < 0) {
    GST_ELEMENT_ERROR (sink, RESOURCE, SETTINGS,
	(_("Could not set audio device \"%s\" to %d Hz."), sink->audio_filename,
	    sink->rate), GST_ERROR_SYSTEM);
    return FALSE;
  }

  /* Get rid of the padder, if any. */
  if (sink->padder != NULL) {
    g_free (sink->padder);
    sink->padder = NULL;
  }

  sink->mode = DXR3AUDIOSINK_MODE_PCM;

  return TRUE;
}


/**
 * dxr3audiosink_set_mode:
 * @sink: The sink element to operate on
 *
 * Set the operation mode of the element to AC3.
 */
static gboolean
dxr3audiosink_set_mode_ac3 (Dxr3AudioSink * sink)
{
  int tmp, audiomode;

  if (sink->audio_fd == -1 || sink->control_fd == -1) {
    return FALSE;
  }

  /* Set the sampling rate. */
  tmp = AC3_BYTE_RATE;
  if (ioctl (sink->audio_fd, SNDCTL_DSP_SPEED, &tmp) < 0 ||
      tmp != AC3_BYTE_RATE) {
    GST_ELEMENT_ERROR (sink, RESOURCE, SETTINGS,
	(_("Could not set audio device \"%s\" to %d Hz."), sink->audio_filename,
	    AC3_BYTE_RATE), GST_ERROR_SYSTEM);
    return FALSE;
  }

  /* Set the card's general audio output mode to AC3. */
  audiomode = EM8300_AUDIOMODE_DIGITALAC3;
  ioctl (sink->control_fd, EM8300_IOCTL_SET_AUDIOMODE, &audiomode);

  /* Create a padder if necessary, */
  if (sink->padder == NULL) {
    sink->padder = g_malloc (sizeof (ac3_padder));
    ac3p_init (sink->padder);
  }

  sink->mode = DXR3AUDIOSINK_MODE_AC3;

  return TRUE;
}


static void
dxr3audiosink_close (Dxr3AudioSink * sink)
{
  g_return_if_fail (GST_FLAG_IS_SET (sink, DXR3AUDIOSINK_OPEN));

  if (close (sink->audio_fd) != 0) {
    GST_ELEMENT_ERROR (sink, RESOURCE, CLOSE,
	(_("Could not close audio device \"%s\"."), sink->audio_filename),
	GST_ERROR_SYSTEM);
    return;
  }

  if (close (sink->control_fd) != 0) {
    GST_ELEMENT_ERROR (sink, RESOURCE, CLOSE,
	(_("Could not close control device \"%s\"."), sink->audio_filename),
	GST_ERROR_SYSTEM);
    return;
  }

  GST_FLAG_UNSET (sink, DXR3AUDIOSINK_OPEN);

  g_free (sink->audio_filename);
  sink->audio_filename = NULL;

  g_free (sink->control_filename);
  sink->control_filename = NULL;

  /* Get rid of the padder, if any. */
  if (sink->padder != NULL) {
    g_free (sink->padder);
    sink->padder = NULL;
  }
}


static void
dxr3audiosink_set_clock (GstElement * element, GstClock * clock)
{
  Dxr3AudioSink *src = DXR3AUDIOSINK (element);

  src->clock = clock;
}


static GstPadLinkReturn
dxr3audiosink_pcm_sinklink (GstPad * pad, const GstCaps * caps)
{
  Dxr3AudioSink *sink = DXR3AUDIOSINK (gst_pad_get_parent (pad));
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  gint rate;

  if (!gst_caps_is_fixed (caps)) {
    return GST_PAD_LINK_DELAYED;
  }

  gst_structure_get_int (structure, "rate", &rate);
  sink->rate = rate;

  return GST_PAD_LINK_OK;
}


static void
dxr3audiosink_set_scr (Dxr3AudioSink * sink, guint32 scr)
{
  guint32 zero = 0;

/*   fprintf (stderr, "====== Adjusting SCR\n"); */
  ioctl (sink->control_fd, EM8300_IOCTL_SCR_SET, &zero);
  ioctl (sink->control_fd, EM8300_IOCTL_SCR_SET, &scr);
}


static gboolean
dxr3audiosink_handle_event (GstPad * pad, GstEvent * event)
{
  GstEventType type;
  Dxr3AudioSink *sink = DXR3AUDIOSINK (gst_pad_get_parent (pad));

  type = event ? GST_EVENT_TYPE (event) : GST_EVENT_UNKNOWN;

  switch (type) {
    case GST_EVENT_FLUSH:
      if (sink->control_fd >= 0) {
	unsigned audiomode;

	if (sink->mode == DXR3AUDIOSINK_MODE_AC3) {
	  audiomode = EM8300_AUDIOMODE_DIGITALPCM;
	  ioctl (sink->control_fd, EM8300_IOCTL_SET_AUDIOMODE, &audiomode);
	  audiomode = EM8300_AUDIOMODE_DIGITALAC3;
	  ioctl (sink->control_fd, EM8300_IOCTL_SET_AUDIOMODE, &audiomode);
	}

	/* Report the flush operation. */
	g_signal_emit (G_OBJECT (sink),
	    dxr3audiosink_signals[SIGNAL_FLUSHED], 0);
      }
      break;
    default:
      gst_pad_event_default (pad, event);
      break;
  }

  return TRUE;
}


static void
dxr3audiosink_chain_pcm (GstPad * pad, GstData * _data)
{
  Dxr3AudioSink *sink;
  gint bytes_written = 0;
  GstBuffer *buf;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (_data != NULL);

  sink = DXR3AUDIOSINK (gst_pad_get_parent (pad));

  if (GST_IS_EVENT (_data)) {
    dxr3audiosink_handle_event (pad, GST_EVENT (_data));
    return;
  }

  buf = GST_BUFFER (_data);

  if (sink->mode != DXR3AUDIOSINK_MODE_PCM) {
    /* Switch to PCM mode. */
    dxr3audiosink_set_mode_pcm (sink);
  }

  if (GST_FLAG_IS_SET (sink, DXR3AUDIOSINK_OPEN)) {
    if (GST_BUFFER_TIMESTAMP (buf) != GST_CLOCK_TIME_NONE) {
      /* We have a new scr value. */
      sink->scr = GSTTIME_TO_MPEGTIME (GST_BUFFER_TIMESTAMP (buf));
    }

    /* Update the system reference clock (SCR) in the card. */
    {
      unsigned in, out, odelay;
      unsigned diff;

      ioctl (sink->control_fd, EM8300_IOCTL_SCR_GET, &out);

      ioctl (sink->audio_fd, SNDCTL_DSP_GETODELAY, &odelay);

      in = MPEGTIME_TO_DXRTIME (sink->scr - (odelay * 90) / 192);
      diff = in > out ? in - out : out - in;
      if (diff > 1800) {
	dxr3audiosink_set_scr (sink, in);
      }
    }

    /* Update our SCR value. */
    sink->scr += (unsigned) (GST_BUFFER_SIZE (buf) *
	(90000.0 / ((float) sink->rate * 4)));

    /* Write the buffer to the sound device. */
    bytes_written = write (sink->audio_fd, GST_BUFFER_DATA (buf),
	GST_BUFFER_SIZE (buf));
    if (bytes_written < GST_BUFFER_SIZE (buf)) {
      fprintf (stderr, "dxr3audiosink: Warning: %d bytes should be "
	  "written, only %d bytes written\n",
	  GST_BUFFER_SIZE (buf), bytes_written);
    }
  }

  gst_buffer_unref (buf);
}


static void
dxr3audiosink_chain_ac3 (GstPad * pad, GstData * _data)
{
  Dxr3AudioSink *sink;
  gint bytes_written = 0;
  GstBuffer *buf;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (_data != NULL);

  sink = DXR3AUDIOSINK (gst_pad_get_parent (pad));

  if (GST_IS_EVENT (_data)) {
    dxr3audiosink_handle_event (pad, GST_EVENT (_data));
    return;
  }

  buf = GST_BUFFER (_data);

  if (sink->mode != DXR3AUDIOSINK_MODE_AC3) {
    /* Switch to AC3 mode. */
    dxr3audiosink_set_mode_ac3 (sink);
  }

  if (GST_FLAG_IS_SET (sink, DXR3AUDIOSINK_OPEN)) {
    int event;

    if (GST_BUFFER_TIMESTAMP (buf) != GST_CLOCK_TIME_NONE) {
      /* We have a new scr value. */

/*       fprintf (stderr, "------ Audio Time %.04f\n", */
/*                (double) GST_BUFFER_TIMESTAMP (buf) / GST_SECOND); */

      sink->scr = GSTTIME_TO_MPEGTIME (GST_BUFFER_TIMESTAMP (buf));
    }

    /* Push the new data into the padder. */
    ac3p_push_data (sink->padder, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));

    /* Parse the data. */
    event = ac3p_parse (sink->padder);
    while (event != AC3P_EVENT_PUSH) {
      switch (event) {
	case AC3P_EVENT_FRAME:
	  /* We have a new frame: */

	  /* Update the system reference clock (SCR) in the card. */
	{
	  unsigned in, out, odelay;
	  unsigned diff;

	  ioctl (sink->control_fd, EM8300_IOCTL_SCR_GET, &out);

	  ioctl (sink->audio_fd, SNDCTL_DSP_GETODELAY, &odelay);
	  /* 192000 bytes/sec */

	  in = MPEGTIME_TO_DXRTIME (sink->scr - (odelay * 90) / 192);
	  diff = in > out ? in - out : out - in;
	  if (diff > 1800) {
	    dxr3audiosink_set_scr (sink, in);
	  }
	}

	  /* Update our SCR value. */
	  sink->scr += TIME_FOR_BYTES (ac3p_frame_size (sink->padder));

	  /* Write the frame to the sound device. */
	  bytes_written = write (sink->audio_fd, ac3p_frame (sink->padder),
	      AC3P_IEC_FRAME_SIZE);

	  if (bytes_written < AC3P_IEC_FRAME_SIZE) {
	    fprintf (stderr, "dxr3audiosink: Warning: %d bytes should be "
		"written, only %d bytes written\n",
		AC3P_IEC_FRAME_SIZE, bytes_written);
	  }

	  break;
      }

      event = ac3p_parse (sink->padder);
    }
  }

  gst_buffer_unref (buf);
}

#if 0
/**
 * dxr3audiosink_wait:
 * Make the sink wait the specified amount of time.
 */
static void
dxr3audiosink_wait (Dxr3AudioSink * sink, GstClockTime time)
{
  GstClockID id;
  GstClockTimeDiff jitter;
  GstClockReturn ret;
  GstClockTime current_time = gst_clock_get_time (sink->clock);

  id = gst_clock_new_single_shot_id (sink->clock, current_time + time);
  ret = gst_clock_id_wait (id, &jitter);
  gst_clock_id_free (id);
}


static int
dxr3audiosink_mvcommand (Dxr3AudioSink * sink, int command)
{
  em8300_register_t regs;

  regs.microcode_register = 1;
  regs.reg = 0;
  regs.val = command;

  return ioctl (sink->control_fd, EM8300_IOCTL_WRITEREG, &regs);
}
#endif

static GstElementStateReturn
dxr3audiosink_change_state (GstElement * element)
{
  g_return_val_if_fail (GST_IS_DXR3AUDIOSINK (element), GST_STATE_FAILURE);

  if (GST_STATE_PENDING (element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET (element, DXR3AUDIOSINK_OPEN)) {
      dxr3audiosink_close (DXR3AUDIOSINK (element));
    }
  } else {
    if (!GST_FLAG_IS_SET (element, DXR3AUDIOSINK_OPEN)) {
      if (!dxr3audiosink_open (DXR3AUDIOSINK (element))) {
	return GST_STATE_FAILURE;
      }
    }
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state) {
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);
  }

  return GST_STATE_SUCCESS;
}


/**
 * dxr3audiosink_flushed:
 *
 * Default do nothing implementation for the "flushed" signal.  The
 * "flushed" signal will be fired right after flushing the hardware
 * queues due to a received flush event 
 */
static void
dxr3audiosink_flushed (Dxr3AudioSink * sink)
{
  /* Do nothing. */
}
