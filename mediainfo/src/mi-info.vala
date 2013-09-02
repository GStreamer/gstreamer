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
using Gst;
using Gst.PbUtils;
using Gee;

/*
in the case we need to update the vapi for yet unreleased gstreamer api, these
are the steps. Right now its enough to install the vapi file from git

# checkout vala from gnome git
cd vala/mediainfo/vapi
vala-gen-introspect gstreamer-pbutils-0.10 packages/gstreamer-pbutils-0.10
vapigen --vapidir . --library gstreamer-pbutils-0.10 packages/gstreamer-pbutils-0.10/gstreamer-pbutils-0.10.gi
git diff packages/gstreamer-pbutils-0.10/gstreamer-pbutils-0.10.metadata >vapi.gstreamer-pbutils-0.10.patch

# suse, meego
sudo cp gstreamer-pbutils-0.10.vapi /usr/share/vala/mediainfo/vapi/
# ubuntu
sudo cp gstreamer-pbutils-0.10.vapi /usr/share/vala-0.10/vapi/

# jhbuild
jhbuild build vala
jhbuild shell

*/

public class MediaInfo.Info : Box
{
  // layout
  private bool compact_mode = false;
  // ui components
  private Label container_caps;
  private Label container_name;
  private Label mime_type;
  private Label duration;
  private Image icon_image;
  private Notebook all_streams;    // there is either all or separate a/mediainfo/v/st
  private Notebook video_streams;  // depending on screen resolution
  private Notebook audio_streams;
  private Notebook subtitle_streams;
  private AspectFrame drawing_frame;
  private DrawingArea drawing_area;
  private ScrolledWindow info_area;
  // gstreamer objects
  private Discoverer dc;
  private Pipeline pb;
  private bool have_video = false;
  private uint num_video_streams;
  private uint num_audio_streams;
  private uint num_subtitle_streams;
  private float video_ratio = 1.0f;
  private ArrayList<Gdk.Point?> video_resolutions = null;
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
    set_orientation (Gtk.Orientation.VERTICAL);

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
    wikilinks["application/mxf"] = "Material_Exchange_Format";
    wikilinks["audio/ogg"] = "Ogg";
    wikilinks["application/vnd.rn-realmedia"] = "RealMedia";
    wikilinks["application/x-3gp"] = "3GP_and_3G2";
    wikilinks["application/x-annodex"] = "Ogg";
    wikilinks["application/x-id3"] = "ID3";
    wikilinks["application/x-pn-realaudio"] = "RealAudio";
    wikilinks["video/ogg"] = "Ogg";
    wikilinks["video/x-flv"] = "Flash_Video";
    wikilinks["video/x-matroska"] = "Matroska";
    wikilinks["video/webm"] = "WebM";
    wikilinks["video/x-ms-asf"] = "Advanced_Systems_Format";
    wikilinks["video/x-msvideo"] = "Audio_Video_Interleave";
    wikilinks["video/x-quicktime"] = "QuickTime_File_Format";
    wikilinks["video/quicktime"] = "QuickTime_File_Format";
    // audio codecs
    wikilinks["MPEG-1 Layer 3 (MP3)"] = "MP3";
    wikilinks["MPEG-4 AAC"] = "Advanced_Audio_Coding";
    wikilinks["audio/x-flac"] = "Flac";
    wikilinks["audio/x-vorbis"] = "Vorbis";
    wikilinks["audio/x-wav"] = "WAV";
    // video codecs
    wikilinks["video/x-divx"] = "MPEG-4_Part_2";
    wikilinks["video/x-h264"] = "H.264/MPEG-4_AVC";
    wikilinks["video/x-msmpeg"] = "MPEG-4_Part_2";
    wikilinks["video/x-svq"] = "Sorenson_codec";
    wikilinks["video/x-theora"] = "Theora";
    wikilinks["video/x-xvid"] = "Xvid";
    
    video_resolutions = new ArrayList<Gdk.Point?> ();

    int screen_height = Gdk.Screen.get_default().get_height();
    if (screen_height <= 600) {
      compact_mode = true;
    }

    // add widgets
    drawing_frame = new AspectFrame (null, 0.5f, 0.5f, 1.25f, false);
    drawing_frame.set_size_request (160, 128);
    drawing_frame.set_shadow_type (Gtk.ShadowType.NONE);

    pack_start (drawing_frame, true, true, 0);
    
    drawing_area = new DrawingArea ();
    drawing_area.set_size_request (160, 128);
    drawing_area.draw.connect (on_drawing_area_draw);
    drawing_area.size_allocate.connect (on_drawing_area_size_allocate);
    drawing_area.realize.connect (on_drawing_area_realize);
    drawing_area.unrealize.connect (on_drawing_area_unrealize);
    drawing_frame.add (drawing_area);

    info_area = new ScrolledWindow (null, null);
    info_area.set_policy (PolicyType.NEVER, PolicyType.ALWAYS);
    pack_start (info_area, true, true, 0);

    table = new Table (8, 3, false);
    info_area.add_with_viewport (table);

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

    container_caps = new Label (null);
    container_caps.set_alignment (0.0f, 0.5f);
    container_caps.set_selectable (true);
    container_caps.set_use_markup (true);
    table.attach (container_caps, 0, 2, row, row+1, fill_exp, 0, 3, 1);
    row++;

    label = new Label ("Format:");
    label.set_alignment (1.0f, 0.5f);
    table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
    container_name = new Label (null);
    container_name.set_alignment (0.0f, 0.5f);
    container_name.set_selectable (true);
    container_name.set_use_markup (true);
    table.attach (container_name, 1, 2, row, row+1, fill_exp, 0, 3, 1);
    row++;

    label = new Label ("Mime-Type:");
    label.set_alignment (1.0f, 0.5f);
    table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
    mime_type = new Label (null);
    mime_type.set_alignment (0.0f, 0.5f);
    mime_type.set_selectable (true);
    table.attach (mime_type, 1, 2, row, row+1, fill_exp, 0, 3, 1);
    row++;

    label = new Label ("Duration:");
    label.set_alignment (1.0f, 0.5f);
    table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
    duration = new Label (null);
    duration.set_alignment (0.0f, 0.5f);
    duration.set_selectable (true);
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

      label = new Label (null);
      label.set_markup("<b>Subtitle Streams</b>");
      label.set_alignment (0.0f, 0.5f);
      table.attach (label, 0, 3, row, row+1, fill_exp, 0, 0, 1);
      row++;

      subtitle_streams = new Notebook ();
      subtitle_streams.switch_page.connect (on_subtitle_stream_switched);
      table.attach (subtitle_streams, 0, 3, row, row+1, fill_exp, 0, 0, 1);
      row++;
    }

    // TODO: add container stream info widgets

    // TODO: add tag list widget

    // TODO: add message list widget

    show_all ();

    // set up the gstreamer components
    try {
      dc = new Discoverer ((ClockTime)(Gst.SECOND * 10));
      dc.discovered.connect (on_uri_discovered);
    } catch (Error e) {
      debug ("Failed to create the discoverer: %s: %s", e.domain.to_string (), e.message);
    }

    pb = ElementFactory.make ("playbin", "player") as Pipeline;
    Gst.Bus bus = pb.get_bus ();
    //bus.set_sync_handler ((Gst.BusSyncHandler)bus.sync_signal_handler);
    bus.enable_sync_message_emission();
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

    if (uri != null) {
      File file = File.new_for_uri(uri);

      // stop previous playback
      pb.set_state (State.READY);
      album_art = null;
      drawing_area.queue_draw();

      try {
        FileInfo finfo = file.query_info ("standard::*", FileQueryInfoFlags.NONE, null);
        mime_type.set_text (finfo.get_attribute_string (FileAttribute.STANDARD_CONTENT_TYPE));
        icon_image.set_from_gicon ((Icon) finfo.get_attribute_object (FileAttribute.STANDARD_ICON), IconSize.DIALOG);
      } catch (Error e) {
        debug ("Failed to query file info from %s: %s: %s", uri, e.domain.to_string (), e.message);
      }

      if (false) {
        /* sync API */
        try {
          process_new_uri (dc.discover_uri (uri));
        } catch (Error e) {
          debug ("Failed to extract metadata from %s: %s: %s", uri, e.domain.to_string (), e.message);
        }
      } else {
        /* async API */
        dc.stop();
        dc.start();
        dc.discover_uri_async (uri);
      }
    }
    return (res);
  }
  
  private void on_uri_discovered (DiscovererInfo info, Error e)
  {
    if (e != null) {
      debug ("Failed to extract metadata from %s: %s: %s", info.get_uri(), e.domain.to_string (), e.message);
      container_caps.set_text ("");
      container_name.set_text ("");
      duration.set_text ("");
    }
    process_new_uri (info);
  }
  
  private void set_wikilink(Label label, Caps caps)
  {
    string str = get_codec_description (caps);
    string wikilink = wikilinks[str];

    if (wikilink == null) {
      wikilink = wikilinks[caps.get_structure(0).get_name()];
    }
    if (wikilink != null) {
      // FIXME: make prefix and link translatable
      label.set_markup ("<a href=\"http://en.wikipedia.org/wiki/%s\">%s</a>".printf (wikilink, str));
    } else {
      label.set_text (str);
    }
  }

  private void process_new_uri (DiscovererInfo info)
  {
    string uri = info.get_uri();
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
    string str;
    Caps caps;
    unowned Structure s;
    unowned TagList t;

    if (info == null) {
      container_caps.set_text ("");
      container_name.set_text ("");
      duration.set_text ("");
      return;
    }

    ClockTime dur = info.get_duration ();
    str = "%u:%02u:%02u.%09u".printf (
      (uint) (dur / (SECOND * 60 * 60)),
      (uint) ((dur / (SECOND * 60)) % 60),
      (uint) ((dur / SECOND) % 60),
      (uint) ((dur) % SECOND));
    duration.set_text (str);
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
    if (sinfo != null) {
      caps = sinfo.get_caps ();
      container_caps.set_text (caps.to_string ());
      set_wikilink (container_name, caps); 
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
    video_resolutions.clear();
    for (int i = 0; i < num_video_streams; i++) {
      sinfo = l.nth_data (i);
      caps = sinfo.get_caps ();
      
      Gdk.Point res = {
        (int)((DiscovererVideoInfo)sinfo).get_width(),
        (int)((DiscovererVideoInfo)sinfo).get_height()
      };
      video_resolutions.add(res);

      row = 0;
      table = new Table (2, 8, false);

      label = new Label (caps.to_string ());
      label.set_ellipsize (Pango.EllipsizeMode.END);
      label.set_alignment (0.0f, 0.5f);
      label.set_selectable (true);
      table.attach (label, 0, 2, row, row+1, fill_exp, 0, 0, 1);
      row++;

      label = new Label ("Codec:");
      label.set_alignment (1.0f, 0.5f);
      table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
      label = new Label (null);
      label.set_alignment (0.0f, 0.5f);
      label.set_selectable (true);
      label.set_use_markup (true);
      set_wikilink (label, caps); 
      table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
      row++;

      label = new Label ("Bitrate:");
      label.set_alignment (1.0f, 0.5f);
      table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
      str = "%u / %u bits/second".printf (((DiscovererVideoInfo)sinfo).get_bitrate(),((DiscovererVideoInfo)sinfo).get_max_bitrate());
      label = new Label (str);
      label.set_alignment (0.0f, 0.5f);
      label.set_selectable (true);
      table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
      row++;

      // add named resolutions: (640x480=VGA)
      label = new Label ("Resolution:");
      label.set_alignment (1.0f, 0.5f);
      table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
      string resolution = "%u x %u".printf (res.x, res.y);
      string named_res = resolutions[resolution];
      if (named_res != null) {
        str = "%s (%s)".printf (named_res, resolution);
      } else {
        str = resolution;
      }
      label = new Label (str);
      label.set_alignment (0.0f, 0.5f);
      label.set_selectable (true);
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
      label.set_selectable (true);
      table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
      row++;

      label = new Label ("PixelAspect:");
      label.set_alignment (1.0f, 0.5f);
      table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
      str = "%u : %u".printf (((DiscovererVideoInfo)sinfo).get_par_num(),((DiscovererVideoInfo)sinfo).get_par_denom());
      label = new Label (str);
      label.set_alignment (0.0f, 0.5f);
      label.set_selectable (true);
      table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
      row++;

      label = new Label ("Bitdepth:");
      label.set_alignment (1.0f, 0.5f);
      table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
      str = "%u bits/pixel".printf (((DiscovererVideoInfo)sinfo).get_depth());
      label = new Label (str);
      label.set_alignment (0.0f, 0.5f);
      label.set_selectable (true);
      table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
      row++;

      if ((s = sinfo.get_misc ()) != null) {
        label = new Label ("Details:");
        label.set_alignment (1.0f, 0.5f);
        table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
        label = new Label (s.to_string ());
        label.set_ellipsize (Pango.EllipsizeMode.END);
        label.set_alignment (0.0f, 0.5f);
        label.set_selectable (true);
        table.attach (label, 1, 2, row, row+1, fill_exp, 0, 0, 1);
        row++;
      }

      if ((t = sinfo.get_tags ()) != null) {
        // FIXME: use treeview inside scrolled window
        label = new Label ("Tags:");
        label.set_alignment (1.0f, 0.0f);
        table.attach (label, 0, 1, row, row+1, fill, fill, 0, 0);
        str = build_taglist_info (t);
        label = new Label (str);
        label.set_ellipsize (Pango.EllipsizeMode.END);
        label.set_alignment (0.0f, 0.5f);
        label.set_selectable (true);
        label.set_use_markup (true);
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

      label = new Label (caps.to_string ());
      label.set_ellipsize (Pango.EllipsizeMode.END);
      label.set_alignment (0.0f, 0.5f);
      label.set_selectable (true);
      table.attach (label, 0, 2, row, row+1, fill_exp, 0, 0, 1);
      row++;

      label = new Label ("Codec:");
      label.set_alignment (1.0f, 0.5f);
      table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
      label = new Label (null);
      label.set_alignment (0.0f, 0.5f);
      label.set_selectable (true);
      label.set_use_markup (true);
      set_wikilink (label, caps); 
      table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
      row++;

      label = new Label ("Bitrate:");
      label.set_alignment (1.0f, 0.5f);
      table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
      str = "%u / %u bits/second".printf (((DiscovererAudioInfo)sinfo).get_bitrate(),((DiscovererAudioInfo)sinfo).get_max_bitrate());
      label = new Label (str);
      label.set_alignment (0.0f, 0.5f);
      label.set_selectable (true);
      table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
      row++;

      label = new Label ("Samplerate:");
      label.set_alignment (1.0f, 0.5f);
      table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
      str = "%u samples/second".printf (((DiscovererAudioInfo)sinfo).get_sample_rate());
      label = new Label (str);
      label.set_alignment (0.0f, 0.5f);
      label.set_selectable (true);
      table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
      row++;

      // TODO: check channel layouts, can we have some nice names here ?
      label = new Label ("Channels:");
      label.set_alignment (1.0f, 0.5f);
      table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
      str = "%u".printf (((DiscovererAudioInfo)sinfo).get_channels());
      label = new Label (str);
      label.set_alignment (0.0f, 0.5f);
      label.set_selectable (true);
      table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
      row++;

      label = new Label ("Bitdepth:");
      label.set_alignment (1.0f, 0.5f);
      table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
      str = "%u bits/sample".printf (((DiscovererAudioInfo)sinfo).get_depth());
      label = new Label (str);
      label.set_alignment (0.0f, 0.5f);
      label.set_selectable (true);
      table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
      row++;

      if ((s = sinfo.get_misc ()) != null) {
        label = new Label ("Details:");
        label.set_alignment (1.0f, 0.5f);
        table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
        label = new Label (s.to_string ());
        label.set_ellipsize (Pango.EllipsizeMode.END);
        label.set_alignment (0.0f, 0.5f);
        label.set_selectable (true);
        table.attach (label, 1, 2, row, row+1, fill_exp, 0, 0, 1);
        row++;
      }

      if ((t = sinfo.get_tags ()) != null) {
        // FIXME: use treeview inside scrolled window
        label = new Label ("Tags:");
        label.set_alignment (1.0f, 0.0f);
        table.attach (label, 0, 1, row, row+1, fill, fill, 0, 0);
        str = build_taglist_info (t);
        label = new Label (str);
        label.set_ellipsize (Pango.EllipsizeMode.END);
        label.set_alignment (0.0f, 0.5f);
        label.set_selectable (true);
        label.set_use_markup (true);
        table.attach (label, 1, 2, row, row+1, fill_exp, 0, 0, 1);
        row++;
      }

      nb.append_page (table, new Label (@"audio $i"));
    }
    nb.show_all();
    
    nb = compact_mode ? all_streams : subtitle_streams;
    l = info.get_subtitle_streams ();
    num_subtitle_streams = l.length ();
    for (int i = 0; i < num_subtitle_streams; i++) {
      sinfo = l.nth_data (i);
      caps = sinfo.get_caps ();

      row = 0;
      table = new Table (2, 7, false);

      label = new Label (caps.to_string ());
      label.set_ellipsize (Pango.EllipsizeMode.END);
      label.set_alignment (0.0f, 0.5f);
      label.set_selectable (true);
      table.attach (label, 0, 2, row, row+1, fill_exp, 0, 0, 1);
      row++;

      label = new Label ("Codec:");
      label.set_alignment (1.0f, 0.5f);
      table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
      label = new Label (null);
      label.set_alignment (0.0f, 0.5f);
      label.set_selectable (true);
      label.set_use_markup (true);
      set_wikilink (label, caps); 
      table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
      row++;

      if ((s = sinfo.get_misc ()) != null) {
        label = new Label ("Details:");
        label.set_alignment (1.0f, 0.5f);
        table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
        label = new Label (s.to_string ());
        label.set_ellipsize (Pango.EllipsizeMode.END);
        label.set_alignment (0.0f, 0.5f);
        label.set_selectable (true);
        table.attach (label, 1, 2, row, row+1, fill_exp, 0, 0, 1);
        row++;
      }

      if ((t = sinfo.get_tags ()) != null) {
        // FIXME: use treeview inside scrolled window
        label = new Label ("Tags:");
        label.set_alignment (1.0f, 0.0f);
        table.attach (label, 0, 1, row, row+1, fill, fill, 0, 0);
        str = build_taglist_info (t);
        label = new Label (str);
        label.set_ellipsize (Pango.EllipsizeMode.END);
        label.set_alignment (0.0f, 0.5f);
        label.set_selectable (true);
        label.set_use_markup (true);
        table.attach (label, 1, 2, row, row+1, fill_exp, 0, 0, 1);
        row++;
      }

      nb.append_page (table, new Label (@"subtitle $i"));
    }
    nb.show_all();
    
    if (have_video) {
      Gdk.Point res = video_resolutions[0];
      video_ratio = (float)(res.x) / (float)(res.y);
      stdout.printf("video_ratio from video: %f\n", video_ratio);
    } else if (album_art != null) {
      int sw=album_art.get_width ();
      int sh=album_art.get_height ();
      video_ratio = (float)sw / (float)sh;
      stdout.printf("video_ratio from album art: %f\n", video_ratio);
    } else {
      video_ratio = 1.0f;
      stdout.printf("video_ratio --- : %f\n", video_ratio);
    }
    drawing_frame.set (0.5f, 0.5f, video_ratio, false);
    drawing_area.queue_draw();

    //l = info.get_container_streams ();

    // play file
    ((GLib.Object)pb).set_property ("uri", uri);
    pb.set_state (State.PLAYING);
  }

  // signal handlers
  
  private void on_drawing_area_size_allocate (Widget widget, Gtk.Allocation frame)
  {
    Gtk.Allocation alloc;
    get_allocation (out alloc);
    stdout.printf("size_allocate: %d x %d\n", alloc.width, alloc.height);

    Gtk.Requisition requisition;
    info_area.get_child ().get_preferred_size (null, out requisition);
    stdout.printf("info_area: %d x %d\n", requisition.width, requisition.height);
    stdout.printf("video_area: %d x %d\n", frame.width, frame.height);
    
    int max_h = alloc.height - frame.height;
    info_area.set_min_content_height (int.min (requisition.height, max_h));
  }

  private bool on_drawing_area_draw (Widget widget, Cairo.Context cr)
  {
    // redraw if not playing and if there is no video
    if (pb.current_state < State.PAUSED || !have_video) {
      widget.set_double_buffered (true);

      Gtk.Allocation frame;      
      widget.get_allocation (out frame);
      int w = frame.width;
      int h = frame.height;
      //stdout.printf("on_drawing_area_draw:video_ratio: %d x %d : %f\n", w, h, video_ratio);

      if (album_art != null) {
        Gdk.Pixbuf pb = album_art.scale_simple (w, h, Gdk.InterpType.BILINEAR);
        Gdk.cairo_set_source_pixbuf (cr, pb, 0, 0);
      } else {
        cr.set_source_rgb (0, 0, 0);
      }
      cr.rectangle (0, 0, w, h);
      cr.fill ();
    } else {      
      widget.set_double_buffered (false);
    }
    return false;
  }
  
  private void on_drawing_area_realize (Widget widget)
  {
    widget.get_window ().ensure_native ();
    widget.set_double_buffered (false);
  }

  private void on_drawing_area_unrealize (Widget widget)
  {
    pb.set_state (State.NULL);
  }

  private void on_element_sync_message (Gst.Bus bus, Message message)
  {
    if (Gst.Video.is_video_overlay_prepare_window_handle_message (message)) {
      Gst.Video.Overlay overlay = message.src as Gst.Video.Overlay;
      overlay.set_window_handle ((uint *)Gdk.X11Window.get_xid (drawing_area.get_window()));
      drawing_frame.set (0.5f, 0.5f, video_ratio, false);
      stdout.printf("on_element_sync_message:video_ratio: %f\n", video_ratio);

      /* playbin does this in 1.0
      if (message.src.get_class ().find_property ("force-aspect-ratio") != null) {
        ((GLib.Object)message.src).set_property ("force-aspect-ratio", true);
      }
      */
    }
  }

  /* FIXME: discoverer not neccesarily return the stream in the same order as
   * playbin2 sees them: https://bugzilla.gnome.org/show_bug.cgi?id=634407
   */
  private void on_video_stream_switched (Notebook nb, Widget page, uint page_num)
  {
    if (pb.current_state > State.PAUSED) {
      stdout.printf ("Switching video to: %u\n", page_num);
      ((GLib.Object)pb).set_property ("current-video", (int)page_num);
      Gdk.Point res = video_resolutions[(int)page_num];
      video_ratio = (float)(res.x) / (float)(res.y);
      drawing_frame.set (0.5f, 0.5f, video_ratio, false);
      stdout.printf("on_video_stream_switched:video_ratio: %f\n", video_ratio);
    }
  }

  private void on_audio_stream_switched (Notebook nb, Widget page, uint page_num)
  {
    if (pb.current_state > State.PAUSED) {
      stdout.printf ("Switching audio to: %u\n", page_num);
      ((GLib.Object)pb).set_property ("current-audio", (int)page_num);
    }
  }

  private void on_subtitle_stream_switched (Notebook nb, Widget page, uint page_num)
  {
    if (pb.current_state > State.PAUSED) {
      stdout.printf ("Switching subtitle to: %u\n", page_num);
      ((GLib.Object)pb).set_property ("current-text", (int)page_num);
    }
  }

  private void on_stream_switched (Notebook nb, Widget page, uint page_num)
  {
    if (pb.current_state > State.PAUSED) {
      if (page_num < num_video_streams) {
        stdout.printf ("Switching video to: %u\n", page_num);
        ((GLib.Object)pb).set_property ("current-video", (int)page_num);
        return;
      }
      page_num -= num_video_streams;
      if (page_num < num_audio_streams) {
        stdout.printf ("Switching audio to: %u\n", page_num);
        ((GLib.Object)pb).set_property ("current-audio", (int)page_num);
        return;
      }
      page_num -= num_audio_streams;
      if (page_num < num_subtitle_streams) {
        stdout.printf ("Switching subtitle to: %u\n", page_num);
        ((GLib.Object)pb).set_property ("current-text", (int)page_num);
        return;
      }
    }
  }

  private string build_taglist_info (TagList t)
  {
    uint i;
    string str, fn, vstr;
    GLib.Value v;

    str = "";
    for (i = 0; i < t.n_tags(); i++) {
      fn = t.nth_tag_name (i);
      // skip a few tags
      if (tag_black_list.contains (fn))
        continue;
      if (fn.has_prefix("private-"))
        continue;

      if (str.length > 0)
        str += "\n";

      // decode images, we show them in the drawing area
      v = t.get_value_index (fn, 0);
      if (v.holds(typeof(Gst.Sample))) {
        Gst.Sample sample = (Gst.Sample)v.get_boxed();
        Gst.Buffer buf = sample.get_buffer();
        Caps c = sample.get_caps();
        Gst.MapInfo info;
        buf.map(out info, Gst.MapFlags.READ);

        try {
          InputStream is = new MemoryInputStream.from_data (info.data,null);
          album_art = new Gdk.Pixbuf.from_stream (is, null);
          debug("found album art");
          is.close(null);
        } catch (Error e) {
          debug ("Decoding album art failed: %s: %s", e.domain.to_string (), e.message);
        }
        buf.unmap(info);

        // FIXME: having the actual resolution here would be nice
        vstr = c.to_string();
      } else  {
        vstr = Gst.Value.serialize (v).compress ();
        if (vstr.has_prefix("http://") || vstr.has_prefix("https://")) {
          vstr = "<a href=\"" + vstr + "\">" + vstr + "</a>";
        }
      }
      str += fn + " = " + vstr;
    }

    return str;
  }
}
