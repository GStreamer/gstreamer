/*
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * parse1.c: Test various parsing stuff
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
#include <unistd.h>

/* variables used by the TEST_* macros */
static gint test = 0;
static guint iterations;
static GstElement *cur = NULL;
static GError *error = NULL;

/* variables needed for checking */
static gint i;
static gboolean b;
static gchar *s;

#define TEST_CHECK_FAIL(condition) G_STMT_START{ \
  if (condition) { \
    g_print ("TEST %2d line %3d    OK\n", test, __LINE__); \
  } else { \
    g_print ("TEST %2d line %3d  FAILED : %s\n", test, __LINE__, #condition); \
    return -test; \
  } \
}G_STMT_END
#define TEST_START(pipeline) G_STMT_START{ \
  g_print ("TEST %2d line %3d  START   : %s\n", ++test, __LINE__, pipeline); \
  cur = gst_parse_launch (pipeline, &error); \
  if (error == NULL) { \
    g_print ("TEST %2d line %3d CREATED\n", test, __LINE__); \
  } else { \
    g_print ("TEST %2d line %3d  FAILED  : %s\n", test, __LINE__, error->message); \
    g_error_free (error); \
    return -test; \
  } \
}G_STMT_END
#define TEST_OK G_STMT_START{ \
  gst_object_unref (GST_OBJECT (cur)); \
  cur = NULL; \
  g_print ("TEST %2d line %3d COMPLETE\n", test, __LINE__); \
}G_STMT_END
#define TEST_RUN G_STMT_START{ \
  alarm(10); \
  g_print ("TEST %2d line %3d   RUN\n", test, __LINE__); \
  if (gst_element_set_state (cur, GST_STATE_PLAYING) == GST_STATE_FAILURE) { \
    g_print ("TEST %2d line %3d  FAILED  : pipeline could not be set to state PLAYING\n", test, __LINE__); \
    return -test; \
  } \
  iterations = 0; \
  while (gst_bin_iterate (GST_BIN (cur))) iterations++; \
  if (gst_element_set_state (cur, GST_STATE_NULL) == GST_STATE_FAILURE) { \
    g_print ("TEST %2d line %3d  FAILED  : pipeline could not be reset to state NULL\n", test, __LINE__); \
    return -test; \
  } \
  g_print ("TEST %2d line %3d STOPPED  : %u iterations\n", test, __LINE__, iterations); \
  alarm(0); \
}G_STMT_END
#define PIPELINE1  "fakesrc"
#define PIPELINE2  "fakesrc name=donald num-buffers= 27 silent =TruE sizetype = 3 eos  =    falSe data=   Subbuffer\\ data"
#define PIPELINE3  "fakesrc identity fakesink"
#define PIPELINE4  "fakesrc num-buffers=4 .src ! identity !.sink identity .src ! .sink fakesink"
#define PIPELINE5  "fakesrc num-buffers=4 name=src identity name=id1 identity name = id2 fakesink name =sink src. ! id1. id1.! id2.sink id2.src!sink.sink"
#define PIPELINE6  "pipeline.(name=\"john\" fakesrc num-buffers=4 ( thread. ( ! queue ! identity !{ queue ! fakesink }) ))"
#define PIPELINE7  "fakesrc num-buffers=4 ! tee name=tee .src%d! fakesink tee.src%d ! fakesink fakesink name =\"foo\" tee.src%d ! foo."
/* aggregator is borked
#define PIPELINE8  "fakesrc num-buffers=4 ! tee name=tee1 .src0,src1 ! .sink0, sink1 aggregator ! fakesink"
*/
#define PIPELINE8  "fakesrc num-buffers=4 ! fakesink"
#define PIPELINE9  "fakesrc num-buffers=4 ! test. fakesink name=test"
#define PIPELINE10 "( fakesrc num-buffers=\"4\" ! ) identity ! fakesink"
#define PIPELINE11 "fakesink name = sink identity name=id ( fakesrc num-buffers=\"4\" ! id. ) id. ! sink."


gint
main (gint argc, gchar * argv[])
{
  gst_init (&argc, &argv);

  /**
   * checks:
   * - specifying an element works :)
   * - if only 1 element is requested, no bin is returned, but the element
   */
  TEST_START (PIPELINE1);
  TEST_CHECK_FAIL (G_OBJECT_TYPE (cur) == g_type_from_name ("GstFakeSrc"));
  TEST_OK;

  /**
   * checks:
   * - properties works
   * - string, int, boolean and enums can be properly set (note: eos should be false)
   * - first test of escaping strings
   */
  TEST_START (PIPELINE2);
  g_object_get (G_OBJECT (cur), "name", &s, "num-buffers", &i, "silent", &b,
      NULL);
  TEST_CHECK_FAIL (strcmp (s, "donald") == 0);
  TEST_CHECK_FAIL (i == 27);
  TEST_CHECK_FAIL (b == TRUE);
  g_object_get (G_OBJECT (cur), "eos", &b, "sizetype", &i, NULL);
  TEST_CHECK_FAIL (i == 3);
  TEST_CHECK_FAIL (b == FALSE);
  g_object_get (G_OBJECT (cur), "data", &i, NULL);
  TEST_CHECK_FAIL (i == 2);
  TEST_OK;

  /**
   * checks:
   * - specifying multiple elements without links works
   * - if multiple toplevel elements exist, a pipeline is returned
   */
  TEST_START (PIPELINE3);
  TEST_CHECK_FAIL (GST_BIN (cur)->numchildren == 3);    /* a bit hacky here */
  TEST_CHECK_FAIL (GST_IS_PIPELINE (cur));
  TEST_OK;

  /**
   * checks:
   * - test default link "!"
   * - test if specifying pads on links works
   */
  TEST_START (PIPELINE4);
  TEST_RUN;
  TEST_OK;

  /**
   * checks:
   * - test if appending the links works, too
   * - check if the pipeline constructed works the same as the one before (how?)
   */
  TEST_START (PIPELINE5);
  TEST_RUN;
  TEST_OK;

  /**
   * checks:
   * - test various types of bins
   * - test if linking across bins works
   * - test if escaping strings works
   */
  TEST_START (PIPELINE6);
  TEST_CHECK_FAIL (GST_IS_PIPELINE (cur));
  g_object_get (G_OBJECT (cur), "name", &s, NULL);
  TEST_CHECK_FAIL (strcmp (s, "john") == 0);
  TEST_RUN;
  TEST_OK;

  /**
   * checks:
   * - test request pads
   */
  TEST_START (PIPELINE7);
  TEST_RUN;
  TEST_OK;

  /**
   * checks:
   * - multiple pads on 1 link
   */
  TEST_START (PIPELINE8);
  TEST_RUN;
  TEST_OK;

  /**
   * checks:
   * - failed in grammar.y cvs version 1.17
   */
  TEST_START (PIPELINE9);
  TEST_RUN;
  TEST_OK;

  /**
   * checks:
   * - failed in grammar.y cvs version 1.17
   */
  TEST_START (PIPELINE10);
  TEST_RUN;
  TEST_OK;

  /**
   * checks:
   * - failed in grammar.y cvs version 1.18
   */
  TEST_START (PIPELINE11);
  TEST_RUN;
  TEST_OK;

  return 0;
}
