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

struct _GstPlayBaseBin {
  GstBin 	 bin;
	
  /* properties */
  gboolean	 threaded;
  GMutex	*preroll_lock;
  GCond		*preroll_cond;

  /* internal thread */
  GstElement	*thread;
  gchar 	*uri;
  GstElement	*source;

  gint		 nstreams;
  GList		*streaminfo;

  /* list of usable factories */
  GList		*factories;
};

struct _GstPlayBaseBinClass {
  GstBinClass 	 parent_class;

  void	(*mute_stream)		(GstPlayBaseBin *play_base_bin, 
				 GstStreamInfo *info,
				 gboolean mute);
  void	(*link_stream)		(GstPlayBaseBin *play_base_bin, 
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

void		gst_play_base_bin_mute_stream		(GstPlayBaseBin *play_base_bin, 
							 GstStreamInfo *info,
							 gboolean mute);
void		gst_play_base_bin_link_stream		(GstPlayBaseBin *play_base_bin, 
							 GstStreamInfo *info,
							 GstPad *pad);
void		gst_play_base_bin_unlink_stream		(GstPlayBaseBin *play_base_bin, 
							 GstStreamInfo *info);

G_END_DECLS

#endif /* __GST_PLAYBASEBIN_H__ */

