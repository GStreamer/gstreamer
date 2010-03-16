/* GStreamer
 *
 * gstv4ljpegsrc.c: V4L source element for JPEG cameras
 *
 * Copyright (C) 2004-2005 Jan Schmidt <thaytan@mad.scientist.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 e Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <sys/time.h>
#include "gstv4ljpegsrc.h"
#include "v4lsrc_calls.h"

GST_DEBUG_CATEGORY_STATIC (v4ljpegsrc_debug);
#define GST_CAT_DEFAULT v4ljpegsrc_debug

/* init functions */
static void gst_v4ljpegsrc_base_init (gpointer g_class);
static void gst_v4ljpegsrc_class_init (GstV4lJpegSrcClass * klass);
static void gst_v4ljpegsrc_init (GstV4lJpegSrc * v4ljpegsrc);

/* buffer functions */
static GstPadLinkReturn gst_v4ljpegsrc_src_link (GstPad * pad,
    const GstCaps * caps);
static GstCaps *gst_v4ljpegsrc_getcaps (GstPad * pad);
static GstData *gst_v4ljpegsrc_get (GstPad * pad);

static GstElementClass *parent_class = NULL;

GType
gst_v4ljpegsrc_get_type (void)
{
  static GType v4ljpegsrc_type = 0;

  if (!v4ljpegsrc_type) {
    static const GTypeInfo v4ljpegsrc_info = {
      sizeof (GstV4lJpegSrcClass),
      gst_v4ljpegsrc_base_init,
      NULL,
      (GClassInitFunc) gst_v4ljpegsrc_class_init,
      NULL,
      NULL,
      sizeof (GstV4lJpegSrc),
      0,
      (GInstanceInitFunc) gst_v4ljpegsrc_init,
      NULL
    };

    v4ljpegsrc_type =
        g_type_register_static (GST_TYPE_V4LSRC, "GstV4lJpegSrc",
        &v4ljpegsrc_info, 0);
    GST_DEBUG_CATEGORY_INIT (v4ljpegsrc_debug, "v4ljpegsrc", 0,
        "V4L JPEG source element");
  }
  return v4ljpegsrc_type;
}

static void
gst_v4ljpegsrc_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (gstelement_class,
      "Video (video4linux/raw) Jpeg Source", "Source/Video",
      "Reads jpeg frames from a video4linux (eg ov519) device",
      "Jan Schmidt <thaytan@mad.scientist.com>");
}

static void
gst_v4ljpegsrc_class_init (GstV4lJpegSrcClass * klass)
{
  parent_class = g_type_class_peek_parent (klass);
}

static void
gst_v4ljpegsrc_init (GstV4lJpegSrc * v4ljpegsrc)
{
  GstV4lSrc *v4lsrc = GST_V4LSRC (v4ljpegsrc);
  GstPad *pad = v4lsrc->srcpad;

  /*
   * Stash away and then replace the getcaps and get functions on the src pad
   */
  v4ljpegsrc->getfn = GST_RPAD_GETFUNC (pad);
  v4ljpegsrc->getcapsfn = GST_RPAD_GETCAPSFUNC (pad);

  gst_pad_set_get_function (v4lsrc->srcpad, gst_v4ljpegsrc_get);
  gst_pad_set_getcaps_function (v4lsrc->srcpad, gst_v4ljpegsrc_getcaps);
  gst_pad_set_link_function (v4lsrc->srcpad, gst_v4ljpegsrc_src_link);
}

static GstPadLinkReturn
gst_v4ljpegsrc_src_link (GstPad * pad, const GstCaps * vscapslist)
{
  GstV4lJpegSrc *v4ljpegsrc;
  GstV4lSrc *v4lsrc;
  gint w, h, palette = -1;
  const GValue *fps;
  GstStructure *structure;
  gboolean was_capturing;
  struct video_window *vwin;

  v4ljpegsrc = GST_V4LJPEGSRC (gst_pad_get_parent (pad));
  v4lsrc = GST_V4LSRC (v4ljpegsrc);
  vwin = &GST_V4LELEMENT (v4lsrc)->vwin;
  was_capturing = v4lsrc->is_capturing;

  /* in case the buffers are active (which means that we already
   * did capsnego before and didn't clean up), clean up anyways */
  if (GST_V4L_IS_ACTIVE (GST_V4LELEMENT (v4lsrc))) {
    if (was_capturing) {
      if (!gst_v4lsrc_capture_stop (v4lsrc))
        return GST_PAD_LINK_REFUSED;
    }
    if (!gst_v4lsrc_capture_deinit (v4lsrc))
      return GST_PAD_LINK_REFUSED;
  } else if (!GST_V4L_IS_OPEN (GST_V4LELEMENT (v4lsrc))) {
    return GST_PAD_LINK_DELAYED;
  }

  structure = gst_caps_get_structure (vscapslist, 0);

  gst_structure_get_int (structure, "width", &w);
  gst_structure_get_int (structure, "height", &h);
  fps = gst_structure_get_value (structure, "framerate");

  GST_DEBUG_OBJECT (v4ljpegsrc, "linking with %dx%d at %d/%d fps", w, h,
      gst_value_get_fraction_numerator (fps),
      gst_value_get_fraction_denominator (fps));

  /* set framerate if it's not already correct */
  if (fps != gst_v4lsrc_get_fps (v4lsrc)) {
    int fps_index = fps / 15.0 * 16;

    GST_DEBUG_OBJECT (v4ljpegsrc, "Trying to set fps index %d", fps_index);
    /* set bits 16 to 21 to 0 */
    vwin->flags &= (0x3F00 - 1);
    /* set bits 16 to 21 to the index */
    vwin->flags |= fps_index << 16;
    if (!gst_v4l_set_window_properties (GST_V4LELEMENT (v4lsrc))) {
      return GST_PAD_LINK_DELAYED;
    }
  }

  /*
   * Try to set the camera to capture RGB24 
   */
  palette = VIDEO_PALETTE_RGB24;
  v4lsrc->buffer_size = w * h * 3;

  GST_DEBUG_OBJECT (v4ljpegsrc, "trying to set_capture %dx%d, palette %d",
      w, h, palette);
  /* this only fills in v4lsrc->mmap values */
  if (!gst_v4lsrc_set_capture (v4lsrc, w, h, palette)) {
    GST_WARNING_OBJECT (v4ljpegsrc, "could not set_capture %dx%d, palette %d",
        w, h, palette);
    return GST_PAD_LINK_REFUSED;
  }

  /* first try the negotiated settings using try_capture */
  if (!gst_v4lsrc_try_capture (v4lsrc, w, h, palette)) {
    GST_DEBUG_OBJECT (v4ljpegsrc, "failed trying palette %d for %dx%d", palette,
        w, h);
    return GST_PAD_LINK_REFUSED;
  }

  if (!gst_v4lsrc_capture_init (v4lsrc))
    return GST_PAD_LINK_REFUSED;

  if (was_capturing || GST_STATE (v4lsrc) == GST_STATE_PLAYING) {
    if (!gst_v4lsrc_capture_start (v4lsrc))
      return GST_PAD_LINK_REFUSED;
  }

  return GST_PAD_LINK_OK;
}

static GstCaps *
gst_v4ljpegsrc_getcaps (GstPad * pad)
{
  GstCaps *list;
  GstV4lJpegSrc *v4ljpegsrc = GST_V4LJPEGSRC (gst_pad_get_parent (pad));
  GstV4lSrc *v4lsrc = GST_V4LSRC (v4ljpegsrc);
  struct video_capability *vcap = &GST_V4LELEMENT (v4lsrc)->vcap;
  gfloat fps = 0.0;

  if (!GST_V4L_IS_OPEN (GST_V4LELEMENT (v4lsrc))) {
    return gst_caps_new_any ();
  }
  if (!v4lsrc->autoprobe) {
    /* FIXME: query current caps and return those, with _any appended */
    return gst_caps_new_any ();
  }

  list = gst_caps_new_simple ("image/jpeg", NULL);
  GST_DEBUG_OBJECT (v4ljpegsrc,
      "Device reports w: %d-%d, h: %d-%d, fps: %f",
      vcap->minwidth, vcap->maxwidth, vcap->minheight, vcap->maxheight, fps);

  if (vcap->minwidth < vcap->maxwidth) {
    gst_caps_set_simple (list, "width", GST_TYPE_INT_RANGE, vcap->minwidth,
        vcap->maxwidth, NULL);
  } else {
    gst_caps_set_simple (list, "width", G_TYPE_INT, vcap->minwidth, NULL);
  }
  if (vcap->minheight < vcap->maxheight) {
    gst_caps_set_simple (list, "height", GST_TYPE_INT_RANGE, vcap->minheight,
        vcap->maxheight, NULL);
  } else {
    gst_caps_set_simple (list, "height", G_TYPE_INT, vcap->minheight, NULL);
  }

  if (v4lsrc->fps_list) {
    GstStructure *structure = gst_caps_get_structure (list, 0);

    gst_structure_set_value (structure, "framerate", v4lsrc->fps_list);
  }
  GST_DEBUG_OBJECT (v4ljpegsrc, "caps: %" GST_PTR_FORMAT, list);

  return list;
}

static GstData *
gst_v4ljpegsrc_get (GstPad * pad)
{
  GstV4lJpegSrc *v4ljpegsrc;
  GstV4lSrc *v4lsrc;
  GstData *data;
  GstBuffer *buf;
  GstBuffer *outbuf;
  int jpeg_size;

  g_return_val_if_fail (pad != NULL, NULL);
  v4ljpegsrc = GST_V4LJPEGSRC (gst_pad_get_parent (pad));
  v4lsrc = GST_V4LSRC (v4ljpegsrc);

  /* Fetch from the v4lsrc class get fn.  */
  data = v4ljpegsrc->getfn (pad);

  /* If not a buffer, return it unchanged */
  if (!data || (!GST_IS_BUFFER (data)))
    return data;

  buf = GST_BUFFER (data);

  /* Confirm that the buffer contains jpeg data */

  /* 
   * Create a new subbuffer from the jpeg data 
   * The first 2 bytes in the buffer are the size of the jpeg data
   */
  if (GST_BUFFER_SIZE (buf) > 2) {
    jpeg_size = (int) (GST_READ_UINT16_LE (GST_BUFFER_DATA (buf))) * 8;
  } else
    jpeg_size = 0;

  /* Check that the size is sensible */
  if ((jpeg_size <= 0) || (jpeg_size > GST_BUFFER_SIZE (buf) - 2)) {
    GST_ELEMENT_ERROR (v4ljpegsrc, STREAM, FORMAT, (NULL),
        ("Invalid non-jpeg frame from camera"));
    return NULL;
  }

  GST_DEBUG_OBJECT (v4ljpegsrc, "Creating JPEG subbuffer of size %d",
      jpeg_size);
  outbuf = gst_buffer_create_sub (buf, 2, jpeg_size);

  /* Copy timestamps onto the subbuffer */
  gst_buffer_stamp (outbuf, buf);

  /* Release the main buffer */
  gst_buffer_unref (buf);

  return GST_DATA (outbuf);
}
