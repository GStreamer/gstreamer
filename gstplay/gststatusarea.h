#ifndef __GST_STATUS_AREA_H__
#define __GST_STATUS_AREA_H__

#include <gst/gst.h>
#include <gtk/gtk.h>

typedef struct _GstStatusArea GstStatusArea;
typedef struct _GstStatusAreaClass GstStatusAreaClass;

#define GST_TYPE_STATUS_AREA \
  (gst_status_area_get_type())
#define GST_STATUS_AREA(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_STATUS_AREA,GstStatusArea))
#define GST_STATUS_AREA_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_STATUS_AREA,GstStatusAreaClass))
#define GST_IS_STATUS_AREA(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_STATUS_AREA))
#define GST_IS_STATUS_AREA_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_STATUS_AREA))

typedef enum {
  GST_STATUS_AREA_STATE_INIT,
  GST_STATUS_AREA_STATE_PLAYING,
  GST_STATUS_AREA_STATE_PAUSED,
  GST_STATUS_AREA_STATE_STOPPED,
} GstStatusAreaState;

struct _GstStatusArea {
  GtkWidget parent;

  GstStatusAreaState state;
  guchar *playtime;
};

struct _GstStatusAreaClass {
  GtkWidgetClass parent_class;

};


GtkType 	gst_status_area_get_type	(void);

GstStatusArea*	gst_status_area_new		(void);

void 		gst_status_area_set_state	(GstStatusArea *area, GstStatusAreaState state);
void	 	gst_status_area_set_playtime	(GstStatusArea *area, const guchar *time);
void 		gst_status_area_set_streamtype	(GstStatusArea *area, const guchar *type);

void 		gst_status_area_show_extended	(GstStatusArea *area, gboolean show);

#endif /* __GST_STATUS_AREA_H__ */
