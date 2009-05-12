using System;

namespace Gst {
  public struct PropertyInfo {
    internal string name;
    public string Name {
      get {
        return name;
      }
    }

    internal string nick;
    public string Nick {
      get {
        return nick;
      }
    }

    internal string blurb;
    public string Blurb {
      get {
        return blurb;
      }
    }

    internal bool readable;
    public bool Readable {
      get {
        return readable;
      }
    }

    internal bool writeable;
    public bool Writeable {
      get {
        return writeable;
      }
    }

    internal bool controllable;
    public bool Controllable {
      get {
        return controllable;
      }
    }

    internal System.Type type;
    public System.Type Type {
      get {
        return type;
      }
    }

    internal GLib.GType gtype;
    public GLib.GType GType {
      get {
        return gtype;
      }
    }

    internal object dflt;
    public object Default {
      get {
        return dflt;
      }
    }

    internal object min;
    public object Min {
      get {
        return min;
      }
    }

    internal object max;
    public object Max {
      get {
        return max;
      }
    }
  }

}
