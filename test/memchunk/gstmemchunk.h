
#include <gst/gst.h>

typedef struct _GstMemChunk GstMemChunk;
typedef struct _GstMemChunkElement GstMemChunkElement;

struct _GstMemChunkElement
{
  GstMemChunkElement *link;		/* next cell in the lifo */
  // data is here
};

struct _GstMemChunk
{
  volatile GstMemChunkElement *free;	/* the first free element */
  volatile unsigned long cnt;		/* used to avoid ABA problem */

  gulong area_size;
  gulong chunk_size;
  gulong atom_size;
};

GstMemChunk*	gst_mem_chunk_new 	(gchar *name,
					 gint atom_size,
					 gulong area_size,
					 gint type);

gpointer 	gst_mem_chunk_alloc 	(GstMemChunk *mem_chunk);
void	 	gst_mem_chunk_free 	(GstMemChunk *mem_chunk,
					 gpointer mem);
