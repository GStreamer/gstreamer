#include <gst/gst.h>

#ifdef __cplusplus
extern "C"
{
#endif				/* __cplusplus */

  void *gst_arts_wrapper_new (GstPad * sinkpad, GstPad * sourcepad);
  void gst_arts_wrapper_free (void *wrapper);
  void gst_arts_wrapper_do (void *wrapper);

#ifdef __cplusplus
}
#endif				/* __cplusplus */
