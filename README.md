# GStreamer AJA source/sink plugin

[GStreamer](https://gstreamer.freedesktop.org/) plugin for
[AJA](https://www.aja.com) capture and output cards.

This plugin requires the NTV2 SDK version 16 or newer.

## Example usage

Capture 1080p30 audio/video and display it locally

```sh
gst-launch-1.0 ajasrc video-format=1080p-3000 ! ajasrcdemux name=d \
    d.video ! queue max-size-bytes=0 max-size-buffers=0 max-size-time=1000000000 ! videoconvert ! autovideosink \
    d.audio ! queue max-size-bytes=0 max-size-buffers=0 max-size-time=1000000000 ! audioconvert ! audioresample ! autoaudiosink
```

Output a 1080p2997 test audio/video stream

```sh
gst-launch-1.0 videotestsrc pattern=ball ! video/x-raw,format=v210,width=1920,height=1080,framerate=30000/1001,interlace-mode=progressive ! timeoverlay ! timecodestamper ! combiner.video \
    audiotestsrc freq=440 ! audio/x-raw,format=S32LE,rate=48000,channels=16 ! audiobuffersplit output-buffer-duration=1/30 ! combiner.audio \
    ajasinkcombiner name=combiner ! ajasink channel=0
```

Capture 1080p30 audio/video and directly output it again on the same card

```sh
gst-launch-1.0 ajasrc video-format=1080p-3000 channel=1 input-source=sdi-1 audio-system=2 ! ajasrcdemux name=d \
    d.video ! queue max-size-bytes=0 max-size-buffers=0 max-size-time=1000000000 ! c.video \
    d.audio ! queue max-size-bytes=0 max-size-buffers=0 max-size-time=1000000000 ! c.audio \
    ajasinkcombiner name=c ! ajasink channel=0 reference-source=input-1
```

## Configuration

### Source

```
  audio-source        : Audio source to use
                        flags: readable, writable
                        Enum "GstAjaAudioSource" Default: 0, "Embedded"
                           (0): Embedded         - embedded
                           (1): AES              - aes
                           (2): Analog           - analog
                           (3): HDMI             - hdmi
                           (4): Microphone       - mic
  audio-system        : Audio system to use
                        flags: readable, writable
                        Enum "GstAjaAudioSystem" Default: 0, "Auto (based on selected channel)"
                           (0): Auto (based on selected channel) - auto
                           (1): Audio system 1   - 1
                           (2): Audio system 2   - 2
                           (3): Audio system 3   - 3
                           (4): Audio system 4   - 4
                           (5): Audio system 5   - 5
                           (6): Audio system 6   - 6
                           (7): Audio system 7   - 7
                           (8): Audio system 8   - 8
  capture-cpu-core    : Sets the affinity of the capture thread to this CPU core (-1=disabled)
                        flags: readable, writable
                        Unsigned Integer. Range: 0 - 4294967295 Default: 4294967295 
  channel             : Channel to use
                        flags: readable, writable
                        Unsigned Integer. Range: 0 - 7 Default: 0 
  device-identifier   : Input device instance to use
                        flags: readable, writable
                        String. Default: "0"
  input-source        : Input source to use
                        flags: readable, writable
                        Enum "GstAjaInputSource" Default: 0, "Auto (based on selected channel)"
                           (0): Auto (based on selected channel) - auto
                           (1): Analog Input 1   - analog-1
                           (6): SDI Input 1      - sdi-1
                           (7): SDI Input 2      - sdi-2
                           (8): SDI Input 3      - sdi-3
                           (9): SDI Input 4      - sdi-4
                           (10): SDI Input 5      - sdi-5
                           (11): SDI Input 6      - sdi-6
                           (12): SDI Input 7      - sdi-7
                           (13): SDI Input 8      - sdi-8
                           (2): HDMI Input 1     - hdmi-1
                           (3): HDMI Input 2     - hdmi-2
                           (4): HDMI Input 3     - hdmi-3
                           (5): HDMI Input 4     - hdmi-4
  queue-size          : Size of internal queue in number of video frames. Half of this is allocated as device buffers and equal to the latency.
                        flags: readable, writable
                        Unsigned Integer. Range: 1 - 2147483647 Default: 16 
  reference-source    : Reference source to use
                        flags: readable, writable
                        Enum "GstAjaReferenceSource" Default: 1, "Freerun"
                           (0): Auto             - auto
                           (1): Freerun          - freerun
                           (2): External         - external
                           (3): SDI Input 1      - input-1
                           (4): SDI Input 2      - input-2
                           (5): SDI Input 3      - input-3
                           (6): SDI Input 4      - input-4
                           (7): SDI Input 5      - input-5
                           (8): SDI Input 6      - input-6
                           (9): SDI Input 7      - input-7
                           (10): SDI Input 8      - input-8
  timecode-index      : Timecode index to use
                        flags: readable, writable
                        Enum "GstAjaTimecodeIndex" Default: 0, "Embedded SDI ATC LTC"
                           (0): Embedded SDI VITC - vitc
                           (0): Embedded SDI ATC LTC - atc-ltc
                           (2): Analog LTC 1     - ltc-1
                           (3): Analog LTC 2     - ltc-2
  video-format        : Video format to use
                        flags: readable, writable
                        Enum "GstAjaVideoFormat" Default: 0, "1080i 5000"
                           (0): 1080i 5000       - 1080i-5000
                           (1): 1080i 5994       - 1080i-5994
                           (2): 1080i 6000       - 1080i-6000
                           (3): 720p 5994        - 720p-5994
                           (4): 720p 6000        - 720p-6000
                           (5): 1080p 2997       - 1080p-2997
                           (6): 1080p 3000       - 1080p-3000
                           (7): 1080p 2500       - 1080p-2500
                           (8): 1080p 2398       - 1080p-2398
                           (9): 1080p 2400       - 1080p-2400
                           (10): 720p 5000        - 720p-5000
                           (11): 720p 2398        - 720p-2398
                           (12): 720p 2500        - 720p-2500
                           (13): 1080p 5000 A     - 1080p-5000-a
                           (14): 1080p 5994 A     - 1080p-5994-a
                           (15): 1080p 6000 A     - 1080p-6000-a
                           (16): 625 5000         - 625-5000
                           (17): 525 5994         - 525-5994
                           (18): 525 2398         - 525-2398
                           (19): 525 2400         - 525-2400
```

### Sink

```
  audio-system        : Audio system to use
                        flags: readable, writable
                        Enum "GstAjaAudioSystem" Default: 0, "Auto (based on selected channel)"
                           (0): Auto (based on selected channel) - auto
                           (1): Audio system 1   - 1
                           (2): Audio system 2   - 2
                           (3): Audio system 3   - 3
                           (4): Audio system 4   - 4
                           (5): Audio system 5   - 5
                           (6): Audio system 6   - 6
                           (7): Audio system 7   - 7
                           (8): Audio system 8   - 8
  channel             : Channel to use
                        flags: readable, writable
                        Unsigned Integer. Range: 0 - 7 Default: 0 
  device-identifier   : Input device instance to use
                        flags: readable, writable
                        String. Default: "0"
  output-cpu-core     : Sets the affinity of the output thread to this CPU core (-1=disabled)
                        flags: readable, writable
                        Unsigned Integer. Range: 0 - 4294967295 Default: 4294967295 
  output-destination  : Output destination to use
                        flags: readable, writable
                        Enum "GstAjaOutputDestination" Default: 0, "Auto (based on selected channel)"
                           (0): Auto (based on selected channel) - auto
                           (1): Analog Output    - analog
                           (2): SDI Output 1     - sdi-1
                           (3): SDI Output 2     - sdi-2
                           (4): SDI Output 3     - sdi-3
                           (5): SDI Output 4     - sdi-4
                           (6): SDI Output 5     - sdi-5
                           (7): SDI Output 6     - sdi-6
                           (8): SDI Output 7     - sdi-7
                           (9): SDI Output 8     - sdi-8
                           (10): HDMI Output      - hdmi
  queue-size          : Size of internal queue in number of video frames. Half of this is allocated as device buffers and equal to the latency.
                        flags: readable, writable
                        Unsigned Integer. Range: 1 - 2147483647 Default: 16 
  reference-source    : Reference source to use
                        flags: readable, writable
                        Enum "GstAjaReferenceSource" Default: 0, "Auto"
                           (0): Auto             - auto
                           (1): Freerun          - freerun
                           (2): External         - external
                           (3): SDI Input 1      - input-1
                           (4): SDI Input 2      - input-2
                           (5): SDI Input 3      - input-3
                           (6): SDI Input 4      - input-4
                           (7): SDI Input 5      - input-5
                           (8): SDI Input 6      - input-6
                           (9): SDI Input 7      - input-7
                           (10): SDI Input 8      - input-8
  timecode-index      : Timecode index to use
                        flags: readable, writable
                        Enum "GstAjaTimecodeIndex" Default: 0, "Embedded SDI ATC LTC"
                           (0): Embedded SDI VITC - vitc
                           (0): Embedded SDI ATC LTC - atc-ltc
                           (2): Analog LTC 1     - ltc-1
                           (3): Analog LTC 2     - ltc-2
```
