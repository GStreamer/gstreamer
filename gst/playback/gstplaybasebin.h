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


#ifndef __GST_PLAYBASEBIN_H__
#define __GST_PLAYBASEBIN_H__

#include <gst/gst.h>
#include "gststreaminfo.h"

G_BEGIN_DECLS

#define GST_TYPE_PLAY_BASE_BIN 		(gst_play_base_bin_get_type())
#define GST_PLAY_BASE_BIN(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_PLAY_BASE_BIN,GstPlayBaseBin))
#define GST_PLAY_BASE_BIN_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_PLAY_BASE_BIN,GstPlayBaseBinClass))
#define GST_IS_PLAY_BASE_BIN(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_PLAY_BASE_BIN))
#define GST_IS_PLAY_BASE_BIN_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_PLAY_BASE_BIN))

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
  GstPlayBaseBin *bin;	/* ref to the owner */

  gint		 nstreams;
  GList		*streaminfo;

  gint		 naudiopads;
  gint		 nvideopads;
  gint		 nunknownpads;

  GList		*preroll_elems;
} GstPlayBaseGroup;

struct _GstPlayBaseBin {
  GstBin 	 bin;
	
  /* properties */
  gboolean	 threaded;
  guint64	 queue_size;

  /* internal thread */
  GstElement	*thread;
  gchar 	*uri;
  GstElement	*source;
  GstElement	*decoder;
  gboolean	 need_rebuild;

  /* group management */
  GMutex	*group_lock;		/* lock and mutex to signal availability of new group */
  GCond		*group_cond;
  GstPlayBaseGroup *building_group; 	/* the group that we are constructing */
  GList		*queued_groups;      	/* the constructed groups, head is the active one */

  /* list of usable factories */
  GList		*factories;
};

struct _GstPlayBaseBinClass {
  GstBinClass 	 parent_class;

  /* signals */
  void (*setup_output_pads)	(GstPlayBaseBin *play_base_bin);
  void (*removed_output_pad)	(GstPlayBaseBin *play_base_bin,
				 GstStreamInfo *info);

  /* action signals */
  gboolean (*link_stream)	(GstPlayBaseBin *play_base_bin, 
				 GstStreamInfo *info,
				 GstPad *pad);
  void	(*unlink_stream)	(GstPlayBaseBin *play_base_bin, 
				 GstStreamInfo *info);
};

GType gst_play_base_bin_get_type (void);

gint		gst_play_base_bin_get_nstreams		(GstPlayBaseBin *play_base_bin);
const GList*	gst_play_base_bin_get_streaminfo	(GstPlayBaseBin *play_base_bin);
gint		gst_play_base_bin_get_nstreams_of_type	(GstPlayBaseBin *play_base_bin,
							 GstStreamType type);

gboolean	gst_play_base_bin_link_stream		(GstPlayBaseBin *play_base_bin, 
							 GstStreamInfo *info,
							 GstPad *pad);
void		gst_play_base_bin_unlink_stream		(GstPlayBaseBin *play_base_bin, 
							 GstStreamInfo *info);

G_END_DECLS

#endif /* __GST_PLAYBASEBIN_H__ */

