/*
 * Copyright (C) 2024 Piotr Brzeziński <piotr@centricular.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "gstiosaudiosession.h"
#include <AVFAudio/AVFAudio.h>

// Default according to: https://developer.apple.com/documentation/avfaudio/avaudiosessioncategorysoloambient
#define DEFAULT_CAT AVAudioSessionCategorySoloAmbient

void 
gst_ios_audio_session_setup (gboolean is_src)
{
  AVAudioSession *session = [AVAudioSession sharedInstance];

  /* This is just a quick best effort setup.
   * In any serious applications, you should disable configure-session
   * and handle AVAudioSession setup yourself. */

  if (is_src) {
    /* For mic capture, let's use PlayAndRecord as that allows simultaneous playback and recording
     * We don't have to check if a sink (output) already set a category, as the behaviour will not
     * change in PlayAndRecord. */
    [session setCategory:AVAudioSessionCategoryPlayAndRecord error:NULL];
  } else if ([session category] == DEFAULT_CAT) {
    /* For output, let's just use Playback, but only if a src element didn't set things up first */
    [session setCategory:AVAudioSessionCategoryPlayback error:NULL];
  }

  [session setActive:YES error:NULL];
}
