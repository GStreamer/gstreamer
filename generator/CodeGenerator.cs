// GtkSharp.Generation.CodeGenerator.cs - The main code generation engine.
//
// Author: Mike Kestner <mkestner@speakeasy.net>
//
// Copyright (c) 2001-2003 Mike Kestner 
// Copyright (c) 2003-2004 Novell Inc.
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
	using System.Xml;

	public class CodeGenerator  {

		public static int Main (string[] args)
		{
			if (args.Length < 2) {
				Console.WriteLine ("Usage: codegen --generate <filename1...>");
				return 0;
			}

			bool generate = false;
			string dir = "";
			string custom_dir = "";
			string assembly_name = "";
			string glue_filename = "";
			string glue_includes = "";
			string gluelib_name = "";

			SymbolTable table = SymbolTable.Table;
			ArrayList gens = new ArrayList ();
			foreach (string arg in args) {
				string filename = arg;
				if (arg == "--generate") {
					generate = true;
					continue;
				} else if (arg == "--include") {
					generate = false;
					continue;
				} else if (arg.StartsWith ("-I:")) {
					generate = false;
					filename = filename.Substring (3);
				} else if (arg.StartsWith ("--outdir=")) {
					generate = false;
					dir = arg.Substring (9);
					continue;
				} else if (arg.StartsWith ("--customdir=")) {
					generate = false;
					custom_dir = arg.Substring (12);
					continue;
				} else if (arg.StartsWith ("--assembly-name=")) {
					generate = false;
					assembly_name = arg.Substring (16);
					continue;
				} else if (arg.StartsWith ("--glue-filename=")) {
					generate = false;
					glue_filename = arg.Substring (16);
					continue;
				} else if (arg.StartsWith ("--glue-includes=")) {
					generate = false;
					glue_includes = arg.Substring (16);
					continue;
				} else if (arg.StartsWith ("--gluelib-name=")) {
					generate = false;
					gluelib_name = arg.Substring (15);
					continue;
				}

				Parser p = new Parser ();
				IGeneratable[] curr_gens = p.Parse (filename);
				table.AddTypes (curr_gens);
				if (generate)
					gens.AddRange (curr_gens);
			}

			// Now that everything is loaded, validate all the to-be-
			// generated generatables and then remove the invalid ones.
			ArrayList invalids = new ArrayList ();
			foreach (IGeneratable gen in gens) {
				if (!gen.Validate ())
					invalids.Add (gen);
			}
			foreach (IGeneratable gen in invalids)
				gens.Remove (gen);

			GenerationInfo gen_info = null;
			if (dir != "" || assembly_name != "" || glue_filename != "" || glue_includes != "" || gluelib_name != "")
				gen_info = new GenerationInfo (dir, custom_dir, assembly_name, glue_filename, glue_includes, gluelib_name);
			
			foreach (IGeneratable gen in gens) {
				if (gen_info == null)
					gen.Generate ();
				else
					gen.Generate (gen_info);
			}

			ObjectGen.GenerateMappers ();

			if (gen_info != null)
				gen_info.CloseGlueWriter ();

			Statistics.Report();
			return 0;
		}
	}
}
