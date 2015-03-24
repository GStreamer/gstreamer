/* GStreamer Editing Services
 * Copyright (C) 2015 Mathieu Duponchelle <mathieu.duponchelle@opencreed.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef _GES_LAUNCHER
#define _GES_LAUNCHER

#include <ges/ges.h>

G_BEGIN_DECLS

#define GES_TYPE_LAUNCHER ges_launcher_get_type()

#define GES_LAUNCHER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_LAUNCHER, GESLauncher))

#define GES_LAUNCHER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_LAUNCHER, GESLauncherClass))

#define GES_IS_LAUNCHER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_LAUNCHER))

#define GES_IS_LAUNCHER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_LAUNCHER))

#define GES_LAUNCHER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_LAUNCHER, GESLauncherClass))

typedef struct _GESLauncherPrivate GESLauncherPrivate;
typedef struct _GESLauncher GESLauncher;
typedef struct _GESLauncherClass GESLauncherClass;

struct _GESLauncher {
  GApplication parent;

  /*< private >*/
  GESLauncherPrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

struct _GESLauncherClass {
  /*< private >*/
  GApplicationClass parent_class;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GType ges_launcher_get_type (void);

GESLauncher* ges_launcher_new (void);
gint ges_launcher_get_exit_status (GESLauncher *self);

G_END_DECLS

#endif /* _GES_LAUNCHER */
