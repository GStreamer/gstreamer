/* GStreamer
 * Copyright (C) 2003 Martin Soto <martinsoto@users.sourceforge.net>
 *
 * dxr3spusink.h: Subpicture sink for em8300 based cards.
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

#include "dxr3spusink.h"
#include "dxr3marshal.h"

#include "dxr3common.h"


/* ElementFactory information. */
static GstElementDetails dxr3spusink_details = {
  "dxr3/Hollywood+ mpeg decoder board subpicture element",
  "Sink/Video",
  "Feeds subpicture information to Sigma Designs em8300 based boards",
  "Martin Soto <martinsoto@users.sourceforge.net>"
};


/* Dxr3SpuSink signals and args */
enum {
  SET_CLUT_SIGNAL,
  HIGHLIGHT_ON_SIGNAL,
  HIGHLIGHT_OFF_SIGNAL,
  SIGNAL_FLUSHED,
  LAST_SIGNAL
};

enum {
  ARG_0,
};


static GstStaticPadTemplate dxr3spusink_sink_factory =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS_ANY
);


static void	dxr3spusink_class_init   	(Dxr3SpuSinkClass *klass);
static void	dxr3spusink_base_init   	(Dxr3SpuSinkClass *klass);
static void	dxr3spusink_init		(Dxr3SpuSink *dxr3spusink);

static void	dxr3spusink_set_property	(GObject *object,
                                                 guint prop_id, 
                                                 const GValue *value,
                                                 GParamSpec *pspec);
static void	dxr3spusink_get_property	(GObject *object,
                                                 guint prop_id, 
						 GValue *value,
                                                 GParamSpec *pspec);

static gboolean dxr3spusink_open	 	(Dxr3SpuSink *sink);
static void 	dxr3spusink_close 		(Dxr3SpuSink *sink);
static void	dxr3spusink_set_clock		(GstElement *element,
                                                 GstClock *clock);

static gboolean dxr3spusink_handle_event  	(GstPad *pad, GstEvent *event);
static void	dxr3spusink_chain		(GstPad *pad,GstData *_data);

static GstElementStateReturn dxr3spusink_change_state (GstElement *element);

/* static void	dxr3spusink_wait		(Dxr3SpuSink *sink, */
/*                                                  GstClockTime time); */

static void     dxr3spusink_set_clut		(Dxr3SpuSink *sink,
                                                 const guint32 *clut);
static void     dxr3spusink_highlight_on	(Dxr3SpuSink *sink,
                                                 unsigned palette,
                                                 unsigned sx, unsigned sy,
                                                 unsigned ex, unsigned ey,
                                                 unsigned pts);
static void     dxr3spusink_highlight_off	(Dxr3SpuSink *sink);

static void	dxr3spusink_flushed		(Dxr3SpuSink *sink);


static GstElementClass *parent_class = NULL;
static guint dxr3spusink_signals[LAST_SIGNAL] = { 0 };


GType
dxr3spusink_get_type (void) 
{
  static GType dxr3spusink_type = 0;

  if (!dxr3spusink_type) {
    static const GTypeInfo dxr3spusink_info = {
      sizeof (Dxr3SpuSinkClass),
      (GBaseInitFunc)dxr3spusink_base_init,
      NULL,
      (GClassInitFunc)dxr3spusink_class_init,
      NULL,
      NULL,
      sizeof (Dxr3SpuSink),
      0,
      (GInstanceInitFunc)dxr3spusink_init,
    };
    dxr3spusink_type = g_type_register_static (GST_TYPE_ELEMENT,
                                               "Dxr3SpuSink",
                                               &dxr3spusink_info, 0);
  }
  return dxr3spusink_type;
}


static void
dxr3spusink_base_init (Dxr3SpuSinkClass *klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
	gst_static_pad_template_get (&dxr3spusink_sink_factory));
  gst_element_class_set_details (element_class,
				 &dxr3spusink_details);
}

static void
dxr3spusink_class_init (Dxr3SpuSinkClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  dxr3spusink_signals[SET_CLUT_SIGNAL] =
    g_signal_new ("set_clut",
        G_TYPE_FROM_CLASS (klass),
        G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
        G_STRUCT_OFFSET (Dxr3SpuSinkClass, set_clut),
        NULL, NULL,
        dxr3_marshal_VOID__POINTER,
        G_TYPE_NONE, 1,
        G_TYPE_POINTER);

  dxr3spusink_signals[HIGHLIGHT_ON_SIGNAL] =
    g_signal_new ("highlight_on",
        G_TYPE_FROM_CLASS (klass),
        G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
        G_STRUCT_OFFSET (Dxr3SpuSinkClass, highlight_on),
        NULL, NULL,
        dxr3_marshal_VOID__UINT_UINT_UINT_UINT_UINT_UINT,
        G_TYPE_NONE, 6,
        G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT,
        G_TYPE_UINT, G_TYPE_UINT);

  dxr3spusink_signals[HIGHLIGHT_OFF_SIGNAL] =
    g_signal_new ("highlight_off",
        G_TYPE_FROM_CLASS (klass),
        G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
        G_STRUCT_OFFSET (Dxr3SpuSinkClass, highlight_off),
        NULL, NULL,
        dxr3_marshal_VOID__VOID,
        G_TYPE_NONE, 0);

  dxr3spusink_signals[SIGNAL_FLUSHED] =
    g_signal_new ("flushed", G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (Dxr3SpuSinkClass, flushed),
                  NULL, NULL,
                  dxr3_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  klass->set_clut = dxr3spusink_set_clut;
  klass->highlight_on = dxr3spusink_highlight_on;
  klass->highlight_off = dxr3spusink_highlight_off;
  klass->flushed = dxr3spusink_flushed;

  gobject_class->set_property = dxr3spusink_set_property;
  gobject_class->get_property = dxr3spusink_get_property;

  gstelement_class->change_state = dxr3spusink_change_state;
  gstelement_class->set_clock = dxr3spusink_set_clock;
}


static void 
dxr3spusink_init (Dxr3SpuSink *sink) 
{
  GstPad *pad;

  pad = gst_pad_new_from_template (
      gst_static_pad_template_get (&dxr3spusink_sink_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (sink), pad);
  gst_pad_set_chain_function (pad, dxr3spusink_chain);

  GST_FLAG_SET (GST_ELEMENT (sink), GST_ELEMENT_EVENT_AWARE);

  sink->card_number = 0;

  sink->spu_filename = NULL;
  sink->spu_fd = -1;
  sink->control_filename = NULL;
  sink->control_fd = -1;

  sink->clock = NULL;
}


static void
dxr3spusink_set_property (GObject *object, guint prop_id,
                                const GValue *value, GParamSpec *pspec)
{
  Dxr3SpuSink *sink;

  /* it's not null if we got it, but it might not be ours */
  sink = DXR3SPUSINK (object);

  switch (prop_id) {
    default:
      break;
  }
}


static void   
dxr3spusink_get_property (GObject *object, guint prop_id,
                          GValue *value, GParamSpec *pspec)
{
  Dxr3SpuSink *sink;
 
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_DXR3SPUSINK (object));
 
  sink = DXR3SPUSINK (object);
  
  switch (prop_id) {
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}


static gboolean
dxr3spusink_open (Dxr3SpuSink *sink)
{
  g_return_val_if_fail (!GST_FLAG_IS_SET (sink,
                                          DXR3SPUSINK_OPEN), FALSE);

  /* Compute the name of the spu device file. */
  sink->spu_filename = g_strdup_printf ("/dev/em8300_sp-%d",
                                        sink->card_number );

  sink->spu_fd = open (sink->spu_filename, O_WRONLY);
  if (sink->spu_fd < 0) {
    gst_element_error (sink, RESOURCE, OPEN_WRITE,
                       (_("Could not open spu device \"%s\" for writing"), sink->spu_filename),                         GST_ERROR_SYSTEM);
    return FALSE;
  }

  /* Open the control device. */
  sink->control_filename = g_strdup_printf ("/dev/em8300-%d",
                                            sink->card_number );

  sink->control_fd = open (sink->control_filename, O_WRONLY);
  if (sink->control_fd < 0) {
    gst_element_error (sink, RESOURCE, OPEN_WRITE,
                       (_("Could not open control device \"%s\" for writing"), sink->control_filename),                         GST_ERROR_SYSTEM);
    return FALSE;
  }

  GST_FLAG_SET (sink, DXR3SPUSINK_OPEN);

  return TRUE;
}


static void
dxr3spusink_close (Dxr3SpuSink *sink)
{
  g_return_if_fail (GST_FLAG_IS_SET (sink, DXR3SPUSINK_OPEN));

  if (close (sink->spu_fd) != 0) {
    gst_element_error (sink, RESOURCE, CLOSE,
                       (_("Could not close spu device \"%s\""), sink->spu_filename),
                        GST_ERROR_SYSTEM);
    return;
  }

  if (close (sink->control_fd) != 0)
  {
    gst_element_error (sink, RESOURCE, CLOSE,
                       (_("Could not close control device \"%s\""), sink->audio_filename),
                        GST_ERROR_SYSTEM);
    return;
  }

  GST_FLAG_UNSET (sink, DXR3SPUSINK_OPEN);

  free (sink->spu_filename);
  sink->spu_filename = NULL;
}


static void
dxr3spusink_set_clock (GstElement *element, GstClock *clock)
{
  Dxr3SpuSink *src = DXR3SPUSINK (element);
  
  src->clock = clock;
}


static gboolean
dxr3spusink_handle_event (GstPad *pad, GstEvent *event)
{
  GstEventType type;
  Dxr3SpuSink *sink;

  sink = DXR3SPUSINK (gst_pad_get_parent (pad));

  type = event ? GST_EVENT_TYPE (event) : GST_EVENT_UNKNOWN;

  switch (type) {
  case GST_EVENT_FLUSH:
    if (sink->control_fd >= 0) {
      int subdevice;
      subdevice =  EM8300_SUBDEVICE_SUBPICTURE;
      ioctl (sink->control_fd, EM8300_IOCTL_FLUSH, &subdevice);

      /* FIXME: There should be a nicer way to do this, but I tried
         everything and nothing else seems to really reset the video
         fifo. */ 
/*       dxr3spusink_close (sink); */
/*       dxr3spusink_open (sink); */

      /* Report the flush operation. */
      g_signal_emit (G_OBJECT (sink),
                     dxr3spusink_signals[SIGNAL_FLUSHED], 0);
    }
    break;
  default:
    gst_pad_event_default (pad, event);
    break;
  }

  return TRUE;
}


static void 
dxr3spusink_chain (GstPad *pad, GstData *_data) 
{
  GstBuffer *buf = GST_BUFFER (_data);
  Dxr3SpuSink *sink;
  gint bytes_written = 0;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  sink = DXR3SPUSINK (gst_pad_get_parent (pad));

  if (GST_IS_EVENT (buf)) {
    dxr3spusink_handle_event (pad, GST_EVENT (buf));
    return;
  }

  if (GST_FLAG_IS_SET (sink, DXR3SPUSINK_OPEN)) {
    /* If we have PTS information for the SPU unit, register it now.
       The card needs the PTS to be written *before* the actual data. */
    if (GST_BUFFER_TIMESTAMP (buf) != GST_CLOCK_TIME_NONE) {
      guint pts = (guint) GSTTIME_TO_MPEGTIME (GST_BUFFER_TIMESTAMP (buf));
      ioctl (sink->spu_fd, EM8300_IOCTL_SPU_SETPTS, &pts);
    }

    bytes_written = write (sink->spu_fd, GST_BUFFER_DATA (buf),
                           GST_BUFFER_SIZE (buf));
    if (bytes_written < GST_BUFFER_SIZE (buf)) {
      fprintf (stderr, "dxr3spusink: Warning: %d bytes should be written,"
               " only %d bytes written\n",
               GST_BUFFER_SIZE (buf), bytes_written);
    }
  }

  gst_buffer_unref (buf);
}


static GstElementStateReturn
dxr3spusink_change_state (GstElement *element)
{
  g_return_val_if_fail (GST_IS_DXR3SPUSINK (element), GST_STATE_FAILURE);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      if (!GST_FLAG_IS_SET (element, DXR3SPUSINK_OPEN)) {
        if (!dxr3spusink_open (DXR3SPUSINK (element))) {
          return GST_STATE_FAILURE;
        }
      }
      break;
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      break;
    case GST_STATE_READY_TO_NULL:
      if (GST_FLAG_IS_SET (element, DXR3SPUSINK_OPEN)) {
        dxr3spusink_close (DXR3SPUSINK (element));
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
 * dxr3spusink_wait:
 * Make the sink wait the specified amount of time.
 */
static void
dxr3spusink_wait (Dxr3SpuSink *sink, GstClockTime time)
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
 * dxr3spusink_set_clut:
 *
 * Set a new SPU color lookup table (clut) in the dxr3 card.
 */
static void
dxr3spusink_set_clut (Dxr3SpuSink *sink, const guint32 *clut)
{
  guint32 clut_fixed[16];
  int i;

  /* Fix the byte order of the table. */
  for (i=0; i<16; i++) {
    clut_fixed[i] = GUINT32_TO_LE (clut[i]);
  }

  if (ioctl (sink->spu_fd, EM8300_IOCTL_SPU_SETPALETTE, clut_fixed))
    fprintf (stderr, "dxr3spusink: failed to set CLUT (%s)\n",
             strerror (errno));
}


static void
dxr3spusink_highlight_on (Dxr3SpuSink *sink, unsigned palette,
                          unsigned sx, unsigned sy,
                          unsigned ex, unsigned ey,
                          unsigned pts)
{
  em8300_button_t btn;

  btn.color = palette >> 16;
  btn.contrast = palette;
  btn.left = sx;
  btn.top = sy;
  btn.right = ex;
  btn.bottom = ey;

  if (ioctl (sink->spu_fd, EM8300_IOCTL_SPU_BUTTON, &btn)) {
    fprintf (stderr, "dxr3spusink: failed to set spu button (%s)\n",
             strerror (errno));
  }
}


static void
dxr3spusink_highlight_off (Dxr3SpuSink *sink)
{
  if (ioctl (sink->spu_fd, EM8300_IOCTL_SPU_BUTTON, NULL)) {
    fprintf (stderr, "dxr3spusink: failed to set spu button (%s)\n",
             strerror (errno));
  }
}


/**
 * dxr3spusink_flushed:
 *
 * Default do nothing implementation for the "flushed" signal.  The
 * "flushed" signal will be fired right after flushing the hardware
 * queues due to a received flush event 
 */
static void
dxr3spusink_flushed (Dxr3SpuSink *sink)
{
  /* Do nothing. */
}
