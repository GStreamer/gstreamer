#ifndef __GSTPLAY_H__
#define __GSTPLAY_H__

#include <gst/gst.h>

#define GST_TYPE_PLAY          (gst_play_get_type ())
#define GST_PLAY(obj)          (GTK_CHECK_CAST ((obj), GST_TYPE_PLAY, GstPlay))
#define GST_PLAY_CLASS(klass)  (GTK_CHECK_CLASS_CAST ((klass), GST_TYPE_PLAY, GstPlayClass))
#define GST_IS_PLAY(obj)       (GTK_CHECK_TYPE ((obj), GST_TYPE_PLAY))
#define GST_IS_PLAY_CLASS(obj) (GTK_CHECK_CLASS_TYPE ((klass), GST_TYPE_PLAY))

typedef struct _GstPlay GstPlay;
typedef struct _GstPlayClass GstPlayClass;

typedef enum {
	GST_PLAY_STOPPED,
	GST_PLAY_PLAYING,
	GST_PLAY_PAUSED,
} GstPlayState;

typedef enum {
  GST_PLAY_OK,
  GST_PLAY_UNKNOWN_MEDIA,
  GST_PLAY_CANNOT_PLAY,
  GST_PLAY_ERROR,
} GstPlayReturn;

typedef enum {
	GST_PLAY_TYPE_AUDIO = (1 << 0),
	GST_PLAY_TYPE_VIDEO = (1 << 1),
} GstPlayMediaTypeFlags;

struct _GstPlay {
	GtkHBox parent;
	
	GstPlayState state;
	GstPlayMediaTypeFlags flags;
	
	gpointer priv;
};

#define GST_PLAY_STATE(play)         ((play)->state)
#define GST_PLAY_MEDIA_TYPE(play)    ((play)->flags)
#define GST_PLAY_IS_AUDIO_TYPE(play) ((play)->flags & GST_PLAY_TYPE_AUDIO)
#define GST_PLAY_IS_VIDEO_TYPE(play) ((play)->flags & GST_PLAY_TYPE_VIDEO)

struct _GstPlayClass {
	GtkHBoxClass parent_class;

	void (*state_changed)   (GstPlay *play, GstPlayState state);
	void (*frame_displayed) (GstPlay *play);
	void (*audio_played)    (GstPlay *play);
};


GtkType 	gst_play_get_type		(void);

/* setup the player */
GstPlay*	gst_play_new			(void);
GstPlayReturn 	gst_play_set_uri		(GstPlay *play, const guchar *uri);

/* control the player */
void 		gst_play_play			(GstPlay *play);
void 		gst_play_pause			(GstPlay *play);
void 		gst_play_stop			(GstPlay *play);

void 		gst_play_mute			(GstPlay *play, gboolean mute);

/* information about the media stream */
gulong 		gst_play_get_media_size		(GstPlay *play);
gulong 		gst_play_get_media_offset	(GstPlay *play);
gboolean	gst_play_media_can_seek		(GstPlay *play);
void 		gst_play_media_seek		(GstPlay *play, gulong offset);

gulong 		gst_play_get_media_total_time	(GstPlay *play);
gulong 		gst_play_get_media_current_time	(GstPlay *play);

/* set display stuff */
void            gst_play_set_display_size       ();

/* the autoplugged pipeline */
GstElement*	gst_play_get_pipeline		(GstPlay *play);

#endif /* __GSTPLAY_H__ */
