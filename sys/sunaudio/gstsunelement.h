#ifndef __GST_SUNAUDIO_ELEMENT_H__
#define __GST_SUNAUDIO_ELEMENT_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_SUNAUDIOELEMENT \
  (gst_sunaudioelement_get_type())
#define GST_SUNAUDIOELEMENT(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SUNAUDIOELEMENT,GstSunAudioElement))
#define GST_SUNAUDIOELEMENT_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SUNAUDIOELEMENT,GstSunAudioElementClass))
#define GST_IS_SUNAUDIOELEMENT(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SUNAUDIOELEMENT))
#define GST_IS_SUNAUDIOELEMENT_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SUNAUDIOELEMENT))
#define GST_SUNAUDIOELEMENT_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_SUNAUDIOELEMENT, GstSunAudioElementClass))

typedef enum {
  GST_SUNAUDIOELEMENT_READ,
  GST_SUNAUDIOELEMENT_WRITE,
} GstSunAudioOpenMode;


struct _GstSunAudioElement
{
  /* yes, we're a gstelement too */
  GstElement     parent;
                                                                                
  gchar         *device,
                *mixer_dev;
        
  /* device state */
  int            fd;
  GstSunAudioOpenMode mode;
 
  /* mixer stuff */
  GList         *tracklist;
  gint           mixer_fd;
  gchar         *device_name;
};

struct _GstSunAudioElementClass {
  GstElementClass klass;
 
  GList         *device_combinations;
};

typedef struct _GstSunAudioDeviceCombination {
  gchar *mixer;
  dev_t dev;
} GstSunAudioDeviceCombination;

typedef struct _GstSunAudioElement GstSunAudioElement;
typedef struct _GstSunAudioElementClass GstSunAudioElementClass;

GType           gst_sunaudioelement_get_type         (void);

G_END_DECLS

#endif
