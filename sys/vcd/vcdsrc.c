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


static GstElementDetails vcdsrc_details = GST_ELEMENT_DETAILS ("VCD Source",
    "Source/File",
    "Asynchronous read from VCD disk",
    "Erik Walthinsen <omega@cse.ogi.edu>");


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
  ARG_TRACK,
  ARG_BYTESPERREAD,
  ARG_OFFSET,
  ARG_MAX_ERRORS,
};

static void vcdsrc_base_init (gpointer g_class);
static void vcdsrc_class_init (VCDSrcClass * klass);
static void vcdsrc_init (VCDSrc * vcdsrc);
static void vcdsrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void vcdsrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstData *vcdsrc_get (GstPad * pad);

/*static GstBuffer *		vcdsrc_get_region	(GstPad *pad,gulong offset,gulong size); */
static GstElementStateReturn vcdsrc_change_state (GstElement * element);

static void vcdsrc_recalculate (VCDSrc * vcdsrc);


static GstElementClass *parent_class = NULL;

/*static guint vcdsrc_signals[LAST_SIGNAL] = { 0 }; */

GType
vcdsrc_get_type (void)
{
  static GType vcdsrc_type = 0;

  if (!vcdsrc_type) {
    static const GTypeInfo vcdsrc_info = {
      sizeof (VCDSrcClass),
      vcdsrc_base_init,
      NULL,
      (GClassInitFunc) vcdsrc_class_init,
      NULL,
      NULL,
      sizeof (VCDSrc),
      0,
      (GInstanceInitFunc) vcdsrc_init,
    };

    vcdsrc_type =
        g_type_register_static (GST_TYPE_ELEMENT, "VCDSrc", &vcdsrc_info, 0);
  }
  return vcdsrc_type;
}

static void
vcdsrc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &vcdsrc_details);
}
static void
vcdsrc_class_init (VCDSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LOCATION, g_param_spec_string ("location", "location", "location", NULL, G_PARAM_READWRITE));    /* CHECKME */
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_TRACK, g_param_spec_int ("track", "track", "track", G_MININT, G_MAXINT, 0, G_PARAM_READWRITE));  /* CHECKME */
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BYTESPERREAD, g_param_spec_int ("bytesperread", "bytesperread", "bytesperread", G_MININT, G_MAXINT, 0, G_PARAM_READABLE));       /* CHECKME */
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_OFFSET, g_param_spec_int ("offset", "offset", "offset", G_MININT, G_MAXINT, 0, G_PARAM_READWRITE));      /* CHECKME */
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MAX_ERRORS,
      g_param_spec_int ("max-errors", "", "", 0, G_MAXINT, 16,
          G_PARAM_READWRITE));

  gobject_class->set_property = vcdsrc_set_property;
  gobject_class->get_property = vcdsrc_get_property;

  gstelement_class->change_state = vcdsrc_change_state;
}

static void
vcdsrc_init (VCDSrc * vcdsrc)
{
  vcdsrc->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_pad_set_get_function (vcdsrc->srcpad, vcdsrc_get);
/*  gst_pad_set_get_region_function (vcdsrc->srcpad, vcdsrc_getregion); */
  gst_element_add_pad (GST_ELEMENT (vcdsrc), vcdsrc->srcpad);

  vcdsrc->device = g_strdup ("/dev/cdrom");
  vcdsrc->track = 2;
  vcdsrc->fd = 0;
  vcdsrc->trackoffset = 0;
  vcdsrc->curoffset = 0;
  vcdsrc->bytes_per_read = VCD_BYTES_PER_SECTOR;
  vcdsrc->seq = 0;
  vcdsrc->max_errors = 16;
}


static void
vcdsrc_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  VCDSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_VCDSRC (object));
  src = VCDSRC (object);

  switch (prop_id) {
    case ARG_LOCATION:
      /* the element must be stopped in order to do this */
/*      g_return_if_fail(!GST_FLAG_IS_SET(src,GST_STATE_RUNNING)); */

      if (src->device)
        g_free (src->device);
      /* clear the filename if we get a NULL (is that possible?) */
      if (g_value_get_string (value) == NULL)
        src->device = NULL;
      /* otherwise set the new filename */
      else
        src->device = g_strdup (g_value_get_string (value));
      break;
    case ARG_TRACK:
      src->track = g_value_get_int (value);
      vcdsrc_recalculate (src);
      break;
/*    case ARG_BYTESPERREAD:
      src->bytes_per_read = g_value_get_int (value);
      break;*/
    case ARG_OFFSET:
      src->curoffset = g_value_get_int (value) / VCD_BYTES_PER_SECTOR;
      break;
    case ARG_MAX_ERRORS:
      src->max_errors = g_value_get_int (value);
      break;
    default:
      break;
  }

}

static void
vcdsrc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  VCDSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_VCDSRC (object));
  src = VCDSRC (object);

  switch (prop_id) {
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

static GstData *
vcdsrc_get (GstPad * pad)
{
  VCDSrc *vcdsrc;
  GstBuffer *buf;
  gulong offset;
  struct cdrom_msf *msf;
  gint error_count = 0;

  /* fprintf(stderr,"in vcdsrc_push\n"); */

  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  vcdsrc = VCDSRC (GST_OBJECT_PARENT (pad));
  g_return_val_if_fail (GST_FLAG_IS_SET (vcdsrc, VCDSRC_OPEN), NULL);

  /* create the buffer */
  /* FIXME: should eventually use a bufferpool for this */
  buf = gst_buffer_new ();
  g_return_val_if_fail (buf != NULL, NULL);

  /* allocate the space for the buffer data */
  GST_BUFFER_DATA (buf) = g_malloc (vcdsrc->bytes_per_read);
  memset (GST_BUFFER_DATA (buf), 0, vcdsrc->bytes_per_read);
  g_return_val_if_fail (GST_BUFFER_DATA (buf) != NULL, NULL);

  msf = (struct cdrom_msf *) GST_BUFFER_DATA (buf);

read_sector:

  /* read it in from the device */
  offset = vcdsrc->trackoffset + vcdsrc->curoffset;
  msf->cdmsf_frame0 = offset % 75;
  msf->cdmsf_sec0 = (offset / 75) % 60;
  msf->cdmsf_min0 = (offset / (75 * 60));

  /*GST_INFO("msf is %d:%d:%d\n",msf->cdmsf_min0,msf->cdmsf_sec0, */
  /* msf->cdmsf_frame0); */

  if (ioctl (vcdsrc->fd, CDROMREADRAW, msf)) {
    if (++error_count > vcdsrc->max_errors) {
      gst_element_set_eos (GST_ELEMENT (vcdsrc));
      return GST_DATA (gst_event_new (GST_EVENT_EOS));
    }

    fprintf (stderr, "%s while reading raw data from cdrom at %d:%d:%d\n",
        strerror (errno), msf->cdmsf_min0, msf->cdmsf_sec0, msf->cdmsf_frame0);
    vcdsrc->curoffset += 1;

    /* Or we can return a zero-filled buffer.  Which is better? */
    goto read_sector;
  }


  GST_BUFFER_OFFSET (buf) = vcdsrc->curoffset;
  GST_BUFFER_SIZE (buf) = vcdsrc->bytes_per_read;
  vcdsrc->curoffset += 1;

  return GST_DATA (buf);
}

/* open the file, necessary to go to RUNNING state */
static gboolean
vcdsrc_open_file (VCDSrc * src)
{
  int i;

  g_return_val_if_fail (!GST_FLAG_IS_SET (src, VCDSRC_OPEN), FALSE);

  /* open the device */
  src->fd = open (src->device, O_RDONLY);
  if (src->fd < 0) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL), GST_ERROR_SYSTEM);
    return FALSE;
  }

  /* read the table of contents */
  if (ioctl (src->fd, CDROMREADTOCHDR, &src->tochdr)) {
    perror ("reading toc of VCD\n");
/* FIXME 	*/
/*    exit(1);	*/
  }

  /* allocate enough track structs for disk */
  src->numtracks = (src->tochdr.cdth_trk1 - src->tochdr.cdth_trk0) + 1;
  src->tracks = g_new (struct cdrom_tocentry, src->numtracks);

  /* read each track entry */
  for (i = 0; i < src->numtracks; i++) {
    src->tracks[i].cdte_track = i + 1;
    src->tracks[i].cdte_format = CDROM_MSF;
    if (ioctl (src->fd, CDROMREADTOCENTRY, &src->tracks[i])) {
      perror ("reading tocentry");
/* FIXME 	*/
/*      exit(1);*/
    }
    fprintf (stderr, "VCDSrc: track begins at %d:%d:%d\n",
        src->tracks[i].cdte_addr.msf.minute,
        src->tracks[i].cdte_addr.msf.second,
        src->tracks[i].cdte_addr.msf.frame);
  }

  src->trackoffset =
      (((src->tracks[src->track - 1].cdte_addr.msf.minute * 60) +
          src->tracks[src->track - 1].cdte_addr.msf.second) * 75) +
      src->tracks[src->track - 1].cdte_addr.msf.frame;
  fprintf (stderr, "VCDSrc: track offset is %ld\n", src->trackoffset);

  GST_FLAG_SET (src, VCDSRC_OPEN);
  return TRUE;
}

/* close the file */
static void
vcdsrc_close_file (VCDSrc * src)
{
  g_return_if_fail (GST_FLAG_IS_SET (src, VCDSRC_OPEN));

  /* close the file */
  close (src->fd);

  /* zero out a lot of our state */
  src->fd = 0;
  src->curoffset = 0;
  src->seq = 0;

  GST_FLAG_UNSET (src, VCDSRC_OPEN);
}

static GstElementStateReturn
vcdsrc_change_state (GstElement * element)
{
  g_return_val_if_fail (GST_IS_VCDSRC (element), GST_STATE_FAILURE);

  /* if going down into NULL state, close the file if it's open */
  if (GST_STATE_PENDING (element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET (element, VCDSRC_OPEN))
      vcdsrc_close_file (VCDSRC (element));
    /* otherwise (READY or higher) we need to open the sound card */
  } else {
    if (!GST_FLAG_IS_SET (element, VCDSRC_OPEN)) {
      if (!vcdsrc_open_file (VCDSRC (element)))
        return GST_STATE_FAILURE;
    }
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);
  return GST_STATE_SUCCESS;
}

static void
vcdsrc_recalculate (VCDSrc * vcdsrc)
{
  if (GST_FLAG_IS_SET (vcdsrc, VCDSRC_OPEN)) {
    /* calculate track offset (beginning of track) */
    vcdsrc->trackoffset =
        (((vcdsrc->tracks[vcdsrc->track - 1].cdte_addr.msf.minute * 60) +
            vcdsrc->tracks[vcdsrc->track - 1].cdte_addr.msf.second) * 75) +
        vcdsrc->tracks[vcdsrc->track - 1].cdte_addr.msf.frame;
    fprintf (stderr, "VCDSrc: track offset is %ld\n", vcdsrc->trackoffset);
  }
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
