/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2003 Colin Walters <walters@verbum.org>
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

#include "gst_private.h"
#include <glib.h>


#include "gstcpu.h"
#include "gstinfo.h"
#include <setjmp.h>
#include <signal.h>

static guint32 _gst_cpu_flags = 0;

#if defined(HAVE_CPU_I386) && defined(__GNUC__)
#define _gst_cpu_initialize_arch _gst_cpu_initialize_i386
gboolean _gst_cpu_initialize_i386 (gulong * flags, GString * featurelist);
#else
#define _gst_cpu_initialize_arch _gst_cpu_initialize_none
gboolean _gst_cpu_initialize_none (gulong * flags, GString * featurelist);
#endif


void
_gst_cpu_initialize (gboolean opt)
{
  GString *featurelist = g_string_new ("");
  gulong flags = 0;

  if (opt) {
    if (!_gst_cpu_initialize_arch (&flags, featurelist))
      g_string_append (featurelist, "NONE");
  } else
    g_string_append (featurelist, "(DISABLED)");

  GST_CAT_INFO (GST_CAT_GST_INIT, "CPU features: (%08lx) %s", flags,
      featurelist->str);
  g_string_free (featurelist, TRUE);
}

gboolean
_gst_cpu_initialize_none (gulong * flags, GString * featurelist)
{
  return FALSE;
}

#if defined(HAVE_CPU_I386) && defined(__GNUC__)

/* From liboil */

static jmp_buf jump_env;
static struct sigaction action;
static struct sigaction oldaction;
static int in_try_block;

static void
illegal_instruction_handler (int num)
{
  if (in_try_block) {
    longjmp (jump_env, 1);
  } else {
    abort ();
  }
}

void
cpu_fault_check_enable (void)
{
  memset (&action, 0, sizeof (action));
  action.sa_handler = &illegal_instruction_handler;
  sigaction (SIGILL, &action, &oldaction);
  in_try_block = 0;
}

int
cpu_fault_check_try (void (*func) (void *), void *priv)
{
  int ret;

  in_try_block = 1;
  ret = setjmp (jump_env);
  if (!ret) {
    func (priv);
  }
  in_try_block = 0;

  return (ret == 0);
}

void
cpu_fault_check_disable (void)
{
  sigaction (SIGILL, &oldaction, NULL);
}

/* end of code from liboil */

static void
gst_cpuid_i386 (int x, unsigned int *eax, unsigned int *ebx,
    unsigned int *ecx, unsigned int *edx)
{
  /* *INDENT-OFF* */
  asm (
      /* GCC-3.2 (and possibly others) don't clobber ebx properly,
       * so we save/restore it directly. */
      "  pushl %%ebx\n"
      "  cpuid\n"
      "  mov %%ebx, %%esi\n"
      "  popl %%ebx\n"
      : "=a" (*eax), "=S" (*ebx), "=c" (*ecx), "=d" (*edx)
      : "0" (x));
  /* *INDENT-ON* */
}

static void
test_cpuid (void *ignore)
{
  unsigned int eax, ebx, ecx, edx;

  gst_cpuid_i386 (0, &eax, &ebx, &ecx, &edx);
}

static gboolean
gst_cpuid_test (void)
{
  int ret;

  cpu_fault_check_enable ();
  ret = cpu_fault_check_try (test_cpuid, NULL);
  cpu_fault_check_disable ();

  return ret;
}

gboolean
_gst_cpu_initialize_i386 (gulong * flags, GString * featurelist)
{
  gboolean AMD;
  guint eax = 0, ebx = 0, ecx = 0, edx = 0;

  if (!gst_cpuid_test ()) {
    return FALSE;
  }

  gst_cpuid_i386 (0, &eax, &ebx, &ecx, &edx);

  AMD = (ebx == 0x68747541) && (ecx == 0x444d4163) && (edx == 0x69746e65);

  gst_cpuid_i386 (1, &eax, &ebx, &ecx, &edx);

  if (edx & (1 << 23)) {
    _gst_cpu_flags |= GST_CPU_FLAG_MMX;
    g_string_append (featurelist, "MMX ");

    if (edx & (1 << 25)) {
      _gst_cpu_flags |= GST_CPU_FLAG_SSE;
      _gst_cpu_flags |= GST_CPU_FLAG_MMXEXT;
      g_string_append (featurelist, "SSE ");
    }

    gst_cpuid_i386 (0x80000000, &eax, &ebx, &ecx, &edx);

    if (eax >= 0x80000001) {

      gst_cpuid_i386 (0x80000001, &eax, &ebx, &ecx, &edx);

      if (edx & (1 << 31)) {
        _gst_cpu_flags |= GST_CPU_FLAG_3DNOW;
        g_string_append (featurelist, "3DNOW ");
      }
      if (AMD && (edx & (1 << 22))) {
        _gst_cpu_flags |= GST_CPU_FLAG_MMXEXT;
        g_string_append (featurelist, "MMXEXT ");
      }
    }
  }
  *flags = _gst_cpu_flags;
  if (_gst_cpu_flags)
    return TRUE;
  return FALSE;
}
#endif

GstCPUFlags
gst_cpu_get_flags (void)
{
  return _gst_cpu_flags;
}
