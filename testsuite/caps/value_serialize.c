#include <gst/gst.h>

static void
test1 (void)
{
  GValue value = { 0 };
  gboolean ret;

  g_value_init (&value, GST_TYPE_BUFFER);
  ret = gst_value_deserialize (&value, "1234567890abcdef");
  g_assert (ret);
}

static gboolean
test_string_serialization (void)
{
  gchar *try[] = {
    "Dude",
    "Hi, I'm a string",
    "tüüüt!"
  };
  gchar *tmp;
  GValue v = { 0, };
  guint i;
  gboolean ret = TRUE;

  g_value_init (&v, G_TYPE_STRING);
  for (i = 0; i < G_N_ELEMENTS (try); i++) {
    g_value_set_string (&v, try[i]);
    tmp = gst_value_serialize (&v);
    if (!tmp) {
      g_print ("couldn't serialize: %s\n", try[i]);
      ret = FALSE;
      continue;
    }
    if (!gst_value_deserialize (&v, tmp)) {
      g_print ("couldn't deserialize: %s\n", tmp);
      g_free (tmp);
      ret = FALSE;
      continue;
    }
    g_free (tmp);
    if (!g_str_equal (g_value_get_string (&v), try[i])) {
      g_print ("serialized  : %s\n", try[i]);
      g_print ("deserialized: %s\n", g_value_get_string (&v));
      ret = FALSE;
      continue;
    }
  }
  g_value_unset (&v);
  return ret;

}

static gboolean
test_string_deserialization (void)
{
  struct
  {
    gchar *from;
    gchar *to;
  } tests[] = {
    {
    "", ""}, {
    "\\", "\\"}, {
    "\"\"", ""},
        /* FAILURES */
    {
    "\"", NULL},                /* missing second quote */
    {
    "\"Hello\\ World", NULL},   /* missing second quote */
    {
    "\"\\", NULL},              /* quote at end, missing second quote */
    {
    "\"\\0", NULL},             /* missing second quote */
    {
    "\"\\0\"", NULL},           /* unfinished escaped character */
    {
    "\" \"", NULL},             /* spaces must be escaped */
    {
    "tüüt", NULL}             /* string with special chars must be escaped */
  };
  guint i;
  GValue v = { 0, };
  gboolean ret = TRUE;

  g_value_init (&v, G_TYPE_STRING);
  for (i = 0; i < G_N_ELEMENTS (tests); i++) {
    if (gst_value_deserialize (&v, tests[i].from)) {
      if (tests[i].to == NULL) {
        g_print ("should fail\n");
        g_print ("but got: %s\n", g_value_get_string (&v));
        ret = FALSE;
      } else if (!g_str_equal (g_value_get_string (&v), tests[i].to)) {
        g_print ("wanted: %s\n", tests[i].to);
        g_print ("got   : %s\n", g_value_get_string (&v));
        ret = FALSE;
      }
    } else {
      if (tests[i].to != NULL) {
        g_print ("failed\n");
        g_print ("but wanted: %s\n", tests[i].to);
        ret = FALSE;
      }
    }
  }
  g_value_unset (&v);
  return ret;
}

int
main (int argc, char *argv[])
{
  gboolean ret = TRUE;

  gst_init (&argc, &argv);

  test1 ();
  ret &= test_string_serialization ();
  ret &= test_string_deserialization ();

  return ret ? 0 : 1;
}
