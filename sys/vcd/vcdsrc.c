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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
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

#include "vcdsrc.h"

#define DEFAULT_DEVICE "/dev/cdrom"

/* VCDSrc signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_DEVICE,
  PROP_TRACK,
  PROP_MAX_ERRORS
};

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_vcdsrc_debug);
#define GST_CAT_DEFAULT gst_vcdsrc_debug

static void gst_vcdsrc_uri_handler_init (gpointer g_iface, gpointer iface_data);
#define gst_vcdsrc_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVCDSrc, gst_vcdsrc, GST_TYPE_PUSH_SRC,
    G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER, gst_vcdsrc_uri_handler_init));

static void gst_vcdsrc_finalize (GObject * object);

static void gst_vcdsrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_vcdsrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_vcdsrc_start (GstBaseSrc * src);
static gboolean gst_vcdsrc_stop (GstBaseSrc * src);
static GstFlowReturn gst_vcdsrc_create (GstPushSrc * src, GstBuffer ** out);


static void
gst_vcdsrc_class_init (GstVCDSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstBaseSrcClass *basesrc_class;
  GstPushSrcClass *pushsrc_class;

  gobject_class = (GObjectClass *) klass;
  basesrc_class = (GstBaseSrcClass *) klass;
  pushsrc_class = (GstPushSrcClass *) klass;
  element_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_vcdsrc_set_property;
  gobject_class->get_property = gst_vcdsrc_get_property;
  gobject_class->finalize = gst_vcdsrc_finalize;

  g_object_class_install_property (gobject_class, PROP_DEVICE,
      g_param_spec_string ("device", "Device",
          "CD device location", NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_TRACK,
      g_param_spec_int ("track", "Track",
          "Track number to play", G_MININT, G_MAXINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_MAX_ERRORS,
      g_param_spec_int ("max-errors", "Max. errors",
          "Maximum number of errors before bailing out",
          0, G_MAXINT, 16, G_PARAM_READWRITE));

  basesrc_class->start = gst_vcdsrc_start;
  basesrc_class->stop = gst_vcdsrc_stop;

  pushsrc_class->create = gst_vcdsrc_create;

  GST_DEBUG_CATEGORY_INIT (gst_vcdsrc_debug, "vcdsrc", 0,
      "VideoCD Source element");

  gst_element_class_set_static_metadata (element_class, "VCD Source",
      "Source/File",
      "Asynchronous read from VCD disk", "Erik Walthinsen <omega@cse.ogi.edu>");

  gst_element_class_add_static_pad_template (element_class, &srctemplate);
}

static void
gst_vcdsrc_init (GstVCDSrc * vcdsrc)
{
  vcdsrc->device = g_strdup (DEFAULT_DEVICE);
  vcdsrc->track = 1;
  vcdsrc->fd = 0;
  vcdsrc->trackoffset = 0;
  vcdsrc->curoffset = 0;
  vcdsrc->bytes_per_read = VCD_BYTES_PER_SECTOR;
  vcdsrc->max_errors = 16;
}

static void
gst_vcdsrc_finalize (GObject * object)
{
  GstVCDSrc *vcdsrc = GST_VCDSRC (object);

  g_free (vcdsrc->device);

  G_OBJECT_CLASS (parent_class)->finalize (object);
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
  /* calculate track offset (beginning of track) */
  vcdsrc->trackoffset = gst_vcdsrc_msf (vcdsrc, vcdsrc->track);
  GST_DEBUG ("track offset is %ld", vcdsrc->trackoffset);
}

static void
gst_vcdsrc_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstVCDSrc *src;

  src = GST_VCDSRC (object);

  switch (prop_id) {
    case PROP_DEVICE:
      g_free (src->device);
      src->device = g_value_dup_string (value);
      break;
    case PROP_TRACK:
      if (g_value_get_int (value) >= 1 &&
          g_value_get_int (value) < src->numtracks) {
        src->track = g_value_get_int (value);
        gst_vcdsrc_recalculate (src);
      }
      break;
    case PROP_MAX_ERRORS:
      src->max_errors = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
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
    case PROP_DEVICE:
      g_value_set_string (value, src->device);
      break;
    case PROP_TRACK:
      g_value_set_int (value, src->track);
      break;
    case PROP_MAX_ERRORS:
      g_value_set_int (value, src->max_errors);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

#if 0
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
#endif

#if 0
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
#endif

static GstFlowReturn
gst_vcdsrc_create (GstPushSrc * src, GstBuffer ** buf)
{
  GstVCDSrc *vcdsrc;
  GstBuffer *outbuf;
  GstMapInfo map_info;
  gulong offset;
  struct cdrom_msf *msf;
  gint error_count = 0;

  vcdsrc = GST_VCDSRC (src);

  offset = vcdsrc->trackoffset + vcdsrc->curoffset;
  if (offset >= gst_vcdsrc_msf (vcdsrc, vcdsrc->track + 1))
    goto eos;

  /* create the buffer */
  outbuf = gst_buffer_new_allocate (NULL, vcdsrc->bytes_per_read, NULL);
  if (!outbuf)
    goto error_unmapped;

  if (!gst_buffer_map (outbuf, &map_info, GST_MAP_READWRITE))
    goto error_unmapped;

  msf = (struct cdrom_msf *) map_info.data;


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
    goto error_mapped;
  }

  gst_buffer_unmap (outbuf, &map_info);
  vcdsrc->curoffset += 1;

  *buf = outbuf;
  return GST_FLOW_OK;

  /* ERRORS */
error_mapped:
  gst_buffer_unmap (outbuf, &map_info);
error_unmapped:
  if (outbuf)
    gst_buffer_unref (outbuf);
  return GST_FLOW_ERROR;
eos:
  {
    GST_DEBUG_OBJECT (vcdsrc, "got eos");
    return GST_FLOW_EOS;
  }
}

/* open the file, necessary to go to RUNNING state */
static gboolean
gst_vcdsrc_start (GstBaseSrc * bsrc)
{
  int i;
  GstVCDSrc *src = GST_VCDSRC (bsrc);
  struct stat buf;

  /* open the device */
  src->fd = open (src->device, O_RDONLY);
  if (src->fd < 0)
    goto open_failed;

  if (fstat (src->fd, &buf) < 0)
    goto toc_failed;
  /* If it's not a block device, then we need to try and
   * parse the cue file if there is one
   * FIXME implement */
  if (!S_ISBLK (buf.st_mode)) {
    GST_DEBUG ("Reading CUE files not handled yet, cannot process %s",
        GST_STR_NULL (src->device));
    goto toc_failed;
  }

  /* read the table of contents */
  if (ioctl (src->fd, CDROMREADTOCHDR, &src->tochdr))
    goto toc_failed;

  /* allocate enough track structs for disk */
  src->numtracks = (src->tochdr.cdth_trk1 - src->tochdr.cdth_trk0) + 1;
  src->tracks = g_new (struct cdrom_tocentry, src->numtracks + 1);

  /* read each track entry */
  for (i = 0; i <= src->numtracks; i++) {
    src->tracks[i].cdte_track = i == src->numtracks ? CDROM_LEADOUT : i + 1;
    src->tracks[i].cdte_format = CDROM_MSF;
    if (ioctl (src->fd, CDROMREADTOCENTRY, &src->tracks[i]))
      goto toc_entry_failed;

    GST_DEBUG ("track %d begins at %d:%02d.%02d", i,
        src->tracks[i].cdte_addr.msf.minute,
        src->tracks[i].cdte_addr.msf.second,
        src->tracks[i].cdte_addr.msf.frame);
  }

  src->curoffset = 0;

  gst_vcdsrc_recalculate (src);

  return TRUE;

  /* ERRORS */
open_failed:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL), GST_ERROR_SYSTEM);
    return FALSE;
  }
toc_failed:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL), GST_ERROR_SYSTEM);
    close (src->fd);
    return FALSE;
  }
toc_entry_failed:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL), GST_ERROR_SYSTEM);
    g_free (src->tracks);
    close (src->fd);
    return FALSE;
  }
}

/* close the file */
static gboolean
gst_vcdsrc_stop (GstBaseSrc * bsrc)
{
  GstVCDSrc *src = GST_VCDSRC (bsrc);

  /* close the file */
  close (src->fd);

  /* zero out a lot of our state */
  src->fd = 0;
  src->curoffset = 0;

  g_free (src->tracks);

  return TRUE;
}

/*
 * URI interface.
 */

static GstURIType
gst_vcdsrc_uri_get_type (GType type)
{
  return GST_URI_SRC;
}

static const gchar *const *
gst_vcdsrc_uri_get_protocols (GType type)
{
  static const gchar *protocols[] = { "vcd", NULL };

  return protocols;
}

static gchar *
gst_vcdsrc_uri_get_uri (GstURIHandler * handler)
{
  GstVCDSrc *src = GST_VCDSRC (handler);
  gchar *result;

  GST_OBJECT_LOCK (src);
  result = g_strdup_printf ("vcd://%d", src->track);
  GST_OBJECT_UNLOCK (src);

  return result;
}

static gboolean
gst_vcdsrc_uri_set_uri (GstURIHandler * handler, const gchar * uri,
    GError ** error)
{
  GstVCDSrc *src = GST_VCDSRC (handler);
  gchar *location = NULL;
  gint tracknr;

  GST_DEBUG_OBJECT (src, "setting uri '%s'", uri);

  if (!gst_uri_has_protocol (uri, "vcd"))
    goto wrong_protocol;

  /* parse out the track in the location */
  if (!(location = gst_uri_get_location (uri)))
    goto no_location;

  GST_DEBUG_OBJECT (src, "have location '%s'", location);

  /*
   * URI structure: vcd:///path/to/device,track-num
   */
  if (g_str_has_prefix (uri, "vcd://")) {
    GST_OBJECT_LOCK (src);
    g_free (src->device);
    if (strlen (uri) > 6)
      src->device = g_strdup (uri + 6);
    else
      src->device = g_strdup (DEFAULT_DEVICE);
    GST_DEBUG_OBJECT (src, "configured device %s", src->device);
    GST_OBJECT_UNLOCK (src);
  }

  /* Parse the track number */
  {
    char **split;

    split = g_strsplit (location, ",", 2);
    if (split == NULL || *split == NULL || split[1] == NULL) {
      tracknr = 1;
    } else if (sscanf (split[1], "%d", &tracknr) != 1 || tracknr < 1) {
      g_strfreev (split);
      goto invalid_location;
    }
    g_strfreev (split);
  }

  GST_OBJECT_LOCK (src);
  src->track = tracknr;
  GST_DEBUG_OBJECT (src, "configured track %d", src->track);
  GST_OBJECT_UNLOCK (src);

  g_free (location);

  return TRUE;

  /* ERRORS */
wrong_protocol:
  {
    GST_ERROR_OBJECT (src, "Wrong protocol, uri = %s", uri);
    g_set_error (error, GST_URI_ERROR, GST_URI_ERROR_UNSUPPORTED_PROTOCOL,
        "Wrong protocol, uri = %s", uri);
    return FALSE;
  }
no_location:
  {
    GST_ERROR_OBJECT (src, "No location specified");
    g_set_error_literal (error, GST_URI_ERROR, GST_URI_ERROR_BAD_URI,
        "No location specified");
    return FALSE;
  }
invalid_location:
  {
    GST_ERROR_OBJECT (src, "Invalid location '%s' in URI '%s'", location, uri);
    g_set_error (error, GST_URI_ERROR, GST_URI_ERROR_BAD_URI,
        "Invalid location '%s' in URI '%s'", location, uri);
    g_free (location);
    return FALSE;
  }
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
  return gst_element_register (plugin, "vcdsrc", GST_RANK_SECONDARY,
      GST_TYPE_VCDSRC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    vcdsrc,
    "Asynchronous read from VCD disk",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
