#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <string.h>

#include "gstmikmod.h"

extern int need_sync;

static BOOL GST_READER_Eof (MREADER * reader);
static BOOL GST_READER_Read (MREADER * reader, void *ptr, size_t size);
static int GST_READER_Get (MREADER * reader);
static BOOL GST_READER_Seek (MREADER * reader, long offset, int whence);
static long GST_READER_Tell (MREADER * reader);


static BOOL
GST_READER_Eof (MREADER * reader)
{
  GST_READER *gst_reader;

  gst_reader = (GST_READER *) reader;

  return gst_reader->eof;
}


static BOOL
GST_READER_Read (MREADER * reader, void *ptr, size_t size)
{
  GST_READER *gst_reader;

  gst_reader = (GST_READER *) reader;

  memcpy (ptr, GST_BUFFER_DATA (gst_reader->mik->Buffer) + gst_reader->offset,
      size);
  gst_reader->offset = gst_reader->offset + size;

  return 1;
}


static int
GST_READER_Get (MREADER * reader)
{
  GST_READER *gst_reader;
  int res;

  gst_reader = (GST_READER *) reader;

  res = *(GST_BUFFER_DATA (gst_reader->mik->Buffer) + gst_reader->offset);
  gst_reader->offset += 1;

  return res;
}


static BOOL
GST_READER_Seek (MREADER * reader, long offset, int whence)
{
  GST_READER *gst_reader;

  gst_reader = (GST_READER *) reader;

  if (whence == SEEK_SET)
    gst_reader->offset = offset;
  else
    gst_reader->offset += offset;

  return 1;
}


static long
GST_READER_Tell (MREADER * reader)
{
  GST_READER *gst_reader;

  gst_reader = (GST_READER *) reader;

  return gst_reader->offset;
}


MREADER *
GST_READER_new (GstMikMod * mik)
{
  GST_READER *gst_reader;

  gst_reader = (GST_READER *) g_malloc (sizeof (GST_READER));
  gst_reader->offset = 0;
  gst_reader->eof = 0;
  gst_reader->mik = mik;
  if (gst_reader) {
    gst_reader->core.Eof = &GST_READER_Eof;
    gst_reader->core.Read = &GST_READER_Read;
    gst_reader->core.Get = &GST_READER_Get;
    gst_reader->core.Seek = &GST_READER_Seek;
    gst_reader->core.Tell = &GST_READER_Tell;
  }

  return (MREADER *) gst_reader;
}


void
GST_READER_free (MREADER * reader)
{
  if (reader)
    g_free (reader);
}
