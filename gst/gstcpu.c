/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#include "config.h"
#include "gstcpu.h"

static guint32 _gst_cpu_flags;

#ifdef HAVE_CPU_I386
void gst_cpuid_i386(int,long *,long *,long *,long *);
#define gst_cpuid gst_cpuid_i386

#else
#define gst_cpuid(o,a,b,c,d) (void)(a);(void)(b);(void)(c);
#endif

void _gst_cpu_initialize(void) 
{
  long eax=0, ebx=0, ecx=0, edx=0;

  gst_cpuid(1, &eax, &ebx, &ecx, &edx);

  g_print("CPU features : ");

  if (edx & (1<<23)) {
    _gst_cpu_flags |= GST_CPU_FLAG_MMX;
    g_print("MMX ");
  }
  if (edx & (1<<25)) {
    _gst_cpu_flags |= GST_CPU_FLAG_SSE;
    g_print("SSE ");
  }

  if (!_gst_cpu_flags) {
    g_print("NONE");
  }
  g_print("\n");

}

guint32 gst_cpu_get_flags(void) 
{
  return _gst_cpu_flags;
}
