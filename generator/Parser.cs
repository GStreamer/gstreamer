// GtkSharp.Generation.Parser.cs - The XML Parsing engine.
//
// Author: Mike Kestner <mkestner@speakeasy.net>
//
// Copyright (c) 2001-2003 Mike Kestner
// Copyright (c) 2003 Ximian Inc.
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


namespace GtkSharp.Generation {

	using System;
	using System.Collections;
	using System.IO;
	using System.Xml;

	public class Parser  {
		const int curr_parser_version = 2;

		private XmlDocument Load (string filename)
		{
			XmlDocument doc = new XmlDocument ();

			try {
				Stream stream = File.OpenRead (filename);
				doc.Load (stream);
				stream.Close ();
			} catch (XmlException e) {
				Console.WriteLine ("Invalid XML file.");
				Console.WriteLine (e);
				doc = null;
			}

			return doc;
		}

		public IGeneratable[] Parse (string filename)
		{
			XmlDocument doc = Load (filename);
			if (doc == null)
				return null;

			XmlElement root = doc.DocumentElement;

			if ((root == null) || !root.HasChildNodes) {
				Console.WriteLine ("No Namespaces found.");
				return null;
			}

			int parser_version;
			if (root.HasAttribute ("parser_version")) {
				try {
					parser_version = int.Parse (root.GetAttribute ("parser_version"));
				} catch {
					Console.WriteLine ("ERROR: Unable to parse parser_version attribute value \"{0}\" to a number. Input file {1} will be ignored", root.GetAttribute ("parser_version"), filename);
					return null;
				}
			} else
				parser_version = 1;

			if (parser_version > curr_parser_version)
				Console.WriteLine ("WARNING: The input file {0} was created by a parser that was released after this version of the generator. Consider updating the code generator if you experience problems.", filename);

			ArrayList gens = new ArrayList ();

			foreach (XmlNode child in root.ChildNodes) {
				XmlElement elem = child as XmlElement;
				if (elem == null)
					continue;

				switch (child.Name) {
				case "namespace":
					gens.AddRange (ParseNamespace (elem));
					break;
				case "symbol":
					gens.Add (ParseSymbol (elem));
					break;
				default:
					Console.WriteLine ("Parser::Parse - Unexpected child node: " + child.Name);
					break;
				}
			}

			return (IGeneratable[]) gens.ToArray (typeof (IGeneratable));
		}

		private ArrayList ParseNamespace (XmlElement ns)
		{
			ArrayList result = new ArrayList ();

			foreach (XmlNode def in ns.ChildNodes) {

				XmlElement elem = def as XmlElement;
				if (elem == null)
					continue;

				if (elem.HasAttribute("hidden"))
					continue;

				bool is_opaque = false;
				if (elem.GetAttribute ("opaque") == "true" ||
				    elem.GetAttribute ("opaque") == "1")
					is_opaque = true;

				switch (def.Name) {
				case "alias":
					string aname = elem.GetAttribute("cname");
					string atype = elem.GetAttribute("type");
					if ((aname == "") || (atype == ""))
						continue;
					result.Add (new AliasGen (aname, atype));
					break;
				case "boxed":
					result.Add (is_opaque ? new OpaqueGen (ns, elem) as object : new BoxedGen (ns, elem) as object);
					break;
				case "callback":
					result.Add (new CallbackGen (ns, elem));
					break;
				case "enum":
					result.Add (new EnumGen (ns, elem));
					break;
				case "interface":
					result.Add (new InterfaceGen (ns, elem));
					break;
				case "object":
					result.Add (new ObjectGen (ns, elem));
					break;
				case "class":
					result.Add (new ClassGen (ns, elem));
					break;
				case "struct":
					result.Add (is_opaque ? new OpaqueGen (ns, elem) as object : new StructGen (ns, elem) as object);
					break;
				default:
					Console.WriteLine ("Parser::ParseNamespace - Unexpected node: " + def.Name);
					break;
				}
			}

			return result;
		}

		private IGeneratable ParseSymbol (XmlElement symbol)
		{
			string type = symbol.GetAttribute ("type");
			string cname = symbol.GetAttribute ("cname");
			string name = symbol.GetAttribute ("name");
			IGeneratable result = null;

			if (type == "simple") {
				if (symbol.HasAttribute ("default_value"))
					result = new SimpleGen (cname, name, symbol.GetAttribute ("default_value"));
				else {
					Console.WriteLine ("Simple type element " + cname + " has no specified default value");
					result = new SimpleGen (cname, name, String.Empty);
				}
			} else if (type == "manual")
				result = new ManualGen (cname, name);
			else if (type == "alias")
				result = new AliasGen (cname, name);
			else if (type == "marshal") {
				string mtype = symbol.GetAttribute ("marshal_type");
				string call = symbol.GetAttribute ("call_fmt");
				string from = symbol.GetAttribute ("from_fmt");
				result = new MarshalGen (cname, name, mtype, call, from);
			} else
				Console.WriteLine ("Parser::ParseSymbol - Unexpected symbol type " + type);

			return result;
		}
	}
}
