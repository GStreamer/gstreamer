#define VIDEO_RGB08          1  /* bt848 dithered */
#define VIDEO_GRAY           2
#define VIDEO_RGB15_LE       3  /* 15 bpp little endian */
#define VIDEO_RGB16_LE       4  /* 16 bpp little endian */
#define VIDEO_RGB15_BE       5  /* 15 bpp big endian */
#define VIDEO_RGB16_BE       6  /* 16 bpp big endian */
#define VIDEO_BGR24          7  /* bgrbgrbgrbgr (LE) */
#define VIDEO_BGR32          8  /* bgr-bgr-bgr- (LE) */
#define VIDEO_RGB24          9  /* rgbrgbrgbrgb (BE)*/
#define VIDEO_RGB32         10  /* -rgb-rgb-rgb (BE)*/
#define VIDEO_LUT2          11  /* lookup-table 2 byte depth */
#define VIDEO_LUT4          12  /* lookup-table 4 byte depth */
#define VIDEO_YUV422	    13  /* YUV 4:2:2 */
#define VIDEO_YUV422P       14  /* YUV 4:2:2 (planar) */
#define VIDEO_YUV420P	    15  /* YUV 4:2:0 (planar) */
#define VIDEO_MJPEG	    16  /* MJPEG */

#define CAN_AUDIO_VOLUME     1

#define GRAB_ATTR_VOLUME     1
#define GRAB_ATTR_MUTE       2
#define GRAB_ATTR_MODE       3

#define GRAB_ATTR_COLOR     11
#define GRAB_ATTR_BRIGHT    12
#define GRAB_ATTR_HUE       13
#define GRAB_ATTR_CONTRAST  14

#define TRAP(txt) fprintf(stderr,"%s:%d:%s\n",__FILE__,__LINE__,txt);exit(1);

/* ------------------------------------------------------------------------- */

struct STRTAB {
    long nr;
    char *str;
};

typedef struct _OverlayClip OverlayClip;

struct _OverlayClip {
	  int x1, x2, y1, y2;
};

struct GRAB_ATTR {
	  int   id;
	  int   have;
	  int   get;
	  int   set;
	  void  *arg;
};

struct GRABBER {
    char            *name;
    int             flags;
    const struct STRTAB   *norms;
    struct STRTAB   *inputs;
    const struct STRTAB   *audio_modes;
		int             opened;
		char            *map;
		int             fd, fd_grab;

		/* generic informations */
		struct video_capability  capability;
		struct video_channel     *channels;
		struct video_audio       audio;
		struct video_tuner       *tuner;
		struct video_picture     pict;
#define NUM_ATTR 7
		struct GRAB_ATTR         grab_attr[NUM_ATTR];

		int                      cur_input;
		int                      cur_norm;
		int   grab_read_size;
		char *grab_read_buf;

		/* overlay */
		struct video_window      ov_win;
		struct video_clip        ov_clips[32];
		struct video_buffer      ov_fbuf;

		/* screen grab */
		struct video_mmap        gb_even;
		struct video_mmap        gb_odd;
		int                      even,pixmap_bytes;
		int                      gb_grab,gb_sync;
		struct video_mbuf        gb_buffers;


		/* state */
		int                      overlay, swidth, sheight;

    int   (*grab_open)(struct GRABBER *grab_v4l, char *opt);
    int   (*grab_close)(struct GRABBER *grab_v4l);

    int   (*grab_setupfb)(struct GRABBER *grab_v4l, int sw, int sh, int format, void *base, int bpl);
    int   (*grab_overlay)(struct GRABBER *grab_v4l, int x, int y, int width, int height, int format,
					           OverlayClip *oc, int count);
    int   (*grab_offscreen)(struct GRABBER *grab_v4l, int start, int pitch, int width, int height,
				             int format);

		int   (*grab_setparams)(struct GRABBER *grab_v4l, int format, int *width, int *height, int *linelength);
		void* (*grab_capture)(struct GRABBER *grab_v4l, int single);
		void  (*grab_cleanup)(struct GRABBER *grab_v4l);

    int   (*grab_tune)(struct GRABBER *grab_v4l, unsigned long freq);
		int   (*grab_tuned)(struct GRABBER *grab_v4l);
		int   (*grab_input)(struct GRABBER *grab_v4l, int input, int norm);

#if 0
		int   (*grab_picture)(int color, int bright, int hue, int contrast);
	  int   (*grab_audio)(int mute, int volume, int *mode);
#else
	  int   (*grab_hasattr)(struct GRABBER *grab_v4l, int id);
	  int   (*grab_getattr)(struct GRABBER *grab_v4l, int id);
	  int   (*grab_setattr)(struct GRABBER *grab_v4l, int id, int val);
#endif
};

/* ------------------------------------------------------------------------- */

struct GRABBER *grab_init();
