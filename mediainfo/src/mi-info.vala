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
using Gst;

public class MediaInfo.Info : VBox
{
  private Label uri;
  //private Discoverer dc;

  public Info ()
  {
    // configure the view
    set_homogeneous (false);

    // add widgets
    uri = new Label ("");
    pack_start (uri, false, false, 0);

    show_all ();

    // set up the gstreamer components
    //dc = new Discoverer (Gst.SECONDS * 10, null);
  }

  public bool discover (string uri)
  {
    this.uri.set_text (uri);

    //DiscovererInfo info = dc.discover_uri (uri, null);
    
    return (true);
  }
}