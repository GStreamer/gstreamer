/*
 *   Debugging implementation of MALLOC and friends
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "mem.h"

#if defined(DBG_MEMLEAKS)

typedef struct
{
  void *mem;
  char *allocated_in_func;
  char *allocated_in_file;
  unsigned int allocated_in_line;
}
MemDesc;


static int initialized = 0;
static int alloc_count = 0;
static MemDesc *alloc_list = NULL;


static void
dbg_memleaks_done (int exitcode, void *dummy)
{
  unsigned int i;

  (void) dummy;

  if (exitcode == 0 && alloc_count != 0) {
    fprintf (stderr, "\nmemory leak detected !!!\n");
    fprintf (stderr, "\nalloc_count == %i\n\n", alloc_count);
    for (i = 0; i < alloc_count; i++) {
      MemDesc *d = &alloc_list[i];

      fprintf (stderr, "chunk %p allocated in %s (%s: %u) not free'd !!\n",
          d->mem, d->allocated_in_func, d->allocated_in_file,
          d->allocated_in_line);
    }
    free (alloc_list);
  }
  fprintf (stderr, "\n");
}


static void
dbg_memleaks_init (void)
{
  on_exit (dbg_memleaks_done, NULL);
  initialized = 1;
}


void *
dbg_malloc (char *file, int line, char *func, size_t bytes)
{
  void *mem = (void *) malloc (bytes);
  MemDesc *d;

  if (!initialized)
    dbg_memleaks_init ();

  alloc_count++;
  alloc_list = realloc (alloc_list, alloc_count * sizeof (MemDesc));

  d = &alloc_list[alloc_count - 1];
  d->mem = mem;
  d->allocated_in_func = func;
  d->allocated_in_file = file;
  d->allocated_in_line = line;

  return mem;
}


void *
dbg_calloc (char *file, int line, char *func, size_t count, size_t bytes)
{
  void *mem = (void *) calloc (count, bytes);
  MemDesc *d;

  if (!initialized)
    dbg_memleaks_init ();

  alloc_count++;
  alloc_list = realloc (alloc_list, alloc_count * sizeof (MemDesc));

  d = &alloc_list[alloc_count - 1];
  d->mem = mem;
  d->allocated_in_func = func;
  d->allocated_in_file = file;
  d->allocated_in_line = line;

  return mem;
}


void *
dbg_realloc (char *file, int line, char *func, char *what,
    void *mem, size_t bytes)
{
  unsigned int i;

  for (i = 0; i < alloc_count; i++) {
    if (alloc_list[i].mem == mem) {
      alloc_list[i].mem = (void *) realloc (mem, bytes);
      return alloc_list[i].mem;
    }
  }

  if (mem != NULL) {
    fprintf (stderr,
        "%s: trying to reallocate unknown chunk %p (%s)\n"
        "          in %s (%s: %u) !!!\n",
        __FUNCTION__, mem, what, func, file, line);
    exit (-1);
  }

  return dbg_malloc (file, line, func, bytes);
}


void
dbg_free (char *file, int line, char *func, char *what, void *mem)
{
  unsigned int i;

  if (!initialized)
    dbg_memleaks_init ();

  for (i = 0; i < alloc_count; i++) {
    if (alloc_list[i].mem == mem) {
      free (mem);
      alloc_count--;
      memmove (&alloc_list[i], &alloc_list[i + 1],
          (alloc_count - i) * sizeof (MemDesc));
      return;
    }
  }

  fprintf (stderr, "%s: trying to free unknown chunk %p (%s)\n"
      "          in %s (%s: %u) !!!\n",
      __FUNCTION__, mem, what, func, file, line);
  exit (-1);
}


#endif
