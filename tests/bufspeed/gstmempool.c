#include "gstmempool.h"
#include <string.h>             /* memset */

#ifdef __SMP__
#define POOL_LOCK "lock ; "
#else
#define POOL_LOCK ""
#endif

#define GST_MEM_POOL_AREA(pool) 	(((GstMemPoolElement*)(pool))->area)
#define GST_MEM_POOL_DATA(pool) 	((gpointer)(((GstMemPoolElement*)(pool)) + 1))
#define GST_MEM_POOL_LINK(mem) 		((GstMemPoolElement*)((guint8*)(mem) - sizeof (GstMemPoolElement)))

#ifdef HAVE_CPU_I386
#define USE_ASM
#endif

/*******************************************************
 *         area size
 * +-----------------------------------------+
 *   pool size
 * +------------+
 *
 * !next!data... !next!data.... !next!data...
 *  !             ^ !            ^ !
 *  +-------------+ +------------+ +---> NULL
 *
 */
static gboolean
populate (GstMemPool * mem_pool)
{
  guint8 *area;
  gint i;

  if (mem_pool->cleanup)
    return FALSE;

  area = (guint8 *) g_malloc (mem_pool->area_size);

  for (i = 0; i < mem_pool->area_size; i += mem_pool->pool_size) {
    guint8 *areap = area + i;

    GST_MEM_POOL_AREA (areap) = (GstMemPoolElement *) area;

    if (mem_pool->alloc_func) {
      mem_pool->alloc_func (mem_pool, GST_MEM_POOL_DATA (areap));
    }

    gst_mem_pool_free (mem_pool, GST_MEM_POOL_DATA (areap));
  }

  return TRUE;
}


GstMemPool *
gst_mem_pool_new (gchar * name, gint atom_size, gulong area_size, gint type,
    GstMemPoolAllocFunc alloc_func, GstMemPoolFreeFunc free_func)
{
  GstMemPool *mem_pool;

  g_return_val_if_fail (atom_size > 0, NULL);
  g_return_val_if_fail (area_size >= atom_size, NULL);

  mem_pool = g_malloc (sizeof (GstMemPool));

  mem_pool->pool_size = atom_size + sizeof (GstMemPoolElement);
  area_size = (area_size / atom_size) * mem_pool->pool_size;

  mem_pool->name = g_strdup (name);
  mem_pool->free = NULL;
  mem_pool->cnt = 0;
  mem_pool->atom_size = atom_size;
  mem_pool->area_size = area_size;
  mem_pool->cleanup = FALSE;
  mem_pool->alloc_func = alloc_func;
  mem_pool->free_func = free_func;
  mem_pool->chunk_lock = g_mutex_new ();

  populate (mem_pool);

  return mem_pool;
}

static gboolean
free_area (gpointer key, gpointer value, gpointer user_data)
{
  g_print ("free %p\n", key);
  g_free (key);

  return TRUE;
}

void
gst_mem_pool_destroy (GstMemPool * mem_pool)
{
  GHashTable *elements = g_hash_table_new (NULL, NULL);
  gpointer data;

  mem_pool->cleanup = TRUE;

  data = gst_mem_pool_alloc (mem_pool);
  while (data) {
    GstMemPoolElement *elem = GST_MEM_POOL_LINK (data);

    g_hash_table_insert (elements, GST_MEM_POOL_AREA (elem), NULL);

    data = gst_mem_pool_alloc (mem_pool);
  }
  g_hash_table_foreach_remove (elements, free_area, NULL);

  g_hash_table_destroy (elements);
  g_free (mem_pool->name);
  g_free (mem_pool);
}

gpointer
gst_mem_pool_alloc (GstMemPool * mem_pool)
{
  volatile GstMemPoolElement *pool = NULL;

  g_return_val_if_fail (mem_pool != NULL, NULL);

again:
#if defined(USE_ASM) && defined(HAVE_CPU_I386)
__asm__ __volatile__ ("  testl %%eax, %%eax 		\n\t" "  jz 20f 			\n" "10:				\t" "  movl (%%eax), %%ebx  	\n\t" "  movl %%edx, %%ecx    	\n\t" "  incl %%ecx 			\n\t" POOL_LOCK "cmpxchg8b %1 	\n\t" "  jz 20f 			\n\t" "  testl %%eax, %%eax 		\n\t" "  jnz 10b 			\n" "20:\t":"=a" (pool)
:    "m" (*mem_pool), "a" (mem_pool->free), "d" (mem_pool->cnt)
:    "ecx", "ebx");
#else
  g_mutex_lock (mem_pool->chunk_lock);
  if (mem_pool->free) {
    pool = mem_pool->free;
    mem_pool->free = pool->link;
  }
  g_mutex_unlock (mem_pool->chunk_lock);
#endif

  if (!pool) {
    /*g_print ("extending\n"); */
    if (populate (mem_pool))
      goto again;
    else
      return NULL;
  }
  return GST_MEM_POOL_DATA (pool);
}

gpointer
gst_mem_pool_alloc0 (GstMemPool * mem_pool)
{
  gpointer mem = gst_mem_pool_alloc (mem_pool);

  if (mem)
    memset (mem, 0, mem_pool->atom_size);

  return mem;
}

void
gst_mem_pool_free (GstMemPool * mem_pool, gpointer mem)
{
  GstMemPoolElement *pool;

  g_return_if_fail (mem_pool != NULL);
  g_return_if_fail (mem != NULL);

  pool = GST_MEM_POOL_LINK (mem);

#if defined(USE_ASM) && defined(HAVE_CPU_I386)
  __asm__ __volatile__ ("1:				\t"
      "  movl %2, (%1) 		\n"
      POOL_LOCK "cmpxchg %1, %0 	\n\t"
      "  jnz 1b 			\n\t"::
      "m" (*mem_pool), "r" (pool), "a" (mem_pool->free));
#else
  g_mutex_lock (mem_pool->chunk_lock);
  pool->link = (GstMemPoolElement *) mem_pool->free;
  mem_pool->free = pool;
  g_mutex_unlock (mem_pool->chunk_lock);
#endif
}
