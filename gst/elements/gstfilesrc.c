/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstfilesrc.c:
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
#  include "config.h"
#endif

#include <gst/gst.h>
#include "gstfilesrc.h"

#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_MMAP
#include <sys/mman.h>
#endif
#include <errno.h>
#include <string.h>

#include "../gst-i18n-lib.h"

/* FIXME we should be using glib for this */
#ifndef S_ISREG
#define S_ISREG(mode) ((mode)&_S_IFREG)
#endif
#ifndef S_ISDIR
#define S_ISDIR(mode) ((mode)&_S_IFDIR)
#endif
#ifndef S_ISSOCK
#define S_ISSOCK(x) (0)
#endif
#ifndef O_BINARY
#define O_BINARY (0)
#endif


/**********************************************************************
 * GStreamer Default File Source
 * Theory of Operation
 *
 * This source uses mmap(2) to efficiently load data from a file.
 * To do this without seriously polluting the applications' memory
 * space, it must do so in smaller chunks, say 1-4MB at a time.
 * Buffers are then subdivided from these mmap'd chunks, to directly
 * make use of the mmap.
 *
 * To handle refcounting so that the mmap can be freed at the appropriate
 * time, a buffer will be created for each mmap'd region, and all new
 * buffers will be sub-buffers of this top-level buffer.  As they are 
 * freed, the refcount goes down on the mmap'd buffer and its free()
 * function is called, which will call munmap(2) on itself.
 *
 * If a buffer happens to cross the boundaries of an mmap'd region, we
 * have to decide whether it's more efficient to copy the data into a
 * new buffer, or mmap() just that buffer.  There will have to be a
 * breakpoint size to determine which will be done.  The mmap() size
 * has a lot to do with this as well, because you end up in double-
 * jeopardy: the larger the outgoing buffer, the more data to copy when
 * it overlaps, *and* the more frequently you'll have buffers that *do*
 * overlap.
 *
 * Seeking is another tricky aspect to do efficiently.  The initial
 * implementation of this source won't make use of these features, however.
 * The issue is that if an application seeks backwards in a file, *and*
 * that region of the file is covered by an mmap that hasn't been fully
 * deallocated, we really should re-use it.  But keeping track of these
 * regions is tricky because we have to lock the structure that holds
 * them.  We need to settle on a locking primitive (GMutex seems to be
 * a really good option...), then we can do that.
 */


GST_DEBUG_CATEGORY_STATIC (gst_filesrc_debug);
#define GST_CAT_DEFAULT gst_filesrc_debug

GstElementDetails gst_filesrc_details = GST_ELEMENT_DETAILS ("File Source",
    "Source/File",
    "Read from arbitrary point in a file",
    "Erik Walthinsen <omega@cse.ogi.edu>");

#define DEFAULT_BLOCKSIZE 	4*1024
#define DEFAULT_MMAPSIZE 	4*1024*1024

/* FileSrc signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_LOCATION,
  ARG_FD,
  ARG_BLOCKSIZE,
  ARG_MMAPSIZE,
  ARG_TOUCH
};

static const GstEventMask *
gst_filesrc_get_event_mask (GstPad * pad)
{
  static const GstEventMask masks[] = {
    {GST_EVENT_SEEK, GST_SEEK_METHOD_CUR |
          GST_SEEK_METHOD_SET | GST_SEEK_METHOD_END | GST_SEEK_FLAG_FLUSH},
    {GST_EVENT_FLUSH, 0},
    {GST_EVENT_SIZE, 0},
    {0, 0}
  };

  return masks;
}

static const GstQueryType *
gst_filesrc_get_query_types (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    0
  };

  return types;
}

static const GstFormat *
gst_filesrc_get_formats (GstPad * pad)
{
  static const GstFormat formats[] = {
    GST_FORMAT_BYTES,
    0,
  };

  return formats;
}

static void gst_filesrc_dispose (GObject * object);

static void gst_filesrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_filesrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_filesrc_check_filesize (GstFileSrc * src);
static GstData *gst_filesrc_get (GstPad * pad);
static gboolean gst_filesrc_srcpad_event (GstPad * pad, GstEvent * event);
static gboolean gst_filesrc_srcpad_query (GstPad * pad, GstQueryType type,
    GstFormat * format, gint64 * value);

static GstElementStateReturn gst_filesrc_change_state (GstElement * element);

static void gst_filesrc_uri_handler_init (gpointer g_iface,
    gpointer iface_data);

static void
_do_init (GType filesrc_type)
{
  static const GInterfaceInfo urihandler_info = {
    gst_filesrc_uri_handler_init,
    NULL,
    NULL
  };

  g_type_add_interface_static (filesrc_type, GST_TYPE_URI_HANDLER,
      &urihandler_info);
  GST_DEBUG_CATEGORY_INIT (gst_filesrc_debug, "filesrc", 0, "filesrc element");
}

GST_BOILERPLATE_FULL (GstFileSrc, gst_filesrc, GstElement, GST_TYPE_ELEMENT,
    _do_init);

static void
gst_filesrc_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (gstelement_class, &gst_filesrc_details);
}
static void
gst_filesrc_class_init (GstFileSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class = (GObjectClass *) klass;


  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FD,
      g_param_spec_int ("fd", "File-descriptor",
          "File-descriptor for the file being mmap()d", 0, G_MAXINT, 0,
          G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LOCATION,
      g_param_spec_string ("location", "File Location",
          "Location of the file to read", NULL, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BLOCKSIZE,
      g_param_spec_ulong ("blocksize", "Block size",
          "Size in bytes to read per buffer", 1, G_MAXULONG, DEFAULT_BLOCKSIZE,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MMAPSIZE,
      g_param_spec_ulong ("mmapsize", "mmap() Block Size",
          "Size in bytes of mmap()d regions", 0, G_MAXULONG, DEFAULT_MMAPSIZE,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_TOUCH,
      g_param_spec_boolean ("touch", "Touch read data",
          "Touch data to force disk read", FALSE, G_PARAM_READWRITE));

  gobject_class->dispose = gst_filesrc_dispose;
  gobject_class->set_property = gst_filesrc_set_property;
  gobject_class->get_property = gst_filesrc_get_property;

  gstelement_class->change_state = gst_filesrc_change_state;
}

static void
gst_filesrc_init (GstFileSrc * src)
{
  src->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_pad_set_get_function (src->srcpad, gst_filesrc_get);
  gst_pad_set_event_function (src->srcpad, gst_filesrc_srcpad_event);
  gst_pad_set_event_mask_function (src->srcpad, gst_filesrc_get_event_mask);
  gst_pad_set_query_function (src->srcpad, gst_filesrc_srcpad_query);
  gst_pad_set_query_type_function (src->srcpad, gst_filesrc_get_query_types);
  gst_pad_set_formats_function (src->srcpad, gst_filesrc_get_formats);
  gst_element_add_pad (GST_ELEMENT (src), src->srcpad);

#ifdef HAVE_MMAP
  src->pagesize = getpagesize ();
#endif

  src->filename = NULL;
  src->fd = 0;
  src->filelen = 0;
  src->uri = NULL;

  src->curoffset = 0;
  src->block_size = DEFAULT_BLOCKSIZE;
  src->touch = FALSE;

  src->mapbuf = NULL;
  src->mapsize = DEFAULT_MMAPSIZE;      /* default is 4MB */

  src->is_regular = FALSE;
}

static void
gst_filesrc_dispose (GObject * object)
{
  GstFileSrc *src;

  src = GST_FILESRC (object);

  g_free (src->filename);
  g_free (src->uri);

  /* dispose may be called multiple times */
  src->filename = NULL;
  src->uri = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static gboolean
gst_filesrc_set_location (GstFileSrc * src, const gchar * location)
{
  /* the element must be stopped in order to do this */
  if (GST_STATE (src) != GST_STATE_READY && GST_STATE (src) != GST_STATE_NULL)
    return FALSE;

  g_free (src->filename);
  g_free (src->uri);

  /* clear the filename if we get a NULL (is that possible?) */
  if (location == NULL) {
    src->filename = NULL;
    src->uri = NULL;
  } else {
    src->filename = g_strdup (location);
    src->uri = gst_uri_construct ("file", src->filename);
  }
  g_object_notify (G_OBJECT (src), "location");
  gst_uri_handler_new_uri (GST_URI_HANDLER (src), src->uri);

  return TRUE;
}

static void
gst_filesrc_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstFileSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_FILESRC (object));

  src = GST_FILESRC (object);

  switch (prop_id) {
    case ARG_LOCATION:
      gst_filesrc_set_location (src, g_value_get_string (value));
      break;
    case ARG_BLOCKSIZE:
      src->block_size = g_value_get_ulong (value);
      g_object_notify (G_OBJECT (src), "blocksize");
      break;
    case ARG_MMAPSIZE:
      if ((src->mapsize % src->pagesize) == 0) {
        src->mapsize = g_value_get_ulong (value);
        g_object_notify (G_OBJECT (src), "mmapsize");
      } else {
        GST_INFO_OBJECT (src,
            "invalid mapsize, must be a multiple of pagesize, which is %d",
            src->pagesize);
      }
      break;
    case ARG_TOUCH:
      src->touch = g_value_get_boolean (value);
      g_object_notify (G_OBJECT (src), "touch");
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_filesrc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstFileSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_FILESRC (object));

  src = GST_FILESRC (object);

  switch (prop_id) {
    case ARG_LOCATION:
      g_value_set_string (value, src->filename);
      break;
    case ARG_FD:
      g_value_set_int (value, src->fd);
      break;
    case ARG_BLOCKSIZE:
      g_value_set_ulong (value, src->block_size);
      break;
    case ARG_MMAPSIZE:
      g_value_set_ulong (value, src->mapsize);
      break;
    case ARG_TOUCH:
      g_value_set_boolean (value, src->touch);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

#ifdef HAVE_MMAP
static void
gst_filesrc_free_parent_mmap (GstBuffer * buf)
{
  GST_LOG ("freeing mmap()d buffer at %" G_GUINT64_FORMAT "+%u",
      GST_BUFFER_OFFSET (buf), GST_BUFFER_SIZE (buf));

#ifdef MADV_DONTNEED
  /* madvise to tell the kernel what to do with it */
  madvise (GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf), MADV_DONTNEED);
#endif
  /* now unmap the memory */
  munmap (GST_BUFFER_DATA (buf), GST_BUFFER_MAXSIZE (buf));
  /* cast to unsigned long, since there's no gportable way to print
   * guint64 as hex */
  GST_LOG ("unmapped region %08lx+%08lx at %p",
      (unsigned long) GST_BUFFER_OFFSET (buf),
      (unsigned long) GST_BUFFER_MAXSIZE (buf), GST_BUFFER_DATA (buf));

  GST_BUFFER_DATA (buf) = NULL;
}
#endif

#ifdef HAVE_MMAP
static GstBuffer *
gst_filesrc_map_region (GstFileSrc * src, off_t offset, size_t size)
{
  GstBuffer *buf;
  gint retval;
  void *mmapregion;

  g_return_val_if_fail (offset >= 0, NULL);

  GST_LOG_OBJECT (src, "mapping region %08llx+%08lx from file into memory",
      offset, (unsigned long) size);
  mmapregion = mmap (NULL, size, PROT_READ, MAP_SHARED, src->fd, offset);

  if (mmapregion == NULL) {
    GST_ELEMENT_ERROR (src, RESOURCE, TOO_LAZY, (NULL), ("mmap call failed."));
    return NULL;
  } else if (mmapregion == MAP_FAILED) {
    GST_WARNING_OBJECT (src, "mmap (0x%08lx, %d, 0x%llx) failed: %s",
        (unsigned long) size, src->fd, offset, strerror (errno));
    return NULL;
  }
  GST_LOG_OBJECT (src, "mapped region %08lx+%08lx from file into memory at %p",
      (unsigned long) offset, (unsigned long) size, mmapregion);

  /* time to allocate a new mapbuf */
  buf = gst_buffer_new ();
  /* mmap() the data into this new buffer */
  GST_BUFFER_DATA (buf) = mmapregion;

#ifdef MADV_SEQUENTIAL
  /* madvise to tell the kernel what to do with it */
  retval =
      madvise (GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf), MADV_SEQUENTIAL);
#endif
  /* fill in the rest of the fields */
  GST_BUFFER_FLAG_SET (buf, GST_BUFFER_READONLY);
  GST_BUFFER_FLAG_SET (buf, GST_BUFFER_ORIGINAL);
  GST_BUFFER_SIZE (buf) = size;
  GST_BUFFER_MAXSIZE (buf) = size;
  GST_BUFFER_OFFSET (buf) = offset;
  GST_BUFFER_OFFSET_END (buf) = offset + size;
  GST_BUFFER_TIMESTAMP (buf) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_PRIVATE (buf) = src;
  GST_BUFFER_FREE_DATA_FUNC (buf) = gst_filesrc_free_parent_mmap;

  return buf;
}
#endif

#ifdef HAVE_MMAP
static GstBuffer *
gst_filesrc_map_small_region (GstFileSrc * src, off_t offset, size_t size)
{
  size_t mapsize;
  off_t mod, mapbase;
  GstBuffer *map;

/*  printf("attempting to map a small buffer at %d+%d\n",offset,size); */

  /* if the offset starts at a non-page boundary, we have to special case */
  if ((mod = offset % src->pagesize)) {
    GstBuffer *ret;

    mapbase = offset - mod;
    mapsize =
        ((size + mod + src->pagesize - 1) / src->pagesize) * src->pagesize;
/*    printf("not on page boundaries, resizing map to %d+%d\n",mapbase,mapsize);*/
    map = gst_filesrc_map_region (src, mapbase, mapsize);
    if (map == NULL)
      return NULL;

    ret = gst_buffer_create_sub (map, offset - mapbase, size);
    GST_BUFFER_OFFSET (ret) = GST_BUFFER_OFFSET (map) + offset - mapbase;

    gst_buffer_unref (map);

    return ret;
  }

  return gst_filesrc_map_region (src, offset, size);
}
#endif

#ifdef HAVE_MMAP
/**
 * gst_filesrc_get_mmap:
 * @src: #GstElement to get data from
 *
 * Returns: a new #GstData from the mmap'd source.
 */
static GstData *
gst_filesrc_get_mmap (GstFileSrc * src)
{
  GstBuffer *buf = NULL;
  size_t readsize, mapsize;
  off_t readend, mapstart, mapend;
  int i;

  /* calculate end pointers so we don't have to do so repeatedly later */
  readsize = src->block_size;
  readend = src->curoffset + src->block_size;   /* note this is the byte *after* the read */
  mapstart = GST_BUFFER_OFFSET (src->mapbuf);
  mapsize = GST_BUFFER_SIZE (src->mapbuf);
  mapend = mapstart + mapsize;  /* note this is the byte *after* the map */

  /* check to see if we're going to overflow the end of the file */
  if (readend > src->filelen) {
    if (!gst_filesrc_check_filesize (src) || readend > src->filelen) {
      readsize = src->filelen - src->curoffset;
      readend = src->curoffset + readsize;
    }
  }

  GST_LOG ("attempting to read %08lx, %08lx, %08lx, %08lx",
      (unsigned long) readsize, (unsigned long) readend,
      (unsigned long) mapstart, (unsigned long) mapend);

  /* if the start is past the mapstart */
  if (src->curoffset >= mapstart) {
    /* if the end is before the mapend, the buffer is in current mmap region... */
    /* ('cause by definition if readend is in the buffer, so's readstart) */
    if (readend <= mapend) {
      GST_LOG_OBJECT (src,
          "read buf %llu+%d lives in current mapbuf %lld+%d, creating subbuffer of mapbuf",
          src->curoffset, (int) readsize, mapstart, mapsize);
      buf =
          gst_buffer_create_sub (src->mapbuf, src->curoffset - mapstart,
          readsize);
      GST_BUFFER_OFFSET (buf) = src->curoffset;

      /* if the start actually is within the current mmap region, map an overlap buffer */
    } else if (src->curoffset < mapend) {
      GST_LOG_OBJECT (src,
          "read buf %llu+%d starts in mapbuf %d+%d but ends outside, creating new mmap",
          (unsigned long long) src->curoffset, (gint) readsize, (gint) mapstart,
          (gint) mapsize);
      buf = gst_filesrc_map_small_region (src, src->curoffset, readsize);
      if (buf == NULL)
        return NULL;
    }

    /* the only other option is that buffer is totally outside, which means we search for it */

    /* now we can assume that the start is *before* the current mmap region */
    /* if the readend is past mapstart, we have two options */
  } else if (readend >= mapstart) {
    /* either the read buffer overlaps the start of the mmap region */
    /* or the read buffer fully contains the current mmap region    */
    /* either way, it's really not relevant, we just create a new region anyway */
    GST_LOG_OBJECT (src,
        "read buf %llu+%d starts before mapbuf %d+%d, but overlaps it",
        (unsigned long long) src->curoffset, (gint) readsize, (gint) mapstart,
        (gint) mapsize);
    buf = gst_filesrc_map_small_region (src, src->curoffset, readsize);
    if (buf == NULL)
      return NULL;
  }

  /* then deal with the case where the read buffer is totally outside */
  if (buf == NULL) {
    /* first check to see if there's a map that covers the right region already */
    GST_LOG_OBJECT (src, "searching for mapbuf to cover %llu+%d",
        src->curoffset, (int) readsize);

    /* if the read buffer crosses a mmap region boundary, create a one-off region */
    if ((src->curoffset / src->mapsize) != (readend / src->mapsize)) {
      GST_LOG_OBJECT (src,
          "read buf %llu+%d crosses a %d-byte boundary, creating a one-off",
          src->curoffset, (int) readsize, (int) src->mapsize);
      buf = gst_filesrc_map_small_region (src, src->curoffset, readsize);
      if (buf == NULL)
        return NULL;

      /* otherwise we will create a new mmap region and set it to the default */
    } else {
      size_t mapsize;

      off_t nextmap = src->curoffset - (src->curoffset % src->mapsize);

      GST_LOG_OBJECT (src,
          "read buf %llu+%d in new mapbuf at %llu+%d, mapping and subbuffering",
          src->curoffset, readsize, nextmap, src->mapsize);
      /* first, we're done with the old mapbuf */
      gst_buffer_unref (src->mapbuf);
      mapsize = src->mapsize;

      /* double the mapsize as long as the readsize is smaller */
      while (readsize + src->curoffset > nextmap + mapsize) {
        GST_LOG_OBJECT (src, "readsize smaller then mapsize %08x %d",
            readsize, (int) mapsize);
        mapsize <<= 1;
      }
      /* create a new one */
      src->mapbuf = gst_filesrc_map_region (src, nextmap, mapsize);
      if (src->mapbuf == NULL)
        return NULL;

      /* subbuffer it */
      buf =
          gst_buffer_create_sub (src->mapbuf, src->curoffset - nextmap,
          readsize);
      GST_BUFFER_OFFSET (buf) =
          GST_BUFFER_OFFSET (src->mapbuf) + src->curoffset - nextmap;
    }
  }

  /* if we need to touch the buffer (to bring it into memory), do so */
  if (src->touch) {
    volatile guchar *p = GST_BUFFER_DATA (buf), c;

    for (i = 0; i < GST_BUFFER_SIZE (buf); i += src->pagesize)
      c = p[i];
  }

  /* we're done, return the buffer */
  g_assert (src->curoffset == GST_BUFFER_OFFSET (buf));
  src->curoffset += GST_BUFFER_SIZE (buf);
  return GST_DATA (buf);
}
#endif

static GstData *
gst_filesrc_get_read (GstFileSrc * src)
{
  GstBuffer *buf = NULL;
  size_t readsize;
  int ret;

  readsize = src->block_size;
  /* for regular files, we can use the filesize to check how much we
     can read */
  if (src->is_regular) {
    if (src->curoffset + readsize > src->filelen) {
      if (!gst_filesrc_check_filesize (src)
          || src->curoffset + readsize > src->filelen) {
        readsize = src->filelen - src->curoffset;
      }
    }
  }

  buf = gst_buffer_new_and_alloc (readsize);
  g_return_val_if_fail (buf != NULL, NULL);

  GST_LOG_OBJECT (src, "Reading %d bytes", readsize);
  ret = read (src->fd, GST_BUFFER_DATA (buf), readsize);
  if (ret < 0) {
    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL), GST_ERROR_SYSTEM);
    return NULL;
  }
  /* regular files should have given us what we expected */
  if (ret < readsize && src->is_regular) {
    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
        ("unexpected end of file."));
    return NULL;
  }
  /* other files should eos if they read 0 */
  if (ret == 0) {
    GST_DEBUG ("non-regular file hits EOS");
    gst_buffer_unref (buf);
    gst_element_set_eos (GST_ELEMENT (src));
    return GST_DATA (gst_event_new (GST_EVENT_EOS));
  }
  readsize = ret;

  GST_BUFFER_SIZE (buf) = readsize;
  GST_BUFFER_MAXSIZE (buf) = readsize;
  GST_BUFFER_OFFSET (buf) = src->curoffset;
  GST_BUFFER_OFFSET_END (buf) = src->curoffset + readsize;
  src->curoffset += readsize;

  return GST_DATA (buf);
}

static GstData *
gst_filesrc_get (GstPad * pad)
{
  GstFileSrc *src;

  g_return_val_if_fail (pad != NULL, NULL);
  src = GST_FILESRC (gst_pad_get_parent (pad));
  g_return_val_if_fail (GST_FLAG_IS_SET (src, GST_FILESRC_OPEN), NULL);

  /* check for flush */
  if (src->need_flush) {
    src->need_flush = FALSE;
    GST_DEBUG_OBJECT (src, "sending flush");
    return GST_DATA (gst_event_new_flush ());
  }
  /* check for seek */
  if (src->need_discont) {
    GstEvent *event;

    GST_DEBUG_OBJECT (src, "sending discont");
    event =
        gst_event_new_discontinuous (src->need_discont > 1, GST_FORMAT_BYTES,
        (guint64) src->curoffset, GST_FORMAT_UNDEFINED);
    src->need_discont = 0;
    return GST_DATA (event);
  }

  /* check for EOF if it's a regular file */
  if (src->is_regular) {
    g_assert (src->curoffset <= src->filelen);
    if (src->curoffset == src->filelen) {
      if (!gst_filesrc_check_filesize (src) || src->curoffset >= src->filelen) {
        GST_DEBUG_OBJECT (src, "eos %" G_GINT64_FORMAT " %" G_GINT64_FORMAT,
            src->curoffset, src->filelen);
      }
      gst_element_set_eos (GST_ELEMENT (src));
      return GST_DATA (gst_event_new (GST_EVENT_EOS));

    }
  }
#ifdef HAVE_MMAP
  if (src->using_mmap) {
    return gst_filesrc_get_mmap (src);
  } else {
    return gst_filesrc_get_read (src);
  }
#else
  return gst_filesrc_get_read (src);
#endif
}

/* TRUE if the filesize of the file was updated */
static gboolean
gst_filesrc_check_filesize (GstFileSrc * src)
{
  struct stat stat_results;

  g_return_val_if_fail (GST_FLAG_IS_SET (src, GST_FILESRC_OPEN), FALSE);

  fstat (src->fd, &stat_results);
  GST_DEBUG_OBJECT (src,
      "checked filesize on %s (was %" G_GUINT64_FORMAT ", is %" G_GUINT64_FORMAT
      ")", src->filename, src->filelen, (guint64) stat_results.st_size);
  if (src->filelen == (guint64) stat_results.st_size)
    return FALSE;
  src->filelen = stat_results.st_size;
  return TRUE;
}

/* open the file and mmap it, necessary to go to READY state */
static gboolean
gst_filesrc_open_file (GstFileSrc * src)
{
  g_return_val_if_fail (!GST_FLAG_IS_SET (src, GST_FILESRC_OPEN), FALSE);

  if (src->filename == NULL || src->filename[0] == '\0') {
    GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND,
        (_("No file name specified for reading.")), (NULL));
    return FALSE;
  }


  GST_INFO_OBJECT (src, "opening file %s", src->filename);

  /* open the file */
  src->fd = open (src->filename, O_RDONLY | O_BINARY);
  if (src->fd < 0) {
    if (errno == ENOENT)
      GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND, (NULL),
          ("No such file \"%s\"", src->filename));
    else
      GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
          (_("Could not open file \"%s\" for reading."), src->filename),
          GST_ERROR_SYSTEM);
    return FALSE;
  } else {
    /* check if it is a regular file, otherwise bail out */
    struct stat stat_results;

    fstat (src->fd, &stat_results);

    if (S_ISDIR (stat_results.st_mode)) {
      GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
          (_("\"%s\" is a directory."), src->filename), (NULL));
      close (src->fd);
      return FALSE;
    }
    if (S_ISSOCK (stat_results.st_mode)) {
      GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
          (_("File \"%s\" is a socket."), src->filename), (NULL));
      close (src->fd);
      return FALSE;
    }

    /* find the file length */
    src->filelen = stat_results.st_size;

    src->using_mmap = FALSE;

    /* record if it's a regular (hence seekable and lengthable) file */
    if (S_ISREG (stat_results.st_mode))
      src->is_regular = TRUE;
#ifdef HAVE_MMAP
    /* FIXME: maybe we should only try to mmap if it's a regular file */
    /* allocate the first mmap'd region if it's a regular file ? */
    src->mapbuf = gst_filesrc_map_region (src, 0, src->mapsize);
    if (src->mapbuf != NULL) {
      GST_DEBUG_OBJECT (src, "using mmap for file");
      src->using_mmap = TRUE;
    }
#endif

    src->curoffset = 0;

    GST_FLAG_SET (src, GST_FILESRC_OPEN);
  }
  return TRUE;
}

/* unmap and close the file */
static void
gst_filesrc_close_file (GstFileSrc * src)
{
  g_return_if_fail (GST_FLAG_IS_SET (src, GST_FILESRC_OPEN));

  /* close the file */
  close (src->fd);

  /* zero out a lot of our state */
  src->fd = 0;
  src->filelen = 0;
  src->curoffset = 0;
  src->is_regular = FALSE;

  if (src->mapbuf) {
    gst_buffer_unref (src->mapbuf);
    src->mapbuf = NULL;
  }

  GST_FLAG_UNSET (src, GST_FILESRC_OPEN);
}


static GstElementStateReturn
gst_filesrc_change_state (GstElement * element)
{
  GstFileSrc *src = GST_FILESRC (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    case GST_STATE_READY_TO_PAUSED:
      if (!GST_FLAG_IS_SET (element, GST_FILESRC_OPEN)) {
        if (!gst_filesrc_open_file (GST_FILESRC (element)))
          return GST_STATE_FAILURE;
      }
      src->need_discont = 2;
      break;
    case GST_STATE_PAUSED_TO_READY:
      if (GST_FLAG_IS_SET (element, GST_FILESRC_OPEN))
        gst_filesrc_close_file (GST_FILESRC (element));
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
gst_filesrc_srcpad_query (GstPad * pad, GstQueryType type,
    GstFormat * format, gint64 * value)
{
  GstFileSrc *src = GST_FILESRC (GST_PAD_PARENT (pad));

  if (*format == GST_FORMAT_DEFAULT)
    *format = GST_FORMAT_BYTES;

  switch (type) {
    case GST_QUERY_TOTAL:
      if (*format != GST_FORMAT_BYTES) {
        return FALSE;
      }
      if (!src->is_regular)
        return FALSE;
      gst_filesrc_check_filesize (src);
      *value = src->filelen;
      break;
    case GST_QUERY_POSITION:
      switch (*format) {
        case GST_FORMAT_BYTES:
          *value = src->curoffset;
          break;
        case GST_FORMAT_PERCENT:
          if (src->filelen == 0)
            return FALSE;
          if (!src->is_regular)
            return FALSE;
          *value = src->curoffset * GST_FORMAT_PERCENT_MAX / src->filelen;
          break;
        default:
          return FALSE;
      }
      break;
    default:
      return FALSE;
      break;
  }
  return TRUE;
}

static gboolean
gst_filesrc_srcpad_event (GstPad * pad, GstEvent * event)
{
  GstFileSrc *src = GST_FILESRC (GST_PAD_PARENT (pad));

  GST_DEBUG_OBJECT (src, "received event type %d", GST_EVENT_TYPE (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      gint64 offset;

      if (GST_EVENT_SEEK_FORMAT (event) != GST_FORMAT_BYTES &&
          GST_EVENT_SEEK_FORMAT (event) != GST_FORMAT_DEFAULT) {
        goto error;
      }
      if (!src->is_regular) {
        GST_DEBUG ("can't handle seek on a non-regular file");
        goto error;
      }

      offset = GST_EVENT_SEEK_OFFSET (event);

      switch (GST_EVENT_SEEK_METHOD (event)) {
        case GST_SEEK_METHOD_SET:
          if (offset < 0) {
            goto error;
          } else if (offset > src->filelen && (!gst_filesrc_check_filesize (src)
                  || offset > src->filelen)) {
            src->curoffset = src->filelen;
          } else {
            src->curoffset = offset;
          }
          GST_DEBUG_OBJECT (src, "seek set pending to %" G_GINT64_FORMAT,
              src->curoffset);
          break;
        case GST_SEEK_METHOD_CUR:
          if (offset + src->curoffset > src->filelen &&
              (!gst_filesrc_check_filesize (src)
                  || offset + src->curoffset > src->filelen)) {
            src->curoffset = src->filelen;
          } else if (offset + src->curoffset < 0) {
            src->curoffset = 0;
          } else {
            src->curoffset += offset;
          }
          GST_DEBUG_OBJECT (src, "seek cur pending to %" G_GINT64_FORMAT,
              src->curoffset);
          break;
        case GST_SEEK_METHOD_END:
          if (offset > 0) {
            goto error;
          } else if (offset > src->filelen && (!gst_filesrc_check_filesize (src)
                  || offset > src->filelen)) {
            src->curoffset = 0;
          } else {
            src->curoffset = src->filelen + offset;
          }
          GST_DEBUG_OBJECT (src, "seek end pending to %" G_GINT64_FORMAT,
              src->curoffset);
          break;
        default:
          goto error;
          break;
      }
      src->need_discont = 1;
      src->need_flush = GST_EVENT_SEEK_FLAGS (event) & GST_SEEK_FLAG_FLUSH;
      break;
    }
    case GST_EVENT_SIZE:
      if (GST_EVENT_SIZE_FORMAT (event) != GST_FORMAT_BYTES) {
        goto error;
      }
      src->block_size = GST_EVENT_SIZE_VALUE (event);
      g_object_notify (G_OBJECT (src), "blocksize");
      break;
    case GST_EVENT_FLUSH:
      src->need_flush = TRUE;
      break;
    default:
      goto error;
      break;
  }
  gst_event_unref (event);
  return TRUE;

error:
  gst_event_unref (event);
  return FALSE;
}

/*** GSTURIHANDLER INTERFACE *************************************************/

static guint
gst_filesrc_uri_get_type (void)
{
  return GST_URI_SRC;
}
static gchar **
gst_filesrc_uri_get_protocols (void)
{
  static gchar *protocols[] = { "file", NULL };

  return protocols;
}
static const gchar *
gst_filesrc_uri_get_uri (GstURIHandler * handler)
{
  GstFileSrc *src = GST_FILESRC (handler);

  return src->uri;
}

static gboolean
gst_filesrc_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  gchar *protocol, *location;
  gboolean ret;
  GstFileSrc *src = GST_FILESRC (handler);

  protocol = gst_uri_get_protocol (uri);
  if (strcmp (protocol, "file") != 0) {
    g_free (protocol);
    return FALSE;
  }
  g_free (protocol);
  location = gst_uri_get_location (uri);
  ret = gst_filesrc_set_location (src, location);
  g_free (location);

  return ret;
}

static void
gst_filesrc_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_filesrc_uri_get_type;
  iface->get_protocols = gst_filesrc_uri_get_protocols;
  iface->get_uri = gst_filesrc_uri_get_uri;
  iface->set_uri = gst_filesrc_uri_set_uri;
}
