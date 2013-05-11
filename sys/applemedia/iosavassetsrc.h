/*
 * GStreamer
 * Copyright (C) 2013 Fluendo S.L. <support@fluendo.com>
 *    Authors: Andoni Morales Alastruey <amorales@fluendo.com>
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

#ifndef __GST_AVASSET_SRC_H__
#define __GST_AVASSET_SRC_H__

#include <gst/gst.h>
#import <AVFoundation/AVAssetReader.h>
#import <AVFoundation/AVAssetReaderOutput.h>

G_BEGIN_DECLS

#define GST_TYPE_AVASSET_SRC \
  (gst_avasset_src_get_type())
#define GST_AVASSET_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AVASSET_SRC,GstAVAssetSrc))
#define GST_AVASSET_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AVASSET_SRC,GstAVAssetSrcClass))
#define GST_IS_AVASSET_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AVASSET_SRC))
#define GST_IS_AVASSET_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AVASSET_SRC))
#define GST_AVASSET_SRC_ERROR gst_avasset_src_error_quark ()

typedef struct _GstAVAssetSrc      GstAVAssetSrc;
typedef struct _GstAVAssetSrcClass GstAVAssetSrcClass;

typedef enum
{
  GST_AVASSET_READER_MEDIA_TYPE_AUDIO,
  GST_AVASSET_READER_MEDIA_TYPE_VIDEO,
} GstAVAssetReaderMediaType;

typedef enum
{
  GST_AVASSET_ERROR_NOT_PLAYABLE,
  GST_AVASSET_ERROR_INIT,
  GST_AVASSET_ERROR_START,
  GST_AVASSET_ERROR_READ,
} GstAVAssetError;

typedef enum
{
  GST_AVASSET_SRC_STATE_STOPPED,
  GST_AVASSET_SRC_STATE_STARTED,
  GST_AVASSET_SRC_STATE_READING,
} GstAVAssetSrcState;

@interface GstAVAssetReader: NSObject
{
  AVAsset *asset;
  AVAssetReader *reader;
  AVAssetReaderTrackOutput *video_track;
  AVAssetReaderTrackOutput *audio_track;
  NSArray *audio_tracks;
  NSArray *video_tracks;
  int selected_audio_track;
  int selected_video_track;
  GstCaps *audio_caps;
  GstCaps *video_caps;
  gboolean reading;
  GstClockTime duration;
  GstClockTime position;
}

@property GstClockTime duration;
@property GstClockTime position;

- (id) initWithURI:(gchar*) uri : (GError **) error;
- (void) start : (GError **) error;
- (void) stop;
- (void) seekTo: (guint64) start : (guint64) stop : (GError **) error;
- (bool) hasMediaType: (GstAVAssetReaderMediaType) type;
- (GstCaps *) getCaps: (GstAVAssetReaderMediaType) type;
- (bool) selectTrack: (GstAVAssetReaderMediaType) type : (gint) index;
- (GstBuffer *) nextBuffer:  (GstAVAssetReaderMediaType) type : (GError **) error;
@end

struct _GstAVAssetSrc
{
  GstElement element;

  GstPad *videopad;
  GstPad *audiopad;
  GstTask *video_task;
  GstTask *audio_task;
  GStaticRecMutex video_lock;
  GStaticRecMutex audio_lock;
  gint selected_video_track;
  gint selected_audio_track;

  GstAVAssetReader *reader;
  GstAVAssetSrcState state;
  GMutex lock;
  GstEvent *seek_event;

  /* Properties */
  gchar * uri;
};

struct _GstAVAssetSrcClass
{
  GstElementClass parent_class;
};

GType gst_avasset_src_get_type (void);

G_END_DECLS

#endif /* __GST_AVASSET_SRC_H__ */
