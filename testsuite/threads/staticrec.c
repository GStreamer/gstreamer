#include <glib.h>

GStaticRecMutex mutex = G_STATIC_REC_MUTEX_INIT;

static void *
thread1 (void *t)
{
  gint i = 0;

  while (TRUE) {
    g_static_rec_mutex_lock (&mutex);
    if (i++ % 100000 == 0)
      g_print ("*");
    g_static_rec_mutex_unlock (&mutex);
    if (i++ % 100000 == 0)
      g_print ("*");
  }
  return NULL;
}

static void *
thread2 (void *t)
{
  gint i = 0;

  while (TRUE) {
    g_static_rec_mutex_lock (&mutex);
    if (i++ % 100000 == 0)
      g_print (".");
    g_static_rec_mutex_unlock (&mutex);
    if (i++ % 100000 == 0)
      g_print (".");
  }
  return NULL;
}

int
main (gint argc, gchar * argv[])
{
  g_thread_init (NULL);
  g_thread_create_full (thread1,
      NULL, 0x200000, FALSE, TRUE, G_THREAD_PRIORITY_NORMAL, NULL);
  g_thread_create_full (thread2,
      NULL, 0x200000, FALSE, TRUE, G_THREAD_PRIORITY_NORMAL, NULL);

  g_usleep (G_MAXLONG);

  return 0;
}
