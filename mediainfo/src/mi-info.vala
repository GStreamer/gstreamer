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

/*
we need to update the vapi for yet unreleased gstreamer api:

cd vala/mediainfo/vapi
vala-gen-introspect gstreamer-pbutils-0.10 packages/gstreamer-pbutils-0.10
vapigen --vapidir . --library gstreamer-pbutils-0.10 packages/gstreamer-pbutils-0.10/gstreamer-pbutils-0.10.gi
 
*/

public class MediaInfo.Info : VBox
{
  // ui components
  private Label mime_type;
  private Label duration;
  private DrawingArea drawing_area;
  // gstreamer objects
  private Discoverer dc;
  private Pipeline pb;

  public Info ()
  {
    Label label;
    Table table;
    AttachOptions fill = AttachOptions.FILL;
    AttachOptions fill_exp = AttachOptions.EXPAND|AttachOptions.FILL;
    uint row = 0;

    // configure the view
    set_homogeneous (false);

    // add widgets
    // FIXME: handle aspect ratio (AspectFrame.ratio)
    // FIXME: paint it black from the start
    drawing_area = new DrawingArea ();
    drawing_area.set_size_request (300, 150);
    pack_start (drawing_area, true, true, 0);

    table = new Table (5, 2, false);
    pack_start (table, false, false, 0);
     
    label = new Label (null);
    label.set_markup("<b>Container</b>");
    label.set_alignment (0.0f, 0.5f);
    table.attach (label, 0, 2, row, row+1, fill_exp, 0, 0, 1);
    row++;

    label = new Label ("Mime-Type:");
    label.set_alignment (1.0f, 0.5f);
    table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
    mime_type = new Label (null);
    mime_type.set_alignment (0.0f, 0.5f);
    table.attach (mime_type, 1, 2, row, row+1, fill_exp, 0, 3, 1);
    row++;

    label = new Label ("Duration:");
    label.set_alignment (1.0f, 0.5f);
    table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
    duration = new Label (null);
    duration.set_alignment (0.0f, 0.5f);
    table.attach (duration, 1, 2, row, row+1, fill_exp, 0, 3, 1);
    row++;

    label = new Label (null);
    label.set_markup("<b>Video Streams</b>");
    label.set_alignment (0.0f, 0.5f);
    table.attach (label, 0, 2, row, row+1, fill_exp, 0, 0, 1);
    row++;

    // TODO: add video stream info widgets

    label = new Label (null);
    label.set_markup("<b>Audio Streams</b>");
    label.set_alignment (0.0f, 0.5f);
    table.attach (label, 0, 2, row, row+1, fill_exp, 0, 0, 1);
    row++;

    // TODO: add audio stream info widgets

    
    
    show_all ();

    // set up the gstreamer components
    try {
      dc = new Discoverer ((ClockTime)(Gst.SECOND * 10));
    } catch (Error e) {
      debug ("Failed to create the discoverer: %s: %s", e.domain.to_string (), e.message);
    }

    pb = ElementFactory.make ("playbin2", "player") as Pipeline;
    Gst.Bus bus = pb.get_bus ();
    bus.set_sync_handler (bus.sync_signal_handler);
    bus.sync_message["element"].connect (on_element_sync_message);

  }

  ~Info ()
  {
    // stop previous playback
    pb.set_state (State.NULL);
  }

  // public methods

  public bool discover (string uri)
  {
    bool res = true;

    // stop previous playback
    pb.set_state (State.NULL);

    // TODO: check if uri is a regular (local file)?
    if (uri != null) {
      DiscovererInfo info;
      bool uncertain;

      string mime_type = g_content_type_guess (uri, null, out uncertain);
      if (uncertain) {
        this.mime_type.set_markup (@"<i>$mime_type</i>");
      } else {
        this.mime_type.set_text (mime_type);
      }

      try {
        info = dc.discover_uri (uri);

        ClockTime dur = info.get_duration ();
        string dur_str = "%u:%02u:%02u.%09u".printf (
          (uint) (dur / (SECOND * 60 * 60)),
          (uint) ((dur / (SECOND * 60)) % 60),
          (uint) ((dur / SECOND) % 60),
          (uint) ((dur) % SECOND));
        this.duration.set_text (dur_str);

        //stdout.printf ("Duration: %s\n", dur_str);

        // TODO: get more info
        // list = info.get_video_streams ();
        // list = info.get_audio_streams ();
        // list = info.get_container_streams ();
        
      } catch (Error e) {
        debug ("Failed to extract metadata from %s: %s: %s", uri, e.domain.to_string (), e.message);
      }

      // play file
      ((GLib.Object)pb).set_property ("uri", uri);
      pb.set_state (State.PLAYING);

      res = true;
    }
    
    return (res);
  }

  // signal handlers

  private void on_element_sync_message (Gst.Bus bus, Message message)
  {
    Structure structure = message.get_structure ();
    if (structure.has_name ("prepare-xwindow-id"))
    {
      XOverlay xoverlay = message.src as XOverlay;
      xoverlay.set_xwindow_id (Gdk.x11_drawable_get_xid (drawing_area.window));

      if (message.src.get_class ().find_property ("force-aspect-ratio") != null) {
        ((GLib.Object)message.src).set_property ("force-aspect-ratio", true);
      }

      drawing_area.unset_flags(Gtk.WidgetFlags.DOUBLE_BUFFERED);
    }
  }
}
