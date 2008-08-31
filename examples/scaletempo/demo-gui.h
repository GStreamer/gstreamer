/* demo-gui.h
 * Copyright (C) 2008 Rov Juvano <rovjuvano@users.sourceforge.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __DEMO_GUI_H_INCLUDED_
#define __DEMO_GUI_H_INCLUDED_

#include <glib-object.h>
#include "demo-player.h"

G_BEGIN_DECLS

#define DEMO_TYPE_GUI          (demo_gui_get_type())
#define DEMO_GUI(o)            (G_TYPE_CHECK_INSTANCE_CAST((o), DEMO_TYPE_GUI, DemoGui))
#define DEMO_IS_GUI(o)         (G_TYPE_CHECK_INSTANCE_TYPE((o), DEMO_TYPE_GUI))
#define DEMO_GUI_TYPE(o)       (G_TYPE_FROM_INSTANCE (o))
#define DEMO_GUI_TYPE_NAME(o)  (g_type_name (DEMO_GUI_GUI (o)))

#define DEMO_GUI_CLASS(c)      (G_TYPE_CHECK_CLASS_CAST((c),    DEMO_TYPE_GUI, DemoGuiClass))
#define DEMO_IS_GUI_CLASS(c)   (G_TYPE_CHECK_CLASS_TYPE((c),    DEMO_TYPE_GUI))
#define DEMO_GUI_GET_CLASS(o)  (G_TYPE_INSTANCE_GET_CLASS ((o), DEMO_TYPE_GUI, DemoGuiClass))

typedef struct _DemoGui DemoGui;
typedef struct _DemoGuiClass DemoGuiClass;

struct _DemoGui
{
  GObject parent;
};

struct _DemoGuiClass
{
  GObjectClass parent;
  void (*set_player)   (DemoGui   *gui, DemoPlayer   *player);
  void (*set_playlist) (DemoGui   *gui, GList        *uris);
  void (*show)         (DemoGui   *gui);
};

GType demo_gui_get_type (void);

void demo_gui_set_player   (DemoGui   *gui, DemoPlayer   *player);
void demo_gui_set_playlist (DemoGui   *gui, GList        *uris);
void demo_gui_show         (DemoGui   *gui);

G_END_DECLS

#endif /* __DEMO_GUI_H_INCLUDED_ */
