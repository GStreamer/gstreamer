/* gui.h
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

#ifndef __DEMO_PLAYER_H_INCLUDED_
#define __DEMO_PLAYER_H_INCLUDED_

#include <glib-object.h>

G_BEGIN_DECLS

#define DEMO_TYPE_PLAYER          (demo_player_get_type())
#define DEMO_PLAYER(o)            (G_TYPE_CHECK_INSTANCE_CAST((o), DEMO_TYPE_PLAYER, DemoPlayer))
#define DEMO_IS_PLAYER(o)         (G_TYPE_CHECK_INSTANCE_TYPE((o), DEMO_TYPE_PLAYER))
#define DEMO_PLAYER_TYPE(o)       (G_TYPE_FROM_INSTANCE (o))
#define DEMO_PLAYER_TYPE_NAME(o)  (g_type_name (DEMO_PLAYER_TYPE (o)))

#define DEMO_PLAYER_CLASS(c)      (G_TYPE_CHECK_CLASS_CAST((c),    DEMO_TYPE_PLAYER, DemoPlayerClass))
#define DEMO_IS_PLAYER_CLASS(c)   (G_TYPE_CHECK_CLASS_TYPE((c),    DEMO_TYPE_PLAYER))
#define DEMO_PLAYER_GET_CLASS(o)  (G_TYPE_INSTANCE_GET_CLASS ((o), DEMO_TYPE_PLAYER, DemoPlayerClass))

typedef struct _DemoPlayer DemoPlayer;
typedef struct _DemoPlayerClass DemoPlayerClass;

struct _DemoPlayer
{
  GObject parent;
};

struct _DemoPlayerClass
{
  GObjectClass parent;
  void (*scale_rate)   (DemoPlayer   *player, gdouble     scale);
  void (*set_rate)     (DemoPlayer   *player, gdouble     new_rate);
  void (*load_uri)     (DemoPlayer   *player, gchar      *uri);
  void (*play)         (DemoPlayer   *player);
  void (*pause)        (DemoPlayer   *player);
  void (*seek_by)      (DemoPlayer   *player, gint        seconds);
  void (*seek_to)      (DemoPlayer   *player, gint        seconds);
  gint (*get_position) (DemoPlayer   *player);
  gint (*get_duration) (DemoPlayer   *player);
};

GType demo_player_get_type (void);

void demo_player_scale_rate   (DemoPlayer   *player, gdouble    scale);
void demo_player_set_rate     (DemoPlayer   *player, gdouble    new_rate);
void demo_player_load_uri     (DemoPlayer   *player, gchar      *uri);
void demo_player_play         (DemoPlayer   *player);
void demo_player_pause        (DemoPlayer   *player);
void demo_player_seek_by      (DemoPlayer   *player, gint        seconds);
void demo_player_seek_to      (DemoPlayer   *player, gint        second);
gint demo_player_get_position (DemoPlayer   *player);
gint demo_player_get_duration (DemoPlayer   *player);

G_END_DECLS

#endif /* __DEMO_PLAYER_H_INCLUDED_ */
