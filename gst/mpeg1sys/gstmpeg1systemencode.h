/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#ifndef __SYSTEM_ENCODE_H__
#define __SYSTEM_ENCODE_H__


#include <gst/gst.h>
#include <gst/getbits/getbits.h>

#include "buffer.h"
#include "main.h"

#ifdef __cplusplus
extern "C"
{
#endif				/* __cplusplus */


#define GST_TYPE_SYSTEM_ENCODE \
  (gst_mpeg1_system_encode_get_type())
#define GST_SYSTEM_ENCODE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SYSTEM_ENCODE,GstMPEG1SystemEncode))
#define GST_SYSTEM_ENCODE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SYSTEM_ENCODE,GstMPEG1SystemEncode))
#define GST_IS_SYSTEM_ENCODE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SYSTEM_ENCODE))
#define GST_IS_SYSTEM_ENCODE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SYSTEM_ENCODE))

  typedef struct _GstMPEG1SystemEncode GstMPEG1SystemEncode;
  typedef struct _GstMPEG1SystemEncodeClass GstMPEG1SystemEncodeClass;

  struct _GstMPEG1SystemEncode
  {
    GstElement element;

    GstPad *srcpad;

    gboolean have_setup;

    GMutex *lock;

    guint num_audio_pads;
    guint num_video_pads;

    Mpeg1MuxBuffer *audio_buffer;
    Mpeg1MuxBuffer *video_buffer;

    Pack_struc *pack;
    Sys_header_struc *sys_header;
    Sector_struc *sector;

    guint data_rate, video_rate, audio_rate;
    gdouble delay, audio_delay, video_delay;
    gdouble clock_cycles;
    gulong sectors_delay, video_delay_ms, audio_delay_ms;
    gulong startup_delay;
    gulong audio_buffer_size;
    gulong video_buffer_size;
    gulong mux_rate, dmux_rate;
    guint64 SCR;
    gint which_streams;

    gint current_pack;
    gulong min_packet_data;
    gulong max_packet_data;
    gint packets_per_pack;
    gulong packet_size;
    gulong bytes_output;

    GList *mta;

    /* stream input pads */
    GstPad *private_1_pad[8];	/* up to 8 ac3 audio tracks <grumble> */
    GstPad *private_2_pad;
    GstPad *video_pad[16];
    GstPad *audio_pad[32];
  };

  struct _GstMPEG1SystemEncodeClass
  {
    GstElementClass parent_class;
  };

  GType gst_mpeg1_system_encode_get_type (void);

/* multplex.c */

#ifdef __cplusplus
}
#endif				/* __cplusplus */


#endif				/* __SYSTEM_ENCODE_H__ */
