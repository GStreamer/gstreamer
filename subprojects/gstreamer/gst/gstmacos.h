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

/**
 * gst_macos_main:
 * @main_func: (scope call): pointer to the main function to be called
 * @argc: the amount of arguments passed in @argv
 * @argv: (array length=argc): an array of arguments to be passed to the main function
 * @user_data: (nullable): user data to be passed to the main function
 *
 * Starts an NSApplication on the main thread before calling
 * the provided main() function on a secondary thread.
 *
 * This ensures that GStreamer can correctly perform actions
 * such as creating a GL window, which require a Cocoa main loop
 * to be running on the main thread.
 *
 * Do not call this function more than once - especially while
 * another one is still running - as that will cause unpredictable
 * behaviour and most likely completely fail.
 *
 * Returns: the return value of the provided main_func
 *
 * Since: 1.22
 */
GST_API
int              gst_macos_main                   (GstMainFunc main_func,
                                                   int argc,
                                                   char *argv[],
                                                   gpointer user_data);

/**
 * gst_macos_main_simple:
 * @main_func: (scope call): pointer to the main function to be called
 * @user_data: (nullable): user data to be passed to the main function
 *
 * Simplified variant of gst_macos_main(), meant to be used with bindings
 * for languages which do not have to pass argc and argv like C does.
 * See gst_macos_main() for a more detailed description.
 *
 * Returns: the return value of the provided main_func
 *
 * Since: 1.22
 */
GST_API
int              gst_macos_main_simple            (GstMainFuncSimple main_func,
                                                   gpointer user_data);

G_END_DECLS

#endif /* __GST_MACOS_H__ */
