#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <math.h>
#include <gst/gst.h>
#include <gst/controller/gstcontroller.h>

#include "gstplugin.h"
#include "gstgaussblur.h"

static gboolean gauss_blur_stop (GstBaseTransform * btrans);
static gboolean gauss_blur_set_caps (GstBaseTransform * btrans,
    GstCaps * incaps, GstCaps * outcaps);
static GstFlowReturn gauss_blur_process_frame (GstBaseTransform * btrans,
    GstBuffer * in_buf, GstBuffer * out_buf);

static void gauss_blur_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gauss_blur_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

GST_DEBUG_CATEGORY_STATIC (gst_gauss_blur_debug);
#define GST_CAT_DEFAULT gst_gauss_blur_debug

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define CAPS_STR_RGB GST_VIDEO_CAPS_BGRx ";" GST_VIDEO_CAPS_RGBx
#else
#define CAPS_STR_RGB GST_VIDEO_CAPS_xRGB ";" GST_VIDEO_CAPS_xBGR
#endif

#define CAPS_STR GST_VIDEO_CAPS_YUV("AYUV")

/* The capabilities of the inputs and outputs. */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CAPS_STR)
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CAPS_STR)
    );

enum
{
  PROP_0,
  PROP_SIGMA,
  PROP_LAST
};

static void cleanup (GaussBlur * gb);
static gboolean make_gaussian_kernel (GaussBlur * gb, float sigma);
static void gaussian_smooth (GaussBlur * gb, guint8 * image,
    guint8 * out_image);

GST_BOILERPLATE (GaussBlur, gauss_blur, GstVideoFilter, GST_TYPE_VIDEO_FILTER);

#define DEFAULT_SIGMA 1.2

static void
gauss_blur_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple (element_class,
      "GaussBlur",
      "Filter/Effect/Video",
      "Perform Gaussian blur/sharpen on a video",
      "Jan Schmidt <thaytan@noraisin.net>");

  gst_element_class_add_static_pad_template (element_class, &src_factory);
  gst_element_class_add_static_pad_template (element_class, &sink_factory);
}

static void
gauss_blur_class_init (GaussBlurClass * klass)
{
  GObjectClass *object_class = (GObjectClass *) klass;
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) klass;

  object_class->set_property = gauss_blur_set_property;
  object_class->get_property = gauss_blur_get_property;

  trans_class->stop = gauss_blur_stop;
  trans_class->set_caps = gauss_blur_set_caps;
  trans_class->transform = gauss_blur_process_frame;

  g_object_class_install_property (object_class, PROP_SIGMA,
      g_param_spec_double ("sigma", "Sigma",
          "Sigma value for gaussian blur (negative for sharpen)",
          -20.0, 20.0, DEFAULT_SIGMA,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
}

static void
gauss_blur_init (GaussBlur * gb, GaussBlurClass * gclass)
{
  gb->sigma = DEFAULT_SIGMA;
  gb->cur_sigma = -1.0;
}

static void
cleanup (GaussBlur * gb)
{
  g_free (gb->tempim);
  gb->tempim = NULL;

  g_free (gb->smoothedim);
  gb->smoothedim = NULL;

  g_free (gb->kernel);
  gb->kernel = NULL;
  g_free (gb->kernel_sum);
  gb->kernel_sum = NULL;
}

static gboolean
gauss_blur_stop (GstBaseTransform * btrans)
{
  GaussBlur *gb = GAUSS_BLUR (btrans);

  cleanup (gb);

  return TRUE;
}

static gboolean
gauss_blur_set_caps (GstBaseTransform * btrans,
    GstCaps * incaps, GstCaps * outcaps)
{
  GaussBlur *gb = GAUSS_BLUR (btrans);
  GstStructure *structure;
  GstVideoFormat format;
  guint32 n_elems;

  structure = gst_caps_get_structure (incaps, 0);
  g_return_val_if_fail (structure != NULL, FALSE);

  if (!gst_video_format_parse_caps (incaps, &format, &gb->width, &gb->height))
    return FALSE;

  /* get stride */
  gb->stride = gst_video_format_get_row_stride (format, 0, gb->width);

  n_elems = gb->stride * gb->height;

  gb->tempim = g_malloc (sizeof (gfloat) * n_elems);
  //gb->smoothedim = g_malloc (sizeof (guint16) * n_elems);

  return TRUE;
}

static GstFlowReturn
gauss_blur_process_frame (GstBaseTransform * btrans,
    GstBuffer * in_buf, GstBuffer * out_buf)
{
  GaussBlur *gb = GAUSS_BLUR (btrans);
  GstClockTime timestamp;
  gint64 stream_time;
  gfloat sigma;

  /* GstController: update the properties */
  timestamp = GST_BUFFER_TIMESTAMP (in_buf);
  stream_time =
      gst_segment_to_stream_time (&btrans->segment, GST_FORMAT_TIME, timestamp);
  if (GST_CLOCK_TIME_IS_VALID (stream_time))
    gst_object_sync_values (G_OBJECT (gb), stream_time);

  GST_OBJECT_LOCK (gb);
  sigma = gb->sigma;
  GST_OBJECT_UNLOCK (gb);

  if (gb->cur_sigma != sigma) {
    g_free (gb->kernel);
    gb->kernel = NULL;
    g_free (gb->kernel_sum);
    gb->kernel_sum = NULL;
    gb->cur_sigma = sigma;
  }
  if (gb->kernel == NULL && !make_gaussian_kernel (gb, gb->cur_sigma)) {
    GST_ELEMENT_ERROR (btrans, RESOURCE, NO_SPACE_LEFT, ("Out of memory"),
        ("Failed to allocation gaussian kernel"));
    return GST_FLOW_ERROR;
  }

  /*
   * Perform gaussian smoothing on the image using the input standard
   * deviation.
   */
  memcpy (GST_BUFFER_DATA (out_buf), GST_BUFFER_DATA (in_buf),
      gb->height * gb->stride);
  gaussian_smooth (gb, GST_BUFFER_DATA (in_buf), GST_BUFFER_DATA (out_buf));

  return GST_FLOW_OK;
}

static void
blur_row_x (GaussBlur * gb, guint8 * in_row, gfloat * out_row)
{
  int c, cc, center;
  float dot[4], sum;
  int k, kmin, kmax;

  center = gb->windowsize / 2;

  for (c = 0; c < gb->width; c++) {
    /* Calculate min */
    cc = center - c;
    kmin = MAX (0, cc);
    cc = kmin - cc;
    /* Calc max */
    kmax = MIN (gb->windowsize, gb->width - cc);
    cc *= 4;

    dot[0] = dot[1] = dot[2] = dot[3] = 0.0;
    /* Calculate sum for range */
    sum = gb->kernel_sum[kmax - 1];
    sum -= kmin ? gb->kernel_sum[kmin - 1] : 0.0;

    for (k = kmin; k < kmax; k++) {
      float coeff = gb->kernel[k];
      dot[0] += (float) in_row[cc++] * coeff;
      dot[1] += (float) in_row[cc++] * coeff;
      dot[2] += (float) in_row[cc++] * coeff;
      dot[3] += (float) in_row[cc++] * coeff;
    }

    out_row[c * 4] = dot[0] / sum;
    out_row[c * 4 + 1] = dot[1] / sum;
    out_row[c * 4 + 2] = dot[2] / sum;
    out_row[c * 4 + 3] = dot[3] / sum;
  }
}

static void
gaussian_smooth (GaussBlur * gb, guint8 * image, guint8 * out_image)
{
  int r, c, rr, center;
  float dot[4], sum;
  int k, kmin, kmax;
  guint8 *in_row = image;
  float *tmp_out_row = gb->tempim;
  float *tmp_in_pos;
  gint y_avail = 0;
  guint8 *out_row;

  /* Apply the gaussian kernel */
  center = gb->windowsize / 2;

  /* Blur in the y - direction. */
  for (r = 0; r < gb->height; r++) {
    /* Calculate input row range */
    rr = center - r;
    kmin = MAX (0, rr);
    rr = kmin - rr;
    /* Calc max */
    kmax = MIN (gb->windowsize, gb->height - rr);

    /* Precalculate sum for range */
    sum = gb->kernel_sum[kmax - 1];
    sum -= kmin ? gb->kernel_sum[kmin - 1] : 0.0;

    /* Blur more input rows (x direction blur) */
    while (y_avail <= (r + center) && y_avail < gb->height) {
      blur_row_x (gb, in_row, tmp_out_row);
      in_row += gb->stride;
      tmp_out_row += gb->stride;
      y_avail++;
    }

    tmp_in_pos = gb->tempim + (rr * gb->stride);
    out_row = out_image + r * gb->stride;

    for (c = 0; c < gb->width; c++) {
      float *tmp = tmp_in_pos;

      dot[0] = dot[1] = dot[2] = dot[3] = 0.0;
      for (k = kmin; k < kmax; k++, tmp += gb->stride) {
        float kern = gb->kernel[k];
        dot[0] += tmp[0] * kern;
        dot[1] += tmp[1] * kern;
        dot[2] += tmp[2] * kern;
        dot[3] += tmp[3] * kern;
      }

      *out_row++ = (guint8) CLAMP ((dot[0] / sum + 0.5), 0, 255);
      *out_row++ = (guint8) CLAMP ((dot[1] / sum + 0.5), 0, 255);
      *out_row++ = (guint8) CLAMP ((dot[2] / sum + 0.5), 0, 255);
      *out_row++ = (guint8) CLAMP ((dot[3] / sum + 0.5), 0, 255);

      tmp_in_pos += 4;
    }
  }
}

/*
 * Create a one dimensional gaussian kernel.
 */
static gboolean
make_gaussian_kernel (GaussBlur * gb, float sigma)
{
  int i, center, left, right;
  float sum, sum2;
  const float fe = -0.5 / (sigma * sigma);
  const float dx = 1.0 / (sigma * sqrt (2 * G_PI));

  center = ceil (2.5 * fabs (sigma));
  gb->windowsize = (int) (1 + 2 * center);

  gb->kernel = g_new (float, gb->windowsize);
  gb->kernel_sum = g_new (float, gb->windowsize);
  if (gb->kernel == NULL || gb->kernel_sum == NULL)
    return FALSE;

  if (gb->windowsize == 1) {
    gb->kernel[0] = 1.0;
    gb->kernel_sum[0] = 1.0;
    return TRUE;
  }

  /* Center co-efficient */
  sum = gb->kernel[center] = dx;

  /* Other coefficients */
  left = center - 1;
  right = center + 1;
  for (i = 1; i <= center; i++, left--, right++) {
    float fx = dx * pow (G_E, fe * i * i);
    gb->kernel[right] = gb->kernel[left] = fx;
    sum += 2 * fx;
  }

  if (sigma < 0) {
    sum = -sum;
    gb->kernel[center] += 2.0 * sum;
  }

  for (i = 0; i < gb->windowsize; i++)
    gb->kernel[i] /= sum;

  sum2 = 0.0;
  for (i = 0; i < gb->windowsize; i++) {
    sum2 += gb->kernel[i];
    gb->kernel_sum[i] = sum2;
  }

#if 0
  g_print ("Sigma %f: ", sigma);
  for (i = 0; i < gb->windowsize; i++)
    g_print ("%f ", gb->kernel[i]);
  g_print ("\n");
  g_print ("sums: ");
  for (i = 0; i < gb->windowsize; i++)
    g_print ("%f ", gb->kernel_sum[i]);
  g_print ("\n");
  g_print ("sum %f sum2 %f\n", sum, sum2);
#endif

  return TRUE;
}

static void
gauss_blur_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GaussBlur *gb = GAUSS_BLUR (object);
  switch (prop_id) {
    case PROP_SIGMA:
      GST_OBJECT_LOCK (object);
      gb->sigma = g_value_get_double (value);
      GST_OBJECT_UNLOCK (object);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gauss_blur_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GaussBlur *gb = GAUSS_BLUR (object);
  switch (prop_id) {
    case PROP_SIGMA:
      GST_OBJECT_LOCK (gb);
      g_value_set_double (value, gb->sigma);
      GST_OBJECT_UNLOCK (gb);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* Register the element factories and other features. */
gboolean
gst_gauss_blur_plugin_init (GstPlugin * plugin)
{
  /* debug category for fltering log messages */
  GST_DEBUG_CATEGORY_INIT (gst_gauss_blur_debug, "gaussianblur",
      0, "Gaussian Blur video effect");

  return gst_element_register (plugin, "gaussianblur", GST_RANK_NONE,
      GST_TYPE_GAUSS_BLUR);
}
