#ifndef FULL_SCREEN_H
#define FULL_SCREEN_H

#include <libgnome/gnome-defs.h>
#include <gtk/gtkwindow.h>
#include "gstplay.h"

BEGIN_GNOME_DECLS



#define TYPE_FULL_SCREEN            (full_screen_get_type ())
#define FULL_SCREEN(obj)            (GTK_CHECK_CAST ((obj), TYPE_FULL_SCREEN, FullScreen))
#define FULL_SCREEN_CLASS(klass)    (GTK_CHECK_CLASS_CAST ((klass), TYPE_FULL_SCREEN,	\
				     FullScreenClass))
#define IS_FULL_SCREEN(obj)         (GTK_CHECK_TYPE ((obj), TYPE_FULL_SCREEN))
#define IS_FULL_SCREEN_CLASS(klass) (GTK_CHECK_CLASS_TYPE ((klass), TYPE_FULL_SCREEN))

typedef struct _FullScreen FullScreen;
typedef struct _FullScreenClass FullScreenClass;

typedef struct _FullScreenPrivate FullScreenPrivate;

struct _FullScreen {
	GtkWindow window;

	/* Private data */
	FullScreenPrivate *priv;
};

struct _FullScreenClass {
	GtkWindowClass parent_class;
};

GtkType full_screen_get_type (void);

GtkWidget *full_screen_new (void);

void full_screen_set_uri (FullScreen *fs, GstPlay *play, const guchar *uri);
GstPlay * full_screen_get_gst_play (FullScreen *fs);

END_GNOME_DECLS

#endif
