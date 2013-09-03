/* GStreamer media browser
 * Copyright (C) 2010-2013 Stefan Sauer <ensonic@user.sf.net>
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

using MediaInfo;
using GLib;

bool version;
const OptionEntry[] options = {
{ "version", 0, 0, OptionArg.NONE, ref version, "Display version number", null },
  { null }
};

int
main(string[] args)
{
    
  Intl.bindtextdomain (Config.GETTEXT_PACKAGE, Config.LOCALEDIR);
  Intl.bind_textdomain_codeset (Config.GETTEXT_PACKAGE, "UTF-8");
  Intl.textdomain (Config.GETTEXT_PACKAGE);

  OptionContext opt_context = new OptionContext (_("<directory>"));
  opt_context.set_help_enabled (true);
  opt_context.add_main_entries (options, null);
  opt_context.add_group (Gst.init_get_option_group ());
  opt_context.add_group (Gtk.get_option_group (true));
  try {
    opt_context.parse (ref args);
  } catch (Error e) {
    stdout.printf ("%s", opt_context.get_help(true, null));
    return (0);  
  }

  if (version) {
	  stdout.printf ("%s\n", Config.PACKAGE_STRING);
    return (0);
  }

  // take remaining arg and use as default dir
  string directory = null;  
  if (args.length > 1) {
    directory=args[1];
  }

  App app = new App (directory);
  app.show_all ();

  Gtk.main ();

  return (0);
}
