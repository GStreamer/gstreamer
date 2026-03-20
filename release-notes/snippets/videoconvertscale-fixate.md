## vapostproc, videoscale: Improve format selection

When multiple formats and resolutions are possible, `videoconvertscale` and `vapostproc`
now picks the resolution closests to the input resolution, instead of picking the first one.

For example this pipeline used to downscale but is now passthrough:
```
gst-launch-1.0 -v videotestsrc ! video/x-raw,width=1920,height=1080 ! videoconvertscale ! "video/x-raw,width=1280,height=720;video/x-raw,width=1920,height=1080" ! fakesink
```
