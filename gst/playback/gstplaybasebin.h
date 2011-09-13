/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *               <2007> Wim Taymans <wim.taymans@gmail.com>
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

#ifndef __GST_PLAYBASEBIN_H__
#define __GST_PLAYBASEBIN_H__

#include <gst/gst.h>
#include "gststreaminfo.h"

G_BEGIN_DECLS

#define GST_TYPE_PLAY_BASE_BIN            (gst_play_base_bin_get_type())
#define GST_PLAY_BASE_BIN(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_PLAY_BASE_BIN,GstPlayBaseBin))
#define GST_PLAY_BASE_BIN_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_PLAY_BASE_BIN,GstPlayBaseBinClass))
#define GST_IS_PLAY_BASE_BIN(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_PLAY_BASE_BIN))
#define GST_IS_PLAY_BASE_BIN_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_PLAY_BASE_BIN))
#define GST_PLAY_BASE_BIN_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_PLAY_BASE_BIN, \
                              GstPlayBaseBinClass))

typedef struct _GstPlayBaseBin GstPlayBaseBin;
typedef struct _GstPlayBaseBinClass GstPlayBaseBinClass;

/* a GstPlayBaseGroup is a group of pads and streaminfo that together 
 * make up a playable stream. A new group is created from the current 
 * set of pads that are alive when the preroll elements are filled or 
 * when the no-more-pads signal is fired.
 *
 * We have to queue the groups as they can be created while the preroll
 * queues are still playing the old group. We monitor the EOS signals
 * on the preroll queues and when all the streams in the current group
 * have EOSed, we switch to the next queued group.
 */
typedef struct
{
  GstPlayBaseBin *bin;  /* ref to the owner */

  gint           nstreams;
  GList         *streaminfo;
  GValueArray   *streaminfo_value_array;

  /* contained decoded elementary streams */
  struct {
    gint         npads;
    GstBin      *bin;
    GstElement  *preroll;
    GstElement  *selector;
    gboolean     done;
#define NUM_TYPES 4
  } type[NUM_TYPES]; /* AUDIO, VIDEO, TEXT, SUBPIC */
} GstPlayBaseGroup;

struct _GstPlayBaseBin {
  GstPipeline    pipeline;
        
  /* properties */
  guint64        queue_size;
  guint64        queue_threshold;
  guint64        queue_min_threshold;
  /* connection speed in bits/sec (0 = unknown) */
  guint          connection_speed;
  

  /* currently loaded media */
  gint           current[NUM_TYPES];
  gchar         *uri, *suburi;
  gboolean       is_stream;
  GstElement    *source;
  GSList        *decoders;
  GstElement    *subtitle;              /* additional filesrc ! subparse bin */
  gboolean       subtitle_done;
  gboolean       need_rebuild;
  gboolean       raw_decoding_mode;     /* Use smaller queues when source outputs raw data */

  GSList        *subtitle_elements;     /* subtitle elements that have 'subtitle-encoding' property */
  gchar         *subencoding;           /* encoding to propagate to the above subtitle elements     */
  GMutex        *sub_lock;              /* protecting subtitle_elements and subencoding members     */

  /* group management - using own lock */
  GMutex        *group_lock;            /* lock and mutex to signal availability of new group */
  GCond         *group_cond;
  GstPlayBaseGroup *building_group;     /* the group that we are constructing */
  GList         *queued_groups;         /* the constructed groups, head is the active one */

  /* for dynamic sources */
  guint          src_np_sig_id;		/* new-pad signal id */
  guint          src_nmp_sig_id;        /* no-more-pads signal id */
  gint           pending;
};

struct _GstPlayBaseBinClass {
  GstPipelineClass parent_class;

  /* virtual functions */
  gboolean (*setup_output_pads) (GstPlayBaseBin *play_base_bin,
                                 GstPlayBaseGroup *group);

  void     (*set_subtitles_visible) (GstPlayBaseBin *play_base_bin,
                                     gboolean visible);
  void     (*set_audio_mute)        (GstPlayBaseBin *play_base_bin,
                                     gboolean mute);
};

GType gst_play_base_bin_get_type (void);

G_END_DECLS

#endif /* __GST_PLAYBASEBIN_H__ */

