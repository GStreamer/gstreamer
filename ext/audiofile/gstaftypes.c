/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstaftypes.c: 
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
#include <string.h>
#include <audiofile.h>
#include <af_vfs.h>

static ssize_t gst_aftypes_vf_read (AFvirtualfile *vfile, void *data, size_t nbytes);
static long gst_aftypes_vf_length (AFvirtualfile *vfile);
static ssize_t gst_aftypes_vf_write (AFvirtualfile *vfile, const void *data, size_t nbytes);
static void gst_aftypes_vf_destroy(AFvirtualfile *vfile);
static long gst_aftypes_vf_seek   (AFvirtualfile *vfile, long offset, int is_relative);
static long gst_aftypes_vf_tell   (AFvirtualfile *vfile);

static void gst_aftypes_type_find(GstTypeFind *tf, gpointer private);

typedef struct _GstAFTypesBuffer GstAFTypesBuffer;
struct _GstAFTypesBuffer {
  guint8 *data;
  int length;
  long offset;
};

#define GST_AUDIOFILE_TYPE_FIND_SIZE 4096
#define AF_CAPS(type) gst_caps_new ("audiofile_type_find", type, NULL)
static void
gst_aftypes_type_find(GstTypeFind *tf, gpointer private)
{
  GstAFTypesBuffer *buffer_wrap;
  guint8 *data;
  AFvirtualfile *vfile;
  AFfilehandle file;
  int file_format, format_version;
  gchar *type;

  data = gst_type_find_peek (tf, 0, GST_AUDIOFILE_TYPE_FIND_SIZE);
  if (data == NULL) return;

  buffer_wrap = g_new0(GstAFTypesBuffer, 1);
  buffer_wrap->data = data;
  buffer_wrap->length = GST_AUDIOFILE_TYPE_FIND_SIZE;
  buffer_wrap->offset = 0;

  vfile = af_virtual_file_new();
  vfile->closure = buffer_wrap;
  vfile->read = gst_aftypes_vf_read;
  vfile->length = gst_aftypes_vf_length;
  vfile->write = gst_aftypes_vf_write;
  vfile->destroy = gst_aftypes_vf_destroy;
  vfile->seek = gst_aftypes_vf_seek;
  vfile->tell = gst_aftypes_vf_tell;
  
  file = afOpenVirtualFile (vfile, "r", AF_NULL_FILESETUP);
  file_format = afGetFileFormat (file, &format_version);
  afCloseFile (file);

  //GST_DEBUG("file format: %d", file_format);

  /* reject raw data, just in case it is some other format */
  if (file_format == AF_FILE_UNKNOWN ||
      file_format == AF_FILE_RAWDATA){
    g_free (buffer_wrap);
    return;
  }
  switch (file_format){
    case AF_FILE_AIFF:
    case AF_FILE_AIFFC:
      type = "audio/x-aiff";
      break;
    case AF_FILE_WAVE:
      type = "audio/x-wav";
      break;
    case AF_FILE_NEXTSND:
      type = "audio/basic";
      break;
    case AF_FILE_BICSF:
    default:
      type=NULL;
      break;
  }
  
  if (type != NULL){
    gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, AF_CAPS(type));
  }
}

gboolean
gst_aftypes_plugin_init (GstPlugin *plugin)
{
  static gchar * af_exts[] = { "aiff", "aif", "aifc", "wav", "au", "snd", NULL };
 
  gst_type_find_register (plugin, "audio/x-mod",
			  GST_RANK_MARGINAL,
			  gst_aftypes_type_find, af_exts,
			  GST_CAPS_ANY, NULL);
  
  return TRUE;
}


static ssize_t 
gst_aftypes_vf_read (AFvirtualfile *vfile, void *data, size_t nbytes)
{
  GstAFTypesBuffer *bwrap = (GstAFTypesBuffer*)vfile->closure;
  long bufsize = bwrap->length;
  guint8 *bytes = bwrap->data;

  if (bwrap->offset + nbytes > bufsize){
    nbytes = bufsize;
  }
  
  bytes += bwrap->offset;
  memcpy(data, bytes, nbytes);
  bwrap->offset += nbytes;
  
  g_print("read %d bytes\n", nbytes);

  return nbytes;
}

static long 
gst_aftypes_vf_seek   (AFvirtualfile *vfile, long offset, int is_relative)
{
  GstAFTypesBuffer *bwrap = (GstAFTypesBuffer*)vfile->closure;
  long bufsize = bwrap->length;
  
  g_print("request seek to: %ld\n", offset);
  if (!is_relative){
    if (offset > bufsize || offset < 0){
      return -1;
    }
    bwrap->offset = offset;
  }
  else {
    if (bwrap->offset + offset > bufsize || 
        bwrap->offset + offset < 0){
      return -1;
    }
    bwrap->offset += offset;
  }
  
  g_print("seek to: %ld\n", bwrap->offset);
  return bwrap->offset;
}

static long 
gst_aftypes_vf_length (AFvirtualfile *vfile)
{
  GstAFTypesBuffer *bwrap = (GstAFTypesBuffer*)vfile->closure;
  return bwrap->length;
}

static ssize_t 
gst_aftypes_vf_write (AFvirtualfile *vfile, const void *data, size_t nbytes)
{
  g_warning("shouldn't write to a readonly pad");
  return 0;
}

static void 
gst_aftypes_vf_destroy(AFvirtualfile *vfile)
{

}

static long 
gst_aftypes_vf_tell   (AFvirtualfile *vfile)
{
  GstAFTypesBuffer *bwrap = (GstAFTypesBuffer*)vfile->closure;
  g_print("doing tell: %ld\n", bwrap->offset);
  return bwrap->offset;
}



