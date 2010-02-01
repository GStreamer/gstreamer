/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *
 * gstsignalprocessor.h:
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


#ifndef __GST_SIGNAL_PROCESSOR_H__
#define __GST_SIGNAL_PROCESSOR_H__

#include <gst/gst.h>
#include <gst/audio/multichannel.h>

G_BEGIN_DECLS


typedef enum 
{
  GST_SIGNAL_PROCESSOR_CLASS_FLAG_CAN_PROCESS_IN_PLACE = 1<<0
} GstSignalProcessorClassFlags;

#define GST_SIGNAL_PROCESSOR_CLASS_CAN_PROCESS_IN_PLACE(klass)		\
  (GST_SIGNAL_PROCESSOR_CLASS (klass)->flags & 				\
   GST_SIGNAL_PROCESSOR_CLASS_FLAG_CAN_PROCESS_IN_PLACE)
#define GST_SIGNAL_PROCESSOR_CLASS_SET_CAN_PROCESS_IN_PLACE(klass)	\
  GST_SIGNAL_PROCESSOR_CLASS (klass)->flags |=				\
    GST_SIGNAL_PROCESSOR_CLASS_FLAG_CAN_PROCESS_IN_PLACE

typedef enum 
{
  GST_SIGNAL_PROCESSOR_STATE_NULL,
  GST_SIGNAL_PROCESSOR_STATE_INITIALIZED,
  GST_SIGNAL_PROCESSOR_STATE_RUNNING
} GstSignalProcessorState;


#define GST_TYPE_SIGNAL_PROCESSOR            (gst_signal_processor_get_type())
#define GST_SIGNAL_PROCESSOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SIGNAL_PROCESSOR,GstSignalProcessor))
#define GST_SIGNAL_PROCESSOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SIGNAL_PROCESSOR,GstSignalProcessorClass))
#define GST_SIGNAL_PROCESSOR_GET_CLASS(obj) \
        (G_TYPE_INSTANCE_GET_CLASS ((obj),GST_TYPE_SIGNAL_PROCESSOR,GstSignalProcessorClass))
#define GST_IS_SIGNAL_PROCESSOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SIGNAL_PROCESSOR))
#define GST_IS_SIGNAL_PROCESSOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SIGNAL_PROCESSOR))

#define GST_SIGNAL_PROCESSOR_IS_INITIALIZED(obj) \
  (GST_SIGNAL_PROCESSOR (obj)->state >= GST_SIGNAL_PROCESSOR_STATE_INITIALIZED)
#define GST_SIGNAL_PROCESSOR_IS_RUNNING(obj) \
  (GST_SIGNAL_PROCESSOR (obj)->state == GST_SIGNAL_PROCESSOR_STATE_RUNNING)

typedef struct _GstSignalProcessorGroup GstSignalProcessorGroup;
typedef struct _GstSignalProcessor GstSignalProcessor;
typedef struct _GstSignalProcessorClass GstSignalProcessorClass;


struct _GstSignalProcessorGroup {
  guint channels; /**< Number of channels in buffers */
  guint nframes; /**< Number of frames currently allocated per channel */
  gfloat *interleaved_buffer; /**< Interleaved buffer (c1c2c1c2...)*/
  gfloat *buffer; /**< De-interleaved buffer (c1c1...c2c2...) */
};

struct _GstSignalProcessor {
  GstElement     element;

  /* state */
  GstCaps *caps;
  GstSignalProcessorState state;
  GstFlowReturn flow_state;
  GstActivateMode mode;

  /* pending inputs before processing can take place */
  guint pending_in;
  /* pending outputs to be filled */
  guint pending_out;

  /* multi-channel signal pads */
  GstSignalProcessorGroup *group_in;
  GstSignalProcessorGroup *group_out;

  /* single channel signal pads */
  gfloat **audio_in;
  gfloat **audio_out;

  /* controls */
  gfloat *control_in;
  gfloat *control_out;
  
  /* sampling rate */
  gint sample_rate;

};

struct _GstSignalProcessorClass {
  GstElementClass parent_class;

  /*< public >*/
  guint num_group_in;
  guint num_group_out;
  guint num_audio_in;
  guint num_audio_out;
  guint num_control_in;
  guint num_control_out;

  guint flags;

  /* virtual methods for subclasses */

  gboolean      (*setup)        (GstSignalProcessor *self, GstCaps *caps);
  gboolean      (*start)        (GstSignalProcessor *self);
  void          (*stop)         (GstSignalProcessor *self);
  void          (*cleanup)      (GstSignalProcessor *self);
  void          (*process)      (GstSignalProcessor *self, guint num_frames);
  gboolean      (*event)        (GstSignalProcessor *self, GstEvent *event);
};


GType gst_signal_processor_get_type (void);
void gst_signal_processor_class_add_pad_template (GstSignalProcessorClass *klass,
    const gchar *name, GstPadDirection direction, guint index, guint channels);



G_END_DECLS


#endif /* __GST_SIGNAL_PROCESSOR_H__ */
