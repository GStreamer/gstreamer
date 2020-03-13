/* GStreamer
 * Copyright (C) 2003 Christophe Fergeau <teuf@gnome.org>
 * Copyright (C) 2008 Jonathan Matthew <jonathan@d14n.org>
 * Copyright (C) 2008 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * gstflactag.c: plug-in for reading/modifying vorbis comments in flac files
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef GST_FLAC_TAG_H
#define GST_FLAC_TAG_H

#include <gst/gst.h>
#include <gst/base/gstadapter.h>

#define GST_TYPE_FLAC_TAG (gst_flac_tag_get_type())
G_DECLARE_FINAL_TYPE (GstFlacTag, gst_flac_tag, GST, FLAC_TAG, GstElement)

typedef enum
{
  GST_FLAC_TAG_STATE_INIT,
  GST_FLAC_TAG_STATE_METADATA_BLOCKS,
  GST_FLAC_TAG_STATE_METADATA_NEXT_BLOCK,
  GST_FLAC_TAG_STATE_WRITING_METADATA_BLOCK,
  GST_FLAC_TAG_STATE_VC_METADATA_BLOCK,
  GST_FLAC_TAG_STATE_ADD_VORBIS_COMMENT,
  GST_FLAC_TAG_STATE_AUDIO_DATA
}
GstFlacTagState;

struct _GstFlacTag
{
  GstElement element;

  /* < private > */

  /* pads */
  GstPad *sinkpad;
  GstPad *srcpad;

  GstFlacTagState state;

  GstAdapter *adapter;
  GstBuffer *vorbiscomment;
  GstTagList *tags;

  guint metadata_block_size;
  gboolean metadata_last_block;
};

#endif /* GST_FLAC_TAG_H */
