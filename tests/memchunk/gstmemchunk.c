#include <string.h>		/* memset */
#include <stdlib.h>		/* memset */
#include "gstmemchunk.h"

#ifdef __SMP__
#define CHUNK_LOCK "lock ; "
#else
#define CHUNK_LOCK ""
#endif

#define GST_MEM_CHUNK_AREA(chunk) 	(((GstMemChunkElement*)(chunk))->area)
#define GST_MEM_CHUNK_DATA(chunk) 	((gpointer)(((GstMemChunkElement*)(chunk)) + 1))
#define GST_MEM_CHUNK_LINK(mem) 	((GstMemChunkElement*)((guint8*)(mem) - sizeof (GstMemChunkElement)))

/*******************************************************
 *         area size
 * +-----------------------------------------+
 *   chunk size
 * +------------+
 *
 * !next!data... !next!data.... !next!data...
 *  !             ^ !            ^ !
 *  +-------------+ +------------+ +---> NULL
 *
 */
static gboolean
populate (GstMemChunk * mem_chunk)
{
  guint8 *area;
  gint i;

  if (mem_chunk->cleanup)
    return FALSE;

  area = (guint8 *) g_malloc (mem_chunk->area_size);
  g_print ("alloc %p\n", area);

  for (i = 0; i < mem_chunk->area_size; i += mem_chunk->chunk_size) {
    GST_MEM_CHUNK_AREA (area + i) = (GstMemChunkElement *) area;
    gst_mem_chunk_free (mem_chunk, GST_MEM_CHUNK_DATA (area + i));
  }

  return TRUE;
}


GstMemChunk *
gst_mem_chunk_new (gchar * name, gint atom_size, gulong area_size, gint type)
{
  GstMemChunk *mem_chunk;

  g_return_val_if_fail (atom_size > 0, NULL);
  g_return_val_if_fail (area_size >= atom_size, NULL);

  mem_chunk = g_malloc (sizeof (GstMemChunk));

  mem_chunk->chunk_size = atom_size + sizeof (GstMemChunkElement);
  area_size = (area_size / atom_size) * mem_chunk->chunk_size;

  mem_chunk->name = g_strdup (name);
  mem_chunk->free = NULL;
  mem_chunk->cnt = 0;
  mem_chunk->atom_size = atom_size;
  mem_chunk->area_size = area_size;
  mem_chunk->cleanup = FALSE;

  populate (mem_chunk);

  return mem_chunk;
}

static gboolean
free_area (gpointer key, gpointer value, gpointer user_data)
{
  g_print ("free %p\n", key);
  g_free (key);

  return TRUE;
}

void
gst_mem_chunk_destroy (GstMemChunk * mem_chunk)
{
  GHashTable *elements = g_hash_table_new (NULL, NULL);
  gpointer data;

  mem_chunk->cleanup = TRUE;

  data = gst_mem_chunk_alloc (mem_chunk);
  while (data) {
    GstMemChunkElement *elem = GST_MEM_CHUNK_LINK (data);

    g_hash_table_insert (elements, GST_MEM_CHUNK_AREA (elem), NULL);

    data = gst_mem_chunk_alloc (mem_chunk);
  }
  g_hash_table_foreach_remove (elements, free_area, NULL);

  g_hash_table_destroy (elements);
  g_free (mem_chunk->name);
  g_free (mem_chunk);
}

gpointer
gst_mem_chunk_alloc (GstMemChunk * mem_chunk)
{
  GstMemChunkElement *chunk = NULL;

  g_return_val_if_fail (mem_chunk != NULL, NULL);

again:
#ifdef HAVE_I386
__asm__ __volatile__ ("  testl %%eax, %%eax 		\n\t" "  jz 20f 			\n" "10:				\t" "  movl (%%eax), %%ebx  	\n\t" "  movl %%edx, %%ecx    	\n\t" "  incl %%ecx 			\n\t" CHUNK_LOCK "cmpxchg8b %1 	\n\t" "  jz 20f 			\n\t" "  testl %%eax, %%eax 		\n\t" "  jnz 10b 			\n" "20:\t":"=a" (chunk)
:    "m" (*mem_chunk), "a" (mem_chunk->free), "d" (mem_chunk->cnt)
:    "ecx", "ebx");
#else
  fprintf (stderr, "This only compiles correctly on i386.  Sorry\n");
  abort ();
#endif

  if (!chunk) {
    /*g_print ("extending\n"); */
    if (populate (mem_chunk))
      goto again;
    else
      return NULL;
  }
  return GST_MEM_CHUNK_DATA (chunk);
}

gpointer
gst_mem_chunk_alloc0 (GstMemChunk * mem_chunk)
{
  gpointer mem = gst_mem_chunk_alloc (mem_chunk);

  if (mem)
    memset (mem, 0, mem_chunk->atom_size);

  return mem;
}

void
gst_mem_chunk_free (GstMemChunk * mem_chunk, gpointer mem)
{
  GstMemChunkElement *chunk;

  g_return_if_fail (mem_chunk != NULL);
  g_return_if_fail (mem != NULL);

  chunk = GST_MEM_CHUNK_LINK (mem);

#ifdef HAVE_I386
  __asm__ __volatile__ ("1:				\t"
      "  movl %2, (%1) 		\n"
      CHUNK_LOCK "cmpxchg %1, %0 	\n\t"
      "  jnz 1b 			\n\t"::"m"
      (*mem_chunk), "r" (chunk), "a" (mem_chunk->free));
#else
  fprintf (stderr, "This only compiles correctly on i386.  Sorry\n");
  abort ();
#endif
}
