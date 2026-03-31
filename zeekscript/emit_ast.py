#!/usr/bin/env python3
"""Emit a semantic ASCII representation of a Zeek script's parse tree.

Reads Zeek source (from a file or stdin), parses with tree-sitter, and
emits a semantic representation where each node is classified by what
it IS (BINARY-OP, IDENTIFIER, GLOBAL-DECL, etc.) rather than by the
grammar rule that produced it.

Output format
-------------
The output is a token stream of TAGs, quoted strings, and { } blocks.
Whitespace (spaces, newlines, indentation) is not significant — it
exists only for human readability.  A C++ consumer parses this with
simple recursive descent.

    TAG "arg1" "arg2" { child1 child2 ... }

Leaf nodes:     TAG "value"
Container nodes: TAG "args" { children }
Multiple groups: TAG "args" { group1 } { group2 }

Special markers:
    BLANK           Author's blank line (preserve it).
    COMMENT-TRAILING "text"   Comment on same line as preceding token.
    COMMENT-LEADING "text"    Own-line comment before following content.
    COMMENT-PREV "text"       Comment attached to preceding stmt (##<, #@).
    RAW "text"                Verbatim text (NO-FORMAT region).
    SEMI                      Statement-ending semicolon.

If tree-sitter reports parse errors, the program exits with status 1.
"""

from __future__ import annotations

import json
import sys
from typing import Sequence

import tree_sitter
import tree_sitter_zeek


def _quote(s: str) -> str:
    return json.dumps(s, ensure_ascii=False)


# Operators that appear as anonymous tokens in binary expressions.
_BINARY_OPS = frozenset({
    "+", "-", "*", "/", "%",
    "&&", "||",
    "==", "!=", "<", ">", "<=", ">=",
    "&", "|", "^",
    "in", "!in",
    "=", "+=", "-=",
    "?$",
})

_UNARY_OPS = frozenset({"!", "-", "~"})


class Emitter:
    def __init__(self, source: bytes, root: tree_sitter.Node):
        self.source = source
        self.root = root
        self.out: list[str] = []
        self._indent = 0
        self._prev_content_end = 0  # for blank-line detection
        self._prev_content_line = -1  # for comment attachment

    # ------------------------------------------------------------------
    # Output helpers
    # ------------------------------------------------------------------

    def _w(self, text: str) -> None:
        """Write text at current indent, on a new line."""
        self.out.append("  " * self._indent + text)

    def _open(self, header: str) -> None:
        self._w(header + " {")
        self._indent += 1

    def _close(self) -> None:
        self._indent -= 1
        self._w("}")

    def _text(self, node: tree_sitter.Node) -> str:
        return self.source[node.start_byte:node.end_byte].decode(
            "utf-8", errors="replace")

    def _children(self, node: tree_sitter.Node) -> list[tree_sitter.Node]:
        """Non-extra children."""
        return [c for c in node.children if not c.is_extra]

    def _extras(self, node: tree_sitter.Node) -> list[tree_sitter.Node]:
        """Extra children (comments, newlines)."""
        return [c for c in node.children if c.is_extra]

    def _has_blank(self, start: int, end: int) -> bool:
        return self.source[start:end].count(b"\n") >= 2

    # ------------------------------------------------------------------
    # Comment handling
    # ------------------------------------------------------------------

    def _emit_extras_in(self, node: tree_sitter.Node) -> None:
        """Emit comments among a node's children, classifying attachment."""
        children = node.children
        for i, child in enumerate(children):
            if not child.is_extra:
                continue
            if child.type == "nl":
                continue
            text = self._text(child)

            # Prev-attached: ##< (zeekygen prev) or #@ (annotation)
            if text.startswith("##<") or text.startswith("#@"):
                self._w(f'COMMENT-PREV {_quote(text)}')
                continue

            # Find preceding and following non-extra siblings for context.
            prev_content = None
            for j in range(i - 1, -1, -1):
                if not children[j].is_extra:
                    prev_content = children[j]
                    break

            # Trailing: same line as preceding content
            if (prev_content is not None
                    and child.start_point[0] == prev_content.end_point[0]):
                self._w(f'COMMENT-TRAILING {_quote(text)}')
            else:
                self._w(f'COMMENT-LEADING {_quote(text)}')

    def _iter_children(self, node: tree_sitter.Node):
        """Yield non-extra children, emitting extras inline in source order.

        Use this instead of _children() + _emit_extras_in() for container
        nodes (records, enums, expr lists, etc.) where trailing comments
        must stay adjacent to the sibling they trail.
        """
        last_non_extra = None
        for child in node.children:
            if child.is_extra:
                if child.type == "nl":
                    continue
                self._maybe_blank(child)
                text = self._text(child)
                if text.startswith("##<") or text.startswith("#@"):
                    self._w(f'COMMENT-PREV {_quote(text)}')
                elif (last_non_extra is not None
                        and child.start_point[0]
                            == last_non_extra.end_point[0]):
                    self._w(f'COMMENT-TRAILING {_quote(text)}')
                else:
                    self._w(f'COMMENT-LEADING {_quote(text)}')
                self._mark_content(child)
                continue
            last_non_extra = child
            yield child

    # ------------------------------------------------------------------
    # Blank line between items
    # ------------------------------------------------------------------

    def _maybe_blank(self, node: tree_sitter.Node) -> None:
        if (self._prev_content_end > 0
                and self._has_blank(self._prev_content_end,
                                    node.start_byte)):
            self._w("BLANK")
            self._prev_content_end = node.start_byte

    def _mark_content(self, node: tree_sitter.Node) -> None:
        self._prev_content_end = node.end_byte
        self._prev_content_line = node.end_point[0]

    # ------------------------------------------------------------------
    # Main dispatch
    # ------------------------------------------------------------------

    def emit(self) -> None:
        self._emit_source_file(self.root)

    def _emit_source_file(self, node: tree_sitter.Node) -> None:
        for child in node.children:
            if child.is_extra:
                if child.type != "nl":
                    self._maybe_blank(child)
                    text = self._text(child)
                    if text.startswith("##<") or text.startswith("#@"):
                        self._w(f'COMMENT-PREV {_quote(text)}')
                    elif (self._prev_content_line >= 0
                          and child.start_point[0]
                              == self._prev_content_line):
                        self._w(f'COMMENT-TRAILING {_quote(text)}')
                    else:
                        self._w(f'COMMENT-LEADING {_quote(text)}')
                    self._mark_content(child)
                continue
            self._maybe_blank(child)
            self._emit(child)

    def _emit(self, node: tree_sitter.Node) -> None:
        """Dispatch a non-extra node to its semantic handler."""
        typ = node.type

        # Transparent wrappers — skip to the single meaningful child.
        if typ == "decl":
            kids = self._children(node)
            if len(kids) == 1:
                self._emit(kids[0])
                return
        if typ == "stmt":
            self._emit_stmt(node)
            return

        # Expression-level types
        if typ == "expr":
            self._emit_expr(node)
            return
        if typ == "id":
            self._w(f'IDENTIFIER {_quote(self._text(node))}')
            self._mark_content(node)
            return
        if typ == "constant":
            self._emit_expr_child(node)
            return
        if typ == "type":
            self._emit_type(node)
            return

        dispatch = {
            "source_file": self._emit_source_file,
            "global_decl": self._emit_global_decl,
            "const_decl": self._emit_global_decl,
            "option_decl": self._emit_global_decl,
            "redef_decl": self._emit_global_decl,
            "type_decl": self._emit_type_decl,
            "func_decl": self._emit_func_decl,
            "export_decl": self._emit_export_decl,
            "module_decl": self._emit_module_decl,
            "redef_record_decl": self._emit_redef_record_decl,
            "redef_enum_decl": self._emit_redef_enum_decl,
            "preproc_directive": self._emit_preproc,
        }
        handler = dispatch.get(typ)
        if handler:
            handler(node)
        else:
            self._emit_unknown(node)

    # ------------------------------------------------------------------
    # Expressions
    # ------------------------------------------------------------------

    def _emit_expr(self, node: tree_sitter.Node) -> None:
        """Classify and emit an expression node."""
        kids = self._children(node)

        if not kids:
            self._w(f'UNKNOWN-EXPR')
            self._mark_content(node)
            return

        # Single-child expr: unwrap
        if len(kids) == 1:
            self._emit_expr_child(kids[0])
            return

        # Keyword-prefix expressions: hook expr, schedule expr, copy expr
        if (not kids[0].is_named
                and self._text(kids[0]) in ("hook", "schedule", "copy")
                and len(kids) == 2):
            keyword = self._text(kids[0])
            self._open(f'KEYWORD-EXPR {_quote(keyword)}')
            self._emit_expr_child(kids[1])
            self._close()
            self._mark_content(node)
            return

        # when-local: local id = expr (inside when condition)
        if not kids[0].is_named and self._text(kids[0]) == "local":
            name = ""
            init_expr = None
            for k in kids:
                if k.type == "id" and not name:
                    name = self._text(k)
                elif k.type == "expr":
                    init_expr = k
            self._open(f'WHEN-LOCAL {_quote(name)}')
            if init_expr:
                self._emit_expr(init_expr)
            self._close()
            self._mark_content(node)
            return

        # Classify by token patterns in children
        tokens = {self._text(k): k for k in kids if not k.is_named}
        token_texts = set(tokens.keys())

        # Ternary: expr ? expr : expr
        if "?" in token_texts and ":" in token_texts:
            cond, then, els = [k for k in kids if k.is_named or k.type == "expr"]
            self._open('TERNARY')
            self._emit_expr(cond)
            self._emit_expr(then)
            self._emit_expr(els)
            self._close()
            return

        # Cardinality/absolute: | expr |
        if (len(kids) == 3
                and self._text(kids[0]) == "|"
                and self._text(kids[2]) == "|"
                and kids[1].is_named):
            self._open('UNARY-OP "|...|"')
            self._emit_expr_child(kids[1])
            self._close()
            self._mark_content(node)
            return

        # Binary operator
        bin_ops = token_texts & _BINARY_OPS
        if bin_ops and len(kids) == 3:
            op = bin_ops.pop()
            self._open(f'BINARY-OP {_quote(op)}')
            self._emit_expr_child(kids[0])
            self._emit_expr_child(kids[2])
            self._emit_extras_in(node)
            self._close()
            self._mark_content(node)
            return

        # Multi-token binary operator: !in (two tokens: "!" and "in")
        if ("!" in token_texts and "in" in token_texts
                and len(kids) == 4 and not kids[1].is_named
                and not kids[2].is_named):
            self._open('BINARY-OP "!in"')
            self._emit_expr_child(kids[0])
            self._emit_expr_child(kids[3])
            self._emit_extras_in(node)
            self._close()
            self._mark_content(node)
            return

        # Unary operator
        if len(kids) == 2 and not kids[0].is_named:
            op_text = self._text(kids[0])
            if op_text in _UNARY_OPS:
                self._open(f'UNARY-OP {_quote(op_text)}')
                self._emit_expr_child(kids[1])
                self._close()
                self._mark_content(node)
                return

        # Function/event call: expr ( expr_list )
        # Also handles keyword-named constructors: set(...), table(...), vector(...)
        if "(" in token_texts and ")" in token_texts:
            first_text = self._text(kids[0])
            if kids[0].is_named or first_text in ("set", "table", "vector",
                                                    "record", "event"):
                if kids[0].is_named:
                    callee_text = None  # emit via _emit_expr_child
                else:
                    callee_text = first_text
                args = [k for k in kids if k.type == "expr_list"]
                self._open('CALL')
                if callee_text:
                    self._w(f'IDENTIFIER {_quote(callee_text)}')
                else:
                    self._emit_expr_child(kids[0])
                if args:
                    self._open('ARGS')
                    self._emit_expr_list(args[0])
                    self._close()
                self._emit_extras_in(node)
                self._close()
                self._mark_content(node)
                return

        # Parenthesized expression: ( expr )
        if ("(" in token_texts and ")" in token_texts
                and len(kids) == 3
                and not kids[0].is_named):
            self._open('PAREN')
            self._emit_expr_child(kids[1])
            self._close()
            self._mark_content(node)
            return

        # Slice: expr [ expr : expr ]
        # Always emit both lo and hi; use CONSTANT "" for absent bounds.
        slices = [k for k in kids if k.type == "index_slice"]
        if slices and kids[0].is_named:
            base = kids[0]
            sl = slices[0]
            sl_kids = self._children(sl)
            # Find the ":" separator to determine lo vs hi.
            colon_idx = None
            for i, c in enumerate(sl_kids):
                if not c.is_named and self._text(c) == ":":
                    colon_idx = i
                    break
            lo_exprs = [c for c in sl_kids[:colon_idx]
                        if c.type == "expr"]
            hi_exprs = [c for c in sl_kids[colon_idx + 1:]
                        if c.type == "expr"]
            self._open('SLICE')
            self._emit_expr_child(base)
            if lo_exprs:
                self._emit_expr(lo_exprs[0])
            else:
                self._w('CONSTANT ""')
            if hi_exprs:
                self._emit_expr(hi_exprs[0])
            else:
                self._w('CONSTANT ""')
            self._emit_extras_in(node)
            self._close()
            self._mark_content(node)
            return

        # Index literal (composite key): [ expr_list ]
        if ("[" in token_texts and "]" in token_texts
                and not kids[0].is_named
                and self._text(kids[0]) == "["):
            args = [k for k in kids if k.type == "expr_list"]
            self._open('INDEX-LITERAL')
            if args:
                self._emit_expr_list(args[0])
            self._close()
            self._mark_content(node)
            return

        # Index: expr [ expr_list ]
        if "[" in token_texts and "]" in token_texts and kids[0].is_named:
            base = kids[0]
            args = [k for k in kids if k.type == "expr_list"]
            self._open('INDEX')
            self._emit_expr_child(base)
            if args:
                self._open('SUBSCRIPTS')
                self._emit_expr_list(args[0])
                self._close()
            self._emit_extras_in(node)
            self._close()
            self._mark_content(node)
            return

        # Field assignment in record constructor: $field = expr
        if "$" in token_texts and not kids[0].is_named:
            # $foo=expr pattern
            field_name = ""
            value_expr = None
            for k in kids:
                if k.type == "id" and not field_name:
                    field_name = self._text(k)
                elif k.type == "expr":
                    value_expr = k
            if value_expr:
                self._open(f'FIELD-ASSIGN {_quote(field_name)}')
                self._emit_expr(value_expr)
                self._close()
            else:
                self._w(f'FIELD-ASSIGN {_quote(field_name)}')
            self._mark_content(node)
            return

        # Field access: expr $ id
        if "$" in token_texts:
            base = kids[0]
            field = kids[-1]
            self._open('FIELD-ACCESS')
            self._emit_expr_child(base)
            self._w(f'IDENTIFIER {_quote(self._text(field))}')
            self._close()
            self._mark_content(node)
            return

        # Lambda: function begin_lambda func_body
        if kids[0].type == "function" or self._text(kids[0]) == "function":
            self._emit_lambda(node)
            return

        # Schedule expression: schedule interval { event }
        if (not kids[0].is_named
                and self._text(kids[0]) == "schedule"
                and "{" in token_texts):
            interval = [k for k in kids if k.is_named and k.type == "expr"]
            event_hdrs = [k for k in kids if k.type == "event_hdr"]
            self._open('SCHEDULE')
            if interval:
                self._emit_expr(interval[0])
            if event_hdrs:
                eh = event_hdrs[0]
                eh_kids = self._children(eh)
                name = ""
                args = None
                for c in eh_kids:
                    if c.type == "id":
                        name = self._text(c)
                    elif c.type == "expr_list":
                        args = c
                self._open(f'CALL')
                self._w(f'IDENTIFIER {_quote(name)}')
                if args:
                    self._open('ARGS')
                    self._emit_expr_list(args)
                    self._close()
                self._close()
            self._close()
            self._mark_content(node)
            return

        # Brace initializer: { expr_list }
        # Infer type: table if any element is key=value, else set.
        if "{" in token_texts and "}" in token_texts:
            args = [k for k in kids if k.type == "expr_list"]
            keyword = "set"
            if args:
                for child in args[0].children:
                    if child.type == "expr":
                        for c in child.children:
                            if not c.is_named and self._text(c) == "=":
                                keyword = "table"
                                break
                        if keyword == "table":
                            break
            self._open(f'CONSTRUCTOR {_quote(keyword)}')
            if args:
                self._emit_expr_list(args[0])
            self._close()
            self._mark_content(node)
            return

        # Fallback
        self._emit_unknown(node)

    def _emit_expr_child(self, node: tree_sitter.Node) -> None:
        """Emit an expression child, unwrapping trivial wrappers."""
        if node.type == "expr":
            self._emit_expr(node)
        elif node.type == "type":
            self._emit_type(node)
        elif node.type == "id":
            self._w(f'IDENTIFIER {_quote(self._text(node))}')
            self._mark_content(node)
        elif node.type == "constant":
            kids = self._children(node)
            if kids and kids[0].type == "interval":
                ival = self._children(kids[0])
                num = self._text(ival[0]) if ival else "?"
                unit = self._text(ival[1]) if len(ival) > 1 else ""
                self._w(f'INTERVAL {_quote(num)} {_quote(unit)}')
            else:
                self._w(f'CONSTANT {_quote(self._text(node))}')
            self._mark_content(node)
        elif node.type == "begin_lambda":
            # Part of lambda — handled by parent
            pass
        elif node.type == "func_body":
            self._emit_func_body(node)
        else:
            self._emit_expr(node)

    def _emit_expr_list(self, node: tree_sitter.Node) -> None:
        """Emit children of an expr_list, skipping commas."""
        for child in self._iter_children(node):
            if child.type == "expr":
                self._emit_expr(child)
            elif not child.is_named and self._text(child) == ",":
                pass  # structural separator, C++ knows from context
            else:
                self._emit_expr_child(child)

    def _emit_lambda(self, node: tree_sitter.Node) -> None:
        """Emit a lambda (anonymous function) expression."""
        kids = self._children(node)
        # Find begin_lambda and func_body
        begin = None
        body = None
        capture = None
        for k in kids:
            if k.type == "begin_lambda":
                begin = k
            elif k.type == "func_body":
                body = k
            elif k.type == "capture_list":
                capture = k

        self._open('LAMBDA')
        if capture:
            self._open('CAPTURES')
            for ck in self._children(capture):
                if ck.type == "id":
                    self._w(f'IDENTIFIER {_quote(self._text(ck))}')
            self._close()
        if begin:
            self._emit_func_params_from(begin)
        if body:
            self._emit_func_body(body)
        self._close()
        self._mark_content(node)

    # ------------------------------------------------------------------
    # Types
    # ------------------------------------------------------------------

    def _emit_type(self, node: tree_sitter.Node) -> None:
        """Classify and emit a type node."""
        kids = self._children(node)
        if not kids:
            self._w('TYPE-ATOM "?"')
            return

        first = kids[0]
        first_text = self._text(first)

        # Parameterized types: table[...] of ..., set[...], vector of ...
        if first_text in ("table", "set") and len(kids) > 1:
            of_type = None
            of_idx = -1
            # Find the type after "of" keyword
            for i, k in enumerate(kids):
                if not k.is_named and self._text(k) == "of":
                    if i + 1 < len(kids):
                        of_type = kids[i + 1]
                        of_idx = i + 1
                    break
            # Parameters are type children before "of"
            params = [k for i, k in enumerate(kids)
                      if k.type == "type" and i != of_idx]
            self._open(f'TYPE-PARAMETERIZED {_quote(first_text)}')
            for p in params:
                self._emit_type(p)
            if of_type and of_type.type == "type":
                self._w("OF")
                self._emit_type(of_type)
            self._close()
            return

        if first_text == "vector":
            of_type = None
            for i, k in enumerate(kids):
                if not k.is_named and self._text(k) == "of":
                    if i + 1 < len(kids):
                        of_type = kids[i + 1]
                    break
            if of_type:
                self._open(f'TYPE-PARAMETERIZED "vector"')
                self._w("OF")
                self._emit_type(of_type)
                self._close()
            else:
                self._w('TYPE-ATOM "vector"')
            return

        # Function types: event(...), function(...): ret, hook(...)
        if first_text in ("event", "function", "hook"):
            self._open(f'TYPE-FUNC {_quote(first_text)}')
            params = [k for k in kids if k.type == "formal_args"]
            ret = None
            # Return type after ':'
            for i, k in enumerate(kids):
                if not k.is_named and self._text(k) == ":":
                    if i + 1 < len(kids) and kids[i + 1].type == "type":
                        ret = kids[i + 1]
                    break
            if params:
                self._open('PARAMS')
                self._emit_formal_args(params[0])
                self._close()
            if ret:
                self._open('RETURNS')
                self._emit_type(ret)
                self._close()
            self._close()
            return

        # Record type
        if first_text == "record":
            self._open('TYPE-RECORD')
            for k in self._iter_children(node):
                if k.type == "type_spec":
                    self._maybe_blank(k)
                    self._emit_type_spec(k)
            self._close()
            return

        # Enum type
        if first_text == "enum":
            self._open('TYPE-ENUM')
            for k in kids:
                if k.type == "enum_body":
                    self._emit_enum_body(k)
            self._close()
            return

        # Simple type name (count, string, addr, etc.) or named type (id)
        if len(kids) == 1:
            if first.type == "id":
                self._w(f'TYPE-ATOM {_quote(self._text(first))}')
            else:
                self._w(f'TYPE-ATOM {_quote(first_text)}')
            return

        # Fallback
        self._w(f'TYPE-ATOM {_quote(self._text(node))}')

    def _emit_type_spec(self, node: tree_sitter.Node) -> None:
        """Emit a record field (type_spec): id : type [attrs] ;"""
        kids = self._children(node)
        name = ""
        typ = None
        attrs = None
        for k in kids:
            if k.type == "id":
                name = self._text(k)
            elif k.type == "type":
                typ = k
            elif k.type == "attr_list":
                attrs = k
        self._open(f'FIELD {_quote(name)}')
        if typ:
            self._emit_type(typ)
        if attrs:
            self._emit_attr_list(attrs)
        self._emit_extras_in(node)
        self._close()

    def _emit_enum_body(self, node: tree_sitter.Node) -> None:
        """Emit enum body elements."""
        for child in self._iter_children(node):
            if child.type == "enum_body_elem":
                kids = self._children(child)
                for k in kids:
                    if k.type == "id":
                        self._w(f'ENUM-VALUE {_quote(self._text(k))}')
                self._mark_content(child)
            elif child.type == "id":
                self._w(f'ENUM-VALUE {_quote(self._text(child))}')
                self._mark_content(child)

    # ------------------------------------------------------------------
    # Attributes
    # ------------------------------------------------------------------

    def _emit_attr_list(self, node: tree_sitter.Node) -> None:
        self._open('ATTR-LIST')
        for child in self._iter_children(node):
            if child.type == "attr":
                self._emit_attr(child)
        self._close()

    def _emit_attr(self, node: tree_sitter.Node) -> None:
        kids = self._children(node)
        name = self._text(kids[0]) if kids else "?"
        value_expr = None
        for k in kids:
            if k.type == "expr":
                value_expr = k
                break
        if value_expr:
            self._open(f'ATTR {_quote(name)}')
            self._emit_expr(value_expr)
            self._close()
        else:
            self._w(f'ATTR {_quote(name)}')

    # ------------------------------------------------------------------
    # Parameters
    # ------------------------------------------------------------------

    def _emit_formal_args(self, node: tree_sitter.Node) -> None:
        for child in self._iter_children(node):
            if child.type == "formal_arg":
                self._emit_formal_arg(child)

    def _emit_formal_arg(self, node: tree_sitter.Node) -> None:
        kids = self._children(node)
        name = ""
        typ = None
        attrs = None
        for k in kids:
            if k.type == "id":
                name = self._text(k)
            elif k.type == "type":
                typ = k
            elif k.type == "attr_list":
                attrs = k
        self._open(f'PARAM {_quote(name)}')
        if typ:
            self._emit_type(typ)
        if attrs:
            self._emit_attr_list(attrs)
        self._close()

    def _emit_func_params_from(self, node: tree_sitter.Node) -> None:
        """Extract and emit PARAMS, RETURNS, and ATTR-LIST from a begin_lambda or func_hdr child."""
        params = None
        ret = None
        attrs = None
        for k in node.children:
            if k.is_extra:
                continue
            if k.type == "func_params":
                for pk in self._children(k):
                    if pk.type == "formal_args":
                        params = pk
                    elif pk.type == "type":
                        ret = pk
            elif k.type == "attr_list":
                attrs = k
        if params:
            self._open('PARAMS')
            self._emit_formal_args(params)
            self._close()
        if ret:
            self._open('RETURNS')
            self._emit_type(ret)
            self._close()
        if attrs:
            self._emit_attr_list(attrs)

    # ------------------------------------------------------------------
    # Declarations
    # ------------------------------------------------------------------

    def _emit_global_decl(self, node: tree_sitter.Node) -> None:
        kids = self._children(node)
        keyword = self._text(kids[0])  # global/const/option/redef
        name = ""
        typ = None
        init_expr = None
        init_op = "="
        attrs = None

        i = 1
        if i < len(kids) and kids[i].type == "id":
            name = self._text(kids[i])
            i += 1
        # : type
        if i < len(kids) and self._text(kids[i]) == ":":
            i += 1
            if i < len(kids) and kids[i].type == "type":
                typ = kids[i]
                i += 1
        # initializer
        if i < len(kids) and kids[i].type == "initializer":
            init_node = kids[i]
            for ik in self._children(init_node):
                if ik.type == "init_class":
                    init_op = self._text(self._children(ik)[0])
                elif ik.type == "expr":
                    init_expr = ik
            i += 1
        # attr_list
        if i < len(kids) and kids[i].type == "attr_list":
            attrs = kids[i]
            i += 1

        self._open(f'GLOBAL-DECL {_quote(keyword)} {_quote(name)}')
        if typ:
            self._open('TYPE')
            self._emit_type(typ)
            self._close()
        if init_expr:
            self._open(f'INIT {_quote(init_op)}')
            self._emit_init_expr(init_expr, typ)
            self._close()
        if attrs:
            self._emit_attr_list(attrs)
        self._emit_extras_in(node)
        self._w('SEMI')
        self._close()
        self._mark_content(node)

    def _emit_init_expr(self, node: tree_sitter.Node,
                        type_node: tree_sitter.Node | None) -> None:
        """Emit an initializer expression, doing brace-to-constructor transform."""
        kids = self._children(node)
        # Brace initializer with known type → CONSTRUCTOR
        if (kids and self._text(kids[0]) == "{"
                and type_node is not None):
            type_kids = self._children(type_node)
            ctor_name = self._text(type_kids[0]) if type_kids else None
            if ctor_name in ("table", "set", "vector"):
                args = [k for k in kids if k.type == "expr_list"]
                self._open(f'CONSTRUCTOR {_quote(ctor_name)}')
                if args:
                    self._emit_expr_list(args[0])
                self._close()
                return
        self._emit_expr(node)

    def _emit_func_decl(self, node: tree_sitter.Node) -> None:
        kids = self._children(node)
        func_hdr = None
        func_body = None
        for k in kids:
            if k.type == "func_hdr":
                func_hdr = k
            elif k.type == "func_body":
                func_body = k

        kind = ""
        name = ""
        hdr_inner = None  # event/func/hook node inside func_hdr
        if func_hdr:
            for k in self._children(func_hdr):
                if k.type in ("event", "func", "hook"):
                    hdr_inner = k
                    break
        if hdr_inner:
            hdr_kids = self._children(hdr_inner)
            kind = self._text(hdr_kids[0])
            if len(hdr_kids) > 1 and hdr_kids[1].type == "id":
                name = self._text(hdr_kids[1])

        self._open(f'FUNC-DECL {_quote(kind)} {_quote(name)}')
        if hdr_inner:
            self._emit_func_params_from(hdr_inner)
        if func_body:
            self._emit_func_body(func_body)
        self._emit_extras_in(node)
        self._close()
        self._mark_content(node)

    def _emit_func_body(self, node: tree_sitter.Node) -> None:
        self._open('BODY')
        # Emit extras in source order so comments before/after
        # stmt_list land at the right position.
        for child in node.children:
            if child.is_extra:
                if child.type == "nl":
                    continue
                self._maybe_blank(child)
                text = self._text(child)
                same_line = (self._prev_content_line >= 0
                        and child.start_point[0]
                            == self._prev_content_line)
                if same_line:
                    self._w(f'COMMENT-TRAILING {_quote(text)}')
                elif text.startswith("##<") or text.startswith("#@"):
                    self._w(f'COMMENT-PREV {_quote(text)}')
                else:
                    self._w(f'COMMENT-LEADING {_quote(text)}')
                self._mark_content(child)
            elif child.type == "stmt_list":
                self._emit_stmt_list(child)
            else:
                # Track { and } for same-line comment detection.
                self._mark_content(child)
        self._close()

    def _emit_export_decl(self, node: tree_sitter.Node) -> None:
        kids = self._children(node)
        self._open('EXPORT')
        for k in kids:
            if k.type == "decl":
                inner = self._children(k)
                if inner:
                    self._maybe_blank(inner[0])
                    self._emit(inner[0])
            elif k.is_extra and k.type != "nl":
                self._w(f'COMMENT-LEADING {_quote(self._text(k))}')
        self._emit_extras_in(node)
        self._close()
        self._mark_content(node)

    def _emit_module_decl(self, node: tree_sitter.Node) -> None:
        kids = self._children(node)
        name = ""
        for k in kids:
            if k.type == "id":
                name = self._text(k)
        self._w(f'MODULE {_quote(name)}')
        self._mark_content(node)

    def _emit_type_decl(self, node: tree_sitter.Node) -> None:
        kids = self._children(node)
        name = ""
        typ = None
        for k in kids:
            if k.type == "id":
                name = self._text(k)
            elif k.type == "type":
                typ = k
        self._open(f'TYPE-DECL {_quote(name)}')
        if typ:
            self._emit_type(typ)
        self._emit_extras_in(node)
        self._w('SEMI')
        self._close()
        self._mark_content(node)

    def _emit_redef_record_decl(self, node: tree_sitter.Node) -> None:
        name = ""
        for k in self._children(node):
            if k.type == "id":
                name = self._text(k)
                break
        self._open(f'REDEF-RECORD {_quote(name)}')
        for k in self._iter_children(node):
            if k.type == "type_spec":
                self._maybe_blank(k)
                self._emit_type_spec(k)
        self._w('SEMI')
        self._close()
        self._mark_content(node)

    def _emit_redef_enum_decl(self, node: tree_sitter.Node) -> None:
        kids = self._children(node)
        name = ""
        for k in kids:
            if k.type == "id":
                name = self._text(k)
        self._open(f'REDEF-ENUM {_quote(name)}')
        for k in kids:
            if k.type == "enum_body":
                self._emit_enum_body(k)
        self._emit_extras_in(node)
        self._w('SEMI')
        self._close()
        self._mark_content(node)

    # ------------------------------------------------------------------
    # Statements
    # ------------------------------------------------------------------

    def _emit_stmt_list(self, node: tree_sitter.Node) -> None:
        for child in node.children:
            if child.is_extra:
                if child.type != "nl":
                    self._maybe_blank(child)
                    text = self._text(child)
                    if text.startswith("##<") or text.startswith("#@"):
                        self._w(f'COMMENT-PREV {_quote(text)}')
                    elif (self._prev_content_line >= 0
                          and child.start_point[0]
                              == self._prev_content_line):
                        self._w(f'COMMENT-TRAILING {_quote(text)}')
                    else:
                        self._w(f'COMMENT-LEADING {_quote(text)}')
                    self._mark_content(child)
                continue
            self._maybe_blank(child)
            self._emit(child)

    def _emit_stmt(self, node: tree_sitter.Node) -> None:
        """Classify and emit a statement node."""
        kids = self._children(node)
        if not kids:
            self._emit_unknown(node)
            return

        # Preprocessor directives can appear as statements
        if kids[0].type == "preproc_directive":
            self._emit_preproc(kids[0])
            return

        first_text = self._text(kids[0])

        # Local/const declaration (statement-level)
        if first_text in ("local", "const"):
            self._emit_local_decl(node)
            return

        if first_text == "if":
            self._emit_if(node)
            return
        if first_text == "for":
            self._emit_for(node)
            return
        if first_text == "while":
            self._emit_while(node)
            return
        if first_text == "return":
            self._emit_return(node)
            return
        if first_text == "print":
            self._emit_print(node)
            return
        if first_text == "event":
            self._emit_event_stmt(node)
            return
        if first_text == "switch":
            self._emit_switch(node)
            return
        if first_text == "when":
            self._emit_when(node)
            return
        if first_text in ("add", "delete"):
            self._emit_add_delete(node, first_text)
            return
        if first_text in ("next", "break", "fallthrough"):
            self._w(first_text.upper())
            self._w('SEMI')
            self._mark_content(node)
            return

        # Brace block (standalone { stmt_list })
        if first_text == "{":
            self._open('BLOCK')
            for k in kids:
                if k.type == "stmt_list":
                    self._emit_stmt_list(k)
            self._emit_extras_in(node)
            self._close()
            self._mark_content(node)
            return

        # Expression statement
        expr_kids = [k for k in kids if k.type == "expr"]
        if expr_kids:
            self._open('EXPR-STMT')
            self._emit_expr(expr_kids[0])
            self._emit_extras_in(node)
            self._w('SEMI')
            self._close()
            self._mark_content(node)
            return

        self._emit_unknown(node)

    def _emit_local_decl(self, node: tree_sitter.Node) -> None:
        kids = self._children(node)
        keyword = self._text(kids[0])
        name = ""
        typ = None
        init_expr = None
        init_op = "="
        attrs = None

        i = 1
        if i < len(kids) and kids[i].type == "id":
            name = self._text(kids[i])
            i += 1
        if i < len(kids) and self._text(kids[i]) == ":":
            i += 1
            if i < len(kids) and kids[i].type == "type":
                typ = kids[i]
                i += 1
        if i < len(kids) and kids[i].type == "initializer":
            for ik in self._children(kids[i]):
                if ik.type == "init_class":
                    init_op = self._text(self._children(ik)[0])
                elif ik.type == "expr":
                    init_expr = ik
            i += 1
        if i < len(kids) and kids[i].type == "attr_list":
            attrs = kids[i]
            i += 1

        self._open(f'LOCAL-DECL {_quote(keyword)} {_quote(name)}')
        if typ:
            self._open('TYPE')
            self._emit_type(typ)
            self._close()
        if init_expr:
            self._open(f'INIT {_quote(init_op)}')
            self._emit_init_expr(init_expr, typ)
            self._close()
        if attrs:
            self._emit_attr_list(attrs)
        self._emit_extras_in(node)
        self._w('SEMI')
        self._close()
        self._mark_content(node)

    def _emit_if_extras(self, node, after_node, before_node,
                         ref_node=None) -> None:
        """Emit extra children of `node` between `after_node` and
        `before_node`.  `ref_node` is used for same-line detection."""
        past_after = after_node is None
        for child in node.children:
            if not child.is_extra:
                if child == after_node:
                    past_after = True
                if child == before_node:
                    break
                continue
            if not past_after:
                continue
            if child.type == "nl":
                self._maybe_blank(child)
                continue
            text = self._text(child)
            same_line = (ref_node is not None
                    and child.start_point[0]
                        == ref_node.end_point[0])
            if same_line:
                self._w(f'COMMENT-TRAILING {_quote(text)}')
            elif text.startswith("##<") or text.startswith("#@"):
                self._w(f'COMMENT-PREV {_quote(text)}')
            else:
                self._w(f'COMMENT-LEADING {_quote(text)}')
            self._mark_content(child)

    def _emit_if(self, node: tree_sitter.Node) -> None:
        # Classify non-extra children.
        kids = self._children(node)
        cond = None
        body = None
        else_body = None
        found_else = False
        for k in kids:
            if k.type == "expr" and cond is None:
                cond = k
            elif k.type == "stmt" and not found_else:
                body = k
            elif self._text(k) == "else":
                found_else = True
            elif k.type == "stmt" and found_else:
                else_body = k

        self._open('IF')
        if cond:
            self._open('COND')
            self._emit_expr(cond)
            self._close()

        # Emit extras between cond and body (e.g. #@ annotation
        # after the closing paren of the condition).
        self._emit_if_extras(node, cond, body, ref_node=cond)

        if body:
            self._open('BODY')
            self._emit_stmt(body)
            self._close()

        # Emit extras between body and else.
        self._emit_if_extras(node, body, else_body, ref_node=body)

        if else_body:
            self._open('ELSE')
            self._emit_stmt(else_body)
            self._close()
        self._close()
        self._mark_content(node)

    def _emit_for(self, node: tree_sitter.Node) -> None:
        kids = self._children(node)
        ids = [k for k in kids if k.type == "id"]
        exprs = [k for k in kids if k.type == "expr"]
        body = [k for k in kids if k.type == "stmt"]

        self._open('FOR')
        if ids:
            self._open('VARS')
            for ident in ids:
                self._w(f'IDENTIFIER {_quote(self._text(ident))}')
            self._close()
        if exprs:
            self._open('ITERABLE')
            self._emit_expr(exprs[0])
            self._close()
        if body:
            self._open('BODY')
            self._emit_stmt(body[0])
            self._close()
        self._close()
        self._mark_content(node)

    def _emit_while(self, node: tree_sitter.Node) -> None:
        kids = self._children(node)
        cond = [k for k in kids if k.type == "expr"]
        body = [k for k in kids if k.type == "stmt"]

        self._open('WHILE')
        if cond:
            self._open('COND')
            self._emit_expr(cond[0])
            self._close()
        if body:
            self._open('BODY')
            self._emit_stmt(body[0])
            self._close()
        self._close()
        self._mark_content(node)

    def _emit_return(self, node: tree_sitter.Node) -> None:
        kids = self._children(node)
        expr = [k for k in kids if k.type == "expr"]
        if expr:
            self._open('RETURN')
            self._emit_expr(expr[0])
            self._close()
        else:
            self._w('RETURN')
        self._w('SEMI')
        self._mark_content(node)

    def _emit_print(self, node: tree_sitter.Node) -> None:
        kids = self._children(node)
        elist = [k for k in kids if k.type == "expr_list"]
        self._open('PRINT')
        if elist:
            self._emit_expr_list(elist[0])
        self._emit_extras_in(node)
        self._w('SEMI')
        self._close()
        self._mark_content(node)

    def _emit_event_stmt(self, node: tree_sitter.Node) -> None:
        """Event statement: event name(args);"""
        kids = self._children(node)
        name = ""
        args = None
        for k in kids:
            if k.type == "id":
                name = self._text(k)
            elif k.type == "expr_list":
                args = k
        self._open(f'EVENT-STMT {_quote(name)}')
        if args:
            self._open('ARGS')
            self._emit_expr_list(args)
            self._close()
        self._emit_extras_in(node)
        self._w('SEMI')
        self._close()
        self._mark_content(node)

    def _emit_switch(self, node: tree_sitter.Node) -> None:
        kids = self._children(node)
        exprs = [k for k in kids if k.type == "expr"]
        case_list = [k for k in kids if k.type == "case_list"]

        self._open('SWITCH')
        if exprs:
            self._open('EXPR')
            self._emit_expr(exprs[0])
            self._close()
        if case_list:
            self._emit_case_list(case_list[0])
        self._close()
        self._mark_content(node)

    def _emit_case_list(self, node: tree_sitter.Node) -> None:
        kids = self._children(node)
        i = 0
        while i < len(kids):
            k = kids[i]
            if self._text(k) == "case":
                # Collect: case expr_list : stmt_list
                i += 1
                exprs = kids[i] if i < len(kids) and kids[i].type == "expr_list" else None
                i += 1  # skip ':'
                if i < len(kids) and self._text(kids[i]) == ":":
                    i += 1
                stmts = kids[i] if i < len(kids) and kids[i].type == "stmt_list" else None
                self._open('CASE')
                if exprs:
                    self._open('VALUES')
                    self._emit_expr_list(exprs)
                    self._close()
                if stmts:
                    self._open('BODY')
                    self._emit_stmt_list(stmts)
                    self._close()
                self._close()
                if stmts:
                    i += 1
            elif self._text(k) == "default":
                i += 1  # skip ':'
                if i < len(kids) and self._text(kids[i]) == ":":
                    i += 1
                stmts = kids[i] if i < len(kids) and kids[i].type == "stmt_list" else None
                self._open('DEFAULT')
                if stmts:
                    self._emit_stmt_list(stmts)
                    i += 1
                self._close()
            else:
                i += 1

    def _emit_when(self, node: tree_sitter.Node) -> None:
        kids = self._children(node)
        cond = None
        body = None
        timeout_expr = None
        timeout_body = None

        state = "init"
        for k in kids:
            if k.is_extra or not k.is_named:
                if self._text(k) == "timeout":
                    state = "timeout"
                continue
            if state == "init":
                if k.type == "expr":
                    cond = k
                elif k.type == "stmt":
                    body = k
                    state = "after_body"
            elif state == "after_body":
                if self._text(k) == "timeout":
                    state = "timeout"
            elif state == "timeout":
                if k.type == "expr":
                    timeout_expr = k
                elif k.type == "stmt_list":
                    timeout_body = k

        self._open('WHEN')
        if cond:
            self._open('COND')
            self._emit_expr(cond)
            self._close()
        if body:
            self._open('BODY')
            self._emit_stmt(body)
            self._close()
        if timeout_expr:
            self._open('TIMEOUT')
            self._emit_expr(timeout_expr)
            if timeout_body:
                self._open('BODY')
                self._emit_stmt_list(timeout_body)
                self._close()
            self._close()
        self._close()
        self._mark_content(node)

    def _emit_add_delete(self, node: tree_sitter.Node, keyword: str) -> None:
        kids = self._children(node)
        expr = [k for k in kids if k.type == "expr"]
        self._open(keyword.upper())
        if expr:
            self._emit_expr(expr[0])
        self._w('SEMI')
        self._close()
        self._mark_content(node)

    # ------------------------------------------------------------------
    # Preprocessor
    # ------------------------------------------------------------------

    def _emit_preproc(self, node: tree_sitter.Node) -> None:
        kids = self._children(node)
        directive = self._text(kids[0]) if kids else "?"
        args = [k for k in kids[1:] if k.is_named]
        if args:
            arg_strs = " ".join(_quote(self._text(a)) for a in args)
            self._w(f'PREPROC {_quote(directive)} {arg_strs}')
        else:
            self._w(f'PREPROC {_quote(directive)}')
        self._mark_content(node)

    # ------------------------------------------------------------------
    # Fallback
    # ------------------------------------------------------------------

    def _emit_unknown(self, node: tree_sitter.Node) -> None:
        kids = self._children(node)
        if kids:
            self._open(f'UNKNOWN {_quote(node.type)}')
            for k in kids:
                if k.is_named:
                    self._emit(k)
                else:
                    self._w(f'TOKEN {_quote(self._text(k))}')
            self._close()
        else:
            self._w(f'UNKNOWN {_quote(node.type)} {_quote(self._text(node))}')
        self._mark_content(node)


# ------------------------------------------------------------------
# Error checking
# ------------------------------------------------------------------

def _has_error(node: tree_sitter.Node) -> bool:
    if node.type == "ERROR" or node.is_missing:
        return True
    for child in node.children:
        if _has_error(child):
            return True
    return False


# ------------------------------------------------------------------
# Entry point
# ------------------------------------------------------------------

def main() -> None:
    if len(sys.argv) > 1 and sys.argv[1] != "-":
        with open(sys.argv[1], "rb") as f:
            source = f.read()
    else:
        source = sys.stdin.buffer.read()

    lang = tree_sitter.Language(tree_sitter_zeek.language())
    parser = tree_sitter.Parser(lang)
    tree = parser.parse(source)

    if _has_error(tree.root_node):
        print("error: parse failed", file=sys.stderr)
        sys.exit(1)

    emitter = Emitter(source, tree.root_node)
    emitter.emit()

    sys.stdout.write("\n".join(emitter.out))
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
