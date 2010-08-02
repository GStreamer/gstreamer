#ifndef __GST_GAUSS_BLUR_H__
#define __GST_GAUSS_BLUR_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>

G_BEGIN_DECLS

#define GST_TYPE_GAUSS_BLUR (gauss_blur_get_type())
#define GAUSS_BLUR(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GAUSS_BLUR, GaussBlur))

typedef struct GaussBlur GaussBlur;
typedef struct GaussBlurClass GaussBlurClass;

struct GaussBlur
{
  GstVideoFilter videofilter;
  gint width, height, stride;

  float cur_sigma, sigma;
  int windowsize;

  float *kernel;
  float *kernel_sum;
  float *tempim;
  gint16 *smoothedim;
};

struct GaussBlurClass
{
  GstVideoFilterClass parent_class;
};

GType gauss_blur_get_type(void);

G_END_DECLS

#endif
