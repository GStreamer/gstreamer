/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <linux/cdrom.h>

#include <vcdsrc.h>

/* VCDSrc signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_LOCATION,
  ARG_DEVICE,
  ARG_TRACK,
  ARG_BYTESPERREAD,
  ARG_OFFSET,
  ARG_MAX_ERRORS
};

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_vcdsrc_debug);
#define GST_CAT_DEFAULT gst_vcdsrc_debug

static void gst_vcdsrc_base_init (gpointer g_class);
static void gst_vcdsrc_class_init (GstVCDSrcClass * klass);
static void gst_vcdsrc_init (GstVCDSrc * vcdsrc);
static void gst_vcdsrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_vcdsrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static const GstEventMask *gst_vcdsrc_get_event_mask (GstPad * pad);
static const GstQueryType *gst_vcdsrc_get_query_types (GstPad * pad);
static const GstFormat *gst_vcdsrc_get_formats (GstPad * pad);
static gboolean gst_vcdsrc_srcpad_event (GstPad * pad, GstEvent * event);
static gboolean gst_vcdsrc_srcpad_query (GstPad * pad, GstQueryType type,
    GstFormat * format, gint64 * value);

static GstData *gst_vcdsrc_get (GstPad * pad);
static GstStateChangeReturn gst_vcdsrc_change_state (GstElement * element,
    GstStateChange transition);
static inline guint64 gst_vcdsrc_msf (GstVCDSrc * vcdsrc, gint track);
static void gst_vcdsrc_recalculate (GstVCDSrc * vcdsrc);

static void gst_vcdsrc_uri_handler_init (gpointer g_iface, gpointer iface_data);

static GstElementClass *parent_class = NULL;

/*static guint vcdsrc_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_vcdsrc_get_type (void)
{
  static GType gst_vcdsrc_type = 0;

  if (!gst_vcdsrc_type) {
    static const GTypeInfo gst_vcdsrc_info = {
      sizeof (GstVCDSrcClass),
      gst_vcdsrc_base_init,
      NULL,
      (GClassInitFunc) gst_vcdsrc_class_init,
      NULL,
      NULL,
      sizeof (GstVCDSrc),
      0,
      (GInstanceInitFunc) gst_vcdsrc_init,
    };
    static const GInterfaceInfo urihandler_info = {
      gst_vcdsrc_uri_handler_init,
      NULL,
      NULL
    };

    gst_vcdsrc_type =
        g_type_register_static (GST_TYPE_ELEMENT,
        "GstVCDSrc", &gst_vcdsrc_info, 0);
    g_type_add_interface_static (gst_vcdsrc_type,
        GST_TYPE_URI_HANDLER, &urihandler_info);
  }

  return gst_vcdsrc_type;
}

static void
gst_vcdsrc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  static GstElementDetails gst_vcdsrc_details =
      GST_ELEMENT_DETAILS ("VCD Source",
      "Source/File",
      "Asynchronous read from VCD disk",
      "Erik Walthinsen <omega@cse.ogi.edu>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&srctemplate));
  gst_element_class_set_details (element_class, &gst_vcdsrc_details);
}

static void
gst_vcdsrc_class_init (GstVCDSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  g_object_class_install_property (gobject_class, ARG_LOCATION,
      g_param_spec_string ("location", "Location",
          "CD device location (deprecated; use device)",
          NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_DEVICE,
      g_param_spec_string ("device", "Device",
          "CD device location", NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_TRACK,
      g_param_spec_int ("track", "Track",
          "Track number to play", G_MININT, G_MAXINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_BYTESPERREAD,
      g_param_spec_int ("bytesperread", "Bytes per read",
          "Bytes to read per iteration (VCD sector size)",
          G_MININT, G_MAXINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_OFFSET,
      g_param_spec_int ("offset", "Offset", "Offset",
          G_MININT, G_MAXINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_MAX_ERRORS,
      g_param_spec_int ("max-errors", "Max. errors",
          "Maximum number of errors before bailing out",
          0, G_MAXINT, 16, G_PARAM_READWRITE));

  gobject_class->set_property = gst_vcdsrc_set_property;
  gobject_class->get_property = gst_vcdsrc_get_property;

  gstelement_class->change_state = gst_vcdsrc_change_state;

  GST_DEBUG_CATEGORY_INIT (gst_vcdsrc_debug, "vcdsrc", 0,
      "VideoCD Source element");
}

static void
gst_vcdsrc_init (GstVCDSrc * vcdsrc)
{
  vcdsrc->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&srctemplate),
      "src");
  gst_pad_set_get_function (vcdsrc->srcpad, gst_vcdsrc_get);
#if 0
  gst_pad_set_get_region_function (vcdsrc->srcpad, vcdsrc_getregion);
#endif
  gst_pad_set_event_function (vcdsrc->srcpad, gst_vcdsrc_srcpad_event);
  gst_pad_set_event_mask_function (vcdsrc->srcpad, gst_vcdsrc_get_event_mask);
  gst_pad_set_query_function (vcdsrc->srcpad, gst_vcdsrc_srcpad_query);
  gst_pad_set_query_type_function (vcdsrc->srcpad, gst_vcdsrc_get_query_types);
  gst_pad_set_formats_function (vcdsrc->srcpad, gst_vcdsrc_get_formats);
  gst_element_add_pad (GST_ELEMENT (vcdsrc), vcdsrc->srcpad);

  vcdsrc->device = g_strdup ("/dev/cdrom");
  vcdsrc->track = 1;
  vcdsrc->fd = 0;
  vcdsrc->trackoffset = 0;
  vcdsrc->curoffset = 0;
  vcdsrc->tempoffset = 0;
  vcdsrc->discont = vcdsrc->flush = FALSE;
  vcdsrc->bytes_per_read = VCD_BYTES_PER_SECTOR;
  vcdsrc->seq = 0;
  vcdsrc->max_errors = 16;
}


static void
gst_vcdsrc_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstVCDSrc *src;

  g_return_if_fail (GST_IS_VCDSRC (object));
  src = GST_VCDSRC (object);

  switch (prop_id) {
    case ARG_DEVICE:
    case ARG_LOCATION:
      /* the element must be stopped in order to do this */
/*      g_return_if_fail(!GST_OBJECT_FLAG_IS_SET(src,GST_STATE_RUNNING)); */

      g_free (src->device);
      /* clear the filename if we get a NULL (is that possible?) */
      if (g_value_get_string (value) == NULL)
        src->device = NULL;
      /* otherwise set the new filename */
      else
        src->device = g_strdup (g_value_get_string (value));
      break;
    case ARG_TRACK:
      if (g_value_get_int (value) >= 1 &&
          g_value_get_int (value) < src->numtracks) {
        src->track = g_value_get_int (value);
        gst_vcdsrc_recalculate (src);
      }
      break;
    case ARG_MAX_ERRORS:
      src->max_errors = g_value_get_int (value);
      break;
    default:
      break;
  }

}

static void
gst_vcdsrc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVCDSrc *src;

  g_return_if_fail (GST_IS_VCDSRC (object));
  src = GST_VCDSRC (object);

  switch (prop_id) {
    case ARG_DEVICE:
    case ARG_LOCATION:
      g_value_set_string (value, src->device);
      break;
    case ARG_TRACK:
      g_value_set_int (value, src->track);
      break;
    case ARG_BYTESPERREAD:
      g_value_set_int (value, src->bytes_per_read);
      break;
    case ARG_OFFSET:
      g_value_set_int (value, src->curoffset * VCD_BYTES_PER_SECTOR);
      break;
    case ARG_MAX_ERRORS:
      g_value_set_int (value, src->max_errors);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/*
 * Querying and seeking.
 */

static const GstEventMask *
gst_vcdsrc_get_event_mask (GstPad * pad)
{
  static const GstEventMask masks[] = {
    {GST_EVENT_SEEK, GST_SEEK_METHOD_CUR |
          GST_SEEK_METHOD_SET | GST_SEEK_METHOD_END | GST_SEEK_FLAG_FLUSH},
    {0, 0}
  };

  return masks;
}

static const GstQueryType *
gst_vcdsrc_get_query_types (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    0
  };

  return types;
}

static const GstFormat *
gst_vcdsrc_get_formats (GstPad * pad)
{
  static const GstFormat formats[] = {
    GST_FORMAT_BYTES,
    0,
  };

  return formats;
}

static gboolean
gst_vcdsrc_srcpad_event (GstPad * pad, GstEvent * event)
{
  GstVCDSrc *vcdsrc = GST_VCDSRC (gst_pad_get_parent (pad));
  gboolean res = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:{
      gint64 new_off;

      if (GST_EVENT_SEEK_FORMAT (event) != GST_FORMAT_BYTES)
        return FALSE;

      new_off = GST_EVENT_SEEK_OFFSET (event);
      switch (GST_EVENT_SEEK_METHOD (event)) {
        case GST_SEEK_METHOD_SET:
          /* no-op */
          break;
        case GST_SEEK_METHOD_CUR:
          new_off += vcdsrc->curoffset * vcdsrc->bytes_per_read;
          break;
        case GST_SEEK_METHOD_END:
          new_off = (gst_vcdsrc_msf (vcdsrc, vcdsrc->track + 1) -
              vcdsrc->trackoffset) * vcdsrc->bytes_per_read - new_off;
          break;
        default:
          return FALSE;
      }

      if (new_off < 0 ||
          new_off > (gst_vcdsrc_msf (vcdsrc, vcdsrc->track + 1) -
              vcdsrc->trackoffset) * vcdsrc->bytes_per_read)
        return FALSE;

      vcdsrc->curoffset = new_off / vcdsrc->bytes_per_read;
      vcdsrc->tempoffset = new_off % vcdsrc->bytes_per_read;
      vcdsrc->discont = TRUE;
      if (GST_EVENT_SEEK_FLAGS (event) & GST_SEEK_FLAG_FLUSH)
        vcdsrc->flush = TRUE;
      break;
    }
    default:
      res = FALSE;
      break;
  }

  gst_event_unref (event);

  return res;
}

static gboolean
gst_vcdsrc_srcpad_query (GstPad * pad, GstQueryType type,
    GstFormat * format, gint64 * value)
{
  GstVCDSrc *vcdsrc = GST_VCDSRC (gst_pad_get_parent (pad));
  gboolean res = TRUE;

  if (*format != GST_FORMAT_BYTES)
    return FALSE;

  switch (type) {
    case GST_QUERY_TOTAL:
      *value = (gst_vcdsrc_msf (vcdsrc, vcdsrc->track + 1) -
          vcdsrc->trackoffset) * vcdsrc->bytes_per_read;
      break;
    case GST_QUERY_POSITION:
      *value = vcdsrc->curoffset * vcdsrc->bytes_per_read;
      break;
    default:
      res = FALSE;
      break;
  }

  return res;
}

/*
 * Data.
 */

static GstData *
gst_vcdsrc_get (GstPad * pad)
{
  GstVCDSrc *vcdsrc;
  GstFormat fmt = GST_FORMAT_BYTES;
  GstBuffer *buf;
  gulong offset;
  struct cdrom_msf *msf;
  gint error_count = 0;

  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  vcdsrc = GST_VCDSRC (GST_OBJECT_PARENT (pad));
  g_return_val_if_fail (GST_OBJECT_FLAG_IS_SET (vcdsrc, VCDSRC_OPEN), NULL);

  offset = vcdsrc->trackoffset + vcdsrc->curoffset;
  if (offset >= gst_vcdsrc_msf (vcdsrc, vcdsrc->track + 1)) {
    gst_element_set_eos (GST_ELEMENT (vcdsrc));
    return GST_DATA (gst_event_new (GST_EVENT_EOS));
  }

  if (vcdsrc->discont) {
    if (vcdsrc->flush) {
      vcdsrc->flush = FALSE;
      return GST_DATA (gst_event_new (GST_EVENT_FLUSH));
    }
    vcdsrc->discont = FALSE;
    return GST_DATA (gst_event_new_discontinuous (FALSE,
            GST_FORMAT_BYTES, vcdsrc->curoffset * vcdsrc->bytes_per_read,
            GST_FORMAT_UNDEFINED));
  }

  /* create the buffer */
  /* FIXME: should eventually use a bufferpool for this */
  buf = gst_buffer_new_and_alloc (vcdsrc->bytes_per_read);
  msf = (struct cdrom_msf *) GST_BUFFER_DATA (buf);

read:
  /* read it in from the device */
  msf->cdmsf_frame0 = offset % 75;
  msf->cdmsf_sec0 = (offset / 75) % 60;
  msf->cdmsf_min0 = (offset / (75 * 60));

  GST_LOG ("msf is %d:%d:%d", msf->cdmsf_min0, msf->cdmsf_sec0,
      msf->cdmsf_frame0);

  if (ioctl (vcdsrc->fd, CDROMREADRAW, msf) < 0) {
    if (++error_count <= vcdsrc->max_errors) {
      vcdsrc->curoffset++;
      offset++;
      goto read;
    }

    GST_ELEMENT_ERROR (vcdsrc, RESOURCE, READ, (NULL),
        ("Read from cdrom at %d:%d:%d failed: %s",
            msf->cdmsf_min0, msf->cdmsf_sec0, msf->cdmsf_frame0,
            strerror (errno)));

    return GST_DATA (gst_event_new (GST_EVENT_INTERRUPT));
  }


  gst_pad_query (pad, GST_QUERY_POSITION, &fmt,
      (gint64 *) & GST_BUFFER_OFFSET (buf));
  GST_BUFFER_SIZE (buf) = vcdsrc->bytes_per_read;
  vcdsrc->curoffset += 1;

  if (vcdsrc->tempoffset != 0) {
    GstBuffer *sub;

    sub = gst_buffer_create_sub (buf, vcdsrc->tempoffset,
        vcdsrc->bytes_per_read - vcdsrc->tempoffset);
    vcdsrc->tempoffset = 0;
    gst_buffer_unref (buf);
    buf = sub;
  }

  return GST_DATA (buf);
}

/* open the file, necessary to go to RUNNING state */
static gboolean
gst_vcdsrc_open_file (GstVCDSrc * src)
{
  int i;

  g_return_val_if_fail (!GST_OBJECT_FLAG_IS_SET (src, VCDSRC_OPEN), FALSE);

  /* open the device */
  src->fd = open (src->device, O_RDONLY);
  if (src->fd < 0) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL), GST_ERROR_SYSTEM);
    return FALSE;
  }

  /* read the table of contents */
  if (ioctl (src->fd, CDROMREADTOCHDR, &src->tochdr)) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL), GST_ERROR_SYSTEM);
    close (src->fd);
    return FALSE;
  }

  /* allocate enough track structs for disk */
  src->numtracks = (src->tochdr.cdth_trk1 - src->tochdr.cdth_trk0) + 1;
  src->tracks = g_new (struct cdrom_tocentry, src->numtracks + 1);

  /* read each track entry */
  for (i = 0; i <= src->numtracks; i++) {
    src->tracks[i].cdte_track = i == src->numtracks ? CDROM_LEADOUT : i + 1;
    src->tracks[i].cdte_format = CDROM_MSF;
    if (ioctl (src->fd, CDROMREADTOCENTRY, &src->tracks[i])) {
      GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL), GST_ERROR_SYSTEM);
      g_free (src->tracks);
      close (src->fd);
      return FALSE;
    }
    GST_DEBUG ("track %d begins at %d:%02d.%02d", i,
        src->tracks[i].cdte_addr.msf.minute,
        src->tracks[i].cdte_addr.msf.second,
        src->tracks[i].cdte_addr.msf.frame);
  }

  GST_OBJECT_FLAG_SET (src, VCDSRC_OPEN);
  gst_vcdsrc_recalculate (src);

  return TRUE;
}

/* close the file */
static void
gst_vcdsrc_close_file (GstVCDSrc * src)
{
  g_return_if_fail (GST_OBJECT_FLAG_IS_SET (src, VCDSRC_OPEN));

  /* close the file */
  close (src->fd);

  /* zero out a lot of our state */
  src->fd = 0;
  src->curoffset = 0;
  src->seq = 0;

  g_free (src->tracks);

  GST_OBJECT_FLAG_UNSET (src, VCDSRC_OPEN);
}

static GstStateChangeReturn
gst_vcdsrc_change_state (GstElement * element, GstStateChange transition)
{
  GstVCDSrc *vcdsrc = GST_VCDSRC (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (GST_OBJECT_FLAG_IS_SET (element, VCDSRC_OPEN))
        gst_vcdsrc_close_file (vcdsrc);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      vcdsrc->curoffset = 0;
      vcdsrc->discont = vcdsrc->flush = FALSE;
      break;
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!GST_OBJECT_FLAG_IS_SET (element, VCDSRC_OPEN))
        if (!gst_vcdsrc_open_file (vcdsrc))
          return GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return GST_STATE_CHANGE_SUCCESS;
}

static inline guint64
gst_vcdsrc_msf (GstVCDSrc * vcdsrc, gint track)
{
  return (vcdsrc->tracks[track].cdte_addr.msf.minute * 60 +
      vcdsrc->tracks[track].cdte_addr.msf.second) * 75 +
      vcdsrc->tracks[track].cdte_addr.msf.frame;
}

static void
gst_vcdsrc_recalculate (GstVCDSrc * vcdsrc)
{
  if (GST_OBJECT_FLAG_IS_SET (vcdsrc, VCDSRC_OPEN)) {
    /* calculate track offset (beginning of track) */
    vcdsrc->trackoffset = gst_vcdsrc_msf (vcdsrc, vcdsrc->track);
    GST_DEBUG ("track offset is %ld", vcdsrc->trackoffset);
  }
}

/*
 * URI interface.
 */

static guint
gst_vcdsrc_uri_get_type (void)
{
  return GST_URI_SRC;
}

static gchar **
gst_vcdsrc_uri_get_protocols (void)
{
  static gchar *protocols[] = { "vcd", NULL };

  return protocols;
}

static const gchar *
gst_vcdsrc_uri_get_uri (GstURIHandler * handler)
{
  return "vcd://";
}

static gboolean
gst_vcdsrc_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  gboolean ret;
  gchar *protocol = gst_uri_get_protocol (uri);

  ret = (protocol && !strcmp (protocol, "vcd")) ? TRUE : FALSE;
  g_free (protocol);

  return ret;
}

static void
gst_vcdsrc_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_vcdsrc_uri_get_type;
  iface->get_protocols = gst_vcdsrc_uri_get_protocols;
  iface->get_uri = gst_vcdsrc_uri_get_uri;
  iface->set_uri = gst_vcdsrc_uri_set_uri;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "vcdsrc", GST_RANK_NONE,
      GST_TYPE_VCDSRC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "vcdsrc",
    "Asynchronous read from VCD disk",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
