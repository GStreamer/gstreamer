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
# suse
sudo cp gstreamer-pbutils-0.10.vapi /usr/share/vala/mediainfo/vapi/
# ubuntu
sudo cp gstreamer-pbutils-0.10.vapi /usr/share/vala-0.10/vapi/
*/

public class MediaInfo.Info : VBox
{
  // ui components
  private Label container_name;
  private Label mime_type;
  private Label duration;
  private Image icon_image;
  private Notebook video_streams;
  private Notebook audio_streams;
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
    drawing_area.expose_event.connect (on_drawing_area_expose);
    drawing_area.realize.connect (on_drawing_area_realize);
    drawing_area.unrealize.connect (on_drawing_area_unrealize);
    pack_start (drawing_area, true, true, 0);

    table = new Table (8, 3, false);
    pack_start (table, false, false, 0);
     
    label = new Label (null);
    label.set_markup("<b>Container</b>");
    label.set_alignment (0.0f, 0.5f);
    table.attach (label, 0, 3, row, row+1, fill_exp, 0, 0, 1);
    row++;

    icon_image = new Image ();
    table.attach (icon_image, 2, 3, row, row+3, fill, 0, 0, 0);

    label = new Label ("Format:");
    label.set_alignment (1.0f, 0.5f);
    table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
    container_name = new Label (null);
    container_name.set_alignment (0.0f, 0.5f);
    table.attach (container_name, 1, 2, row, row+1, fill_exp, 0, 3, 1);
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
    table.attach (label, 0, 3, row, row+1, fill_exp, 0, 0, 1);
    row++;

    video_streams = new Notebook ();
    video_streams.switch_page.connect (on_video_stream_switched);
    table.attach (video_streams, 0, 3, row, row+1, fill_exp, 0, 0, 1);
    row++;

    label = new Label (null);
    label.set_markup("<b>Audio Streams</b>");
    label.set_alignment (0.0f, 0.5f);
    table.attach (label, 0, 3, row, row+1, fill_exp, 0, 0, 1);
    row++;

    audio_streams = new Notebook ();
    audio_streams.switch_page.connect (on_audio_stream_switched);
    table.attach (audio_streams, 0, 3, row, row+1, fill_exp, 0, 0, 1);
    row++;

    // TODO: add container stream info widgets

    // TODO: add tag list widget

    // TODO: add message list widget    
    
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

    state = State.NULL;
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

    if (uri != null) {
      DiscovererInfo info;
      File file = File.new_for_uri(uri);

      try {
        FileInfo finfo = file.query_info ("standard::*", FileQueryInfoFlags.NONE, null);
        mime_type.set_text (finfo.get_attribute_string (FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE));
        icon_image.set_from_gicon ((Icon) finfo.get_attribute_object (FILE_ATTRIBUTE_STANDARD_ICON), IconSize.DIALOG);
      } catch (Error e) {
        debug ("Failed to query file info from %s: %s: %s", uri, e.domain.to_string (), e.message);
      } 

      try {
        GLib.List<DiscovererStreamInfo> l;
        DiscovererStreamInfo sinfo;
        //DiscovererVideoInfo vinfo;
        //DiscovererAudioInfo ainfo;
        Table table;
        Label label;
        uint row;
        AttachOptions fill = AttachOptions.FILL;
        AttachOptions fill_exp = AttachOptions.EXPAND|AttachOptions.FILL;
        string str;

        info = dc.discover_uri (uri);

        ClockTime dur = info.get_duration ();
        str = "%u:%02u:%02u.%09u".printf (
          (uint) (dur / (SECOND * 60 * 60)),
          (uint) ((dur / (SECOND * 60)) % 60),
          (uint) ((dur / SECOND) % 60),
          (uint) ((dur) % SECOND));
        this.duration.set_text (str);
        //stdout.printf ("Duration: %s\n", dur_str);

        // TODO: need caps for the container, so that we can have human readable container name
        //container_name.set_text (pb_utils_get_codec_description (Caps.from_string("application/ogg")));

        // get stream info
        while (video_streams.get_n_pages() > 0) {
          video_streams.remove_page (-1);
        }
        l = info.get_video_streams ();
        for (int i = 0; i < l.length (); i++) {
          sinfo = l.nth_data (i);

          row = 0;
          table = new Table (2, 7, false);

          label = new Label(sinfo.get_caps ().to_string ());
          label.set_ellipsize (Pango.EllipsizeMode.END);
          label.set_alignment (0.0f, 0.5f);
          table.attach (label, 0, 2, row, row+1, fill_exp, 0, 0, 1);
          row++;

          label = new Label ("Codec:");
          label.set_alignment (1.0f, 0.5f);
          table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
          str = pb_utils_get_codec_description (sinfo.get_caps ());
          label = new Label (str);
          label.set_alignment (0.0f, 0.5f);
          table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
          row++;

          label = new Label ("Bitrate:");
          label.set_alignment (1.0f, 0.5f);
          table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
          str = "%u / %u bits/second".printf (((DiscovererVideoInfo)sinfo).get_bitrate(),((DiscovererVideoInfo)sinfo).get_max_bitrate());
          label = new Label (str);
          label.set_alignment (0.0f, 0.5f);
          table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
          row++;

          // TODO: add named resolutions: (640x480=VGA)
          label = new Label ("Resolution:");
          label.set_alignment (1.0f, 0.5f);
          table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
          str = "%u x %u".printf (((DiscovererVideoInfo)sinfo).get_width(),((DiscovererVideoInfo)sinfo).get_height());
          label = new Label (str);
          label.set_alignment (0.0f, 0.5f);
          table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
          row++;

          label = new Label ("Framerate:");
          label.set_alignment (1.0f, 0.5f);
          table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
          str = "%u / %u frames/second".printf (((DiscovererVideoInfo)sinfo).get_framerate_num(),((DiscovererVideoInfo)sinfo).get_framerate_denom());
          label = new Label (str);
          label.set_alignment (0.0f, 0.5f);
          table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
          row++;

          label = new Label ("PixelAspect:");
          label.set_alignment (1.0f, 0.5f);
          table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
          str = "%u : %u".printf (((DiscovererVideoInfo)sinfo).get_par_num(),((DiscovererVideoInfo)sinfo).get_par_denom());
          label = new Label (str);
          label.set_alignment (0.0f, 0.5f);
          table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
          row++;

          label = new Label ("Bitdepth:");
          label.set_alignment (1.0f, 0.5f);
          table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
          str = "%u bits/pixel".printf (((DiscovererVideoInfo)sinfo).get_depth());
          label = new Label (str);
          label.set_alignment (0.0f, 0.5f);
          table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
          row++;

          video_streams.append_page (table, new Label (@"video $i"));
        }
        video_streams.show_all();

        while (audio_streams.get_n_pages() > 0) {
          audio_streams.remove_page (-1);
        }
        l = info.get_audio_streams ();
        for (int i = 0; i < l.length (); i++) {
          sinfo = l.nth_data (i);

          row = 0;
          table = new Table (2, 6, false);

          label = new Label(sinfo.get_caps ().to_string ());
          label.set_ellipsize (Pango.EllipsizeMode.END);
          label.set_alignment (0.0f, 0.5f);
          table.attach (label, 0, 2, row, row+1, fill_exp, 0, 0, 1);
          row++;

          label = new Label ("Codec:");
          label.set_alignment (1.0f, 0.5f);
          table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
          str = pb_utils_get_codec_description (sinfo.get_caps ());
          label = new Label (str);
          label.set_alignment (0.0f, 0.5f);
          table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
          row++;

          label = new Label ("Bitrate:");
          label.set_alignment (1.0f, 0.5f);
          table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
          str = "%u / %u bits/second".printf (((DiscovererAudioInfo)sinfo).get_bitrate(),((DiscovererAudioInfo)sinfo).get_max_bitrate());
          label = new Label (str);
          label.set_alignment (0.0f, 0.5f);
          table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
          row++;

          label = new Label ("Samplerate:");
          label.set_alignment (1.0f, 0.5f);
          table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
          str = "%u samples/second".printf (((DiscovererAudioInfo)sinfo).get_sample_rate());
          label = new Label (str);
          label.set_alignment (0.0f, 0.5f);
          table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
          row++;

          label = new Label ("Channels:");
          label.set_alignment (1.0f, 0.5f);
          table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
          str = "%u".printf (((DiscovererAudioInfo)sinfo).get_channels());
          label = new Label (str);
          label.set_alignment (0.0f, 0.5f);
          table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
          row++;

          label = new Label ("Bitdepth:");
          label.set_alignment (1.0f, 0.5f);
          table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
          str = "%u bits/sample".printf (((DiscovererAudioInfo)sinfo).get_depth());
          label = new Label (str);
          label.set_alignment (0.0f, 0.5f);
          table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
          row++;
  
          audio_streams.append_page (table, new Label (@"audio $i"));
        }
        audio_streams.show_all();
        //l = info.get_container_streams ();
        
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

  private bool on_drawing_area_expose (Widget widget, Gdk.EventExpose event)
  {
    if (pb.current_state < State.PAUSED) {
      Gdk.Window w = widget.get_window();
      Gtk.Allocation a;
      widget.get_allocation(out a);
      Cairo.Context cr = Gdk.cairo_create (w);

      cr.set_source_rgb (0, 0, 0);
      cr.rectangle (0, 0, a.width, a.height);
      cr.fill ();
    }
    return false;
  }

  private void on_drawing_area_realize (Widget widget)
  {
    widget.get_window ().ensure_native ();
    widget.unset_flags(Gtk.WidgetFlags.DOUBLE_BUFFERED);
  }

  private void on_drawing_area_unrealize (Widget widget)
  {
    pb.set_state (State.NULL);
  }

  private void on_element_sync_message (Gst.Bus bus, Message message)
  {
    Structure structure = message.get_structure ();
    if (structure.has_name ("prepare-xwindow-id"))
    {
      XOverlay xoverlay = message.src as XOverlay;
      xoverlay.set_xwindow_id (Gdk.x11_drawable_get_xid (drawing_area.get_window()));

      if (message.src.get_class ().find_property ("force-aspect-ratio") != null) {
        ((GLib.Object)message.src).set_property ("force-aspect-ratio", true);
      }
    }
  }

  private void on_video_stream_switched (NotebookPage page, uint page_num)
  {
    if (pb.current_state > State.PAUSED) {
      stdout.printf ("Switching video to: %u\n", page_num);
      ((GLib.Object)pb).set_property ("current-video", (int)page_num);
    }
  }

  private void on_audio_stream_switched (NotebookPage page, uint page_num)
  {
    if (pb.current_state > State.PAUSED) {
      stdout.printf ("Switching audio to: %u\n", page_num);
      ((GLib.Object)pb).set_property ("current-audio", (int)page_num);
    }
  }
}
