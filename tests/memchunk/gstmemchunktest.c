#include <string.h>		/* strerror */
#include <stdlib.h>		/* strerror */
#include <gst/gst.h>
#include "gstmemchunk.h"

#define MAX_THREADS  100

static GstMemChunk *_chunks;

static gint num_allocs;
static gint num_threads;

static gpointer
alloc_chunk (void)
{
  gpointer ret;

  ret = gst_mem_chunk_alloc (_chunks);

  return ret;
}

static void
free_chunk (gpointer chunk)
{
  gst_mem_chunk_free (_chunks, chunk);
}


void *
run_test (void *threadid)
{
  gint i;
  gpointer chunk;

  g_usleep (G_USEC_PER_SEC);

  for (i = 0; i < num_allocs; i++) {
    chunk = alloc_chunk ();
    free_chunk (chunk);
  }

  g_thread_exit (NULL);
  return NULL;
}


gint
main (gint argc, gchar * argv[])
{
  GThread *threads[MAX_THREADS];
  GError *error;
  int t;

  gst_init (&argc, &argv);

  if (argc != 3) {
    g_print ("usage: %s <num_threads> <num_allocs>\n", argv[0]);
    exit (-1);
  }

  num_threads = atoi (argv[1]);
  num_allocs = atoi (argv[2]);

  _chunks = gst_mem_chunk_new ("test", 32, 32 * 16, G_ALLOC_AND_FREE);

  for (t = 0; t < num_threads; t++) {
    error = NULL;
    threads[t] = g_thread_create (run_test, GINT_TO_POINTER (t), TRUE, &error);
    if (error) {
      printf ("ERROR: g_thread_create() %s\n", error->message);
      exit (-1);
    }
  }
  printf ("main(): Created %d threads.\n", t);

  for (t = 0; t < num_threads; t++) {
    g_thread_join (threads[t]);
  }
  g_mem_chunk_info ();

  gst_mem_chunk_destroy (_chunks);

  return 0;
}
