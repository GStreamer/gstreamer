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

/* media-info-priv.h: private stuff */

#ifndef __GST_MEDIA_INFO_PRIV_H__
#define __GST_MEDIA_INFO_PRIV_H__

#include <gst/gst.h>

/* debug */
GST_DEBUG_CATEGORY_EXTERN (gst_media_info_debug);
#define GST_CAT_DEFAULT gst_media_info_debug

#define DEBUG
#ifdef DEBUG
static gboolean _gmi_debug = TRUE;
#else
static gboolean _gmi_debug = FALSE;
#endif

#ifdef G_HAVE_ISO_VARARGS

#define GMI_DEBUG(...) \
  { if (_gmi_debug) { g_print ( __VA_ARGS__ ); }}

#elif defined(G_HAVE_GNUC_VARARGS)

#define GMI_DEBUG(format, args...) \
  { if (_gmi_debug) { g_print ( format , ## args ); }}

#endif


/* state machine enum; FIXME: can we move this to priv.c ? */
typedef enum
{
  GST_MEDIA_INFO_STATE_NULL,
  GST_MEDIA_INFO_STATE_TYPEFIND,
  GST_MEDIA_INFO_STATE_STREAM,
  GST_MEDIA_INFO_STATE_METADATA,
  GST_MEDIA_INFO_STATE_STREAMINFO,
  GST_MEDIA_INFO_STATE_FORMAT,
  GST_MEDIA_INFO_STATE_DONE
} GstMediaInfoState;

/* private structure */
struct GstMediaInfoPriv
{
  GstElement *pipeline;

  GstElement *typefind;

  GstCaps *type;
  GstPad *decoder_pad;                  /* pad for querying decoded caps */
  GstPad *source_pad;                   /* pad for querying encoded caps */

  GstCaps *format;
  GstCaps *metadata;
  gint metadata_iters;
  GstCaps *streaminfo;

  GstElement *decoder;                  /* will be != NULL during collection */
  GstElement *fakesink;			/* so we can get caps from the
                                           decoder sink pad */
  gchar *source_element;                /* type of element used as source */
  GstElement *source;

  GHashTable *decoders;                 /* a table of decoder GstElements */

  GstMediaInfoState state;              /* current state of state machine */
  gchar *location;                      /* location set on the info object */
  guint16 flags;                        /* flags supplied for detection */
  GstMediaInfoTrack *current_track;     /* track pointer under inspection */
  glong current_track_num;              /* current track under inspection */

  GstMediaInfoStream *stream;           /* total stream properties */
  char *cache;                          /* location of cache */
};

/* declarations */
GstMediaInfoStream *
		gmi_stream_new			(void);
void		gmi_stream_free			(GstMediaInfoStream *stream);

GstMediaInfoTrack *
		gmi_track_new			(void);

void		gmi_reset			(GstMediaInfo *info);
gboolean	gmi_seek_to_track		(GstMediaInfo *info,
		                                 long track);

GstElement *	gmi_get_decoder			(GstMediaInfo *info,
		                                 const char *mime);
void		gmi_set_decoder			(GstMediaInfo *info,
		                                 GstElement *decoder);
void		gmi_clear_decoder		(GstMediaInfo *info);

void		deep_notify_callback            (GObject *object,
		                                 GstObject *origin,
						 GParamSpec *pspec,
						 GstMediaInfoPriv *priv);

gboolean	gmip_find_type_pre		(GstMediaInfoPriv *priv);
gboolean	gmip_find_type_post		(GstMediaInfoPriv *priv);
gboolean	gmip_find_type			(GstMediaInfoPriv *priv);
gboolean	gmip_find_stream_pre		(GstMediaInfoPriv *priv);
gboolean	gmip_find_stream_post		(GstMediaInfoPriv *priv);
gboolean	gmip_find_stream			(GstMediaInfoPriv *priv);
gboolean	gmip_find_track_metadata_pre	(GstMediaInfoPriv *priv);
gboolean	gmip_find_track_metadata_post	(GstMediaInfoPriv *priv);
gboolean	gmip_find_track_metadata		(GstMediaInfoPriv *priv);
gboolean	gmip_find_track_streaminfo_pre	(GstMediaInfoPriv *priv);
gboolean	gmip_find_track_streaminfo_post	(GstMediaInfoPriv *priv);
gboolean	gmip_find_track_streaminfo	(GstMediaInfoPriv *priv);
gboolean	gmip_find_track_format_pre	(GstMediaInfoPriv *priv);
gboolean	gmip_find_track_format_post	(GstMediaInfoPriv *priv);
gboolean	gmip_find_track_format		(GstMediaInfoPriv *priv);

#endif /* __GST_MEDIA_INFO_PRIV_H__ */
