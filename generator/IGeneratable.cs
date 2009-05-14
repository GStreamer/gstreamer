// GtkSharp.Generation.IGeneratable.cs - Interface to generate code for a type.
//
// Author: Mike Kestner <mkestner@novell.com>
//
// Copyright (c) 2001 Mike Kestner
// Copyright (c) 2007 Novell, Inc.
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

	public interface IGeneratable  {

		// The C name of the generatable
		string CName {get;}

		// The (short) C# name of the generatable
		string Name {get;}

		// The fully-qualified C# name of the generatable
		string QualifiedName {get;}

		// The type (possibly including "ref" or "out") to use in the import
		// signature when passing this generatable to unmanaged code
		string MarshalType {get;}

		// The type to use as the return type in an import signature when
		// receiving this generatable back from unmanaged code
		string MarshalReturnType {get;}

		// The type to use in a managed callback signature when returning this
		// generatable to unmanaged code
		string ToNativeReturnType {get;}

		// The value returned by callbacks that are interrupted prematurely
		// by managed exceptions or other conditions where an appropriate
		// value can't be otherwise obtained.
		string DefaultValue {get;}

		// Generates an expression to convert var_name to MarshalType
		string CallByName (string var_name);

		// Generates an expression to convert var from MarshalType
		string FromNative (string var);

		// Generates an expression to convert var from MarshalReturnType
		string FromNativeReturn (string var);

		// Generates an expression to convert var to ToNativeReturnType
		string ToNativeReturn (string var);

		bool Validate ();

		void Generate ();

		void Generate (GenerationInfo gen_info);
	}
}
