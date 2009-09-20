using System;
using System.Xml;
using System.Xml.XPath;
using System.IO;
using System.Collections;

public class EnumValue {
  public string name;
  public int value;
}

public class FlagValue {
  public string name;
  public uint value;
}

public class EnumInfo {
  public string name;
  public bool flag;
  public ArrayList values;
}

public class SignalParameter {
  public string name;
  public string type;
}

public class SignalInfo {
  public string name;
  public string managed_name;
  public string object_type;
  public string return_type;
  public ArrayList parameters;
}

public class PropertyInfo {
  public string name;
  public string managed_name;
  public string type;
  public bool readable, writeable;
  public EnumInfo enuminfo;
}

public class ElementInfo {
  public string name;
  public string class_name;
  public string gtype_name;
  public ArrayList hierarchy;
  public ArrayList interfaces;

  public ArrayList pads;

  public ArrayList properties;
  public ArrayList signals;
  public ArrayList actions;
}

public class ElementGen {

  static bool IsHidden (XmlNode node) {
    XmlElement elt = node as XmlElement;
    return (elt != null && elt.HasAttribute ("hidden") &&
            (elt.GetAttribute ("hidden") == "1" ||
             elt.GetAttribute ("hidden") == "true"));
  }

  static bool handlePads (XmlNode node, ElementInfo ei) {
    if (ei.pads == null)
      ei.pads = new ArrayList ();

    XmlElement elt = node as XmlElement;
    if (elt == null)
      return true;

    foreach (XmlElement pad in elt.ChildNodes)
      if (!IsHidden (pad) && !IsHidden (pad["name"]))
        ei.pads.Add (pad["name"].InnerText);

    return true;
  }

  static bool handleProperties (XmlNode node, ElementInfo ei) {
    if (ei.properties == null)
      ei.properties = new ArrayList ();

    XmlElement elt = node as XmlElement;
    if (elt == null)
      return true;

    foreach (XmlElement property in elt.ChildNodes) {
      if (IsHidden (property))
        continue;

      PropertyInfo pi = new PropertyInfo ();
      pi.name = property["name"].InnerText;
      pi.managed_name = (property["managed_name"] != null) ? property["managed_name"].InnerText : null;
      pi.type = property["type"].InnerText;
      pi.readable = property["flags"].InnerText.IndexOf ('R') != -1;
      pi.writeable = property["flags"].InnerText.IndexOf ('W') != -1;

      if (property["enum-values"] != null) {
        pi.enuminfo = new EnumInfo ();
        pi.enuminfo.flag = false;
        pi.enuminfo.values = new ArrayList ();

        foreach (XmlNode val in property["enum-values"].ChildNodes) {
          EnumValue env = new EnumValue ();

          env.name = val.Attributes["nick"].InnerText;
          env.value = Int32.Parse (val.Attributes["value"].InnerText);
          pi.enuminfo.values.Add (env);
        }
      } else if (property["flags-values"] != null) {
        pi.enuminfo = new EnumInfo ();
        pi.enuminfo.flag = true;
        pi.enuminfo.values = new ArrayList ();

        foreach (XmlNode val in property["flags-values"].ChildNodes) {
          FlagValue env = new FlagValue ();

          env.name = val.Attributes["nick"].InnerText;
          env.value = UInt32.Parse (val.Attributes["value"].InnerText);
          pi.enuminfo.values.Add (env);
        }
      }

      ei.properties.Add (pi);
    }

    return true;
  }

  static bool handleSignals (XmlNode node, ElementInfo ei) {
    if (ei.signals == null)
      ei.signals = new ArrayList ();

    XmlElement elt = node as XmlElement;
    if (elt == null)
      return true;

    foreach (XmlElement signal in elt.ChildNodes) {
      if (IsHidden (signal))
        continue;

      SignalInfo si = new SignalInfo ();
      si.name = signal["name"].InnerText;
      si.managed_name = (signal["managed_name"] != null) ? signal["managed_name"].InnerText : null;
      si.return_type = signal["return-type"].InnerText;
      si.object_type = signal["object-type"].InnerText;

      XmlElement parms = signal["params"];
      if (parms != null) {
        si.parameters = new ArrayList ();
        foreach (XmlElement parm in parms.ChildNodes) {
          SignalParameter par = new SignalParameter ();
          par.type = parm.InnerText;

          if (parm.Attributes["name"] == null)
            throw new Exception ("All signal parameters need the 'name' attribute");

          par.name = parm.Attributes["name"].InnerText;
          si.parameters.Add (par);
        }
      }

      ei.signals.Add (si);
    }
    return true;
  }

  static bool handleActions (XmlNode node, ElementInfo ei) {
    if (ei.actions == null)
      ei.actions = new ArrayList ();

    XmlElement elt = node as XmlElement;
    if (elt == null)
      return true;

    foreach (XmlElement signal in elt.ChildNodes) {
      if (IsHidden (signal))
        continue;

      SignalInfo si = new SignalInfo ();
      si.name = signal["name"].InnerText;
      si.managed_name = (signal["managed_name"] != null) ? signal["managed_name"].InnerText : null;
      si.return_type = signal["return-type"].InnerText;
      si.object_type = signal["object-type"].InnerText;

      XmlElement parms = signal["params"];
      if (parms != null) {
        si.parameters = new ArrayList ();
        foreach (XmlElement parm in parms.ChildNodes) {
          SignalParameter par = new SignalParameter ();
          par.type = parm.InnerText;

          if (parm.Attributes["name"] == null)
            throw new Exception ("All signal parameters need the 'name' attribute");

          par.name = parm.Attributes["name"].InnerText;
          si.parameters.Add (par);
        }
      }

      ei.actions.Add (si);
    }
    return true;
  }

  public static string CTypeToManagedType (string ctype, XmlDocument api_doc) {
    switch (ctype) {
      case "GObject":
        return "Gst.GLib.Object";
      case "gchararray":
        return "string";
      case "gboolean":
        return "bool";
      case "guint":
        return "uint";
      case "gint":
        return "int";
      case "gulong":
      case "guint64":
        return "ulong";
      case "glong":
      case "gint64":
        return "long";
      case "gfloat":
        return "float";
      case "gdouble":
        return "double";
      case "GValueArray":
        return "Gst.GLib.ValueArray";
    }

    XPathNavigator api_nav = api_doc.CreateNavigator ();
    XPathNodeIterator type_iter = api_nav.Select ("/api/namespace/*[@cname='" + ctype + "']");
    while (type_iter.MoveNext ()) {
      string ret = type_iter.Current.GetAttribute ("name", "");
      if (ret != null && ret != String.Empty) {
        XPathNavigator parent = type_iter.Current.Clone ();
        parent.MoveToParent ();
        ret = parent.GetAttribute ("name", "") + "." + ret;
        return ret;
      }
    }

    return null;
  }

  public static void writeElement (TextWriter writer, ElementInfo ei, StreamReader custom_code, XmlDocument api_doc, string interfaces_dir) {
    ArrayList enums = new ArrayList ();

    writer.WriteLine ("#region Autogenerated code");
    writer.WriteLine ("\t[GTypeName (\"" + ei.gtype_name + "\")]");

    string class_name = (ei.class_name != null) ? ei.class_name : ei.gtype_name.StartsWith ("Gst") ? ei.gtype_name.Substring (3) : ei.gtype_name;

    writer.Write ("\tpublic sealed class " + class_name + " : ");
    for (int i = 1; i < ei.hierarchy.Count; i++) {
      string parent_type = (string) ei.hierarchy[i];
      string parent_managed_type = CTypeToManagedType (parent_type, api_doc);
      if (parent_managed_type != null) {
        writer.Write (parent_managed_type);
        break;
      }
    }

    foreach (string iface in ei.interfaces)
      writer.Write (", " + CTypeToManagedType (iface, api_doc));

    writer.WriteLine (" {");

    writer.WriteLine ("\t\tpublic " + class_name + " (IntPtr raw) : base (raw) { }\n");

    writer.WriteLine ("\t\t[DllImport(\"libgstreamer-0.10.dll\") ]");
    writer.WriteLine ("\t\tstatic extern IntPtr gst_element_factory_make (IntPtr element, IntPtr name);\n");

    writer.WriteLine ("\t\tpublic " + class_name + " (string name) : base (IntPtr.Zero) {");
    writer.WriteLine ("\t\t\tIntPtr native_name = Gst.GLib.Marshaller.StringToPtrGStrdup (name);");
    writer.WriteLine ("\t\t\tIntPtr native_element = Gst.GLib.Marshaller.StringToPtrGStrdup (\"" + ei.name + "\");");
    writer.WriteLine ("\t\t\tRaw = gst_element_factory_make (native_element, native_name);");
    writer.WriteLine ("\t\t\tGst.GLib.Marshaller.Free (native_name);");
    writer.WriteLine ("\t\t\tGst.GLib.Marshaller.Free (native_element);");
    writer.WriteLine ("\t\t\tif (Raw == IntPtr.Zero)");
    writer.WriteLine ("\t\t\t\tthrow new Exception (\"Failed to instantiate element \\\"" + ei.name + "\\\"\");");
    writer.WriteLine ("\t\t}\n");

    writer.WriteLine ("\t\tpublic " + class_name + " () : this ((string) null) { }\n");

    writer.WriteLine ("\t\tpublic static " + class_name + " Make (string name) {");
    writer.WriteLine ("\t\t\treturn Gst.ElementFactory.Make (\"" + ei.name + "\", name) as " + class_name + ";");
    writer.WriteLine ("\t\t}\n");

    writer.WriteLine ("\t\tpublic static " + class_name + " Make () { return Make (null); } \n");

    foreach (PropertyInfo pinfo in ei.properties) {
      string managed_name = (pinfo.managed_name != null) ? pinfo.managed_name : PropToCamelCase (pinfo.name);
      string managed_type = CTypeToManagedType (pinfo.type, api_doc);

      if (managed_type == null && pinfo.enuminfo == null) {
        throw new Exception ("Can't get managed type mapping for type " + pinfo.type);
      } else if (managed_type == null) {
        pinfo.enuminfo.name = pinfo.type;
        enums.Add (pinfo.enuminfo);
        managed_type = pinfo.type.StartsWith (ei.gtype_name) ? pinfo.type.Substring (ei.gtype_name.Length) : pinfo.type.StartsWith ("Gst") ? pinfo.type.Substring (3) : pinfo.type;
        managed_type += "Type";
      }

      writer.WriteLine ("\t\t[Gst.GLib.Property (\"" + pinfo.name + "\")]");
      writer.WriteLine ("\t\tpublic " + managed_type + " " + managed_name + " {");
      if (pinfo.readable) {
        writer.WriteLine ("\t\t\tget {");
        writer.WriteLine ("\t\t\t\tGst.GLib.Value val = GetProperty (\"" + pinfo.name + "\");");
        writer.WriteLine ("\t\t\t\t" + managed_type + " ret = (" + managed_type + ") val.Val;");
        writer.WriteLine ("\t\t\t\tval.Dispose ();");
        writer.WriteLine ("\t\t\t\treturn ret;");
        writer.WriteLine ("\t\t\t}");
      }

      if (pinfo.writeable) {
        writer.WriteLine ("\t\t\tset {");
        writer.WriteLine ("\t\t\t\tGst.GLib.Value val = new Gst.GLib.Value (this, \"" + pinfo.name + "\");");
        writer.WriteLine ("\t\t\t\tval.Val = value;");
        writer.WriteLine ("\t\t\t\tSetProperty (\"" + pinfo.name + "\", val);");
        writer.WriteLine ("\t\t\t\tval.Dispose ();");
        writer.WriteLine ("\t\t\t}");
      }
      writer.WriteLine ("\t\t}\n");
    }

    writer.WriteLine ();

    if (ei.signals.Count > 0) {
      foreach (SignalInfo si in ei.signals) {
        string managed_name = (si.managed_name != null) ? si.managed_name : PropToCamelCase (si.name);

        if (si.parameters.Count > 0) {
          writer.WriteLine ("\t\tpublic delegate void " + managed_name + "Handler (object o, " + managed_name + "Args args);\n");

          writer.WriteLine ("\t\tpublic class " + managed_name + "Args : Gst.GLib.SignalArgs {");
          for (int i = 0; i < si.parameters.Count; i++) {
            SignalParameter param = (SignalParameter) si.parameters[i];
            string managed_type = CTypeToManagedType (param.type, api_doc);
            writer.WriteLine ("\t\t\tpublic " + managed_type + " " + param.name + " {");
            writer.WriteLine ("\t\t\t\tget {");
            writer.WriteLine ("\t\t\t\t\treturn (" + managed_type + ") Args[" + i + "];");
            writer.WriteLine ("\t\t\t\t}");
            writer.WriteLine ("\t\t\t}\n");
          }
          writer.WriteLine ("\t\t}\n");

          writer.WriteLine ("\t\tpublic event " + managed_name + "Handler " + managed_name + " {");
        } else {
          writer.WriteLine ("\t\tpublic event SignalHandler " + managed_name + " {");
        }

        writer.WriteLine ("\t\t\tadd {");
        writer.WriteLine ("\t\t\t\tDynamicSignal.Connect (this, \"" + si.name + "\", value);");
        writer.WriteLine ("\t\t\t}\n");

        writer.WriteLine ("\t\t\tremove {");
        writer.WriteLine ("\t\t\t\tDynamicSignal.Disconnect (this, \"" + si.name + "\", value);");
        writer.WriteLine ("\t\t\t}");
        writer.WriteLine ("\t\t}");
      }
    }

    if (ei.actions.Count > 0) {
      foreach (SignalInfo si in ei.actions) {
        string managed_name = (si.managed_name != null) ? si.managed_name : PropToCamelCase (si.name);
        string managed_return = CTypeToManagedType (si.return_type, api_doc);

        writer.Write ("\t\tpublic " + managed_return + " " + managed_name + " (");

        for (int i = 0; i < si.parameters.Count; i++) {
          SignalParameter param = (SignalParameter) si.parameters[i];
          string managed_type = CTypeToManagedType (param.type, api_doc);

          if (i == 0)
            writer.Write (managed_type + " " + param.name);
          else
            writer.Write (", " + managed_type + " " + param.name);
        }
        writer.WriteLine (") {");

        writer.WriteLine ("\t\t\tobject[] parameters = new object[" + si.parameters.Count + "];");

        for (int i = 0; i < si.parameters.Count; i++) {
          SignalParameter param = (SignalParameter) si.parameters[i];

          writer.WriteLine ("\t\t\tparameters[" + i + "] = " + param.name + ";");
        }

        writer.WriteLine ("\t\t\treturn (" + managed_return + ") Emit (\"" + si.name + "\", parameters);");

        writer.WriteLine ("\t\t}\n");
      }
    }

    if (ei.interfaces.Count > 0) {
      foreach (string iface in ei.interfaces) {
        writer.WriteLine ("#endregion");
        writer.WriteLine ("#region Customized code");
        writer.WriteLine ("#line 1 \"" + iface + ".cs\"");
        StreamReader interface_code = System.IO.File.OpenText (interfaces_dir + "/" + iface + ".cs");
        string iface_code = interface_code.ReadToEnd ();
        writer.WriteLine (iface_code);
      }
    }

    if (enums.Count > 0) {
      foreach (EnumInfo eni in enums) {
        writer.WriteLine ("\t\t[GTypeName (\"" + eni.name + "\")]");
        if (eni.flag)
          writer.WriteLine ("\t\t[Flags]");

        string enum_name = eni.name.StartsWith (ei.gtype_name) ? eni.name.Substring (ei.gtype_name.Length) : eni.name.StartsWith ("Gst") ? eni.name.Substring (3) : eni.name;

        enum_name += "Type";

        writer.WriteLine ("\t\tpublic enum " + enum_name + " {");
        if (eni.flag) {
          foreach (FlagValue ev in eni.values) {
            writer.WriteLine ("\t\t\t" + PropToCamelCase (ev.name) + " = " + ev.value + ", ");
          }
        } else {
          foreach (EnumValue ev in eni.values) {
            writer.WriteLine ("\t\t\t" + PropToCamelCase (ev.name) + " = " + ev.value + ", ");
          }
        }
        writer.WriteLine ("\t\t}\n");
      }
    }

    if (custom_code != null) {
      writer.WriteLine ("#endregion");
      writer.WriteLine ("#region Customized code");
      writer.WriteLine ("#line 1 \"" + ei.name + ".custom\"");

      string custom_code_string = custom_code.ReadToEnd ();
      writer.WriteLine (custom_code_string);
    }

    writer.WriteLine ("#endregion");

    writer.WriteLine ("\t}\n");
  }

  public static string PropToCamelCase (string pname) {
    string ret = Char.ToUpper (pname[0]).ToString ();
    bool next_upper = false;

    for (int i = 1; i < pname.Length; i++) {
      if (pname[i] == '-') {
        next_upper = true;
      } else if (next_upper) {
        ret = ret + Char.ToUpper (pname[i]);
        next_upper = false;
      } else {
        ret = ret + pname[i];
      }
    }
    return ret;
  }

  public static int Main (string[] args) {
    if (args.Length != 5) {
      Console.Error.WriteLine ("Usage: element-gen --namespace=<namespace> --api=<api> --input=<in-filename> --customfile=<custom-file> --interfacesdir=<interfaces-dir>");
      return -1;
    }

    string ns = null;
    XmlDocument api_doc = new XmlDocument ();
    XmlDocument introspect_doc = new XmlDocument ();
    string filename = null;
    string customfile = null;
    StreamReader custom_code = null;
    string interfaces_dir = null;

    foreach (string arg in args) {

      if (arg.StartsWith ("--input=")) {
        filename = arg.Substring (8);

        try {
          Stream stream = File.OpenRead (filename);
          introspect_doc.Load (stream);
          stream.Close ();
        } catch (Exception e) {
          Console.Error.WriteLine ("Failed to load introspection XML:\n" + e.ToString ());
          return -2;
        }
      } else if (arg.StartsWith ("--customfile=")) {
        customfile = arg.Substring (13);

        try {
          custom_code = System.IO.File.OpenText (customfile);
        } catch (Exception) { } // No custom file is OK
      } else if (arg.StartsWith ("--interfacesdir=")) {
        interfaces_dir = arg.Substring (16);
      } else if (arg.StartsWith ("--api=")) {

        string api_filename = arg.Substring (6);

        try {
          Stream stream = File.OpenRead (api_filename);
          api_doc.Load (stream);
          stream.Close ();
        } catch (Exception e) {
          Console.Error.WriteLine ("Failed to load API XML:\n" + e.ToString ());
          return 1;
        }

      } else if (arg.StartsWith ("--namespace=")) {

        ns = arg.Substring (12);
      } else {
        Console.Error.WriteLine ("Usage: element-gen --namespace:<namespace> --api=<api> --input:<in-filename>");
        return 1;
      }
    }

    if (introspect_doc.DocumentElement.Name != "element") {
      Console.Error.WriteLine ("Invalid introspection XML");
      return -3;
    }

    TextWriter writer;

    try {
      writer = Console.Out;
    } catch (Exception e) {
      Console.Error.WriteLine ("Failed to open output file:\n" + e.ToString ());
      return -2;
    }

    if (IsHidden (introspect_doc.DocumentElement))
      return 0;

    writer.WriteLine ("using System;");
    writer.WriteLine ("using System.Collections;");
    writer.WriteLine ("using System.Runtime.InteropServices;");
    writer.WriteLine ("using Gst.GLib;");
    writer.WriteLine ("using Gst;");
    writer.WriteLine ("using Gst.Interfaces;");
    writer.WriteLine ();
    writer.WriteLine (String.Format ("namespace {0} ", ns) + "{");

    ElementInfo ei = new ElementInfo ();

    if (introspect_doc.DocumentElement.Attributes["name"] != null)
      ei.class_name = introspect_doc.DocumentElement.Attributes["name"].InnerText;

    foreach (XmlNode node in introspect_doc.DocumentElement) {
      if (IsHidden (node))
        continue;

      switch (node.Name) {
        case "name":
          ei.name = node.InnerText;
          break;
        case "object":
          XmlElement elt = node as XmlElement;
          ei.gtype_name = elt.GetAttribute ("name");
          ei.hierarchy = new ArrayList ();

          while (elt != null) {
            ei.hierarchy.Add (elt.GetAttribute ("name"));
            elt = elt.ChildNodes[0] as XmlElement;
          }
          break;
        case "interfaces":
          XmlElement elt2 = node as XmlElement;

          ei.interfaces = new ArrayList ();

          foreach (XmlElement iface in elt2.ChildNodes)
            ei.interfaces.Add (iface.GetAttribute ("name"));
          break;
        case "pads":
          if (!handlePads (node, ei))
            return -4;
          break;
        case "element-properties":
          if (!handleProperties (node, ei))
            return -5;
          break;
        case "element-signals":
          if (!handleSignals (node, ei))
            return -6;
          break;
        case "element-actions":
          if (!handleActions (node, ei))
            return -7;
          break;
        case "pad-templates":
        default:
          break;
      }
    }

    writeElement (writer, ei, custom_code, api_doc, interfaces_dir);

    writer.WriteLine ("}");

    return 0;
  }
}
