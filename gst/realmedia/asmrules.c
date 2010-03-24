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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <string.h>
#include <stdlib.h>

#include "asmrules.h"

#define MAX_RULE_LENGTH	2048

/* define to enable some more debug */
#undef DEBUG

static GstASMNode *
gst_asm_node_new (void)
{
  GstASMNode *node;

  node = g_new0 (GstASMNode, 1);
  node->type = GST_ASM_NODE_UNKNOWN;

  return node;
}

static void
gst_asm_node_free (GstASMNode * node)
{
  if (node->left)
    gst_asm_node_free (node->left);
  if (node->right)
    gst_asm_node_free (node->right);
  if (node->type == GST_ASM_NODE_VARIABLE && node->data.varname)
    g_free (node->data.varname);
  g_free (node);
}

static gfloat
gst_asm_operator_eval (GstASMOp optype, gfloat left, gfloat right)
{
  gfloat result = 0.0;

  switch (optype) {
    case GST_ASM_OP_GREATER:
      result = (gfloat) (left > right);
      break;
    case GST_ASM_OP_LESS:
      result = (gfloat) (left < right);
      break;
    case GST_ASM_OP_GREATEREQUAL:
      result = (gfloat) (left >= right);
      break;
    case GST_ASM_OP_LESSEQUAL:
      result = (gfloat) (left <= right);
      break;
    case GST_ASM_OP_EQUAL:
      result = (gfloat) (left == right);
      break;
    case GST_ASM_OP_NOTEQUAL:
      result = (gfloat) (left != right);
      break;
    case GST_ASM_OP_AND:
      result = (gfloat) (left && right);
      break;
    case GST_ASM_OP_OR:
      result = (gfloat) (left || right);
      break;
    default:
      break;
  }
  return result;
}

static gfloat
gst_asm_node_evaluate (GstASMNode * node, GHashTable * vars)
{
  gfloat result = 0.0;

  if (node == NULL)
    return 0.0;

  switch (node->type) {
    case GST_ASM_NODE_VARIABLE:
    {
      gchar *val;

      val = g_hash_table_lookup (vars, node->data.varname);
      if (val)
        result = (gfloat) atof (val);
      break;
    }
    case GST_ASM_NODE_INTEGER:
      result = (gfloat) node->data.intval;
      break;
    case GST_ASM_NODE_FLOAT:
      result = node->data.floatval;
      break;
    case GST_ASM_NODE_OPERATOR:
    {
      gfloat left, right;

      left = gst_asm_node_evaluate (node->left, vars);
      right = gst_asm_node_evaluate (node->right, vars);

      result = gst_asm_operator_eval (node->data.optype, left, right);
      break;
    }
    default:
      break;
  }
  return result;
}

#define IS_SPACE(p) (((p) == ' ') || ((p) == '\n') || \
                     ((p) == '\r') || ((p) == '\t'))
#define IS_RULE_DELIM(p) (((p) == ',') || ((p) == ';') || ((p) == ')'))
#define IS_OPERATOR(p) (((p) == '>') || ((p) == '<') || \
                        ((p) == '=') || ((p) == '!') || \
                        ((p) == '&') || ((p) == '|'))
#define IS_NUMBER(p) ((((p) >= '0') && ((p) <= '9')) || ((p) == '.'))
#define IS_CHAR(p) (!IS_OPERATOR(ch) && !IS_RULE_DELIM(ch) && (ch != '\0'))

#define IS_OP_TOKEN(t) (((t) == GST_ASM_TOKEN_AND) || ((t) == GST_ASM_TOKEN_OR))
#define IS_COND_TOKEN(t) (((t) == GST_ASM_TOKEN_LESS) || ((t) == GST_ASM_TOKEN_LESSEQUAL) || \
		((t) == GST_ASM_TOKEN_GREATER) || ((t) == GST_ASM_TOKEN_GREATEREQUAL) || \
		((t) == GST_ASM_TOKEN_EQUAL) || ((t) == GST_ASM_TOKEN_NOTEQUAL))

typedef struct
{
  const gchar *buffer;
  gint pos;
  gchar ch;

  GstASMToken token;
  gchar val[MAX_RULE_LENGTH];
} GstASMScan;

#define NEXT_CHAR(scan) ((scan)->ch = (scan)->buffer[(scan)->pos++])
#define THIS_CHAR(scan) ((scan)->ch)

static GstASMScan *
gst_asm_scan_new (const gchar * buffer)
{
  GstASMScan *scan;

  scan = g_new0 (GstASMScan, 1);
  scan->buffer = buffer;
  NEXT_CHAR (scan);

  return scan;
}

static void
gst_asm_scan_free (GstASMScan * scan)
{
  g_free (scan);
}

static void
gst_asm_scan_string (GstASMScan * scan, gchar delim)
{
  gchar ch;
  gint i = 0;

  ch = THIS_CHAR (scan);
  while ((ch != delim) && (ch != '\0')) {
    if (i < MAX_RULE_LENGTH - 1)
      scan->val[i++] = ch;
    ch = NEXT_CHAR (scan);
    if (ch == '\\')
      ch = NEXT_CHAR (scan);
  }
  scan->val[i] = '\0';

  if (ch == delim)
    NEXT_CHAR (scan);

  scan->token = GST_ASM_TOKEN_STRING;
}

static void
gst_asm_scan_number (GstASMScan * scan)
{
  gchar ch;
  gint i = 0;
  gboolean have_float = FALSE;

  ch = THIS_CHAR (scan);
  /* real strips all spaces that are not inside quotes for numbers */
  while ((IS_NUMBER (ch) || IS_SPACE (ch))) {
    if (i < (MAX_RULE_LENGTH - 1) && !IS_SPACE (ch))
      scan->val[i++] = ch;
    if (ch == '.')
      have_float = TRUE;
    ch = NEXT_CHAR (scan);
  }
  scan->val[i] = '\0';

  if (have_float)
    scan->token = GST_ASM_TOKEN_FLOAT;
  else
    scan->token = GST_ASM_TOKEN_INT;
}

static void
gst_asm_scan_identifier (GstASMScan * scan)
{
  gchar ch;
  gint i = 0;

  ch = THIS_CHAR (scan);
  /* real strips all spaces that are not inside quotes for identifiers */
  while ((IS_CHAR (ch) || IS_SPACE (ch))) {
    if (i < (MAX_RULE_LENGTH - 1) && !IS_SPACE (ch))
      scan->val[i++] = ch;
    ch = NEXT_CHAR (scan);
  }
  scan->val[i] = '\0';

  scan->token = GST_ASM_TOKEN_IDENTIFIER;
}

static void
gst_asm_scan_print_token (GstASMScan * scan)
{
#ifdef DEBUG
  switch (scan->token) {
    case GST_ASM_TOKEN_NONE:
      g_print ("none\n");
      break;
    case GST_ASM_TOKEN_EOF:
      g_print ("EOF\n");
      break;

    case GST_ASM_TOKEN_INT:
      g_print ("INT %d\n", atoi (scan->val));
      break;
    case GST_ASM_TOKEN_FLOAT:
      g_print ("FLOAT %f\n", atof (scan->val));
      break;
    case GST_ASM_TOKEN_IDENTIFIER:
      g_print ("ID %s\n", scan->val);
      break;
    case GST_ASM_TOKEN_STRING:
      g_print ("STRING %s\n", scan->val);
      break;

    case GST_ASM_TOKEN_HASH:
      g_print ("HASH\n");
      break;
    case GST_ASM_TOKEN_SEMICOLON:
      g_print ("SEMICOLON\n");
      break;
    case GST_ASM_TOKEN_COMMA:
      g_print ("COMMA\n");
      break;
    case GST_ASM_TOKEN_EQUAL:
      g_print ("==\n");
      break;
    case GST_ASM_TOKEN_NOTEQUAL:
      g_print ("!=\n");
      break;
    case GST_ASM_TOKEN_AND:
      g_print ("&&\n");
      break;
    case GST_ASM_TOKEN_OR:
      g_print ("||\n");
      break;
    case GST_ASM_TOKEN_LESS:
      g_print ("<\n");
      break;
    case GST_ASM_TOKEN_LESSEQUAL:
      g_print ("<=\n");
      break;
    case GST_ASM_TOKEN_GREATER:
      g_print (">\n");
      break;
    case GST_ASM_TOKEN_GREATEREQUAL:
      g_print (">=\n");
      break;
    case GST_ASM_TOKEN_DOLLAR:
      g_print ("$\n");
      break;
    case GST_ASM_TOKEN_LPAREN:
      g_print ("(\n");
      break;
    case GST_ASM_TOKEN_RPAREN:
      g_print (")\n");
      break;
    default:
      break;
  }
#endif
}

static GstASMToken
gst_asm_scan_next_token (GstASMScan * scan)
{
  gchar ch;

  ch = THIS_CHAR (scan);

  /* skip spaces */
  while (IS_SPACE (ch))
    ch = NEXT_CHAR (scan);

  /* remove \ which is common in front of " */
  while (ch == '\\')
    ch = NEXT_CHAR (scan);

  switch (ch) {
    case '#':
      scan->token = GST_ASM_TOKEN_HASH;
      NEXT_CHAR (scan);
      break;
    case ';':
      scan->token = GST_ASM_TOKEN_SEMICOLON;
      NEXT_CHAR (scan);
      break;
    case ',':
      scan->token = GST_ASM_TOKEN_COMMA;
      NEXT_CHAR (scan);
      break;
    case '=':
      scan->token = GST_ASM_TOKEN_EQUAL;
      if (NEXT_CHAR (scan) == '=')
        NEXT_CHAR (scan);
      break;
    case '!':
      if (NEXT_CHAR (scan) == '=') {
        scan->token = GST_ASM_TOKEN_NOTEQUAL;
        NEXT_CHAR (scan);
      }
      break;
    case '&':
      scan->token = GST_ASM_TOKEN_AND;
      if (NEXT_CHAR (scan) == '&')
        NEXT_CHAR (scan);
      break;
    case '|':
      scan->token = GST_ASM_TOKEN_OR;
      if (NEXT_CHAR (scan) == '|')
        NEXT_CHAR (scan);
      break;
    case '<':
      scan->token = GST_ASM_TOKEN_LESS;
      if (NEXT_CHAR (scan) == '=') {
        scan->token = GST_ASM_TOKEN_LESSEQUAL;
        NEXT_CHAR (scan);
      }
      break;
    case '>':
      scan->token = GST_ASM_TOKEN_GREATER;
      if (NEXT_CHAR (scan) == '=') {
        scan->token = GST_ASM_TOKEN_GREATEREQUAL;
        NEXT_CHAR (scan);
      }
      break;
    case '$':
      scan->token = GST_ASM_TOKEN_DOLLAR;
      NEXT_CHAR (scan);
      break;
    case '(':
      scan->token = GST_ASM_TOKEN_LPAREN;
      NEXT_CHAR (scan);
      break;
    case ')':
      scan->token = GST_ASM_TOKEN_RPAREN;
      NEXT_CHAR (scan);
      break;
    case '"':
      NEXT_CHAR (scan);
      gst_asm_scan_string (scan, '"');
      break;
    case '\'':
      NEXT_CHAR (scan);
      gst_asm_scan_string (scan, '\'');
      break;
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
      gst_asm_scan_number (scan);
      break;
    case '\0':
      scan->token = GST_ASM_TOKEN_EOF;
      break;
    default:
      gst_asm_scan_identifier (scan);
      break;
  }
  gst_asm_scan_print_token (scan);
  return scan->token;
}

static GstASMRule *
gst_asm_rule_new (void)
{
  GstASMRule *rule;

  rule = g_new (GstASMRule, 1);
  rule->root = NULL;
  rule->props = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

  return rule;
}

static void
gst_asm_rule_free (GstASMRule * rule)
{
  g_hash_table_destroy (rule->props);
  if (rule->root)
    gst_asm_node_free (rule->root);
  g_free (rule);
}

static void
gst_asm_rule_add_property (GstASMRule * rule, gchar * key, gchar * val)
{
  g_hash_table_insert (rule->props, key, val);
}

static GstASMNode *gst_asm_scan_parse_condition (GstASMScan * scan);

static GstASMNode *
gst_asm_scan_parse_operand (GstASMScan * scan)
{
  GstASMNode *node;

  switch (scan->token) {
    case GST_ASM_TOKEN_DOLLAR:
      gst_asm_scan_next_token (scan);

      if (scan->token != GST_ASM_TOKEN_IDENTIFIER)
        g_warning ("identifier expected");

      node = gst_asm_node_new ();
      node->type = GST_ASM_NODE_VARIABLE;
      node->data.varname = g_strdup (scan->val);
      break;
    case GST_ASM_TOKEN_INT:
      node = gst_asm_node_new ();
      node->type = GST_ASM_NODE_INTEGER;
      node->data.intval = (gfloat) atof (scan->val);
      break;
    case GST_ASM_TOKEN_FLOAT:
      node = gst_asm_node_new ();
      node->type = GST_ASM_NODE_FLOAT;
      node->data.floatval = atoi (scan->val);
      break;
    case GST_ASM_TOKEN_LPAREN:
      gst_asm_scan_next_token (scan);
      node = gst_asm_scan_parse_condition (scan);
      if (scan->token != GST_ASM_TOKEN_RPAREN)
        g_warning (") expected");
      break;
    default:
      g_warning ("$ <number> or ) expected");
      node = NULL;
      break;
  }
  gst_asm_scan_next_token (scan);

  return node;
}

static GstASMNode *
gst_asm_scan_parse_expression (GstASMScan * scan)
{
  GstASMNode *node, *left;

  node = gst_asm_scan_parse_operand (scan);

  while (IS_COND_TOKEN (scan->token)) {
    left = node;

    node = gst_asm_node_new ();
    node->type = GST_ASM_NODE_OPERATOR;
    node->data.optype = (GstASMOp) scan->token;

    gst_asm_scan_next_token (scan);

    node->right = gst_asm_scan_parse_operand (scan);
    node->left = left;
  }
  return node;
}

static GstASMNode *
gst_asm_scan_parse_condition (GstASMScan * scan)
{
  GstASMNode *node, *left;

  node = gst_asm_scan_parse_expression (scan);

  while (IS_OP_TOKEN (scan->token)) {
    left = node;

    node = gst_asm_node_new ();
    node->type = GST_ASM_NODE_OPERATOR;
    node->data.optype = (GstASMOp) scan->token;

    gst_asm_scan_next_token (scan);

    node->right = gst_asm_scan_parse_expression (scan);
    node->left = left;
  }
  return node;
}

static void
gst_asm_scan_parse_property (GstASMRule * rule, GstASMScan * scan)
{
  gchar *key, *val;

  if (scan->token != GST_ASM_TOKEN_IDENTIFIER) {
    g_warning ("identifier expected");
    return;
  }
  key = g_strdup (scan->val);

  gst_asm_scan_next_token (scan);
  if (scan->token != GST_ASM_TOKEN_EQUAL) {
    g_warning ("= expected");
    return;
  }
  gst_asm_scan_next_token (scan);
  val = g_strdup (scan->val);

  gst_asm_rule_add_property (rule, key, val);
  gst_asm_scan_next_token (scan);
}

static GstASMRule *
gst_asm_scan_parse_rule (GstASMScan * scan)
{
  GstASMRule *rule;

  rule = gst_asm_rule_new ();

  if (scan->token == GST_ASM_TOKEN_HASH) {
    gst_asm_scan_next_token (scan);
    rule->root = gst_asm_scan_parse_condition (scan);
    if (scan->token == GST_ASM_TOKEN_COMMA)
      gst_asm_scan_next_token (scan);
  }

  if (scan->token != GST_ASM_TOKEN_SEMICOLON) {
    gst_asm_scan_parse_property (rule, scan);
    while (scan->token == GST_ASM_TOKEN_COMMA) {
      gst_asm_scan_next_token (scan);
      gst_asm_scan_parse_property (rule, scan);
    }
    gst_asm_scan_next_token (scan);
  }
  return rule;
}

static gboolean
gst_asm_rule_evaluate (GstASMRule * rule, GHashTable * vars)
{
  gboolean res;

  if (rule->root) {
    res = (gboolean) gst_asm_node_evaluate (rule->root, vars);
  } else
    res = TRUE;

  return res;
}

GstASMRuleBook *
gst_asm_rule_book_new (const gchar * rulebook)
{
  GstASMRuleBook *book;
  GstASMRule *rule = NULL;
  GstASMScan *scan;
  GstASMToken token;

  book = g_new0 (GstASMRuleBook, 1);
  book->rulebook = rulebook;

  scan = gst_asm_scan_new (book->rulebook);
  gst_asm_scan_next_token (scan);

  do {
    rule = gst_asm_scan_parse_rule (scan);
    if (rule) {
      book->rules = g_list_append (book->rules, rule);
      book->n_rules++;
    }
    token = scan->token;
  } while (token != GST_ASM_TOKEN_EOF);

  gst_asm_scan_free (scan);

  return book;
}

void
gst_asm_rule_book_free (GstASMRuleBook * book)
{
  GList *walk;

  for (walk = book->rules; walk; walk = g_list_next (walk)) {
    GstASMRule *rule = (GstASMRule *) walk->data;

    gst_asm_rule_free (rule);
  }
  g_list_free (book->rules);
  g_free (book);
}

gint
gst_asm_rule_book_match (GstASMRuleBook * book, GHashTable * vars,
    gint * rulematches)
{
  GList *walk;
  gint i, n = 0;

  for (walk = book->rules, i = 0; walk; walk = g_list_next (walk), i++) {
    GstASMRule *rule = (GstASMRule *) walk->data;

    if (gst_asm_rule_evaluate (rule, vars)) {
      rulematches[n++] = i;
    }
  }
  return n;
}

#ifdef TEST
gint
main (gint argc, gchar * argv[])
{
  GstASMRuleBook *book;
  gint rulematch[MAX_RULEMATCHES];
  GHashTable *vars;
  gint i, n;

  static const gchar rules1[] =
      "#($Bandwidth < 67959),TimestampDelivery=T,DropByN=T,"
      "priority=9;#($Bandwidth >= 67959) && ($Bandwidth < 167959),"
      "AverageBandwidth=67959,Priority=9;#($Bandwidth >= 67959) && ($Bandwidth"
      " < 167959),AverageBandwidth=0,Priority=5,OnDepend=\\\"1\\\";#($Bandwidth >= 167959)"
      " && ($Bandwidth < 267959),AverageBandwidth=167959,Priority=9;#($Bandwidth >= 167959)"
      " && ($Bandwidth < 267959),AverageBandwidth=0,Priority=5,OnDepend=\\\"3\\\";"
      "#($Bandwidth >= 267959),AverageBandwidth=267959,Priority=9;#($Bandwidth >= 267959)"
      ",AverageBandwidth=0,Priority=5,OnDepend=\\\"5\\\";";
  static const gchar rules2[] =
      "AverageBandwidth=32041,Priority=5;AverageBandwidth=0,"
      "Priority=5,OnDepend=\\\"0\\\", OffDepend=\\\"0\\\";";
  static const gchar rules3[] =
      "#(($Bandwidth >= 27500) && ($OldPNMPlayer)),AverageBandwidth=27500,priority=9,PNMKeyframeRule=T;#(($Bandwidth >= 27500) && ($OldPNMPlayer)),AverageBandwidth=0,priority=5,PNMNonKeyframeRule=T;#(($Bandwidth < 27500) && ($OldPNMPlayer)),TimestampDelivery=T,DropByN=T,priority=9,PNMThinningRule=T;#($Bandwidth < 13899),TimestampDelivery=T,DropByN=T,priority=9;#($Bandwidth >= 13899) && ($Bandwidth < 19000),AverageBandwidth=13899,Priority=9;#($Bandwidth >= 13899) && ($Bandwidth < 19000),AverageBandwidth=0,Priority=5,OnDepend=\\\"4\\\";#($Bandwidth >= 19000) && ($Bandwidth < 27500),AverageBandwidth=19000,Priority=9;#($Bandwidth >= 19000) && ($Bandwidth < 27500),AverageBandwidth=0,Priority=5,OnDepend=\\\"6\\\";#($Bandwidth >= 27500) && ($Bandwidth < 132958),AverageBandwidth=27500,Priority=9;#($Bandwidth >= 27500) && ($Bandwidth < 132958),AverageBandwidth=0,Priority=5,OnDepend=\\\"8\\\";#($Bandwidth >= 132958) && ($Bandwidth < 187958),AverageBandwidth=132958,Priority=9;#($Bandwidth >= 132958) && ($Bandwidth < 187958),AverageBandwidth=0,Priority=5,OnDepend=\\\"10\\\";#($Bandwidth >= 187958),AverageBandwidth=187958,Priority=9;#($Bandwidth >= 187958),AverageBandwidth=0,Priority=5,OnDepend=\\\"12\\\";";

  vars = g_hash_table_new (g_str_hash, g_str_equal);
  g_hash_table_insert (vars, (gchar *) "Bandwidth", (gchar *) "300000");

  book = gst_asm_rule_book_new (rules1);
  n = gst_asm_rule_book_match (book, vars, rulematch);
  gst_asm_rule_book_free (book);

  g_print ("%d rules matched\n", n);
  for (i = 0; i < n; i++) {
    g_print ("rule %d matched\n", rulematch[i]);
  }

  book = gst_asm_rule_book_new (rules2);
  n = gst_asm_rule_book_match (book, vars, rulematch);
  gst_asm_rule_book_free (book);

  g_print ("%d rules matched\n", n);
  for (i = 0; i < n; i++) {
    g_print ("rule %d matched\n", rulematch[i]);
  }

  book = gst_asm_rule_book_new (rules3);
  n = gst_asm_rule_book_match (book, vars, rulematch);
  gst_asm_rule_book_free (book);


  g_print ("%d rules matched\n", n);
  for (i = 0; i < n; i++) {
    g_print ("rule %d matched\n", rulematch[i]);
  }

  g_hash_table_destroy (vars);

  return 0;
}
#endif
