#include <glib.h>

typedef struct _GstMemPool GstMemPool;
typedef struct _GstMemPoolElement GstMemPoolElement;

typedef void (*GstMemPoolAllocFunc) (GstMemPool * pool, gpointer data);
typedef void (*GstMemPoolFreeFunc) (GstMemPool * pool, gpointer data);

struct _GstMemPoolElement
{
  GstMemPoolElement *link;	/* next cell in the lifo */
  GstMemPoolElement *area;
};

struct _GstMemPool
{
  volatile GstMemPoolElement *free;	/* the first free element */
  volatile gulong cnt;		/* used to avoid ABA problem */

  gchar *name;
  gulong area_size;
  gulong pool_size;
  gulong atom_size;
  gboolean cleanup;
  GstMemPoolAllocFunc alloc_func;
  GstMemPoolFreeFunc free_func;
  GMutex *chunk_lock;
};


GstMemPool *gst_mem_pool_new (gchar * name,
    gint atom_size,
    gulong area_size,
    gint type, GstMemPoolAllocFunc alloc_func, GstMemPoolFreeFunc free_func);

void gst_mem_pool_destroy (GstMemPool * mem_pool);

gpointer gst_mem_pool_alloc (GstMemPool * mem_pool);
gpointer gst_mem_pool_alloc0 (GstMemPool * mem_pool);
void gst_mem_pool_free (GstMemPool * mem_pool, gpointer mem);
