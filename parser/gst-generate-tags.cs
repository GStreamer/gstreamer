using System;
using System.IO;

public class GenerateTags {
  public static int Main (string[] args) {
    if (args.Length != 3 && args.Length != 4) {
      Console.WriteLine ("usage: gst-generate-tags --header=<filename> --namespace=<namespace> --class=<name> [--only-body]");
      return 1;
    }

    StreamReader header = null;
    string ns = "Gst";
    string cls = "Tags";
    bool only_body = false;

    foreach (string arg in args) {

      if (arg.StartsWith ("--header=")) {

        string filename = arg.Substring (9);

        try {
          header = new StreamReader (filename);
        } catch (Exception e) {
          Console.WriteLine ("Invalid header file.");
          Console.WriteLine (e);
          return 2;
        }
      } else if (arg.StartsWith ("--namespace=")) {
        ns = arg.Substring (12);
      } else if (arg.StartsWith ("--class=")) {
        cls = arg.Substring (8);
      } else if (arg.StartsWith ("--only-body")) {
        only_body = true;
      } else {
        Console.WriteLine ("Invalid argument '" + arg + "'");
        return 3;
      }
    }

    if (!only_body) {
      Console.WriteLine ("namespace " + ns + " {");
      Console.WriteLine ("\tpublic static class " + cls + " {");
    }

    string line;
    while ( (line = header.ReadLine ()) != null) {
      if (!line.StartsWith ("#define GST_TAG_"))
        continue;

      string tag_name = line.Substring (16);
      string tag_string = tag_name.Substring (tag_name.IndexOf (' '));
      tag_name = tag_name.Substring (0, tag_name.IndexOf (' '));
      if (tag_name.IndexOf ('(') != -1)
        continue;

      /* FIXME: This is not exactly optimal */
      tag_name = tag_name.ToLower ();
      string tag_tmp = new String (new char[] {tag_name[0]}).ToUpper ();
      for (int i = 1; i < tag_name.Length; i++) {
        if (tag_name[i-1] == '_') {
          tag_tmp += (new String (new char[] {tag_name[i]})).ToUpper ();
        } else {
          tag_tmp += (new String (new char[] {tag_name[i]}));
        }
      }
      tag_name = tag_tmp;
      tag_name = tag_name.Replace ("_", String.Empty);

      tag_string = tag_string.Trim ();
      if (tag_string.IndexOf (' ') != -1)
        tag_string = tag_string.Substring (0, tag_string.IndexOf (' '));
      tag_string = tag_string.Trim ();
      if (tag_string[0] != '"' || tag_string[tag_string.Length-1] != '"') {
        Console.WriteLine ("Parse error");
        return 4;
      }
      tag_string = tag_string.Substring (1, tag_string.Length - 2);

      Console.WriteLine ("\t\t public const string " + tag_name + " = \"" + tag_string + "\";");


    }

    if (!only_body) {
      Console.WriteLine ("\t}");
      Console.WriteLine ("}");
    }

    return 0;
  }
}
