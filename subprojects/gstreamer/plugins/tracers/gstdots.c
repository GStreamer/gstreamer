/* gstdotstracer.c */
/**
 * SECTION:tracer-dots
 * @short_description: Tracer for dot file generation setup and pipeline
 * snapshot integration
 * @title: GstDotsTracer
 *
 * The Dots tracer handles dot file generation setup and integrates with the
 * pipeline-snapshot tracer when available. It ensures proper directory setup
 * to collaborate with the `gst-dots-viewer` tool, and it handles file cleanup.
 *
 * The tracer determines the output directory in the following order:
 * 1. Uses GST_DEBUG_DUMP_DOT_DIR if set
 * 2. Falls back to $XDG_CACHE_HOME/gstreamer-dots otherwise
 *
 * The determined directory is created if it doesn't exist and set as
 * `GST_DEBUG_DUMP_DOT_DIR` for the entire process.
 *
 * When available, it instantiates the pipeline-snapshot tracer with the
 * following configuration:
 * - dots-viewer-ws-url=ws://127.0.0.1:3000/snapshot/
 * - xdg-cache=true
 * - folder-mode=numbered
 *
 * ## Examples:
 *
 * ```
 * # Basic usage - will delete existing .dot files
 * GST_TRACERS=dots gst-launch-1.0 videotestsrc ! autovideosink
 *
 * # Keep existing .dot files
 * GST_TRACERS="dots(no-delete=true)" gst-launch-1.0 videotestsrc ! autovideosink
 * ```
 *
 * Since: 1.26
 */

#include "gst/gsttracerfactory.h"
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <gst/gst.h>
#include <gst/gsttracer.h>

#define GST_TYPE_DOTS_TRACER (gst_dots_tracer_get_type())
G_DECLARE_FINAL_TYPE (GstDotsTracer, gst_dots_tracer, GST, DOTS_TRACER,
    GstTracer)
/**
 * GstDotsTracer:
 *
 * The #GstDotsTracer structure.
 *
 * Since: 1.26
 */
/* *INDENT-OFF* */
struct _GstDotsTracer
{
  GstTracer parent;

  gboolean no_delete;
  gchar *output_dir;
  GstTracer *pipeline_snapshot_tracer;
};

G_DEFINE_TYPE (GstDotsTracer, gst_dots_tracer, GST_TYPE_TRACER);

enum {
  PROP_0,
  PROP_NO_DELETE,
  N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = {
  NULL,
};

GST_DEBUG_CATEGORY_STATIC (dots_debug);
#define GST_CAT_DEFAULT dots_debug

static void
gst_dots_tracer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
/* *INDENT-ON* */
{
  GstDotsTracer *self = GST_DOTS_TRACER (object);

  switch (prop_id) {
    case PROP_NO_DELETE:
      self->no_delete = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dots_tracer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDotsTracer *self = GST_DOTS_TRACER (object);

  switch (prop_id) {
    case PROP_NO_DELETE:
      g_value_set_boolean (value, self->no_delete);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dots_tracer_finalize (GObject * obj)
{
  GstDotsTracer *self = GST_DOTS_TRACER (obj);

  g_free (self->output_dir);

  if (self->pipeline_snapshot_tracer) {
    gst_object_unref (self->pipeline_snapshot_tracer);
  }

  G_OBJECT_CLASS (gst_dots_tracer_parent_class)->finalize (obj);
}

static void
clean_dot_files (const gchar * dir_path)
{
  GDir *dir;
  const gchar *filename;
  GError *error = NULL;
  GSList *paths = NULL, *l;
  GSList *dirs = NULL;

  /* Build directory list starting with root dir */
  dirs = g_slist_prepend (dirs, g_strdup (dir_path));

  /* Find all matching files */
  while (dirs) {
    gchar *current_dir = dirs->data;
    dirs = g_slist_delete_link (dirs, dirs);

    dir = g_dir_open (current_dir, 0, &error);
    if (!dir) {
      GST_WARNING ("Could not open directory %s: %s", current_dir,
          error ? error->message : "unknown error");
      g_clear_error (&error);
      g_free (current_dir);
      continue;
    }

    while ((filename = g_dir_read_name (dir))) {
      gchar *path = g_build_filename (current_dir, filename, NULL);

      if (g_file_test (path, G_FILE_TEST_IS_DIR)) {
        dirs = g_slist_prepend (dirs, path);
      } else if (g_str_has_suffix (path, ".dot")) {
        paths = g_slist_prepend (paths, path);
      } else {
        g_free (path);
      }
    }
    g_dir_close (dir);
    g_free (current_dir);
  }

  /* Delete all matched files */
  for (l = paths; l; l = l->next) {
    if (g_unlink (l->data) != 0) {
      GST_WARNING ("Could not delete file %s", (gchar *) l->data);
    }
  }

  g_slist_free_full (paths, g_free);
}

static gboolean
try_create_pipeline_snapshot_tracer (GstDotsTracer * self)
{
  GstRegistry *registry;
  GstPluginFeature *feature;
  GstTracerFactory *factory;

  registry = gst_registry_get ();
  feature = gst_registry_lookup_feature (registry, "pipeline-snapshot");

  if (!feature) {
    GST_WARNING ("pipeline-snapshot tracer not found. \
Please ensure that the `rstracers` plugin is installed.");
    return FALSE;
  }

  factory = GST_TRACER_FACTORY (gst_plugin_feature_load (feature));
  gst_object_unref (feature);

  if (!factory) {
    GST_WARNING ("Could not load pipeline-snapshot factory. \
Please ensure GStreamer is properly installed.");
    return FALSE;
  }

  GType tracer_type = gst_tracer_factory_get_tracer_type (factory);
  GObjectClass *tracer_class = g_type_class_ref (tracer_type);

  if (g_object_class_find_property (tracer_class, "dots-viewer-ws-url"))
    self->pipeline_snapshot_tracer = g_object_new (gst_tracer_factory_get_tracer_type (factory), "dot-dir", self->output_dir, "dots-viewer-ws-url", "ws://127.0.0.1:3000/snapshot/", "folder-mode", 1,  /*numbered */
        NULL);
  else
    self->pipeline_snapshot_tracer =
        g_object_new (gst_tracer_factory_get_tracer_type (factory), NULL);
  gst_object_unref (factory);
  g_type_class_unref (tracer_class);

  if (!self->pipeline_snapshot_tracer) {
    GST_WARNING ("Could not create pipeline-snapshot tracer instance");
    return FALSE;
  }

  GST_INFO ("Successfully created and configured pipeline-snapshot tracer");
  return TRUE;
}

static void
setup_output_directory (GstDotsTracer * self)
{
  const gchar *env_dir;

  // Check GST_DEBUG_DUMP_DOT_DIR first
  env_dir = g_getenv ("GST_DEBUG_DUMP_DOT_DIR");
  if (env_dir) {
    self->output_dir = g_strdup (env_dir);
  } else {
    // Use XDG cache directory if GST_DEBUG_DUMP_DOT_DIR is not set
    self->output_dir =
        g_build_filename (g_get_user_cache_dir (), "gstreamer-dots", NULL);

    GST_DEBUG ("Setting GST_DEBUG_DUMP_DOT_DIR to %s", self->output_dir);

    g_setenv ("GST_DEBUG_DUMP_DOT_DIR", self->output_dir, TRUE);
  }

  // Create output directory if it doesn't exist
  g_mkdir_with_parents (self->output_dir, 0755);

  // Clean existing .dot files unless no-delete is set
  if (!self->no_delete) {
    clean_dot_files (self->output_dir);
  }
}

static void
gst_dots_tracer_init (GstDotsTracer * self)
{
  self->no_delete = FALSE;
  self->pipeline_snapshot_tracer = NULL;

  setup_output_directory (self);

  // Try to create pipeline-snapshot tracer with exact same configuration as
  // gstdump.rs
  try_create_pipeline_snapshot_tracer (self);
}

static void
gst_dots_tracer_class_init (GstDotsTracerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gst_dots_tracer_set_property;
  gobject_class->get_property = gst_dots_tracer_get_property;
  gobject_class->finalize = gst_dots_tracer_finalize;

  gst_tracer_class_set_use_structure_params (GST_TRACER_CLASS (gobject_class),
      TRUE);

  /**
   * GstDotsTracer:no-delete:
   *
   * Don't delete existing .dot files on startup.
   *
   * Since: 1.26
   */
  properties[PROP_NO_DELETE] =
      g_param_spec_boolean ("no-delete", "No Delete",
      "Don't delete existing .dot files on startup", FALSE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, N_PROPERTIES, properties);

  GST_DEBUG_CATEGORY_INIT (dots_debug, "dots", 0, "dots tracer");
}
