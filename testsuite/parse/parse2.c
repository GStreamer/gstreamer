/*
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * parse1.c: Test common pipelines (need various plugins)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gst/gst.h>

#include <string.h>

/* variables used by the TEST_* macros */
static gint test = 0;
static guint iterations;
static GstElement *cur = NULL;
static GError *error = NULL;
static char *audio_file = NULL;
static char *video_file = NULL;

/* variables needed for checking */

#define TEST_CHECK_FAIL(condition) G_STMT_START{ \
  if (condition) { \
    g_print ("TEST %2d line %3d    OK\n", test, __LINE__); \
  } else { \
    g_print ("TEST %2d line %3d  FAILED : %s\n", test, __LINE__, #condition); \
    return -test; \
  } \
}G_STMT_END
#ifdef G_HAVE_ISO_VARARGS
#define TEST_START(...) G_STMT_START{ \
  gchar *pipeline = g_strdup_printf (__VA_ARGS__); \
  g_print ("TEST %2d line %3d  START   : %s\n", ++test, __LINE__, pipeline); \
  cur = gst_parse_launch (pipeline, &error); \
  if (error == NULL) { \
    g_print ("TEST %2d line %3d CREATED\n", test, __LINE__); \
  } else { \
    g_print ("TEST %2d line %3d  FAILED  : %s\n", test, __LINE__, error->message); \
    g_error_free (error); \
    return -test; \
  } \
  g_free (pipeline); \
}G_STMT_END
#elif defined(G_HAVE_GNUC_VARARGS)
#define TEST_START(pipe...) G_STMT_START{ \
  gchar *pipeline = g_strdup_printf ( ## pipe ); \
  g_print ("TEST %2d line %3d  START   : %s\n", ++test, __LINE__, pipeline); \
  cur = gst_parse_launch (pipeline, &error); \
  if (error == NULL) { \
    g_print ("TEST %2d line %3d CREATED\n", test, __LINE__); \
  } else { \
    g_print ("TEST %2d line %3d  FAILED  : %s\n", test, __LINE__, error->message); \
    g_error_free (error); \
    return -test; \
  } \
  g_free (pipeline); \
}G_STMT_END
#else
#error Please fix this macro here
#define TEST_START(pipe...) G_STMT_START{ \
  gchar *pipeline = g_strdup_printf (__VA_ARGS__); \
  g_print ("TEST %2d line %3d  START   : %s\n", ++test, __LINE__, pipeline); \
  cur = gst_parse_launch (pipeline, &error); \
  if (error == NULL) { \
    g_print ("TEST %2d line %3d CREATED\n", test, __LINE__); \
  } else { \
    g_print ("TEST %2d line %3d  FAILED  : %s\n", test, __LINE__, error->message); \
    g_error_free (error); \
    return -test; \
  } \
  g_free (pipeline); \
}G_STMT_END
#endif
#define TEST_OK G_STMT_START{ \
  gst_object_unref (GST_OBJECT (cur)); \
  cur = NULL; \
  g_print ("TEST %2d line %3d COMPLETE\n", test, __LINE__); \
}G_STMT_END
#define TEST_RUN(iters) G_STMT_START{ \
  gint it = iters; \
  g_print ("TEST %2d line %3d   RUN\n", test, __LINE__); \
  if (gst_element_set_state (cur, GST_STATE_PLAYING) == GST_STATE_FAILURE) { \
    g_print ("TEST %2d line %3d  FAILED  : pipeline could not be set to state PLAYING\n", test, __LINE__); \
    return -test; \
  } \
  iterations = 0; \
  while (gst_bin_iterate (GST_BIN (cur)) && it != 0) { \
    iterations++; \
    it--; \
  } \
  if (gst_element_set_state (cur, GST_STATE_NULL) == GST_STATE_FAILURE) { \
    g_print ("TEST %2d line %3d  FAILED  : pipeline could not be reset to state NULL\n", test, __LINE__); \
    return -test; \
  } \
  g_print ("TEST %2d line %3d STOPPED  : %u iterations\n", test, __LINE__, iterations); \
}G_STMT_END
#define TEST_FINISH G_STMT_START{ \
  g_print("\n"); \
  g_print("To run this test there are things required that you do not have. (see above)\n"); \
  g_print("Please correct the above mentioned problem if you want to run this test.\n"); \
  g_print("Currently the following tests will be ignored.\n"); \
  g_print("\n"); \
  exit (0); \
}G_STMT_END
#define TEST_REQUIRE(condition, error) G_STMT_START{ \
  if (condition) { \
    g_print ("REQUIRE line %3d    OK\n", __LINE__); \
  } else { \
    g_print ("REQUIRE line %3d   EXIT   : %s\n", __LINE__, (error)); \
    TEST_FINISH; \
  } \
}G_STMT_END
#define TEST_REQUIRE_ELEMENT(element_name) G_STMT_START{ \
  GstElement *element = gst_element_factory_make ((element_name), NULL); \
  if (element) { \
    g_print ("REQUIRE line %3d    OK\n", __LINE__); \
    gst_object_unref (GST_OBJECT (element)); \
  } else { \
    g_print ("REQUIRE line %3d   EXIT   : No element of type \"%s\" available. Exiting.\n", __LINE__, (element_name)); \
    TEST_FINISH; \
  } \
}G_STMT_END

#define PIPELINE1 "filesrc blocksize =8192  location=%s ! mad ! osssink"
#define PIPELINE2 "filesrc location=%s ! mpegdemux ! mpeg2dec ! xvimagesink"
#define PIPELINE3 "filesrc location=%s ! mpegdemux name = demux ! mpeg2dec ! { queue ! xvimagesink } demux.audio_00 ! mad ! osssink"
#define PIPELINE4 "pipeline. ( { filesrc location=%s ! spider name=spider ! { queue ! volume ! ( tee name=tee ! { queue ! ( goom ) ! colorspace ! ( xvimagesink ) } tee. ! { queue ! ( osssink ) } ) } spider. ! { queue ! colorspace ( xvimagesink ) } } )"
#define PIPELINE5 "pipeline. ( { filesrc location=%s ! spider name=spider ! ( tee name=tee ! { queue ! spider ! ( goom ) ! colorspace ! ( xvimagesink ) } tee. ! { queue ! volume ! ( osssink ) } ) spider. ! { queue! colorspace ( xvimagesink ) } } )"

/* FIXME: Should this run, too?
#define PIPELINE3 "filesrc location=%s ! mpegdemux name = demux ! mpeg2dec ! { queue ! xvimagesink } demux.audio_%%02d ! mad ! osssink"
*/

gint
main (gint argc, gchar * argv[])
{
  gst_init (&argc, &argv);

  goto here;
here:

  /**
   * checks:
   * - default playback pipeline
   * - unsigned parameters
   */
  audio_file = g_build_filename (g_get_home_dir (), "music.mp3", NULL);
  TEST_REQUIRE (g_file_test (audio_file, G_FILE_TEST_EXISTS),
      "The following tests requires a valid mp3 file music.mp3 in your home directory.");
  TEST_REQUIRE_ELEMENT ("mad");
  TEST_REQUIRE_ELEMENT ("osssink");
  TEST_START (PIPELINE1, audio_file);
  TEST_RUN (10);
  TEST_OK;

  /**
   * checks:
   * - default video playback pipeline (without audio)
   * - SOMETIMES pads
   */
  video_file = g_build_filename (g_get_home_dir (), "video.mpeg", NULL);
  TEST_REQUIRE (g_file_test (video_file, G_FILE_TEST_EXISTS),
      "The following tests requires a valid mpeg file video.mpeg in your home directory.");
  TEST_REQUIRE_ELEMENT ("mpegdemux");
  TEST_REQUIRE_ELEMENT ("mpeg2dec");
  TEST_REQUIRE_ELEMENT ("xvimagesink");
  TEST_START (PIPELINE2, video_file);
  TEST_RUN (50);
  TEST_OK;

  /**
   * checks:
   * - default video playback pipeline (with audio)
   * - more SOMETIMES pads
   */
  TEST_START (PIPELINE3, video_file);
  TEST_RUN (200);
  TEST_OK;

  /**
   * checks:
   * - default new gst-player pipeline
   */
  TEST_START (PIPELINE4, video_file);
  TEST_RUN (500);
  TEST_OK;

  /**
   * checks:
   * - default old gst-player pipeline
   */
  TEST_START (PIPELINE5, video_file);
  TEST_RUN (500);
  TEST_OK;

  g_free (audio_file);
  g_free (video_file);
  return 0;
}
