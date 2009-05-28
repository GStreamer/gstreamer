// gapi-parser.cs - parsing driver application.
//
// Author: Mike Kestner <mkestner@novell.com>
//
// Copyright (c) 2005 Novell, Inc.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of version 2 of the GNU General Public
// License as published by the Free Software Foundation.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public
// License along with this program; if not, write to the
// Free Software Foundation, Inc., 59 Temple Place - Suite 330,
// Boston, MA 02111-1307, USA.

namespace GtkSharp.Parsing {

	using System;
	using System.Collections;
	using System.IO;
	using System.Runtime.InteropServices;
	using System.Xml;

	public class Parser  {

		[DllImport ("libc")]
		static extern int system (string command);

		public static int Main (string[] args)
		{
			if (args.Length != 1) {
				Console.WriteLine ("Usage: gapi2-parser <filename>");
				return 0;
			}

			XmlDocument src_doc = new XmlDocument ();

			try {
				using (Stream stream = File.OpenRead (args [0]))
					src_doc.Load (stream);
			} catch (XmlException e) {
				Console.WriteLine ("Couldn't open source file.");
				Console.WriteLine (e);
				return 1;
			}

			XmlNode root = src_doc.DocumentElement;
			if (root.Name != "gapi-parser-input") {
				Console.WriteLine ("Improperly formatted input file: " + args [0]);
				return 1;
			}

			foreach (XmlNode apinode in root.ChildNodes) {
				if (apinode.Name != "api")
					continue;

				string outfile = (apinode as XmlElement).GetAttribute ("filename");
				string prefile = outfile + ".pre";

				if (File.Exists (prefile))
					File.Delete (prefile);

				foreach (XmlNode libnode in apinode.ChildNodes) {
					if (libnode.Name != "library")
						continue;

					string lib = (libnode as XmlElement).GetAttribute ("name");
			
					foreach (XmlNode nsnode in libnode.ChildNodes) {
						if (nsnode.Name != "namespace")
							continue;

						string ns = (nsnode as XmlElement).GetAttribute ("name");
			
						ArrayList files = new ArrayList ();
						Hashtable excludes = new Hashtable ();

						foreach (XmlNode srcnode in nsnode.ChildNodes) {
							if (!(srcnode is XmlElement))
								continue;

							XmlElement elem = srcnode as XmlElement;

							switch (srcnode.Name) {
							case "dir":
								string dir = elem.InnerXml;
								Console.Write ("<dir {0}> ", dir);
								DirectoryInfo di = new DirectoryInfo (dir);
								foreach (FileInfo file in di.GetFiles ("*.c"))
									files.Add (dir + Path.DirectorySeparatorChar + file.Name);
								foreach (FileInfo file in di.GetFiles ("*.h"))
									files.Add (dir + Path.DirectorySeparatorChar + file.Name);
								break;
							case "file":
								string incfile = elem.InnerXml;
								Console.Write ("<file {0}> ", incfile);
								files.Add (incfile);
								break;
							case "exclude":
								string excfile = elem.InnerXml;
								Console.Write ("<exclude {0}> ", excfile);
								excludes [excfile] = 1;
								break;
							case "directory":
								string dir_path = elem.GetAttribute ("path");
								Console.Write ("<directory {0}: excluding ", dir_path);
								Hashtable excs = new Hashtable ();
								foreach (XmlNode exc_node in srcnode.ChildNodes) {
									if (exc_node.Name != "exclude")
										continue;
									string excfilename = (exc_node as XmlElement).InnerXml;
									Console.Write (excfilename + " ");
									excs [excfilename] = 1;
								}
								DirectoryInfo dinfo = new DirectoryInfo (dir_path);
								foreach (FileInfo file in dinfo.GetFiles ("*.c")) {
									if (excs.Contains (file.Name))
										continue;
									files.Add (dir_path + Path.DirectorySeparatorChar + file.Name);
								}
								foreach (FileInfo file in dinfo.GetFiles ("*.h")) {
									if (excs.Contains (file.Name))
										continue;
									files.Add (dir_path + Path.DirectorySeparatorChar + file.Name);
								}
								Console.Write ("> ");
								break;
							default:
								Console.WriteLine ("Invalid source: " + srcnode.Name);
								break;
							}
						}

						Console.WriteLine ();

						if (files.Count == 0)
							continue;

						ArrayList realfiles = new ArrayList ();
						foreach (string file in files) {
							string trimfile = file.TrimEnd ();
							if (excludes.Contains (trimfile))
								continue;

							realfiles.Add (trimfile);
						}
								
						string[] filenames = (string[]) realfiles.ToArray (typeof (string));
						string pp_args = String.Join (" ", filenames);
						system ("gapi_pp.pl " + pp_args + " | gapi2xml.pl " + ns + " " + prefile + " " + lib);
					}
				}
			
				XmlDocument final = new XmlDocument ();
				final.Load (prefile);
				XmlTextWriter writer = new XmlTextWriter (outfile, null);
				writer.Formatting = Formatting.Indented;
				final.Save (writer);
				File.Delete (prefile);
			}

			return 0;
		}
	}
}
