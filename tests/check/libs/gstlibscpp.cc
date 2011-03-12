/* GStreamer
 * Copyright (C) 2011 Tim-Philipp Müller <tim centricular net>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/gst.h>
#include <gst/check/gstcheck.h>

#include <gst/app/gstappbuffer.h>
#include <gst/app/gstapp-marshal.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>

#include <gst/audio/audio-enumtypes.h>
#include <gst/audio/audio.h>
#include <gst/audio/gstaudioclock.h>
#include <gst/audio/gstaudiofilter.h>
#include <gst/audio/gstaudiosink.h>
#include <gst/audio/gstaudiosrc.h>
#include <gst/audio/gstbaseaudiosink.h>
#include <gst/audio/gstbaseaudiosrc.h>
#include <gst/audio/gstringbuffer.h>
#include <gst/audio/mixerutils.h>
#include <gst/audio/multichannel.h>

#include <gst/cdda/gstcddabasesrc.h>

#include <gst/fft/gstfftf32.h>
#include <gst/fft/gstfftf64.h>
#include <gst/fft/gstfft.h>
#include <gst/fft/gstffts16.h>
#include <gst/fft/gstffts32.h>

#include <gst/floatcast/floatcast.h>

#include <gst/interfaces/colorbalancechannel.h>
#include <gst/interfaces/colorbalance.h>
#include <gst/interfaces/interfaces-enumtypes.h>
#include <gst/interfaces/interfaces-marshal.h>
#include <gst/interfaces/mixer.h>
#include <gst/interfaces/mixeroptions.h>
#include <gst/interfaces/mixertrack.h>
#include <gst/interfaces/navigation.h>
#include <gst/interfaces/propertyprobe.h>
#include <gst/interfaces/streamvolume.h>
#include <gst/interfaces/tunerchannel.h>
#include <gst/interfaces/tuner.h>
#include <gst/interfaces/tunernorm.h>
#include <gst/interfaces/videoorientation.h>
#include <gst/interfaces/xoverlay.h>

#include <gst/netbuffer/gstnetbuffer.h>

#include <gst/pbutils/codec-utils.h>
#include <gst/pbutils/descriptions.h>
#include <gst/pbutils/encoding-profile.h>
#include <gst/pbutils/encoding-target.h>
#include <gst/pbutils/gstdiscoverer.h>
#include <gst/pbutils/gstpluginsbaseversion.h>
#include <gst/pbutils/install-plugins.h>
#include <gst/pbutils/missing-plugins.h>
#include <gst/pbutils/pbutils-enumtypes.h>
#include <gst/pbutils/pbutils.h>
#include <gst/pbutils/pbutils-marshal.h>

#include <gst/riff/riff-ids.h>
#include <gst/riff/riff-media.h>
#include <gst/riff/riff-read.h>

#include <gst/rtp/gstbasertpaudiopayload.h>
#include <gst/rtp/gstbasertpdepayload.h>
#include <gst/rtp/gstbasertppayload.h>
#include <gst/rtp/gstrtcpbuffer.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtppayloads.h>

#include <gst/rtsp/gstrtspbase64.h>
#include <gst/rtsp/gstrtspconnection.h>
#include <gst/rtsp/gstrtspdefs.h>
#include <gst/rtsp/gstrtsp-enumtypes.h>
#include <gst/rtsp/gstrtspextension.h>
#include <gst/rtsp/gstrtsp.h>
#include <gst/rtsp/gstrtsp-marshal.h>
#include <gst/rtsp/gstrtspmessage.h>
#include <gst/rtsp/gstrtsprange.h>
#include <gst/rtsp/gstrtsptransport.h>
#include <gst/rtsp/gstrtspurl.h>

#include <gst/sdp/gstsdp.h>
#include <gst/sdp/gstsdpmessage.h>
#include <gst/tag/gsttagdemux.h>

#include <gst/tag/tag.h>

#include <gst/video/gstvideofilter.h>
#include <gst/video/gstvideosink.h>
#include <gst/video/video-enumtypes.h>
#include <gst/video/video.h>

/* we mostly just want to make sure that our library headers don't
 * contain anything a C++ compiler might not like */
GST_START_TEST (test_nothing)
{
  gst_init (NULL, NULL);
}

GST_END_TEST;

static Suite *
libscpp_suite (void)
{
  Suite *s = suite_create ("GstLibsCpp");
  TCase *tc_chain = tcase_create ("C++ libs header tests");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_nothing);

  return s;
}

GST_CHECK_MAIN (libscpp);
