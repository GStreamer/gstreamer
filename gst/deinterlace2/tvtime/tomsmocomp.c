/**
 * Copyright (C) 2004 Billy Biggs <vektor@dumbterm.net>
 * Copyright (C) 2008 Sebastian Dröge <slomo@collabora.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>
#include "_stdint.h"
#include <string.h>

#include "gst/gst.h"
#include "gstdeinterlace2.h"
#include "plugins.h"

#include "tomsmocomp/tomsmocompmacros.h"
#include "x86-64_macros.inc"

#define GST_TYPE_DEINTERLACE_METHOD_TOMSMOCOMP	(gst_deinterlace_method_tomsmocomp_get_type ())
#define GST_IS_DEINTERLACE_METHOD_TOMSMOCOMP(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_DEINTERLACE_METHOD_TOMSMOCOMP))
#define GST_IS_DEINTERLACE_METHOD_TOMSMOCOMP_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_DEINTERLACE_METHOD_TOMSMOCOMP))
#define GST_DEINTERLACE_METHOD_TOMSMOCOMP_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_DEINTERLACE_METHOD_TOMSMOCOMP, GstDeinterlaceMethodTomsMoCompClass))
#define GST_DEINTERLACE_METHOD_TOMSMOCOMP(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_DEINTERLACE_METHOD_TOMSMOCOMP, GstDeinterlaceMethodTomsMoComp))
#define GST_DEINTERLACE_METHOD_TOMSMOCOMP_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_DEINTERLACE_METHOD_TOMSMOCOMP, GstDeinterlaceMethodTomsMoCompClass))
#define GST_DEINTERLACE_METHOD_TOMSMOCOMP_CAST(obj)	((GstDeinterlaceMethodTomsMoComp*)(obj))

GType gst_deinterlace_method_tomsmocomp_get_type (void);

typedef struct
{
  GstDeinterlaceMethod parent;

  guint search_effort;
  gboolean strange_bob;
} GstDeinterlaceMethodTomsMoComp;

typedef struct
{
  GstDeinterlaceMethodClass parent_class;
} GstDeinterlaceMethodTomsMoCompClass;

static int
Fieldcopy (void *dest, const void *src, size_t count,
    int rows, int dst_pitch, int src_pitch)
{
  unsigned char *pDest = (unsigned char *) dest;
  unsigned char *pSrc = (unsigned char *) src;

  int i;

  for (i = 0; i < rows; i++) {
    memcpy (pDest, pSrc, count);
    pSrc += src_pitch;
    pDest += dst_pitch;
  }
  return 0;
}


#define IS_MMX
#define SSE_TYPE MMX
#define FUNCT_NAME tomsmocompDScaler_MMX
#include "tomsmocomp/TomsMoCompAll.inc"
#undef  IS_MMX
#undef  SSE_TYPE
#undef  FUNCT_NAME

#define IS_3DNOW
#define SSE_TYPE 3DNOW
#define FUNCT_NAME tomsmocompDScaler_3DNOW
#include "tomsmocomp/TomsMoCompAll.inc"
#undef  IS_3DNOW
#undef  SSE_TYPE
#undef  FUNCT_NAME

#define IS_SSE
#define SSE_TYPE SSE
#define FUNCT_NAME tomsmocompDScaler_SSE
#include "tomsmocomp/TomsMoCompAll.inc"
#undef  IS_SSE
#undef  SSE_TYPE
#undef  FUNCT_NAME

G_DEFINE_TYPE (GstDeinterlaceMethodTomsMoComp,
    gst_deinterlace_method_tomsmocomp, GST_TYPE_DEINTERLACE_METHOD);

enum
{
  ARG_0,
  ARG_SEARCH_EFFORT,
  ARG_STRANGE_BOB
};

static void
gst_deinterlace_method_tomsmocomp_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDeinterlaceMethodTomsMoComp *self =
      GST_DEINTERLACE_METHOD_TOMSMOCOMP (object);

  switch (prop_id) {
    case ARG_SEARCH_EFFORT:
      self->search_effort = g_value_get_uint (value);
      break;
    case ARG_STRANGE_BOB:
      self->strange_bob = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_deinterlace_method_tomsmocomp_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDeinterlaceMethodTomsMoComp *self =
      GST_DEINTERLACE_METHOD_TOMSMOCOMP (object);

  switch (prop_id) {
    case ARG_SEARCH_EFFORT:
      g_value_set_uint (value, self->search_effort);
      break;
    case ARG_STRANGE_BOB:
      g_value_set_boolean (value, self->strange_bob);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
    gst_deinterlace_method_tomsmocomp_class_init
    (GstDeinterlaceMethodTomsMoCompClass * klass)
{
  GstDeinterlaceMethodClass *dim_class = (GstDeinterlaceMethodClass *) klass;
  GObjectClass *gobject_class = (GObjectClass *) klass;
  guint cpu_flags = oil_cpu_get_flags ();

  gobject_class->set_property = gst_deinterlace_method_tomsmocomp_set_property;
  gobject_class->get_property = gst_deinterlace_method_tomsmocomp_get_property;

  g_object_class_install_property (gobject_class, ARG_SEARCH_EFFORT,
      g_param_spec_uint ("search-effort",
          "Search Effort",
          "Search Effort", 0, 27, 5, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );

  g_object_class_install_property (gobject_class, ARG_STRANGE_BOB,
      g_param_spec_boolean ("strange-bob",
          "Strange Bob",
          "Use strange bob", FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );

  dim_class->fields_required = 4;
  dim_class->name = "Motion Adaptive: Motion Search";
  dim_class->nick = "tomsmocomp";
  dim_class->latency = 1;

  if (cpu_flags & OIL_IMPL_FLAG_SSE) {
    dim_class->deinterlace_frame = tomsmocompDScaler_SSE;
  } else if (cpu_flags & OIL_IMPL_FLAG_3DNOW) {
    dim_class->deinterlace_frame = tomsmocompDScaler_3DNOW;
  } else if (cpu_flags & OIL_IMPL_FLAG_MMX) {
    dim_class->deinterlace_frame = tomsmocompDScaler_MMX;
  } else {
    dim_class->available = FALSE;
  }
}

static void
gst_deinterlace_method_tomsmocomp_init (GstDeinterlaceMethodTomsMoComp * self)
{
  self->search_effort = 5;
  self->strange_bob = FALSE;
}
