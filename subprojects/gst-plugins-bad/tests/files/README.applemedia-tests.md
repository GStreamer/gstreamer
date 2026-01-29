# AppleMedia test media generation

This directory contains small MP4 assets used by AppleMedia pipeline tests.

## vp9_only.mp4

Generate a tiny VP9-only MP4 (profile 0, 8-bit 4:2:0):

```sh
gst-launch-1.0 -e \
  videotestsrc num-buffers=15 pattern=black ! \
    "video/x-raw,width=160,height=90,framerate=15/1,format=I420" ! \
    vp9enc deadline=1 cpu-used=8 threads=1 ! \
    "video/x-vp9,profile=(string)0,bit-depth-luma=(uint)8,bit-depth-chroma=(uint)8,chroma-format=(string)4:2:0,colorimetry=(string)bt601" ! \
    qtmux ! filesink location=subprojects/gst-plugins-bad/tests/files/vp9_only.mp4
```

## av1_only.mp4

Generate a tiny AV1-only MP4 (black frames, 1s):

```sh
gst-launch-1.0 -e \
  videotestsrc num-buffers=15 pattern=black ! \
    "video/x-raw,format=I420,width=160,height=90,framerate=15/1" ! \
    av1enc ! av1parse ! \
    qtmux ! filesink location=subprojects/gst-plugins-bad/tests/files/av1_only.mp4
```

## vp8_aac.mp4

Generate a small VP8+AAC MP4 (black frames, 1s):

```sh
gst-launch-1.0 -e \
  qtmux name=mux ! filesink location=subprojects/gst-plugins-bad/tests/files/vp8_aac.mp4 \
  videotestsrc num-buffers=15 pattern=black ! \
    "video/x-raw,width=160,height=90,framerate=15/1,format=I420" ! \
    vp8enc deadline=1 cpu-used=8 threads=1 ! queue ! mux. \
  audiotestsrc num-buffers=15 wave=silence ! \
    "audio/x-raw,rate=44100,channels=2" ! \
    voaacenc ! queue ! mux.
```

## h264_opus.mp4

Generate a small H.264+Opus MP4 (black frames, 1s):

```sh
gst-launch-1.0 -e \
  qtmux name=mux ! filesink location=subprojects/gst-plugins-bad/tests/files/h264_opus.mp4 \
  videotestsrc num-buffers=15 pattern=black ! \
    "video/x-raw,width=160,height=90,framerate=15/1,format=I420" ! \
    x264enc speed-preset=ultrafast tune=zerolatency key-int-max=15 ! \
    h264parse ! queue ! mux. \
  audiotestsrc num-buffers=15 wave=silence ! \
    "audio/x-raw,rate=48000,channels=2" ! \
    opusenc ! queue ! mux.
```

## h264_aac.mp4

Generate a small H.264+AAC MP4 (black frames, 1s):

```sh
gst-launch-1.0 -e \
  qtmux name=mux ! filesink location=subprojects/gst-plugins-bad/tests/files/h264_aac.mp4 \
  videotestsrc num-buffers=15 pattern=black ! \
    "video/x-raw,width=160,height=90,framerate=15/1,format=I420" ! \
    x264enc speed-preset=ultrafast tune=zerolatency key-int-max=15 ! \
    h264parse ! queue ! mux. \
  audiotestsrc num-buffers=15 wave=silence ! \
    "audio/x-raw,rate=44100,channels=2" ! \
    voaacenc ! queue ! mux.
```
