#include <gnome.h>

#include <gst/gst.h>
#include <gst/gstutils.h>

#include "gsteditor.h"

/* class functions */
static void gst_editor_connection_class_init(GstEditorConnectionClass *klass);
static void gst_editor_connection_init(GstEditorConnection *connection);
static void gst_editor_connection_set_arg(GtkObject *object,GtkArg *arg,guint id);
static void gst_editor_connection_get_arg(GtkObject *object,GtkArg *arg,guint id);
static void gst_editor_connection_destroy(GtkObject *object);
static void gst_editor_connection_realize(GstEditorConnection *connection);

/* events fired by items within self */
//static gint gst_editor_connection_line_event(GnomeCanvasItem *item,
//                                             GdkEvent *event,
//                                             GstEditorConnection *connection);

/* utility functions */


enum {
  ARG_0,
  ARG_X,
  ARG_Y,
  ARG_FROMPAD,
  ARG_TOPAD,
  ARG_GHOST,
};

enum {
  LAST_SIGNAL
};

static GtkObjectClass *parent_class;
//static guint gst_editor_connection_signals[LAST_SIGNAL] = { 0 };

GtkType gst_editor_connection_get_type() {
  static GtkType connection_type = 0;

  if (!connection_type) {
    static const GtkTypeInfo connection_info = {
      "GstEditorConnection",
      sizeof(GstEditorConnection),
      sizeof(GstEditorConnectionClass),
      (GtkClassInitFunc)gst_editor_connection_class_init,
      (GtkObjectInitFunc)gst_editor_connection_init,
      NULL,
      NULL,
      (GtkClassInitFunc)NULL,
    };
    connection_type = gtk_type_unique(gtk_object_get_type(),&connection_info);
  }
  return connection_type;
}

static void gst_editor_connection_class_init(GstEditorConnectionClass *klass) {
  GtkObjectClass *object_class;

  object_class = (GtkObjectClass*)klass;

  parent_class = gtk_type_class(gnome_canvas_line_get_type());

  gtk_object_add_arg_type("GstEditorConnection::x",GTK_TYPE_DOUBLE,
                          GTK_ARG_READWRITE,ARG_X);
  gtk_object_add_arg_type("GstEditorConnection::y",GTK_TYPE_DOUBLE,
                          GTK_ARG_READWRITE,ARG_Y);
  gtk_object_add_arg_type("GstEditorConnection::frompad",GTK_TYPE_POINTER,
                          GTK_ARG_READWRITE,ARG_FROMPAD);
  gtk_object_add_arg_type("GstEditorConnection::topad",GTK_TYPE_POINTER,
                          GTK_ARG_READWRITE,ARG_TOPAD);
  gtk_object_add_arg_type("GstEditorConnection::ghost",GTK_TYPE_BOOL,
                          GTK_ARG_READWRITE,ARG_GHOST);

  klass->realize = gst_editor_connection_realize;

  object_class->set_arg = gst_editor_connection_set_arg;
  object_class->get_arg = gst_editor_connection_get_arg;
  object_class->destroy = gst_editor_connection_destroy;
}

static void gst_editor_connection_init(GstEditorConnection *connection) {
  connection->points = gnome_canvas_points_new(2);
}

GstEditorConnection *gst_editor_connection_new(GstEditorBin *parent,
                                               GstEditorPad *frompad) {
  GstEditorConnection *connection;

  g_return_val_if_fail(parent != NULL, NULL);
  g_return_val_if_fail(GST_IS_EDITOR_BIN(parent), NULL);
  g_return_val_if_fail(frompad != NULL, NULL);
  g_return_val_if_fail(GST_IS_EDITOR_PAD(frompad), NULL);

  connection = GST_EDITOR_CONNECTION(gtk_type_new(GST_TYPE_EDITOR_CONNECTION));
  connection->frompad = frompad;
  connection->frompad->connection = connection;
  connection->fromsrc = connection->frompad->issrc;
  connection->parent = GST_EDITOR_ELEMENT(parent);

  gst_editor_connection_realize(connection);

  return connection;
}


static void gst_editor_connection_set_arg(GtkObject *object,GtkArg *arg,guint id) {
  GstEditorConnection *connection;

  /* get the major types of this object */
  connection = GST_EDITOR_CONNECTION(object);

  switch (id) {
    case ARG_X:
      connection->x = GTK_VALUE_DOUBLE(*arg);
      connection->resize = TRUE;
      break;
    case ARG_Y:
      connection->y = GTK_VALUE_DOUBLE(*arg);
      connection->resize = TRUE;
      break;
    case ARG_TOPAD:
      if (connection->topad) {
        if (connection->ghost)
          connection->topad->ghostconnection = NULL;
        else
          connection->topad->connection = NULL;
      }
      connection->topad = GTK_VALUE_POINTER(*arg);
      /* if this is the same type, refuse */
      if (connection->topad &&
          (connection->frompad->issrc == connection->topad->issrc))
        connection->topad = NULL;
      if (connection->topad) {
        if (connection->ghost)
          connection->topad->ghostconnection = connection;
        else
          connection->topad->connection = connection;
      }
      connection->resize = TRUE;
      break;
    case ARG_GHOST:
      connection->ghost = GTK_VALUE_BOOL(*arg);
      break;
    default:
      g_warning("gsteditorconnection: unknown arg!");
      break;
  }
  gst_editor_connection_resize(connection);
}

static void gst_editor_connection_get_arg(GtkObject *object,GtkArg *arg,guint id) {
  GstEditorConnection *connection;

  /* get the major types of this object */
  connection = GST_EDITOR_CONNECTION(object);

  switch (id) {
    case ARG_X:
      GTK_VALUE_INT(*arg) = connection->x;
      break;
    case ARG_Y:
      GTK_VALUE_INT(*arg) = connection->y;
      break;
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
  }
}


static void gst_editor_connection_realize(GstEditorConnection *connection) {
  connection->points->coords[0] = 0.0;
  connection->points->coords[1] = 0.0;
  connection->points->coords[2] = 0.0;
  connection->points->coords[3] = 0.0;
  connection->line = gnome_canvas_item_new(
    GST_EDITOR_ELEMENT(connection->parent)->group,
    gnome_canvas_line_get_type(),
    "points",connection->points,NULL);
}

static void gst_editor_connection_destroy(GtkObject *object) {
  GstEditorConnection *connection = GST_EDITOR_CONNECTION(object);

  gtk_object_destroy(GTK_OBJECT(connection->line));
}

void gst_editor_connection_resize(GstEditorConnection *connection) {
  gdouble x1,y1,x2,y2;

  if (connection->resize != TRUE) return;
  connection->resize = FALSE;

//  g_print("resizing connection, frompad is %p, topad is %p\n",
//          connection->frompad,connection->topad);

  /* calculate the new endpoints */
  if (connection->topad == NULL) {
    /* our base point is the source pad */
    if (connection->fromsrc)
      x1 = connection->frompad->x + connection->frompad->width;
    else
      x1 = connection->frompad->x;
    y1 = connection->frompad->y + (connection->frompad->height / 2);
    x2 = connection->x;
    y2 = connection->y;
    /* NOTE: coords are in the following state:
       x1,y1: item coords relative to the element's group
       x2,y2: item coords relative to the bin's group
       This means translating the x1,y1 coords into world, then into bin.
    */
    gnome_canvas_item_i2w(GNOME_CANVAS_ITEM(connection->frompad->parent->group),&x1,&y1);
    gnome_canvas_item_w2i(GNOME_CANVAS_ITEM(GST_EDITOR_ELEMENT_GROUP(connection->parent)),
                          &x1,&y1);
  } else {
    if (connection->fromsrc) {
      x1 = connection->frompad->x + connection->frompad->width;
      x2 = connection->topad->x;
    } else {
      x1 = connection->frompad->x;
      x2 = connection->topad->x + connection->topad->width;
    }
    y1 = connection->frompad->y + (connection->frompad->height / 2);
    y2 = connection->topad->y + (connection->topad->height / 2);
    gnome_canvas_item_i2w(GNOME_CANVAS_ITEM(connection->frompad->parent->group),&x1,&y1);
    gnome_canvas_item_w2i(GNOME_CANVAS_ITEM(GST_EDITOR_ELEMENT_GROUP(connection->parent)),
                          &x1,&y1);
    gnome_canvas_item_i2w(GNOME_CANVAS_ITEM(connection->topad->parent->group),&x2,&y2);
    gnome_canvas_item_w2i(GNOME_CANVAS_ITEM(GST_EDITOR_ELEMENT_GROUP(connection->parent)),
                          &x2,&y2);
  }

  connection->points->coords[0] = x1;
  connection->points->coords[1] = y1;
  connection->points->coords[2] = x2;
  connection->points->coords[3] = y2;
  gnome_canvas_item_set(connection->line,
                        "points",connection->points,NULL);
}

void gst_editor_connection_set_endpoint(GstEditorConnection *connection,
                                        gdouble x,gdouble y) {
  connection->x = x;
  connection->y = y;
  if (connection->topad) {
    if (connection->ghost)
      connection->topad->ghostconnection = NULL;
    else
      connection->topad->connection = NULL;
    connection->topad = NULL;
  }
  connection->resize = TRUE;
  gst_editor_connection_resize(connection);
}

void gst_editor_connection_set_endpad(GstEditorConnection *connection,
                                      GstEditorPad *pad) {
  // first check for the trivial case
  if (connection->topad == pad) return;

  // now clean up if we've changed pads
  if (connection->topad) {
    if (connection->ghost)
      connection->topad->ghostconnection = NULL;
    else
      connection->topad->connection = NULL;
  }
  connection->topad = pad;
  if (connection->ghost)
    connection->topad->ghostconnection = connection;
  else
    connection->topad->connection = connection;
  connection->resize = TRUE;
  gst_editor_connection_resize(connection);
}

void gst_editor_connection_connect(GstEditorConnection *connection) {
  if (connection->ghost) {
    g_print("uhhh.... Boo!\n");
  } else {
    if (connection->fromsrc)
      gst_pad_connect(connection->frompad->pad,connection->topad->pad);
    else
      gst_pad_connect(connection->topad->pad,connection->frompad->pad);
  }
}
