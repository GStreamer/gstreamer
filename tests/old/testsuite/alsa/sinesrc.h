/*
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * sinesrc.h: Header file for sinesrc.c
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __SINESRC_H__
#define __SINESRC_H__


#include <gst/gst.h>

#ifdef __cplusplus
extern "C"
{
#endif				/* __cplusplus */


#define TYPE_SINESRC \
  (sinesrc_get_type())
#define SINESRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),TYPE_SINESRC,SineSrc))
#define SINESRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),TYPE_SINESRC,SineSrcClass))
#define IS_SINESRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),TYPE_SINESRC))
#define IS_SINESRC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),TYPE_SINESRC))

  typedef struct _SineSrc SineSrc;
  typedef struct _SineSrcClass SineSrcClass;

  typedef void (*PreGetFunc) (SineSrc * src);

  typedef enum
  {
    SINE_SRC_INT,
    SINE_SRC_FLOAT
  } SineSrcAudio;

  struct _SineSrc
  {
    GstElement element;

    /* pads */
    GstPad *src;

    /* audio parameters */
    SineSrcAudio type;
    gint width;			/* int + float */
    gint depth;			/* int */
    gboolean sign;		/* int */
    gint endianness;		/* int */

    gint rate;
    gint channels;		/* interleaved */

    gboolean newcaps;

    /* freaky stuff for testing */
    PreGetFunc pre_get_func;
  };

  struct _SineSrcClass
  {
    GstElementClass parent_class;
  };

  GType sinesrc_get_type (void);
  GstElement *sinesrc_new (void);

  void sinesrc_set_pre_get_func (SineSrc * src, PreGetFunc func);

#ifdef __cplusplus
}
#endif				/* __cplusplus */


#endif				/* __GST_SINESRC_H__ */
