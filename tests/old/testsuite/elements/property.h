/* extracted from gst-launch */
static void
property_change_callback (GObject *object, GstObject *orig, GParamSpec *pspec)
{
  GValue value = { 0, }; /* the important thing is that value.type = 0 */
  gchar *str = 0;

  if (pspec->flags & G_PARAM_READABLE) {
    g_value_init(&value, G_PARAM_SPEC_VALUE_TYPE (pspec));
    g_object_get_property (G_OBJECT (orig), pspec->name, &value);
    /* fix current bug with g_strdup_value_contents not working with gint64 */
    if (G_IS_PARAM_SPEC_INT64 (pspec))
      str = g_strdup_printf ("%lld", g_value_get_int64 (&value));
    else
      str = g_strdup_value_contents (&value);
    g_print ("%s: %s = %s\n", GST_OBJECT_NAME (orig), pspec->name, str);
    g_free (str);
    g_value_unset(&value);
  } else {
    g_warning ("Parameter not readable. What's up with that?");
  }
}

