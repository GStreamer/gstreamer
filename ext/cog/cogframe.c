

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <cog/cog.h>
#include <cog/cogframe.h>
#include <cog/cogvirtframe.h>
#include "gstcogorc.h"
#include <gst/gst.h>

#include <stdlib.h>
#include <string.h>

/**
 * cog_frame_new:
 *
 * Creates a new CogFrame object.  The created frame is uninitialized
 * and has no data storage associated with it.  The caller must fill
 * in the required information.
 *
 * Returns: a new CogFrame object
 */
CogFrame *
cog_frame_new (void)
{
  CogFrame *frame;

  frame = g_malloc0 (sizeof (*frame));
  frame->refcount = 1;

  return frame;
}

/**
 * cog_frame_new_and_alloc:
 *
 * Creates a new CogFrame object with the requested size and format.
 *
 * Returns: a new CogFrame object
 */
CogFrame *
cog_frame_new_and_alloc (CogMemoryDomain * domain, CogFrameFormat format,
    int width, int height)
{
  return cog_frame_new_and_alloc_extended (domain, format, width, height, 0);
}

CogFrame *
cog_frame_new_and_alloc_extended (CogMemoryDomain * domain,
    CogFrameFormat format, int width, int height, int extension)
{
  CogFrame *frame = cog_frame_new ();
  int bytes_pp;
  int h_shift, v_shift;
  int chroma_width;
  int chroma_height;

  g_return_val_if_fail (width > 0, NULL);
  g_return_val_if_fail (height > 0, NULL);

  frame->format = format;
  frame->width = width;
  frame->height = height;
  frame->domain = domain;
  frame->extension = extension;

  if (COG_FRAME_IS_PACKED (format)) {
    g_return_val_if_fail (extension == 0, NULL);

    frame->components[0].format = format;
    frame->components[0].width = width;
    frame->components[0].height = height;
    if (format == COG_FRAME_FORMAT_AYUV) {
      frame->components[0].stride = width * 4;
    } else {
      frame->components[0].stride = ROUND_UP_POW2 (width, 1) * 2;
    }
    frame->components[0].length = frame->components[0].stride * height;

    frame->regions[0] = g_malloc (frame->components[0].length);

    frame->components[0].data = frame->regions[0];
    frame->components[0].v_shift = 0;
    frame->components[0].h_shift = 0;

    return frame;
  }

  switch (COG_FRAME_FORMAT_DEPTH (format)) {
    case COG_FRAME_FORMAT_DEPTH_U8:
      bytes_pp = 1;
      break;
    case COG_FRAME_FORMAT_DEPTH_S16:
      bytes_pp = 2;
      break;
    case COG_FRAME_FORMAT_DEPTH_S32:
      bytes_pp = 4;
      break;
    default:
      g_return_val_if_reached (NULL);
      bytes_pp = 0;
      break;
  }

  h_shift = COG_FRAME_FORMAT_H_SHIFT (format);
  v_shift = COG_FRAME_FORMAT_V_SHIFT (format);
  chroma_width = ROUND_UP_SHIFT (width, h_shift);
  chroma_height = ROUND_UP_SHIFT (height, v_shift);

  frame->components[0].format = format;
  frame->components[0].width = width;
  frame->components[0].height = height;
  frame->components[0].stride = ROUND_UP_4 ((width + extension * 2) * bytes_pp);
  frame->components[0].length =
      frame->components[0].stride * (frame->components[0].height +
      extension * 2);
  frame->components[0].v_shift = 0;
  frame->components[0].h_shift = 0;

  frame->components[1].format = format;
  frame->components[1].width = chroma_width;
  frame->components[1].height = chroma_height;
  frame->components[1].stride =
      ROUND_UP_4 ((chroma_width + extension * 2) * bytes_pp);
  frame->components[1].length =
      frame->components[1].stride * (frame->components[1].height +
      extension * 2);
  frame->components[1].v_shift = v_shift;
  frame->components[1].h_shift = h_shift;

  frame->components[2].format = format;
  frame->components[2].width = chroma_width;
  frame->components[2].height = chroma_height;
  frame->components[2].stride =
      ROUND_UP_4 ((chroma_width + extension * 2) * bytes_pp);
  frame->components[2].length =
      frame->components[2].stride * (frame->components[2].height +
      extension * 2);
  frame->components[2].v_shift = v_shift;
  frame->components[2].h_shift = h_shift;

  frame->regions[0] = g_malloc (frame->components[0].length +
      frame->components[1].length + frame->components[2].length);

  frame->components[0].data = COG_OFFSET (frame->regions[0],
      frame->components[0].stride * extension + bytes_pp * extension);
  frame->components[1].data = COG_OFFSET (frame->regions[0],
      frame->components[0].length +
      frame->components[1].stride * extension + bytes_pp * extension);
  frame->components[2].data = COG_OFFSET (frame->regions[0],
      frame->components[0].length + frame->components[1].length +
      frame->components[2].stride * extension + bytes_pp * extension);

  return frame;
}

/**
 * cog_frame_new_from_data_YUY2:
 *
 * Creates a new CogFrame object with the requested size using
 * the data pointed to by @data.  The data must be in YUY2 format.
 * The data must remain for the lifetime of the CogFrame object.
 * It is recommended to use cog_frame_set_free_callback() for
 * notification when the data is no longer needed.
 *
 * Returns: a new CogFrame object
 */
CogFrame *
cog_frame_new_from_data_YUY2 (void *data, int width, int height)
{
  CogFrame *frame = cog_frame_new ();

  frame->format = COG_FRAME_FORMAT_YUYV;

  frame->width = width;
  frame->height = height;

  frame->components[0].format = frame->format;
  frame->components[0].width = width;
  frame->components[0].height = height;
  frame->components[0].stride = ROUND_UP_POW2 (width, 1) * 2;
  frame->components[0].data = data;
  frame->components[0].length = frame->components[0].stride * height;
  frame->components[0].v_shift = 0;
  frame->components[0].h_shift = 0;

  return frame;
}

/**
 * cog_frame_new_from_data_YUY2:
 *
 * Creates a new CogFrame object with the requested size using
 * the data pointed to by @data.  The data must be in UYVY format.
 * The data must remain for the lifetime of the CogFrame object.
 * It is recommended to use cog_frame_set_free_callback() for
 * notification when the data is no longer needed.
 *
 * Returns: a new CogFrame object
 */
CogFrame *
cog_frame_new_from_data_UYVY (void *data, int width, int height)
{
  CogFrame *frame = cog_frame_new ();

  frame->format = COG_FRAME_FORMAT_UYVY;

  frame->width = width;
  frame->height = height;

  frame->components[0].format = frame->format;
  frame->components[0].width = width;
  frame->components[0].height = height;
  frame->components[0].stride = ROUND_UP_POW2 (width, 1) * 2;
  frame->components[0].data = data;
  frame->components[0].length = frame->components[0].stride * height;
  frame->components[0].v_shift = 0;
  frame->components[0].h_shift = 0;

  return frame;
}

/**
 * cog_frame_new_from_data_YUY2:
 *
 * Creates a new CogFrame object with the requested size using
 * the data pointed to by @data.  The data must be in UYVY format,
 * although the row stride is allowed to be different than what
 * would normally be calculated from @width.
 * The data must remain for the lifetime of the CogFrame object.
 * It is recommended to use cog_frame_set_free_callback() for
 * notification when the data is no longer needed.
 *
 * Returns: a new CogFrame object
 */
CogFrame *
cog_frame_new_from_data_UYVY_full (void *data, int width, int height,
    int stride)
{
  CogFrame *frame = cog_frame_new ();

  frame->format = COG_FRAME_FORMAT_UYVY;

  frame->width = width;
  frame->height = height;

  frame->components[0].width = width;
  frame->components[0].height = height;
  frame->components[0].stride = stride;
  frame->components[0].data = data;
  frame->components[0].length = frame->components[0].stride * height;
  frame->components[0].v_shift = 0;
  frame->components[0].h_shift = 0;

  return frame;
}

/**
 * cog_frame_new_from_data_AYUV:
 *
 * Creates a new CogFrame object with the requested size using
 * the data pointed to by @data.  The data must be in AYUV format.
 * The data must remain for the lifetime of the CogFrame object.
 * It is recommended to use cog_frame_set_free_callback() for
 * notification when the data is no longer needed.
 *
 * Returns: a new CogFrame object
 */
CogFrame *
cog_frame_new_from_data_AYUV (void *data, int width, int height)
{
  CogFrame *frame = cog_frame_new ();

  frame->format = COG_FRAME_FORMAT_AYUV;

  frame->width = width;
  frame->height = height;

  frame->components[0].format = frame->format;
  frame->components[0].width = width;
  frame->components[0].height = height;
  frame->components[0].stride = width * 4;
  frame->components[0].data = data;
  frame->components[0].length = frame->components[0].stride * height;
  frame->components[0].v_shift = 0;
  frame->components[0].h_shift = 0;

  return frame;
}

/**
 * cog_frame_new_from_data_v216:
 *
 * Creates a new CogFrame object with the requested size using
 * the data pointed to by @data.  The data must be in v216 format.
 * The data must remain for the lifetime of the CogFrame object.
 * It is recommended to use cog_frame_set_free_callback() for
 * notification when the data is no longer needed.
 *
 * Returns: a new CogFrame object
 */
CogFrame *
cog_frame_new_from_data_v216 (void *data, int width, int height)
{
  CogFrame *frame = cog_frame_new ();

  frame->format = COG_FRAME_FORMAT_v216;

  frame->width = width;
  frame->height = height;

  frame->components[0].format = frame->format;
  frame->components[0].width = width;
  frame->components[0].height = height;
  frame->components[0].stride = ROUND_UP_POW2 (width, 1) * 4;
  frame->components[0].data = data;
  frame->components[0].length = frame->components[0].stride * height;
  frame->components[0].v_shift = 0;
  frame->components[0].h_shift = 0;

  return frame;
}

/**
 * cog_frame_new_from_data_v210:
 *
 * Creates a new CogFrame object with the requested size using
 * the data pointed to by @data.  The data must be in v210 format.
 * The data must remain for the lifetime of the CogFrame object.
 * It is recommended to use cog_frame_set_free_callback() for
 * notification when the data is no longer needed.
 *
 * Returns: a new CogFrame object
 */
CogFrame *
cog_frame_new_from_data_v210 (void *data, int width, int height)
{
  CogFrame *frame = cog_frame_new ();

  frame->format = COG_FRAME_FORMAT_v210;

  frame->width = width;
  frame->height = height;

  frame->components[0].format = frame->format;
  frame->components[0].width = width;
  frame->components[0].height = height;
  frame->components[0].stride = ((width + 47) / 48) * 128;
  frame->components[0].data = data;
  frame->components[0].length = frame->components[0].stride * height;
  frame->components[0].v_shift = 0;
  frame->components[0].h_shift = 0;

  return frame;
}

/**
 * cog_frame_new_from_data_I420:
 *
 * Creates a new CogFrame object with the requested size using
 * the data pointed to by @data.  The data must be in I420 format.
 * The data must remain for the lifetime of the CogFrame object.
 * It is recommended to use cog_frame_set_free_callback() for
 * notification when the data is no longer needed.
 *
 * Returns: a new CogFrame object
 */
CogFrame *
cog_frame_new_from_data_I420 (void *data, int width, int height)
{
  CogFrame *frame = cog_frame_new ();

  frame->format = COG_FRAME_FORMAT_U8_420;

  frame->width = width;
  frame->height = height;

  frame->components[0].format = frame->format;
  frame->components[0].width = width;
  frame->components[0].height = height;
  frame->components[0].stride = ROUND_UP_POW2 (width, 2);
  frame->components[0].data = data;
  frame->components[0].length = frame->components[0].stride *
      ROUND_UP_POW2 (frame->components[0].height, 1);
  frame->components[0].v_shift = 0;
  frame->components[0].h_shift = 0;

  frame->components[1].format = frame->format;
  frame->components[1].width = ROUND_UP_SHIFT (width, 1);
  frame->components[1].height = ROUND_UP_SHIFT (height, 1);
  frame->components[1].stride = ROUND_UP_POW2 (frame->components[1].width, 2);
  frame->components[1].length =
      frame->components[1].stride * frame->components[1].height;
  frame->components[1].data =
      COG_OFFSET (frame->components[0].data, frame->components[0].length);
  frame->components[1].v_shift = 1;
  frame->components[1].h_shift = 1;

  frame->components[2].format = frame->format;
  frame->components[2].width = ROUND_UP_SHIFT (width, 1);
  frame->components[2].height = ROUND_UP_SHIFT (height, 1);
  frame->components[2].stride = ROUND_UP_POW2 (frame->components[2].width, 2);
  frame->components[2].length =
      frame->components[2].stride * frame->components[2].height;
  frame->components[2].data =
      COG_OFFSET (frame->components[1].data, frame->components[1].length);
  frame->components[2].v_shift = 1;
  frame->components[2].h_shift = 1;

  return frame;
}

/**
 * cog_frame_new_from_data_YV12:
 *
 * Creates a new CogFrame object with the requested size using
 * the data pointed to by @data.  The data must be in YV12 format.
 * The data must remain for the lifetime of the CogFrame object.
 * It is recommended to use cog_frame_set_free_callback() for
 * notification when the data is no longer needed.
 *
 * Returns: a new CogFrame object
 */
CogFrame *
cog_frame_new_from_data_YV12 (void *data, int width, int height)
{
  CogFrame *frame = cog_frame_new ();

  frame->format = COG_FRAME_FORMAT_U8_420;

  frame->width = width;
  frame->height = height;

  frame->components[0].format = frame->format;
  frame->components[0].width = width;
  frame->components[0].height = height;
  frame->components[0].stride = ROUND_UP_POW2 (width, 2);
  frame->components[0].data = data;
  frame->components[0].length = frame->components[0].stride *
      ROUND_UP_POW2 (frame->components[0].height, 1);
  frame->components[0].v_shift = 0;
  frame->components[0].h_shift = 0;

  frame->components[2].format = frame->format;
  frame->components[2].width = ROUND_UP_SHIFT (width, 1);
  frame->components[2].height = ROUND_UP_SHIFT (height, 1);
  frame->components[2].stride = ROUND_UP_POW2 (frame->components[2].width, 2);
  frame->components[2].length =
      frame->components[2].stride * frame->components[2].height;
  frame->components[2].data =
      COG_OFFSET (frame->components[0].data, frame->components[0].length);
  frame->components[2].v_shift = 1;
  frame->components[2].h_shift = 1;

  frame->components[1].format = frame->format;
  frame->components[1].width = ROUND_UP_SHIFT (width, 1);
  frame->components[1].height = ROUND_UP_SHIFT (height, 1);
  frame->components[1].stride = ROUND_UP_POW2 (frame->components[1].width, 2);
  frame->components[1].length =
      frame->components[1].stride * frame->components[1].height;
  frame->components[1].data =
      COG_OFFSET (frame->components[2].data, frame->components[2].length);
  frame->components[1].v_shift = 1;
  frame->components[1].h_shift = 1;

  return frame;
}

/**
 * cog_frame_new_from_data_Y42B:
 *
 * Creates a new CogFrame object with the requested size using
 * the data pointed to by @data.  The data must be in Y42B format.
 * The data must remain for the lifetime of the CogFrame object.
 * It is recommended to use cog_frame_set_free_callback() for
 * notification when the data is no longer needed.
 *
 * Returns: a new CogFrame object
 */
CogFrame *
cog_frame_new_from_data_Y42B (void *data, int width, int height)
{
  CogFrame *frame = cog_frame_new ();

  frame->format = COG_FRAME_FORMAT_U8_422;

  frame->width = width;
  frame->height = height;

  frame->components[0].format = frame->format;
  frame->components[0].width = width;
  frame->components[0].height = height;
  frame->components[0].stride = ROUND_UP_POW2 (width, 2);
  frame->components[0].data = data;
  frame->components[0].length = frame->components[0].stride *
      ROUND_UP_POW2 (frame->components[0].height, 1);
  frame->components[0].v_shift = 0;
  frame->components[0].h_shift = 0;

  frame->components[1].format = frame->format;
  frame->components[1].width = ROUND_UP_SHIFT (width, 1);
  frame->components[1].height = height;
  frame->components[1].stride = ROUND_UP_POW2 (frame->components[1].width, 2);
  frame->components[1].length =
      frame->components[1].stride * frame->components[1].height;
  frame->components[1].data =
      COG_OFFSET (frame->components[0].data, frame->components[0].length);
  frame->components[1].v_shift = 0;
  frame->components[1].h_shift = 1;

  frame->components[2].format = frame->format;
  frame->components[2].width = ROUND_UP_SHIFT (width, 1);
  frame->components[2].height = height;
  frame->components[2].stride = ROUND_UP_POW2 (frame->components[2].width, 2);
  frame->components[2].length =
      frame->components[2].stride * frame->components[2].height;
  frame->components[2].data =
      COG_OFFSET (frame->components[1].data, frame->components[1].length);
  frame->components[2].v_shift = 0;
  frame->components[2].h_shift = 1;

  return frame;
}

/**
 * cog_frame_new_from_data_Y444:
 *
 * Creates a new CogFrame object with the requested size using
 * the data pointed to by @data.  The data must be in Y444 format.
 * The data must remain for the lifetime of the CogFrame object.
 * It is recommended to use cog_frame_set_free_callback() for
 * notification when the data is no longer needed.
 *
 * Returns: a new CogFrame object
 */
CogFrame *
cog_frame_new_from_data_Y444 (void *data, int width, int height)
{
  CogFrame *frame = cog_frame_new ();

  frame->format = COG_FRAME_FORMAT_U8_444;

  frame->width = width;
  frame->height = height;

  frame->components[0].format = frame->format;
  frame->components[0].width = width;
  frame->components[0].height = height;
  frame->components[0].stride = ROUND_UP_POW2 (width, 4);
  frame->components[0].data = data;
  frame->components[0].length = frame->components[0].stride *
      ROUND_UP_POW2 (frame->components[0].height, 1);
  frame->components[0].v_shift = 0;
  frame->components[0].h_shift = 0;

  frame->components[1].format = frame->format;
  frame->components[1].width = width;
  frame->components[1].height = height;
  frame->components[1].stride = ROUND_UP_POW2 (width, 4);
  frame->components[1].length =
      frame->components[1].stride * frame->components[1].height;
  frame->components[1].data =
      COG_OFFSET (frame->components[0].data, frame->components[0].length);
  frame->components[1].v_shift = 0;
  frame->components[1].h_shift = 0;

  frame->components[2].format = frame->format;
  frame->components[2].width = width;
  frame->components[2].height = height;
  frame->components[2].stride = ROUND_UP_POW2 (width, 4);
  frame->components[2].length =
      frame->components[2].stride * frame->components[2].height;
  frame->components[2].data =
      COG_OFFSET (frame->components[1].data, frame->components[1].length);
  frame->components[2].v_shift = 0;
  frame->components[2].h_shift = 0;

  return frame;
}

/**
 * cog_frame_new_from_data_RGB:
 *
 * Creates a new CogFrame object with the requested size using
 * the data pointed to by @data.  The data must be in RGB format.
 * The data must remain for the lifetime of the CogFrame object.
 * It is recommended to use cog_frame_set_free_callback() for
 * notification when the data is no longer needed.
 *
 * Returns: a new CogFrame object
 */
CogFrame *
cog_frame_new_from_data_RGB (void *data, int width, int height)
{
  CogFrame *frame = cog_frame_new ();

  frame->format = COG_FRAME_FORMAT_RGB;

  frame->width = width;
  frame->height = height;

  frame->components[0].width = width;
  frame->components[0].height = height;
  frame->components[0].stride = ROUND_UP_4 (width * 3);
  frame->components[0].data = data;
  frame->components[0].length = frame->components[0].stride * height;
  frame->components[0].v_shift = 0;
  frame->components[0].h_shift = 0;

  return frame;
}

static CogFrame *
cog_frame_new_from_data_RGB32 (void *data, int width, int height, int format)
{
  CogFrame *frame = cog_frame_new ();

  frame->format = format;

  frame->width = width;
  frame->height = height;

  frame->components[0].width = width;
  frame->components[0].height = height;
  frame->components[0].stride = width * 4;
  frame->components[0].data = data;
  frame->components[0].length = frame->components[0].stride * height;
  frame->components[0].v_shift = 0;
  frame->components[0].h_shift = 0;

  return frame;
}

CogFrame *
cog_frame_new_from_data_RGBx (void *data, int width, int height)
{
  return cog_frame_new_from_data_RGB32 (data, width, height,
      COG_FRAME_FORMAT_RGBx);
}

CogFrame *
cog_frame_new_from_data_xRGB (void *data, int width, int height)
{
  return cog_frame_new_from_data_RGB32 (data, width, height,
      COG_FRAME_FORMAT_xRGB);
}

CogFrame *
cog_frame_new_from_data_BGRx (void *data, int width, int height)
{
  return cog_frame_new_from_data_RGB32 (data, width, height,
      COG_FRAME_FORMAT_BGRx);
}

CogFrame *
cog_frame_new_from_data_xBGR (void *data, int width, int height)
{
  return cog_frame_new_from_data_RGB32 (data, width, height,
      COG_FRAME_FORMAT_xBGR);
}

CogFrame *
cog_frame_new_from_data_RGBA (void *data, int width, int height)
{
  return cog_frame_new_from_data_RGB32 (data, width, height,
      COG_FRAME_FORMAT_RGBA);
}

CogFrame *
cog_frame_new_from_data_ARGB (void *data, int width, int height)
{
  return cog_frame_new_from_data_RGB32 (data, width, height,
      COG_FRAME_FORMAT_ARGB);
}

CogFrame *
cog_frame_new_from_data_BGRA (void *data, int width, int height)
{
  return cog_frame_new_from_data_RGB32 (data, width, height,
      COG_FRAME_FORMAT_BGRA);
}

CogFrame *
cog_frame_new_from_data_ABGR (void *data, int width, int height)
{
  return cog_frame_new_from_data_RGB32 (data, width, height,
      COG_FRAME_FORMAT_ABGR);
}

/**
 * cog_frame_dup:
 *
 * Creates a new CogFrame object with the same dimensions and format
 * as @frame, and copies the data from the @frame to the new object.
 *
 * Returns: a new CogFrame object
 */
CogFrame *
cog_frame_dup (CogFrame * frame)
{
  return cog_frame_dup_extended (frame, 0);
}

CogFrame *
cog_frame_dup_extended (CogFrame * frame, int extension)
{
  CogFrame *dup_frame;

  dup_frame = cog_frame_new_and_alloc_extended (frame->domain,
      frame->format, frame->width, frame->height, extension);
  cog_frame_convert (dup_frame, frame);

  return dup_frame;
}

/**
 * cog_frame_clone:
 *
 * Creates a new CogFrame object with the same dimensions and format
 * as @frame.  This function leaves the data in the new object
 * uninitialized.
 *
 * Returns: a new CogFrame object
 */
CogFrame *
cog_frame_clone (CogMemoryDomain * domain, CogFrame * frame)
{
  return cog_frame_new_and_alloc (domain,
      frame->format, frame->width, frame->height);
}

/**
 * cog_frame_ref:
 * @frame: a frame object
 *
 * Increases the reference count of @frame.
 *
 * Returns: the value of @frame
 */
CogFrame *
cog_frame_ref (CogFrame * frame)
{
  frame->refcount++;
  return frame;
}

/**
 * cog_frame_unref:
 * @frame: a frame object
 *
 * Decreases the reference count of @frame.  If the new reference
 * count is 0, the frame is freed.  If a frame free callback was
 * set, this function is called.
 *
 * Returns: the value of @frame
 */
void
cog_frame_unref (CogFrame * frame)
{
  int i;

  g_return_if_fail (frame->refcount > 0);

  frame->refcount--;
  if (frame->refcount == 0) {
    if (frame->free) {
      frame->free (frame, frame->priv);
    }
#ifdef HAVE_OPENGL
    if (COG_FRAME_IS_OPENGL (frame)) {
      cog_opengl_frame_cleanup (frame);
    }
#endif

    for (i = 0; i < 3; i++) {
      if (frame->regions[i]) {
        g_free (frame->regions[i]);
      }
    }

    if (frame->virt_frame1) {
      cog_frame_unref (frame->virt_frame1);
    }
    if (frame->virt_frame2) {
      cog_frame_unref (frame->virt_frame2);
    }
    if (frame->virt_priv) {
      g_free (frame->virt_priv);
    }

    g_free (frame);
  }
}

/**
 * cog_frame_set_free_callback:
 * @frame: a frame object
 * @free_func: the function to call when the frame is freed
 * @priv: callback key
 *
 * Sets a function that will be called when the object reference
 * count drops to zero and the object is freed.
 */
void
cog_frame_set_free_callback (CogFrame * frame,
    CogFrameFreeFunc free_func, void *priv)
{
  frame->free = free_func;
  frame->priv = priv;
}

/**
 * cog_frame_convert:
 * @dest: destination frame
 * @src: source frame
 *
 * Copies data from the source frame to the destination frame, converting
 * formats if necessary.  Only a few conversions are supported.
 */
void
cog_frame_convert (CogFrame * dest, CogFrame * src)
{
  CogFrame *frame;
  CogFrameFormat dest_format;

  g_return_if_fail (dest != NULL);
  g_return_if_fail (src != NULL);

  switch (dest->format) {
    case COG_FRAME_FORMAT_YUYV:
    case COG_FRAME_FORMAT_UYVY:
      dest_format = COG_FRAME_FORMAT_U8_422;
      break;
    case COG_FRAME_FORMAT_AYUV:
    case COG_FRAME_FORMAT_ARGB:
      dest_format = COG_FRAME_FORMAT_U8_444;
      break;
    default:
      dest_format = dest->format;
      break;
  }
  cog_frame_ref (src);

  frame = cog_virt_frame_new_unpack (src);
  GST_DEBUG ("unpack %p", frame);

  if (COG_FRAME_FORMAT_DEPTH (dest_format) !=
      COG_FRAME_FORMAT_DEPTH (frame->format)) {
    if (COG_FRAME_FORMAT_DEPTH (dest_format) == COG_FRAME_FORMAT_DEPTH_U8) {
      frame = cog_virt_frame_new_convert_u8 (frame);
      GST_DEBUG ("convert_u8 %p", frame);
    } else if (COG_FRAME_FORMAT_DEPTH (dest_format) ==
        COG_FRAME_FORMAT_DEPTH_S16) {
      frame = cog_virt_frame_new_convert_s16 (frame);
      GST_DEBUG ("convert_s16 %p", frame);
    }
  }

  if ((dest_format & 3) != (frame->format & 3)) {
    frame = cog_virt_frame_new_subsample (frame, dest_format,
        COG_CHROMA_SITE_MPEG2, 2);
    GST_DEBUG ("subsample %p", frame);
  }

  switch (dest->format) {
    case COG_FRAME_FORMAT_YUYV:
      frame = cog_virt_frame_new_pack_YUY2 (frame);
      GST_DEBUG ("pack_YUY2 %p", frame);
      break;
    case COG_FRAME_FORMAT_UYVY:
      frame = cog_virt_frame_new_pack_UYVY (frame);
      GST_DEBUG ("pack_UYVY %p", frame);
      break;
    case COG_FRAME_FORMAT_AYUV:
      frame = cog_virt_frame_new_pack_AYUV (frame);
      GST_DEBUG ("pack_AYUV %p", frame);
      break;
    default:
      break;
  }

  if (dest->width < frame->width || dest->height < frame->height) {
    GST_DEBUG ("crop %d %d to %d %d",
        frame->width, frame->height, dest->width, dest->height);

    frame = cog_virt_frame_new_crop (frame, dest->width, dest->height);
    GST_DEBUG ("crop %p", frame);
  }
  if (dest->width > src->width || dest->height > src->height) {
    frame = cog_virt_frame_new_edgeextend (frame, dest->width, dest->height);
    GST_DEBUG ("edgeextend %p", frame);
  }

  cog_virt_frame_render (frame, dest);
  cog_frame_unref (frame);

}


#if 0
void
cog_frame_md5 (CogFrame * frame, uint32_t * state)
{
  uint8_t *line;
  int x, y, k;

  state[0] = 0x67452301;
  state[1] = 0xefcdab89;
  state[2] = 0x98badcfe;
  state[3] = 0x10325476;

  x = 0;
  y = 0;
  k = 0;
  for (k = 0; k < 3; k++) {
    for (y = 0; y < frame->components[k].height; y++) {
      line = COG_FRAME_DATA_GET_LINE (&frame->components[k], y);
      for (x = 0; x + 63 < frame->components[k].width; x += 64) {
        oil_md5 (state, (uint32_t *) (line + x));
      }
      if (x < frame->components[k].width) {
        uint8_t tmp[64];
        int left;
        left = frame->components[k].width - x;
        memcpy (tmp, line + x, left);
        memset (tmp + left, 0, 64 - left);
        oil_md5 (state, (uint32_t *) tmp);
      }
    }
  }

  GST_DEBUG
      ("md5 %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
      state[0] & 0xff, (state[0] >> 8) & 0xff, (state[0] >> 16) & 0xff,
      (state[0] >> 24) & 0xff, state[1] & 0xff, (state[1] >> 8) & 0xff,
      (state[1] >> 16) & 0xff, (state[1] >> 24) & 0xff, state[2] & 0xff,
      (state[2] >> 8) & 0xff, (state[2] >> 16) & 0xff, (state[2] >> 24) & 0xff,
      state[3] & 0xff, (state[3] >> 8) & 0xff, (state[3] >> 16) & 0xff,
      (state[3] >> 24) & 0xff);
}
#endif

void
cog_frame_split_fields (CogFrame * dest1, CogFrame * dest2, CogFrame * src)
{
  CogFrame src_tmp;

  g_return_if_fail ((src->height & 1) == 0);

  memcpy (&src_tmp, src, sizeof (src_tmp));

  src_tmp.height = src->height / 2;
  src_tmp.components[0].stride *= 2;
  src_tmp.components[1].stride *= 2;
  src_tmp.components[2].stride *= 2;

  cog_frame_convert (dest1, &src_tmp);

  src_tmp.components[0].data = COG_FRAME_DATA_GET_LINE (&src->components[0], 1);
  src_tmp.components[1].data = COG_FRAME_DATA_GET_LINE (&src->components[1], 1);
  src_tmp.components[2].data = COG_FRAME_DATA_GET_LINE (&src->components[2], 1);

  cog_frame_convert (dest2, &src_tmp);
}

void
cog_frame_get_subdata (CogFrame * frame, CogFrameData * fd,
    int component, int x, int y)
{
  CogFrameData *comp = frame->components + component;

  g_return_if_fail (COG_FRAME_FORMAT_DEPTH (comp->format) ==
      COG_FRAME_FORMAT_DEPTH_U8);

  fd->format = comp->format;
  fd->data = COG_FRAME_DATA_GET_PIXEL_U8 (comp, x, y);
  fd->stride = comp->stride;
  fd->width = MAX (0, comp->width - x);
  fd->height = MAX (0, comp->height - y);
  fd->h_shift = comp->h_shift;
  fd->v_shift = comp->v_shift;
}
