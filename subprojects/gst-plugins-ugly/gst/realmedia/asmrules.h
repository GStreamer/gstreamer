/* GStreamer
 * Copyright (C) <2007> Wim Taymans <wim.taymans@gmail.com>
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

#ifndef __GST_ASM_RULES_H__
#define __GST_ASM_RULES_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define MAX_RULEMATCHES 16

typedef struct _GstASMNode GstASMNode;
typedef struct _GstASMRule GstASMRule;
typedef struct _GstASMRuleBook GstASMRuleBook;

typedef enum {
  GST_ASM_TOKEN_NONE,
  GST_ASM_TOKEN_EOF,

  GST_ASM_TOKEN_INT,
  GST_ASM_TOKEN_FLOAT,
  GST_ASM_TOKEN_IDENTIFIER,
  GST_ASM_TOKEN_STRING,
  
  GST_ASM_TOKEN_HASH,
  GST_ASM_TOKEN_SEMICOLON,
  GST_ASM_TOKEN_COMMA,
  GST_ASM_TOKEN_DOLLAR,

  GST_ASM_TOKEN_LPAREN,
  GST_ASM_TOKEN_RPAREN,

  GST_ASM_TOKEN_GREATER,
  GST_ASM_TOKEN_LESS,
  GST_ASM_TOKEN_GREATEREQUAL,
  GST_ASM_TOKEN_LESSEQUAL,
  GST_ASM_TOKEN_EQUAL,
  GST_ASM_TOKEN_NOTEQUAL,

  GST_ASM_TOKEN_AND,
  GST_ASM_TOKEN_OR
} GstASMToken;

typedef enum {
  GST_ASM_NODE_UNKNOWN,
  GST_ASM_NODE_VARIABLE,
  GST_ASM_NODE_INTEGER,
  GST_ASM_NODE_FLOAT,
  GST_ASM_NODE_OPERATOR
} GstASMNodeType;

typedef enum {
  GST_ASM_OP_GREATER      = GST_ASM_TOKEN_GREATER,
  GST_ASM_OP_LESS         = GST_ASM_TOKEN_LESS,
  GST_ASM_OP_GREATEREQUAL = GST_ASM_TOKEN_GREATEREQUAL,
  GST_ASM_OP_LESSEQUAL    = GST_ASM_TOKEN_LESSEQUAL,
  GST_ASM_OP_EQUAL        = GST_ASM_TOKEN_EQUAL,
  GST_ASM_OP_NOTEQUAL     = GST_ASM_TOKEN_NOTEQUAL,

  GST_ASM_OP_AND          = GST_ASM_TOKEN_AND,
  GST_ASM_OP_OR           = GST_ASM_TOKEN_OR
} GstASMOp;

struct _GstASMNode {
  GstASMNodeType  type;

  union {
    gchar   *varname;
    gint     intval;
    gfloat   floatval;
    GstASMOp optype;
  } data;

  GstASMNode     *left;
  GstASMNode     *right;
};

struct _GstASMRule {
  GstASMNode *root;
  GHashTable *props;
};

struct _GstASMRuleBook {
  const gchar *rulebook;

  guint        n_rules;
  GList       *rules;
};

G_END_DECLS

GstASMRuleBook*   gst_asm_rule_book_new     (const gchar *rulebook);
void              gst_asm_rule_book_free    (GstASMRuleBook *book);

gint              gst_asm_rule_book_match   (GstASMRuleBook *book, GHashTable *vars, 
		                             gint *rulematches);

#endif /* __GST_ASM_RULES_H__ */
