/*
 * utils for libavcodec
 * Copyright (c) 2001 Fabrice Bellard.
 * Copyright (c) 2003 Michel Bardiaux for the av_log API
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/**
 * @file utils.c
 * utils.
 */

#include "avcodec.h"
#include "dsputil.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>

void *
av_mallocz (unsigned int size)
{
  void *ptr;

  ptr = av_malloc (size);
  if (!ptr)
    return NULL;
  memset (ptr, 0, size);
  return ptr;
}

char *
av_strdup (const char *s)
{
  char *ptr;
  int len;

  len = strlen (s) + 1;
  ptr = av_malloc (len);
  if (!ptr)
    return NULL;
  memcpy (ptr, s, len);
  return ptr;
}

/**
 * realloc which does nothing if the block is large enough
 */
void *
av_fast_realloc (void *ptr, unsigned int *size, unsigned int min_size)
{
  if (min_size < *size)
    return ptr;

  *size = 17 * min_size / 16 + 32;

  return av_realloc (ptr, *size);
}


static unsigned int last_static = 0;
static unsigned int allocated_static = 0;
static void **array_static = NULL;

/**
 * allocation of static arrays - do not use for normal allocation.
 */
void *
av_mallocz_static (unsigned int size)
{
  void *ptr = av_mallocz (size);

  if (ptr) {
    array_static =
        av_fast_realloc (array_static, &allocated_static,
        sizeof (void *) * (last_static + 1));
    array_static[last_static++] = ptr;
  }

  return ptr;
}

/**
 * free all static arrays and reset pointers to 0.
 */
void
av_free_static (void)
{
  while (last_static) {
    av_freep (&array_static[--last_static]);
  }
  av_freep (&array_static);
}

/**
 * Frees memory and sets the pointer to NULL.
 * @param arg pointer to the pointer which should be freed
 */
void
av_freep (void *arg)
{
  void **ptr = (void **) arg;

  av_free (*ptr);
  *ptr = NULL;
}

void
avcodec_get_context_defaults (AVCodecContext * s)
{
  memset (s, 0, sizeof (AVCodecContext));

  s->frame_rate_base = 1;
  s->frame_rate = 25;
}

/**
 * allocates a AVCodecContext and set it to defaults.
 * this can be deallocated by simply calling free() 
 */
AVCodecContext *
avcodec_alloc_context (void)
{
  AVCodecContext *avctx = av_malloc (sizeof (AVCodecContext));

  if (avctx == NULL)
    return NULL;

  avcodec_get_context_defaults (avctx);

  return avctx;
}

/* must be called before any other functions */
void
avcodec_init (void)
{
  static int inited = 0;

  if (inited != 0)
    return;
  inited = 1;

  dsputil_static_init ();
}
