/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstcpu.c: CPU detection and architecture-specific routines
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

#include <glib.h>

#include "gst_private.h"

#include "gstcpu.h"


static guint32 _gst_cpu_flags;

#ifdef HAVE_CPU_I386
void gst_cpuid_i386(int,long *,long *,long *,long *);
#define gst_cpuid gst_cpuid_i386

#else
#define gst_cpuid(o,a,b,c,d) (void)(a);(void)(b);(void)(c);
#endif

static gchar *stringcat (gchar *a,gchar *b) {
  gchar *c;
  if (a) {
    c = g_strconcat(a,b);
    g_free(a);
  } else
    c = g_strdup(b);
  return c;
}

void 
_gst_cpu_initialize (void) 
{
  gchar *featurelist = NULL;

  long eax=0, ebx=0, ecx=0, edx=0;

  gst_cpuid(1, &eax, &ebx, &ecx, &edx);

  if (edx & (1<<23)) {
    _gst_cpu_flags |= GST_CPU_FLAG_MMX;
    featurelist = stringcat(featurelist,"MMX ");
  }
  if (edx & (1<<25)) {
    _gst_cpu_flags |= GST_CPU_FLAG_SSE;
    featurelist = stringcat(featurelist,"SSE ");
  }

  if (!_gst_cpu_flags) {
    featurelist = stringcat(featurelist,"NONE");
  }

  GST_INFO (GST_INFO_GST_INIT, "CPU features: %s",featurelist);
}

GstCPUFlags 
gst_cpu_get_flags (void) 
{
  return _gst_cpu_flags;
}
