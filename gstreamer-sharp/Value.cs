// Wrapper for GLib.Value to add support for GstFraction, GstFourcc, Gst*Range, ...

using System;
using System.Collections;
using System.Runtime.InteropServices;
using GLib;

/* TODO: intersect, compare, substract, .... */
namespace Gst {
  public static class Value {
    public static object GetValue (GLib.Value val) {
      IntPtr type = gstsharp_g_value_type (ref val);
      GType gtype = new GType (type);

      if (type == Fraction.GType.Val)
        return new Fraction (val);
      else if (type == DoubleRange.GType.Val)
        return new DoubleRange (val);
      else if (type == IntRange.GType.Val)
        return new IntRange (val);
      else if (type == FractionRange.GType.Val)
        return new FractionRange (val);
      else if (type == Fourcc.GType.Val)
        return new Fourcc (val);
      else if (type == Date.GType.Val)
        return new Date (val);
      else if (type == Gst.List.GType.Val)
        return new Gst.List (val);
      else if (type == Gst.Array.GType.Val)
        return new Gst.Array (val);
      else if ( ( (Type) gtype).IsSubclassOf (typeof (Gst.MiniObject)))
        return MiniObject.NewFromValue (val);
      else
        return val.Val;
    }

    [DllImport ("gstreamersharpglue-0.10.dll") ]
    private static extern IntPtr gstsharp_g_value_type (ref GLib.Value val);

    public static GLib.Value CreateValue (object o) {
      if (o.GetType () == typeof (Fraction))
        return (GLib.Value) ( (Fraction) o);
      else if (o.GetType () == typeof (DoubleRange))
        return (GLib.Value) ( (DoubleRange) o);
      else if (o.GetType () == typeof (IntRange))
        return (GLib.Value) ( (IntRange) o);
      else if (o.GetType () == typeof (FractionRange))
        return (GLib.Value) ( (FractionRange) o);
      else if (o.GetType () == typeof (Fourcc))
        return (GLib.Value) ( (Fourcc) o);
      else if (o.GetType () == typeof (Date))
        return (GLib.Value) ( (Date) o);
      else if (o.GetType () == typeof (DateTime))
        return (GLib.Value) (new Date ( (DateTime) o));
      else if (o.GetType () == typeof (Gst.List))
        return (GLib.Value) ( (Gst.List) o);
      else if (o.GetType () == typeof (Gst.Array))
        return (GLib.Value) ( (Gst.Array) o);
      else if (o.GetType ().IsSubclassOf (typeof (Gst.MiniObject)))
        return (GLib.Value) ( (MiniObject) o);
      else
        return new GLib.Value (o);
    }
  }

  public struct Fraction {
    public int Numerator {
      get {
        return numerator;
      }

      set {
        numerator = value;
        Reduce ();
      }
    }

    public int Denominator {
      get {
        return denominator;
      }

      set {
        if (denominator == 0)
          throw new ArgumentException ();

        denominator = value;
        Reduce ();
      }
    }

    private int numerator;
    private int denominator;

    public static GLib.GType GType {
      get {
        return new GType (gst_fraction_get_type ());
      }
    }

    private void Reduce () {
      int gcd = GreatestCommonDivisor (this);

      if (gcd != 0) {
        this.numerator /= gcd;
        this.denominator /= gcd;
      }
    }

    private static int GreatestCommonDivisor (Fraction fraction) {
      int a = fraction.numerator;
      int b = fraction.denominator;

      while (b != 0) {
        int temp = a;

        a = b;
        b = temp % b;
      }
      return Math.Abs (a);
    }

    public Fraction (int numerator, int denominator) {
      if (denominator == 0)
        throw new ArgumentException ();

      this.numerator = numerator;
      this.denominator = denominator;
      Reduce ();
    }

    public Fraction (GLib.Value val) : this () {
      this.numerator = gst_value_get_fraction_numerator (ref val);
      this.denominator = gst_value_get_fraction_denominator (ref val);
    }

    public override string ToString () {
      return String.Format ("{0}/{1}", numerator, denominator);
    }

    public static explicit operator GLib.Value (Fraction fraction) {
      GLib.Value val = new GLib.Value (Fraction.GType);
      gst_value_set_fraction (ref val, fraction.Numerator, fraction.Denominator);

      return val;
    }

    public static explicit operator double (Fraction fraction) {
      return ( (double) fraction.numerator) / ( (double) fraction.denominator);
    }

    public static Fraction operator + (Fraction a, Fraction b) {
      return new Fraction ( (a.Numerator * b.Denominator) + (b.Numerator * a.Denominator), a.Denominator * b.Denominator);
    }

    public static Fraction operator - (Fraction a, Fraction b) {
      return new Fraction ( (a.Numerator * b.Denominator) - (b.Numerator * a.Denominator), a.Denominator * b.Denominator);
    }

    public static Fraction operator * (Fraction a, Fraction b) {
      return new Fraction (a.Numerator * b.Numerator, a.Denominator * b.Denominator);
    }

    public static Fraction operator / (Fraction a, Fraction b) {
      return new Fraction (a.Numerator * b.Denominator, a.Denominator * b.Numerator);
    }

    [DllImport ("gstreamer-0.10.dll") ]
    private static extern void gst_value_set_fraction (ref GLib.Value v, int numerator, int denominator);
    [DllImport ("gstreamer-0.10.dll") ]
    private static extern int gst_value_get_fraction_numerator (ref GLib.Value v);
    [DllImport ("gstreamer-0.10.dll") ]
    private static extern int gst_value_get_fraction_denominator (ref GLib.Value v);
    [DllImport ("gstreamer-0.10.dll") ]
    private static extern IntPtr gst_fraction_get_type ();
  }

  public struct DoubleRange {
    public double Min;
    public double Max;

    public static GLib.GType GType {
      get {
        return new GType (gst_double_range_get_type ());
      }
    }

    public DoubleRange (double min, double max) {
      if (min > max)
        throw new ArgumentException ();

      this.Min = min;
      this.Max = max;
    }

    public DoubleRange (GLib.Value val) : this () {
      this.Min = gst_value_get_double_range_min (ref val);
      this.Max = gst_value_get_double_range_max (ref val);
    }

    public override string ToString () {
      return String.Format ("[{0}, {1}]", Min, Max);
    }

    public static explicit operator GLib.Value (DoubleRange range) {
      GLib.Value val = new GLib.Value (DoubleRange.GType);

      gst_value_set_double_range (ref val, range.Min, range.Max);
      return val;
    }

    [DllImport ("gstreamer-0.10.dll") ]
    private static extern IntPtr gst_double_range_get_type ();
    [DllImport ("gstreamer-0.10.dll") ]
    private static extern void gst_value_set_double_range (ref GLib.Value v, double min, double max);
    [DllImport ("gstreamer-0.10.dll") ]
    private static extern double gst_value_get_double_range_min (ref GLib.Value v);
    [DllImport ("gstreamer-0.10.dll") ]
    private static extern double gst_value_get_double_range_max (ref GLib.Value v);
  }

  public struct IntRange {
    public int Min;
    public int Max;

    public static GLib.GType GType {
      get {
        return new GType (gst_int_range_get_type ());
      }
    }

    public IntRange (int min, int max) {
      if (min > max)
        throw new ArgumentException ();

      this.Min = min;
      this.Max = max;
    }

    public IntRange (GLib.Value val) : this () {
      this.Min = gst_value_get_int_range_min (ref val);
      this.Max = gst_value_get_int_range_max (ref val);
    }

    public override string ToString () {
      return String.Format ("[{0}, {1}]", Min, Max);
    }

    public static explicit operator GLib.Value (IntRange range) {
      GLib.Value val = new GLib.Value (IntRange.GType);

      gst_value_set_int_range (ref val, range.Min, range.Max);
      return val;
    }

    [DllImport ("gstreamer-0.10.dll") ]
    private static extern IntPtr gst_int_range_get_type ();
    [DllImport ("gstreamer-0.10.dll") ]
    private static extern void gst_value_set_int_range (ref GLib.Value v, int min, int max);
    [DllImport ("gstreamer-0.10.dll") ]
    private static extern int gst_value_get_int_range_min (ref GLib.Value v);
    [DllImport ("gstreamer-0.10.dll") ]
    private static extern int gst_value_get_int_range_max (ref GLib.Value v);
  }

  public struct FractionRange {
    public Fraction Min;
    public Fraction Max;

    public static GLib.GType GType {
      get {
        return new GType (gst_fraction_range_get_type ());
      }
    }

    public FractionRange (Fraction min, Fraction max) {
      double a = (double) min, b = (double) max;

      if (a > b)
        throw new ArgumentException();

      this.Min = min;
      this.Max = max;
    }

    public FractionRange (GLib.Value val) : this () {
      IntPtr min_ptr, max_ptr;
      GLib.Value min, max;

      min_ptr = gst_value_get_fraction_range_min (ref val);
      max_ptr = gst_value_get_fraction_range_max (ref val);

      min = (GLib.Value) Marshal.PtrToStructure (min_ptr, typeof (GLib.Value));
      max = (GLib.Value) Marshal.PtrToStructure (max_ptr, typeof (GLib.Value));
      this.Min = (Fraction) Gst.Value.GetValue (min);
      this.Max = (Fraction) Gst.Value.GetValue (max);
    }

    public override string ToString () {
      return String.Format ("[{0}, {1}]", Min, Max);
    }

    public static explicit operator GLib.Value (FractionRange range) {
      GLib.Value val = new GLib.Value (FractionRange.GType);

      GLib.Value min = (GLib.Value) (range.Min);
      GLib.Value max = (GLib.Value) (range.Max);
      gst_value_set_fraction_range (ref val, ref min, ref max);
      return val;
    }

    [DllImport ("gstreamer-0.10.dll") ]
    private static extern IntPtr gst_fraction_range_get_type ();
    [DllImport ("gstreamer-0.10.dll") ]
    private static extern void gst_value_set_fraction_range (ref GLib.Value v, ref GLib.Value min, ref GLib.Value max);
    [DllImport ("gstreamer-0.10.dll") ]
    private static extern IntPtr gst_value_get_fraction_range_min (ref GLib.Value v);
    [DllImport ("gstreamer-0.10.dll") ]
    private static extern IntPtr gst_value_get_fraction_range_max (ref GLib.Value v);
  }

  public struct Fourcc {
    public uint Val;

    public static GLib.GType GType {
      get {
        return new GType (gst_fourcc_get_type ());
      }
    }

    public Fourcc (uint fourcc) {
      this.Val = fourcc;
    }

    public Fourcc (char[] fourcc) {
      if (fourcc.Length != 4)
        throw new ArgumentException ();

      this.Val = (uint) ( ( ( (byte) fourcc[0]) << 24) |
                          ( ( (byte) fourcc[1]) << 16) |
                          ( ( (byte) fourcc[2]) << 8) |
                          ( ( (byte) fourcc[3]) << 0));
    }

    public Fourcc (string fourcc) {
      if (fourcc.Length != 4)
        throw new ArgumentException ();

      this.Val = (uint) ( ( ( (byte) fourcc[0]) << 24) |
                          ( ( (byte) fourcc[1]) << 16) |
                          ( ( (byte) fourcc[2]) << 8) |
                          ( ( (byte) fourcc[3]) << 0));
    }

    public Fourcc (GLib.Value val) : this (gst_value_get_fourcc (ref val)) {
    }

    public override string ToString () {
      return String.Format ("{0}{1}{2}{3}", (char) ( (Val >> 24) & 0xff),
                            (char) ( (Val >> 16) & 0xff),
                            (char) ( (Val >> 8) & 0xff),
                            (char) ( (Val >> 0) & 0xff));
    }

    public static explicit operator GLib.Value (Fourcc fourcc) {
      GLib.Value val = new GLib.Value (Fourcc.GType);

      gst_value_set_fourcc (ref val, fourcc.Val);
      return val;
    }

    public static explicit operator char[] (Fourcc fourcc) {
      return new char[] { (char) ( (fourcc.Val >> 24) & 0xff),
                          (char) ( (fourcc.Val >> 16) & 0xff),
                          (char) ( (fourcc.Val >> 8) & 0xff),
                          (char) ( (fourcc.Val >> 0) & 0xff)
                        };
    }

    public static explicit operator string (Fourcc fourcc) {
      return fourcc.ToString ();
    }

    [DllImport ("gstreamer-0.10.dll") ]
    private static extern IntPtr gst_fourcc_get_type ();
    [DllImport ("gstreamer-0.10.dll") ]
    private static extern void gst_value_set_fourcc (ref GLib.Value v, uint fourcc);
    [DllImport ("gstreamer-0.10.dll") ]
    private static extern uint gst_value_get_fourcc (ref GLib.Value v);

  }

  public struct Date {
    public DateTime Val;

    public static GLib.GType GType {
      get {
        return new GType (gst_date_get_type ());
      }
    }

    public Date (DateTime date) {
      this.Val = date;
    }

    public Date (int day, int month, int year) {
      this.Val = new DateTime (year, month, day);
    }

    public Date (GLib.Value val) {
      IntPtr date = gst_value_get_date (ref val);

      this.Val = new DateTime (g_date_get_year (date), g_date_get_month (date), g_date_get_day (date));
    }

    public override string ToString () {
      return Val.ToString ();
    }

    public static explicit operator GLib.Value (Date date) {
      GLib.Value val = new GLib.Value (Date.GType);
      IntPtr date_ptr = g_date_new_dmy ( (byte) date.Val.Day, (int) date.Val.Month, (ushort) date.Val.Year);

      gst_value_set_date (ref val, date_ptr);

      GLib.Marshaller.Free (date_ptr);

      return val;
    }

    [DllImport ("libglib-2.0-0.dll") ]
    private static extern byte g_date_get_day (IntPtr date);
    [DllImport ("libglib-2.0-0.dll") ]
    private static extern int g_date_get_month (IntPtr date);
    [DllImport ("libglib-2.0-0.dll") ]
    private static extern ushort g_date_get_year (IntPtr date);
    [DllImport ("libglib-2.0-0.dll") ]
    private static extern IntPtr g_date_new_dmy (byte day, int month, ushort year);

    [DllImport ("gstreamer-0.10.dll") ]
    private static extern IntPtr gst_date_get_type ();
    [DllImport ("gstreamer-0.10.dll") ]
    private static extern IntPtr gst_value_get_date (ref GLib.Value val);
    [DllImport ("gstreamer-0.10.dll") ]
    private static extern void gst_value_set_date (ref GLib.Value val, IntPtr date);
  }

  public struct List {
    private IList content;

    public static GLib.GType GType {
      get {
        return new GType (gst_value_list_get_type ());
      }
    }

    public List (IList content) {
      this.content = content;
    }

    public List (GLib.Value val) {
      this.content = new ArrayList ();

      uint n = gst_value_list_get_size (ref val);
      for (uint i = 0; i < n; i++) {
        IntPtr v_ptr = gst_value_list_get_value (ref val, i);
        GLib.Value v = (GLib.Value) Marshal.PtrToStructure (v_ptr, typeof (GLib.Value));
        this.content.Add (Gst.Value.GetValue (v));
      }
    }

    public static explicit operator GLib.Value (List l) {
      GLib.Value val = new GLib.Value (List.GType);

      foreach (object o in l.content) {
        GLib.Value v = Gst.Value.CreateValue (o);
        gst_value_list_append_value (ref val, ref v);
      }

      return val;
    }

    [DllImport ("gstreamer-0.10.dll") ]
    private static extern IntPtr gst_value_list_get_type ();
    [DllImport ("gstreamer-0.10.dll") ]
    private static extern uint gst_value_list_get_size (ref GLib.Value val);
    [DllImport ("gstreamer-0.10.dll") ]
    private static extern IntPtr gst_value_list_get_value (ref GLib.Value val, uint index);
    [DllImport ("gstreamer-0.10.dll") ]
    private static extern void gst_value_list_append_value (ref GLib.Value val, ref GLib.Value el);
  }

  public struct Array {
    private IList content;

    public static GLib.GType GType {
      get {
        return new GType (gst_value_array_get_type ());
      }
    }

    public Array (IList content) {
      this.content = content;
    }

    public Array (GLib.Value val) {
      this.content = new ArrayList ();

      uint n = gst_value_array_get_size (ref val);
      for (uint i = 0; i < n; i++) {
        IntPtr v_ptr = gst_value_array_get_value (ref val, i);
        GLib.Value v = (GLib.Value) Marshal.PtrToStructure (v_ptr, typeof (GLib.Value));
        this.content.Add (Gst.Value.GetValue (v));
      }
    }

    public static explicit operator GLib.Value (Array a) {
      GLib.Value val = new GLib.Value (Gst.Array.GType);

      foreach (object o in a.content) {
        GLib.Value v = Gst.Value.CreateValue (o);
        gst_value_array_append_value (ref val, ref v);
      }

      return val;
    }

    [DllImport ("gstreamer-0.10.dll") ]
    private static extern IntPtr gst_value_array_get_type ();
    [DllImport ("gstreamer-0.10.dll") ]
    private static extern uint gst_value_array_get_size (ref GLib.Value val);
    [DllImport ("gstreamer-0.10.dll") ]
    private static extern IntPtr gst_value_array_get_value (ref GLib.Value val, uint index);
    [DllImport ("gstreamer-0.10.dll") ]
    private static extern void gst_value_array_append_value (ref GLib.Value val, ref GLib.Value el);
  }
}
