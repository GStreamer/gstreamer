/*
 * Copyright (C) 2001 CodeFactory AB
 * Copyright (C) 2001 Thomas Nyberg <thomas@codefactory.se>
 * Copyright (C) 2001-2002 Andy Wingo <apwingo@eos.ncsu.edu>
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __GST_ALSA_H__
#define __GST_ALSA_H__

#define ALSA_PCM_NEW_HW_PARAMS_API
#define ALSA_PCM_NEW_SW_PARAMS_API

#include <alsa/asoundlib.h>
#include <alsa/control.h>
#include <alsa/error.h>
#include <gst/gst.h>


GST_DEBUG_CATEGORY_EXTERN (alsa_debug);
#define GST_CAT_DEFAULT alsa_debug


/* error checking for standard alsa functions */
/* NOTE: these functions require a GObject *this and can only be used in 
   functions that return TRUE on success and FALSE on error */
#define SIMPLE_ERROR_CHECK(value) G_STMT_START{ \
  int err = (value); \
  if (err < 0) { \
    GST_WARNING_OBJECT (this, "\"" #value "\": %s", snd_strerror (err)); \
    return FALSE; \
  } \
}G_STMT_END


#ifdef G_HAVE_ISO_VARARGS
#define ERROR_CHECK(value, ...) G_STMT_START{ \
  int err = (value); \
  if (err < 0) { \
    GST_WARNING_OBJECT (this, __VA_ARGS__, snd_strerror (err)); \
    return FALSE; \
  } \
}G_STMT_END

#elif defined(G_HAVE_GNUC_VARARGS)
#define ERROR_CHECK(value, args...) G_STMT_START{ \
  int err = (value); \
  if (err < 0) { \
    GST_WARNING_OBJECT (this, ## args, snd_strerror (err)); \
    return FALSE; \
  } \
}G_STMT_END

#else
#define ERROR_CHECK(value, args...) G_STMT_START{ \
  int err = (value); \
  if (err < 0) { \
    GST_WARNING_OBJECT (this, snd_strerror (err)); \
    return FALSE; \
  } \
}G_STMT_END
#endif


#define GST_ALSA_MIN_RATE	8000
#define GST_ALSA_MAX_RATE	192000
#define GST_ALSA_MAX_TRACKS	64 /* we don't support more than 64 tracks */
#define GST_ALSA_MAX_CHANNELS	32 /* tracks can have up to 32 channels */

/* Mono is 1 channel ; the 5.1 standard is 6 channels. The value for
   GST_ALSA_MAX_CHANNELS comes from alsa/mixer.h. */

/* Max allowed discontinuity in time units between timestamp and playback
   pointer before killing/inserting samples. This should be big enough to allow
   smoothing errors on different video formats. */
#define GST_ALSA_DEFAULT_DISCONT (GST_SECOND / 10)

G_BEGIN_DECLS

#define GST_ALSA(obj)			(G_TYPE_CHECK_INSTANCE_CAST(obj, GST_TYPE_ALSA, GstAlsa))
#define GST_ALSA_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST(klass, GST_TYPE_ALSA, GstAlsaClass))
#define GST_ALSA_GET_CLASS(obj)		(G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_ALSA, GstAlsaClass))
#define GST_IS_ALSA(obj)		(G_TYPE_CHECK_INSTANCE_TYPE(obj, GST_TYPE_ALSA))
#define GST_IS_ALSA_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE(klass, GST_TYPE_ALSA))
#define GST_TYPE_ALSA			(gst_alsa_get_type())

enum {
  GST_ALSA_OPEN = GST_ELEMENT_FLAG_LAST,
  GST_ALSA_RUNNING,
  GST_ALSA_CAPS_NEGO,
  GST_ALSA_FLAG_LAST = GST_ELEMENT_FLAG_LAST + 3
};

typedef enum {
  GST_ALSA_CAPS_PAUSE = 0,
  GST_ALSA_CAPS_RESUME,
  GST_ALSA_CAPS_SYNC_START
  /* add more */
} GstAlsaPcmCaps;

#define GST_ALSA_CAPS_IS_SET(obj, flag) (GST_ALSA (obj)->pcm_caps & (1<<(flag)))
#define GST_ALSA_CAPS_SET(obj, flag, set) G_STMT_START{  \
  if (set) { (GST_ALSA (obj)->pcm_caps |=  (1<<(flag))); } \
  else     { (GST_ALSA (obj)->pcm_caps &= ~(1<<(flag))); } \
}G_STMT_END

typedef struct _GstAlsaClock GstAlsaClock;
typedef struct _GstAlsaClockClass GstAlsaClockClass;

typedef struct _GstAlsa GstAlsa;
typedef struct _GstAlsaClass GstAlsaClass;

typedef int (*GstAlsaTransmitFunction) (GstAlsa *this, snd_pcm_sframes_t *avail);

typedef struct {
  snd_pcm_format_t	format;
  guint			rate;
  gint			channels;
} GstAlsaFormat;

struct _GstAlsa {
  GstElement			parent;

  /* array of GstAlsaPads */
  GstPad *			pad[GST_ALSA_MAX_TRACKS];

  gchar *			device;
  gchar *                       cardname;
  snd_pcm_t *			handle;
  guint				pcm_caps;	/* capabilities of the pcm device, see GstAlsaPcmCaps */
  snd_output_t *		out;

  GstAlsaFormat *		format;		/* NULL if undefined */
  gboolean			mmap; 		/* use mmap transmit (fast) or read/write (sloooow) */
  GstAlsaTransmitFunction	transmit;

  /* latency / performance parameters */
  snd_pcm_uframes_t		period_size;
  unsigned int			period_count;

  gboolean			autorecover;

  /* clocking */
  GstAlsaClock *		clock;		/* our provided clock */
  GstClockTime			clock_base;
  snd_pcm_uframes_t		played; 	/* samples transmitted since last sync 
						   This thing actually is our master clock.
						   We will event insert silent samples or
						   drop some to sync to incoming timestamps.
						 */
  snd_pcm_uframes_t		captured;
  GstClockTime			max_discont;	/* max difference between current
  						   playback timestamp and buffers timestamps
						 */
};

struct _GstAlsaClass {
  GstElementClass		parent_class;

  snd_pcm_stream_t		stream;

  /* different transmit functions */
  GstAlsaTransmitFunction	transmit_mmap;
  GstAlsaTransmitFunction	transmit_rw;

  /* autodetected devices available */
  GList *devices;
};

GType gst_alsa_get_type (void);

void			gst_alsa_set_eos	(GstAlsa *		this);
GstPadLinkReturn	gst_alsa_link		(GstPad *		pad,
						 const GstCaps *	caps);
GstCaps *		gst_alsa_get_caps	(GstPad *		pad);
GstCaps *		gst_alsa_fixate 	(GstPad *		pad,
                                                 const GstCaps *        caps);
GstCaps *		gst_alsa_caps		(snd_pcm_format_t	format,
						 gint			rate,
						 gint			channels);

/* audio processing functions */
inline snd_pcm_sframes_t	gst_alsa_update_avail	(GstAlsa * this);
inline gboolean			gst_alsa_pcm_wait	(GstAlsa * this);
inline gboolean			gst_alsa_start		(GstAlsa * this);
gboolean      			gst_alsa_xrun_recovery	(GstAlsa * this);

/* format conversions */
inline snd_pcm_uframes_t	gst_alsa_timestamp_to_samples 	(GstAlsa *		this,
								 GstClockTime 		time);
inline GstClockTime		gst_alsa_samples_to_timestamp 	(GstAlsa *		this,
								 snd_pcm_uframes_t 	samples);
inline snd_pcm_uframes_t	gst_alsa_bytes_to_samples 	(GstAlsa *		this,
								 guint	 		bytes);
inline guint			gst_alsa_samples_to_bytes 	(GstAlsa *		this,
								 snd_pcm_uframes_t 	samples);
inline GstClockTime		gst_alsa_bytes_to_timestamp 	(GstAlsa *		this,
								 guint	 		bytes);
inline guint			gst_alsa_timestamp_to_bytes 	(GstAlsa *		this,
								 GstClockTime	 	time);

/* debugging functions (useful in gdb) - require running with --gst-debug=alsa:4 or better */
void 				gst_alsa_sw_params_dump		(GstAlsa *		this, 
								 snd_pcm_sw_params_t *	sw_params);
void				gst_alsa_hw_params_dump		(GstAlsa *		this, 
								 snd_pcm_hw_params_t *	hw_params);


G_END_DECLS

#endif /* __GST_ALSA_H__ */
