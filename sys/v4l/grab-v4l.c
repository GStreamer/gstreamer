/*
 * interface to the v4l driver
 *
 *   (c) 1997-99 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <endian.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <X11/Intrinsic.h>

#include <asm/types.h>		/* XXX glibc */
#include <linux/videodev.h>

#include "grab.h"

#define SYNC_TIMEOUT 1

/* ---------------------------------------------------------------------- */

/* experimental, interface might change */
#ifndef VIDIOCSWIN2
#define VIDIOCSWIN2 _IOW('v',28,struct video_window2)
struct video_window2
{
    __u16   palette;                /* Palette (aka video format) in use */
    __u32   start;                  /* start address, relative to video_buffer.base */
    __u32   pitch;
    __u32   width;
    __u32   height;
    __u32   flags;
    
    struct video_clip *clips;
    int clipcount;
};
#endif

/* ---------------------------------------------------------------------- */

/* open+close */
static int   grab_open(struct GRABBER *grab_v4l, char *filename);
static int   grab_close(struct GRABBER *grab_v4l);

/* overlay */
static int   grab_setupfb(struct GRABBER *grab_v4l, int sw, int sh, int format, void *base, int width);
static int   grab_overlay(struct GRABBER *grab_v4l, int x, int y, int width, int height, int format,
                          OverlayClip *oc, int count);
static int   grab_offscreen(struct GRABBER *grab_v4l, int start, int pitch, int width, int height,
                            int format);

/* capture */
static int   grab_mm_setparams(struct GRABBER *grab_v4l, int format, int *width, int *height,
                               int *linelength);
static void* grab_mm_capture(struct GRABBER *grab_v4l, int single);
static void  grab_mm_cleanup(struct GRABBER *grab_v4l);
static int   grab_read_setparams(struct GRABBER *grab_v4l, int format, int *width, int *height,
                                 int *linelength);
static void* grab_read_capture(struct GRABBER *grab_v4l, int single);
static void  grab_read_cleanup(struct GRABBER *grab_v4l);

/* control */
static int   grab_tune(struct GRABBER *grab_v4l, unsigned long freq);
static int   grab_tuned(struct GRABBER *grab_v4l);
static int   grab_input(struct GRABBER *grab_v4l, int input, int norm);
static int   grab_hasattr(struct GRABBER *grab_v4l, int id);
static int   grab_getattr(struct GRABBER *grab_v4l, int id);
static int   grab_setattr(struct GRABBER *grab_v4l, int id, int val);

/* internal helpers */
static int   grab_wait(struct GRABBER *grab_v4l, struct video_mmap *gb);

/* ---------------------------------------------------------------------- */

static const char *device_cap[] = {
  "capture", "tuner", "teletext", "overlay", "chromakey", "clipping",
  "frameram", "scales", "monochrome", NULL
};

static const char *device_pal[] = {
  "-", "grey", "hi240", "rgb16", "rgb24", "rgb32", "rgb15",
  "yuv422", "yuyv", "uyvy", "yuv420", "yuv411", "raw",
  "yuv422p", "yuv411p", "yuv420p", "yuv410p"
};
#define PALETTE(x) ((x < sizeof(device_pal)/sizeof(char*)) ? device_pal[x] : "UNKNOWN")

static const struct STRTAB stereo[] = {
                                  {  0, "auto"    },
                                  {  1, "mono"    },
                                  {  2, "stereo"  },
                                  {  4, "lang1"   },
                                  {  8, "lang2"   },
                                  { -1, NULL,     },
                                };
static struct STRTAB norms[] = {
                                 {  0, "PAL" },
                                 {  1, "NTSC" },
                                 {  2, "SECAM" },
                                 {  3, "AUTO" },
                                 { -1, NULL }
                               };
static const struct STRTAB norms_bttv[] = {
                                      {  0, "PAL" },
                                      {  1, "NTSC" },
                                      {  2, "SECAM" },
                                      {  3, "PAL-NC" },
                                      {  4, "PAL-M" },
                                      {  5, "PAL-N" },
                                      {  6, "NTSC-JP" },
                                      { -1, NULL }
                                    };
static int gb_pal[] = {
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0,
  0, 0, 0, 0, 0
};

static const unsigned short format2palette[] = {
  0,/* unused */
  VIDEO_PALETTE_HI240,/* RGB8   */
  VIDEO_PALETTE_GREY,/* GRAY8  */
#if __BYTE_ORDER == __BIG_ENDIAN
  0,
  0,
  VIDEO_PALETTE_RGB555,/* RGB15_BE  */
  VIDEO_PALETTE_RGB565,/* RGB16_BE  */
  0,
  0,
  VIDEO_PALETTE_RGB24,/* RGB24     */
  VIDEO_PALETTE_RGB32,/* RGB32     */
#else
  VIDEO_PALETTE_RGB555,/* RGB15_LE  */
  VIDEO_PALETTE_RGB565,/* RGB16_LE  */
  0,
  0,
  VIDEO_PALETTE_RGB24,/* BGR24     */
  VIDEO_PALETTE_RGB32,/* BGR32     */
  0,
  0,
#endif
  0,                          /* LUT 2    */
  0,                          /* LUT 4    */
  VIDEO_PALETTE_YUV422,       /* YUV422   */
  VIDEO_PALETTE_YUV422P,      /* YUV422P  */
  VIDEO_PALETTE_YUV420P,      /* YUV420P  */
};

static const unsigned int format2depth[] = {
  0,               /* unused   */
  8,               /* RGB8     */
  8,               /* GRAY8    */
  16,              /* RGB15 LE */
  16,              /* RGB16 LE */
  16,              /* RGB15 BE */
  16,              /* RGB16 BE */
  24,              /* BGR24    */
  32,              /* BGR32    */
  24,              /* RGB24    */
  32,              /* RGB32    */
  16,              /* LUT2     */
  32,              /* LUT4     */
  16,          /* YUV422   */
  16,          /* YUV422P  */
  12,          /* YUV420P  */
  0,           /* MJPEG    */
};

static const unsigned char* format_desc[] = {
  "",
  "8 bit PseudoColor (dithering)",
  "8 bit StaticGray",
  "15 bit TrueColor (LE)",
  "16 bit TrueColor (LE)",
  "15 bit TrueColor (BE)",
  "16 bit TrueColor (BE)",
  "24 bit TrueColor (LE: bgr)",
  "32 bit TrueColor (LE: bgr-)",
  "24 bit TrueColor (BE: rgb)",
  "32 bit TrueColor (BE: -rgb)",
  "16 bit TrueColor (lut)",
  "32 bit TrueColor (lut)",
  "16 bit YUV 4:2:2",
  "16 bit YUV 4:2:2 (planar)",
  "12 bit YUV 4:2:0 (planar)",
  "MJPEG"
};


/* pass 0/1 by reference */
static const int                      one = 1, zero = 0;

/* ---------------------------------------------------------------------- */
static const struct GRAB_ATTR init_grab_attr[] = 
	        { 
                  { GRAB_ATTR_VOLUME,   1, VIDIOCGAUDIO, VIDIOCSAUDIO, NULL },
	          { GRAB_ATTR_MUTE,     1, VIDIOCGAUDIO, VIDIOCSAUDIO, NULL },
	          { GRAB_ATTR_MODE,     1, VIDIOCGAUDIO, VIDIOCSAUDIO, NULL },

                  { GRAB_ATTR_COLOR,    1, VIDIOCGPICT,  VIDIOCSPICT,  NULL },
                  { GRAB_ATTR_BRIGHT,   1, VIDIOCGPICT,  VIDIOCSPICT,  NULL },
                  { GRAB_ATTR_HUE,      1, VIDIOCGPICT,  VIDIOCSPICT,  NULL },
                  { GRAB_ATTR_CONTRAST, 1, VIDIOCGPICT,  VIDIOCSPICT,  NULL },
		};

struct GRABBER *grab_init()
{
  struct GRABBER *new_grabber;

  new_grabber = malloc(sizeof(struct GRABBER));

  new_grabber->name = "v4l";
  new_grabber->flags = 0;
  new_grabber->norms = norms;
  new_grabber->inputs = NULL;
  new_grabber->opened = 0;
  new_grabber->fd = -1;
  new_grabber->overlay = 0;
  new_grabber->audio_modes = stereo;
  new_grabber->grab_open = grab_open;
  new_grabber->grab_close = grab_close;
  new_grabber->grab_setupfb = grab_setupfb;
  new_grabber->grab_overlay = NULL;
  new_grabber->grab_offscreen = NULL;
  new_grabber->grab_setparams = NULL;
  new_grabber->grab_capture = NULL;
  new_grabber->grab_cleanup = NULL;
  new_grabber->grab_tune = grab_tune;
  new_grabber->grab_tuned = grab_tuned;
  new_grabber->grab_input = grab_input;
  new_grabber->grab_hasattr = grab_hasattr;
  new_grabber->grab_getattr = grab_getattr;
  new_grabber->grab_setattr = grab_setattr;

  memcpy(new_grabber->grab_attr, init_grab_attr, sizeof(init_grab_attr));

  new_grabber->grab_attr[0].arg = &new_grabber->audio;
  new_grabber->grab_attr[1].arg = &new_grabber->audio;
  new_grabber->grab_attr[2].arg = &new_grabber->audio;
  new_grabber->grab_attr[3].arg = &new_grabber->pict;
  new_grabber->grab_attr[4].arg = &new_grabber->pict;
  new_grabber->grab_attr[5].arg = &new_grabber->pict;
  new_grabber->grab_attr[6].arg = &new_grabber->pict;

  return new_grabber;
}

/* FIXME this isn't used (yet?)
static void grab_cleanup(struct GRABBER *grab_v4l)
{
}
*/

static int grab_open(struct GRABBER *grab_v4l, char *filename)
{
  int i;

  if (-1 != grab_v4l->fd)
    goto err;

  if (-1 == (grab_v4l->fd = open(filename ? filename : "/dev/video",O_RDWR))) {
    fprintf(stderr,"v4l: open %s: %s\n",
            filename ? filename : "/dev/video",strerror(errno));
    goto err;
  }

  if (-1 == ioctl(grab_v4l->fd,VIDIOCGCAP,&grab_v4l->capability)) {
    perror("v4l: open\n");
    goto err;
	}

  fprintf(stderr, "v4l: open\n");

  fcntl(grab_v4l->fd,F_SETFD,FD_CLOEXEC);

  fprintf(stderr,"v4l: device is %s\n",grab_v4l->capability.name); 

  sprintf(grab_v4l->name = malloc(strlen(grab_v4l->capability.name)+8),
          "v4l: %s",grab_v4l->capability.name);

  fprintf(stderr,"v4l: capabilities: ");
  for (i = 0; device_cap[i] != NULL; i++)
    if (grab_v4l->capability.type & (1 << i))
      fprintf(stderr," %s",device_cap[i]);
  fprintf(stderr,"\n");

  /* input sources */
  fprintf(stderr,"v4l:   channels: %d\n",grab_v4l->capability.channels);

  grab_v4l->channels = malloc(sizeof(struct video_channel)*grab_v4l->capability.channels);
  memset(grab_v4l->channels,0,sizeof(struct video_channel)*grab_v4l->capability.channels);

  grab_v4l->inputs = malloc(sizeof(struct STRTAB)*(grab_v4l->capability.channels+1));
  memset(grab_v4l->inputs,0,sizeof(struct STRTAB)*(grab_v4l->capability.channels+1));

  for (i = 0; i < grab_v4l->capability.channels; i++) {
    grab_v4l->channels[i].channel = i;
    if (-1 == ioctl(grab_v4l->fd,VIDIOCGCHAN,&grab_v4l->channels[i]))
      perror("v4l: ioctl VIDIOCGCHAN"), exit(0);
    grab_v4l->inputs[i].nr  = i;
    grab_v4l->inputs[i].str = grab_v4l->channels[i].name;
    fprintf(stderr,"v4l:    %s: %d %s%s %s%s\n",
              grab_v4l->channels[i].name,
              grab_v4l->channels[i].tuners,
              (grab_v4l->channels[i].flags & VIDEO_VC_TUNER)   ? "tuner "  : "",
              (grab_v4l->channels[i].flags & VIDEO_VC_AUDIO)   ? "audio "  : "",
              (grab_v4l->channels[i].type & VIDEO_TYPE_TV)     ? "tv "     : "",
              (grab_v4l->channels[i].type & VIDEO_TYPE_CAMERA) ? "camera " : "");
  }
  grab_v4l->inputs[i].nr  = -1;
  grab_v4l->inputs[i].str = NULL;

  /* ioctl probe, switch to input 0 */
  if (-1 == ioctl(grab_v4l->fd,VIDIOCSCHAN,&grab_v4l->channels[0])) {
    fprintf(stderr,"v4l: you need a newer bttv version (>= 0.5.14)\n");
    goto err;
  }

  /* audios */
  fprintf(stderr,"v4l:  audios  : %d\n",grab_v4l->capability.audios);

  if (grab_v4l->capability.audios) {
    grab_v4l->audio.audio = 0;
    if (-1 == ioctl(grab_v4l->fd,VIDIOCGAUDIO,&grab_v4l->audio))
      perror("v4l: ioctl VIDIOCGCAUDIO") /* , exit(0) */ ;
    fprintf(stderr,"v4l:    %d (%s): ",i,grab_v4l->audio.name);
    if (grab_v4l->audio.flags & VIDEO_AUDIO_MUTABLE)
      fprintf(stderr,"muted=%s ",
              (grab_v4l->audio.flags&VIDEO_AUDIO_MUTE) ? "yes":"no");
    if (grab_v4l->audio.flags & VIDEO_AUDIO_VOLUME)
      fprintf(stderr,"volume=%d ",grab_v4l->audio.volume);
    if (grab_v4l->audio.flags & VIDEO_AUDIO_BASS)
      fprintf(stderr,"bass=%d ",grab_v4l->audio.bass);
    if (grab_v4l->audio.flags & VIDEO_AUDIO_TREBLE)
      fprintf(stderr,"treble=%d ",grab_v4l->audio.treble);
    fprintf(stderr,"\n");

    if (!(grab_v4l->audio.flags & VIDEO_AUDIO_VOLUME)) {
      grab_v4l->grab_attr[0].have = 0; /* volume     */
    }
  } else {
    grab_v4l->grab_attr[0].have = 0; /* volume     */
    grab_v4l->grab_attr[1].have = 0; /* mute       */
    grab_v4l->grab_attr[2].have = 0; /* audio mode */
  }

  fprintf(stderr,"v4l:  size    : %dx%d => %dx%d\n",
            grab_v4l->capability.minwidth,grab_v4l->capability.minheight,
            grab_v4l->capability.maxwidth,grab_v4l->capability.maxheight);

  /* tuner (more than one???) */
  if (grab_v4l->capability.type & VID_TYPE_TUNER) {
    grab_v4l->tuner = malloc(sizeof(struct video_tuner));
    memset(grab_v4l->tuner,0,sizeof(struct video_tuner));
    if (-1 == ioctl(grab_v4l->fd,VIDIOCGTUNER,grab_v4l->tuner))
      perror("v4l: ioctl VIDIOCGTUNER");
    fprintf(stderr,"v4l:  tuner   : %s %lu-%lu",
              grab_v4l->tuner->name,grab_v4l->tuner->rangelow,grab_v4l->tuner->rangehigh);
    for (i = 0; norms[i].str != NULL; i++) {
      if (grab_v4l->tuner->flags & (1<<i)) {
        fprintf(stderr," %s",norms[i].str);
      } else
        norms[i].nr = -1;
    }
    fprintf(stderr,"\n");
  } else {
    struct video_channel vchan;

    memcpy(&vchan, &grab_v4l->channels[0], sizeof(struct video_channel));
    for (i = 0; norms[i].str != NULL; i++) {
      vchan.norm = i;
      if (-1 == ioctl(grab_v4l->fd,VIDIOCSCHAN,&vchan))
        norms[i].nr = -1;
      fprintf(stderr," %s",norms[i].str);
    }
    fprintf(stderr,"\n");
    if (-1 == ioctl(grab_v4l->fd,VIDIOCSCHAN,&grab_v4l->channels[0])) {
      fprintf(stderr,"v4l: you need a newer bttv version (>= 0.5.14)\n");
      goto err;
    }
    grab_v4l->grab_tune  = NULL;
    grab_v4l->grab_tuned = NULL;
  }
#if 1
#define BTTV_VERSION  	        _IOR('v' , BASE_VIDIOCPRIVATE+6, int)
  /* dirty hack time / v4l design flaw -- works with bttv only
   * this adds support for a few less common PAL versions */
  if (-1 != ioctl(grab_v4l->fd,BTTV_VERSION,0)) {
    grab_v4l->norms = norms_bttv;
  }
#endif
   
  /* frame buffer */
  if (-1 == ioctl(grab_v4l->fd,VIDIOCGFBUF,&grab_v4l->ov_fbuf))
    perror("v4l: ioctl VIDIOCGFBUF");
  fprintf(stderr,"v4l:  fbuffer : base=0x%p size=%dx%d depth=%d bpl=%d\n",
            grab_v4l->ov_fbuf.base, grab_v4l->ov_fbuf.width, grab_v4l->ov_fbuf.height,
            grab_v4l->ov_fbuf.depth, grab_v4l->ov_fbuf.bytesperline);

  /* picture parameters */
  if (-1 == ioctl(grab_v4l->fd,VIDIOCGPICT,&grab_v4l->pict))
    perror("v4l: ioctl VIDIOCGPICT");

  fprintf(stderr,
          "v4l:  picture : brightness=%d hue=%d colour=%d contrast=%d\n",
          grab_v4l->pict.brightness, grab_v4l->pict.hue, grab_v4l->pict.colour, grab_v4l->pict.contrast);
  fprintf(stderr,
            "v4l:  picture : whiteness=%d depth=%d palette=%s\n",
            grab_v4l->pict.whiteness, grab_v4l->pict.depth, PALETTE(grab_v4l->pict.palette));

  /* map grab buffer */
  if (-1 == ioctl(grab_v4l->fd,VIDIOCGMBUF,&grab_v4l->gb_buffers)) {
    perror("v4l: ioctl VIDIOCGMBUF");
  }
  grab_v4l->map = mmap(0,grab_v4l->gb_buffers.size,PROT_READ|PROT_WRITE,MAP_SHARED,grab_v4l->fd,0);
  if ((char*)-1 != grab_v4l->map) {
    grab_v4l->grab_setparams = grab_mm_setparams;
    grab_v4l->grab_capture   = grab_mm_capture;
    grab_v4l->grab_cleanup   = grab_mm_cleanup;
  } else {
    perror("v4l: mmap");
    grab_v4l->grab_setparams = grab_read_setparams;
    grab_v4l->grab_capture   = grab_read_capture;
    grab_v4l->grab_cleanup   = grab_read_cleanup;
  }
	grab_v4l->opened = 1;

  return grab_v4l->fd;

  err:
  if (grab_v4l->fd != -1) {
    close(grab_v4l->fd);
    grab_v4l->fd = -1;
  }
  return -1;
}

static int
  grab_close(struct GRABBER *grab_v4l)
{
  if (-1 == grab_v4l->fd)
    return 0;

  if (grab_v4l->gb_grab > grab_v4l->gb_sync)
    grab_wait(grab_v4l, grab_v4l->even ? &grab_v4l->gb_even : &grab_v4l->gb_odd);

  if ((char*)-1 != grab_v4l->map)
    munmap(grab_v4l->map,grab_v4l->gb_buffers.size);

  fprintf(stderr, "v4l: close\n");

  close(grab_v4l->fd);
  grab_v4l->fd = -1;
	grab_v4l->opened = 0;
  return 0;
}

/* ---------------------------------------------------------------------- */
/* do overlay                                                             */

static int
  grab_setupfb(struct GRABBER *grab_v4l, int sw, int sh, int format, void *base, int bpl)
{
  int settings_ok = 1;
  grab_v4l->swidth  = sw;
  grab_v4l->sheight = sh;

  /* double-check settings */
  fprintf(stderr,"v4l: %dx%d, %d bit/pixel, %d byte/scanline\n",
          grab_v4l->ov_fbuf.width,grab_v4l->ov_fbuf.height,
          grab_v4l->ov_fbuf.depth,grab_v4l->ov_fbuf.bytesperline);
  if ((bpl > 0 && grab_v4l->ov_fbuf.bytesperline != bpl) ||
      (grab_v4l->ov_fbuf.width  != sw) ||
      (grab_v4l->ov_fbuf.height != sh)) {
    fprintf(stderr,"v4l: WARNING: v4l and dga disagree about the screen size\n");
    fprintf(stderr,"v4l: WARNING: Is v4l-conf installed correctly?\n");
    settings_ok = 0;
  }
  if (format2depth[format] != ((grab_v4l->ov_fbuf.depth+7)&0xf8)) {
    fprintf(stderr,"v4l: WARNING: v4l and dga disagree about the color depth\n");
    fprintf(stderr,"v4l: WARNING: Is v4l-conf installed correctly?\n");
    fprintf(stderr,"%d %d\n",format2depth[format],grab_v4l->ov_fbuf.depth);
    settings_ok = 0;
  }
  if (settings_ok) {
    grab_v4l->grab_overlay   = grab_overlay;
    grab_v4l->grab_offscreen = grab_offscreen;
    return 0;
  } else {
    fprintf(stderr,"v4l: WARNING: overlay mode disabled\n");
    return -1;
  }
}

static int
  grab_overlay(struct GRABBER *grab_v4l, int x, int y, int width, int height, int format,
               OverlayClip *oc, int count)
  {
      int i,xadjust=0,yadjust=0;

      //fprintf(stderr,"v4l: overlay %d %d\n", x, y);

      if (width == 0 || height == 0) {
        //fprintf(stderr,"v4l: overlay off\n");
        ioctl(grab_v4l->fd, VIDIOCCAPTURE, &zero);
        grab_v4l->overlay = 0;
        return 0;
      }

      grab_v4l->ov_win.x          = x;
      grab_v4l->ov_win.y          = y;
      grab_v4l->ov_win.width      = width;
      grab_v4l->ov_win.height     = height;
      grab_v4l->ov_win.flags      = 0;

      /* check against max. size */
      ioctl(grab_v4l->fd,VIDIOCGCAP,&grab_v4l->capability);
      if (grab_v4l->ov_win.width > grab_v4l->capability.maxwidth) {
        grab_v4l->ov_win.width = grab_v4l->capability.maxwidth;
        grab_v4l->ov_win.x += (width - grab_v4l->ov_win.width)/2;
      }
      if (grab_v4l->ov_win.height > grab_v4l->capability.maxheight) {
        grab_v4l->ov_win.height = grab_v4l->capability.maxheight;
        grab_v4l->ov_win.y +=  (height - grab_v4l->ov_win.height)/2;
      }

      /* pass aligned values -- the driver does'nt get it right yet */
      grab_v4l->ov_win.width  &= ~3;
      grab_v4l->ov_win.height &= ~3;
      grab_v4l->ov_win.x      &= ~3;
      if (grab_v4l->ov_win.x              < x)        grab_v4l->ov_win.x     += 4;
      if (grab_v4l->ov_win.x+grab_v4l->ov_win.width > x+width)  grab_v4l->ov_win.width -= 4;

      /* fixups */
      xadjust = grab_v4l->ov_win.x - x;
      yadjust = grab_v4l->ov_win.y - y;

      if (grab_v4l->capability.type & VID_TYPE_CLIPPING) {
        grab_v4l->ov_win.clips      = grab_v4l->ov_clips;
        grab_v4l->ov_win.clipcount  = count;
        
        for (i = 0; i < count; i++) {
          grab_v4l->ov_clips[i].x      = oc[i].x1 - xadjust;
          grab_v4l->ov_clips[i].y      = oc[i].y1 - yadjust;
          grab_v4l->ov_clips[i].width  = oc[i].x2-oc[i].x1 /* XXX */;
          grab_v4l->ov_clips[i].height = oc[i].y2-oc[i].y1;
          //fprintf(stderr,"v4l: clip=%dx%d+%d+%d\n",
                    //grab_v4l->ov_clips[i].width,grab_v4l->ov_clips[i].height,
                    //grab_v4l->ov_clips[i].x,grab_v4l->ov_clips[i].y);
        }
      }
      if (grab_v4l->capability.type & VID_TYPE_CHROMAKEY) {
        grab_v4l->ov_win.chromakey  = 0;    /* XXX */
      }
      if (-1 == ioctl(grab_v4l->fd, VIDIOCSWIN, &grab_v4l->ov_win))
        perror("v4l: ioctl VIDIOCSWIN");

      if (!grab_v4l->overlay) {
        grab_v4l->pict.palette =
          (format < sizeof(format2palette)/sizeof(unsigned short))?
          format2palette[format]: 0;
        if(grab_v4l->pict.palette == 0) {
          fprintf(stderr,"v4l: unsupported overlay video format: %s\n",
                  format_desc[format]);
          return -1;
        }
        if (-1 == ioctl(grab_v4l->fd,VIDIOCSPICT,&grab_v4l->pict))
          perror("v4l: ioctl VIDIOCSPICT");
        if (-1 == ioctl(grab_v4l->fd, VIDIOCCAPTURE, &one))
          perror("v4l: ioctl VIDIOCCAPTURE");
        grab_v4l->overlay = 1;
      }

      //fprintf(stderr,"v4l: overlay win=%dx%d+%d+%d, %d clips\n",
        //       width,height,x,y, count);

      return 0;
  }

static int
  grab_offscreen(struct GRABBER *grab_v4l, int start, int pitch, int width, int height, int format)
{
  struct video_window2 vo;

  if (width == 0 || height == 0) {
    fprintf(stderr,"v4l: offscreen off\n");
    ioctl(grab_v4l->fd, VIDIOCCAPTURE, &zero);
    grab_v4l->overlay = 0;
    return 0;
  }

  vo.palette   = VIDEO_PALETTE_YUV422; /* FIXME */
  vo.start     = start;
  vo.pitch     = pitch;
  vo.width     = width;
  vo.height    = height;
  vo.flags     = 0;
  vo.clips     = NULL;
  vo.clipcount = 0;
   
  if (-1 == ioctl(grab_v4l->fd_grab,VIDIOCSWIN2,&vo))
    perror("v4l: ioctl VIDIOCSOFFSCREEN");
  if (-1 == ioctl(grab_v4l->fd_grab,VIDIOCCAPTURE,&one))
    perror("v4l: ioctl VIDIOCCAPTURE");
  fprintf(stderr,"v4l: offscreen size=%dx%d\n",
            width,height);
  return 0;
}

/* ---------------------------------------------------------------------- */
/* capture using mmaped buffers (with double-buffering, ...)              */

static int
  grab_queue(struct GRABBER *grab_v4l, struct video_mmap *gb, int probe)
{
  //fprintf(stderr,"g%d",gb->frame);
#if 0
  /* might be useful for debugging driver problems */
  memset(map + grab_v4l->gb_buffers.offsets[gb->frame],0,
         grab_v4l->gb_buffers.size/grab_v4l->gb_buffers.frames);
#endif
  if (-1 == ioctl(grab_v4l->fd,VIDIOCMCAPTURE,gb)) {
    if (errno == EAGAIN)
      fprintf(stderr,"v4l: grabber chip can't sync (no station tuned in?)\n");
    else
      if (!probe)
        fprintf(stderr,"v4l: ioctl VIDIOCMCAPTURE(%d,%s,%dx%d): %s\n",
                gb->frame,PALETTE(gb->format),gb->width,gb->height,
                strerror(errno));
    return -1;
  }
  //fprintf(stderr,"* ");
  grab_v4l->gb_grab++;
  return 0;
}

static int
  grab_wait(struct GRABBER *grab_v4l, struct video_mmap *gb)
{
  int ret = 0;
   
  //alarm(SYNC_TIMEOUT);
  //fprintf(stderr,"s%d",gb->frame);

  if (-1 == ioctl(grab_v4l->fd,VIDIOCSYNC,&(gb->frame))) {
    //perror("v4l: ioctl VIDIOCSYNC");
    ret = -1;
  }
  grab_v4l->gb_sync++;
  //fprintf(stderr,"* ");
  //alarm(0);
  return ret;
}

static int
  grab_probe(struct GRABBER *grab_v4l, int format)
{
  struct video_mmap gb;

  if (0 != gb_pal[format])
    goto done;

  gb.frame  = 0;
  gb.width  = 64;
  gb.height = 48;

  fprintf(stderr, "v4l: capture probe %s...\t", device_pal[format]);
  gb.format = format;
  if (-1 == grab_queue(grab_v4l, &gb,1)) {
    gb_pal[format] = 2;
    goto done;
  }
  if (-1 == grab_wait(grab_v4l, &gb)) {
    gb_pal[format] = 2;
    goto done;
  }
  gb_pal[format] = 1;
  fprintf(stderr, "ok\n");

  done:
  return gb_pal[format] == 1;
}

static int
  grab_mm_setparams(struct GRABBER *grab_v4l, int format, int *width, int *height, int *linelength)
{
  //if (!grab_v4l->opened) return -1;

  /* finish old stuff */
  if (grab_v4l->gb_grab > grab_v4l->gb_sync)
    grab_wait(grab_v4l, grab_v4l->even ? &grab_v4l->gb_even : &grab_v4l->gb_odd);

  /* verify parameters */
  ioctl(grab_v4l->fd,VIDIOCGCAP,&grab_v4l->capability);
  if (*width > grab_v4l->capability.maxwidth)
    *width = grab_v4l->capability.maxwidth;
  if (*height > grab_v4l->capability.maxheight)
    *height = grab_v4l->capability.maxheight; 
  *linelength = *width * format2depth[format] / 8;

#if 1
  /* XXX bttv bug workaround - it returns a larger size than it can handle */
  if (*width > 768+76) {
    *width = 768+76;
    *linelength = *width * format2depth[format] / 8;
  }
#endif

  /* initialize everything */
  grab_v4l->gb_even.format = grab_v4l->gb_odd.format =
                     (format < sizeof(format2palette)/sizeof(unsigned short)) ?
                     format2palette[format] : 0;
  if (grab_v4l->gb_even.format == 0 || !grab_probe(grab_v4l, grab_v4l->gb_even.format)) {
    return -1;
  }
  grab_v4l->pixmap_bytes   = format2depth[format] / 8;
  grab_v4l->gb_even.frame  = 0;
  grab_v4l->gb_odd.frame   = 1;
  grab_v4l->gb_even.width  = *width;
  grab_v4l->gb_even.height = *height;
  grab_v4l->gb_odd.width   = *width;
  grab_v4l->gb_odd.height  = *height;
  grab_v4l->even = 0;

  return 0;
}

static void*
  grab_mm_capture(struct GRABBER *grab_v4l, int single)
{
  void *buf;

  if (!single && grab_v4l->gb_grab == grab_v4l->gb_sync)
    /* streaming capture started */
    if (-1 == grab_queue(grab_v4l, grab_v4l->even ? &grab_v4l->gb_even : &grab_v4l->gb_odd,0))
      return NULL;

  if (single && grab_v4l->gb_grab > grab_v4l->gb_sync)
    /* clear streaming capture */
    grab_wait(grab_v4l, grab_v4l->even ? &grab_v4l->gb_even : &grab_v4l->gb_odd);

  /* queue */
  if (-1 == grab_queue(grab_v4l, grab_v4l->even ? &grab_v4l->gb_odd : &grab_v4l->gb_even,0))
    return NULL;
  if (grab_v4l->gb_grab > grab_v4l->gb_sync+1) {
    /* wait -- streaming */
	  //fprintf(stderr, "v4lsrc: 1 %d %d\n", grab_v4l->gb_grab, gb_sync);
    grab_wait(grab_v4l, grab_v4l->even ? &grab_v4l->gb_even : &grab_v4l->gb_odd);
    buf = grab_v4l->map + grab_v4l->gb_buffers.offsets[grab_v4l->even ? 0 : 1];
  } else {
    /* wait -- single */
	  //fprintf(stderr, "v4lsrc: 2 %d %d %d\n", grab_v4l->gb_grab, gb_sync, even);
    grab_wait(grab_v4l, grab_v4l->even ? &grab_v4l->gb_odd : &grab_v4l->gb_even);
    buf = grab_v4l->map + grab_v4l->gb_buffers.offsets[grab_v4l->even ? 1 : 0];
  }
  grab_v4l->even = !grab_v4l->even;
	//fprintf(stderr, "v4lsrc: even %d\n", even);

  return buf;
}

static void
  grab_mm_cleanup(struct GRABBER *grab_v4l)
{
  if (grab_v4l->gb_grab > grab_v4l->gb_sync)
    grab_wait(grab_v4l, grab_v4l->even ? &grab_v4l->gb_even : &grab_v4l->gb_odd);
}

/* ---------------------------------------------------------------------- */
/* capture using simple read()                                            */

static int
  grab_read_setparams(struct GRABBER *grab_v4l, int format, int *width, int *height, int *linelength)
{
  struct video_window win;
   
  grab_v4l->pict.depth   = format2depth[format];
  grab_v4l->pict.palette = format2palette[format];

  /* set format */
  if (-1 == ioctl(grab_v4l->fd,VIDIOCSPICT,&grab_v4l->pict)) {
    perror("v4l: ioctl VIDIOCSPICT");
    return -1;
  }
  if (-1 == ioctl(grab_v4l->fd,VIDIOCGPICT,&grab_v4l->pict)) {
    perror("v4l: ioctl VIDIOCGPICT");
    return -1;
  }
  
  /* set size */
  ioctl(grab_v4l->fd,VIDIOCGCAP,&grab_v4l->capability);
  if (*width > grab_v4l->capability.maxwidth)
    *width = grab_v4l->capability.maxwidth;
  if (*height > grab_v4l->capability.maxheight)
    *height = grab_v4l->capability.maxheight;
  memset(&win,0,sizeof(struct video_window));
  win.width  = *width;
  win.height = *height;
  if (-1 == ioctl(grab_v4l->fd,VIDIOCSWIN,&win)) {
    perror("v4l: ioctl VIDIOCSWIN");
    return -1;
  }
  if (-1 == ioctl(grab_v4l->fd,VIDIOCGWIN,&win)) {
    perror("v4l: ioctl VIDIOCGWIN");
    return -1;
  }

  *width  = win.width;
  *height = win.height;
  *linelength = *width * format2depth[format] / 8;

  /* alloc buffer */
  grab_v4l->grab_read_size = *linelength * *height;
  if (grab_v4l->grab_read_buf)
    free(grab_v4l->grab_read_buf);
  grab_v4l->grab_read_buf = malloc(grab_v4l->grab_read_size);
  if (NULL == grab_v4l->grab_read_buf)
    return -1;

   
  return 0;
}

static void*
grab_read_capture(struct GRABBER *grab_v4l, int single)
{
   int rc;

   rc = read(grab_v4l->fd,grab_v4l->grab_read_buf,grab_v4l->grab_read_size);
   if (grab_v4l->grab_read_size != rc) {
	   fprintf(stderr,"v4l: grabber read error (rc=%d)\n",rc);
     return NULL;
   }
   return grab_v4l->grab_read_buf;
}

static void
grab_read_cleanup(struct GRABBER *grab_v4l)
{
   if (grab_v4l->grab_read_buf) {
     free(grab_v4l->grab_read_buf);
     grab_v4l->grab_read_buf = NULL;
   }
}

/* ---------------------------------------------------------------------- */

static int
  grab_tune(struct GRABBER *grab_v4l, unsigned long freq)
{
  freq = freq*16/1000;
  fprintf(stderr,"v4l: freq: %.3f\n",(float)freq/16);
  if (-1 == ioctl(grab_v4l->fd, VIDIOCSFREQ, &freq))
    perror("v4l: ioctl VIDIOCSFREQ");
  return 0;
}

static int
  grab_tuned(struct GRABBER *grab_v4l)
{
  usleep(10000);
  if (-1 == ioctl(grab_v4l->fd,VIDIOCGTUNER,grab_v4l->tuner)) {
    perror("v4l: ioctl VIDIOCGTUNER");
    return 0;
  }
  return grab_v4l->tuner->signal ? 1 : 0;
}

static int
  grab_input(struct GRABBER *grab_v4l, int input, int norm)
{
  if (-1 != input) {
    fprintf(stderr,"v4l: input: %d\n",input);
    grab_v4l->cur_input = input;
  }
  if (-1 != norm) {
    fprintf(stderr,"v4l: norm : %d\n",norm);
    grab_v4l->cur_norm = norm;
  }

  grab_v4l->channels[grab_v4l->cur_input].norm = grab_v4l->cur_norm;
  if (-1 == ioctl(grab_v4l->fd, VIDIOCSCHAN, &grab_v4l->channels[grab_v4l->cur_input]))
    perror("v4l: ioctl VIDIOCSCHAN");
  return 0;
}

/* ---------------------------------------------------------------------- */

int grab_hasattr(struct GRABBER *grab_v4l, int id)
{
  int i;

  for (i = 0; i < NUM_ATTR; i++)
    if (id == grab_v4l->grab_attr[i].id && grab_v4l->grab_attr[i].have)
      break;
  if (i == NUM_ATTR)
    return 0;
  return 1;
}

int grab_getattr(struct GRABBER *grab_v4l, int id)
{
  int i;

  for (i = 0; i < NUM_ATTR; i++)
    if (id == grab_v4l->grab_attr[i].id && grab_v4l->grab_attr[i].have)
      break;
  if (i == NUM_ATTR)
    return -1;
  if (-1 == ioctl(grab_v4l->fd,grab_v4l->grab_attr[i].get,grab_v4l->grab_attr[i].arg))
    perror("v4l: ioctl get");

  switch (id) {
    case GRAB_ATTR_VOLUME:   return grab_v4l->audio.volume;
    case GRAB_ATTR_MUTE:     return grab_v4l->audio.flags & VIDEO_AUDIO_MUTE;
    case GRAB_ATTR_MODE:     return grab_v4l->audio.mode;
    case GRAB_ATTR_COLOR:    return grab_v4l->pict.contrast;
    case GRAB_ATTR_BRIGHT:   return grab_v4l->pict.contrast;
    case GRAB_ATTR_HUE:      return grab_v4l->pict.contrast;
    case GRAB_ATTR_CONTRAST: return grab_v4l->pict.contrast;
      default: return -1;
  }
}

int grab_setattr(struct GRABBER *grab_v4l, int id, int val)
{
  int i;
   
  /* read ... */
  for (i = 0; i < NUM_ATTR; i++)
    if (id == grab_v4l->grab_attr[i].id && grab_v4l->grab_attr[i].have)
      break;
  if (i == NUM_ATTR)
    return -1;
  if (-1 == ioctl(grab_v4l->fd,grab_v4l->grab_attr[i].set,grab_v4l->grab_attr[i].arg))
    perror("v4l: ioctl get");

  /* ... modify ... */
  switch (id) {
    case GRAB_ATTR_VOLUME:   grab_v4l->audio.volume = val; break;
    case GRAB_ATTR_MUTE:
      if (val)
        grab_v4l->audio.flags |= VIDEO_AUDIO_MUTE;
      else
        grab_v4l->audio.flags &= ~VIDEO_AUDIO_MUTE;
      break;
    case GRAB_ATTR_MODE:     grab_v4l->audio.mode      = val; break;
    case GRAB_ATTR_COLOR:    grab_v4l->pict.colour     = val; break;
    case GRAB_ATTR_BRIGHT:   grab_v4l->pict.brightness = val; break;
    case GRAB_ATTR_HUE:      grab_v4l->pict.hue        = val; break;
    case GRAB_ATTR_CONTRAST: grab_v4l->pict.contrast   = val; break;
      default: return -1;
  }

  /* ... write */
  if (-1 == ioctl(grab_v4l->fd,grab_v4l->grab_attr[i].set,grab_v4l->grab_attr[i].arg))
    perror("v4l: ioctl set");
  return 0;
}

