/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */


#ifndef __GST_EDITOR_H__
#define __GST_EDITOR_H__

#include <gnome.h>
#include <gst/gst.h>


typedef struct _GstEditor GstEditor;
typedef struct _GstEditorClass GstEditorClass;
typedef struct _GstEditorElement GstEditorElement;
typedef struct _GstEditorElementClass GstEditorElementClass;
typedef struct _GstEditorBin GstEditorBin;
typedef struct _GstEditorBinClass GstEditorBinClass;
typedef struct _GstEditorCanvas GstEditorCanvas;
typedef struct _GstEditorCanvasClass GstEditorCanvasClass;
typedef struct _GstEditorPad GstEditorPad;
typedef struct _GstEditorPadClass GstEditorPadClass;
typedef struct _GstEditorConnection GstEditorConnection;
typedef struct _GstEditorConnectionClass GstEditorConnectionClass;



#define GST_TYPE_EDITOR \
  (gst_editor_get_type())
#define GST_EDITOR(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_EDITOR,GstEditor))
#define GST_EDITOR_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_EDITOR,GstEditorClass))
#define GST_IS_EDITOR(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_EDITOR))
#define GST_IS_EDITOR_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_EDITOR))


struct _GstEditor {
  GtkWindow window;

  /* the actual element to be associated with this thing */
  GstElement *element;

  /* the editor canvas */
  GstEditorCanvas *canvas;

  /* the canvas and scrollwindow */
  GtkWidget *canvaswidget;
  GtkWidget *scrollwindow;
};

struct _GstEditorClass {
  GtkWindowClass parent_class;

  void (*name_changed) (GstEditor *editor);
};


GtkType gst_editor_get_type();
GstEditor *gst_editor_new(GstElement *element);

char *gst_editor_get_name(GstEditor *editor);


#define GST_EDITOR_SET_OBJECT(item,object) \
  (gtk_object_set_data(GTK_OBJECT(item),"gsteditorobject",(object)))
#define GST_EDTIOR_GET_OBJECT(item) \
  (gtk_object_get_data(GTK_OBJECT(item),"gsteditorobject"))



#define GST_TYPE_EDITOR_ELEMENT \
  (gst_editor_element_get_type())
#define GST_EDITOR_ELEMENT(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_EDITOR_ELEMENT,GstEditorElement))
#define GST_EDITOR_ELEMENT_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_EDITOR_ELEMENT,GstEditorElementClass))
#define GST_IS_EDITOR_ELEMENT(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_EDITOR_ELEMENT))
#define GST_IS_EDITOR_ELEMENT_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_EDITOR_ELEMENT))

#define GST_EDITOR_ELEMENT_PARENT(obj) (GST_EDITOR_ELEMENT(obj)->parent)
#define GST_EDITOR_ELEMENT_GROUP(obj) (GST_EDITOR_ELEMENT(obj)->group)
#define GST_EDITOR_ELEMENT_CANVAS(obj) (GST_EDITOR_ELEMENT(obj)->canvas)

struct _GstEditorElement {
  GstObject object;

  /* parent object (NULL if I am the parent) */
  GstEditorBin *parent;

  /* toplevel canvas (myself if I am the toplevel) */
  GstEditorCanvas *canvas;

  /* the element we're associated with */
  GstElement *element;

  /* whether we've been realized or not */
  gboolean realized;

  /* toplevel group, must be !NULL */
  GnomeCanvasGroup *group;			// parent group

  /* visual stuff */
  gdouble x,y;					// center
  gdouble width,height;				// size
  GnomeCanvasItem *border,*title,*resizebox;	// easy ones
  GnomeCanvasItem *statebox[4],*statetext[4];	// GST_STATE_*

  gdouble insidewidth,insideheight;		// minimum space inside
  gdouble minwidth,minheight;			// minimum size
  gdouble titlewidth,titleheight;		// size of title
  gdouble statewidth,stateheight;		// size of state boxes
  gdouble sinkwidth,sinkheight;			// size of sink pads
  gdouble srcwidth,srcheight;			// size of src pads
  gint sinks,srcs;				// how many pads?

  GnomeCanvasGroup *insidegroup;		// contents if any

  gboolean resize;				// does it need resizing?

  /* list of pads */
  GList *srcpads,*sinkpads;
  gboolean padlistchange;

  /* interaction state */
  gboolean dragging,resizing,moved,hesitating;
  gdouble offx,offy,dragx,dragy;
};

struct _GstEditorElementClass {
  GnomeCanvasGroupClass parent_class;

  void (*name_changed) (GstEditorElement *element);
  void (*realize) (GstEditorElement *element);
  gint (*event) (GnomeCanvasItem *item,GdkEvent *event,
                GstEditorElement *element);
  gint (*button_event) (GnomeCanvasItem *item,GdkEvent *event,
                        GstEditorElement *element);
};


GtkType gst_editor_element_get_type();
GstEditorElement *gst_editor_element_new(GstEditorBin *parent,
                                         GstElement *element,
                                         const gchar *first_arg_name,...);
void gst_editor_element_construct(GstEditorElement *element,
                                  GstEditorBin *parent,
                                  const gchar *first_arg_name,
                                  va_list args);
void gst_editor_element_repack(GstEditorElement *element);
GstEditorPad *gst_editor_element_add_pad(GstEditorElement *element,
                                         GstPad *pad);  
void gst_editor_element_set_name(GstEditorElement *element,
	                                const gchar *name);
const gchar *gst_editor_element_get_name(GstEditorElement *element);


#define GST_TYPE_EDITOR_BIN \
  (gst_editor_bin_get_type())
#define GST_EDITOR_BIN(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_EDITOR_BIN,GstEditorBin))
#define GST_EDITOR_BIN_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_EDITOR_BIN,GstEditorBin))
#define GST_IS_EDITOR_BIN(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_EDITOR_BIN))
#define GST_IS_EDITOR_BIN_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_EDITOR_BIN))
  
struct _GstEditorBin {
  GstEditorElement element;

  /* lists of GUI elements and connections */ 
  GList *elements, *connections;

  /* connection state */
  GstEditorPad *frompad;		// where the drawing started from
  gboolean fromsrc;			// are we connecting *from* a source?
  gboolean connecting;			// if we're trying to connect right now
  GstEditorConnection *connection;	// the connection we're operating on
  GstEditorPad *ghostpad;		// potential ghost pad
  gboolean inpadregion;			// is cursor in pad region
};

struct _GstEditorBinClass {
  GstEditorElementClass parent_class;
};



GtkType gst_editor_bin_get_type();
GstEditorBin *gst_editor_bin_new(GstEditorBin *parent,GstBin *bin,
                                 const gchar *first_arg_name,...);
void gst_editor_bin_connection_drag(GstEditorBin *bin,
                                    gdouble wx,gdouble wy);
void gst_editor_bin_start_banding(GstEditorBin *bin,GstEditorPad *pad);


#define GST_TYPE_EDITOR_CANVAS \
  (gst_editor_canvas_get_type())
#define GST_EDITOR_CANVAS(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_EDITOR_CANVAS,GstEditorCanvas))
#define GST_EDITOR_CANVAS_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_EDITOR_CANVAS,GstEditorCanvasClass))
#define GST_IS_EDITOR_CANVAS(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_EDITOR_CANVAS))
#define GST_IS_EDITOR_CANVAS_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_EDITOR_CANVAS))


struct _GstEditorCanvas {
  GstEditorBin bin;

  gboolean inchild;

  GnomeCanvas *canvas;
};

struct _GstEditorCanvasClass {
  GnomeCanvasClass parent_class;
};


GtkType gst_editor_canvas_get_type();
GstEditorCanvas *gst_editor_canvas_new(GstBin *bin,
                                       const gchar *first_arg_name,...);
GtkWidget *gst_editor_canvas_get_canvas(GstEditorCanvas *canvas);
void gst_editor_bin_add(GstEditorBin *parent,GstEditorElement *element);


#define GST_TYPE_EDITOR_PAD \
  (gst_editor_pad_get_type())
#define GST_EDITOR_PAD(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_EDITOR_PAD,GstEditorPad))
#define GST_EDITOR_PAD_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_EDITOR_PAD,GstEditorPadClass))
#define GST_IS_EDITOR_PAD(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_EDITOR_PAD))
#define GST_IS_EDITOR_PAD_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_EDITOR_PAD))

struct _GstEditorPad {
  GtkObject object; 

  /* parent element */
  GstEditorElement *parent;

  /* toplevel canvas */
  GstEditorCanvas *canvas;

  /* the pad we're associated with */
  GstPad *pad;
  /* if this is a sink (convenience) */
  gboolean issrc;

  /* whether we've been realized or not */
  gboolean realized;

  /* connections */
  GstEditorConnection *connection;
  GstEditorConnection *ghostconnection;

  /* visual stuff */
  GnomeCanvasGroup *group;
  GnomeCanvasItem *border,*title,*padbox;
  gboolean sinkpad;				// is this a sink pad?
  gdouble x,y;					// location
  gdouble width,height;				// actual size
  gdouble boxwidth,boxheight;			// size of pad box
  gboolean resize;				// does it need resizing?
  
  /* interaction state */
  gboolean dragging,resizing,moved;
  gdouble dragx,dragy;

  /* connection */
//  GnomeCanvasItem *connection;		// can't use
//GstEditorConnection
};
  
struct _GstEditorPadClass {
  GtkObjectClass parent_class;

  void (*realize) (GstEditorPad *pad);
};
  
GtkType gst_editor_pad_get_type();
GstEditorPad *gst_editor_pad_new(GstEditorElement *parent,GstPad *pad,
                                 const gchar *first_arg_name, ...);
void gst_editor_pad_construct(GstEditorPad *element,
                              GstEditorElement *parent,
                              const gchar *first_arg_name,va_list args);
void gst_editor_pad_repack(GstEditorPad *pad);



#define GST_TYPE_EDITOR_CONNECTION \
  (gst_editor_connection_get_type())
#define GST_EDITOR_CONNECTION(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_EDITOR_CONNECTION,GstEditorConnection))
#define GST_EDITOR_CONNECTION_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_EDITOR_CONNECTION,GstEditorConnectionClass))
#define GST_IS_EDITOR_CONNECTION(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_EDITOR_CONNECTION))
#define GST_IS_EDITOR_CONNECTION_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_EDITOR_CONNECTION))

struct _GstEditorConnection {
  GtkObject object;

  /* our parent */
  GstEditorElement *parent;

  /* the two pads we're connecting */
  GstEditorPad *frompad, *topad;
  /* is this starting at a source (convenience) */
  gboolean fromsrc;

  /* toplevel canvas */
  GstEditorCanvas *canvas;

  /* whether we've been realized or not */
  gboolean realized;

  /* are we a ghosted connection? */
  gboolean ghost;

  /* visual stuff */
  GnomeCanvasItem *line;
  GnomeCanvasPoints *points;
  gdouble x,y;                                  // terminating point
  gboolean resize;                             // does it need resizing?
};

struct _GstEditorConnectionClass {
  GtkObjectClass parent_class;
  void (*realize) (GstEditorConnection *connection);
};

GtkType gst_editor_connection_get_type();
GstEditorConnection *gst_editor_connection_new(GstEditorBin *parent,
                                               GstEditorPad *frompad);

void gst_editor_connection_resize(GstEditorConnection *connection);
void gst_editor_connection_set_endpoint(GstEditorConnection *connection,
                                        gdouble x,gdouble y);
void gst_editor_connection_set_endpad(GstEditorConnection *connection,
                                      GstEditorPad *pad);
void gst_editor_connection_connect(GstEditorConnection *connection);


#endif /* __GST_EDITOR_H__ */
