/* GStreamer
 * Copyright (C) 2003 Martin Soto <martinsoto@users.sourceforge.net>
 *
 * dxr3videosink.c: Video sink for em8300 based cards.
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

#include <linux/em8300.h>

#include <gst/gst.h>

#include "dxr3videosink.h"
#include "dxr3marshal.h"

#include "dxr3common.h"


/* ElementFactory information. */
static GstElementDetails dxr3videosink_details = {
  "dxr3/Hollywood+ mpeg decoder board video element",
  "Sink/Video",
  "Feeds MPEG2 video to Sigma Designs em8300 based boards",
  "Martin Soto <martinsoto@users.sourceforge.net>"
};


/* Dxr3VideoSink signals and args */
enum {
  SIGNAL_FLUSHED,
  LAST_SIGNAL
};

enum {
  ARG_0,
};

/* Possible states for the MPEG start code scanner. */
enum {
  SCAN_STATE_WAITING,	/* Waiting for a code. */
  SCAN_STATE_0,		/* 0 seen. */
  SCAN_STATE_00,	/* 00 seen. */
  SCAN_STATE_001	/* 001 seen. */
};

/* Possible states for the MPEG sequence parser. */
enum {
  PARSE_STATE_WAITING,	/* Waiting for the start of a sequence. */
  PARSE_STATE_START,	/* Start of sequence seen. */
  PARSE_STATE_PICTURE,	/* Picture start seen. */
};


/* Relevant mpeg start codes. */
#define START_CODE_PICTURE 0x00
#define START_CODE_SEQUENCE_HEADER 0xB3
#define START_CODE_SEQUENCE_END 0xB7

static GstStaticPadTemplate dxr3videosink_sink_factory =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (
    "video/mpeg, "
      "mpegversion = (int) { 1, 2 }, "
      "systemstream = (boolean) FALSE"
      /* width/height/framerate omitted, we don't
       * need a parsed stream */
  )
);


static void	dxr3videosink_class_init	(Dxr3VideoSinkClass *klass);
static void	dxr3videosink_base_init		(Dxr3VideoSinkClass *klass);
static void	dxr3videosink_init		(Dxr3VideoSink *dxr3videosink);

static void	dxr3videosink_set_property	(GObject *object,
                                                 guint prop_id, 
                                                 const GValue *value,
                                                 GParamSpec *pspec);
static void	dxr3videosink_get_property	(GObject *object,
                                                 guint prop_id, 
						 GValue *value,
                                                 GParamSpec *pspec);

static gboolean dxr3videosink_open		(Dxr3VideoSink *sink);
static void 	dxr3videosink_close		(Dxr3VideoSink *sink);
static void	dxr3videosink_set_clock		(GstElement *element,
                                                 GstClock *clock);

static void	dxr3videosink_reset_parser	(Dxr3VideoSink *sink);
static int	dxr3videosink_next_start_code	(Dxr3VideoSink *sink);
static void	dxr3videosink_discard_data	(Dxr3VideoSink *sink,
                                                 guint cut);
static void	dxr3videosink_write_data	(Dxr3VideoSink *sink,
                                                 guint cut);
static void	dxr3videosink_parse_data	(Dxr3VideoSink *sink);

static gboolean dxr3videosink_handle_event	(GstPad *pad, GstEvent *event);
static void	dxr3videosink_chain		(GstPad *pad,GstData *_data);

static GstElementStateReturn dxr3videosink_change_state (GstElement *element);

/* static void	dxr3videosink_wait		(Dxr3VideoSink *sink, */
/*                                                  GstClockTime time); */
static int	dxr3videosink_mvcommand		(Dxr3VideoSink *sink,
                                                 int command);

static void	dxr3videosink_flushed		(Dxr3VideoSink *sink);

static GstElementClass *parent_class = NULL;
static guint dxr3videosink_signals[LAST_SIGNAL] = { 0 };


extern GType
dxr3videosink_get_type (void) 
{
  static GType dxr3videosink_type = 0;

  if (!dxr3videosink_type) {
    static const GTypeInfo dxr3videosink_info = {
      sizeof (Dxr3VideoSinkClass),
      (GBaseInitFunc) dxr3videosink_base_init,
      NULL,
      (GClassInitFunc) dxr3videosink_class_init,
      NULL,
      NULL,
      sizeof (Dxr3VideoSink),
      0,
      (GInstanceInitFunc) dxr3videosink_init,
    };
    dxr3videosink_type = g_type_register_static (GST_TYPE_ELEMENT,
                                                 "Dxr3VideoSink",
                                                 &dxr3videosink_info, 0);
  }

  return dxr3videosink_type;
}


static void
dxr3videosink_base_init (Dxr3VideoSinkClass *klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
	gst_static_pad_template_get (&dxr3videosink_sink_factory));
  gst_element_class_set_details (element_class,
				 &dxr3videosink_details);
}

static void
dxr3videosink_class_init (Dxr3VideoSinkClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  dxr3videosink_signals[SIGNAL_FLUSHED] =
    g_signal_new ("flushed", G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (Dxr3VideoSinkClass, flushed),
                  NULL, NULL,
                  dxr3_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  klass->flushed = dxr3videosink_flushed;

  gobject_class->set_property = dxr3videosink_set_property;
  gobject_class->get_property = dxr3videosink_get_property;

  gstelement_class->change_state = dxr3videosink_change_state;
  gstelement_class->set_clock = dxr3videosink_set_clock;
}


static void 
dxr3videosink_init (Dxr3VideoSink *sink) 
{
  GstPad *pad;

  pad = gst_pad_new_from_template (
      gst_static_pad_template_get (&dxr3videosink_sink_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (sink), pad);
  gst_pad_set_chain_function (pad, dxr3videosink_chain);

  GST_FLAG_SET (GST_ELEMENT (sink), GST_ELEMENT_EVENT_AWARE);

  sink->card_number = 0;

  sink->video_filename = NULL;
  sink->video_fd = -1;
  sink->control_filename = NULL;
  sink->control_fd = -1;

  sink->clock = NULL;

  sink->last_ts = GST_CLOCK_TIME_NONE;

  sink->cur_buf = NULL;
  dxr3videosink_reset_parser (sink);
}


static void
dxr3videosink_set_property (GObject *object, guint prop_id,
                            const GValue *value, GParamSpec *pspec)
{
  Dxr3VideoSink *sink;

  /* it's not null if we got it, but it might not be ours */
  sink = DXR3VIDEOSINK (object);

  switch (prop_id) {
    default:
      break;
  }
}


static void   
dxr3videosink_get_property (GObject *object, guint prop_id,
                            GValue *value, GParamSpec *pspec)
{
  Dxr3VideoSink *sink;
 
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_DXR3VIDEOSINK (object));
 
  sink = DXR3VIDEOSINK (object);
  
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static gboolean
dxr3videosink_open (Dxr3VideoSink *sink)
{
  g_return_val_if_fail (!GST_FLAG_IS_SET (sink,
                                          DXR3VIDEOSINK_OPEN), FALSE);

  /* Compute the name of the video device file. */
  sink->video_filename = g_strdup_printf ("/dev/em8300_mv-%d",
                                          sink->card_number );

  sink->video_fd = open (sink->video_filename, O_WRONLY);
  if (sink->video_fd < 0) {
    gst_element_error (GST_ELEMENT (sink),
                       g_strconcat ("Error opening device file \"",
                                    sink->video_filename, "\": ",
                                    g_strerror (errno), NULL));
    return FALSE;
  }

  /* Open the control device. */
  sink->control_filename = g_strdup_printf ("/dev/em8300-%d",
                                            sink->card_number );

  sink->control_fd = open (sink->control_filename, O_WRONLY);
  if (sink->control_fd < 0) {
    gst_element_error (GST_ELEMENT (sink),
                       g_strconcat ("Error opening device file \"",
                                    sink->control_filename, "\": ",
                                    g_strerror (errno), NULL));
    return FALSE;
  }

  GST_FLAG_SET (sink, DXR3VIDEOSINK_OPEN);

  return TRUE;
}


static void
dxr3videosink_close (Dxr3VideoSink *sink)
{
  g_return_if_fail (GST_FLAG_IS_SET (sink, DXR3VIDEOSINK_OPEN));

  if (close (sink->video_fd) != 0)
  {
    gst_element_error (GST_ELEMENT (sink),
                       g_strconcat ("Error closing file \"",
                                    sink->video_filename, "\": ",
                                    g_strerror (errno), NULL));
    return;
  }

  if (close (sink->control_fd) != 0)
  {
    gst_element_error (GST_ELEMENT (sink),
                       g_strconcat ("Error closing file \"",
                                    sink->control_filename, "\": ",
                                    g_strerror (errno), NULL));
    return;
  }

  GST_FLAG_UNSET (sink, DXR3VIDEOSINK_OPEN);

  free (sink->video_filename);
  sink->video_filename = NULL;
}


static void
dxr3videosink_set_clock (GstElement *element, GstClock *clock)
{
  Dxr3VideoSink *src = DXR3VIDEOSINK (element);

  src->clock = clock;
}


static void
dxr3videosink_reset_parser (Dxr3VideoSink *sink)
{
  if (sink->cur_buf != NULL) {
    gst_buffer_unref (sink->cur_buf);
    sink->cur_buf = NULL;
  }
  sink->cur_ts = GST_CLOCK_TIME_NONE;

  sink->scan_state = SCAN_STATE_WAITING;
  sink->scan_pos = 0;

  sink->parse_state = PARSE_STATE_WAITING;
}


static int
dxr3videosink_next_start_code (Dxr3VideoSink *sink)
{
  guchar c;

  g_return_val_if_fail (sink->cur_buf != NULL, -1);

  while (sink->scan_pos < GST_BUFFER_SIZE (sink->cur_buf)) {
    c = (GST_BUFFER_DATA (sink->cur_buf))[sink->scan_pos];

    switch (sink->scan_state) {
    case SCAN_STATE_WAITING:
      if (c == 0x00) {
        sink->scan_state = SCAN_STATE_0;
      }
      break;
    case SCAN_STATE_0:
      if (c == 0x00) {
        sink->scan_state = SCAN_STATE_00;
      }
      else {
        sink->scan_state = SCAN_STATE_WAITING;
      }
      break;
    case SCAN_STATE_00:
      if (c == 0x01) {
        sink->scan_state = SCAN_STATE_001;
      }
      else if (c != 0x00) { 
        sink->scan_state = SCAN_STATE_WAITING;
      }
      break;
    case SCAN_STATE_001:
      sink->scan_pos++;
      sink->scan_state = SCAN_STATE_WAITING;
      return c;
    }

    sink->scan_pos++;
  }

  return -1;
}


static void
dxr3videosink_discard_data (Dxr3VideoSink *sink, guint cut)
{
  GstBuffer *sub;
  guint size;

  g_return_if_fail (sink->cur_buf != NULL);
  g_assert (cut <= sink->scan_pos);

  size = sink->scan_pos - cut;

  g_return_if_fail (size <= GST_BUFFER_SIZE (sink->cur_buf));

  if (GST_BUFFER_SIZE (sink->cur_buf) == size) {
    gst_buffer_unref (sink->cur_buf);
    sink->cur_buf = NULL;
  }
  else {
    sub = gst_buffer_create_sub (sink->cur_buf, size,
                                 GST_BUFFER_SIZE (sink->cur_buf)
                                 - size);
    gst_buffer_unref (sink->cur_buf);
    sink->cur_buf = sub;
  }

  sink->scan_state = SCAN_STATE_WAITING;
  sink->scan_pos = cut;

  sink->cur_ts = GST_CLOCK_TIME_NONE;
}


static void
dxr3videosink_write_data (Dxr3VideoSink *sink, guint cut)
{
  guint size, written;
  guint8 *data;

  g_return_if_fail (sink->cur_buf != NULL);

  if (GST_FLAG_IS_SET (sink, DXR3VIDEOSINK_OPEN)) {
    if (sink->cur_ts != GST_CLOCK_TIME_NONE) {
      guint pts;

/*       fprintf (stderr, "------ Video Time %.04f\n", */
/*                (double) sink->cur_ts / GST_SECOND); */

      pts = (guint) GSTTIME_TO_MPEGTIME (sink->cur_ts);
      ioctl (sink->video_fd, EM8300_IOCTL_VIDEO_SETPTS, &pts);
      sink->cur_ts = GST_CLOCK_TIME_NONE;
    }

    data = GST_BUFFER_DATA (sink->cur_buf);
    size = sink->scan_pos - cut;

    g_assert (size <= GST_BUFFER_SIZE (sink->cur_buf));

    /* We should always write data that corresponds to whole MPEG
       video sintactical elements.  They should always start with an
       MPEG start code. */
    g_assert (size >= 4 && data[0] == 0 && data[1] == 0 && data[2] == 1);

    while (size > 0) {
      written = write (sink->video_fd, data, size);
      if (written < 0) {
        gst_element_error (GST_ELEMENT (sink), "Writing to %s: %s",
                           sink->video_filename, strerror (errno));
        break;
      }
      size = size - written;
      data = data + written;
    };
  }

  dxr3videosink_discard_data (sink, cut);
}


static void
dxr3videosink_parse_data (Dxr3VideoSink *sink)
{
  int code;

  /* Timestamp handling assumes that timestamps are associated to
     sequence starts.  This seems to be the case, at least for
     DVDs. */

  code = dxr3videosink_next_start_code (sink);
  while (code >= 0) {
    switch (sink->parse_state) {

    case PARSE_STATE_WAITING:
      if (code == START_CODE_SEQUENCE_HEADER) {
        dxr3videosink_discard_data (sink, 4);
        sink->parse_state = PARSE_STATE_START;
        sink->cur_ts = sink->last_ts;
      }
      break;

    case PARSE_STATE_START:
      switch (code) {
      case START_CODE_SEQUENCE_HEADER:
        dxr3videosink_discard_data (sink, 4);
        sink->cur_ts = sink->last_ts;
        break;
      case START_CODE_SEQUENCE_END:
        dxr3videosink_discard_data (sink, 0);
        sink->parse_state = PARSE_STATE_WAITING;
        break;
      case START_CODE_PICTURE:
        sink->parse_state = PARSE_STATE_PICTURE;
        break;
      }
      break;

    case PARSE_STATE_PICTURE:
      switch (code) {
      case START_CODE_SEQUENCE_HEADER:
        dxr3videosink_write_data (sink, 4);
        sink->parse_state = PARSE_STATE_START;
        sink->cur_ts = sink->last_ts;
        break;
      case START_CODE_SEQUENCE_END:
        dxr3videosink_write_data (sink, 0);
        sink->parse_state = PARSE_STATE_WAITING;
        break;
      case START_CODE_PICTURE:
        dxr3videosink_write_data (sink, 4);
        break;
      }
      break;

    }

    code = dxr3videosink_next_start_code (sink);
  }

  if (sink->parse_state == PARSE_STATE_WAITING) {
    dxr3videosink_discard_data (sink, 0);
  }
}


static gboolean
dxr3videosink_handle_event (GstPad *pad, GstEvent *event)
{
  GstEventType type;
  Dxr3VideoSink *sink;

  sink = DXR3VIDEOSINK (gst_pad_get_parent (pad));

  type = event ? GST_EVENT_TYPE (event) : GST_EVENT_UNKNOWN;

  switch (type) {
  case GST_EVENT_EMPTY:
    //fprintf (stderr, "++++++ Video empty event\n");
    {
      /* FIXME: Handle this with a discontinuity or something. */
      /* Write an MPEG2 sequence end code, to ensure that the card
         actually displays the last picture.  Apparently some DVDs are
         encoded without proper sequence end codes. */
      static guint8 sec[4] = { 0x00, 0x00, 0x01, 0xb7 };

      if (sink->cur_buf != NULL) {
        dxr3videosink_write_data (sink, 0);
      }

      write (sink->video_fd, &sec, 4);
    }
    break;

  case GST_EVENT_DISCONTINUOUS:
    //fprintf (stderr, "++++++ Video discont event\n");
    {
      gint64 time;
      gboolean has_time;
      unsigned cur_scr, mpeg_scr, diff;

      has_time = gst_event_discont_get_value (event,
                                              GST_FORMAT_TIME,
                                              &time);
      if (has_time) {
/*         fprintf (stderr, "^^^^^^ Discontinuous event has time %.4f\n", */
/*                  (double) time / GST_SECOND); */

        /* If the SCR in the card is way off, fix it. */
        ioctl (sink->control_fd, EM8300_IOCTL_SCR_GET, &cur_scr);
        mpeg_scr = MPEGTIME_TO_DXRTIME (GSTTIME_TO_MPEGTIME (time));

        diff = cur_scr > mpeg_scr ? cur_scr - mpeg_scr : mpeg_scr - cur_scr;
        if (diff > 1800) {
          unsigned zero = 0;
/*           fprintf (stderr, "====== Adjusting SCR from video\n"); */

          ioctl (sink->control_fd, EM8300_IOCTL_SCR_SET, &zero);
          ioctl (sink->control_fd, EM8300_IOCTL_SCR_SET, &mpeg_scr);
        }
      }
      else {
/*         fprintf (stderr, "^^^^^^ Discontinuous event has no time\n"); */
      }
    }
    break;

  case GST_EVENT_FLUSH:
    dxr3videosink_reset_parser (sink);
    break;

  default:
    gst_pad_event_default (pad, event);
    break;
  }

  return TRUE;
}


static void 
dxr3videosink_chain (GstPad *pad, GstData *_data) 
{
  GstBuffer *buf = GST_BUFFER (_data);
  Dxr3VideoSink *sink;
  GstBuffer *merged;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  sink = DXR3VIDEOSINK (gst_pad_get_parent (pad));

  if (GST_IS_EVENT (buf)) {
    dxr3videosink_handle_event (pad, GST_EVENT (buf));
    return;
  }

/*   fprintf (stderr, "^^^^^^ Video block\n"); */

  if (sink->cur_buf == NULL) {
    sink->cur_buf = buf;
  }
  else {
    merged = gst_buffer_merge (sink->cur_buf, buf);
    gst_buffer_unref (sink->cur_buf);
    gst_buffer_unref (buf);
    sink->cur_buf = merged;
  }

  sink->last_ts = GST_BUFFER_TIMESTAMP (buf);

  dxr3videosink_parse_data (sink);
}


static GstElementStateReturn
dxr3videosink_change_state (GstElement *element)
{
  g_return_val_if_fail (GST_IS_DXR3VIDEOSINK (element), GST_STATE_FAILURE);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      if (!GST_FLAG_IS_SET (element, DXR3VIDEOSINK_OPEN)) {
        if (!dxr3videosink_open (DXR3VIDEOSINK (element))) {
          return GST_STATE_FAILURE;
        }
      }
      break;
    case GST_STATE_READY_TO_PAUSED:
      dxr3videosink_mvcommand (DXR3VIDEOSINK (element), MVCOMMAND_PAUSE);
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      dxr3videosink_mvcommand (DXR3VIDEOSINK (element), MVCOMMAND_START);
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      dxr3videosink_mvcommand (DXR3VIDEOSINK (element), MVCOMMAND_PAUSE);
      break;
    case GST_STATE_PAUSED_TO_READY:
      dxr3videosink_mvcommand (DXR3VIDEOSINK (element), MVCOMMAND_STOP);
      break;
    case GST_STATE_READY_TO_NULL:
      if (GST_FLAG_IS_SET (element, DXR3VIDEOSINK_OPEN)) {
        dxr3videosink_close (DXR3VIDEOSINK (element));
      }
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state) {
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);
  }

  return GST_STATE_SUCCESS;
}

#if 0
/**
 * dxr3videosink_wait:
 * Make the sink wait the specified amount of time.
 */
static void
dxr3videosink_wait (Dxr3VideoSink *sink, GstClockTime time)
{
  GstClockID id;
  GstClockTimeDiff jitter;
  GstClockReturn ret;
  GstClockTime current_time = gst_clock_get_time (sink->clock);

  id = gst_clock_new_single_shot_id (sink->clock, current_time + time);
  ret = gst_clock_id_wait (id, &jitter);
  gst_clock_id_free (id);
}
#endif

/**
 * dxr3videosink_mvcommand
 *
 * Send an MVCOMMAND to the card.
 */
static int
dxr3videosink_mvcommand (Dxr3VideoSink *sink, int command)
{
  em8300_register_t regs;
  
  regs.microcode_register = 1;
  regs.reg = 0;
  regs.val = command;
  
  return ioctl (sink->control_fd, EM8300_IOCTL_WRITEREG, &regs);
}


/**
 * dxr3videosink_flushed:
 *
 * Default do nothing implementation for the "flushed" signal.  The
 * "flushed" signal will be fired right after flushing the hardware
 * queues due to a received flush event 
 */
static void
dxr3videosink_flushed (Dxr3VideoSink *sink)
{
  /* Do nothing. */
}
