
#include <gst/gst.h>

typedef struct _GstMemChunk GstMemChunk;
typedef struct _GstMemChunkElement GstMemChunkElement;

struct _GstMemChunkElement
{
  GstMemChunkElement *link;		/* next cell in the lifo */
  GstMemChunkElement *area;
};

struct _GstMemChunk
{
  volatile GstMemChunkElement *free;	/* the first free element */
  volatile gulong cnt;			/* used to avoid ABA problem */

  gchar *name;
  gulong area_size;
  gulong chunk_size;
  gulong atom_size;
  gboolean cleanup;
};

GstMemChunk*	gst_mem_chunk_new 	(gchar *name,
					 gint atom_size,
					 gulong area_size,
					 gint type);

void 		gst_mem_chunk_destroy 	(GstMemChunk *mem_chunk);

gpointer 	gst_mem_chunk_alloc 	(GstMemChunk *mem_chunk);
void	 	gst_mem_chunk_free 	(GstMemChunk *mem_chunk,
					 gpointer mem);
