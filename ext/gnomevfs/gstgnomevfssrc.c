/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2001 Bastien Nocera <hadess@hadess.net>
 *
 * gnomevfssrc.c:
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

#define BROKEN_SIG 1
/*#undef BROKEN_SIG */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>

#include <gst/gst.h>
#include <libgnomevfs/gnome-vfs.h>

GstElementDetails gst_gnomevfssrc_details;

#define GST_TYPE_GNOMEVFSSRC \
  (gst_gnomevfssrc_get_type())
#define GST_GNOMEVFSSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GNOMEVFSSRC,GstGnomeVFSSrc))
#define GST_GNOMEVFSSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GNOMEVFSSRC,GstGnomeVFSSrcClass))
#define GST_IS_GNOMEVFSSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GNOMEVFSSRC))
#define GST_IS_GNOMEVFSSRC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GNOMEVFSSRC))

typedef enum {
	GST_GNOMEVFSSRC_OPEN = GST_ELEMENT_FLAG_LAST,

	GST_GNOMEVFSSRC_FLAG_LAST = GST_ELEMENT_FLAG_LAST + 2,
} GstGnomeVFSSrcFlags;

typedef struct _GstGnomeVFSSrc GstGnomeVFSSrc;
typedef struct _GstGnomeVFSSrcClass GstGnomeVFSSrcClass;

struct _GstGnomeVFSSrc {
	GstElement element;
	/* pads */
	GstPad *srcpad;

	/* filename */
	gchar *filename;
	/* is it a local file ? */
	gboolean is_local;
	/* uri */
	GnomeVFSURI *uri;

	/* handle */
	GnomeVFSHandle *handle;

	/* local filename */
	gchar *local_name;
	/* fd for local file fallback */
	gint fd;
	/* mmap */
	guchar *map;			/* where the file is mapped to */

	/* details for fallback synchronous read */
	GnomeVFSFileSize size;
	GnomeVFSFileOffset curoffset;	/* current offset in file */
	gulong bytes_per_read;		/* bytes per read */
	gboolean new_seek;
};

struct _GstGnomeVFSSrcClass {
	GstElementClass parent_class;
};

GstElementDetails gst_gnomevfssrc_details = {
	"GnomeVFS Source",
	"Source/File",
	"Read from any GnomeVFS file",
	VERSION,
	"Bastien Nocera <hadess@hadess.net>",
	"(C) 2001",
};

/* GnomeVFSSrc signals and args */
enum {
	LAST_SIGNAL
};

enum {
	ARG_0,
	ARG_LOCATION,
	ARG_BYTESPERREAD,
	ARG_OFFSET,
	ARG_FILESIZE,
};

GType gst_gnomevfssrc_get_type(void);

static void gst_gnomevfssrc_class_init(GstGnomeVFSSrcClass *klass);
static void gst_gnomevfssrc_init(GstGnomeVFSSrc *gnomevfssrc);

static void gst_gnomevfssrc_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_gnomevfssrc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static GstBuffer *gst_gnomevfssrc_get(GstPad *pad);

static GstElementStateReturn gst_gnomevfssrc_change_state(GstElement *element);

static void gst_gnomevfssrc_close_file(GstGnomeVFSSrc *src);
static gboolean gst_gnomevfssrc_open_file(GstGnomeVFSSrc *src);


static GstElementClass *parent_class = NULL;

GType gst_gnomevfssrc_get_type(void)
{
	static GType gnomevfssrc_type = 0;

	if (!gnomevfssrc_type) {
		static const GTypeInfo gnomevfssrc_info = {
      sizeof(GstGnomeVFSSrcClass),      NULL,
      NULL,
      (GClassInitFunc) gst_gnomevfssrc_class_init,
      NULL,
      NULL,
      sizeof(GstGnomeVFSSrc),
      0,
      (GInstanceInitFunc) gst_gnomevfssrc_init,
		};
		gnomevfssrc_type =
		    g_type_register_static(GST_TYPE_ELEMENT, "GstGnomeVFSSrc", &gnomevfssrc_info, 0);
	}
	return gnomevfssrc_type;
}

static void gst_gnomevfssrc_class_init(GstGnomeVFSSrcClass *klass)
{
	GObjectClass *gobject_class;
	GstElementClass *gstelement_class;

	gobject_class = (GObjectClass *) klass;
	gstelement_class = (GstElementClass *) klass;

	parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

      	gst_element_install_std_props (
               GST_ELEMENT_CLASS (klass),
               "offset",       ARG_OFFSET,       G_PARAM_READWRITE,
               "filesize",     ARG_FILESIZE,     G_PARAM_READABLE,
               "bytesperread", ARG_BYTESPERREAD, G_PARAM_READWRITE,
               "location",     ARG_LOCATION,     G_PARAM_READWRITE,
               NULL);

	gstelement_class->set_property = gst_gnomevfssrc_set_property;
	gstelement_class->get_property = gst_gnomevfssrc_get_property;

	gstelement_class->change_state = gst_gnomevfssrc_change_state;

	/* gnome vfs engine init */
	if (gnome_vfs_initialized() == FALSE)
		gnome_vfs_init();
}

static void gst_gnomevfssrc_init(GstGnomeVFSSrc *gnomevfssrc)
{

	gnomevfssrc->srcpad = gst_pad_new("src", GST_PAD_SRC);
	gst_pad_set_get_function(gnomevfssrc->srcpad, gst_gnomevfssrc_get);
	gst_element_add_pad(GST_ELEMENT(gnomevfssrc), gnomevfssrc->srcpad);

	gnomevfssrc->filename = NULL;
	gnomevfssrc->is_local = FALSE;
	gnomevfssrc->uri = NULL;
	gnomevfssrc->handle = NULL;
	gnomevfssrc->fd = 0;
	gnomevfssrc->curoffset = 0;
	gnomevfssrc->bytes_per_read = 4096;
	gnomevfssrc->new_seek = FALSE;
}


static void gst_gnomevfssrc_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GstGnomeVFSSrc *src;
        const gchar *location;
        gchar cwd[PATH_MAX];

	/* it's not null if we got it, but it might not be ours */
	g_return_if_fail(GST_IS_GNOMEVFSSRC(object));

	src = GST_GNOMEVFSSRC(object);

	switch (prop_id) {
	case ARG_LOCATION:
		/* the element must be stopped or paused in order to do this */
		g_return_if_fail((GST_STATE(src) < GST_STATE_PLAYING)
				 || (GST_STATE(src) == GST_STATE_PAUSED));

		if (src->filename)
			g_free(src->filename);

		/* clear the filename if we get a NULL (is that possible?) */
		if (g_value_get_string (value) == NULL) {
			gst_element_set_state(GST_ELEMENT(object), GST_STATE_NULL);
			src->filename = NULL;
		} else {
			/* otherwise set the new filename */
                        location = g_value_get_string (value);
                        /* if it's not a proper uri, default to file:// -- this
                         * is a crude test */
                        if (!strchr (location, ':'))
                                if (*location == '/')
                                        src->filename = g_strdup_printf ("file://%s", location);
                                else
                                        src->filename = g_strdup_printf ("file://%s/%s", getcwd(cwd, PATH_MAX), location);
                        else
                                src->filename = g_strdup (g_value_get_string (value));
		}

		if ((GST_STATE(src) == GST_STATE_PAUSED)
		    && (src->filename != NULL)) {
			gst_gnomevfssrc_close_file(src);
			gst_gnomevfssrc_open_file(src);
		}
		break;
	case ARG_BYTESPERREAD:
		src->bytes_per_read = g_value_get_int (value);
		break;
	case ARG_OFFSET:
		src->curoffset = g_value_get_int64 (value);
		src->new_seek = TRUE;
		break;
	default:
		break;
	}
}

static void gst_gnomevfssrc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstGnomeVFSSrc *src;

	/* it's not null if we got it, but it might not be ours */
	g_return_if_fail(GST_IS_GNOMEVFSSRC(object));

	src = GST_GNOMEVFSSRC(object);

	switch (prop_id) {
	case ARG_LOCATION:
		g_value_set_string (value, src->filename);
		break;
	case ARG_BYTESPERREAD:
		g_value_set_int (value, src->bytes_per_read);
		break;
	case ARG_OFFSET:
		g_value_set_int64 (value, src->curoffset);
		break;
	case ARG_FILESIZE:
		g_value_set_int64 (value, src->size);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * gst_gnomevfssrc_get:
 * @pad: #GstPad to push a buffer from
 *
 * Push a new buffer from the gnomevfssrc at the current offset.
 */
static GstBuffer *gst_gnomevfssrc_get(GstPad *pad)
{
	GstGnomeVFSSrc *src;
	GnomeVFSResult result = 0;
	GstBuffer *buf;
	GnomeVFSFileSize readbytes;

	g_return_val_if_fail(pad != NULL, NULL);
	src = GST_GNOMEVFSSRC(gst_pad_get_parent(pad));
	g_return_val_if_fail(GST_FLAG_IS_SET(src, GST_GNOMEVFSSRC_OPEN),
			     NULL);

	/* deal with EOF state */
	if ((src->curoffset >= src->size) && (src->size != 0))
	{
		gst_element_set_eos (GST_ELEMENT (src));
		return GST_BUFFER (gst_event_new (GST_EVENT_EOS));
	}

	/* create the buffer */
	/* FIXME: should eventually use a bufferpool for this */
	buf = gst_buffer_new();
	g_return_val_if_fail(buf, NULL);

	/* read it in from the file */
	if (src->is_local)
	{
		/* simply set the buffer to point to the correct region of the
		 * file */
		GST_BUFFER_DATA (buf) = src->map + src->curoffset;
		GST_BUFFER_OFFSET (buf) = src->curoffset;
		GST_BUFFER_FLAG_SET (buf, GST_BUFFER_DONTFREE);

		if ((src->curoffset + src->bytes_per_read) > src->size)
		{
			GST_BUFFER_SIZE (buf) = src->size - src->curoffset;
		} else {
			GST_BUFFER_SIZE (buf) = src->bytes_per_read;
		}

		src->curoffset += GST_BUFFER_SIZE (buf);

                g_object_notify ((GObject*) src, "offset");

		if (src->new_seek)
		{
			/* FIXME do a discont, flush event */
			GST_DEBUG (0,"new seek\n");
			src->new_seek = FALSE;
		}
	} else {
		/* allocate the space for the buffer data */
		GST_BUFFER_DATA(buf) = g_malloc(src->bytes_per_read);
		g_return_val_if_fail(GST_BUFFER_DATA(buf) != NULL, NULL);

		if (src->new_seek)
		{
			result = gnome_vfs_seek(src->handle,
					GNOME_VFS_SEEK_START, src->curoffset);
			GST_DEBUG(0, "new_seek: %s\n",
					gnome_vfs_result_to_string(result));
			/* FIXME do a discont, flush event */
			src->new_seek = FALSE;
		}

		result = gnome_vfs_read(src->handle, GST_BUFFER_DATA(buf),
				   src->bytes_per_read, &readbytes);

		GST_DEBUG(0, "read: %s, readbytes: %Lu\n",
				gnome_vfs_result_to_string(result), readbytes);
		/* deal with EOS */
		if (readbytes == 0)
		{
			gst_buffer_unref(buf);

			gst_element_set_eos (GST_ELEMENT (src));

			return GST_BUFFER (gst_event_new (GST_EVENT_EOS));
		}
		
		GST_BUFFER_OFFSET(buf) = src->curoffset;
		GST_BUFFER_SIZE(buf) = readbytes;
		src->curoffset += readbytes;
                g_object_notify ((GObject*) src, "offset");
	}

	/* we're done, return the buffer */
	return buf;
}

/* open the file and mmap it, necessary to go to READY state */
static gboolean gst_gnomevfssrc_open_file(GstGnomeVFSSrc *src)
{
	GnomeVFSResult result;

	g_return_val_if_fail(!GST_FLAG_IS_SET(src, GST_GNOMEVFSSRC_OPEN),
			     FALSE);

	/* create the uri */
	src->uri = gnome_vfs_uri_new(src->filename);
	if (!src->uri) {
		gst_element_error(GST_ELEMENT(src),
				  "creating uri \"%s\" (%s)",
				  	src->filename, strerror (errno));
		return FALSE;
	}

	/* open the file using open() if the file is local */
	src->is_local = gnome_vfs_uri_is_local(src->uri);

	if (src->is_local) {
		src->local_name =
			gnome_vfs_get_local_path_from_uri(src->filename);
		src->fd = open(src->local_name, O_RDONLY);
		if (src->fd < 0)
		{
			gst_element_error(GST_ELEMENT(src),
					  "opening local file \"%s\" (%s)",
						src->filename, strerror (errno));
			return FALSE;
		}

		/* find the file length */
		src->size = lseek (src->fd, 0, SEEK_END);
		lseek (src->fd, 0, SEEK_SET);
		src->map = mmap (NULL, src->size, PROT_READ, MAP_SHARED,
				src->fd, 0);
		madvise (src->map,src->size, 2);
		/* collapse state if that failed */
		if (src->map == NULL)
		{
			gst_gnomevfssrc_close_file(src);
			gst_element_error (GST_ELEMENT (src),"mmapping file");
			return FALSE;
		}

                g_object_notify (G_OBJECT (src), "filesize");

		src->new_seek = TRUE;
	} else {
		result =
		    gnome_vfs_open_uri(&(src->handle), src->uri,
				       GNOME_VFS_OPEN_READ);
		if (result != GNOME_VFS_OK)
		{
			gst_element_error(GST_ELEMENT(src),
				  "opening vfs file \"%s\" (%s)",
				      src->filename, 
				      gnome_vfs_result_to_string(result));
			return FALSE;
		}

		/* find the file length */
		{
			GnomeVFSResult size_result;
			GnomeVFSFileInfo *info;

			info = gnome_vfs_file_info_new ();
			size_result = gnome_vfs_get_file_info_uri(src->uri,
					info, GNOME_VFS_FILE_INFO_DEFAULT);
			
			if (size_result != GNOME_VFS_OK)
				src->size = 0;
			else
				src->size = info->size;

                        g_object_notify (G_OBJECT (src), "filesize");

			gnome_vfs_file_info_unref(info);
		}

		GST_DEBUG(0, "open %s\n", gnome_vfs_result_to_string(result));
	}

	GST_FLAG_SET(src, GST_GNOMEVFSSRC_OPEN);

	return TRUE;
}

/* unmap and close the file */
static void gst_gnomevfssrc_close_file(GstGnomeVFSSrc *src)
{
	g_return_if_fail(GST_FLAG_IS_SET(src, GST_GNOMEVFSSRC_OPEN));

	/* close the file */
	if (src->is_local)
	{
		/* unmap the file from memory */
		munmap (src->map, src->size);
		close(src->fd);
	} else {
		gnome_vfs_close(src->handle);
	}

	/* zero out a lot of our state */
	src->is_local = FALSE;
	if (src->uri)
		g_free(src->uri);
	if (src->handle)
		g_free(src->handle);
	if (src->local_name)
		g_free(src->local_name);
	src->fd = 0;
	src->map = NULL;
	src->size = 0;
	src->curoffset = 0;
	src->new_seek = FALSE;

	GST_FLAG_UNSET(src, GST_GNOMEVFSSRC_OPEN);
}

static GstElementStateReturn gst_gnomevfssrc_change_state(GstElement *element)
{
	g_return_val_if_fail(GST_IS_GNOMEVFSSRC(element),
			     GST_STATE_FAILURE);

	if (GST_STATE_TRANSITION(element) == GST_STATE_NULL_TO_READY) {
		if (!GST_FLAG_IS_SET(element, GST_GNOMEVFSSRC_OPEN)) {
			if (!gst_gnomevfssrc_open_file
			    (GST_GNOMEVFSSRC(element)))
				return GST_STATE_FAILURE;
		}
	} else if (GST_STATE_TRANSITION(element) ==
		   GST_STATE_READY_TO_NULL) {
		if (GST_FLAG_IS_SET(element, GST_GNOMEVFSSRC_OPEN))
			gst_gnomevfssrc_close_file(GST_GNOMEVFSSRC
						   (element));
	}

	if (GST_ELEMENT_CLASS(parent_class)->change_state)
		return GST_ELEMENT_CLASS(parent_class)->
		    change_state(element);

	return GST_STATE_SUCCESS;
}

static gboolean plugin_init(GModule *module, GstPlugin *plugin)
{
	GstElementFactory *factory;

	/* create an elementfactory for the aasink element */
	factory =
	    gst_elementfactory_new("gnomevfssrc", GST_TYPE_GNOMEVFSSRC,
				   &gst_gnomevfssrc_details);
	g_return_val_if_fail(factory != NULL, FALSE);

	gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

	return TRUE;
}

GstPluginDesc plugin_desc = {
	GST_VERSION_MAJOR,
	GST_VERSION_MINOR,
	"gnomevfssrc",
	plugin_init
};
