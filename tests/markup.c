#include <stdarg.h>
#include <gst/gst.h>
#include <assert.h>
#include <string.h>

gchar *
gst_markup_vsprintf (const gchar *format,...)
{
  va_list args;
  gint i,j;
  gboolean have_hash = FALSE;
  gboolean longarg = FALSE;
  GstObject *object;
  gchar *newformat = g_new0(gchar,strlen(format));
  gchar *newstring;
  GSList *tofree = NULL;
  gint dummy;

  va_start(args, format);
  for (i=0,j=0;i<strlen(format);i++,j++) {
    newformat[j] = format[i];
    if (have_hash) {
      switch (format[i]) {
        case 'l': longarg = TRUE;break;
        case 'O': {
          if (longarg) j--;
          newformat[j] = 's';
          object = va_arg(args,GstObject *);
          if (longarg) {
            fprintf(stderr,"have %%lO\n");
            newstring = gst_object_get_path_string(object);
            tofree = g_slist_prepend (tofree, newstring);
            longarg = FALSE;
          } else {
            fprintf(stderr,"have %%O\n");
            newstring = "something";
          }
          break;
        }
        case '%': break;
        default: dummy = va_arg(args,gint);
      }
    }
    if (format[i] == '%')
      have_hash = TRUE;
  }
  newformat[j] = '\0';

  fprintf(stderr,"new format is '%s'\n",newformat);

  return g_strdup_vprintf(newformat,args);
}

int main(int argc,char *argv[]) {
  GstElement *pipeline;
  GstElement *src;

  GST_DEBUG_ENTER("(%d)",argc);

  gst_init(&argc,&argv);

  pipeline = gst_pipeline_new("fakepipeline");
  src = gst_elementfactory_make("fakesrc","src");
  gst_bin_add(GST_BIN(pipeline),src);

  fprintf(stderr,gst_markup_vsprintf("testing %d, %lO\n",2,"src"));

  return 0;
}
