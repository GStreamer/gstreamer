/* GStreamer
 *
 * unit test for GstObject
 *
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
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

#include <signal.h>
#include <string.h>
#include <stdlib.h>

#include <check.h>

#include <gst/gst.h>


/* FIXME: externalize */
/* logging function for tests
 * a test uses g_message() to log a debug line
 * a gst unit test can be run with GST_TEST_DEBUG env var set to see the
 * messages
 */
gboolean _gst_test_debug = FALSE;

gboolean _gst_test_threads_running = FALSE;

void gst_test_log_func
    (const gchar * log_domain, GLogLevelFlags log_level,
    const gchar * message, gpointer user_data)
{
  // g_print ("HANDLER CALLED\n");
  if (_gst_test_debug) {
    g_print (message);
  }
}

/* initialize GStreamer testing */
void
gst_test_init (void)
{
  if (g_getenv ("GST_TEST_DEBUG"))
    _gst_test_debug = TRUE;

  g_log_set_handler (NULL, G_LOG_LEVEL_MESSAGE, gst_test_log_func, NULL);
}

/*
  Create a fake subclass
 */
typedef struct _GstFakeObjectClass GstFakeObjectClass;
typedef struct _GstFakeObject GstFakeObject;

struct _GstFakeObject
{
  GstObject object;
};

struct _GstFakeObjectClass
{
  GstObjectClass parent_class;
};

GType _gst_fake_object_type = 0;

//static GstObjectClass *parent_class = NULL;
//static guint gst_fake_object_signals[LAST_SIGNAL] = { 0 };

GType
gst_fake_object_get_type (void)
{
  if (!_gst_fake_object_type) {
    static const GTypeInfo fake_object_info = {
      sizeof (GstFakeObjectClass),
      NULL,                     //gst_fake_object_base_class_init,
      NULL,                     //gst_fake_object_base_class_finalize,
      NULL,                     //(GClassInitFunc) gst_fake_object_class_init,
      NULL,
      NULL,
      sizeof (GstFakeObject),
      0,
      NULL,                     //(GInstanceInitFunc) gst_fake_object_init,
      NULL
    };

    _gst_fake_object_type = g_type_register_static (GST_TYPE_OBJECT,
        "GstFakeObject", &fake_object_info, 0);
  }
  return _gst_fake_object_type;
}

/* g_object_new on abstract GstObject should fail */
START_TEST (test_fail_abstract_new)
{
  GstObject *object;

  object = g_object_new (gst_object_get_type (), NULL);
  /* this should assert and segfault, but we add fallbacks anyway */
  fail_unless (object == NULL, "Created an instance of abstract GstObject");
}

END_TEST
/* g_object_new on GstFakeObject should succeed */
START_TEST (test_fake_object_new)
{
  GstObject *object;

  object = g_object_new (gst_fake_object_get_type (), NULL);
  fail_if (object == NULL, "Failed to create instance of GstFakeObject");
  fail_unless (GST_IS_OBJECT (object),
      "GstFakeObject instance is not a GstObject");
}

END_TEST
/* GstFakeObject name tests */
START_TEST (test_fake_object_name)
{
  GstObject *object;
  gchar *name;
  gchar *name2;

  object = g_object_new (gst_fake_object_get_type (), NULL);

  name = gst_object_get_name (object);
  fail_if (name != NULL, "Newly created object has a name");

  /* give a random name by setting with NULL;
   * GstFakeObject class -> fakeobject%d */
  gst_object_set_name (object, NULL);
  name = gst_object_get_name (object);
  fail_if (name == NULL, "Random name was not assigned");
  fail_if (strncmp (name, "fakeobject", 10) != 0,
      "Random name %s does not start with Gst", name);
  g_free (name);

  gst_object_set_name (object, "fake");
  name = gst_object_get_name (object);
  fail_if (name == NULL, "Failed to get name of GstFakeObject");
  fail_if (strcmp (name, "fake") != 0, "Name of GstFakeObject is not 'fake'");

  /* change the gotten name to see that it's a copy and not the original */
  name[0] = 'm';
  name2 = gst_object_get_name (object);
  fail_if (strcmp (name2, "fake") != 0,
      "Copy of object name affected actual object name");
  g_free (name);
  g_free (name2);
}

END_TEST
/***
 * thread test macros and variables
 */
    GList * thread_list = NULL;
GMutex *mutex;
GCond *start_cond;              /* used to notify main thread of thread startups */
GCond *sync_cond;               /* used to synchronize all threads and main thread */

#define MAIN_START_THREADS(count, function, data)		\
MAIN_INIT();							\
MAIN_START_THREAD_FUNCTIONS(count, function, data);		\
MAIN_SYNCHRONIZE();

#define MAIN_INIT()			\
G_STMT_START {				\
  _gst_test_threads_running = TRUE;	\
					\
  mutex = g_mutex_new ();		\
  start_cond = g_cond_new ();		\
  sync_cond = g_cond_new ();		\
} G_STMT_END;

#define MAIN_START_THREAD_FUNCTIONS(count, function, data)	\
G_STMT_START {							\
  int i;							\
  GThread *thread = NULL;					\
  for (i = 0; i < count; ++i) {					\
    g_message ("MAIN: creating thread %d\n", i);		\
    g_mutex_lock (mutex);					\
    thread = g_thread_create ((GThreadFunc) function, data,	\
	TRUE, NULL);						\
    thread_list = g_list_append (thread_list, thread);		\
								\
    /* wait for thread to signal us that it's ready */		\
    g_message ("MAIN: waiting for thread %d\n", i);		\
    g_cond_wait (start_cond, mutex);				\
    g_mutex_unlock (mutex);					\
  }								\
} G_STMT_END;

#define MAIN_SYNCHRONIZE()		\
G_STMT_START {				\
  g_message ("MAIN: synchronizing\n");	\
  g_cond_broadcast (sync_cond);		\
  g_message ("MAIN: synchronized\n");	\
} G_STMT_END;

#define MAIN_STOP_THREADS()					\
G_STMT_START {							\
  _gst_test_threads_running = FALSE;				\
								\
  /* join all threads */					\
  g_message ("MAIN: joining\n");				\
  g_list_foreach (thread_list, (GFunc) g_thread_join, NULL);	\
  g_message ("MAIN: joined\n");					\
} G_STMT_END;

#define THREAD_START()						\
THREAD_STARTED();						\
THREAD_SYNCHRONIZE();

#define THREAD_STARTED()					\
G_STMT_START {							\
  /* signal main thread that we started */			\
  g_message ("THREAD %p: started\n", g_thread_self ());		\
  g_mutex_lock (mutex);						\
  g_cond_signal (start_cond);					\
} G_STMT_END;

#define THREAD_SYNCHRONIZE()					\
G_STMT_START {							\
  /* synchronize everyone */					\
  g_message ("THREAD %p: syncing\n", g_thread_self ());		\
  g_cond_wait (sync_cond, mutex);				\
  g_message ("THREAD %p: synced\n", g_thread_self ());		\
  g_mutex_unlock (mutex);					\
} G_STMT_END;

#define THREAD_TEST_RUNNING()	(_gst_test_threads_running == TRUE)

/* thread function for threaded name change test */
gpointer
thread_name_object (GstObject * object)
{
  gchar *thread_id = g_strdup_printf ("%p", g_thread_self ());

  THREAD_START ();

  /* give main thread a head start */
  g_usleep (100000);

  /* write our name repeatedly */
  g_message ("THREAD %s: starting loop\n", thread_id);
  while (THREAD_TEST_RUNNING ()) {
    gst_object_set_name (object, thread_id);
    /* a minimal sleep invokes a thread switch */
    g_usleep (1);
  }

  /* thread is done, so let's return */
  g_message ("THREAD %s: set name\n", thread_id);
  g_free (thread_id);

  return NULL;
}

/*
 * main thread sets and gets name while other threads set the name
 * constantly; fails because lock is released inbetween set and get
 */

START_TEST (test_fake_object_name_threaded_wrong)
{
  GstObject *object;
  gchar *name;
  gint i;
  gboolean expected_failure = FALSE;

  g_message ("\nTEST: set/get without lock\n");

  object = g_object_new (gst_fake_object_get_type (), NULL);
  gst_object_set_name (object, "main");

  MAIN_START_THREADS (5, thread_name_object, object);

  /* start looping and set/get name repeatedly */
  for (i = 0; i < 1000; ++i) {
    gst_object_set_name (object, "main");
    g_usleep (1);               /* switch */
    name = gst_object_get_name (object);
    if (strcmp (name, "main") != 0) {
      g_message ("MAIN: expected failure during run %d\n", i);
      expected_failure = TRUE;
      g_free (name);
      break;
    }
    g_free (name);
  }
  MAIN_STOP_THREADS ();

  fail_unless (expected_failure, "name did not get changed");
}

END_TEST
/*
 * main thread sets and gets name directly on struct inside the object lock
 * succeed because lock is held during set/get, and threads are locked out
 */
START_TEST (test_fake_object_name_threaded_right)
{
  GstObject *object;
  gchar *name;
  gint i;

  g_message ("\nTEST: set/get inside lock\n");

  object = g_object_new (gst_fake_object_get_type (), NULL);
  gst_object_set_name (object, "main");

  MAIN_START_THREADS (5, thread_name_object, object);

  /* start looping and set/get name repeatedly */
  for (i = 0; i < 1000; ++i) {
    GST_LOCK (object);
    g_free (GST_OBJECT_NAME (object));
    GST_OBJECT_NAME (object) = g_strdup ("main");
    g_usleep (1);               /* switch */
    name = g_strdup (GST_OBJECT_NAME (object));
    GST_UNLOCK (object);

    fail_unless (strcmp (name, "main") == 0,
        "Name got changed while lock held during run %d", i);
    g_free (name);
  }
  MAIN_STOP_THREADS ();
}
END_TEST Suite * gst_object_suite (void)
{
  Suite *s = suite_create ("GstObject");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test_raise_signal (tc_chain, test_fail_abstract_new, SIGSEGV);
  tcase_add_test (tc_chain, test_fake_object_new);
  tcase_add_test (tc_chain, test_fake_object_name);
  tcase_add_test (tc_chain, test_fake_object_name_threaded_wrong);
  tcase_add_test (tc_chain, test_fake_object_name_threaded_right);
  //tcase_add_checked_fixture (tc_chain, setup, teardown);
  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = gst_object_suite ();
  SRunner *sr = srunner_create (s);

  gst_init (&argc, &argv);
  gst_test_init ();

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
