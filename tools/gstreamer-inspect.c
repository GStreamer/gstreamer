#include <gst/gst.h>

int main(int argc,char *argv[]) {
  GstElementFactory *factory;
  GstElement *element;
  GstElementClass *gstelement_class;
  GList *pads;
  GstPad *pad;

  gst_init(&argc,&argv);

  factory = gst_elementfactory_find(argv[1]);
  element = gst_elementfactory_create(factory,argv[1]);
  gstelement_class = GST_ELEMENT_CLASS (GTK_OBJECT (element)->klass);

  printf("Element Details:\n");
  printf("  Long name:\t%s\n",factory->details->longname);
  printf("  Class:\t%s\n",factory->details->klass);
  printf("  Description:\t%s\n",factory->details->description);
  printf("  Version:\t%s\n",factory->details->version);
  printf("  Author(s):\t%s\n",factory->details->author);
  printf("  Copyright:\t%s\n",factory->details->copyright);
  printf("\n");

  printf("Element Flags:\n");
  if (GST_FLAG_IS_SET(element,GST_ELEMENT_COMPLEX))
    printf("  GST_ELEMENT_COMPLEX\n");
  if (GST_FLAG_IS_SET(element,GST_ELEMENT_DECOUPLED))
    printf("  GST_ELEMENT_DECOUPLED\n");
  if (GST_FLAG_IS_SET(element,GST_ELEMENT_THREAD_SUGGESTED))
    printf("  GST_ELEMENT_THREADSUGGESTED\n");
  if (GST_FLAG_IS_SET(element,GST_ELEMENT_NO_SEEK))
    printf("  GST_ELEMENT_NO_SEEK\n");
  if (! GST_FLAG_IS_SET(element, GST_ELEMENT_COMPLEX | GST_ELEMENT_DECOUPLED |
                                 GST_ELEMENT_THREAD_SUGGESTED | GST_ELEMENT_NO_SEEK))
    printf("  no flags set\n");
  printf("\n");


  printf("Element Implementation:\n");
  if (element->loopfunc)
    printf("  loopfunc()-based element\n");
  else
    printf("  No loopfunc(), must be chain-based or not configured yet\n");
  if (gstelement_class->change_state)
    printf("  Has change_state() function\n");
  else
    printf("  No change_state() class function\n");
  if (gstelement_class->save_thyself)
    printf("  Has custom save_thyself() class function\n");
  if (gstelement_class->restore_thyself)
    printf("  Has custom restore_thyself() class function\n");
  printf("\n");


  printf("Pads:\n");
  pads = gst_element_get_pad_list(element);
  while (pads) {
    pad = GST_PAD(pads->data);
    pads = g_list_next(pads);
    if (gst_pad_get_direction(pad) == GST_PAD_SRC)
      printf("  SRC: %s\n",gst_pad_get_name(pad));
    else if (gst_pad_get_direction(pad) == GST_PAD_SINK)
      printf("  SINK: %s\n",gst_pad_get_name(pad));
    else
      printf("  UNKNOWN!!!: %s\n",gst_pad_get_name(pad));

    printf("    Implementation:\n");
    if (pad->chainfunc)
      printf("      Has chainfunc(): %s\n",GST_DEBUG_FUNCPTR_NAME(pad->chainfunc));
    if (pad->getfunc)
      printf("      Has getfunc(): %s\n",GST_DEBUG_FUNCPTR_NAME(pad->getfunc));
    if (pad->getregionfunc)
      printf("      Has getregionfunc(): %s\n",GST_DEBUG_FUNCPTR_NAME(pad->getregionfunc));
    if (pad->qosfunc)
      printf("      Has qosfunc(): %s\n",GST_DEBUG_FUNCPTR_NAME(pad->qosfunc));
    if (pad->eosfunc) {
      if (pad->eosfunc == gst_pad_eos_func)
        printf("      Has default eosfunc() gst_pad_eos_func()\n");
      else
        printf("      Has eosfunc(): %s\n",GST_DEBUG_FUNCPTR_NAME(pad->eosfunc));
    }
    printf("\n");
  }
  printf("\n");

  return 0;
}
