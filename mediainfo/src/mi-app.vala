/* GStreamer media browser
 * Copyright (C) 2010-2012 Stefan Sauer <ensonic@user.sf.net>
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

  public string directory { get; set; }

  public App(string? directory)
  {
    GLib.Object (type :  WindowType.TOPLEVEL);
    this.directory = directory;

    // configure the window
    set_title (_("GStreamer Media Info"));
    set_default_size (500, 350);
    try {
      set_default_icon_from_file (Config.PKGDATADIR + "/ui/icons/gst-mi.png");
    } catch (Error e) {
      debug ("Application icon missing: %s: %s", e.domain.to_string (), e.message);
    }
    destroy.connect (Gtk.main_quit);

    VBox vbox = new VBox( false, 0);
    add (vbox);

    // add a menubar
    vbox.pack_start (create_menu(), false, false, 0);

    HPaned paned = new HPaned ();
    paned.set_border_width (0);
    vbox.pack_start (paned, true, true, 3);

    // add a file-chooser with info pane as preview widget
    chooser = new FileChooserWidget (FileChooserAction.OPEN);
    paned.pack1 (chooser, true, true);

    if (directory != null) {
      //chooser.set_current_folder (GLib.Environment.get_home_dir ());
      chooser.set_current_folder (directory);
    }
    chooser.set_show_hidden (false);
    chooser.selection_changed.connect (on_update_preview);

    info = new Info ();
    paned.pack2 (info, true, true);
  }

  // helper

  private MenuBar create_menu ()
  {
    MenuBar menu_bar = new MenuBar ();
    Gtk.MenuItem item;
    Gtk.Menu sub_menu;
    AccelGroup accel_group;

    accel_group = new AccelGroup ();
    this.add_accel_group (accel_group);

    item = new Gtk.MenuItem.with_label (_("File"));
    menu_bar.append (item);

    sub_menu = new Gtk.Menu ();
    item.set_submenu (sub_menu);

    // TODO: add "open uri" item
    // -> dialog with text entry (pre-file with clipboard content)
    // -> discover that uri and clear selection in browser

    item = new ImageMenuItem.from_stock (Stock.QUIT, accel_group);
    sub_menu.append (item);
    item.activate.connect (Gtk.main_quit);

    item = new Gtk.MenuItem.with_label (_("View"));
    //item.set_accel_path ("<GstMi-Main>/MainMenu/View");
    menu_bar.append (item);

    sub_menu = new Gtk.Menu ();
    item.set_submenu (sub_menu);

    CheckMenuItem citem = new CheckMenuItem.with_label (_("Full Screen"));
    // see http://bugzilla.gnome.org/show_bug.cgi?id=551184
    // FIXME: we're also not getting a proper accelerator shown in the menu item
    citem.add_accelerator("activate", accel_group, Gdk.keyval_from_name ("F11"), 0, 0);
    //citem.set_accel_path ("<GstMi-Main>/MainMenu/View/FullScreen");
    //AccelMap.add_entry ("<GstMi-Main>/MainMenu/View/FullScreen", 0xffc8, 0);

    sub_menu.append (citem);
    citem.toggled.connect (on_fullscreen_toggled);

    // add "help" menu with "about" item
    item = new Gtk.MenuItem.with_label (_("Help"));
    menu_bar.append (item);

    sub_menu = new Gtk.Menu ();
    item.set_submenu (sub_menu);

    item = new ImageMenuItem.from_stock (Stock.ABOUT, accel_group);
    sub_menu.append (item);
    item.activate.connect (on_about_clicked);

    return (menu_bar);
  }

  // signal handler

  private void on_update_preview ()
  {
    File file = chooser.get_file();
    bool res = false;

    if (file != null && file.query_file_type (FileQueryInfoFlags.NONE, null) == FileType.REGULAR) {
      res = info.discover (chooser.get_uri());
    }
    chooser.set_preview_widget_active (res);
  }

  private void on_fullscreen_toggled (CheckMenuItem item)
  {
    if (item.active) {
      fullscreen();
    } else {
      unfullscreen();
    }
  }

  private void on_about_clicked (Gtk.MenuItem item)
  {
    AboutDialog dlg = new AboutDialog ();

    dlg.set_version(Config.PACKAGE_VERSION);
    dlg.set_program_name("GStreamer Media Info");
    dlg.set_comments(_("Quickly browse, play and analyze media files."));
    dlg.set_copyright("Stefan Sauer <ensonic@users.sf.net>");
    dlg.run();
    dlg.hide();
  }
}

