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

using Gtk;
using Gst;
using Gst.PbUtils;
using Gee;

public class MediaInfo.Info : Box
{
  // layout
  private bool compact_mode = false;
  // ui components
  private Label mime_type;
  private Label duration;
  private Image icon_image;
  private Notebook container_streams;
  private Notebook all_streams;    // there is either all or separate a/mediainfo/v/st
  private Notebook video_streams;  // depending on screen resolution
  private Notebook audio_streams;
  private Notebook subtitle_streams;
  private TreeView toc_entries;
  private Preview preview;
  private ScrolledWindow info_area;
  // gstreamer objects
  private Discoverer dc;
  private Pipeline pb;
  private Video.Overlay overlay;
  private bool have_video = false;
  private uint num_video_streams;
  private uint num_audio_streams;
  private uint num_subtitle_streams;
  private ArrayList<Gdk.Point?> video_resolutions = null;
  // stream data
  private Gdk.Pixbuf album_art = null;

  private HashMap<string, string> resolutions;
  private HashSet<string> tag_black_list;
  private HashMap<string, string> wikilinks;

  public Info () {
    Label label;
    Table table;
    AttachOptions fill = AttachOptions.FILL;
    AttachOptions fill_exp = AttachOptions.EXPAND|AttachOptions.FILL;
    uint row = 0;

    // configure the view
    set_border_width (0);
    set_orientation (Gtk.Orientation.VERTICAL);

    // setup lookup tables
    // TODO(ensonic); move to a data class
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
    tag_black_list.add ("language-code");
    tag_black_list.add ("nominal-bitrate");
    tag_black_list.add ("maximum-bitrate");

    // map from media-type/codec-desc to wikipedia-articles, see set_wikilink()
    // where they are prefixed with 'http://en.wikipedia.org/wiki/'
    // alternative source could be http://codecdictionary.com/
    wikilinks = new HashMap<string, string> ();
    // container/tag formats
    wikilinks["application/mxf"] = "Material_Exchange_Format";
    wikilinks["audio/x-aiff"] = "Audio_Interchange_File_Format";
    wikilinks["application/x-apetag"] = "APE_tag";
    wikilinks["audio/ogg"] = "Ogg";
    wikilinks["application/vnd.rn-realmedia"] = "RealMedia";
    wikilinks["application/x-3gp"] = "3GP_and_3G2";
    wikilinks["application/x-annodex"] = "Ogg";
    wikilinks["application/x-id3"] = "ID3";
    wikilinks["application/x-pn-realaudio"] = "RealAudio";
    wikilinks["video/x-flv"] = "Flash_Video";
    wikilinks["video/x-matroska"] = "Matroska";
    wikilinks["video/mpeg"] = "MPEG-1#Part_1:_Systems";
    wikilinks["video/mpegts"] = "MPEG_transport_stream";
    wikilinks["video/ogg"] = "Ogg";
    wikilinks["video/webm"] = "WebM";
    wikilinks["video/x-ms-asf"] = "Advanced_Systems_Format";
    wikilinks["video/x-msvideo"] = "Audio_Video_Interleave";
    wikilinks["video/x-quicktime"] = "QuickTime_File_Format";
    wikilinks["video/quicktime"] = "QuickTime_File_Format";
    // audio codecs
    wikilinks["MPEG-1 Layer 2 (MP2)"] = "MPEG-1_Audio_Layer_II";
    wikilinks["MPEG-1 Layer 3 (MP3)"] = "MP3";
    wikilinks["MPEG-4 AAC"] = "Advanced_Audio_Coding";
    wikilinks["Windows Media Audio 8"] = "Windows_Media_Audio#Windows_Media_Audio";
    wikilinks["audio/x-ac3"] = "Dolby_AC-3";
    wikilinks["audio/x-flac"] = "Flac";
    wikilinks["audio/x-opus"] = "Opus_codec";
    wikilinks["audio/x-qdm"] = "QDesign";
    wikilinks["audio/x-vorbis"] = "Vorbis";
    wikilinks["audio/x-wav"] = "WAV";
    wikilinks["audio/x-wavpack"] = "Wavpack";
    // video codecs
    wikilinks["MPEG-1 Video"] = "MPEG-1#Part_2:_Video";
    wikilinks["MPEG-4 Video"] = "MPEG4";
    wikilinks["Windows Media Video 9 Screen"] = "Windows_Media_Video#Windows_Media_Video_Screen";
    wikilinks["image/gif"] = "GIF";
    wikilinks["image/jpeg"] = "JPEG";
    wikilinks["image/png"] = "Portable_Network_Graphics";
    wikilinks["video/x-divx"] = "MPEG-4_Part_2";
    wikilinks["video/x-flash-video"] = "Sorenson_codec#Sorenson_Spark_.28FLV1.29";
    wikilinks["video/x-h264"] = "H.264/MPEG-4_AVC";
    wikilinks["video/x-msmpeg"] = "MPEG-4_Part_2";
    wikilinks["video/x-svq"] = "Sorenson_codec#Sorenson_Video_.28SVQ1.2FSVQ3.29";
    wikilinks["video/x-theora"] = "Theora";
    wikilinks["video/x-vp8"] = "VP8";
    wikilinks["video/x-xvid"] = "Xvid";

    video_resolutions = new ArrayList<Gdk.Point?> ();

    int screen_height = Gdk.Screen.get_default().get_height();
    if (screen_height <= 600) {
      compact_mode = true;
    }

    // add widgets
    preview = new Preview ();
    preview.add_events (Gdk.EventMask.STRUCTURE_MASK);
    preview.configure_event.connect (on_preview_configured);
    pack_start (preview, false, false, 0);

    info_area = new ScrolledWindow (null, null);
    info_area.set_policy (PolicyType.NEVER, PolicyType.ALWAYS);
    pack_start (info_area, true, true, 0);

    table = new Table (8, 3, false);
    info_area.add_with_viewport (table);
    
    /* TODO(ensonic): add a 'Source' box ? maybe only for streams?
    Transport: {file, http, rtsp, ....} as wikilink
    Size: (in bytes)
    */

    label = new Label (null);
    label.set_markup("<b>Container</b>");
    label.set_alignment (0.0f, 0.5f);
    table.attach (label, 0, 2, row, row+1, fill_exp, 0, 0, 1);

    icon_image = new Image ();
    table.attach (icon_image, 2, 3, row, row+3, fill, 0, 0, 0);
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

    container_streams = new Notebook ();
    table.attach (container_streams, 0, 3, row, row+1, fill_exp, 0, 0, 1);
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

    label = new Label (null);
    label.set_markup("<b>Toc</b>");
    label.set_alignment (0.0f, 0.5f);
    table.attach (label, 0, 3, row, row+1, fill_exp, 0, 0, 1);
    row++;

    // TODO(ensonic): use tabs for editions?
    toc_entries = new TreeView ();
    toc_entries.set_enable_search (false);
    toc_entries.set_headers_visible (false);
    toc_entries.get_selection ().set_mode (SelectionMode.BROWSE);
    toc_entries.cursor_changed.connect (on_toc_entry_changed);

    TreeViewColumn column = new TreeViewColumn ();
    toc_entries.append_column (column);
    CellRendererText renderer = new CellRendererText ();
    column.pack_start (renderer, false);
    column.add_attribute (renderer, "text", 0);

    table.attach (toc_entries, 0, 3, row, row+1, fill_exp, 0, 0, 1);
    row++;
    
    // TODO: add message list widget

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
    bus.add_signal_watch();
    bus.message["elemnt"].connect (on_element_message);
  }

  ~Info () {
    // stop previous playback
    pb.set_state (State.NULL);
  }

  // public methods

  public bool discover (string uri) {
    bool res = true;

    if (uri != null) {
      File file = File.new_for_uri(uri);

      // stop previous playback
      pb.set_state (State.READY);
      album_art = null;

      try {
        // TODO(ensonic): this does not work for streams
        FileInfo finfo = file.query_info ("standard::*", FileQueryInfoFlags.NONE, null);
        mime_type.set_text (finfo.get_attribute_string (FileAttribute.STANDARD_CONTENT_TYPE));
        icon_image.set_from_gicon ((Icon) finfo.get_attribute_object (FileAttribute.STANDARD_ICON), IconSize.DIALOG);
      } catch (Error e) {
        debug ("Failed to query file info from %s: %s: %s", uri, e.domain.to_string (), e.message);
      }

      debug ("Discovering '%s'", uri);
      if (true) {
        /* sync API */
        try {
          process_new_uri (dc.discover_uri (uri));
        } catch (Error e) {
          // we're failing here when there are missing container plugins
          debug ("Failed to extract metadata from %s: %s: %s", uri, e.domain.to_string (), e.message);
        }
      } else {
        // TODO(ensonic): this breaks when discovering 'too quickly'
        /* async API */
        dc.stop();
        dc.start();
        dc.discover_uri_async (uri);
      }
    }
    return (res);
  }
  
  private void on_uri_discovered (DiscovererInfo info, Error e) {
    if (e != null) {
      debug ("Failed to extract metadata from %s: %s: %s", info.get_uri(), e.domain.to_string (), e.message);
      process_new_uri (null);
    } else {
      process_new_uri (info);
    }
  }
  
  private void process_new_uri (DiscovererInfo? info) {
    GLib.List<DiscovererStreamInfo> l;
    DiscovererStreamInfo sinfo;
    Notebook nb;
    unowned Toc toc = null;
    // sort streams
    ArrayList<string> sids = new ArrayList<string> ();
    int six;
    int page_offset = 0;

	  // reset notebooks
	  clear_notebook (container_streams);
    if (compact_mode) {
      clear_notebook (all_streams);
    } else {
      clear_notebook (video_streams);
      clear_notebook (audio_streams);
      clear_notebook (subtitle_streams);
    }

    if (info == null) {
      toc_entries.set_model (null);
      duration.set_text ("");
      return;
    }
    
    // prepare file from preview
    ((GLib.Object)pb).set_property ("uri", info.get_uri());
    pb.set_state (State.PAUSED);

    // update info view
    duration.set_text (format_time(info.get_duration ()));

    /*
    < ensonic> bilboed-pi: is gst_discoverer_info_get_container_streams() containing the info for the conatiner or can those be multiple ones as well?
    < bilboed-pi> ensonic, if you have DV system-stream in MXF .... you'll have two container streams
    < bilboed-pi> (yes, they exist)
    < bilboed-pi> I'd recommend grabbing the top-level stream_info and walking your way down
    */
    // do container streams
    nb = container_streams;
    sinfo = info.get_stream_info ();
    toc = sinfo.get_toc();
    nb.append_page (describe_container_stream (sinfo), new Label (@"container 0"));
    six = 1;
    //l = info.get_stream_list ();
    // FIXME: this is always null?
    l = info.get_container_streams ();
    for (int i = 0; i < l.length (); i++) {
      sinfo = l.nth_data (i);

      // need to skip audio/video/subtitle streams
      string nick =  sinfo.get_stream_type_nick();
      debug("container[%d]=%s : %s", i, nick,sinfo.get_stream_id());
      if ((nick != "container") && (nick != "unknown")) {
        continue;
      }
    
      if (toc == null) {
        toc = sinfo.get_toc();
      }
      
      nb.append_page (describe_container_stream (sinfo), new Label (@"container $six"));
      six++;
    }
    nb.show_all();

    // do video streams
    nb = compact_mode ? all_streams : video_streams;
    l = info.get_video_streams ();
    num_video_streams = l.length ();
    have_video = (num_video_streams > 0);
    video_resolutions.clear();
    sids.clear();
    for (int i = 0; i < num_video_streams; i++) {
      sinfo = l.nth_data (i);
      debug("video[%d]=%s", i, sinfo.get_stream_id());
      
      if (toc == null) {
        toc = sinfo.get_toc();
      }

      six = get_stream_index (sinfo, sids);
      nb.insert_page (describe_video_stream (sinfo), new Label (@"video $i"), page_offset + six);
    }
    if (compact_mode) {
      page_offset += (int)num_video_streams;
    } else {
      nb.show_all();
    }

    // do audio streams
    nb = compact_mode ? all_streams : audio_streams;
    l = info.get_audio_streams ();
    num_audio_streams = l.length ();
    sids.clear();
    for (int i = 0; i < num_audio_streams; i++) {
      sinfo = l.nth_data (i);
      debug("audio[%d]=%s", i, sinfo.get_stream_id());

      if (toc == null) {
        toc = sinfo.get_toc();
      }

      six = get_stream_index (sinfo, sids);
      nb.insert_page (describe_audio_stream (sinfo), new Label (@"audio $i"), page_offset + six);
    }
    if (compact_mode) {
      page_offset += (int)num_audio_streams;
    } else {
      nb.show_all();
    }

    // do subtitle streams
    nb = compact_mode ? all_streams : subtitle_streams;
    l = info.get_subtitle_streams ();
    num_subtitle_streams = l.length ();
    sids.clear();
    for (int i = 0; i < num_subtitle_streams; i++) {
      sinfo = l.nth_data (i);

      if (toc == null) {
        toc = sinfo.get_toc();
      }

      six = get_stream_index (sinfo, sids);
      nb.insert_page (describe_subtitle_stream (sinfo), new Label (@"subtitle $i"), page_offset + six);
    }
    if (compact_mode) {
      page_offset += (int)num_subtitle_streams;
    }
    nb.show_all();

    toc_entries.set_model (build_toc_info (toc));
    toc_entries.expand_all ();

    // TODO(ensonic): ideally do async wait for PAUSED
    if (have_video) {
      Gdk.Point res = video_resolutions[0];
      preview.set_content_size(res.x, res.y);
      preview.set_double_buffered (false);
    } else if (album_art != null) {
      preview.set_static_content(album_art);
      preview.set_double_buffered (true);
    } else {
      preview.reset();
      preview.set_double_buffered (true);
    }

    // play file
    pb.set_state (State.PLAYING);
  }

  // signal handlers
  
  private bool on_preview_configured (Gdk.EventConfigure event) {
    if (overlay != null)
      overlay.expose();
    return false;
  }  

  private void on_element_sync_message (Gst.Bus bus, Message message) {
    if (Video.is_video_overlay_prepare_window_handle_message (message)) {
      Gdk.Window window = preview.get_window ();
      debug ("prepare overlay: %p", window);
      overlay = message.src as Gst.Video.Overlay;
      overlay.set_window_handle ((uint *)Gdk.X11Window.get_xid (window));
      debug ("prepared overlay");
    }
  }

  private void on_element_message (Gst.Bus bus, Message message) {
    if (PbUtils.is_missing_plugin_message (message)) {
      string details = PbUtils.missing_plugin_message_get_description(message);
      debug ("Missing plugin: %s", details);
      // TODO(ensonic): use this in addition to e.. container/codec names
    }
  }

  private void on_video_stream_switched (Notebook nb, Widget page, uint page_num) {
    if (pb.current_state > State.PAUSED) {
      debug ("Switching video to: %u", page_num);
      ((GLib.Object)pb).set_property ("current-video", (int)page_num);
      Gdk.Point res = video_resolutions[(int)page_num];
      preview.set_content_size(res.x, res.y);
    }
  }

  private void on_audio_stream_switched (Notebook nb, Widget page, uint page_num) {
    if (pb.current_state > State.PAUSED) {
      debug ("Switching audio to: %u", page_num);
      ((GLib.Object)pb).set_property ("current-audio", (int)page_num);
    }
  }

  private void on_subtitle_stream_switched (Notebook nb, Widget page, uint page_num) {
    if (pb.current_state > State.PAUSED) {
      debug ("Switching subtitle to: %u", page_num);
      ((GLib.Object)pb).set_property ("current-text", (int)page_num);
    }
  }

  private void on_stream_switched (Notebook nb, Widget page, uint page_num) {
    if (pb.current_state > State.PAUSED) {
      if (page_num < num_video_streams) {
        debug ("Switching video to: %u", page_num);
        ((GLib.Object)pb).set_property ("current-video", (int)page_num);
        return;
      }
      page_num -= num_video_streams;
      if (page_num < num_audio_streams) {
        debug ("Switching audio to: %u", page_num);
        ((GLib.Object)pb).set_property ("current-audio", (int)page_num);
        return;
      }
      page_num -= num_audio_streams;
      if (page_num < num_subtitle_streams) {
        debug ("Switching subtitle to: %u", page_num);
        ((GLib.Object)pb).set_property ("current-text", (int)page_num);
        return;
      }
    }
  }
  
  private void on_toc_entry_changed (TreeView view) {
    TreeSelection sel = view.get_selection ();
    if (sel == null)
      return;
    
    TreeModel model;
    TreeIter iter;
    if (sel.get_selected (out model, out iter)) {
      int64 start;
      model.get(iter, 1, out start, -1);
      if (start != Gst.CLOCK_TIME_NONE) {
        // we ignore 'stop' right now
        pb.seek_simple (Gst.Format.TIME, Gst.SeekFlags.FLUSH, start);
      }
    }
  }
  
  // helpers
  
  private Widget describe_container_stream (DiscovererStreamInfo sinfo) {
    Table table = new Table (2, 4, false);

    uint row = 0;
    add_table_rows_for_caps (table, row, "Format:", sinfo.get_caps ());
    row+=2;
    
    if (add_table_row_for_structure (table, row, sinfo.get_misc ())) {
      row++;
    }
    if (add_table_row_for_taglist (table, row, sinfo.get_tags ())) {
      row++;
    }
    
    return (Widget)table;
  }

  private Widget describe_video_stream (DiscovererStreamInfo sinfo) {
    DiscovererVideoInfo vinfo = (DiscovererVideoInfo)sinfo;
    Table table = new Table (2, 8, false);
    
    Gdk.Point res = {
      (int)((DiscovererVideoInfo)sinfo).get_width(),
      (int)((DiscovererVideoInfo)sinfo).get_height()
    };
    video_resolutions.add(res);

    string str;
    uint row = 0;
    add_table_rows_for_caps (table, row, "Codec:", sinfo.get_caps ());
    row+=2;
    
    add_table_row_for_bitrates (table, row, vinfo.get_bitrate(), vinfo.get_max_bitrate());
    row++;

    // add named resolutions: (640x480=VGA)
    string resolution = "%u x %u".printf (res.x, res.y);
    string named_res = resolutions[resolution];
    if (named_res != null) {
      str = "%s (%s)".printf (named_res, resolution);
    } else {
      str = resolution;
    }
    add_table_row_for_string (table, row, "Resolution:", str);
    row++;

    double fps_num = (double)vinfo.get_framerate_num();
    double fps_denom = (double)vinfo.get_framerate_denom();
    if (fps_num != 0) {
      str = "%.3lf frames/second".printf (fps_num/fps_denom);
    } else {
      if (fps_denom == 1) {
        // TODO(ensonic): there are a few files where video is flaged as still image
        // ~/temp/Video/luc_00036.MTS
        // ~/temp/Video/lookinggood.asx
        str = "still image";
      } else {
        str = "unknown";
      }
    }
    add_table_row_for_string (table, row, "Framerate:", str);
    row++;

    str = "%u : %u".printf (vinfo.get_par_num(),vinfo.get_par_denom());
    add_table_row_for_string (table, row, "PixelAspect:", str);
    row++;

    str = "%u bits/pixel".printf (vinfo.get_depth());
    add_table_row_for_string (table, row, "Bitdepth:", str);
    row++;

    str = "%s".printf (vinfo.is_interlaced() ? "true" : "false");
    add_table_row_for_string (table, row, "Interlaced:", str);
    row++;

    if (add_table_row_for_structure (table, row, sinfo.get_misc ())) {
      row++;
    }
    if (add_table_row_for_taglist (table, row, sinfo.get_tags ())) {
      row++;
    }

    return (Widget)table;
  }

  private Widget describe_audio_stream (DiscovererStreamInfo sinfo) {
    DiscovererAudioInfo ainfo = (DiscovererAudioInfo)sinfo;
    Table table = new Table (2, 7, false);

    string str;
    uint row = 0;
    add_table_rows_for_caps (table, row, "Codec:", sinfo.get_caps ());
    row+=2;

    add_table_row_for_bitrates (table, row, ainfo.get_bitrate(), ainfo.get_max_bitrate());
    row++;

    str = "%u samples/second".printf (ainfo.get_sample_rate());
    add_table_row_for_string (table, row, "Samplerate:", str);
    row++;

    // TODO: check channel layouts, can we have some nice names here ?
    // GstDiscoverer should expose channel positions
    str = "%u".printf (ainfo.get_channels());
    add_table_row_for_string (table, row, "Channels:", str);
    row++;

    str = "%u bits/sample".printf (ainfo.get_depth());
    add_table_row_for_string (table, row, "Bitdepth:", str);
    row++;

    add_table_row_for_string (table, row, "Language:", ainfo.get_language());
    row++;

    if (add_table_row_for_structure (table, row, sinfo.get_misc ())) {
      row++;
    }
    if (add_table_row_for_taglist (table, row, sinfo.get_tags ())) {
      row++;
    }

    return (Widget)table;
  }

  private Widget describe_subtitle_stream (DiscovererStreamInfo sinfo) {
    DiscovererSubtitleInfo tinfo = (DiscovererSubtitleInfo) sinfo;
    Table table = new Table (2, 5, false);
    
    uint row = 0;
    add_table_rows_for_caps (table, row, "Codec:", sinfo.get_caps ());
    row+=2;

    add_table_row_for_string (table, row, "Language:", tinfo.get_language());
    row++;

    if (add_table_row_for_structure (table, row, sinfo.get_misc ())) {
      row++;
    }
    if (add_table_row_for_taglist (table, row, sinfo.get_tags ())) {
      row++;
    }

    return (Widget)table;
  }

  private void clear_notebook (Notebook nb) {
    while (nb.get_n_pages() > 0) {
      nb.remove_page (-1);
    }
  }
  
  private void set_wikilink (Label label, Caps caps) {
    string str = get_codec_description (caps);
    string wikilink = wikilinks[str];

    if (wikilink == null) {
      wikilink = wikilinks[caps.get_structure(0).get_name()];
    }
    if (wikilink != null) {
      // FIXME: make prefix (en) and link translatable
      label.set_markup ("<a href=\"http://en.wikipedia.org/wiki/%s\">%s</a>".printf (wikilink, str));
    } else {
      label.set_text (str);
    }
  }

  private void add_table_rows_for_caps (Table table, uint row, string title, Caps caps) {
    AttachOptions fill = AttachOptions.FILL;
    AttachOptions fill_exp = AttachOptions.EXPAND|AttachOptions.FILL;

    // filter buffer entries from caps
    // TODO(ensonic): add filtering api to gstreamer
    Structure structure = caps.get_structure (0).copy();
    while (structure.foreach ( (id, val) => {
      if (val.holds(typeof (Gst.Buffer))) {
        structure.remove_field (id.to_string ());
        return false;
      }
      return true;
    }) == false) {}
    string str = structure.to_string( );
    Label label = new Label (str);
    label.set_ellipsize (Pango.EllipsizeMode.END);
    label.set_alignment (0.0f, 0.5f);
    label.set_selectable (true);
    label.set_tooltip_text (str);
    table.attach (label, 0, 2, row, row+1, fill_exp, 0, 0, 1);
    row++;

    label = new Label (title);
    label.set_alignment (1.0f, 0.5f);
    table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
    label = new Label (null);
    label.set_alignment (0.0f, 0.5f);
    label.set_selectable (true);
    label.set_use_markup (true);
    set_wikilink (label, caps); 
    table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
  }
  
  private void add_table_row_for_bitrates (Table table, uint row, uint br, uint mbr) {
    string str;
    
    if (br == mbr) {
      mbr = 0; // no point in printing this as a range
    }
    
    if (mbr != 0) {
      str = "%.2f ... %.2f kbit/second".printf (br/1024.0, mbr/1024.0);
    } else {
      if (br != 0) {
        str = "%.2f kbit/second".printf (br/1024.0);
      } else {
        str = "unknown";
      }
    }
    add_table_row_for_string (table, row, "Bitrate:", str);
  }
  
  private void add_table_row_for_string (Table table, uint row, string title, string? str) {
    AttachOptions fill = AttachOptions.FILL;
    AttachOptions fill_exp = AttachOptions.EXPAND|AttachOptions.FILL;

    Label label = new Label (title);
    label.set_alignment (1.0f, 0.5f);
    table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
    label = new Label (str);
    label.set_alignment (0.0f, 0.5f);
    label.set_selectable (true);
    table.attach (label, 1, 2, row, row+1, fill_exp, 0, 3, 1);
  }

  private bool add_table_row_for_structure (Table table, uint row, Structure? s) {
    if (s == null)
      return false;

    AttachOptions fill = AttachOptions.FILL;
    AttachOptions fill_exp = AttachOptions.EXPAND|AttachOptions.FILL;

    Label label = new Label ("Details:");
    label.set_alignment (1.0f, 0.5f);
    table.attach (label, 0, 1, row, row+1, fill, 0, 0, 0);
    label = new Label (s.to_string ());
    label.set_ellipsize (Pango.EllipsizeMode.END);
    label.set_alignment (0.0f, 0.5f);
    label.set_selectable (true);
    table.attach (label, 1, 2, row, row+1, fill_exp, 0, 0, 1);
    return true;
  }

  private bool add_table_row_for_taglist (Table table, uint row, TagList? t) {
    if (t == null)
      return false;

    AttachOptions fill = AttachOptions.FILL;
    AttachOptions fill_exp = AttachOptions.EXPAND|AttachOptions.FILL;

    Label label = new Label ("Tags:");
    label.set_alignment (1.0f, 0.0f);
    table.attach (label, 0, 1, row, row+1, fill, fill, 0, 0);
    label = new Label (build_taglist_info (t));
    label.set_ellipsize (Pango.EllipsizeMode.END);
    label.set_alignment (0.0f, 0.5f);
    label.set_selectable (true);
    label.set_use_markup (true);
    table.attach (label, 1, 2, row, row+1, fill_exp, 0, 0, 1);
    return true;
  }
  
  // get stream index where streams are orderd by stream_id
  private int get_stream_index (DiscovererStreamInfo sinfo, ArrayList<string> sids) {
    string sid = sinfo.get_stream_id ();
    int six = 0;
    
    for (six = 0; six <  sids.size; six++) {
      if (strcmp (sid, sids[six]) <= 0)
        break;
    }
    sids.insert (six, sid);

    return six;
  }

  private string build_taglist_info (TagList t) {
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
          debug ("found album art");
          is.close(null);
        } catch (Error e) {
          debug ("Decoding album art failed: %s: %s", e.domain.to_string (), e.message);
        }
        buf.unmap(info);

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
  
  private string format_time(ClockTime t) {
    if (t == Gst.CLOCK_TIME_NONE)
      return "unknown";

    return "%u:%02u:%02u.%09u".printf (
      (uint) (t / (SECOND * 60 * 60)),
      (uint) ((t / (SECOND * 60)) % 60),
      (uint) ((t / SECOND) % 60),
      (uint) ((t) % SECOND));
  }
    
  private void build_toc_info_for_entry (TreeStore s, TocEntry e, TreeIter? p) {
    TreeIter iter;
    int64 start, stop;

    e.get_start_stop_times(out start, out stop);
    string str = "";
    if (start != Gst.CLOCK_TIME_NONE) {
      str += "%s ".printf(format_time((ClockTime)start));
    }
    if (stop != Gst.CLOCK_TIME_NONE) {
      str += "- %s ".printf(format_time((ClockTime)stop));
    }    
    str += TocEntryType.get_nick(e.get_entry_type());
    
    s.append(out iter, p);
    s.set(iter, 0, str, 1, start, 2, stop, -1);
    
    unowned GLib.List<TocEntry> entries = e.get_sub_entries ();
    if (entries != null) {
      foreach (TocEntry se in entries) {
        build_toc_info_for_entry (s, se, iter);
      }
    }
  }
  
  private TreeStore? build_toc_info (Toc? t) {
    if (t == null)
      return null;
    
    TreeStore s = new TreeStore(3, typeof (string), typeof (int64), typeof (int64));
    unowned GLib.List<TocEntry> entries = t.get_entries ();
    foreach (TocEntry e in entries) {
      build_toc_info_for_entry (s, e, null);
    }
    
    return s;
  }
}
