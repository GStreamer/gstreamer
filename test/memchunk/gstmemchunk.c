#include "gstmemchunk.h"

#ifdef __SMP__
#define CHUNK_LOCK "lock ; "
#else
#define CHUNK_LOCK ""
#endif

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
static void
populate (GstMemChunk *mem_chunk) 
{
  guint8 *area;
  gint i;

  area = (guint8 *)g_malloc0 (mem_chunk->area_size);

  for (i = 0; i < mem_chunk->area_size; i += mem_chunk->chunk_size) { 
    gst_mem_chunk_free (mem_chunk, area + sizeof (gpointer));
    area += mem_chunk->chunk_size;
  }
}


GstMemChunk *
gst_mem_chunk_new (gchar * name, gint atom_size, gulong area_size, gint type)
{
  GstMemChunk *mem_chunk;
  gint chunk_size;

  //g_print ("create: atom size %d, area_size %lu\n", atom_size, area_size);

  chunk_size = atom_size + sizeof (gpointer);
  area_size = (area_size/atom_size) * chunk_size;

  //g_print ("chunk size %d, real area_size %lu\n", chunk_size, area_size);

  mem_chunk = g_malloc (sizeof (GstMemChunk));

  mem_chunk->free = NULL;
  mem_chunk->cnt = 0;
  mem_chunk->atom_size = atom_size;
  mem_chunk->chunk_size = chunk_size;
  mem_chunk->area_size = area_size;

  populate (mem_chunk);

  return mem_chunk;
}

gpointer
gst_mem_chunk_alloc (GstMemChunk *mem_chunk)
{
  guint8 *chunk = NULL;
  
  g_return_val_if_fail (mem_chunk != NULL, NULL);

again:
  __asm__ __volatile__ ("testl %%eax, %%eax \n\t"
  			"jz 20f \n"
			"10:\t"
			"movl (%%eax), %%ebx \n\t"
			"movl %%edx, %%ecx \n\t"
			"incl %%ecx \n\t"
			CHUNK_LOCK "cmpxchg8b %1 \n\t"
			"jz 20f \n\t"
			"testl %%eax, %%eax \n\t"
			"jnz 10b \n"
			"20:\t"
			:"=a" (chunk)
			:"m" (*mem_chunk), "a" (mem_chunk->free), "d" (mem_chunk->cnt)
			:"ecx", "ebx");

  if (chunk)
    chunk += sizeof (gpointer);
  else {
    //g_print ("extending\n");
    populate (mem_chunk);
    goto again;
  }

  return (gpointer) chunk;
}

void
gst_mem_chunk_free (GstMemChunk *mem_chunk, gpointer mem)
{
  guint8 *chunk = ((guint8 *)mem) - sizeof (gpointer);

  g_return_if_fail (mem_chunk != NULL);
  g_return_if_fail (mem != NULL);

  __asm__ __volatile__ ( "1:\t"
			"movl %2, (%1) \n"
			CHUNK_LOCK "cmpxchg %1, %0 \n\t"
			"jnz 1b \n\t"
			:
			:"m" (*mem_chunk), "r" (chunk), "a" (mem_chunk->free));
}
