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

public class MediaInfo.Preview : DrawingArea {
  private Gdk.Pixbuf content = null;
  private int width = 0;
  private int height = 0;
  private float ratio = 0.0f;
  private int alloc_width;
  private int alloc_height;
  
  construct {
    set_has_window (true);
  }
  
  public void reset () {
    content = null;
    ratio = 0.0f;
    debug ("no content: 0.0");
    queue_resize ();
  }
  
  public void set_static_content (Gdk.Pixbuf? content) {
    this.content = content;
    if (content != null) {
      width = content.get_width ();
      height = content.get_height ();
      ratio = (float)width / (float)height;
      debug ("ratio from album art: %f", ratio);
      queue_resize ();
    }
  }

  public void set_content_size (uint width, uint height) {
    if (height != 0) {
      ratio = (float)width / (float)height;
    } else {
      ratio = 0.0f;
    }
    debug ("ratio from video: %f", ratio);
    queue_resize ();
  }
  
  // vmethods
  
  public override SizeRequestMode get_request_mode () {
    return SizeRequestMode.HEIGHT_FOR_WIDTH;
  }
  
  public override void get_preferred_width (out int minimal_width, out int natural_width) {
    if (ratio != 0.0) {
      minimal_width = 16;
      natural_width = (int)(alloc_height * ratio);
      natural_width = int.max (minimal_width, natural_width);
    } else {
      minimal_width = natural_width = 0;
    }
    debug ("width w,h: %d,%d", natural_width, alloc_height);
  }
  
  public override void get_preferred_height (out int minimal_height, out int natural_height) {
    if (ratio != 0.0) {
      minimal_height = 12;
      natural_height = (int)(alloc_width / ratio);
      natural_height = int.max (minimal_height, natural_height);
    } else {
      minimal_height = natural_height = 0;
    }
    debug ("height w,h: %d,%d", alloc_width, natural_height);
  }
  
  public override void get_preferred_width_for_height (int height, out int minimal_width, out int natural_width) {
    if (ratio != 0.0) {
      minimal_width = 16;
      natural_width = (int)(height * ratio);
    } else {
      minimal_width = natural_width = 0;
    }
    debug ("width_for_height w,h: %d,%d", natural_width, height);
  }

  public override void get_preferred_height_for_width (int width, out int minimal_height, out int natural_height) {
    if (ratio != 0.0) {
      minimal_height = 12;
      natural_height = (int)(width / ratio);
    } else {
      minimal_height = natural_height = 0;
    }
    debug ("height_for_width w,h: %d,%d", width, natural_height);
  }
  
  public override void size_allocate (Gtk.Allocation alloc) {
    base.size_allocate (alloc);

    alloc_width = alloc.width;
    alloc_height = alloc.height;
    debug ("alloc w,h: %d,%d", alloc_width, alloc_height);
  }
  
  public override bool draw (Cairo.Context cr) {
    if (content != null) {
      Gdk.Pixbuf pb = content.scale_simple (alloc_width, alloc_height, Gdk.InterpType.BILINEAR);
      Gdk.cairo_set_source_pixbuf (cr, pb, 0, 0);
    } else {
      cr.set_source_rgb (0, 0, 0);
    }
    cr.rectangle (0, 0, alloc_width, alloc_height);
    cr.fill ();
    debug ("draw w,h: %d,%d", alloc_width, alloc_height);
    return false;
  }
}

