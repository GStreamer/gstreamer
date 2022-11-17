#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "validate.h"
#include "gst-validate-utils.h"
#include "gst-validate-internal.h"



#define EXTRA_CHECKS_WRONG_NUMBER_OF_INSTANCES g_quark_from_static_string ("extrachecks::wrong-number-of-instances")

typedef struct
{
  gchar *pname;
  gchar *klass;
  gint expected_n_instances;
  gint n_instances;
} CheckNumInstanceData;

static CheckNumInstanceData *
gst_validate_check_num_instances_data_new (GstStructure * check)
{
  CheckNumInstanceData *data = g_new0 (CheckNumInstanceData, 1);

  if (!gst_structure_get_int (check, "num-instances",
          &data->expected_n_instances)) {
    gst_validate_abort
        ("[CONFIG ERROR] Mandatory field `num-instances` not found in "
        "extra-check `num-instances`");
    goto failed;
  }

  data->pname = g_strdup (gst_structure_get_string (check, "pipeline-name"));
  if (!data->pname) {
    gst_validate_abort
        ("[CONFIG ERROR] Mandatory field `pipeline` not found in "
        "extra-check `num-instances`");
    goto failed;
  }

  data->klass = g_strdup (gst_structure_get_string (check, "element-klass"));
  if (!data->klass) {
    gst_validate_abort
        ("[CONFIG ERROR] Mandatory field `element-klass` not found in "
        "extra-check `num-instances`");
    goto failed;
  }

  return data;

failed:
  g_free (data);
  g_free (data->klass);

  return NULL;
}

static void
gst_validate_check_num_instances_data_free (CheckNumInstanceData * data)
{
  g_free (data->pname);
  g_free (data);
}

static void
gst_validate_check_num_instances (GstValidateOverride * o,
    GstValidateMonitor * monitor, GstElement * nelem)
{
  gchar *pname;
  CheckNumInstanceData *data = g_object_get_data (G_OBJECT (o), "check-data");
  GstPipeline *pipe = gst_validate_monitor_get_pipeline (monitor);

  if (!pipe)
    return;

  pname = gst_object_get_name (GST_OBJECT (pipe));
  if (g_strcmp0 (data->pname, pname))
    goto done;

  if (!gst_validate_element_has_klass (nelem, data->klass))
    return;

  data->n_instances++;

  if (data->n_instances > data->expected_n_instances) {
    GST_VALIDATE_REPORT (o, EXTRA_CHECKS_WRONG_NUMBER_OF_INSTANCES,
        "%d instances allows in pipeline %s but already %d where added.",
        data->expected_n_instances, pname, data->n_instances);
  }
  GST_ERROR_OBJECT (nelem, "HERE I AM %d", data->n_instances);

done:
  g_free (pname);
  gst_object_unref (pipe);
}

static void
runner_stopping (GstValidateRunner * runner, GstValidateOverride * o)
{
  CheckNumInstanceData *data = g_object_get_data (G_OBJECT (o), "check-data");

  if (data->expected_n_instances != data->n_instances) {
    GST_VALIDATE_REPORT (o, EXTRA_CHECKS_WRONG_NUMBER_OF_INSTANCES,
        "%d instances expected in pipeline %s but %d where added.",
        data->expected_n_instances, data->pname, data->n_instances);
  }
}

static void
_runner_set (GObject * object, GParamSpec * pspec, gpointer user_data)
{
  GstValidateRunner *runner =
      gst_validate_reporter_get_runner (GST_VALIDATE_REPORTER (object));

  g_signal_connect (runner, "stopping", G_CALLBACK (runner_stopping), object);
  gst_object_unref (runner);
}

static void
gst_validate_add_num_instances_check (GstStructure * structure)
{
  CheckNumInstanceData *data =
      gst_validate_check_num_instances_data_new (structure);
  GstValidateOverride *o = gst_validate_override_new ();

  g_object_set_data_full (G_OBJECT (o), "check-data", data,
      (GDestroyNotify) gst_validate_check_num_instances_data_free);

  gst_validate_override_set_element_added_handler (o,
      gst_validate_check_num_instances);

  g_signal_connect (o, "notify::validate-runner", G_CALLBACK (_runner_set),
      NULL);

  gst_validate_override_register_by_type (GST_TYPE_BIN, o);
  gst_object_unref (o);
}

gboolean
gst_validate_extra_checks_init ()
{
  GList *config, *tmp;
  config = gst_validate_get_config ("extrachecks");

  if (!config)
    return TRUE;

  for (tmp = config; tmp; tmp = tmp->next) {
    GstStructure *check = tmp->data;

    if (gst_structure_has_field (check, "num-instances"))
      gst_validate_add_num_instances_check (check);
  }
  g_list_free (config);

  gst_validate_issue_register (gst_validate_issue_new
      (EXTRA_CHECKS_WRONG_NUMBER_OF_INSTANCES,
          "The configured number of possible instances of an element type"
          " in a pipeline is not respected.",
          "The `num-instances` extra checks allow user to make sure that"
          " a previously defined number of instances of an element is added"
          " in a given pipeline, that test failed.",
          GST_VALIDATE_REPORT_LEVEL_CRITICAL));

  return TRUE;
}
