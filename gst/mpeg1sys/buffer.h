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


#ifndef __BUFFER_H__
#define __BUFFER_H__

#include <gst/gst.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define MPEG1MUX_BUFFER_QUEUED(mb) (g_list_length((mb)->timecode_list))
#define MPEG1MUX_BUFFER_SPACE(mb) ((mb)->length)
#define MPEG1MUX_BUFFER_DATA(mb) ((mb)->buffer)
#define MPEG1MUX_BUFFER_TYPE(mb) ((mb)->buffer)
#define MPEG1MUX_BUFFER_FIRST_TIMECODE(mb) (g_list_first((mb)->timecode_list)->data)

#define BUFFER_TYPE_VIDEO 1
#define BUFFER_TYPE_AUDIO 2

#define FRAME_TYPE_IFRAME 1
#define FRAME_TYPE_BFRAME 2
#define FRAME_TYPE_PFRAME 3
#define FRAME_TYPE_AUDIO  4

typedef struct _Mpeg1MuxBuffer Mpeg1MuxBuffer;
typedef struct _Mpeg1MuxTimecode Mpeg1MuxTimecode;

typedef struct video_struc      /* Informationen ueber Video Stream     */
{
  unsigned int stream_length  ;
  unsigned int num_sequence   ;
  unsigned int num_seq_end    ;
  unsigned int num_pictures   ;
  unsigned int num_groups     ;
  unsigned int num_frames[4]  ;
  unsigned int avg_frames[4]  ;

  unsigned int horizontal_size;
  unsigned int vertical_size  ;
  unsigned int aspect_ratio   ;
  unsigned int picture_rate   ;
  unsigned int bit_rate       ;
  unsigned int comp_bit_rate  ;
  unsigned int vbv_buffer_size;
  unsigned int CSPF           ;

  guint64 PTS;
  guint64 DTS;

  guint64 current_PTS;
  guint64 current_DTS;
  guchar current_type;

  double secs_per_frame;
  gulong group_order, decoding_order;
} Video_struc;

typedef struct audio_struc      /* Informationen ueber Audio Stream     */
{
  unsigned int stream_length  ;
  unsigned int num_syncword   ;
  unsigned int num_frames [2] ;
  unsigned int framesize      ;
  unsigned int layer          ;
  unsigned int protection     ;
  unsigned int bit_rate       ;
  unsigned int frequency      ;
  unsigned int mode           ;
  unsigned int mode_extension ;
  unsigned int copyright      ;
  unsigned int original_copy  ;
  unsigned int emphasis       ;

  guint64 PTS;

  guint64 current_PTS;

  double samples_per_second;
  gulong decoding_order;
} Audio_struc;

struct _Mpeg1MuxTimecode {
  gulong length;
  gulong original_length;
  guchar frame_type;
  guint64 PTS;
  guint64 DTS;
};

struct _Mpeg1MuxBuffer {
  unsigned char *buffer;
  gulong length;
  gulong base;
  gulong scan_pos;
  gulong last_pos;
  gulong current_start;
  guchar buffer_type;
  guchar stream_id;
  gboolean new_frame;
  guint64 next_frame_time;

  union {
    Video_struc video;
    Audio_struc audio;
  } info;

  GList *timecode_list;
  GList *queued_list;
};

Mpeg1MuxBuffer *mpeg1mux_buffer_new(guchar type, guchar id);

void mpeg1mux_buffer_queue(Mpeg1MuxBuffer *mb, GstBuffer *buf);
void mpeg1mux_buffer_shrink(Mpeg1MuxBuffer *mb, gulong size);
gulong mpeg1mux_buffer_update_queued(Mpeg1MuxBuffer *mb, guint64 scr);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __BUFFER_H__ */
