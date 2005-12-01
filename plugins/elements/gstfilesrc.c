/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *               2000,2005 Wim Taymans <wim@fluendo.com>
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
/**
 * SECTION:element-filesrc
 * @short_description: read from arbitrary point in a file
 * @see_also: #GstFileSrc
 *
 * Read data from a file in the local file system. The implementation is using
 * mmap(2) to read chunks from the file in an efficient way.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>
#include "gstfilesrc.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#ifdef HAVE_MMAP
# include <sys/mman.h>
#endif

#ifdef HAVE_WIN32
#  include <io.h>               /* lseek, open, close, read */
#endif

#include <errno.h>
#include <string.h>

#include "../../gst/gst-i18n-lib.h"

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

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


GST_DEBUG_CATEGORY_STATIC (gst_file_src_debug);
#define GST_CAT_DEFAULT gst_file_src_debug

static GstElementDetails gst_file_src_details =
GST_ELEMENT_DETAILS ("File Source",
    "Source/File",
    "Read from arbitrary point in a file",
    "Erik Walthinsen <omega@cse.ogi.edu>");

/* FileSrc signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_BLOCKSIZE	4*1024
#define DEFAULT_MMAPSIZE	4*1024*1024
#define DEFAULT_TOUCH		FALSE

enum
{
  ARG_0,
  ARG_LOCATION,
  ARG_FD,
  ARG_MMAPSIZE,
  ARG_TOUCH
};

static void gst_file_src_finalize (GObject * object);

static void gst_file_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_file_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_file_src_start (GstBaseSrc * basesrc);
static gboolean gst_file_src_stop (GstBaseSrc * basesrc);

static gboolean gst_file_src_is_seekable (GstBaseSrc * src);
static gboolean gst_file_src_get_size (GstBaseSrc * src, guint64 * size);
static GstFlowReturn gst_file_src_create (GstBaseSrc * src, guint64 offset,
    guint length, GstBuffer ** buffer);

static void gst_file_src_uri_handler_init (gpointer g_iface,
    gpointer iface_data);

static void
_do_init (GType filesrc_type)
{
  static const GInterfaceInfo urihandler_info = {
    gst_file_src_uri_handler_init,
    NULL,
    NULL
  };

  g_type_add_interface_static (filesrc_type, GST_TYPE_URI_HANDLER,
      &urihandler_info);
  GST_DEBUG_CATEGORY_INIT (gst_file_src_debug, "filesrc", 0, "filesrc element");
}

GST_BOILERPLATE_FULL (GstFileSrc, gst_file_src, GstBaseSrc, GST_TYPE_BASE_SRC,
    _do_init);

static void
gst_file_src_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));

  gst_element_class_set_details (gstelement_class, &gst_file_src_details);
}

static void
gst_file_src_class_init (GstFileSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;

  gobject_class->set_property = gst_file_src_set_property;
  gobject_class->get_property = gst_file_src_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FD,
      g_param_spec_int ("fd", "File-descriptor",
          "File-descriptor for the file being mmap()d", 0, G_MAXINT, 0,
          G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LOCATION,
      g_param_spec_string ("location", "File Location",
          "Location of the file to read", NULL, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MMAPSIZE,
      g_param_spec_ulong ("mmapsize", "mmap() Block Size",
          "Size in bytes of mmap()d regions", 0, G_MAXULONG, DEFAULT_MMAPSIZE,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_TOUCH,
      g_param_spec_boolean ("touch", "Touch read data",
          "Touch data to force disk read", DEFAULT_TOUCH, G_PARAM_READWRITE));

  gobject_class->finalize = gst_file_src_finalize;

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_file_src_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_file_src_stop);
  gstbasesrc_class->is_seekable = GST_DEBUG_FUNCPTR (gst_file_src_is_seekable);
  gstbasesrc_class->get_size = GST_DEBUG_FUNCPTR (gst_file_src_get_size);
  gstbasesrc_class->create = GST_DEBUG_FUNCPTR (gst_file_src_create);

  if (sizeof (off_t) < 8) {
    GST_LOG ("No large file support, sizeof (off_t) = %u!", sizeof (off_t));
  }
}

static void
gst_file_src_init (GstFileSrc * src, GstFileSrcClass * g_class)
{
#ifdef HAVE_MMAP
  src->pagesize = getpagesize ();
#endif

  src->filename = NULL;
  src->fd = 0;
  src->uri = NULL;

  src->touch = DEFAULT_TOUCH;

  src->mapbuf = NULL;
  src->mapsize = DEFAULT_MMAPSIZE;      /* default is 4MB */

  src->is_regular = FALSE;
}

static void
gst_file_src_finalize (GObject * object)
{
  GstFileSrc *src;

  src = GST_FILE_SRC (object);

  g_free (src->filename);
  g_free (src->uri);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_file_src_set_location (GstFileSrc * src, const gchar * location)
{
  /* the element must be stopped in order to do this */
  GST_STATE_LOCK (src);
  {
    GstState state;

    state = GST_STATE (src);
    if (state != GST_STATE_READY && state != GST_STATE_NULL)
      goto wrong_state;
  }
  GST_STATE_UNLOCK (src);

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

  /* ERROR */
wrong_state:
  {
    GST_DEBUG_OBJECT (src, "setting location in wrong state");
    GST_STATE_UNLOCK (src);
    return FALSE;
  }
}

static void
gst_file_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstFileSrc *src;

  g_return_if_fail (GST_IS_FILE_SRC (object));

  src = GST_FILE_SRC (object);

  switch (prop_id) {
    case ARG_LOCATION:
      gst_file_src_set_location (src, g_value_get_string (value));
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
gst_file_src_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstFileSrc *src;

  g_return_if_fail (GST_IS_FILE_SRC (object));

  src = GST_FILE_SRC (object);

  switch (prop_id) {
    case ARG_LOCATION:
      g_value_set_string (value, src->filename);
      break;
    case ARG_FD:
      g_value_set_int (value, src->fd);
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

/***
 * mmap code below
 */

#ifdef HAVE_MMAP

/* GstMmapBuffer */

typedef struct _GstMmapBuffer GstMmapBuffer;
typedef struct _GstMmapBufferClass GstMmapBufferClass;

#define GST_TYPE_MMAP_BUFFER                         (gst_mmap_buffer_get_type())

#define GST_IS_MMAP_BUFFER(obj)  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MMAP_BUFFER))
#define GST_IS_MMAP_BUFFER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MMAP_BUFFER))
#define GST_MMAP_BUFFER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_MMAP_BUFFER, GstMmapBufferClass))
#define GST_MMAP_BUFFER(obj)     (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MMAP_BUFFER, GstMmapBuffer))
#define GST_MMAP_BUFFER_CLASS(klass)  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MMAP_BUFFER, GstMmapBufferClass))



struct _GstMmapBuffer
{
  GstBuffer buffer;

  GstFileSrc *filesrc;
};

struct _GstMmapBufferClass
{
  GstBufferClass buffer_class;
};

static void gst_mmap_buffer_init (GTypeInstance * instance, gpointer g_class);
static void gst_mmap_buffer_class_init (gpointer g_class, gpointer class_data);
static void gst_mmap_buffer_finalize (GstMmapBuffer * mmap_buffer);

static GType
gst_mmap_buffer_get_type (void)
{
  static GType _gst_mmap_buffer_type;

  if (G_UNLIKELY (_gst_mmap_buffer_type == 0)) {
    static const GTypeInfo mmap_buffer_info = {
      sizeof (GstMmapBufferClass),
      NULL,
      NULL,
      gst_mmap_buffer_class_init,
      NULL,
      NULL,
      sizeof (GstMmapBuffer),
      0,
      gst_mmap_buffer_init,
      NULL
    };

    _gst_mmap_buffer_type = g_type_register_static (GST_TYPE_BUFFER,
        "GstMmapBuffer", &mmap_buffer_info, 0);
  }
  return _gst_mmap_buffer_type;
}

static void
gst_mmap_buffer_class_init (gpointer g_class, gpointer class_data)
{
  GstMiniObjectClass *mini_object_class = GST_MINI_OBJECT_CLASS (g_class);

  mini_object_class->finalize =
      (GstMiniObjectFinalizeFunction) gst_mmap_buffer_finalize;
}

static void
gst_mmap_buffer_init (GTypeInstance * instance, gpointer g_class)
{
  GstBuffer *buf = (GstBuffer *) instance;

  GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_READONLY);
  /* before we re-enable this flag, we probably need to fix _copy()
   * _make_writable(), etc. in GstMiniObject/GstBuffer as well */
  /* GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_ORIGINAL); */
}

static void
gst_mmap_buffer_finalize (GstMmapBuffer * mmap_buffer)
{
  guint size;
  gpointer data;
  guint64 offset;
  GstFileSrc *src;
  GstBuffer *buffer = GST_BUFFER (mmap_buffer);

  /* get info */
  size = GST_BUFFER_SIZE (buffer);
  offset = GST_BUFFER_OFFSET (buffer);
  data = GST_BUFFER_DATA (buffer);
  src = mmap_buffer->filesrc;

  GST_LOG ("freeing mmap()d buffer at %" G_GUINT64_FORMAT "+%u", offset, size);

#ifdef MADV_DONTNEED
  /* madvise to tell the kernel what to do with it */
  if (madvise (data, size, MADV_DONTNEED) < 0) {
    GST_WARNING_OBJECT (src, "warning: madvise failed: %s", strerror (errno));
  }
#endif

  /* now unmap the memory */
  if (munmap (data, size) < 0) {
    GST_WARNING_OBJECT (src, "warning: munmap failed: %s", strerror (errno));
  }

  /* cast to unsigned long, since there's no gportable way to print
   * guint64 as hex */
  GST_LOG ("unmapped region %08lx+%08lx at %p",
      (gulong) offset, (gulong) size, data);
}

static GstBuffer *
gst_file_src_map_region (GstFileSrc * src, off_t offset, size_t size,
    gboolean testonly)
{
  GstBuffer *buf;
  void *mmapregion;

  g_return_val_if_fail (offset >= 0, NULL);

  GST_LOG_OBJECT (src, "mapping region %08llx+%08lx from file into memory",
      offset, (gulong) size);

  mmapregion = mmap (NULL, size, PROT_READ, MAP_SHARED, src->fd, offset);

  if (mmapregion == NULL || mmapregion == MAP_FAILED)
    goto mmap_failed;

  GST_LOG_OBJECT (src, "mapped region %08lx+%08lx from file into memory at %p",
      (gulong) offset, (gulong) size, mmapregion);

  /* time to allocate a new mapbuf */
  buf = (GstBuffer *) gst_mini_object_new (GST_TYPE_MMAP_BUFFER);
  /* mmap() the data into this new buffer */
  GST_BUFFER_DATA (buf) = mmapregion;
  GST_MMAP_BUFFER (buf)->filesrc = src;

#ifdef MADV_SEQUENTIAL
  /* madvise to tell the kernel what to do with it */
  if (madvise (mmapregion, size, MADV_SEQUENTIAL) < 0) {
    GST_WARNING_OBJECT (src, "warning: madvise failed: %s", strerror (errno));
  }
#endif

  /* fill in the rest of the fields */
  GST_BUFFER_SIZE (buf) = size;
  GST_BUFFER_OFFSET (buf) = offset;
  GST_BUFFER_OFFSET_END (buf) = offset + size;
  GST_BUFFER_TIMESTAMP (buf) = GST_CLOCK_TIME_NONE;

  return buf;

  /* ERROR */
mmap_failed:
  {
    if (!testonly) {
      GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
          ("mmap (0x%08lx, %d, 0x%llx) failed: %s",
              (gulong) size, src->fd, offset, strerror (errno)));
    }
    return NULL;
  }
}

static GstBuffer *
gst_file_src_map_small_region (GstFileSrc * src, off_t offset, size_t size)
{
  GstBuffer *ret;
  off_t mod;
  guint pagesize;

  GST_LOG_OBJECT (src,
      "attempting to map a small buffer at %llu+%d",
      (unsigned long long) offset, (gint) size);

  pagesize = src->pagesize;

  mod = offset % pagesize;

  /* if the offset starts at a non-page boundary, we have to special case */
  if (mod != 0) {
    size_t mapsize;
    off_t mapbase;
    GstBuffer *map;

    mapbase = offset - mod;
    mapsize = ((size + mod + pagesize - 1) / pagesize) * pagesize;

    GST_LOG_OBJECT (src,
        "not on page boundaries, resizing to map to %llu+%d",
        (unsigned long long) mapbase, (gint) mapsize);

    map = gst_file_src_map_region (src, mapbase, mapsize, FALSE);
    if (map == NULL)
      return NULL;

    ret = gst_buffer_create_sub (map, offset - mapbase, size);
    GST_BUFFER_OFFSET (ret) = GST_BUFFER_OFFSET (map) + offset - mapbase;

    gst_buffer_unref (map);
  } else {
    ret = gst_file_src_map_region (src, offset, size, FALSE);
  }

  return ret;
}

static GstFlowReturn
gst_file_src_create_mmap (GstFileSrc * src, guint64 offset, guint length,
    GstBuffer ** buffer)
{
  GstBuffer *buf = NULL;
  size_t readsize, mapsize;
  off_t readend, mapstart, mapend;
  int i;

  /* calculate end pointers so we don't have to do so repeatedly later */
  readsize = length;
  readend = offset + readsize;  /* note this is the byte *after* the read */

  mapstart = GST_BUFFER_OFFSET (src->mapbuf);
  mapsize = GST_BUFFER_SIZE (src->mapbuf);
  mapend = mapstart + mapsize;  /* note this is the byte *after* the map */

  GST_LOG ("attempting to read %08lx, %08lx, %08lx, %08lx",
      (unsigned long) readsize, (unsigned long) readend,
      (unsigned long) mapstart, (unsigned long) mapend);

  /* if the start is past the mapstart */
  if (offset >= mapstart) {
    /* if the end is before the mapend, the buffer is in current mmap region... */
    /* ('cause by definition if readend is in the buffer, so's readstart) */
    if (readend <= mapend) {
      GST_LOG_OBJECT (src,
          "read buf %llu+%d lives in current mapbuf %lld+%d, creating subbuffer of mapbuf",
          offset, (int) readsize, mapstart, mapsize);
      buf = gst_buffer_create_sub (src->mapbuf, offset - mapstart, readsize);
      GST_BUFFER_OFFSET (buf) = offset;

      /* if the start actually is within the current mmap region, map an overlap buffer */
    } else if (offset < mapend) {
      GST_LOG_OBJECT (src,
          "read buf %llu+%d starts in mapbuf %d+%d but ends outside, creating new mmap",
          (unsigned long long) offset, (gint) readsize, (gint) mapstart,
          (gint) mapsize);
      buf = gst_file_src_map_small_region (src, offset, readsize);
      if (buf == NULL)
        goto could_not_mmap;
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
        (unsigned long long) offset, (gint) readsize, (gint) mapstart,
        (gint) mapsize);
    buf = gst_file_src_map_small_region (src, offset, readsize);
    if (buf == NULL)
      goto could_not_mmap;
  }

  /* then deal with the case where the read buffer is totally outside */
  if (buf == NULL) {
    /* first check to see if there's a map that covers the right region already */
    GST_LOG_OBJECT (src, "searching for mapbuf to cover %llu+%d",
        offset, (int) readsize);

    /* if the read buffer crosses a mmap region boundary, create a one-off region */
    if ((offset / src->mapsize) != (readend / src->mapsize)) {
      GST_LOG_OBJECT (src,
          "read buf %llu+%d crosses a %d-byte boundary, creating a one-off",
          offset, (int) readsize, (int) src->mapsize);
      buf = gst_file_src_map_small_region (src, offset, readsize);
      if (buf == NULL)
        goto could_not_mmap;

      /* otherwise we will create a new mmap region and set it to the default */
    } else {
      size_t mapsize;

      off_t nextmap = offset - (offset % src->mapsize);

      GST_LOG_OBJECT (src,
          "read buf %llu+%d in new mapbuf at %llu+%d, mapping and subbuffering",
          offset, readsize, nextmap, src->mapsize);
      /* first, we're done with the old mapbuf */
      gst_buffer_unref (src->mapbuf);
      mapsize = src->mapsize;

      /* double the mapsize as long as the readsize is smaller */
      while (readsize + offset > nextmap + mapsize) {
        GST_LOG_OBJECT (src, "readsize smaller then mapsize %08x %d",
            readsize, (int) mapsize);
        mapsize <<= 1;
      }
      /* create a new one */
      src->mapbuf = gst_file_src_map_region (src, nextmap, mapsize, FALSE);
      if (src->mapbuf == NULL)
        goto could_not_mmap;

      /* subbuffer it */
      buf = gst_buffer_create_sub (src->mapbuf, offset - nextmap, readsize);
      GST_BUFFER_OFFSET (buf) =
          GST_BUFFER_OFFSET (src->mapbuf) + offset - nextmap;
    }
  }

  /* if we need to touch the buffer (to bring it into memory), do so */
  if (src->touch) {
    volatile guchar *p = GST_BUFFER_DATA (buf), c;

    /* read first byte of each page */
    for (i = 0; i < GST_BUFFER_SIZE (buf); i += src->pagesize)
      c = p[i];
  }

  /* we're done, return the buffer */
  *buffer = buf;

  return GST_FLOW_OK;

  /* ERROR */
could_not_mmap:
  {
    return GST_FLOW_ERROR;
  }
}
#endif

/***
 * read code below
 * that is to say, you shouldn't read the code below, but the code that reads
 * stuff is below.  Well, you shouldn't not read the code below, feel free
 * to read it of course.  It's just that "read code below" is a pretty crappy
 * documentation string because it sounds like we're expecting you to read
 * the code to understand what it does, which, while true, is really not
 * the sort of attitude we want to be advertising.  No sir.
 *
 */

static GstFlowReturn
gst_file_src_create_read (GstFileSrc * src, guint64 offset, guint length,
    GstBuffer ** buffer)
{
  int ret;
  GstBuffer *buf;

  if (src->read_position != offset) {
    off_t res;

    res = lseek (src->fd, offset, SEEK_SET);
    if (res < 0 || res != offset)
      goto seek_failed;
  }

  buf = gst_buffer_new_and_alloc (length);

  GST_LOG_OBJECT (src, "Reading %d bytes", length);
  ret = read (src->fd, GST_BUFFER_DATA (buf), length);
  if (ret < 0)
    goto could_not_read;

  /* regular files should have given us what we expected */
  if ((guint) ret < length && src->is_regular)
    goto unexpected_eos;

  /* other files should eos if they read 0 */
  if (ret == 0) {
    GST_DEBUG ("non-regular file hits EOS");
    gst_buffer_unref (buf);
    return GST_FLOW_UNEXPECTED;
  }
  length = ret;

  GST_BUFFER_SIZE (buf) = length;
  GST_BUFFER_OFFSET (buf) = offset;
  GST_BUFFER_OFFSET_END (buf) = offset + length;

  *buffer = buf;

  src->read_position += length;

  return GST_FLOW_OK;

  /* ERROR */
seek_failed:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL), GST_ERROR_SYSTEM);
    return GST_FLOW_ERROR;
  }
could_not_read:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL), GST_ERROR_SYSTEM);
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
unexpected_eos:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
        ("unexpected end of file."));
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_file_src_create (GstBaseSrc * basesrc, guint64 offset, guint length,
    GstBuffer ** buffer)
{
  GstFileSrc *src;
  GstFlowReturn ret;

  src = GST_FILE_SRC (basesrc);

#ifdef HAVE_MMAP
  if (src->using_mmap) {
    ret = gst_file_src_create_mmap (src, offset, length, buffer);
  } else {
    ret = gst_file_src_create_read (src, offset, length, buffer);
  }
#else
  ret = gst_file_src_create_read (src, offset, length, buffer);
#endif

  return ret;
}

static gboolean
gst_file_src_is_seekable (GstBaseSrc * basesrc)
{
  GstFileSrc *src = GST_FILE_SRC (basesrc);

  return src->seekable;
}

static gboolean
gst_file_src_get_size (GstBaseSrc * basesrc, guint64 * size)
{
  struct stat stat_results;
  GstFileSrc *src;

  src = GST_FILE_SRC (basesrc);

  if (!src->seekable) {
    /* If it isn't seekable, we won't know the length (but fstat will still
     * succeed, and wrongly say our length is zero. */
    return FALSE;
  }

  if (fstat (src->fd, &stat_results) < 0)
    goto could_not_stat;

  *size = stat_results.st_size;

  return TRUE;

  /* ERROR */
could_not_stat:
  {
    return FALSE;
  }
}

/* open the file and mmap it, necessary to go to READY state */
static gboolean
gst_file_src_start (GstBaseSrc * basesrc)
{
  GstFileSrc *src = GST_FILE_SRC (basesrc);
  struct stat stat_results;

  if (src->filename == NULL || src->filename[0] == '\0')
    goto no_filename;

  GST_INFO_OBJECT (src, "opening file %s", src->filename);

  /* open the file */
  src->fd = open (src->filename, O_RDONLY | O_BINARY);
  if (src->fd < 0)
    goto open_failed;

  /* check if it is a regular file, otherwise bail out */
  if (fstat (src->fd, &stat_results) < 0)
    goto no_stat;

  if (S_ISDIR (stat_results.st_mode))
    goto was_directory;

  if (S_ISSOCK (stat_results.st_mode))
    goto was_socket;

  src->using_mmap = FALSE;
  src->read_position = 0;

  /* record if it's a regular (hence seekable and lengthable) file */
  if (S_ISREG (stat_results.st_mode))
    src->is_regular = TRUE;

#ifdef HAVE_MMAP
  /* FIXME: maybe we should only try to mmap if it's a regular file */
  /* allocate the first mmap'd region if it's a regular file ? */
  src->mapbuf = gst_file_src_map_region (src, 0, src->mapsize, TRUE);
  if (src->mapbuf != NULL) {
    GST_DEBUG_OBJECT (src, "using mmap for file");
    src->using_mmap = TRUE;
    src->seekable = TRUE;
  } else
#endif
  {
    /* If not in mmap mode, we need to check if the underlying file is
     * seekable. */
    off_t res = lseek (src->fd, 0, SEEK_CUR);

    if (res < 0) {
      GST_LOG_OBJECT (src, "disabling seeking, not in mmap mode and lseek "
          "failed: %s", strerror (errno));
      src->seekable = FALSE;
    } else {
      src->seekable = TRUE;
    }
  }

  /* We can only really do seeking on regular files - for other file types, we
   * don't know their length, so seeking isn't useful/meaningful */
  src->seekable = src->seekable && src->is_regular;

  return TRUE;

  /* ERROR */
no_filename:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND,
        (_("No file name specified for reading.")), (NULL));
    return FALSE;
  }
open_failed:
  {
    switch (errno) {
      case ENOENT:
        GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND, (NULL),
            ("No such file \"%s\"", src->filename));
        break;
      default:
        GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
            (_("Could not open file \"%s\" for reading: %s."), src->filename,
                strerror (errno)), GST_ERROR_SYSTEM);
        break;
    }
    return FALSE;
  }
no_stat:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        (_("could not get info on \"%s\"."), src->filename), (NULL));
    close (src->fd);
    return FALSE;
  }
was_directory:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        (_("\"%s\" is a directory."), src->filename), (NULL));
    close (src->fd);
    return FALSE;
  }
was_socket:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ,
        (_("File \"%s\" is a socket."), src->filename), (NULL));
    close (src->fd);
    return FALSE;
  }
}

/* unmap and close the file */
static gboolean
gst_file_src_stop (GstBaseSrc * basesrc)
{
  GstFileSrc *src = GST_FILE_SRC (basesrc);

  /* close the file */
  close (src->fd);

  /* zero out a lot of our state */
  src->fd = 0;
  src->is_regular = FALSE;

  if (src->mapbuf) {
    gst_buffer_unref (src->mapbuf);
    src->mapbuf = NULL;
  }

  return TRUE;
}

/*** GSTURIHANDLER INTERFACE *************************************************/

static guint
gst_file_src_uri_get_type (void)
{
  return GST_URI_SRC;
}
static gchar **
gst_file_src_uri_get_protocols (void)
{
  static gchar *protocols[] = { "file", NULL };

  return protocols;
}
static const gchar *
gst_file_src_uri_get_uri (GstURIHandler * handler)
{
  GstFileSrc *src = GST_FILE_SRC (handler);

  return src->uri;
}

static gboolean
gst_file_src_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  gchar *protocol, *location;
  gboolean ret;
  GstFileSrc *src = GST_FILE_SRC (handler);

  protocol = gst_uri_get_protocol (uri);
  if (strcmp (protocol, "file") != 0) {
    g_free (protocol);
    return FALSE;
  }
  g_free (protocol);
  location = gst_uri_get_location (uri);
  ret = gst_file_src_set_location (src, location);
  g_free (location);

  return ret;
}

static void
gst_file_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_file_src_uri_get_type;
  iface->get_protocols = gst_file_src_uri_get_protocols;
  iface->get_uri = gst_file_src_uri_get_uri;
  iface->set_uri = gst_file_src_uri_set_uri;
}
