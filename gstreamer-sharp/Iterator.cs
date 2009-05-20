using System;
using System.Collections;
using System.Runtime.InteropServices;
using GLib;

namespace Gst {

  internal class Enumerable : IEnumerable {
    private class Enumerator : IEnumerator {
      Hashtable seen = new Hashtable ();
      IntPtr iterator;

      private object current = null;
      public object Current {
        get {
          return current;
        }
      }

      public bool MoveNext () {
        IntPtr raw_ret;
        bool retry = false;

        if (iterator == IntPtr.Zero)
          return false;

        do {
          int ret = gst_iterator_next (iterator, out raw_ret);
          switch (ret) {
            case 0:
              return false;
            case 1:
              if (seen.Contains (raw_ret)) {
                retry = true;
                break;
              }

              seen.Add (raw_ret, null);
              current = GLib.Object.GetObject (raw_ret, true);
              return true;
            case 2:
              gst_iterator_resync (iterator);
              retry = true;
              break;
            default:
            case 3:
              throw new Exception ("Error while iterating pads");
          }
        } while (retry);

        return false;
      }

      public void Reset () {
        seen.Clear ();
        if (iterator != IntPtr.Zero)
          gst_iterator_resync (iterator);
      }

      public Enumerator (IntPtr iterator) {
        this.iterator = iterator;
      }
    }

    private IntPtr iterator;
    private Enumerator enumerator;

    public Enumerable (IntPtr iterator) {
      this.iterator = iterator;
      this.enumerator = new Enumerator (iterator);
    }

    public IEnumerator GetEnumerator () {
      return enumerator;
    }

    ~Enumerable () {
      if (iterator != IntPtr.Zero)
        gst_iterator_free (iterator);
    }

    [DllImport ("gstreamer-0.10.dll") ]
    static extern int gst_iterator_next (IntPtr iterator, out IntPtr elem);
    [DllImport ("gstreamer-0.10.dll") ]
    static extern void gst_iterator_resync (IntPtr iterator);
    [DllImport ("gstreamer-0.10.dll") ]
    static extern void gst_iterator_free (IntPtr iterator);

  }
}
