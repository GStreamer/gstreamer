#include <string.h>             /* strerror */
#include <stdlib.h>             /* strerror */
#include <gst/gst.h>

#define MAX_THREADS  100

static GMemChunk *_chunks;
static GMutex *_lock;

static gint num_allocs;
static gint num_threads;

static gpointer
alloc_chunk (void)
{
  gpointer ret;

  g_mutex_lock (_lock);
  ret = g_mem_chunk_alloc (_chunks);
  g_mutex_unlock (_lock);

  return ret;
}

static void
free_chunk (gpointer chunk)
{
  g_mutex_lock (_lock);
  g_mem_chunk_free (_chunks, chunk);
  g_mutex_unlock (_lock);
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

  _chunks = g_mem_chunk_new ("test", 32, 32 * 16, G_ALLOC_AND_FREE);
  _lock = g_mutex_new ();

  for (t = 0; t < num_threads; t++) {
    error = NULL;
    threads[t] = g_thread_create (run_test, GINT_TO_POINTER (t), TRUE, &error);
    if (error) {
      printf ("ERROR: g_thread_create () is %s\n", error->message);
      exit (-1);
    }
  }
  printf ("main(): Created %d threads.\n", t);

  g_thread_exit (NULL);
  g_mem_chunk_info ();
  return 0;
}
