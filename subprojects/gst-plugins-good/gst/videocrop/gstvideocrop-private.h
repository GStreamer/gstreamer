#ifndef __GST_VIDEO_CROP_PRIVATE_H__
#define __GST_VIDEO_CROP_PRIVATE_H__

/* aspectvideocrop and videocrop support the same pixel formats, since
 * aspectvideocrop uses videocrop internally.
 * The definitions of supported pixe formats can thus be shared
 * between both, avoiding the need of manual synchronization
 */

#define VIDEO_CROP_FORMATS_PACKED_SIMPLE "RGB, BGR, RGB16, RGB15, " \
  "RGBx, xRGB, BGRx, xBGR, RGBA, ARGB, BGRA, ABGR, " \
  "GRAY8, GRAY16_LE, GRAY16_BE, AYUV"
#define VIDEO_CROP_FORMATS_PACKED_COMPLEX "YVYU, YUY2, UYVY, v210"
#define VIDEO_CROP_FORMATS_PLANAR "I420, A420, YV12, Y444, Y42B, Y41B, " \
  "I420_10BE, A420_10BE, Y444_10BE, A444_10BE, I422_10BE, A422_10BE, " \
  "I420_10LE, A420_10LE, Y444_10LE, A444_10LE, I422_10LE, A422_10LE, " \
  "I420_12BE, Y444_12BE, I422_12BE, " \
  "I420_12LE, Y444_12LE, I422_12LE, " \
  "GBR, GBR_10BE, GBR_10LE, GBR_12BE, GBR_12LE, " \
  "GBRA, GBRA_10BE, GBRA_10LE, GBRA_12BE, GBRA_12LE"
#define VIDEO_CROP_FORMATS_SEMI_PLANAR "NV12, NV21"

/* aspectratiocrop uses videocrop. sync caps changes between both */
#define VIDEO_CROP_CAPS                                \
  GST_VIDEO_CAPS_MAKE ("{" \
	VIDEO_CROP_FORMATS_PACKED_SIMPLE "," \
	VIDEO_CROP_FORMATS_PACKED_COMPLEX "," \
	VIDEO_CROP_FORMATS_PLANAR "," \
	VIDEO_CROP_FORMATS_SEMI_PLANAR "}") "; " \
  "video/x-raw(ANY), " \
         "width = " GST_VIDEO_SIZE_RANGE ", " \
         "height = " GST_VIDEO_SIZE_RANGE ", " \
         "framerate = " GST_VIDEO_FPS_RANGE

#endif /* __GST_VIDEO_CROP_PRIVATE_H__ */
