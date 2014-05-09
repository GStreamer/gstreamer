/*
 * GStreamer
 * Copyright (C) 2009 Luc Deschenaux <luc.deschenaux@freesurf.ch>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstglshadervariables.h"

#if !defined(strtok_r) && defined(G_OS_WIN32)
#if defined(_MSC_VER)
#define strtok_r strtok_s
#else
#define strtok_r(s,d,p) strtok(s,d)
#endif
#endif

#define trimleft(s,chars) while(s[0] && strchr(chars,s[0])) ++s;
#define trimright(s,chars) { \
	char *end; \
	end=s+strlen(s)-1; \
	while(end>=s && strchr(chars,end[0])) (end--)[0]=0; \
}

const char *gst_gl_shadervariable_datatype[] = {
  "bool",
  "int",
  "uint",
  "float",
  "vec2",
  "vec3",
  "vec4",
  "bvec2",
  "bvec3",
  "bvec4",
  "ivec2",
  "ivec3",
  "ivec4",
  "uvec2",
  "uvec3",
  "uvec4",
  "mat2",
  "mat3",
  "mat4",
  "mat2x2",
  "mat2x3",
  "mat2x4",
  "mat3x2",
  "mat3x3",
  "mat3x4",
  "mat4x2",
  "mat4x3",
  "mat4x4",
  0
};

typedef enum
{
  _bool,
  _int,
  _uint,
  _float,
  _vec2,
  _vec3,
  _vec4,
  _bvec2,
  _bvec3,
  _bvec4,
  _ivec2,
  _ivec3,
  _ivec4,
  _uvec2,
  _uvec3,
  _uvec4,
  _mat2,
  _mat3,
  _mat4,
  _mat2x2,
  _mat2x3,
  _mat2x4,
  _mat3x2,
  _mat3x3,
  _mat3x4,
  _mat4x2,
  _mat4x3,
  _mat4x4,
  _datatypecount
} gst_gl_shadervariable_datatypeindex;

typedef struct gst_gl_shadervariable_desc
{
  gst_gl_shadervariable_datatypeindex type;
  char *name;
  int arraysize;
  int count;
  void *value;

} gst_gl_shadervariable_desc;

char *parsename (char **varname, int *arraysize, char **saveptr);
char *parsevalue (char *value, char *_saveptr,
    struct gst_gl_shadervariable_desc *ret);
char *vec_parsevalue (int n, char *value, char *_saveptr,
    struct gst_gl_shadervariable_desc *ret);
char *bvec_parsevalue (int n, char *value, char *_saveptr,
    struct gst_gl_shadervariable_desc *ret);
char *ivec_parsevalue (int n, char *value, char *_saveptr,
    struct gst_gl_shadervariable_desc *ret);
char *uvec_parsevalue (int n, char *value, char *_saveptr,
    struct gst_gl_shadervariable_desc *ret);
char *mat_parsevalue (int n, int m, char *value, char *_saveptr,
    struct gst_gl_shadervariable_desc *ret);

/*
	Function:
		gst_gl_shadervariables_parse

	Description:
		Parse uniform variables declarations and set their values in
		the specified shader.

	Arguments:

		GstGLShader *shader:
			Shader in which variables are to be set.

		char *variables:
			The text to be parsed.

		int (*_setvariable)():
			Defaults to gst_gl_shadervariable_set().
			You can specify here a user function for managing the
			parsed variable description.

	return values:
		 0: 	No error.
		-1:	Error.

*/

int
gst_gl_shadervariables_parse (GstGLShader * shader, char *variables,
    int (*_setvariable) (GstGLShader * shader,
        struct gst_gl_shadervariable_desc * v))
{
  char *p = 0;
  char *p0;
  char *e;
  char e1 = 0;
  char *t = 0;
  char *varname;
  char *vartype;
  char *varvalue;
  int arraysize = 0;
  char *saveptr = variables;
  int line = 1;
  char *lim;
  int i;
  int len;
  struct gst_gl_shadervariable_desc ret;

  if (!_setvariable) {
    _setvariable = gst_gl_shadervariable_set;
  }

  if (!variables)
    return 0;

  p0 = variables;
  trimright (p0, " \t\n");
  lim = variables + strlen (variables);
  e = strchr (p0, ';');
  while (p0 < lim) {

    if (!e) {
      if (p0[0])
        goto parse_error;
      break;
    }

    e1 = e[1];
    e[1] = 0;
    p = g_strdup (p0);
    e[1] = e1;

    trimright (p, " \t");
    trimleft (p, " \t\n");

    t = strtok_r (p, " \t", &saveptr);
    if (!t)
      goto parse_error;
    trimleft (t, " \t");
    trimright (t, " \t\n");

    if (t[0]) {

      if (!strcmp (t, "const")) {
        t = strtok_r (0, " \t", &saveptr);
        if (!t)
          goto parse_error;
        trimleft (t, " \t");
        if (!t[0])
          goto parse_error;
      }
      // parse data type
      for (i = 0; i < _datatypecount; ++i) {
        if (!strcmp (t, gst_gl_shadervariable_datatype[i])) {
          ret.type = (gst_gl_shadervariable_datatypeindex) i;
          break;
        }
      }
      if (i == _datatypecount)
        goto parse_error;

      vartype = g_strdup (t);
      GST_INFO ("vartype : '%s'\n", vartype);

      trimleft (saveptr, " \t");
      t = saveptr;
      if (*saveptr == '=')
        goto parse_error;

      // parse variable name and array size
      t = parsename (&varname, &arraysize, &saveptr);
      if (t)
        goto parse_error;

      trimright (varname, " \t");
      GST_INFO ("varname : '%s'\n", varname);
      GST_INFO ("arraysize : %d\n", arraysize);

      // check type cast after assignement operator
      t = strtok_r (0, "(", &saveptr);
      if (!t)
        goto parse_error;
      trimleft (t, " \t");
      trimright (t, " \t");

      if (arraysize) {
        char *s = g_malloc (strlen (vartype) + 32);
        sprintf (s, "%s[%d]", vartype, arraysize);
        if (strcmp (t, s)) {
          g_free (s);
          goto parse_error;
        }
      } else {
        if (strcmp (t, vartype))
          goto parse_error;
      }

      // extract variable value
      t = strtok_r (0, ";", &saveptr);
      if (!t)
        goto parse_error;
      trimleft (t, " \t");
      trimright (t, " \t");

      if (!t[0])
        goto parse_error;
      if (*(saveptr - 2) != ')')
        goto parse_error;
      *(saveptr - 2) = 0;
      if (!t[0])
        goto parse_error;

      varvalue = g_strdup (t);
      GST_INFO ("value: %s\n\n", varvalue);

      t = saveptr;
      if (t[0])
        goto parse_error;

      // parse variable value
      len = strlen (varvalue);
      ret.name = varname;
      ret.arraysize = arraysize;
      t = parsevalue (varvalue, saveptr, &ret);
      if (t) {
        t -= len;
        goto parse_error;
      }
      // set variable value
      _setvariable (shader, &ret);

      fflush (0);
    }
    // Tell me why we cannot free(p) whithout segfault.
    //g_free(p);
    p0 = e + 1;
    ++line;
    e = strchr (p0, ';');
  }

  return 0;

parse_error:
  if (!t) {
    t = saveptr;
  }
  if (!e) {
    t = p = p0;
  } else {
    e[1] = 0;
    trimleft (p0, " \t\n");
    GST_ERROR ("\n%s", p0);
    e[1] = e1;
  }
  GST_ERROR ("parse error on line %d, position %ld (%s)", line, (glong) (t - p),
      t);
  return -1;
}

/*
	Function:
		parsename

	Description:
		Parse text between the data type and the assignement operator
		(ie: variable name and array size).

	Arguments:

		char **varname:
			Text to parse.

		int *arraysize:
			Pointer to array size. Set to 0 if no array.

		char **saveptr:
			Address of char *saveptr for strtok_r()

	return values:
		 0: 	No error.
		!0:	Pointer to parse error.

*/

char *
parsename (char **varname, int *arraysize, char **saveptr)
{
  char *t;
  char *i;
  gint j;

  *arraysize = 0;
  t = strtok_r (0, "=", saveptr);
  if (!t)
    return *saveptr;

  trimleft (t, " \t");
  trimright (t, " \t");

  i = strchr (t, '[');
  if (!i) {                     // not an array
    if (!t[0])
      return t;
    for (j = 0; j < (gint) strlen (t); ++j) {
      if (!strchr (VALID_VARNAME_CHARS, t[j]))
        return t + j;
    }
    *varname = g_strdup (t);
  } else {                      // is an array

    char *i2;
    char *c;

    i2 = strchr (i + 1, ']');
    if (!i2)
      return i + 1;
    *i = 0;

    if (!t[0])
      return t;
    for (j = 0; j < (gint) strlen (t); ++j) {
      if (!strchr (VALID_VARNAME_CHARS, t[j]))
        return t;
    }

    *varname = g_strdup (t);
    *i = '[';

    for (c = i + 1; c < i2; ++c)
      if (*c < '0' || *c > '9')
        return c;

    *i2 = 0;
    *arraysize = atoi (i + 1);
    *i2 = ']';

    if (!*arraysize)
      return i + 1;
  }

  return 0;
}

/*
	Function:
		gst_gl_shadervariable_set

	Description:
		Set variable value in the specified shader

	Arguments:

		GstGlShader *shader:
			The shader where to set the variable.

		struct gst_gl_shadervariable_desc *ret:
			The variable description.

	return values:
		 0: 	No error.
		!0:	Variable type unknown/incorrect.

*/

int
gst_gl_shadervariable_set (GstGLShader * shader,
    struct gst_gl_shadervariable_desc *ret)
{

  switch (ret->type) {
    case _bool:
      if (ret->arraysize) {
        gst_gl_shader_set_uniform_1iv (shader, ret->name, ret->count,
            (int *) ret->value);
      } else {
        gst_gl_shader_set_uniform_1i (shader, ret->name,
            ((int *) ret->value)[0]);
      }
      break;
    case _int:
      if (ret->arraysize) {
        gst_gl_shader_set_uniform_1iv (shader, ret->name, ret->count,
            (int *) ret->value);
      } else {
        gst_gl_shader_set_uniform_1i (shader, ret->name,
            ((int *) ret->value)[0]);
      }
      break;

    case _uint:
      if (ret->arraysize) {
        gst_gl_shader_set_uniform_1iv (shader, ret->name, ret->count,
            (int *) ret->value);
      } else {
        gst_gl_shader_set_uniform_1i (shader, ret->name,
            ((unsigned int *) ret->value)[0]);
      }
      break;

    case _float:
      if (ret->arraysize) {
        gst_gl_shader_set_uniform_1fv (shader, ret->name, ret->count,
            (float *) ret->value);
      } else {
        gst_gl_shader_set_uniform_1f (shader, ret->name,
            ((float *) ret->value)[0]);
      }
      break;

    case _vec2:
      if (ret->arraysize) {
        gst_gl_shader_set_uniform_2fv (shader, ret->name, ret->count,
            (float *) ret->value);
      } else {
        gst_gl_shader_set_uniform_2f (shader, ret->name,
            ((float *) ret->value)[0], ((float *) ret->value)[1]);
      }
      break;

    case _bvec2:
    case _ivec2:
    case _uvec2:
      if (ret->arraysize) {
        gst_gl_shader_set_uniform_2iv (shader, ret->name, ret->count,
            (int *) ret->value);
      } else {
        gst_gl_shader_set_uniform_2i (shader, ret->name,
            ((int *) ret->value)[0], ((int *) ret->value)[1]);
      }
      break;

    case _vec3:
      if (ret->arraysize) {
        gst_gl_shader_set_uniform_3fv (shader, ret->name, ret->count,
            (float *) ret->value);
      } else {
        gst_gl_shader_set_uniform_3f (shader, ret->name,
            ((float *) ret->value)[0], ((float *) ret->value)[1],
            ((float *) ret->value)[2]);
      }
      break;

    case _bvec3:
    case _ivec3:
    case _uvec3:
      if (ret->arraysize) {
        gst_gl_shader_set_uniform_3iv (shader, ret->name, ret->count,
            (int *) ret->value);
      } else {
        gst_gl_shader_set_uniform_3i (shader, ret->name,
            ((int *) ret->value)[0], ((int *) ret->value)[1],
            ((int *) ret->value)[2]);
      }
      break;

    case _vec4:
      if (ret->arraysize) {
        gst_gl_shader_set_uniform_4fv (shader, ret->name, ret->count,
            (float *) ret->value);
      } else {
        gst_gl_shader_set_uniform_4f (shader, ret->name,
            ((float *) ret->value)[0], ((float *) ret->value)[1],
            ((float *) ret->value)[2], ((float *) ret->value)[3]);
      }
      break;

    case _bvec4:
    case _ivec4:
    case _uvec4:
      if (ret->arraysize) {
        gst_gl_shader_set_uniform_4iv (shader, ret->name, ret->count,
            (int *) ret->value);
      } else {
        gst_gl_shader_set_uniform_4i (shader, ret->name,
            ((int *) ret->value)[0], ((int *) ret->value)[1],
            ((int *) ret->value)[2], ((int *) ret->value)[3]);
      }
      break;

    case _mat2:
    case _mat2x2:
      gst_gl_shader_set_uniform_matrix_2fv (shader, ret->name, ret->count, 0,
          (float *) ret->value);
      break;

    case _mat3:
    case _mat3x3:
      gst_gl_shader_set_uniform_matrix_3fv (shader, ret->name, ret->count, 0,
          (float *) ret->value);
      break;

    case _mat4:
    case _mat4x4:
      gst_gl_shader_set_uniform_matrix_4fv (shader, ret->name, ret->count, 0,
          (float *) ret->value);
      break;

#if GST_GL_HAVE_OPENGL
    case _mat2x3:
      gst_gl_shader_set_uniform_matrix_2x3fv (shader, ret->name, ret->count, 0,
          (float *) ret->value);
      break;

    case _mat3x2:
      gst_gl_shader_set_uniform_matrix_3x2fv (shader, ret->name, ret->count, 0,
          (float *) ret->value);
      break;

    case _mat2x4:
      gst_gl_shader_set_uniform_matrix_2x4fv (shader, ret->name, ret->count, 0,
          (float *) ret->value);
      break;

    case _mat4x2:
      gst_gl_shader_set_uniform_matrix_4x2fv (shader, ret->name, ret->count, 0,
          (float *) ret->value);
      break;

    case _mat3x4:
      gst_gl_shader_set_uniform_matrix_3x4fv (shader, ret->name, ret->count, 0,
          (float *) ret->value);
      break;

    case _mat4x3:
      gst_gl_shader_set_uniform_matrix_4x3fv (shader, ret->name, ret->count, 0,
          (float *) ret->value);
      break;
#endif

    default:
      return -1;
  }
  return 0;
}

/*
	Function:
		parsevalue

	Description:
		Parse text coming after the assignement operator for scalar
		variables or call the appropriate subroutine to parse vector
		variables.

	Arguments:

		char *value:
			Text to be parsed.

		char *_saveptr:
			Index of end of value.

		struct gst_gl_shadervariable_desc *ret:
			The variable description to be completed
			At input time it contains the data type index (type),
			variable name (name) and array size (arraysize).

	return values:
		 0: 	No error.
		!0:	Pointer to parse error.

*/

char *
parsevalue (char *value, char *_saveptr, struct gst_gl_shadervariable_desc *ret)
{

  int i, j;
  char *t;
  char *saveptr = value;

  switch (ret->type) {
    case _bool:
      ret->count = (ret->arraysize) ? ret->arraysize : 1;
      if (ret->count == 1) {    // no array
        if (strcmp (value, "true") && strcmp (value, "false"))
          return _saveptr;
        ret->value = (void *) g_malloc (sizeof (int));
        ((int *) ret->value)[0] = strcmp (value, "false");

      } else {                  // array
        ret->value = g_malloc (sizeof (int *) * ret->count);
        t = strtok_r (value, ",", &saveptr);
        for (i = 0; i < ret->count; ++i) {
          if (!t)
            return _saveptr + (saveptr - value);
          trimleft (t, " \t");
          trimright (t, " \t");
          if (strcmp (t, "true") && strcmp (t, "false"))
            return _saveptr + (saveptr - t);
          ((int *) ret->value)[i] = strcmp (t, "false");
          t = strtok_r (0, ",", &saveptr);
        }
      }
      break;
    case _int:
      ret->count = (ret->arraysize) ? ret->arraysize : 1;
      if (ret->count == 1) {
        for (j = 0; j < (gint) strlen (value); ++j) {
          if (!strchr ("-0123456789", value[j]))
            return _saveptr + j;
        }
        ret->value = (void *) g_malloc (sizeof (int));
        *((int *) ret->value) = atoi (value);

      } else {
        ret->value = g_malloc (sizeof (int) * ret->count);
        t = strtok_r (value, ",", &saveptr);

        for (i = 0; i < ret->count; ++i) {

          if (!t)
            return _saveptr + (saveptr - value);

          trimleft (t, " \t");
          trimright (t, " \t");
          if (!t[0])
            return _saveptr + (saveptr - t);

          for (j = 0; j < (gint) strlen (value); ++j) {
            if (!strchr ("-0123456789", value[j]))
              return _saveptr + (saveptr - t) + j;
          }

          ((int *) ret->value)[i] = atoi (t);
          t = strtok_r (0, ",", &saveptr);
        }
      }
      break;

    case _uint:
      ret->count = (ret->arraysize) ? ret->arraysize : 1;
      if (ret->count == 1) {
        for (j = 0; j < (gint) strlen (value); ++j) {
          if (!strchr ("0123456789", value[j]))
            return _saveptr + j;
        }
        ret->value = (void *) g_malloc (sizeof (unsigned int));
        *((unsigned int *) ret->value) = atoi (value);

      } else {
        ret->value = g_malloc (sizeof (unsigned int) * ret->count);
        t = strtok_r (value, ",", &saveptr);

        for (i = 0; i < ret->count; ++i) {

          if (!t)
            return _saveptr + (saveptr - value);

          trimleft (t, " \t");
          trimright (t, " \t");
          if (!t[0])
            return _saveptr + (saveptr - t);

          for (j = 0; j < (gint) strlen (value); ++j) {
            if (!strchr ("0123456789", value[j]))
              return _saveptr + (saveptr - t) + j;
          }

          ((unsigned int *) ret->value)[i] = atoi (t);
          t = strtok_r (0, ",", &saveptr);
        }
      }
      break;

    case _float:
      ret->count = (ret->arraysize) ? ret->arraysize : 1;
      if (ret->count == 1) {
        for (j = 0; j < (gint) strlen (value); ++j) {
          if (!strchr ("0123456789.-", value[j]))
            return _saveptr + j;
        }
        ret->value = (void *) g_malloc (sizeof (float));
        *((float *) ret->value) = (float) strtod (value, NULL);

      } else {
        ret->value = g_malloc (sizeof (float) * ret->count);
        t = strtok_r (value, ",", &saveptr);

        for (i = 0; i < ret->count; ++i) {

          if (!t)
            return _saveptr + (saveptr - value);

          trimleft (t, " \t");
          trimright (t, " \t");
          if (!t[0])
            return _saveptr + (saveptr - t);

          for (j = 0; j < (gint) strlen (value); ++j) {
            if (!strchr ("0123456789.-", value[j]))
              return _saveptr + (saveptr - t) + j;
          }

          ((float *) ret->value)[i] = (float) strtod (t, NULL);
          t = strtok_r (0, ",", &saveptr);
        }
      }
      break;

    case _vec2:
      return vec_parsevalue (2, value, _saveptr, ret);
      break;

    case _bvec2:
      return bvec_parsevalue (2, value, _saveptr, ret);
      break;

    case _ivec2:
      return ivec_parsevalue (2, value, _saveptr, ret);
      break;

    case _uvec2:
      return uvec_parsevalue (2, value, _saveptr, ret);
      break;

    case _vec3:
      return vec_parsevalue (3, value, _saveptr, ret);
      break;

    case _bvec3:
      return bvec_parsevalue (3, value, _saveptr, ret);
      break;

    case _ivec3:
      return uvec_parsevalue (3, value, _saveptr, ret);
      break;

    case _uvec3:
      return uvec_parsevalue (3, value, _saveptr, ret);
      break;

    case _vec4:
      return vec_parsevalue (4, value, _saveptr, ret);
      break;

    case _bvec4:
      return bvec_parsevalue (4, value, _saveptr, ret);
      break;

    case _ivec4:
      return ivec_parsevalue (4, value, _saveptr, ret);
      break;

    case _uvec4:
      return uvec_parsevalue (4, value, _saveptr, ret);
      break;

    case _mat2:
    case _mat2x2:
      return mat_parsevalue (2, 2, value, _saveptr, ret);
      break;

    case _mat2x3:
      return mat_parsevalue (2, 3, value, _saveptr, ret);
      break;

    case _mat3x2:
      return mat_parsevalue (3, 2, value, _saveptr, ret);
      break;

    case _mat2x4:
      return mat_parsevalue (2, 4, value, _saveptr, ret);
      break;

    case _mat4x2:
      return mat_parsevalue (4, 2, value, _saveptr, ret);
      break;

    case _mat3:
    case _mat3x3:
      return mat_parsevalue (3, 3, value, _saveptr, ret);
      break;

    case _mat3x4:
      return mat_parsevalue (3, 4, value, _saveptr, ret);
      break;

    case _mat4x3:
      return mat_parsevalue (4, 3, value, _saveptr, ret);
      break;

    case _mat4:
    case _mat4x4:
      return mat_parsevalue (4, 4, value, _saveptr, ret);
      break;

    default:
      break;
  }
  return 0;
}

/*
	Function:
		vec_parsevalue

	Description:
		Parse text coming after the assignement operator for vec
		type variables.

	Arguments:

		int n;
			Vector dimension.

		char *value:
			Text to be parsed.

		char *_saveptr:
			Index of end of value.

		struct gst_gl_shadervariable_desc *ret:
			The variable description to be completed
			At input time it contains the data type index (type),
			variable name (name) and array size (arraysize).

	return values:
		 0: 	No error.
		!0:	Pointer to parse error.

*/

char *
vec_parsevalue (int n, char *value, char *_saveptr,
    struct gst_gl_shadervariable_desc *ret)
{

  int i;
  int j;
  int k;
  char *saveptr = value;
  char *saveptr2;
  char *t;
  char *u;

  ret->count = (ret->arraysize) ? ret->arraysize * n : n;
  ret->value = g_malloc (sizeof (float) * ret->count);

  if (!ret->arraysize) {
    t = strtok_r (value, ",", &saveptr);

    for (i = 0; i < ret->count; ++i) {

      if (!t)
        return _saveptr + (saveptr - value);

      trimleft (t, " \t");
      trimright (t, " \t");
      if (!t[0])
        return _saveptr + (saveptr - t);

      for (j = 0; j < (gint) strlen (value); ++j) {
        if (!strchr ("0123456789.-", value[j]))
          return _saveptr + (saveptr - t) + j;
      }

      ((float *) ret->value)[i] = (float) strtod (t, NULL);
      t = strtok_r (0, ",", &saveptr);
    }

  } else {

    saveptr2 = value;
    u = strtok_r (value, ")", &saveptr2);

    for (k = 0; k < ret->arraysize; ++k) {

      if (!u)
        return _saveptr + (saveptr2 - value);

      trimleft (u, " \t");
      trimright (u, " \t");

      if (k) {
        if (u[0] != ',')
          return _saveptr + (u - value);
        ++u;
        trimleft (u, " \t");
      }

      if (strncmp (u, gst_gl_shadervariable_datatype[ret->type],
              strlen (gst_gl_shadervariable_datatype[ret->type])))
        return _saveptr + (u - value);

      u += strlen (gst_gl_shadervariable_datatype[ret->type]);
      trimleft (u, " \t");
      if (u[0] != '(')
        return _saveptr + (u - value);
      ++u;

      t = strtok_r (u, ",", &saveptr);
      if (!t)
        return _saveptr + (u - value);

      for (i = 0; i < n; ++i) {

        trimleft (t, " \t");
        trimright (t, " \t");
        if (!t[0])
          return _saveptr + (t - value);

        for (j = 0; j < (gint) strlen (t); ++j) {
          if (!strchr ("0123456789.-", t[j]))
            return _saveptr + (t - value) + j;
        }

        ((float *) ret->value)[k * n + i] = (float) strtod (t, NULL);
        t = strtok_r (0, ",", &saveptr);
        if (i < (n - 1) && !t)
          return _saveptr + (saveptr - value);

      }
      u = strtok_r (0, ")", &saveptr2);
    }
  }
  return 0;
}

/*
	Function:
		bvec_parsevalue

	Description:
		Parse text coming after the assignement operator for bvec
		type variables.

	Arguments:

		int n;
			Vector dimension.

		char *value:
			Text to be parsed.

		char *_saveptr:
			Index of end of value.

		struct gst_gl_shadervariable_desc *ret:
			The variable description to be completed
			At input time it contains the data type index (type),
			variable name (name) and array size (arraysize).

	return values:
		 0: 	No error.
		!0:	Pointer to parse error.

*/

char *
bvec_parsevalue (int n, char *value, char *_saveptr,
    struct gst_gl_shadervariable_desc *ret)
{

  int i;
  int k;
  char *saveptr = value;
  char *saveptr2;
  char *t;
  char *u;

  ret->count = (ret->arraysize) ? ret->arraysize * n : n;
  ret->value = g_malloc (sizeof (char **) * ret->count);

  if (!ret->arraysize) {
    t = strtok_r (value, ",", &saveptr);

    for (i = 0; i < ret->count; ++i) {

      if (!t)
        return _saveptr + (saveptr - value);

      trimleft (t, " \t");
      trimright (t, " \t");
      if (!t[0])
        return _saveptr + (saveptr - t);

      if (strcmp ("true", value) || strcmp ("false", value))
        return _saveptr + (saveptr - t);

      ((int *) ret->value)[i] = strcmp (t, "false");
      t = strtok_r (0, ",", &saveptr);
    }

  } else {

    saveptr2 = value;
    u = strtok_r (value, ")", &saveptr2);

    for (k = 0; k < ret->arraysize; ++k) {

      if (!u)
        return _saveptr + (saveptr2 - value);

      trimleft (u, " \t");
      trimright (u, " \t");

      if (k) {
        if (u[0] != ',')
          return _saveptr + (u - value);
        ++u;
        trimleft (u, " \t");
      }

      if (strncmp (u, gst_gl_shadervariable_datatype[ret->type],
              strlen (gst_gl_shadervariable_datatype[ret->type])))
        return _saveptr + (u - value);

      u += strlen (gst_gl_shadervariable_datatype[ret->type]);
      trimleft (u, " \t");
      if (u[0] != '(')
        return _saveptr + (u - value);
      ++u;

      t = strtok_r (u, ",", &saveptr);
      if (!t)
        return _saveptr + (u - value);

      for (i = 0; i < n; ++i) {

        trimleft (t, " \t");
        trimright (t, " \t");
        if (!t[0])
          return _saveptr + (t - value);

        if (strcmp ("true", t) || strcmp ("false", t))
          return _saveptr + (saveptr - t);

        ((int *) ret->value)[k * n + i] = strcmp (t, "false");
        t = strtok_r (0, ",", &saveptr);
        if (i < (n - 1) && !t)
          return _saveptr + (saveptr - value);

      }
      u = strtok_r (0, ")", &saveptr2);
    }
  }
  return 0;
}

/*
	Function:
		ivec_parsevalue

	Description:
		Parse text coming after the assignement operator for ivec
		type variables.

	Arguments:

		int n;
			Vector dimension.

		char *value:
			Text to be parsed.

		char *_saveptr:
			Index of end of value.

		struct gst_gl_shadervariable_desc *ret:
			The variable description to be completed
			At input time it contains the data type index (type),
			variable name (name) and array size (arraysize).

	return values:
		 0: 	No error.
		!0:	Pointer to parse error.

*/

char *
ivec_parsevalue (int n, char *value, char *_saveptr,
    struct gst_gl_shadervariable_desc *ret)
{

  int i;
  int j;
  int k;
  char *saveptr = value;
  char *saveptr2;
  char *t;
  char *u;

  ret->count = (ret->arraysize) ? ret->arraysize * n : n;
  ret->value = g_malloc (sizeof (int) * ret->count);

  if (!ret->arraysize) {
    t = strtok_r (value, ",", &saveptr);

    for (i = 0; i < ret->count; ++i) {

      if (!t)
        return _saveptr + (saveptr - value);

      trimleft (t, " \t");
      trimright (t, " \t");
      if (!t[0])
        return _saveptr + (saveptr - t);

      for (j = 0; j < (gint) strlen (value); ++j) {
        if (!strchr ("0123456789-", value[j]))
          return _saveptr + (saveptr - t) + j;
      }

      ((int *) ret->value)[i] = atoi (t);
      t = strtok_r (0, ",", &saveptr);
    }

  } else {

    saveptr2 = value;
    u = strtok_r (value, ")", &saveptr2);

    for (k = 0; k < ret->arraysize; ++k) {

      if (!u)
        return _saveptr + (saveptr2 - value);

      trimleft (u, " \t");
      trimright (u, " \t");

      if (k) {
        if (u[0] != ',')
          return _saveptr + (u - value);
        ++u;
        trimleft (u, " \t");
      }

      if (strncmp (u, gst_gl_shadervariable_datatype[ret->type],
              strlen (gst_gl_shadervariable_datatype[ret->type])))
        return _saveptr + (u - value);

      u += strlen (gst_gl_shadervariable_datatype[ret->type]);
      trimleft (u, " \t");
      if (u[0] != '(')
        return _saveptr + (u - value);
      ++u;

      t = strtok_r (u, ",", &saveptr);
      if (!t)
        return _saveptr + (u - value);

      for (i = 0; i < n; ++i) {

        trimleft (t, " \t");
        trimright (t, " \t");
        if (!t[0])
          return _saveptr + (t - value);

        for (j = 0; j < (gint) strlen (t); ++j) {
          if (!strchr ("0123456789-", t[j]))
            return _saveptr + (t - value) + j;
        }

        ((int *) ret->value)[k * n + i] = atoi (t);
        t = strtok_r (0, ",", &saveptr);
        if (i < (n - 1) && !t)
          return _saveptr + (saveptr - value);

      }
      u = strtok_r (0, ")", &saveptr2);
    }
  }
  return 0;
}

/*
	Function:
		uvec_parsevalue

	Description:
		Parse text coming after the assignement operator for uvec
		type variables.

	Arguments:

		int n;
			Vector dimension.

		char *value:
			Text to be parsed.

		char *_saveptr:
			Index of end of value.

		struct gst_gl_shadervariable_desc *ret:
			The variable description to be completed
			At input time it contains the data type index (type),
			variable name (name) and array size (arraysize).

	return values:
		 0: 	No error.
		!0:	Pointer to parse error.

*/

char *
uvec_parsevalue (int n, char *value, char *_saveptr,
    struct gst_gl_shadervariable_desc *ret)
{

  int i;
  int j;
  int k;
  char *saveptr = value;
  char *saveptr2;
  char *t;
  char *u;

  ret->count = (ret->arraysize) ? ret->arraysize * n : n;
  ret->value = g_malloc (sizeof (unsigned int) * ret->count);

  if (!ret->arraysize) {
    t = strtok_r (value, ",", &saveptr);

    for (i = 0; i < ret->count; ++i) {

      if (!t)
        return _saveptr + (saveptr - value);

      trimleft (t, " \t");
      trimright (t, " \t");
      if (!t[0])
        return _saveptr + (saveptr - t);

      for (j = 0; j < (gint) strlen (value); ++j) {
        if (!strchr ("0123456789", value[j]))
          return _saveptr + (saveptr - t) + j;
      }

      ((unsigned int *) ret->value)[i] = atoi (t);
      t = strtok_r (0, ",", &saveptr);
    }

  } else {

    saveptr2 = value;
    u = strtok_r (value, ")", &saveptr2);

    for (k = 0; k < ret->arraysize; ++k) {

      if (!u)
        return _saveptr + (saveptr2 - value);

      trimleft (u, " \t");
      trimright (u, " \t");

      if (k) {
        if (u[0] != ',')
          return _saveptr + (u - value);
        ++u;
        trimleft (u, " \t");
      }

      if (strncmp (u, gst_gl_shadervariable_datatype[ret->type],
              strlen (gst_gl_shadervariable_datatype[ret->type])))
        return _saveptr + (u - value);

      u += strlen (gst_gl_shadervariable_datatype[ret->type]);
      trimleft (u, " \t");
      if (u[0] != '(')
        return _saveptr + (u - value);
      ++u;

      t = strtok_r (u, ",", &saveptr);
      if (!t)
        return _saveptr + (u - value);

      for (i = 0; i < n; ++i) {

        trimleft (t, " \t");
        trimright (t, " \t");
        if (!t[0])
          return _saveptr + (t - value);

        for (j = 0; j < (gint) strlen (t); ++j) {
          if (!strchr ("0123456789", t[j]))
            return _saveptr + (t - value) + j;
        }

        ((unsigned int *) ret->value)[k * n + i] = atoi (t);
        t = strtok_r (0, ",", &saveptr);
        if (i < (n - 1) && !t)
          return _saveptr + (saveptr - value);

      }
      u = strtok_r (0, ")", &saveptr2);
    }
  }
  return 0;
}

/*
	Function:
		mat_parsevalue

	Description:
		Parse text coming after the assignement operator for matrix
		type variables.

	Arguments:

		int n,m;
			Matrix dimensions.

		char *value:
			Text to be parsed.

		char *_saveptr:
			Index of end of value.

		struct gst_gl_shadervariable_desc *ret:
			The variable description to be completed
			At input time it contains the data type index (type),
			variable name (name) and array size (arraysize).

	return values:
		 0: 	No error.
		!0:	Pointer to parse error.

*/

char *
mat_parsevalue (int n, int m, char *value, char *_saveptr,
    struct gst_gl_shadervariable_desc *ret)
{

  int i;
  int j;
  int k;
  char *saveptr = value;
  char *saveptr2;
  char *t;
  char *u;

  ret->count = (ret->arraysize) ? ret->arraysize * n * m : n * m;
  ret->value = g_malloc (sizeof (float) * ret->count);

  if (!ret->arraysize) {
    t = strtok_r (value, ",", &saveptr);

    for (i = 0; i < ret->count; ++i) {

      if (!t)
        return _saveptr + (saveptr - value);

      trimleft (t, " \t");
      trimright (t, " \t");
      if (!t[0])
        return _saveptr + (saveptr - t);

      for (j = 0; j < (gint) strlen (value); ++j) {
        if (!strchr ("0123456789.-", value[j]))
          return _saveptr + (saveptr - t) + j;
      }

      ((float *) ret->value)[i] = (float) strtod (t, NULL);
      t = strtok_r (0, ",", &saveptr);
    }

  } else {

    saveptr2 = value;
    u = strtok_r (value, ")", &saveptr2);

    for (k = 0; k < ret->arraysize; ++k) {

      if (!u)
        return _saveptr + (saveptr2 - value);

      trimleft (u, " \t");
      trimright (u, " \t");

      if (k) {
        if (u[0] != ',')
          return _saveptr + (u - value);
        ++u;
        trimleft (u, " \t");
      }

      if (strncmp (u, gst_gl_shadervariable_datatype[ret->type],
              strlen (gst_gl_shadervariable_datatype[ret->type])))
        return _saveptr + (u - value);

      u += strlen (gst_gl_shadervariable_datatype[ret->type]);
      trimleft (u, " \t");
      if (u[0] != '(')
        return _saveptr + (u - value);
      ++u;

      t = strtok_r (u, ",", &saveptr);
      if (!t)
        return _saveptr + (u - value);

      for (i = 0; i < n * m; ++i) {

        trimleft (t, " \t");
        trimright (t, " \t");
        if (!t[0])
          return _saveptr + (t - value);

        for (j = 0; j < (gint) strlen (t); ++j) {
          if (!strchr ("0123456789.-", t[j]))
            return _saveptr + (t - value) + j;
        }

        ((float *) ret->value)[k * n * m + i] = (float) strtod (t, NULL);
        t = strtok_r (0, ",", &saveptr);
        if (i < (n * m - 1) && !t)
          return _saveptr + (saveptr - value);

      }
      u = strtok_r (0, ")", &saveptr2);
    }
  }
  return 0;
}
