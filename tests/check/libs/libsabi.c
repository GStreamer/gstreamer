/* GStreamer
 * Copyright (C) 2005 Wim Taymans <wim@fluendo.com>
 *               2011 Stefan Kost <ensonic@users.sf.net>
 *
 * libsabi.c: Unit test for ABI compatibility
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

#include <config.h>
#include <gst/check/gstcheck.h>

#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <gst/audio/audio.h>
#include <gst/audio/gstaudiocdsrc.h>
#include <gst/audio/gstaudioclock.h>
#include <gst/audio/gstaudiofilter.h>
#include <gst/audio/gstaudiosrc.h>
#include <gst/audio/gstaudiosink.h>
#include <gst/audio/gstaudioringbuffer.h>
#include <gst/audio/streamvolume.h>
#include <gst/fft/gstfft.h>
#include <gst/fft/gstffts16.h>
#include <gst/fft/gstffts32.h>
#include <gst/fft/gstfftf32.h>
#include <gst/fft/gstfftf64.h>
#include <gst/pbutils/pbutils.h>
#include <gst/riff/riff-media.h>
#include <gst/riff/riff-read.h>
#include <gst/rtp/gstrtpbaseaudiopayload.h>
#include <gst/rtp/gstrtpbasedepayload.h>
#include <gst/rtp/gstrtpbasepayload.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>
#include <gst/rtp/gstrtppayloads.h>
#include <gst/rtsp/gstrtsp.h>
#include <gst/rtsp/gstrtspconnection.h>
#include <gst/rtsp/gstrtspextension.h>
#include <gst/rtsp/gstrtspmessage.h>
#include <gst/rtsp/gstrtsprange.h>
#include <gst/rtsp/gstrtsptransport.h>
#include <gst/rtsp/gstrtspurl.h>
#include <gst/sdp/gstsdp.h>
#include <gst/sdp/gstsdpmessage.h>
#include <gst/tag/tag.h>
#include <gst/tag/gsttagdemux.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>
#include <gst/video/gstvideosink.h>
#include <gst/video/colorbalance.h>
#include <gst/video/videoorientation.h>
#include <gst/video/videooverlay.h>
#include <gst/video/navigation.h>

/* initial version of the file was generated using:
 * grep -A1 "<STRUCT>" ../../docs/libs/gst-plugins-base-libs-decl.txt | \
 * grep "<NAME>" | grep -v "Private" | sort | \
 * sed -e 's/<NAME>\(.*\)<\/NAME>/\  {\"\1\", sizeof (\1), 0\},/'
 *
 * it needs a bit of editing to remove opaque structs
 */

#ifdef HAVE_CPU_I386
# ifdef __APPLE__
#   include "struct_i386_osx.h"
#   define HAVE_ABI_SIZES FALSE
# else
#   include "struct_i386.h"
#   define HAVE_ABI_SIZES TRUE
# endif
#else
#ifdef HAVE_CPU_X86_64
#include "struct_x86_64.h"
#define HAVE_ABI_SIZES TRUE
#else
#ifdef HAVE_CPU_ARM
#include "struct_arm.h"
#define HAVE_ABI_SIZES FALSE
#else
#ifdef __powerpc__
#include "struct_ppc32.h"
#define HAVE_ABI_SIZES TRUE
#else /* in case someone wants to generate a new arch */
#include "struct_i386.h"
#define HAVE_ABI_SIZES FALSE
#endif
#endif
#endif
#endif

GST_START_TEST (test_ABI)
{
  gst_check_abi_list (list, HAVE_ABI_SIZES);
}

GST_END_TEST;

static Suite *
libsabi_suite (void)
{
  Suite *s = suite_create ("LibsABI");
  TCase *tc_chain = tcase_create ("size check");

  tcase_set_timeout (tc_chain, 0);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_ABI);
  return s;
}

GST_CHECK_MAIN (libsabi);
