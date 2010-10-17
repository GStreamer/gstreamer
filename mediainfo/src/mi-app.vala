/* GStreamer media browser
 * Copyright (C) 2010 Stefan Sauer <ensonic@user.sf.net>
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
 * Free Software Foundation, Inc., 51 Franklin Steet,
 * Boston, MA 02110-1301, USA.
 */

using Gtk;
using MediaInfo;

public class MediaInfo.App : Window
{
  private FileChooserWidget chooser;
  private Info info;


  public App()
  {
    // configure the window
    set_title (_("GStreamer Media Info"));
    set_default_size (400, 300);
    destroy.connect (Gtk.main_quit);
    
    VBox vbox = new VBox(false, 0);
    add (vbox);
    
    // add a menubar
    vbox.pack_start (create_menu(), false, false, 0);
    
    // add a file-chooser with info pane as preview widget
    chooser = new FileChooserWidget (FileChooserAction.OPEN);
    vbox.pack_start (chooser, true, true, 3);

    info = new Info ();
    chooser.set_preview_widget (info);
    chooser.set_use_preview_label (false);
    chooser.update_preview.connect (on_update_preview);
  }

  // helper

  private MenuBar create_menu ()
  {
    MenuBar menu_bar = new MenuBar ();
    MenuItem item;
    Menu sub_menu;
    AccelGroup accel_group;
    
    accel_group = new AccelGroup ();
    this.add_accel_group (accel_group);
    
    item = new MenuItem.with_label (_("File"));
    menu_bar.append (item);
    
    sub_menu = new Menu ();
    item.set_submenu (sub_menu);
    
    item = new ImageMenuItem.from_stock (STOCK_QUIT, accel_group);
    sub_menu.append (item);
    item.activate.connect (Gtk.main_quit);

    return (menu_bar);  
  }

  // signal handler

  private void on_update_preview ()
  {
    string uri = chooser.get_preview_uri();
    bool res = info.discover (uri);

    chooser.set_preview_widget_active (res);
  }
}

