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

static GstCaps* gst_aftypes_type_find(GstBuffer *buf, gpointer private);

static GstTypeDefinition aftype_definitions[] = {
  { "aftypes audio/audiofile", "audio/audiofile", ".wav .aiff .aif .aifc", gst_aftypes_type_find },
  { NULL, NULL, NULL, NULL },
};

typedef struct _GstAFTypesBuffer GstAFTypesBuffer;
struct _GstAFTypesBuffer {
  GstBuffer *buffer;
  long offset;
};

static GstCaps* 
gst_aftypes_type_find(GstBuffer *buf, gpointer private)
{
  
  GstAFTypesBuffer *buffer_wrap = g_new0(GstAFTypesBuffer, 1);
  AFvirtualfile *vfile;
  AFfilehandle file;
  int file_format, format_version;

  g_print("calling gst_aftypes_type_find\n");

  buffer_wrap->buffer = buf;
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

  g_print("file format: %d\n", file_format);

  /* reject raw data, just in case it is some other format */
  if (file_format == AF_FILE_UNKNOWN ||
      file_format == AF_FILE_RAWDATA){
    return NULL;
  }

  return gst_caps_new ("audiofile_type_find", "audio/audiofile", NULL);
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  gint i=0;
 
  while (aftype_definitions[i].name) {
    GstTypeFactory *type;

    type = gst_type_factory_new (&aftype_definitions[i]);
    gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (type));
    i++;
  }
    
  return TRUE;
}


GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "aftypes",
  plugin_init
};


static ssize_t 
gst_aftypes_vf_read (AFvirtualfile *vfile, void *data, size_t nbytes)
{
  GstAFTypesBuffer *bwrap = (GstAFTypesBuffer*)vfile->closure;
  long bufsize = GST_BUFFER_SIZE(bwrap->buffer);
  guchar *bytes = GST_BUFFER_DATA(bwrap->buffer);  

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
  long bufsize = GST_BUFFER_SIZE(bwrap->buffer);
  
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
  return GST_BUFFER_SIZE(bwrap->buffer);
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



