/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-qa-file-checker.h - QA File conformance check utility functions / structs
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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

#ifndef __GST_QA_FILE_CHECK_H__
#define __GST_QA_FILE_CHECK_H__

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>

G_BEGIN_DECLS

#define GST_TYPE_QA_FILE_CHECKER			(gst_qa_file_checker_get_type ())
#define GST_IS_QA_FILE_CHECKER(obj)		        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_QA_FILE_CHECKER))
#define GST_IS_QA_FILE_CHECKER_CLASS(klass)	        (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_QA_FILE_CHECKER))
#define GST_QA_FILE_CHECKER_GET_CLASS(obj)	        (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_QA_FILE_CHECKER, GstQaFileCheckerClass))
#define GST_QA_FILE_CHECKER(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_QA_FILE_CHECKER, GstQaFileChecker))
#define GST_QA_FILE_CHECKER_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_QA_FILE_CHECKER, GstQaFileCheckerClass))
#define GST_QA_FILE_CHECKER_CAST(obj)                ((GstQaFileChecker*)(obj))
#define GST_QA_FILE_CHECKER_CLASS_CAST(klass)        ((GstQaFileCheckerClass*)(klass))

typedef struct _GstQaFileChecker GstQaFileChecker;
typedef struct _GstQaFileCheckerClass GstQaFileCheckerClass;

/**
 * GstQaFileChecker:
 *
 * GStreamer QA FileChecker class.
 *
 * Class that wraps a #GObject for QA checks
 */
struct _GstQaFileChecker {
  GObject 	 object;

  /* <File checking data> */
  /* Value for the expected total duration of the file in nanosecs
   * Set to GST_CLOCK_TIME_NONE if it shouldn't be tested */
  GstClockTime duration;
  /* Acceptable tolerance for duration */
  GstClockTime duration_tolerance;

  /* Expected file_size, set to 0 to skip test */
  guint64 file_size;
  /* Acceptable tolerance for file_size check */
  guint64 file_size_tolerance;

  gboolean seekable; /* TODO should we care about disabling this check? */

  gboolean test_playback;
  gboolean test_reverse_playback;

  gchar *uri;

  /* Set to NULL to skip check */
  GstEncodingProfile *profile;
};

/**
 * GstQaFileCheckerClass:
 * @parent_class: parent
 *
 * GStreamer QA FileChecker object class.
 */
struct _GstQaFileCheckerClass {
  GObjectClass	parent_class;
};

/* normal GObject stuff */
GType		gst_qa_file_checker_get_type  (void);

gboolean        gst_qa_file_checker_run       (GstQaFileChecker * fc);

G_END_DECLS

#endif /* __GST_QA_FILE_CHECK_H__ */

