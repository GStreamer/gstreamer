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
ffmpeg -y \
  -f lavfi -i "color=c=black:s=160x90:r=15:d=1" \
  -c:v libaom-av1 -crf 32 -b:v 0 -cpu-used 8 -row-mt 1 -tiles 1x1 \
  -pix_fmt yuv420p \
  -movflags +faststart \
  subprojects/gst-plugins-bad/tests/files/av1_only.mp4
```

Note: generating AV1 MP4s with GStreamer (`av1enc` + `qtmux`/`mp4mux`)
currently produces an `av1C` box without config OBUs (4-byte payload),
which AVFoundation refuses to decode. The ffmpeg/libaom command above
produces a full `av1C` (config OBUs included), which is required by
`avfassetsrc` on macOS.
