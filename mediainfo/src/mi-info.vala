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
using Gee;

/*
in the case we need to update the vapi for yet unreleased gstreamer api, these
are the steps. Right now its enough to install the vapi file from git

# checkout vala from gnome git
cd vala/mediainfo/vapi
vala-gen-introspect gstreamer-pbutils-0.10 packages/gstreamer-pbutils-0.10
vapigen --vapidir . --library gstreamer-pbutils-0.10 packages/gstreamer-pbutils-0.10/gstreamer-pbutils-0.10.gi
git diff packages/gstreamer-pbutils-0.10/gstreamer-pbutils-0.10.metadata >vapi.gstreamer-pbutils-0.10.patch

# suse
sudo cp gstreamer-pbutils-0.10.vapi /usr/share/vala/mediainfo/vapi/
# ubuntu
sudo cp gstreamer-pbutils-0.10.vapi /usr/share/vala-0.10/vapi/
*/

public class MediaInfo.Info : VPaned
{
  // layout
  private bool compact_mode = false;
  // ui components
  private Label container_name;
  private Label mime_type;
  private Label duration;
  private Image icon_image;
  private Notebook all_streams;    // there is either all or separate a/mediainfo/v
  private Notebook video_streams;  // depending on  sreen resolution
  private Notebook audio_streams;
  private DrawingArea drawing_area;
  // gstreamer objects
  private Discoverer dc;
  private Pipeline pb;
  private bool have_video = false;
  private uint num_video_streams;
  private uint cur_video_stream;
  private uint num_audio_streams;
  private uint cur_audio_stream;
  // stream data
  private Gdk.Pixbuf album_art = null;

  private HashMap<string, string> resolutions;
  private HashSet<string> tag_black_list;
  private HashMap<string, string> wikilinks;

  public Info ()
  {
    Label label;
    Table table;
    AttachOptions fill = AttachOptions.FILL;
    AttachOptions fill_exp = AttachOptions.EXPAND|AttachOptions.FILL;
    uint row = 0;

    // configure the view
    set_border_width (0);

    // setup lookup tables
    // video resolutions: http://upload.wikimedia.org/wikipedia/mediainfo/commons/e/e5/Vector_Video_Standards2.svg
    // FIXME: these are only for PAR = 1:1
    // we could have another list for CIF (http://en.wikipedia.org/wiki/Common_Intermediate_Format)
    resolutions = new HashMap<string, string> ();
    // 5:4
    resolutions["1280 x 1024"] = "SXGA";
    resolutions["2560 x 2048"] = "QSXGA";
    // 4:3
    resolutions["320 x 240"] = "QVGA";
    resolutions["640 x 480"] = "VGA";
    resolutions["768 x 576"] = "PAL";
    resolutions["800 x 600"] = "SVGA";
    resolutions["1024 x 768"] = "XGA";
    resolutions["1400 x 1050"] = "SXGA+";
    resolutions["1600 x 1200"] = "UXGA";
    resolutions["2048 x 1536"] = "QXGA";
    // 8:5 (16:10)
    resolutions["320 x 200"] = "CGA";
    resolutions["1280 x 800"] = "WXGA";
    resolutions["1680 x 1050"] = "WXGA+";
    resolutions["1920 x 1200"] = "WUXGA";
    // 5:3
    resolutions["800 x 480"] = "WVGA";
    resolutions["1280 x 768"] = "WXGA";
    // 16:9
    resolutions["854 x 480"] = "WVGA";
    resolutions["1280 x 720"] = "HD 720";
    resolutions["1920 x 1080"] = "HD 1080";

    // tags to skip (already extraced to specific discoverer fields)
    tag_black_list = new HashSet<string> ();
    tag_black_list.add ("bitrate");
    tag_black_list.add ("container-format");
    tag_black_list.add ("duration");
    tag_black_list.add ("nominal-bitrate");
    tag_black_list.add ("maximum-bitrate");

    // map from media-type to wikipedia-articles, prefix with http://en.wikipedia.org/wiki/
    // alternative source could be http://codecdictionary.com/
    // TODO: add more
    wikilinks = new HashMap<string, string> ();
    // container/tag formats
    wikilinks["application/ogg"] = "Ogg";
    wikilinks["application/x-annodex"] = "Ogg";
    wikilinks["video/x-msvideo"] = "Audio_Video_Interleave";
    // audio codecs
    wikilinks["audio/x-vorbis"] = "Vorbis";
    wikilinks["audio/x-wav"] = "WAV";
    // video codecs
    wikilinks["video/x-theora"] = "Theora";

    int screen_height = Gdk.Screen.get_default().get_height();
    if (screen_height <= 600) {
      compact_mode = true;
    }

    // add widgets
    // FIXME: handle aspect ratio (AspectFrame.ratio)
    drawing_area = new DrawingArea ();
    drawing_area.set_size_request (160, 120);
    drawing_area.expose_event.connect (on_drawing_area_expose);
    drawing_area.realize.connect (on_drawing_area_realize);
    drawing_area.unrealize.connect (on_drawing_area_unrealize);
    pack1 (drawing_area, true, true);

    ScrolledWindow sw = new ScrolledWindow (null, null);
    sw.set_policy (PolicyType.NEVER, PolicyType.ALWAYS);
    pack2 (sw, true, true);

    table = new Table (8, 3, false);
    sw.add_with_viewport (table);

    /* TODO: also use tabs for containers
     * - this is needed for e.g. mpeg-ts or mp3 inside ape
     * - we should move duration and mime-type out of the tabs
     */
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
    container_name.set_selectable(true);
    container_name.set_use_markup(true);
    table.attach (container_name, 1, 2, row, row+1, fill_exp, 0, 3, 1);
    row++;

    label = new Label ("Mime-Type:");
    label.set_alignment (1.0f, 0.5f);
    table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
    mime_type = new Label (null);
    mime_type.set_alignment (0.0f, 0.5f);
    mime_type.set_selectable(true);
    table.attach (mime_type, 1, 2, row, row+1, fill_exp, 0, 3, 1);
    row++;

    label = new Label ("Duration:");
    label.set_alignment (1.0f, 0.5f);
    table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
    duration = new Label (null);
    duration.set_alignment (0.0f, 0.5f);
    duration.set_selectable(true);
    table.attach (duration, 1, 2, row, row+1, fill_exp, 0, 3, 1);
    row++;

    if (compact_mode) {
      label = new Label (null);
      label.set_markup("<b>Streams</b>");
      label.set_alignment (0.0f, 0.5f);
      table.attach (label, 0, 3, row, row+1, fill_exp, 0, 0, 1);
      row++;

      all_streams = new Notebook ();
      all_streams.switch_page.connect (on_stream_switched);
      table.attach (all_streams, 0, 3, row, row+1, fill_exp, 0, 0, 1);
      row++;
    } else {
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
    }

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
    pb.set_state (State.READY);
    album_art = null;
    drawing_area.queue_draw();

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
        Notebook nb;
        uint row;
        AttachOptions fill = AttachOptions.FILL;
        AttachOptions fill_exp = AttachOptions.EXPAND|AttachOptions.FILL;
        string str, wikilink;
        Caps caps;
        Structure s;

        info = dc.discover_uri (uri);

        ClockTime dur = info.get_duration ();
        str = "%u:%02u:%02u.%09u".printf (
          (uint) (dur / (SECOND * 60 * 60)),
          (uint) ((dur / (SECOND * 60)) % 60),
          (uint) ((dur / SECOND) % 60),
          (uint) ((dur) % SECOND));
        this.duration.set_text (str);
        //stdout.printf ("Duration: %s\n", dur_str);

        /*
        < ensonic> bilboed-pi: is gst_discoverer_info_get_container_streams() containing the info for the conatiner or can those be multiple ones as well?
        < bilboed-pi> ensonic, if you have DV system-stream in MXF .... you'll have two container streams
        < bilboed-pi> (yes, they exist)
        < bilboed-pi> I'd recommend grabbing the top-level stream_info and walking your way down
        */
        /*
        l = info.get_container_streams ();
        for (int i = 0; i < l.length (); i++) {
          sinfo = l.nth_data (i);
          stdout.printf ("container[%d]: %s\n", i, sinfo.get_caps ().to_string ());
        }
        l = info.get_stream_list ();
        for (int i = 0; i < l.length (); i++) {
          sinfo = l.nth_data (i);
          stdout.printf ("stream[%d:%s]: %s\n", i, sinfo.get_stream_type_nick(), sinfo.get_caps ().to_string ());
        }
        */
        sinfo = info.get_stream_info ();
        caps = sinfo.get_caps ();
        wikilink = wikilinks[caps.get_structure(0).get_name()];
        str = pb_utils_get_codec_description (caps);
        if (wikilink != null) {
          // FIXME: make prefix and link translatable
          container_name.set_markup ("<a href=\"http://en.wikipedia.org/wiki/%s\">%s</a>".printf (wikilink, str));
        } else {
          container_name.set_text (str);
        }

			  // reset notebooks
        if (compact_mode) {
          while (all_streams.get_n_pages() > 0) {
            all_streams.remove_page (-1);
          }
				} else {
          while (video_streams.get_n_pages() > 0) {
            video_streams.remove_page (-1);
          }
          while (audio_streams.get_n_pages() > 0) {
            audio_streams.remove_page (-1);
          }
        }

        // get stream info
        nb = compact_mode ? all_streams : video_streams;
        l = info.get_video_streams ();
        num_video_streams = l.length ();
        have_video = (num_video_streams > 0);
        for (int i = 0; i < num_video_streams; i++) {
          sinfo = l.nth_data (i);
          caps = sinfo.get_caps ();

          row = 0;
          table = new Table (2, 8, false);

          label = new Label(caps.to_string ());
          label.set_ellipsize (Pango.EllipsizeMode.END);
          label.set_alignment (0.0f, 0.5f);
          label.set_selectable(true);
          table.attach (label, 0, 2, row, row+1, fill_exp, 0, 0, 1);
          row++;

          label = new Label ("Codec:");
          label.set_alignment (1.0f, 0.5f);
          table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
          wikilink = wikilinks[caps.get_structure(0).get_name()];
          str = pb_utils_get_codec_description (caps);
          if (wikilink != null) {
            // FIXME: make prefix and link translatable
            str="<a href=\"http://en.wikipedia.org/wiki/%s\">%s</a>".printf (wikilink, str);
          }
          label = new Label (str);
          label.set_alignment (0.0f, 0.5f);
          label.set_selectable(true);
          label.set_use_markup(true);
          table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
          row++;

          label = new Label ("Bitrate:");
          label.set_alignment (1.0f, 0.5f);
          table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
          str = "%u / %u bits/second".printf (((DiscovererVideoInfo)sinfo).get_bitrate(),((DiscovererVideoInfo)sinfo).get_max_bitrate());
          label = new Label (str);
          label.set_alignment (0.0f, 0.5f);
          label.set_selectable(true);
          table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
          row++;

          // add named resolutions: (640x480=VGA)
          label = new Label ("Resolution:");
          label.set_alignment (1.0f, 0.5f);
          table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
          string resolution = "%u x %u".printf (((DiscovererVideoInfo)sinfo).get_width(),((DiscovererVideoInfo)sinfo).get_height());
          string named_res = resolutions[resolution];
          if (named_res != null) {
            str = "%s (%s)".printf (named_res, resolution);
          } else {
            str = resolution;
          }
          label = new Label (str);
          label.set_alignment (0.0f, 0.5f);
          label.set_selectable(true);
          table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
          row++;

          label = new Label ("Framerate:");
          label.set_alignment (1.0f, 0.5f);
          table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
          double fps_num = (double)((DiscovererVideoInfo)sinfo).get_framerate_num();
          double fps_denom = (double)((DiscovererVideoInfo)sinfo).get_framerate_denom();
          str = "%.3lf frames/second".printf (fps_num/fps_denom);
          label = new Label (str);
          label.set_alignment (0.0f, 0.5f);
          label.set_selectable(true);
          table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
          row++;

          label = new Label ("PixelAspect:");
          label.set_alignment (1.0f, 0.5f);
          table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
          str = "%u : %u".printf (((DiscovererVideoInfo)sinfo).get_par_num(),((DiscovererVideoInfo)sinfo).get_par_denom());
          label = new Label (str);
          label.set_alignment (0.0f, 0.5f);
          label.set_selectable(true);
          table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
          row++;

          label = new Label ("Bitdepth:");
          label.set_alignment (1.0f, 0.5f);
          table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
          str = "%u bits/pixel".printf (((DiscovererVideoInfo)sinfo).get_depth());
          label = new Label (str);
          label.set_alignment (0.0f, 0.5f);
          label.set_selectable(true);
          table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
          row++;

          if ((s = sinfo.get_misc ()) != null) {
            label = new Label ("Details:");
            label.set_alignment (1.0f, 0.5f);
            table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
            label = new Label(s.to_string ());
            label.set_ellipsize (Pango.EllipsizeMode.END);
            label.set_alignment (0.0f, 0.5f);
            label.set_selectable(true);
            table.attach (label, 1, 2, row, row+1, fill_exp, 0, 0, 1);
            row++;
          }

          if ((s = (Structure)sinfo.get_tags ()) != null) {
            // FIXME: use treeview inside scrolled window
            label = new Label ("Tags:");
            label.set_alignment (1.0f, 0.0f);
            table.attach (label, 0, 1, row, row+1, fill, fill, 0, 0);
            str = build_taglist_info (s);
            label = new Label(str);
            label.set_ellipsize (Pango.EllipsizeMode.END);
            label.set_alignment (0.0f, 0.5f);
            label.set_selectable(true);
            label.set_use_markup(true);
            table.attach (label, 1, 2, row, row+1, fill_exp, 0, 0, 1);
            row++;
          }

          nb.append_page (table, new Label (@"video $i"));
        }
        nb.show_all();

        nb = compact_mode ? all_streams : audio_streams;
        l = info.get_audio_streams ();
        num_audio_streams = l.length ();
        for (int i = 0; i < num_audio_streams; i++) {
          sinfo = l.nth_data (i);
          caps = sinfo.get_caps ();

          row = 0;
          table = new Table (2, 7, false);

          label = new Label(caps.to_string ());
          label.set_ellipsize (Pango.EllipsizeMode.END);
          label.set_alignment (0.0f, 0.5f);
          label.set_selectable(true);
          table.attach (label, 0, 2, row, row+1, fill_exp, 0, 0, 1);
          row++;

          label = new Label ("Codec:");
          label.set_alignment (1.0f, 0.5f);
          table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
          wikilink = wikilinks[caps.get_structure(0).get_name()];
          str = pb_utils_get_codec_description (caps);
          if (wikilink != null) {
            // FIXME: make prefix and link translatable
            str="<a href=\"http://en.wikipedia.org/wiki/%s\">%s</a>".printf (wikilink, str);
          }
          label = new Label (str);
          label.set_alignment (0.0f, 0.5f);
          label.set_selectable(true);
          label.set_use_markup(true);
          table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
          row++;

          label = new Label ("Bitrate:");
          label.set_alignment (1.0f, 0.5f);
          table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
          str = "%u / %u bits/second".printf (((DiscovererAudioInfo)sinfo).get_bitrate(),((DiscovererAudioInfo)sinfo).get_max_bitrate());
          label = new Label (str);
          label.set_alignment (0.0f, 0.5f);
          label.set_selectable(true);
          table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
          row++;

          label = new Label ("Samplerate:");
          label.set_alignment (1.0f, 0.5f);
          table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
          str = "%u samples/second".printf (((DiscovererAudioInfo)sinfo).get_sample_rate());
          label = new Label (str);
          label.set_alignment (0.0f, 0.5f);
          label.set_selectable(true);
          table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
          row++;

          // TODO: check channel layouts, can we have some nice names here ?
          label = new Label ("Channels:");
          label.set_alignment (1.0f, 0.5f);
          table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
          str = "%u".printf (((DiscovererAudioInfo)sinfo).get_channels());
          label = new Label (str);
          label.set_alignment (0.0f, 0.5f);
          label.set_selectable(true);
          table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
          row++;

          label = new Label ("Bitdepth:");
          label.set_alignment (1.0f, 0.5f);
          table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
          str = "%u bits/sample".printf (((DiscovererAudioInfo)sinfo).get_depth());
          label = new Label (str);
          label.set_alignment (0.0f, 0.5f);
          label.set_selectable(true);
          table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
          row++;

          if ((s = sinfo.get_misc ()) != null) {
            label = new Label ("Details:");
            label.set_alignment (1.0f, 0.5f);
            table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
            label = new Label(s.to_string ());
            label.set_ellipsize (Pango.EllipsizeMode.END);
            label.set_alignment (0.0f, 0.5f);
            label.set_selectable(true);
            table.attach (label, 1, 2, row, row+1, fill_exp, 0, 0, 1);
            row++;
          }

          if ((s = (Structure)sinfo.get_tags ()) != null) {
            // FIXME: use treeview inside scrolled window
            label = new Label ("Tags:");
            label.set_alignment (1.0f, 0.0f);
            table.attach (label, 0, 1, row, row+1, fill, fill, 0, 0);
            str = build_taglist_info (s);
            label = new Label(str);
            label.set_ellipsize (Pango.EllipsizeMode.END);
            label.set_alignment (0.0f, 0.5f);
            label.set_selectable(true);
            label.set_use_markup(true);
            table.attach (label, 1, 2, row, row+1, fill_exp, 0, 0, 1);
            row++;
          }

          nb.append_page (table, new Label (@"audio $i"));
        }
        nb.show_all();

        //l = info.get_container_streams ();

      } catch (Error e) {
        debug ("Failed to extract metadata from %s: %s: %s", uri, e.domain.to_string (), e.message);
      }

      // play file
      cur_video_stream = cur_audio_stream = 0;
      ((GLib.Object)pb).set_property ("uri", uri);
      pb.set_state (State.PLAYING);

      res = true;
    }

    return (res);
  }

  // signal handlers

  private bool on_drawing_area_expose (Widget widget, Gdk.EventExpose event)
  {
    // redraw if not playing and if there is no video
    if (pb.current_state < State.PAUSED || !have_video) {
      Gtk.Allocation a;
      widget.get_allocation(out a);
      Cairo.Context cr = Gdk.cairo_create (widget.get_window());

      cr.set_source_rgb (0, 0, 0);
      cr.rectangle (0, 0, a.width, a.height);
      cr.fill ();
      if (album_art != null) {
        int sw=album_art.get_width();
        int sh=album_art.get_height();
        double sr = (double)sw / (double)sh;
        double dr = (double)a.width / (double)a.height;
        double wr = (double)sw / (double)a.width;
        double hr = (double)sh / (double)a.height;
        int x,y,w,h;

        // stdout.printf("s: %d x %d : %f -> d: %d x %d : %f\n",sw,sh,sr,a.width,a.height,dr);
        if (sr > dr) {
          w = a.width;
          h = (int)(w / sr);
          x = 0;
          y = (a.height - h) / 2;
        } else if (sr < dr) {
          h = a.height;
          w = (int)(h * sr);
          x = (a.width - w) / 2;
          y = 0;
        } else {
          w = a.width;
          h = a.height;
          x = 0;
          y = 0;
        }
        // stdout.printf("r: %d x %d\n",w,h);

        Gdk.Pixbuf pb = album_art.scale_simple(w,h,Gdk.InterpType.BILINEAR);
        Gdk.cairo_set_source_pixbuf(cr,pb,x,y);
        cr.rectangle (x, y, w, h);
        cr.fill ();
      }
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

  /* FIXME: discoverer not neccesarily return the stream in the same order as
   * playbin2 sees them: https://bugzilla.gnome.org/show_bug.cgi?id=634407
   */
  private void on_video_stream_switched (Notebook nb, NotebookPage page, uint page_num)
  {
    if (pb.current_state > State.PAUSED) {
      stdout.printf ("Switching video to: %u\n", page_num);
      ((GLib.Object)pb).set_property ("current-video", (int)page_num);
    }
  }

  private void on_audio_stream_switched (Notebook nb, NotebookPage page, uint page_num)
  {
    if (pb.current_state > State.PAUSED) {
      stdout.printf ("Switching audio to: %u\n", page_num);
      ((GLib.Object)pb).set_property ("current-audio", (int)page_num);
    }
  }

  private void on_stream_switched (Notebook nb, NotebookPage page, uint page_num)
  {
    if (pb.current_state > State.PAUSED) {
      if (page_num < num_video_streams) {
        stdout.printf ("Switching video to: %u\n", page_num);
        ((GLib.Object)pb).set_property ("current-video", (int)page_num);
      } else {
        page_num -= num_video_streams;
        stdout.printf ("Switching audio to: %u\n", page_num);
        ((GLib.Object)pb).set_property ("current-audio", (int)page_num);
      }
    }
  }

  private string build_taglist_info (Structure s)
  {
    uint i;
    string str, fn, vstr;
    Gst.Value v;

    str = "";
    for (i = 0; i < s.n_fields(); i++) {
      fn = s.nth_field_name (i);
      // skip a few tags
      if (tag_black_list.contains (fn))
        continue;
      if (fn.has_prefix("private-"))
        continue;

      if (str.length > 0)
        str += "\n";

      // TODO: decode images:
      /*
      GInputStream is = g_memory_input_stream_new_from_data(GST_BUFFER_DATA(buf),GST_BUFFER_SIZE(buf,NULL);
      GdkPixbuf pb = gdk_pixbuf_new_from_stream(is, NULL, NULL);
      g_input_stream_close(is,NULL,NULL);
      res = gdk_pixbuf_scale_simple(pb, w, h, GDK_INTERP_BILINEAR);
      */
      // - need to figure a way to return them
      // - where we show them -> in the drawing area
      v = s.get_value (fn);
      if (v.holds(typeof(Gst.Buffer))) {
        Gst.Buffer buf = v.get_buffer();
        Caps c = buf.get_caps();

        try {
          InputStream is = new MemoryInputStream.from_data (buf.data,buf.size,null);
          album_art = new Gdk.Pixbuf.from_stream (is, null);
          is.close();
        } catch (Error e) {
          debug ("Decoding album art failed: %s: %s", e.domain.to_string (), e.message);
        }

        // FIXME: having the actual resolution here would be nice
        vstr = c.to_string();
      } else  {
        vstr = v.serialize ().compress ();
        if (vstr.has_prefix("http://") || vstr.has_prefix("https://")) {
          vstr = "<a href=\"" + vstr + "\">" + vstr + "</a>";
        }
      }
      str += fn + " = " + vstr;
    }

    return str;
  }
}
