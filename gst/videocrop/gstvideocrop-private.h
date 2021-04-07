#ifndef __GST_VIDEO_CROP_PRIVATE_H__
#define __GST_VIDEO_CROP_PRIVATE_H__

/* aspectvideocrop and videocrop support the same pixel formats, since
 * aspectvideocrop uses videocrop internally.
 * The definitions of supported pixe formats can thus be shared
 * between both, avoiding the need of manual synchronization
 */

#define VIDEO_CROP_CAPS                                \
  GST_VIDEO_CAPS_MAKE ("{ RGBx, xRGB, BGRx, xBGR, "    \
	  "RGBA, ARGB, BGRA, ABGR, RGB, BGR, AYUV, YUY2, Y444, " \
	  "Y42B, Y41B, YVYU, UYVY, I420, YV12, RGB16, RGB15, "  \
	  "GRAY8, NV12, NV21, GRAY16_LE, GRAY16_BE }") "; "     \
  "video/x-raw(ANY), "                                      \
	  "width = " GST_VIDEO_SIZE_RANGE ", "                  \
	  "height = " GST_VIDEO_SIZE_RANGE ", "                 \
	  "framerate = " GST_VIDEO_FPS_RANGE

#endif /* __GST_VIDEO_CROP_PRIVATE_H__ */
