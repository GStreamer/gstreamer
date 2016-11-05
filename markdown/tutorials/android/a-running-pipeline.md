# Android tutorial 2: A running pipeline

### Goal

![screenshot]

The tutorials seen in the [Basic](tutorials/basic/index.md) and
[Playback](tutorials/playback/index.md) sections are intended for Desktop
platforms and, therefore, their main thread is allowed to block (using
`gst_bus_pop_filtered()`) or relinquish control to a GLib main loop. On
Android this would lead to the application being tagged as
non-responsive and probably closed.

This tutorial shows how to overcome this problem. In particular, we will
learn:

  - How to move the native code to its own thread
  - How to allow threads created from C code to communicate with Java
  - How to access Java code from C
  - How to allocate a `CustomData` structure from C and have Java host
    it

### Introduction

When using a Graphical User Interface (UI), if the application waits for
GStreamer calls to complete the user experience will suffer. The usual
approach, with the [GTK+ toolkit](http://www.gtk.org) for example, is to
relinquish control to a GLib `GMainLoop` and let it control the events
coming from the UI or GStreamer.

This approach can be very cumbersome when GStreamer and the Android UI
communicate through the JNI interface, so we take a cleaner route: We
use a GLib main loop, and move it to its own thread, so it does not
block the application. This simplifies the GStreamer-Android
integration, and we only need to worry about a few inter-process
synchronization bits, which are detailed in this tutorial.

Additionally, this tutorial shows how to obtain, from any thread, the
[JNI Environment
pointer](http://docs.oracle.com/javase/1.5.0/docs/guide/jni/spec/design.html#wp16696)
required to make JNI calls. This is necessary, for example, to call Java
code from callbacks in threads spawned deep within GStreamer, which
never received this pointer directly.

Finally, this tutorial explains how to call Java methods from native C
code, which involves locating the desired method’s ID in the class.
These IDs never change, so they are cached as global variables in the C
code and obtained in the static initializer of the class.

The code below builds a pipeline with an `audiotestsrc` and an
`autoaudiosink` (it plays an audible tone). Two buttons in the UI allow
setting the pipeline to PLAYING or PAUSED. A TextView in the UI shows
messages sent from the C code (for errors and state changes).

### A pipeline on Android \[Java code\]

**src/org/freedesktop/gstreamer/tutorials/tutorial\_2/Tutorial2.java**

``` java
package org.freedesktop.gstreamer.tutorials.tutorial_2;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.view.View.OnClickListener;
import android.widget.ImageButton;
import android.widget.TextView;
import android.widget.Toast;

import org.freedesktop.gstreamer.GStreamer;

public class Tutorial2 extends Activity {
    private native void nativeInit();     // Initialize native code, build pipeline, etc
    private native void nativeFinalize(); // Destroy pipeline and shutdown native code
    private native void nativePlay();     // Set pipeline to PLAYING
    private native void nativePause();    // Set pipeline to PAUSED
    private static native boolean nativeClassInit(); // Initialize native class: cache Method IDs for callbacks
    private long native_custom_data;      // Native code will use this to keep private data

    private boolean is_playing_desired;   // Whether the user asked to go to PLAYING

    // Called when the activity is first created.
    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);

        // Initialize GStreamer and warn if it fails
        try {
            GStreamer.init(this);
        } catch (Exception e) {
            Toast.makeText(this, e.getMessage(), Toast.LENGTH_LONG).show();
            finish();
            return;
        }

        setContentView(R.layout.main);

        ImageButton play = (ImageButton) this.findViewById(R.id.button_play);
        play.setOnClickListener(new OnClickListener() {
            public void onClick(View v) {
                is_playing_desired = true;
                nativePlay();
            }
        });

        ImageButton pause = (ImageButton) this.findViewById(R.id.button_stop);
        pause.setOnClickListener(new OnClickListener() {
            public void onClick(View v) {
                is_playing_desired = false;
                nativePause();
            }
        });

        if (savedInstanceState != null) {
            is_playing_desired = savedInstanceState.getBoolean("playing");
            Log.i ("GStreamer", "Activity created. Saved state is playing:" + is_playing_desired);
        } else {
            is_playing_desired = false;
            Log.i ("GStreamer", "Activity created. There is no saved state, playing: false");
        }

        // Start with disabled buttons, until native code is initialized
        this.findViewById(R.id.button_play).setEnabled(false);
        this.findViewById(R.id.button_stop).setEnabled(false);

        nativeInit();
    }

    protected void onSaveInstanceState (Bundle outState) {
        Log.d ("GStreamer", "Saving state, playing:" + is_playing_desired);
        outState.putBoolean("playing", is_playing_desired);
    }

    protected void onDestroy() {
        nativeFinalize();
        super.onDestroy();
    }

    // Called from native code. This sets the content of the TextView from the UI thread.
    private void setMessage(final String message) {
        final TextView tv = (TextView) this.findViewById(R.id.textview_message);
        runOnUiThread (new Runnable() {
          public void run() {
            tv.setText(message);
          }
        });
    }

    // Called from native code. Native code calls this once it has created its pipeline and
    // the main loop is running, so it is ready to accept commands.
    private void onGStreamerInitialized () {
        Log.i ("GStreamer", "Gst initialized. Restoring state, playing:" + is_playing_desired);
        // Restore previous playing state
        if (is_playing_desired) {
            nativePlay();
        } else {
            nativePause();
        }

        // Re-enable buttons, now that GStreamer is initialized
        final Activity activity = this;
        runOnUiThread(new Runnable() {
            public void run() {
                activity.findViewById(R.id.button_play).setEnabled(true);
                activity.findViewById(R.id.button_stop).setEnabled(true);
            }
        });
    }

    static {
        System.loadLibrary("gstreamer_android");
        System.loadLibrary("tutorial-2");
        nativeClassInit();
    }

}
```

As usual, the first bit that gets executed is the static initializer of
the class:

``` java
static {
    System.loadLibrary("gstreamer_android");
    System.loadLibrary("tutorial-2");
    nativeClassInit();
}
```

As explained in the previous tutorial, the two native libraries are
loaded and their `JNI_OnLoad()` methods are executed. Here, we also call
the native method `nativeClassInit()`, previously declared with the
`native` keyword in line 19. We will later see what its purpose is

In the `onCreate()` method GStreamer is initialized as in the previous
tutorial with `GStreamer.init(this)`, and then the layout is inflated
and listeners are setup for the two UI buttons:

``` java
ImageButton play = (ImageButton) this.findViewById(R.id.button_play);
play.setOnClickListener(new OnClickListener() {
    public void onClick(View v) {
        is_playing_desired = true;
        nativePlay();
    }
});
ImageButton pause = (ImageButton) this.findViewById(R.id.button_stop);
pause.setOnClickListener(new OnClickListener() {
    public void onClick(View v) {
        is_playing_desired = false;
        nativePause();
    }
});
```

Each button instructs the native code to set the pipeline to the desired
state, and also remembers this state in the
`is_playing_desired` variable.  This is required so, when the
application is restarted (for example, due to an orientation change), it
can set the pipeline again to the desired state. This approach is easier
and safer than tracking the actual pipeline state, because orientation
changes can happen before the pipeline has moved to the desired state,
for example.

``` java
if (savedInstanceState != null) {
    is_playing_desired = savedInstanceState.getBoolean("playing");
    Log.i ("GStreamer", "Activity created. Saved state is playing:" + is_playing_desired);
} else {
    is_playing_desired = false;
    Log.i ("GStreamer", "Activity created. There is no saved state, playing: false");
}
```

Restore the previous playing state (if any) from `savedInstanceState`.
We will first build the GStreamer pipeline (below) and only when the
native code reports itself as initialized we will use
`is_playing_desired`.

``` java
nativeInit();
```

As will be shown in the C code, `nativeInit()` creates a dedicated
thread, a GStreamer pipeline, a GLib main loop, and, right before
calling `g_main_loop_run()` and going to sleep, it warns the Java code
that the native code is initialized and ready to accept commands.

This finishes the `onCreate()` method and the Java initialization. The
UI buttons are disabled, so nothing will happen until native code is
ready and `onGStreamerInitialized()` is called:

``` java
private void onGStreamerInitialized () {
    Log.i ("GStreamer", "Gst initialized. Restoring state, playing:" + is_playing_desired);
```

This is called by the native code when its main loop is finally running.
We first retrieve the desired playing state from `is_playing_desired`,
and then set that state:

``` java
// Restore previous playing state
if (is_playing_desired) {
    nativePlay();
} else {
    nativePause();
}
```

Here comes the first caveat, when re-enabling the UI buttons:

``` java
// Re-enable buttons, now that GStreamer is initialized
final Activity activity = this;
runOnUiThread(new Runnable() {
    public void run() {
        activity.findViewById(R.id.button_play).setEnabled(true);
        activity.findViewById(R.id.button_stop).setEnabled(true);
    }
});
```

This method is being called from the thread that the native code created
to run its main loop, and is not allowed to issue UI-altering commands:
Only the UI thread can do that. The solution is easy though: Android
Activities have a handy
[runOnUiThread()](http://developer.android.com/reference/android/app/Activity.html#runOnUiThread\(java.lang.Runnable\))
method which lets bits of code to be executed from the correct thread. A
[Runnable](http://developer.android.com/reference/java/lang/Runnable.html)
instance has to be constructed and any parameter can be passed either by
sub-classing
[Runnable](http://developer.android.com/reference/java/lang/Runnable.html)
and adding a dedicated constructor, or by using the `final` modifier, as
shown in the above snippet.

The same problem exists when the native code wants to output a string in
our TextView using the `setMessage()` method: it has to be done from the
UI thread. The solution is the same:

``` java
private void setMessage(final String message) {
    final TextView tv = (TextView) this.findViewById(R.id.textview_message);
    runOnUiThread (new Runnable() {
      public void run() {
      tv.setText(message);
    }
  });
}
```

Finally, a few remaining bits:

``` java
protected void onSaveInstanceState (Bundle outState) {
    Log.d ("GStreamer", "Saving state, playing:" + is_playing_desired);
    outState.putBoolean("playing", is_playing_desired);
}
```

This method stores the currently desired playing state when Android is
about to shut us down, so next time it restarts (after an orientation
change, for example), it can restore the same state.

``` java
protected void onDestroy() {
    nativeFinalize();
    super.onDestroy();
}
```

And this is called before Android destroys our application. We call the
`nativeFinalize()`method to exit the main loop, destroy its thread and
all allocated resources.

This concludes the UI part of the tutorial.

### A pipeline on Android \[C code\]

**jni/tutorial-2.c**

``` c
#include <string.h>
#include <jni.h>
#include <android/log.h>
#include <gst/gst.h>
#include <pthread.h>

GST_DEBUG_CATEGORY_STATIC (debug_category);
#define GST_CAT_DEFAULT debug_category

/*
 * These macros provide a way to store the native pointer to CustomData, which might be 32 or 64 bits, into
 * a jlong, which is always 64 bits, without warnings.
 */
#if GLIB_SIZEOF_VOID_P == 8
## define GET_CUSTOM_DATA(env, thiz, fieldID) (CustomData *)(*env)->GetLongField (env, thiz, fieldID)
## define SET_CUSTOM_DATA(env, thiz, fieldID, data) (*env)->SetLongField (env, thiz, fieldID, (jlong)data)
#else
## define GET_CUSTOM_DATA(env, thiz, fieldID) (CustomData *)(jint)(*env)->GetLongField (env, thiz, fieldID)
## define SET_CUSTOM_DATA(env, thiz, fieldID, data) (*env)->SetLongField (env, thiz, fieldID, (jlong)(jint)data)
#endif

/* Structure to contain all our information, so we can pass it to callbacks */
typedef struct _CustomData {
  jobject app;           /* Application instance, used to call its methods. A global reference is kept. */
  GstElement *pipeline;  /* The running pipeline */
  GMainContext *context; /* GLib context used to run the main loop */
  GMainLoop *main_loop;  /* GLib main loop */
  gboolean initialized;  /* To avoid informing the UI multiple times about the initialization */
} CustomData;

/* These global variables cache values which are not changing during execution */
static pthread_t gst_app_thread;
static pthread_key_t current_jni_env;
static JavaVM *java_vm;
static jfieldID custom_data_field_id;
static jmethodID set_message_method_id;
static jmethodID on_gstreamer_initialized_method_id;

/*
 * Private methods
 */

/* Register this thread with the VM */
static JNIEnv *attach_current_thread (void) {
  JNIEnv *env;
  JavaVMAttachArgs args;

  GST_DEBUG ("Attaching thread %p", g_thread_self ());
  args.version = JNI_VERSION_1_4;
  args.name = NULL;
  args.group = NULL;

  if ((*java_vm)->AttachCurrentThread (java_vm, &env, &args) < 0) {
    GST_ERROR ("Failed to attach current thread");
    return NULL;
  }

  return env;
}

/* Unregister this thread from the VM */
static void detach_current_thread (void *env) {
  GST_DEBUG ("Detaching thread %p", g_thread_self ());
  (*java_vm)->DetachCurrentThread (java_vm);
}

/* Retrieve the JNI environment for this thread */
static JNIEnv *get_jni_env (void) {
  JNIEnv *env;

  if ((env = pthread_getspecific (current_jni_env)) == NULL) {
    env = attach_current_thread ();
    pthread_setspecific (current_jni_env, env);
  }

  return env;
}

/* Change the content of the UI's TextView */
static void set_ui_message (const gchar *message, CustomData *data) {
  JNIEnv *env = get_jni_env ();
  GST_DEBUG ("Setting message to: %s", message);
  jstring jmessage = (*env)->NewStringUTF(env, message);
  (*env)->CallVoidMethod (env, data->app, set_message_method_id, jmessage);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
  }
  (*env)->DeleteLocalRef (env, jmessage);
}

/* Retrieve errors from the bus and show them on the UI */
static void error_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  GError *err;
  gchar *debug_info;
  gchar *message_string;

  gst_message_parse_error (msg, &err, &debug_info);
  message_string = g_strdup_printf ("Error received from element %s: %s", GST_OBJECT_NAME (msg->src), err->message);
  g_clear_error (&err);
  g_free (debug_info);
  set_ui_message (message_string, data);
  g_free (message_string);
  gst_element_set_state (data->pipeline, GST_STATE_NULL);
}

/* Notify UI about pipeline state changes */
static void state_changed_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  GstState old_state, new_state, pending_state;
  gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
  /* Only pay attention to messages coming from the pipeline, not its children */
  if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data->pipeline)) {
    gchar *message = g_strdup_printf("State changed to %s", gst_element_state_get_name(new_state));
    set_ui_message(message, data);
    g_free (message);
  }
}

/* Check if all conditions are met to report GStreamer as initialized.
 * These conditions will change depending on the application */
static void check_initialization_complete (CustomData *data) {
  JNIEnv *env = get_jni_env ();
  if (!data->initialized && data->main_loop) {
    GST_DEBUG ("Initialization complete, notifying application. main_loop:%p", data->main_loop);
    (*env)->CallVoidMethod (env, data->app, on_gstreamer_initialized_method_id);
    if ((*env)->ExceptionCheck (env)) {
      GST_ERROR ("Failed to call Java method");
      (*env)->ExceptionClear (env);
    }
    data->initialized = TRUE;
  }
}

/* Main method for the native code. This is executed on its own thread. */
static void *app_function (void *userdata) {
  JavaVMAttachArgs args;
  GstBus *bus;
  CustomData *data = (CustomData *)userdata;
  GSource *bus_source;
  GError *error = NULL;

  GST_DEBUG ("Creating pipeline in CustomData at %p", data);

  /* Create our own GLib Main Context and make it the default one */
  data->context = g_main_context_new ();
  g_main_context_push_thread_default(data->context);

  /* Build pipeline */
  data->pipeline = gst_parse_launch("audiotestsrc ! audioconvert ! audioresample ! autoaudiosink", &error);
  if (error) {
    gchar *message = g_strdup_printf("Unable to build pipeline: %s", error->message);
    g_clear_error (&error);
    set_ui_message(message, data);
    g_free (message);
    return NULL;
  }

  /* Instruct the bus to emit signals for each received message, and connect to the interesting signals */
  bus = gst_element_get_bus (data->pipeline);
  bus_source = gst_bus_create_watch (bus);
  g_source_set_callback (bus_source, (GSourceFunc) gst_bus_async_signal_func, NULL, NULL);
  g_source_attach (bus_source, data->context);
  g_source_unref (bus_source);
  g_signal_connect (G_OBJECT (bus), "message::error", (GCallback)error_cb, data);
  g_signal_connect (G_OBJECT (bus), "message::state-changed", (GCallback)state_changed_cb, data);
  gst_object_unref (bus);

  /* Create a GLib Main Loop and set it to run */
  GST_DEBUG ("Entering main loop... (CustomData:%p)", data);
  data->main_loop = g_main_loop_new (data->context, FALSE);
  check_initialization_complete (data);
  g_main_loop_run (data->main_loop);
  GST_DEBUG ("Exited main loop");
  g_main_loop_unref (data->main_loop);
  data->main_loop = NULL;

  /* Free resources */
  g_main_context_pop_thread_default(data->context);
  g_main_context_unref (data->context);
  gst_element_set_state (data->pipeline, GST_STATE_NULL);
  gst_object_unref (data->pipeline);

  return NULL;
}

/*
 * Java Bindings
 */

/* Instruct the native code to create its internal data structure, pipeline and thread */
static void gst_native_init (JNIEnv* env, jobject thiz) {
  CustomData *data = g_new0 (CustomData, 1);
  SET_CUSTOM_DATA (env, thiz, custom_data_field_id, data);
  GST_DEBUG_CATEGORY_INIT (debug_category, "tutorial-2", 0, "Android tutorial 2");
  gst_debug_set_threshold_for_name("tutorial-2", GST_LEVEL_DEBUG);
  GST_DEBUG ("Created CustomData at %p", data);
  data->app = (*env)->NewGlobalRef (env, thiz);
  GST_DEBUG ("Created GlobalRef for app object at %p", data->app);
  pthread_create (&gst_app_thread, NULL, &app_function, data);
}

/* Quit the main loop, remove the native thread and free resources */
static void gst_native_finalize (JNIEnv* env, jobject thiz) {
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data) return;
  GST_DEBUG ("Quitting main loop...");
  g_main_loop_quit (data->main_loop);
  GST_DEBUG ("Waiting for thread to finish...");
  pthread_join (gst_app_thread, NULL);
  GST_DEBUG ("Deleting GlobalRef for app object at %p", data->app);
  (*env)->DeleteGlobalRef (env, data->app);
  GST_DEBUG ("Freeing CustomData at %p", data);
  g_free (data);
  SET_CUSTOM_DATA (env, thiz, custom_data_field_id, NULL);
  GST_DEBUG ("Done finalizing");
}

/* Set pipeline to PLAYING state */
static void gst_native_play (JNIEnv* env, jobject thiz) {
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data) return;
  GST_DEBUG ("Setting state to PLAYING");
  gst_element_set_state (data->pipeline, GST_STATE_PLAYING);
}

/* Set pipeline to PAUSED state */
static void gst_native_pause (JNIEnv* env, jobject thiz) {
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data) return;
  GST_DEBUG ("Setting state to PAUSED");
  gst_element_set_state (data->pipeline, GST_STATE_PAUSED);
}

/* Static class initializer: retrieve method and field IDs */
static jboolean gst_native_class_init (JNIEnv* env, jclass klass) {
  custom_data_field_id = (*env)->GetFieldID (env, klass, "native_custom_data", "J");
  set_message_method_id = (*env)->GetMethodID (env, klass, "setMessage", "(Ljava/lang/String;)V");
  on_gstreamer_initialized_method_id = (*env)->GetMethodID (env, klass, "onGStreamerInitialized", "()V");

  if (!custom_data_field_id || !set_message_method_id || !on_gstreamer_initialized_method_id) {
    /* We emit this message through the Android log instead of the GStreamer log because the later
     * has not been initialized yet.
     */
    __android_log_print (ANDROID_LOG_ERROR, "tutorial-2", "The calling class does not implement all necessary interface methods");
    return JNI_FALSE;
  }
  return JNI_TRUE;
}

/* List of implemented native methods */
static JNINativeMethod native_methods[] = {
  { "nativeInit", "()V", (void *) gst_native_init},
  { "nativeFinalize", "()V", (void *) gst_native_finalize},
  { "nativePlay", "()V", (void *) gst_native_play},
  { "nativePause", "()V", (void *) gst_native_pause},
  { "nativeClassInit", "()Z", (void *) gst_native_class_init}
};

/* Library initializer */
jint JNI_OnLoad(JavaVM *vm, void *reserved) {
  JNIEnv *env = NULL;

  java_vm = vm;

  if ((*vm)->GetEnv(vm, (void**) &env, JNI_VERSION_1_4) != JNI_OK) {
    __android_log_print (ANDROID_LOG_ERROR, "tutorial-2", "Could not retrieve JNIEnv");
    return 0;
  }
  jclass klass = (*env)->FindClass (env, "org/freedesktop/gstreamer/tutorials/tutorial_2/Tutorial2");
  (*env)->RegisterNatives (env, klass, native_methods, G_N_ELEMENTS(native_methods));

  pthread_key_create (&current_jni_env, detach_current_thread);

  return JNI_VERSION_1_4;
}
```

Let’s start with the `CustomData` structure. We have seen it in most of
the basic tutorials, and it is used to hold all our information in one
place, so we can easily pass it around to
callbacks:

``` c
/* Structure to contain all our information, so we can pass it to callbacks */
typedef struct _CustomData {
  jobject app;           /* Application instance, used to call its methods. A global reference is kept. */
  GstElement *pipeline;  /* The running pipeline */
  GMainContext *context; /* GLib context used to run the main loop */
  GMainLoop *main_loop;  /* GLib main loop */
  gboolean initialized;  /* To avoid informing the UI multiple times about the initialization */
} CustomData;
```

We will see the meaning of each member as we go. What is interesting now
is that `CustomData` belongs to the application, so a pointer is kept in
the Tutorial2 Java class in the `private long
native_custom_data` attribute. Java only holds this pointer for us; it
is completely handled in C code.

From C, this pointer can be set and retrieved with the
[SetLongField()](http://docs.oracle.com/javase/1.5.0/docs/guide/jni/spec/functions.html#wp16613)
and
[GetLongField()](http://docs.oracle.com/javase/1.5.0/docs/guide/jni/spec/functions.html#wp16572)
JNI functions, but two convenience macros have been defined,
`SET_CUSTOM_DATA` and `GET_CUSTOM_DATA`. These macros are handy because
the `long` type used in Java is always 64 bits wide, but the pointer
used in C can be either 32 or 64 bits wide. The macros take care of the
conversion without warnings.

``` c
/* Library initializer */
jint JNI_OnLoad(JavaVM *vm, void *reserved) {
  JNIEnv *env = NULL;

  java_vm = vm;

  if ((*vm)->GetEnv(vm, (void**) &env, JNI_VERSION_1_4) != JNI_OK) {
    __android_log_print (ANDROID_LOG_ERROR, "tutorial-2", "Could not retrieve JNIEnv");
    return 0;
  }
  jclass klass = (*env)->FindClass (env, "org/freedesktop/gstreamer/tutorials/tutorial_2/Tutorial2");
  (*env)->RegisterNatives (env, klass, native_methods, G_N_ELEMENTS(native_methods));

  pthread_key_create (&current_jni_env, detach_current_thread);

  return JNI_VERSION_1_4;
}
```

The `JNI_OnLoad` function is almost the same as the previous tutorial.
It registers the list of native methods (which is longer in this
tutorial). It also
uses [pthread\_key\_create()](http://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_key_create.html)
to be able to store per-thread information, which is crucial to properly
manage the JNI Environment, as shown later.

``` c
/* Static class initializer: retrieve method and field IDs */
static jboolean gst_native_class_init (JNIEnv* env, jclass klass) {
  custom_data_field_id = (*env)->GetFieldID (env, klass, "native_custom_data", "J");
  set_message_method_id = (*env)->GetMethodID (env, klass, "setMessage", "(Ljava/lang/String;)V");
  on_gstreamer_initialized_method_id = (*env)->GetMethodID (env, klass, "onGStreamerInitialized", "()V");

  if (!custom_data_field_id || !set_message_method_id || !on_gstreamer_initialized_method_id) {
    /* We emit this message through the Android log instead of the GStreamer log because the later
     * has not been initialized yet.
     */
    __android_log_print (ANDROID_LOG_ERROR, "tutorial-2", "The calling class does not implement all necessary interface methods");
    return JNI_FALSE;
  }
  return JNI_TRUE;
}
```

This method is called from the static initializer of the Java class,
which is passed as a parameter (since this is called from a static
method, it receives a class object instead of an instance object). In
order for C code to be able to call a Java method, it needs to know the
method’s
[MethodID](http://docs.oracle.com/javase/1.5.0/docs/guide/jni/spec/types.html#wp1064).
This ID is obtained from the method’s name and signature and can be
cached. The purpose of the `gst_native_class_init()` function is to
obtain the IDs of all the methods and fields that the C code will need.
If some ID cannot be retrieved, the calling Java class does not offer
the expected interface and execution should halt (which is not currently
done for simplicity).

Let’s review now the first native method which can be directly called
from Java:

#### `gst_native_init()` (`nativeInit()` from Java)

This method is called at the end of Java's `onCreate()`.

``` c
static void gst_native_init (JNIEnv* env, jobject thiz) {
  CustomData *data = g_new0 (CustomData, 1);
  SET_CUSTOM_DATA (env, thiz, custom_data_field_id, data);
```

It first allocates memory for the `CustomData` structure and passes the
pointer to the Java class with `SET_CUSTOM_DATA`, so it is remembered.

``` c
data->app = (*env)->NewGlobalRef (env, thiz);
```

A pointer to the application class (the `Tutorial2` class) is also kept
in `CustomData` (a [Global
Reference](http://developer.android.com/guide/practices/jni.html#local_and_global_references)
is used) so its methods can be called later.

``` c
pthread_create (&gst_app_thread, NULL, &app_function, data);
```

Finally, a thread is created and it starts running the
`app_function()` method.

####  `app_function()`

``` c
/* Main method for the native code. This is executed on its own thread. */
static void *app_function (void *userdata) {
  JavaVMAttachArgs args;
  GstBus *bus;
  CustomData *data = (CustomData *)userdata;
  GSource *bus_source;
  GError *error = NULL;

  GST_DEBUG ("Creating pipeline in CustomData at %p", data);

  /* Create our own GLib Main Context and make it the default one */
  data->context = g_main_context_new ();
  g_main_context_push_thread_default(data->context);
```

It first creates a GLib context so all `GSource` are kept in the same
place. This also helps cleaning after GSources created by other
libraries which might not have been properly disposed of. A new context
is created with `g_main_context_new()` and then it is made the default
one for the thread with
`g_main_context_push_thread_default()`.

``` c
data->pipeline = gst_parse_launch("audiotestsrc ! audioconvert ! audioresample ! autoaudiosink", &error);
if (error) {
  gchar *message = g_strdup_printf("Unable to build pipeline: %s", error->message);
  g_clear_error (&error);
  set_ui_message(message, data);
  g_free (message);
  return NULL;
}
```

It then creates a pipeline the easy way, with `gst-parse-launch()`. In
this case, it is simply an `audiotestsrc` (which produces a continuous
tone) and an `autoaudiosink`, with accompanying adapter elements.

``` c
bus = gst_element_get_bus (data->pipeline);
bus_source = gst_bus_create_watch (bus);
g_source_set_callback (bus_source, (GSourceFunc) gst_bus_async_signal_func, NULL, NULL);
g_source_attach (bus_source, data->context);
g_source_unref (bus_source);
g_signal_connect (G_OBJECT (bus), "message::error", (GCallback)error_cb, data);
g_signal_connect (G_OBJECT (bus), "message::state-changed", (GCallback)state_changed_cb, data);
gst_object_unref (bus);
```

These lines create a bus signal watch and connect to some interesting
signals, just like we have been doing in the basic tutorials. The
creation of the watch is done step by step instead of using
`gst_bus_add_signal_watch()` to exemplify how to use a custom GLib
context.

``` c
GST_DEBUG ("Entering main loop... (CustomData:%p)", data);
data->main_loop = g_main_loop_new (data->context, FALSE);
check_initialization_complete (data);
g_main_loop_run (data->main_loop);
GST_DEBUG ("Exited main loop");
g_main_loop_unref (data->main_loop);
data->main_loop = NULL;
```

Finally, the main loop is created and set to run. When it exits (because
somebody else calls `g_main_loop_quit()`) the main loop is disposed of.
Before entering the main loop, though,
`check_initialization_complete()` is called. This method checks if all
conditions are met to consider the native code “ready” to accept
commands. Since having a running main loop is one of the conditions,
`check_initialization_complete()` is called here. This method is
reviewed below.

Once the main loop has quit, all resources are freed in lines 178 to
181.

#### `check_initialization_complete()`

``` c
static void check_initialization_complete (CustomData *data) {
  JNIEnv *env = get_jni_env ();
  if (!data->initialized && data->main_loop) {
    GST_DEBUG ("Initialization complete, notifying application. main_loop:%p", data->main_loop);
    (*env)->CallVoidMethod (env, data->app, on_gstreamer_initialized_method_id);
    if ((*env)->ExceptionCheck (env)) {
      GST_ERROR ("Failed to call Java method");
      (*env)->ExceptionClear (env);
    }
    data->initialized = TRUE;
  }
}
```

This method does not do much in this tutorial, but it will also be used
in the next ones, with progressively more complex functionality. Its
purpose is to check if the native code is ready to accept commands, and,
if so, notify the UI code.

In tutorial 2, the only conditions are 1) the code is not already
initialized and 2) the main loop is running. If these two are met, the
Java `onGStreamerInitialized()` method is called via the
[CallVoidMethod()](http://docs.oracle.com/javase/1.5.0/docs/guide/jni/spec/functions.html#wp4256)
JNI call.

Here comes a tricky bit. JNI calls require a JNI Environment, **which is
different for every thread**. C methods called from Java receive a
`JNIEnv` pointer as a parameter, but this is not the situation with
`check_initialization_complete()`. Here, we are in a thread which has
never been called from Java, so we have no `JNIEnv`. We need to use the
`JavaVM` pointer (passed to us in the `JNI_OnLoad()` method, and shared
among all threads) to attach this thread to the Java Virtual Machine and
obtain a `JNIEnv`. This `JNIEnv` is stored in the [Thread-Local
Storage](http://en.wikipedia.org/wiki/Thread-local_storage) (TLS) using
the pthread key we created in `JNI_OnLoad()`, so we do not need to
attach the thread anymore.

This behavior is implemented in the `get_jni_env()` method, used for
example in `check_initialization_complete()` as we have just seen. Let’s
see how it works, step by step:

#### `get_jni_env()`

``` c
static JNIEnv *get_jni_env (void) {
  JNIEnv *env;
  if ((env = pthread_getspecific (current_jni_env)) == NULL) {
    env = attach_current_thread ();
    pthread_setspecific (current_jni_env, env);
  }
  return env;
}
```

It first retrieves the current `JNIEnv` from the TLS using
[pthread\_getspecific()](http://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_getspecific.html)
and the key we obtained from
[pthread\_key\_create()](http://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_key_create.html).
If it returns NULL, we never attached this thread, so we do now with
`attach_current_thread()` and then store the new `JNIEnv` into the TLS
with
[pthread\_setspecific()](http://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_setspecific.html).

#### `attach_current_thread()`

This method is simply a convenience wrapper around
[AttachCurrentThread()](http://docs.oracle.com/javase/1.5.0/docs/guide/jni/spec/invocation.html#attach_current_thread)
to deal with its parameters.

#### `detach_current_thread()`

This method is called by the pthreads library when a TLS key is deleted,
meaning that the thread is about to be destroyed. We simply detach the
thread from the JavaVM with
[DetachCurrentThread()](http://docs.oracle.com/javase/1.5.0/docs/guide/jni/spec/invocation.html#detach_current_thread).

Let's now review the rest of the native methods accessible from Java:

#### `gst_native_finalize()` (`nativeFinalize()` from Java)

``` c
static void gst_native_finalize (JNIEnv* env, jobject thiz) {
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data) return;
  GST_DEBUG ("Quitting main loop...");
  g_main_loop_quit (data->main_loop);
  GST_DEBUG ("Waiting for thread to finish...");
  pthread_join (gst_app_thread, NULL);
  GST_DEBUG ("Deleting GlobalRef for app object at %p", data->app);
  (*env)->DeleteGlobalRef (env, data->app);
  GST_DEBUG ("Freeing CustomData at %p", data);
  g_free (data);
  SET_CUSTOM_DATA (env, thiz, custom_data_field_id, NULL);
  GST_DEBUG ("Done finalizing");
}
```

This method is called from Java in `onDestroy()`, when the activity is
about to be destroyed. Here, we:

  - Instruct the GLib main loop to quit with `g_main_loop_quit()`. This
    call returns immediately, and the main loop will terminate at its
    earliest convenience.
  - Wait for the thread to finish with
    [pthread\_join()](http://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_join.html).
    This call blocks until the `app_function()` method returns, meaning
    that the main loop has exited, and the thread has been destroyed.
  - Dispose of the global reference we kept for the Java application
    class (`Tutorial2`) in `CustomData`.
  - Free `CustomData` and set the Java pointer inside the
    `Tutorial2` class to NULL with
`SET_CUSTOM_DATA()`.

#### `gst_native_play` and `gst_native_pause()` (`nativePlay` and `nativePause()` from Java)

These two simple methods retrieve `CustomData` from the passed-in object
with `GET_CUSTOM_DATA()` and set the pipeline found inside `CustomData`
to the desired state, returning immediately.

Finally, let’s see how the GStreamer callbacks are handled:

#### `error_cb` and `state_changed_cb`

This tutorial does not do much in these callbacks. They simply parse the
error or state changed message and display a message in the UI using the
`set_ui_message()` method:

#### `set_ui_message()`

``` c
static void set_ui_message (const gchar *message, CustomData *data) {
  JNIEnv *env = get_jni_env ();
  GST_DEBUG ("Setting message to: %s", message);
  jstring jmessage = (*env)->NewStringUTF(env, message);
  (*env)->CallVoidMethod (env, data->app, set_message_method_id, jmessage);
  if ((*env)->ExceptionCheck (env)) {
    GST_ERROR ("Failed to call Java method");
    (*env)->ExceptionClear (env);
  }
  (*env)->DeleteLocalRef (env, jmessage);
}
```



This is the other method (besides `check_initialization_complete()`)
that needs to call a Java function from a thread which never received an
`JNIEnv` pointer directly. Notice how all the complexities of attaching
the thread to the JavaVM and storing the JNI environment in the TLS are
hidden in the simple call to `get_jni_env()`.

The desired message (received in
[ASCII](http://en.wikipedia.org/wiki/ASCII), or modified
[UTF8](http://en.wikipedia.org/wiki/Modified_UTF-8#Modified_UTF-8)), is
converted to [UTF16](http://en.wikipedia.org/wiki/UTF-16) as required by
Java using the
[NewStringUTF()](http://docs.oracle.com/javase/1.5.0/docs/guide/jni/spec/functions.html#wp17220)
JNI call.

The `setMessage()` Java method is called via the JNI
[CallVoidMethod()](http://docs.oracle.com/javase/1.5.0/docs/guide/jni/spec/functions.html#wp4256)
using the global reference to the class we are keeping in
`CustomData` (`data->app`) and the `set_message_method_id` we cached in
`gst_native_class_init()`.

We check for exceptions with the JNI
[ExceptionCheck()](http://docs.oracle.com/javase/1.5.0/docs/guide/jni/spec/functions.html#exception_check)
method and free the UTF16 message with
[DeleteLocalRef()](http://docs.oracle.com/javase/1.5.0/docs/guide/jni/spec/functions.html#DeleteLocalRef).

### A pipeline on Android \[Android.mk\]

**jni/Android.mk**

``` ruby
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := tutorial-2
LOCAL_SRC_FILES := tutorial-2.c
LOCAL_SHARED_LIBRARIES := gstreamer_android
LOCAL_LDLIBS := -llog
include $(BUILD_SHARED_LIBRARY)

ifndef GSTREAMER_ROOT
ifndef GSTREAMER_ROOT_ANDROID
$(error GSTREAMER_ROOT_ANDROID is not defined!)
endif
GSTREAMER_ROOT        := $(GSTREAMER_ROOT_ANDROID)
endif
GSTREAMER_NDK_BUILD_PATH  := $(GSTREAMER_ROOT)/share/gst-android/ndk-build/
include $(GSTREAMER_NDK_BUILD_PATH)/plugins.mk
GSTREAMER_PLUGINS         := $(GSTREAMER_PLUGINS_CORE) $(GSTREAMER_PLUGINS_SYS)
include $(GSTREAMER_NDK_BUILD_PATH)/gstreamer-1.0.mk
```

Notice how the required `GSTREAMER_PLUGINS` are now
`$(GSTREAMER_PLUGINS_CORE)` (For the test source and converter elements)
and `$(GSTREAMER_PLUGINS_SYS)` (for the audio sink).

And this is it\! This has been a rather long tutorial, but we covered a
lot of territory. Building on top of this one, the following ones are
shorter and focus only on the new topics.

### Conclusion

This tutorial has shown:

  - How to manage multiple threads from C code and have them interact
    with java.
  - How to access Java code from any C thread
    using [AttachCurrentThread()](http://docs.oracle.com/javase/1.5.0/docs/guide/jni/spec/invocation.html#attach_current_thread).
  - How to allocate a CustomData structure from C and have Java host it,
    so it is available to all threads.

Most of the methods introduced in this tutorial, like `get_jni_env()`,
`check_initialization_complete()`, `app_function()` and the API methods
`gst_native_init()`, `gst_native_finalize()` and
`gst_native_class_init()` will continue to be used in the following
tutorials with minimal modifications, so better get used to them\!

As usual, it has been a pleasure having you here, and see you soon\!

  [screenshot]: images/tutorials/android-a-running-pipeline-screenshot.png