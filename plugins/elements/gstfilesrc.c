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

#include <gst/gst.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>


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


GstElementDetails gst_filesrc_details = {
  "File Source",
  "Source/File",
  "Read from arbitrary point in a file",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};


#define GST_TYPE_FILESRC \
  (gst_filesrc_get_type())
#define GST_FILESRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FILESRC,GstFileSrc))
#define GST_FILESRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FILESRC,GstFileSrcClass)) 
#define GST_IS_FILESRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FILESRC))
#define GST_IS_FILESRC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FILESRC))

typedef enum {
  GST_FILESRC_OPEN              = GST_ELEMENT_FLAG_LAST,

  GST_FILESRC_FLAG_LAST = GST_ELEMENT_FLAG_LAST + 2,
} GstFileSrcFlags;

typedef struct _GstFileSrc GstFileSrc;
typedef struct _GstFileSrcClass GstFileSrcClass;

struct _GstFileSrc {
  GstElement element;
  GstPad *srcpad;

  guint pagesize;			// system page size
 
  gchar *filename;			// filename
  gint fd;				// open file descriptor
  off_t filelen;			// what's the file length?

  off_t curoffset;			// current offset in file
  off_t block_size;			// bytes per read
  gboolean touch;			// whether to touch every page

  GstBuffer *mapbuf;
  off_t mapsize;

  GTree *map_regions;
  GMutex *map_regions_lock;
};

struct _GstFileSrcClass {
  GstElementClass parent_class;
};


/* FileSrc signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_LOCATION,
  ARG_FILESIZE,
  ARG_FD,
  ARG_BLOCKSIZE,
  ARG_OFFSET,
  ARG_MAPSIZE,
};


static void		gst_filesrc_class_init	(GstFileSrcClass *klass);
static void		gst_filesrc_init	(GstFileSrc *filesrc);

static void		gst_filesrc_set_property	(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void		gst_filesrc_get_property	(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static GstBuffer *	gst_filesrc_get		(GstPad *pad);
static gboolean		gst_filesrc_srcpad_event	(GstPad *pad, GstEventType event, gint64 location, guint32 data);

static GstElementStateReturn	gst_filesrc_change_state	(GstElement *element);


static GstElementClass *parent_class = NULL;
//static guint gst_filesrc_signals[LAST_SIGNAL] = { 0 };

GType
gst_filesrc_get_type(void)
{
  static GType filesrc_type = 0;

  if (!filesrc_type) {
    static const GTypeInfo filesrc_info = {
      sizeof(GstFileSrcClass),      NULL,
      NULL,
      (GClassInitFunc)gst_filesrc_class_init,
      NULL,
      NULL,
      sizeof(GstFileSrc),
      0,
      (GInstanceInitFunc)gst_filesrc_init,
    };
    filesrc_type = g_type_register_static (GST_TYPE_ELEMENT, "GstFileSrc", &filesrc_info, 0);
  }
  return filesrc_type;
}

static void
gst_filesrc_class_init (GstFileSrcClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_LOCATION,
    g_param_spec_string("location","File Location","Location of the file to read",
                        NULL,G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_FILESIZE,
    g_param_spec_ulong("filesize","File Size","Size of the file being read",
                       0,G_MAXULONG,0,G_PARAM_READABLE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_FD,
    g_param_spec_int("fd","File-descriptor","File-descriptor for the file being read",
                     0,G_MAXINT,0,G_PARAM_READABLE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_BLOCKSIZE,
    g_param_spec_ulong("blocksize","Block Size","Block size to read per buffer",
                       0,G_MAXULONG,4096,G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_OFFSET,
    g_param_spec_ulong("offset","File Offset","Byte offset of current read pointer",
                       0,G_MAXULONG,0,G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_MAPSIZE,
    g_param_spec_ulong("mmapsize","mmap() Block Size","Size in bytes of mmap()d regions",
                       0,G_MAXULONG,4*1048576,G_PARAM_READWRITE));

  gobject_class->set_property = gst_filesrc_set_property;
  gobject_class->get_property = gst_filesrc_get_property;

  gstelement_class->change_state = gst_filesrc_change_state;
}

static gint
gst_filesrc_bufcmp (gconstpointer a, gconstpointer b)
{
//  GstBuffer *bufa = (GstBuffer *)a, *bufb = (GstBuffer *)b;

  // sort first by offset, then in reverse by size
  if (GST_BUFFER_OFFSET(a) < GST_BUFFER_OFFSET(b)) return -1;
  else if (GST_BUFFER_OFFSET(a) > GST_BUFFER_OFFSET(b)) return 1;
  else if (GST_BUFFER_SIZE(a) > GST_BUFFER_SIZE(b)) return -1;
  else if (GST_BUFFER_SIZE(a) < GST_BUFFER_SIZE(b)) return 1;
  else return 0;
}

static void
gst_filesrc_init (GstFileSrc *src)
{
  src->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_pad_set_get_function (src->srcpad,gst_filesrc_get);
  gst_pad_set_event_function (src->srcpad,gst_filesrc_srcpad_event);
  gst_element_add_pad (GST_ELEMENT (src), src->srcpad);

  src->pagesize = getpagesize();

  src->filename = NULL;
  src->fd = 0;
  src->filelen = 0;

  src->curoffset = 0;
  src->block_size = 4096;
  src->touch = TRUE;

  src->mapbuf = NULL;
  src->mapsize = 4 * 1024 * 1024;		// default is 4MB

  src->map_regions = g_tree_new(gst_filesrc_bufcmp);
  src->map_regions_lock = g_mutex_new();
}


static void
gst_filesrc_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstFileSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_FILESRC (object));

  src = GST_FILESRC (object);

  switch (prop_id) {
    case ARG_LOCATION:
      /* the element must be stopped in order to do this */
      g_return_if_fail (GST_STATE (src) < GST_STATE_PLAYING);

      if (src->filename) g_free (src->filename);
      /* clear the filename if we get a NULL (is that possible?) */
      if (g_value_get_string (value) == NULL) {
        gst_element_set_state (GST_ELEMENT (object), GST_STATE_NULL);
        src->filename = NULL;
      /* otherwise set the new filename */
      } else {
        src->filename = g_strdup (g_value_get_string (value));
      }
      break;
    case ARG_BLOCKSIZE:
      src->block_size = g_value_get_ulong (value);
      break;
    case ARG_OFFSET:
      src->curoffset = g_value_get_ulong (value);
      break;
    case ARG_MAPSIZE:
      if ((src->mapsize % src->pagesize) == 0)
        src->mapsize = g_value_get_ulong (value);
      else
        GST_INFO(0, "invalid mapsize, must a multiple of pagesize, which is %d\n",src->pagesize);
      break;
    default:
      break;
  }
}

static void
gst_filesrc_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstFileSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_FILESRC (object));

  src = GST_FILESRC (object);

  switch (prop_id) {
    case ARG_LOCATION:
      g_value_set_string (value, src->filename);
      break;
    case ARG_FILESIZE:
      g_value_set_ulong (value, src->filelen);
      break;
    case ARG_FD:
      g_value_set_int (value, src->fd);
      break;
    case ARG_BLOCKSIZE:
      g_value_set_ulong (value, src->block_size);
      break;
    case ARG_OFFSET:
      g_value_set_ulong (value, src->curoffset);
      break;
    case ARG_MAPSIZE:
      g_value_set_ulong (value, src->mapsize);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_filesrc_free_parent_mmap (GstBuffer *buf)
{
  GstFileSrc *src = GST_FILESRC(GST_BUFFER_POOL_PRIVATE(buf));

  fprintf(stderr,"freeing mmap()d buffer at %d+%d\n",GST_BUFFER_OFFSET(buf),GST_BUFFER_SIZE(buf));

  // remove the buffer from the list of available mmap'd regions
  g_mutex_lock(src->map_regions_lock);
  g_tree_remove(src->map_regions,buf);
  // check to see if the tree is empty
  if (g_tree_nnodes(src->map_regions) == 0) {
    // we have to free the bufferpool we don't have yet
  }
  g_mutex_unlock(src->map_regions_lock);

  // now unmap the memory
  munmap(GST_BUFFER_DATA(buf),GST_BUFFER_MAXSIZE(buf));
}

static GstBuffer *
gst_filesrc_map_region (GstFileSrc *src, off_t offset, off_t size)
{
  GstBuffer *buf;
  gint retval;

//  fprintf(stderr,"mapping region %d+%d from file into memory\n",offset,size);

  // time to allocate a new mapbuf
  buf = gst_buffer_new();
  // mmap() the data into this new buffer
  GST_BUFFER_DATA(buf) = mmap (NULL, size, PROT_READ, MAP_SHARED, src->fd, offset);
  if (GST_BUFFER_DATA(buf) == NULL) {
    fprintf(stderr, "ERROR: gstfilesrc couldn't map file!\n");
  } else if (GST_BUFFER_DATA(buf) == (void *)-1) {
    perror("gstfilesrc:mmap()");
  }
  // madvise to tell the kernel what to do with it
  retval = madvise(GST_BUFFER_DATA(buf),GST_BUFFER_SIZE(buf),MADV_SEQUENTIAL);
  // fill in the rest of the fields
  GST_BUFFER_FLAGS(buf) = GST_BUFFER_READONLY | GST_BUFFER_ORIGINAL;
  GST_BUFFER_SIZE(buf) = size;
  GST_BUFFER_MAXSIZE(buf) = size;
  GST_BUFFER_OFFSET(buf) = offset;
  GST_BUFFER_TIMESTAMP(buf) = -1LL;
  GST_BUFFER_POOL_PRIVATE(buf) = src;
  GST_BUFFER_FREE_FUNC(buf) = gst_filesrc_free_parent_mmap;

  g_mutex_lock(src->map_regions_lock);
  g_tree_insert(src->map_regions,buf,buf);
  g_mutex_unlock(src->map_regions_lock);

  return buf;
}

static GstBuffer *
gst_filesrc_map_small_region (GstFileSrc *src, off_t offset, off_t size)
{
  int mod, mapbase, mapsize;
  GstBuffer *map;

//  printf("attempting to map a small buffer at %d+%d\n",offset,size);

  // if the offset starts at a non-page boundary, we have to special case
  if ((mod = offset % src->pagesize)) {
    mapbase = offset - mod;
    mapsize = ((size + mod + src->pagesize - 1) / src->pagesize) * src->pagesize;
//    printf("not on page boundaries, resizing map to %d+%d\n",mapbase,mapsize);
    map = gst_filesrc_map_region(src, mapbase, mapsize);
    return gst_buffer_create_sub (map, offset - mapbase, size);
  }

  return gst_filesrc_map_region(src,offset,size);
}

typedef struct {
  off_t offset;
  off_t size;
} GstFileSrcRegion;

// This allows us to search for a potential mmap region.
static gint
gst_filesrc_search_region_match (gpointer a, gpointer b)
{
  GstFileSrcRegion *r = (GstFileSrcRegion *)b;

  // trying to walk b down the tree, current node is a
  if (r->offset < GST_BUFFER_OFFSET(a)) return -1;
  else if (r->offset >= (GST_BUFFER_OFFSET(a) + GST_BUFFER_SIZE(a))) return 1;
  else if ((r->offset + r->size) <= (GST_BUFFER_OFFSET(a) + GST_BUFFER_SIZE(a))) return 0;

  return -2;
}

/**
 * gst_filesrc_get:
 * @pad: #GstPad to push a buffer from
 *
 * Push a new buffer from the filesrc at the current offset.
 */
static GstBuffer *
gst_filesrc_get (GstPad *pad)
{
  GstFileSrc *src;
  GstBuffer *buf = NULL, *map;
  off_t readend,readsize,mapstart,mapend;
  gboolean eof = FALSE;
  GstFileSrcRegion region;
  int i;

  g_return_val_if_fail (pad != NULL, NULL);
  src = GST_FILESRC (gst_pad_get_parent (pad));
  g_return_val_if_fail (GST_FLAG_IS_SET (src, GST_FILESRC_OPEN), NULL);

  // calculate end pointers so we don't have to do so repeatedly later
  readsize = src->block_size;
  readend = src->curoffset + src->block_size;		// note this is the byte *after* the read
  mapstart = GST_BUFFER_OFFSET(src->mapbuf);
  mapend = mapstart + GST_BUFFER_SIZE(src->mapbuf);	// note this is the byte *after* the map

  // check to see if we're going to overflow the end of the file
  if (readend > src->filelen) {
    readsize = src->filelen - src->curoffset;
    readend = src->curoffset;
    eof = TRUE;
  }

  // if the start is past the mapstart
  if (src->curoffset >= mapstart) {
    // if the end is before the mapend, the buffer is in current mmap region...
    // ('cause by definition if readend is in the buffer, so's readstart)
    if (readend <= mapend) {
//      printf("read buf %d+%d lives in current mapbuf %d+%d, creating subbuffer of mapbuf\n",
//             src->curoffset,readsize,GST_BUFFER_OFFSET(src->mapbuf),GST_BUFFER_SIZE(src->mapbuf));
      buf = gst_buffer_create_sub (src->mapbuf, src->curoffset - GST_BUFFER_OFFSET(src->mapbuf),
                                   readsize);

    // if the start actually is within the current mmap region, map an overlap buffer
    } else if (src->curoffset < mapend) {
//      printf("read buf %d+%d starts in mapbuf %d+%d but ends outside, creating new mmap\n",
//             src->curoffset,readsize,GST_BUFFER_OFFSET(src->mapbuf),GST_BUFFER_SIZE(src->mapbuf));
      buf = gst_filesrc_map_small_region (src, src->curoffset, readsize);
    }

    // the only other option is that buffer is totally outside, which means we search for it

  // now we can assume that the start is *before* the current mmap region
  // if the readend is past mapstart, we have two options
  } else if (readend >= mapstart) {
    // either the read buffer overlaps the start of the mmap region
    // or the read buffer fully contains the current mmap region
    // either way, it's really not relevant, we just create a new region anyway
//    printf("read buf %d+%d starts before mapbuf %d+%d, but overlaps it\n",
//             src->curoffset,readsize,GST_BUFFER_OFFSET(src->mapbuf),GST_BUFFER_SIZE(src->mapbuf));
    buf = gst_filesrc_map_small_region (src, src->curoffset, readsize);
  }

  // then deal with the case where the read buffer is totally outside
  if (buf == NULL) {
    // first check to see if there's a map that covers the right region already
//    printf("searching for mapbuf to cover %d+%d\n",src->curoffset,readsize);
    region.offset = src->curoffset;
    region.size = readsize;
    map = g_tree_search(src->map_regions,gst_filesrc_search_region_match,&region);

    // if we found an exact match, subbuffer it
    if (map != NULL) {
//      printf("found mapbuf at %d+%d, creating subbuffer\n",GST_BUFFER_OFFSET(map),GST_BUFFER_SIZE(map));
      buf = gst_buffer_create_sub (map, src->curoffset - GST_BUFFER_OFFSET(map), readsize);

    // otherwise we need to create something out of thin air
    } else {
      // if the read buffer crosses a mmap region boundary, create a one-off region
      if ((src->curoffset / src->mapsize) != (readend / src->mapsize)) {
//        printf("read buf %d+%d crosses a %d-byte boundary, creating a one-off\n",
//               src->curoffset,readsize,src->mapsize);
        buf = gst_filesrc_map_small_region (src, src->curoffset, readsize);

      // otherwise we will create a new mmap region and set it to the default
      } else {
        off_t nextmap = src->curoffset - (src->curoffset % src->mapsize);
//        printf("read buf %d+%d in new mapbuf at %d+%d, mapping and subbuffering\n",
//               src->curoffset,readsize,nextmap,src->mapsize);
        // first, we're done with the old mapbuf
        gst_buffer_unref(src->mapbuf);
        // create a new one
        src->mapbuf = gst_filesrc_map_region (src, nextmap, src->mapsize);
        // subbuffer it
        buf = gst_buffer_create_sub (src->mapbuf, src->curoffset - GST_BUFFER_OFFSET(src->mapbuf), readsize);
      }
    }
  }

  /* if we need to touch the buffer (to bring it into memory), do so */
  if (src->touch) {
    for (i=0;i<GST_BUFFER_SIZE(buf);i+=src->pagesize)
      *(GST_BUFFER_DATA(buf)+i) = *(GST_BUFFER_DATA(buf)+i);
  }

  // if we hit EOF, 

  /* we're done, return the buffer */
  src->curoffset += GST_BUFFER_SIZE(buf);
  return buf;
}

/* open the file and mmap it, necessary to go to READY state */
static gboolean 
gst_filesrc_open_file (GstFileSrc *src)
{
  g_return_val_if_fail (!GST_FLAG_IS_SET (src ,GST_FILESRC_OPEN), FALSE);

  GST_DEBUG(0, "opening file %s\n",src->filename);

  /* open the file */
  src->fd = open (src->filename, O_RDONLY);
  if (src->fd < 0) {
    perror ("open");
    gst_element_error (GST_ELEMENT (src), g_strconcat("opening file \"", src->filename, "\"", NULL));
    return FALSE;
  } else {
    /* find the file length */
    src->filelen = lseek (src->fd, 0, SEEK_END);
    lseek (src->fd, 0, SEEK_SET);

    // allocate the first mmap'd region
    src->mapbuf = gst_filesrc_map_region (src, 0, src->mapsize);

    src->curoffset = 0;

    GST_FLAG_SET (src, GST_FILESRC_OPEN);
  }
  return TRUE;
}

/* unmap and close the file */
static void
gst_filesrc_close_file (GstFileSrc *src)
{
  g_return_if_fail (GST_FLAG_IS_SET (src, GST_FILESRC_OPEN));

  /* close the file */
  close (src->fd);

  /* zero out a lot of our state */
  src->fd = 0;
  src->filelen = 0;
  src->curoffset = 0;

  GST_FLAG_UNSET (src, GST_FILESRC_OPEN);
}


static GstElementStateReturn
gst_filesrc_change_state (GstElement *element)
{
  g_return_val_if_fail (GST_IS_FILESRC (element), GST_STATE_FAILURE);

  if (GST_STATE_PENDING (element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET (element, GST_FILESRC_OPEN))
      gst_filesrc_close_file (GST_FILESRC (element));
  } else {
    if (!GST_FLAG_IS_SET (element, GST_FILESRC_OPEN)) {
      if (!gst_filesrc_open_file (GST_FILESRC (element)))
        return GST_STATE_FAILURE;
    }
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
gst_filesrc_srcpad_event(GstPad *pad, GstEventType event, gint64 location, guint32 data)
{
  GstFileSrc *src = GST_FILESRC(GST_PAD_PARENT(pad));

  if (event == GST_EVENT_SEEK) {
    if (data == SEEK_SET) {
      src->curoffset = (guint64)location;
    } else if (data == SEEK_CUR) {
      src->curoffset += (gint64)location;
    } else if (data == SEEK_END) {
      src->curoffset = src->filelen - (guint64)location;
    }
    // push a discontinuous event?
    return TRUE;
  }

  return FALSE;
}
