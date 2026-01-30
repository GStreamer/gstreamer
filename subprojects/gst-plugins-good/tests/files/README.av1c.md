# AV1 MP4 Test Files

This directory contains small AV1 MP4 files used by
`pipelines/av1c-qtmux-qtdemux.c`.

The files are intentionally tiny (black test pattern, low resolution) and
serve to validate qtmux/qtdemux handling of the `av1C` box contents.

## av1c_min.mp4

Created without `av1parse`, so `qtmux` sees no `codec_data` and writes a
minimal `av1C` (4‑byte header only).

```
gst-launch-1.0 -e \
  videotestsrc num-buffers=15 pattern=black ! \
  "video/x-raw,format=I420,width=160,height=90,framerate=15/1" ! \
  av1enc ! \
  qtmux ! filesink location=subprojects/gst-plugins-good/tests/files/av1c_min.mp4
```

## av1c_with_obu.mp4

Created with `av1parse`, so `codec_data` includes the Sequence Header OBU and
`qtmux` writes a richer `av1C` (header + OBU).

```
gst-launch-1.0 -e \
  videotestsrc num-buffers=15 pattern=black ! \
  "video/x-raw,format=I420,width=160,height=90,framerate=15/1" ! \
  av1enc ! av1parse ! \
  qtmux ! filesink location=subprojects/gst-plugins-good/tests/files/av1c_with_obu.mp4
```

## av1c_two_entries.mp4

Created with two concatenated AV1 segments (different resolution), so `qtmux`
creates two `stsd` entries.

```
gst-launch-1.0 -e \
  concat name=c ! videoconvert ! \
  av1enc cpu-used=8 lag-in-frames=0 threads=1 ! av1parse ! qtmux ! \
  filesink location=subprojects/gst-plugins-good/tests/files/av1c_two_entries.mp4 \
  videotestsrc num-buffers=10 pattern=solid-color foreground-color=0xff0000 ! \
    "video/x-raw,format=I420,width=160,height=90,framerate=30/1" ! queue ! c. \
  videotestsrc num-buffers=10 pattern=solid-color foreground-color=0x00ff00 ! \
    "video/x-raw,format=I420,width=320,height=180,framerate=30/1" ! queue ! c.
```
