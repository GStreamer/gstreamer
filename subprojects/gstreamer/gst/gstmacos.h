#ifndef __GST_MACOS_H__
#define __GST_MACOS_H__

#include <gst/gstconfig.h>
#include <gst/glib-compat.h>

G_BEGIN_DECLS

/**
 * GstMainFunc:
 * @argc: the amount of arguments passed in @argv
 * @argv: (array length=argc): an array of arguments passed to the main function
 * @user_data: (nullable): user data passed to the main function
 *
 * Represents a simple pointer to the main() function of a program.
 * It is used to pass that function along with its arguments to
 * gst_macos_main(), which ensures correct behaviour of various
 * GStreamer elements (e.g glimagesink) on macOS.
 */
typedef int (*GstMainFunc) (int argc, char** argv, gpointer user_data);

/**
 * GstMainFuncSimple:
 * @user_data: (nullable): user data passed to the main function
 *
 * Simplified version of #GstMainFunc, meant to be used with
 * gst_macos_main_simple(), which does not require argc/argv to be passed.
 */
typedef int (*GstMainFuncSimple) (gpointer user_data);

GST_API
int              gst_macos_main                   (GstMainFunc main_func,
                                                   int argc,
                                                   char *argv[],
                                                   gpointer user_data);

GST_API
int              gst_macos_main_simple            (GstMainFuncSimple main_func,
                                                   gpointer user_data);

G_END_DECLS

#endif /* __GST_MACOS_H__ */
