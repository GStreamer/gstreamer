/* GStreamer
 * Copyright (C) 2005 Andy Wingo <wingo at pobox.com>
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


#include <stdlib.h>
#include <glib.h>
#include <gmodule.h>
#include <gst/gstmemchunk.h>


#define MAX_THREADS  100
#define CHUNK_SIZE 32
#define GMEMCHUNK_THREADSAFE


typedef gpointer (*alloc_func_t) (void);
typedef void (*free_func_t) (gpointer);


static gint num_allocs;
static gint num_threads;
static alloc_func_t _alloc = NULL;
static free_func_t _free = NULL;

static GMemChunk *_gmemchunk;
static GMutex *_gmemchunklock;
static GstMemChunk *_gstmemchunk;

static GCond *ready_cond;
static GCond *start_cond;
static GMutex *sync_mutex;


static gdouble
get_current_time (void)
{
  GTimeVal tv;

  g_get_current_time (&tv);
  return tv.tv_sec + ((gdouble) tv.tv_usec) / G_USEC_PER_SEC;
}


/*
 * GMemChunk implementation
 */

static gpointer
gmemchunk_alloc (void)
{
  gpointer ret;

#ifdef GMEMCHUNK_THREADSAFE
  g_mutex_lock (_gmemchunklock);
#endif
  ret = g_mem_chunk_alloc (_gmemchunk);
#ifdef GMEMCHUNK_THREADSAFE
  g_mutex_unlock (_gmemchunklock);
#endif

  return ret;
}

static void
gmemchunk_free (gpointer chunk)
{
#ifdef GMEMCHUNK_THREADSAFE
  g_mutex_lock (_gmemchunklock);
#endif
  g_mem_chunk_free (_gmemchunk, chunk);
#ifdef GMEMCHUNK_THREADSAFE
  g_mutex_unlock (_gmemchunklock);
#endif
}

/*
 * GstMemChunk implementation
 */

static gpointer
gstmemchunk_alloc (void)
{
  return gst_mem_chunk_alloc (_gstmemchunk);
}

static void
gstmemchunk_free (gpointer chunk)
{
  gst_mem_chunk_free (_gstmemchunk, chunk);
}

/*
 * Normal (malloc/free) implementation
 */

static gpointer
normal_alloc (void)
{
  return g_malloc (CHUNK_SIZE);
}

static void
normal_free (gpointer chunk)
{
  g_free (chunk);
}

/*
 * Normal (malloc/free) implementation
 */

void *(*_google_malloc) (gsize) = NULL;
void (*_google_free) (void *) = NULL;
static gpointer
google_alloc (void)
{
  return _google_malloc (CHUNK_SIZE);
}

static void
google_free (gpointer chunk)
{
  _google_free (chunk);
}

/*
 * The test
 */

void *
worker_thread (void *threadid)
{
  gint i;
  gpointer chunk;

  g_mutex_lock (sync_mutex);
  g_cond_signal (ready_cond);
  g_cond_wait (start_cond, sync_mutex);
  g_mutex_unlock (sync_mutex);

  for (i = 0; i < num_allocs; i++) {
    chunk = _alloc ();
    _free (chunk);
  }

  return NULL;
}

gdouble
run_test (alloc_func_t alloc_func, free_func_t free_func)
{
  gdouble start, end;
  GThread *threads[MAX_THREADS];
  GError *error;
  int t;

  _alloc = alloc_func;
  _free = free_func;

  g_mutex_lock (sync_mutex);
  for (t = 0; t < num_threads; t++) {
    error = NULL;
    threads[t] =
        g_thread_create (worker_thread, GINT_TO_POINTER (t), TRUE, &error);
    g_assert (threads[t]);
    g_cond_wait (ready_cond, sync_mutex);
  }

  g_cond_broadcast (start_cond);
  start = get_current_time ();
  g_mutex_unlock (sync_mutex);

  for (t = 0; t < num_threads; t++)
    g_thread_join (threads[t]);

  end = get_current_time ();

  return end - start;
}

gint
main (gint argc, gchar * argv[])
{
  gdouble time;
  GModule *google_lib;

  g_thread_init (NULL);

  ready_cond = g_cond_new ();
  start_cond = g_cond_new ();
  sync_mutex = g_mutex_new ();

  if (argc != 3) {
    g_print ("usage: %s <num_threads> <num_allocs>\n", argv[0]);
    exit (-1);
  }

  num_threads = atoi (argv[1]);
  num_allocs = atoi (argv[2]);
  g_assert (num_threads > 0);
  g_assert (num_allocs > 0);

  _gmemchunk =
      g_mem_chunk_new ("test", CHUNK_SIZE, CHUNK_SIZE * 16, G_ALLOC_ONLY);
  _gmemchunklock = g_mutex_new ();
  _gstmemchunk =
      gst_mem_chunk_new ("test", CHUNK_SIZE, CHUNK_SIZE * 16, G_ALLOC_ONLY);

  g_print ("%d alloc+frees X %d threads\n", num_allocs, num_threads);
  time = run_test (gmemchunk_alloc, gmemchunk_free);
  g_print ("%fs (%fs/thread) - GMemChunk\n", time, time / num_threads);
  time = run_test (gstmemchunk_alloc, gstmemchunk_free);
  g_print ("%fs (%fs/thread) - GstMemChunk\n", time, time / num_threads);
  time = run_test (normal_alloc, normal_free);
  g_print ("%fs (%fs/thread) - g_malloc/g_free\n", time, time / num_threads);

  google_lib = g_module_open ("libtcmalloc.so", G_MODULE_BIND_LOCAL);
  if (google_lib) {
    gpointer sym;

    g_module_symbol (google_lib, "malloc", &sym);
    g_assert (sym);
    _google_malloc = sym;
    g_module_symbol (google_lib, "free", &sym);
    g_assert (sym);
    _google_free = sym;
    time = run_test (google_alloc, google_free);
    g_print ("%fs (%fs/thread) - google malloc/free\n", time,
        time / num_threads);
  } else {
    g_print ("google malloc unavailable: %s\n", g_module_error ());
  }

  /* g_mem_chunk_info (); */
  return 0;
}
