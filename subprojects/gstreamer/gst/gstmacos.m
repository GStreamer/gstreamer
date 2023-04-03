#include "gstmacos.h"
#include <Cocoa/Cocoa.h>

typedef struct _ThreadArgs ThreadArgs;

struct _ThreadArgs {
  void* main_func;
  int argc;
  char **argv;
  gpointer user_data;
  gboolean is_simple;
};

int
gst_thread_func (ThreadArgs *args)
{
  int ret;
  if (args->is_simple) {
    ret = ((GstMainFuncSimple) args->main_func) (args->user_data);
  } else {
    ret = ((GstMainFunc) args->main_func) (args->argc, args->argv, args->user_data);
  }

  [NSApp terminate: nil];
  return ret;
}

int
run_main_with_nsapp (ThreadArgs args)
{
  GThread *gst_thread;

  [NSApplication sharedApplication]; 
  if ([NSApp activationPolicy] == NSApplicationActivationPolicyProhibited) {
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
  }
  gst_thread = g_thread_new ("macos-gst-thread", (GThreadFunc) gst_thread_func, &args);
  [NSApp run];

  return GPOINTER_TO_INT (g_thread_join (gst_thread));
}

/**
 * gst_macos_main:
 * @main_func: (scope async): pointer to the main function to be called
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
int
gst_macos_main (GstMainFunc main_func, int argc, char **argv, gpointer user_data)
{
  ThreadArgs args;

  args.argc = argc;
  args.argv = argv;
  args.main_func = main_func;
  args.user_data = user_data;
  args.is_simple = FALSE;

  return run_main_with_nsapp (args);
}

/**
 * gst_macos_main_simple:
 * @main_func: (scope async): pointer to the main function to be called
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
int 
gst_macos_main_simple (GstMainFuncSimple main_func, gpointer user_data)
{
  ThreadArgs args;

  args.argc = 0;
  args.argv = NULL;
  args.main_func = main_func;
  args.user_data = user_data;
  args.is_simple = TRUE;

  return run_main_with_nsapp (args);
}
