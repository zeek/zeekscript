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
    RAW "text"                Verbatim text (NO-FORMAT region).
    SEMI                      Statement-ending semicolon.
    TRAILING-COMMA            Author's trailing comma (preserve in output).

If tree-sitter reports parse errors, the program exits with status 1.
"""

from __future__ import annotations

import json
import sys
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

    # ------------------------------------------------------------------
    # Output helpers
    # ------------------------------------------------------------------

    def _w(self, text: str) -> None:
        """Write text at current indent, on a new line."""
        self.out.append("  " * self._indent + text)

    def _kw(self, keyword: str) -> None:
        """Emit a KEYWORD token followed by SP."""
        self._w(f'KEYWORD {_quote(keyword)}')
        self._w('SP')

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

    def _is_atomic_expr(self, node: tree_sitter.Node) -> bool:
        """True if node will emit as a leaf (IDENTIFIER or CONSTANT)."""
        kids = self._children(node)
        if len(kids) != 1:
            return False
        return kids[0].type in ("id", "constant", "integer",
                                "string", "pattern_literal")

    def _find_name(self, node: tree_sitter.Node) -> str:
        """Return the text of the first 'id' child, or empty string."""
        for k in node.children:
            if not k.is_extra and k.type == "id":
                return self._text(k)
        return ""

    def _maybe_trailing_comma(self, node: tree_sitter.Node) -> None:
        """Emit TRAILING-COMMA if the last non-extra child is a comma."""
        kids = self._children(node)
        if (kids
                and not kids[-1].is_named
                and self._text(kids[-1]) == ","):
            self._w('TRAILING-COMMA')

    def _flatten_bool_chain(self, node: tree_sitter.Node,
                            op: str,
                            out: list[tree_sitter.Node]) -> None:
        """Collect operands of left-associative && or || chain."""
        kids = self._children(node)
        token_texts = {self._text(k) for k in kids if not k.is_named}
        bin_ops = token_texts & _BINARY_OPS
        if bin_ops and len(kids) == 3 and bin_ops.pop() == op:
            self._flatten_bool_chain(kids[0], op, out)
            out.append(kids[2])
        else:
            out.append(node)

    def _has_blank(self, start: int, end: int) -> bool:
        return self.source[start:end].count(b"\n") >= 2

    # ------------------------------------------------------------------
    # Comment handling
    # ------------------------------------------------------------------

    def _emit_comment(self, child: tree_sitter.Node,
                      prev: tree_sitter.Node | None) -> None:
        """Emit a comment node, classifying as trailing or leading."""
        self._maybe_blank(child)
        text = self._text(child)
        if (prev is not None
                and child.start_point[0] == prev.end_point[0]):
            self._w(f'COMMENT-TRAILING {_quote(text)}')
        else:
            self._w(f'COMMENT-LEADING {_quote(text)}')
        self._mark_content(child)

    def _emit_extras_in(self, node: tree_sitter.Node) -> None:
        """Emit comments among a node's children, classifying attachment."""
        children = node.children
        for i, child in enumerate(children):
            if not child.is_extra:
                continue
            if child.type == "nl":
                continue

            prev_content = None
            for j in range(i - 1, -1, -1):
                if not children[j].is_extra:
                    prev_content = children[j]
                    break
            self._emit_comment(child, prev_content)

    def _iter_children(self, node: tree_sitter.Node):
        """Yield non-extra children, emitting extras inline in source order.

        This is the primary mechanism for capturing interstitial comments.
        Extras (comments) are classified and emitted as a side effect
        during iteration, so callers just dispatch by child type.
        Marks each yielded child's content range after the caller
        processes it, so _maybe_blank sees the right gap.
        """
        last_non_extra = None
        for child in node.children:
            if child.is_extra:
                if child.type == "nl":
                    continue
                self._emit_comment(child, last_non_extra)
                continue
            last_non_extra = child
            yield child
            self._mark_content(child)

    def _emit_children(self, node: tree_sitter.Node) -> None:
        """Emit all children of node, handling extras and blank lines."""
        for child in self._iter_children(node):
            self._maybe_blank(child)
            self._emit(child)

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

    # ------------------------------------------------------------------
    # Main dispatch
    # ------------------------------------------------------------------

    def emit(self) -> None:
        self._emit_source_file(self.root)

    def _emit_source_file(self, node: tree_sitter.Node) -> None:
        self._emit_children(node)

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
        elif typ == "expr":
            self._emit_expr(node)
        elif typ == "id":
            self._w(f'IDENTIFIER {_quote(self._text(node))}')
        elif typ == "constant":
            self._emit_expr_child(node)
        elif typ == "type":
            self._emit_type(node)
        else:
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

        self._mark_content(node)

    # ------------------------------------------------------------------
    # Expressions
    # ------------------------------------------------------------------

    def _emit_expr(self, node: tree_sitter.Node) -> None:
        """Classify and emit an expression node."""
        self._emit_expr_inner(node)
        self._mark_content(node)

    def _emit_expr_inner(self, node: tree_sitter.Node) -> None:
        kids = self._children(node)

        if not kids:
            self._w('UNKNOWN-EXPR')
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
            self._kw(keyword)
            self._emit_expr_child(kids[1])
            self._close()
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
            return

        # Classify by token patterns in children
        token_texts = {self._text(k) for k in kids if not k.is_named}

        # Ternary: expr ? expr : expr
        if "?" in token_texts and ":" in token_texts:
            cond, then, els = [k for k in kids if k.is_named or k.type == "expr"]
            self._open('TERNARY')
            self._emit_expr(cond)
            self._w('QUESTION')
            self._emit_expr(then)
            self._w('COLON')
            self._emit_expr(els)
            self._close()
            return

        # Cardinality/absolute: | expr |
        if (len(kids) == 3
                and self._text(kids[0]) == "|"
                and self._text(kids[2]) == "|"
                and kids[1].is_named):
            self._open('CARDINALITY')
            self._w('OP "|"')
            self._emit_expr_child(kids[1])
            self._w('OP "|"')
            self._close()
            return

        # Binary operator
        bin_ops = token_texts & _BINARY_OPS
        if bin_ops and len(kids) == 3:
            op = bin_ops.pop()

            # ?$ -> HAS-FIELD
            if op == "?$":
                self._open('HAS-FIELD')
                self._emit_expr_child(kids[0])
                self._w(f'OP {_quote(op)}')
                self._emit_expr_child(kids[2])
                self._emit_extras_in(node)
                self._close()
                return

            # &&/|| chains -> BOOL-CHAIN with flat operands
            if op in ("&&", "||"):
                operands = []
                self._flatten_bool_chain(node, op, operands)
                self._open(f'BOOL-CHAIN {_quote(op)}')
                for i, operand in enumerate(operands):
                    if i > 0:
                        self._w(f'OP {_quote(op)}')
                    self._emit_expr_child(operand)
                self._emit_extras_in(node)
                self._close()
                return

            # / with atomic RHS -> DIV (tight spacing)
            if op == "/" and self._is_atomic_expr(kids[2]):
                self._open('DIV')
                self._emit_expr_child(kids[0])
                self._w(f'OP {_quote(op)}')
                self._emit_expr_child(kids[2])
                self._emit_extras_in(node)
                self._close()
                return

            self._open(f'BINARY-OP {_quote(op)}')
            self._emit_expr_child(kids[0])
            self._w(f'OP {_quote(op)}')
            self._emit_expr_child(kids[2])
            self._emit_extras_in(node)
            self._close()
            return

        # Multi-token binary operator: !in (two tokens: "!" and "in")
        if ("!" in token_texts and "in" in token_texts
                and len(kids) == 4 and not kids[1].is_named
                and not kids[2].is_named):
            self._open('BINARY-OP "!in"')
            self._emit_expr_child(kids[0])
            self._w('OP "!in"')
            self._emit_expr_child(kids[3])
            self._emit_extras_in(node)
            self._close()
            return

        # Unary operator
        if len(kids) == 2 and not kids[0].is_named:
            op_text = self._text(kids[0])
            if op_text == "!":
                self._open('NEGATION')
                self._w('OP "!"')
                self._emit_expr_child(kids[1])
                self._close()
                return
            if op_text in _UNARY_OPS:
                self._open(f'UNARY-OP {_quote(op_text)}')
                self._w(f'OP {_quote(op_text)}')
                self._emit_expr_child(kids[1])
                self._close()
                return

        # Function/event call: expr ( expr_list )
        # Keyword-named constructors (set, table, vector) as the
        # direct RHS of an initializer emit as CONSTRUCTOR (flat or
        # vertical); nested in other exprs they stay as CALL
        # (fill-pack).
        if "(" in token_texts and ")" in token_texts:
            first_text = self._text(kids[0])
            constructors = ("set", "table", "vector")
            callables = constructors + ("record", "event")
            if kids[0].is_named or first_text in callables:
                args = [k for k in kids if k.type == "expr_list"]

                if (not kids[0].is_named
                        and first_text in constructors
                        and node.parent.type == "initializer"):
                    self._emit_constructor(
                        first_text, args[0] if args else None)
                    return

                self._open('CALL')
                if kids[0].is_named:
                    self._emit_expr_child(kids[0])
                else:
                    self._w(f'IDENTIFIER {_quote(first_text)}')
                self._open('ARGS')
                self._w('LPAREN')
                if args:
                    self._emit_expr_list(args[0])
                    self._maybe_trailing_comma(args[0])
                self._w('RPAREN')
                self._close()
                self._emit_extras_in(node)
                self._close()
                return

        # Parenthesized expression: ( expr )
        if ("(" in token_texts and ")" in token_texts
                and len(kids) == 3
                and not kids[0].is_named):
            self._open('PAREN')
            self._w('LPAREN')
            self._emit_expr_child(kids[1])
            self._w('RPAREN')
            self._close()
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
            has_both = lo_exprs and hi_exprs
            tag = "SLICE" if has_both else "SLICE-PARTIAL"
            self._open(tag)
            self._emit_expr_child(base)
            self._w('LBRACKET')
            if lo_exprs:
                self._emit_expr(lo_exprs[0])
            else:
                self._w('CONSTANT ""')
            self._w('COLON')
            if hi_exprs:
                self._emit_expr(hi_exprs[0])
            else:
                self._w('CONSTANT ""')
            self._w('RBRACKET')
            self._emit_extras_in(node)
            self._close()
            return

        # Index literal (composite key): [ expr_list ]
        if ("[" in token_texts and "]" in token_texts
                and not kids[0].is_named
                and self._text(kids[0]) == "["):
            self._open('INDEX-LITERAL')
            self._open('ARGS')
            for child in self._iter_children(node):
                text = self._text(child)
                if not child.is_named and text == "[":
                    self._w('LBRACKET')
                elif not child.is_named and text == "]":
                    self._w('RBRACKET')
                elif child.type == "expr_list":
                    self._emit_expr_list(child)
                    self._maybe_trailing_comma(child)
            self._close()
            self._close()
            return

        # Index: expr [ expr_list ]
        if "[" in token_texts and "]" in token_texts and kids[0].is_named:
            base = kids[0]
            args = [k for k in kids if k.type == "expr_list"]
            self._open('INDEX')
            self._emit_expr_child(base)
            self._open('SUBSCRIPTS')
            self._w('LBRACKET')
            if args:
                self._emit_expr_list(args[0])
            self._w('RBRACKET')
            self._close()
            self._emit_extras_in(node)
            self._close()
            return

        # Field assignment in record constructor: $field = expr
        if "$" in token_texts and not kids[0].is_named:
            field_name = ""
            value_expr = None
            for k in kids:
                if k.type == "id" and not field_name:
                    field_name = self._text(k)
                elif k.type == "expr":
                    value_expr = k
            if value_expr:
                self._open(f'FIELD-ASSIGN {_quote(field_name)}')
                self._w('DOLLAR')
                self._w('ASSIGN "="')
                self._emit_expr(value_expr)
                self._close()
            else:
                self._open(f'FIELD-ASSIGN {_quote(field_name)}')
                self._w('DOLLAR')
                self._close()
            return

        # Field access: expr $ id
        if "$" in token_texts:
            base = kids[0]
            field = kids[-1]
            self._open('FIELD-ACCESS')
            self._emit_expr_child(base)
            self._w('DOLLAR')
            self._w(f'IDENTIFIER {_quote(self._text(field))}')
            self._close()
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
            self._kw("schedule")
            if interval:
                self._emit_expr(interval[0])
            self._w('LBRACE')
            if event_hdrs:
                eh = event_hdrs[0]
                args = None
                for c in self._children(eh):
                    if c.type == "expr_list":
                        args = c
                self._open('CALL')
                self._w(f'IDENTIFIER {_quote(self._find_name(eh))}')
                self._open('ARGS')
                self._w('LPAREN')
                if args:
                    self._emit_expr_list(args)
                self._w('RPAREN')
                self._close()
                self._close()
            self._w('RBRACE')
            self._close()
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
            self._emit_constructor(keyword, args[0] if args else None)
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
        elif node.type == "constant":
            kids = self._children(node)
            if kids and kids[0].type == "interval":
                ival = self._children(kids[0])
                num = self._text(ival[0]) if ival else "?"
                unit = self._text(ival[1]) if len(ival) > 1 else ""
                self._w(f'INTERVAL {_quote(num)} {_quote(unit)}')
            else:
                self._w(f'CONSTANT {_quote(self._text(node))}')
        elif node.type == "pattern":
            self._w(f'CONSTANT {_quote(self._text(node))}')
        elif node.type == "begin_lambda":
            # Part of lambda — handled by parent
            pass
        elif node.type == "func_body":
            self._emit_func_body(node)
        else:
            self._emit_expr(node)

    def _emit_constructor(self, keyword: str,
                          args_node: tree_sitter.Node | None) -> None:
        """Emit CONSTRUCTOR { KEYWORD ARGS { LPAREN expr_list RPAREN } }."""
        self._open('CONSTRUCTOR')
        self._w(f'KEYWORD {_quote(keyword)}')
        self._open('ARGS')
        self._w('LPAREN')
        if args_node:
            self._emit_expr_list(args_node)
            self._maybe_trailing_comma(args_node)
        self._w('RPAREN')
        self._close()
        self._close()

    def _emit_expr_list(self, node: tree_sitter.Node) -> None:
        """Emit children of an expr_list, emitting COMMA tokens."""
        for child in self._iter_children(node):
            if child.type == "expr":
                self._emit_expr(child)
            elif not child.is_named and self._text(child) == ",":
                self._w('COMMA')
            else:
                self._emit_expr_child(child)

    def _emit_lambda(self, node: tree_sitter.Node) -> None:
        """Emit a lambda (anonymous function) expression."""
        # Determine whether captures are present to choose the tag.
        has_captures = False
        for child in node.children:
            if not child.is_extra and child.type == "begin_lambda":
                for blc in child.children:
                    if not blc.is_extra and blc.type == "capture_list":
                        has_captures = True

        tag = 'LAMBDA-CAPTURES' if has_captures else 'LAMBDA'
        self._open(tag)
        self._kw("function")
        for child in self._iter_children(node):
            if child.type == "begin_lambda":
                # Capture list is inside begin_lambda.
                for blc in child.children:
                    if not blc.is_extra and blc.type == "capture_list":
                        self._open('CAPTURES')
                        self._w('LBRACKET')
                        for ck in self._iter_children(blc):
                            if ck.type == "capture":
                                for idk in ck.children:
                                    if not idk.is_extra and idk.type == "id":
                                        self._w(f'IDENTIFIER {_quote(self._text(idk))}')
                            elif not ck.is_named and self._text(ck) == ",":
                                self._w('COMMA')
                        self._w('RBRACKET')
                        self._close()
                self._emit_func_params_from(child)
            elif child.type == "func_body":
                self._emit_func_body(child)
        self._close()

    # ------------------------------------------------------------------
    # Types
    # ------------------------------------------------------------------

    def _emit_type(self, node: tree_sitter.Node) -> None:
        """Classify and emit a type node."""
        self._emit_type_inner(node)
        self._mark_content(node)

    def _emit_type_inner(self, node: tree_sitter.Node) -> None:
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
            self._open('SUBSCRIPTS')
            self._w('LBRACKET')
            for j, p in enumerate(params):
                if j > 0:
                    self._w('COMMA')
                self._emit_type(p)
            self._w('RBRACKET')
            self._close()
            if of_type and of_type.type == "type":
                self._kw("of")
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
                self._open('TYPE-OF "vector"')
                self._kw("of")
                self._emit_type(of_type)
                self._close()
            else:
                self._w('TYPE-ATOM "vector"')
            return

        # Function types: event(...), function(...): ret, hook(...)
        if first_text in ("event", "function", "hook"):
            has_ret = any(
                pk.type == "type"
                for c in node.children if not c.is_extra
                if c.type == "func_params"
                for pk in c.children if not pk.is_extra)
            tag = "TYPE-FUNC-RET" if has_ret else "TYPE-FUNC"
            self._open(f'{tag} {_quote(first_text)}')
            self._emit_func_params_from(node)
            self._close()
            return

        # Record type
        if first_text == "record":
            self._open('TYPE-RECORD')
            for k in self._iter_children(node):
                if not k.is_named:
                    text = self._text(k)
                    if text == "record":
                        self._kw("record")
                    elif text == "{":
                        self._w('LBRACE')
                    elif text == "}":
                        self._w('RBRACE')
                elif k.type == "type_spec":
                    self._maybe_blank(k)
                    self._emit_type_spec(k)
            self._close()
            return

        # Enum type
        if first_text == "enum":
            self._open('TYPE-ENUM')
            for k in self._iter_children(node):
                if not k.is_named:
                    text = self._text(k)
                    if text == "enum":
                        self._kw("enum")
                    elif text == "{":
                        self._w('LBRACE')
                    elif text == "}":
                        self._w('RBRACE')
                elif k.type == "enum_body":
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
        self._open(f'FIELD {_quote(self._find_name(node))}')
        for child in self._iter_children(node):
            if not child.is_named:
                if self._text(child) == ":":
                    self._w('COLON')
                elif self._text(child) == ";":
                    self._w('SEMI')
            elif child.type == "id":
                pass  # already extracted for tag
            elif child.type == "type":
                self._emit_type(child)
            elif child.type == "attr_list":
                self._emit_attr_list(child)
        self._close()

    def _emit_enum_body(self, node: tree_sitter.Node) -> None:
        """Emit enum body elements."""
        for child in self._iter_children(node):
            if child.type == "enum_body_elem":
                kids = self._children(child)
                name = ""
                init = ""
                for k in kids:
                    if k.type == "id":
                        name = self._text(k)
                    elif k.type == "constant":
                        init = self._text(k)
                tag = f'ENUM-VALUE {_quote(name)}'
                if init:
                    tag += f' {_quote("= " + init)}'
                self._w(tag)
            elif child.type == "id":
                self._w(f'ENUM-VALUE {_quote(self._text(child))}')
            elif not child.is_named and self._text(child) == ",":
                self._w('COMMA')

        self._maybe_trailing_comma(node)

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
        # Name is always the first non-extra child.
        name = "?"
        for k in node.children:
            if not k.is_extra:
                name = self._text(k)
                break
        has_expr = False
        for child in self._iter_children(node):
            if not child.is_named and self._text(child) == "=":
                if not has_expr:
                    self._open(f'ATTR {_quote(name)}')
                    has_expr = True
                self._w('ASSIGN "="')
            elif child.type == "expr":
                if not has_expr:
                    self._open(f'ATTR {_quote(name)}')
                    has_expr = True
                self._emit_expr(child)
        if has_expr:
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
            elif not child.is_named and self._text(child) == ",":
                self._w('COMMA')

    def _emit_formal_arg(self, node: tree_sitter.Node) -> None:
        self._open(f'PARAM {_quote(self._find_name(node))}')
        for child in self._iter_children(node):
            if not child.is_named:
                if self._text(child) == ":":
                    self._w('COLON')
            elif child.type == "id":
                pass  # already extracted for PARAM tag
            elif child.type == "type":
                self._emit_type(child)
            elif child.type == "attr_list":
                self._emit_attr_list(child)
        self._close()

    def _emit_func_params_from(self, node: tree_sitter.Node) -> None:
        """Extract and emit PARAMS, RETURNS, and ATTR-LIST from a func_hdr, type, or begin_lambda child."""
        # func_params wraps formal_args + return type (function/hook decls).
        # For event/hook types, formal_args and parens are direct children.
        has_func_params = any(c.type == "func_params"
                             for c in node.children if not c.is_extra)
        if has_func_params:
            for child in self._iter_children(node):
                if child.type == "func_params":
                    formal = None
                    ret_type = None
                    for pk in self._iter_children(child):
                        if pk.type == "formal_args":
                            formal = pk
                        elif pk.type == "type":
                            ret_type = pk
                    self._open('PARAMS')
                    self._w('LPAREN')
                    if formal:
                        self._emit_formal_args(formal)
                    self._w('RPAREN')
                    self._close()
                    if ret_type:
                        self._w('COLON')
                        self._open('RETURNS')
                        self._emit_type(ret_type)
                        self._close()
                elif child.type == "attr_list":
                    self._emit_attr_list(child)
        else:
            formal = None
            for child in self._iter_children(node):
                if child.type == "formal_args":
                    formal = child
            self._open('PARAMS')
            self._w('LPAREN')
            if formal:
                self._emit_formal_args(formal)
            self._w('RPAREN')
            self._close()

    # ------------------------------------------------------------------
    # Declarations
    # ------------------------------------------------------------------

    def _emit_var_decl(self, node: tree_sitter.Node, tag: str) -> None:
        """Emit a variable declaration (global, const, option, redef, local)."""
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

        self._open(tag)
        self._kw(keyword)
        self._w(f'IDENTIFIER {_quote(name)}')
        if typ:
            self._open('DECL-TYPE')
            self._w('COLON')
            self._emit_type(typ)
            self._close()
        if init_expr:
            self._open('DECL-INIT')
            self._w(f'ASSIGN {_quote(init_op)}')
            self._emit_init_expr(init_expr, typ)
            self._close()
        if attrs:
            self._emit_attr_list(attrs)
        self._emit_extras_in(node)
        self._w('SEMI')
        self._close()

    def _emit_global_decl(self, node: tree_sitter.Node) -> None:
        self._emit_var_decl(node, 'GLOBAL-DECL')

    def _emit_local_decl(self, node: tree_sitter.Node) -> None:
        self._emit_var_decl(node, 'LOCAL-DECL')

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
                self._emit_constructor(
                    ctor_name, args[0] if args else None)
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

        has_ret = hdr_inner and any(
            pk.type == "type"
            for c in hdr_inner.children if not c.is_extra
            if c.type == "func_params"
            for pk in c.children if not pk.is_extra)
        tag = "FUNC-DECL-RET" if has_ret else "FUNC-DECL"
        self._open(tag)
        self._kw(kind)
        self._w(f'IDENTIFIER {_quote(name)}')
        if hdr_inner:
            self._emit_func_params_from(hdr_inner)
        if func_body:
            self._emit_func_body(func_body)
        self._emit_extras_in(node)
        self._close()

    def _emit_func_body(self, node: tree_sitter.Node) -> None:
        self._open('BODY')
        for child in self._iter_children(node):
            if child.type == "stmt_list":
                self._emit_stmt_list(child)
            elif not child.is_named:
                text = self._text(child)
                if text == "{":
                    self._w('LBRACE')
                elif text == "}":
                    self._w('RBRACE')
        self._close()

    def _emit_export_decl(self, node: tree_sitter.Node) -> None:
        self._open('EXPORT')
        for child in self._iter_children(node):
            if not child.is_named:
                text = self._text(child)
                if text == "export":
                    self._kw("export")
                elif text == "{":
                    self._w('LBRACE')
                elif text == "}":
                    self._w('RBRACE')
            elif child.type == "decl":
                inner = self._children(child)
                if inner:
                    self._maybe_blank(inner[0])
                    self._emit(inner[0])
        self._close()

    def _emit_module_decl(self, node: tree_sitter.Node) -> None:
        name = ""
        for child in self._iter_children(node):
            if child.type == "id":
                name = self._text(child)
        self._open(f'MODULE {_quote(name)}')
        self._kw("module")
        self._w(f'IDENTIFIER {_quote(name)}')
        self._w('SEMI')
        self._close()

    def _emit_type_decl(self, node: tree_sitter.Node) -> None:
        name = ""
        # Determine the type-decl variant by inspecting the type child.
        variant = "TYPEDECL-ALIAS"
        for k in node.children:
            if not k.is_extra and k.type == "id":
                name = self._text(k)
            elif not k.is_extra and k.type == "type":
                type_kids = self._children(k)
                if type_kids:
                    ft = self._text(type_kids[0])
                    if ft == "record":
                        variant = "TYPEDECL-RECORD"
                    elif ft == "enum":
                        variant = "TYPEDECL-ENUM"
        self._open(f'{variant} {_quote(name)}')
        for child in self._iter_children(node):
            if not child.is_named:
                text = self._text(child)
                if text == "type":
                    self._kw("type")
                elif text == ":":
                    self._w('COLON')
                    self._w('SP')
                elif text == "=":
                    self._w('ASSIGN "="')
            elif child.type == "id":
                self._w(f'IDENTIFIER {_quote(self._text(child))}')
            elif child.type == "type" and child.is_named:
                self._emit_type(child)
        self._w('SEMI')
        self._close()

    def _emit_redef_record_decl(self, node: tree_sitter.Node) -> None:
        self._open(f'REDEF-RECORD {_quote(self._find_name(node))}')
        for child in self._iter_children(node):
            if not child.is_named:
                text = self._text(child)
                if text in ("redef", "record"):
                    self._kw(text)
                elif text == "+=":
                    self._w('ASSIGN "+="')
                elif text == "{":
                    self._w('LBRACE')
                elif text == "}":
                    self._w('RBRACE')
                elif text == ";":
                    self._w('SEMI')
            elif child.type == "id":
                self._w(f'IDENTIFIER {_quote(self._text(child))}')
            elif child.type == "type_spec":
                self._maybe_blank(child)
                self._emit_type_spec(child)
        self._close()

    def _emit_redef_enum_decl(self, node: tree_sitter.Node) -> None:
        self._open(f'REDEF-ENUM {_quote(self._find_name(node))}')
        for child in self._iter_children(node):
            if not child.is_named:
                text = self._text(child)
                if text in ("redef", "enum"):
                    self._kw(text)
                elif text == "+=":
                    self._w('ASSIGN "+="')
                elif text == "{":
                    self._w('LBRACE')
                elif text == "}":
                    self._w('RBRACE')
                elif text == ";":
                    self._w('SEMI')
            elif child.type == "id":
                self._w(f'IDENTIFIER {_quote(self._text(child))}')
            elif child.type == "enum_body":
                self._emit_enum_body(child)
        self._close()

    # ------------------------------------------------------------------
    # Statements
    # ------------------------------------------------------------------

    def _emit_stmt_list(self, node: tree_sitter.Node) -> None:
        self._emit_children(node)

    def _emit_stmt(self, node: tree_sitter.Node) -> None:
        """Classify and emit a statement node."""
        self._emit_stmt_inner(node)
        self._mark_content(node)

    def _emit_stmt_inner(self, node: tree_sitter.Node) -> None:
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
        if first_text == "assert":
            self._emit_assert(node)
            return
        if first_text in ("next", "break", "fallthrough"):
            self._w(f'KEYWORD {_quote(first_text)}')
            self._w('SEMI')
            return

        # Brace block (standalone { stmt_list })
        if first_text == "{":
            self._open('BLOCK')
            self._w('LBRACE')
            for k in self._iter_children(node):
                if k.type == "stmt_list":
                    self._emit_stmt_list(k)
            self._w('RBRACE')
            self._close()
            return

        # Expression statement
        expr_kids = [k for k in kids if k.type == "expr"]
        if expr_kids:
            self._open('EXPR-STMT')
            self._emit_expr(expr_kids[0])
            self._emit_extras_in(node)
            self._w('SEMI')
            self._close()
            return

        self._emit_unknown(node)

    def _emit_if_extras(self, node, after_node, before_node,
                         ref_node=None,
                         skip_trailing=False) -> bool:
        """Emit extra children of `node` between `after_node` and
        `before_node`.  `ref_node` is used for same-line detection.
        If `skip_trailing`, same-line comments are skipped (already
        captured inside the BODY node by the caller)."""
        past_after = after_node is None
        had_extras = False
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
                continue
            if (skip_trailing
                    and ref_node is not None
                    and child.start_point[0]
                        == ref_node.end_point[0]):
                continue
            self._emit_comment(child, ref_node)
            had_extras = True

        return had_extras

    def _emit_if(self, node: tree_sitter.Node) -> None:
        # Classify non-extra children.
        kids = self._children(node)
        cond = None
        body = None
        else_kw = None
        else_body = None
        found_else = False
        for k in kids:
            if k.type == "expr" and cond is None:
                cond = k
            elif k.type == "stmt" and not found_else:
                body = k
            elif self._text(k) == "else":
                found_else = True
                else_kw = k
            elif k.type == "stmt" and found_else:
                else_body = k

        self._open('IF-WITH-ELSE' if else_body else 'IF-NO-ELSE')
        self._kw("if")
        if cond:
            self._w('LPAREN')
            self._emit_expr(cond)
            self._w('RPAREN')

        # Emit extras between cond and body (e.g. #@ annotation
        # after the closing paren of the condition).
        self._emit_if_extras(node, cond, body, ref_node=cond)

        if body:
            self._open('BODY')
            self._emit_stmt(body)
            # Capture same-line trailing comment inside BODY.
            if else_kw:
                for child in node.children:
                    if not child.is_extra:
                        continue
                    if child.type == "nl":
                        continue
                    if child.start_point[0] != body.end_point[0]:
                        continue
                    if child.start_byte <= body.end_byte:
                        continue
                    if child.start_byte >= else_kw.start_byte:
                        continue
                    text = self._text(child)
                    self._w(f'COMMENT-TRAILING {_quote(text)}')
                    self._mark_content(child)
            self._close()

        # Emit extras between body and else, then check for a
        # blank line before the else keyword.
        if else_kw:
            had_extras = self._emit_if_extras(
                node, body, else_body, ref_node=body,
                skip_trailing=True)
            if had_extras:
                self._maybe_blank(else_kw)
            elif (body is not None
                    and self._has_blank(body.end_byte,
                                        else_kw.start_byte)):
                self._w("BLANK")

        if else_body:
            else_kids = self._children(else_body)
            first_text = self._text(else_kids[0]) if else_kids else ""
            if first_text == "if":
                self._open('ELSE-IF')
            else:
                self._open('ELSE-BODY')
            self._kw("else")
            self._emit_stmt(else_body)
            self._close()
        self._close()

    def _emit_for(self, node: tree_sitter.Node) -> None:
        # Pre-scan non-extra children (not _iter_children, which emits
        # extras as a side effect) to detect the for-loop variant.
        raw = self._children(node)
        texts = [self._text(c) if not c.is_named else c.type
                 for c in raw]
        has_bracket = "[" in texts
        has_val_after_bracket = False
        if has_bracket:
            try:
                rb = texts.index("]")
                in_idx = texts.index("in")
                has_val_after_bracket = any(
                    raw[i].type == "id"
                    for i in range(rb + 1, in_idx))
            except ValueError:
                pass

        id_count = 0
        if not has_bracket:
            for c in raw:
                if not c.is_named and self._text(c) == "in":
                    break
                if c.type == "id":
                    id_count += 1

        if has_bracket and has_val_after_bracket:
            cond_tag = 'FOR-COND-BRACKET-VAL'
        elif has_bracket:
            cond_tag = 'FOR-COND-BRACKET'
        elif id_count >= 2:
            cond_tag = 'FOR-COND-VAL'
        else:
            cond_tag = 'FOR-COND'

        self._open('FOR')

        in_cond = False
        in_vars = False
        for child in self._iter_children(node):
            if not child.is_named:
                text = self._text(child)
                if text == "for":
                    self._kw("for")
                elif text == "(":
                    self._w('LPAREN')
                elif text == ")":
                    if in_cond:
                        self._close()
                        in_cond = False
                    self._w('RPAREN')
                elif text == "[":
                    if not in_cond:
                        self._open(cond_tag)
                        in_cond = True
                    self._open('VARS')
                    self._w('LBRACKET')
                elif text == "]":
                    self._w('RBRACKET')
                    self._close()  # close VARS
                elif text == "in":
                    if in_vars:
                        self._close()  # close VARS (non-bracket)
                        in_vars = False
                    self._w('KEYWORD "in"')
                elif text == ",":
                    self._w('COMMA')
            elif child.type == "id":
                if not in_cond:
                    self._open(cond_tag)
                    in_cond = True
                if not has_bracket and not in_vars:
                    pass  # ids are direct children of FOR-COND*
                self._w(f'IDENTIFIER {_quote(self._text(child))}')
            elif child.type == "expr":
                if not in_cond:
                    self._open(cond_tag)
                    in_cond = True
                self._emit_expr(child)
            elif child.type == "stmt":
                if in_cond:
                    self._close()
                    in_cond = False
                self._open('BODY')
                self._emit_stmt(child)
                self._close()

        if in_cond:
            self._close()
        self._close()

    def _emit_while(self, node: tree_sitter.Node) -> None:
        self._open('WHILE')
        for child in self._iter_children(node):
            if not child.is_named:
                text = self._text(child)
                if text == "while":
                    self._kw("while")
                elif text == "(":
                    self._w('LPAREN')
                elif text == ")":
                    self._w('RPAREN')
            elif child.type == "expr":
                self._emit_expr(child)
            elif child.type == "stmt":
                self._open('BODY')
                self._emit_stmt(child)
                self._close()
        self._close()

    def _emit_return(self, node: tree_sitter.Node) -> None:
        has_expr = any(k.type == "expr" for k in self._children(node))
        self._open('RETURN' if has_expr else 'RETURN-VOID')
        self._kw("return")
        for child in self._iter_children(node):
            if child.type == "expr":
                self._emit_expr(child)
        self._w('SEMI')
        self._close()

    def _emit_print(self, node: tree_sitter.Node) -> None:
        self._open('PRINT')
        self._kw("print")
        for child in self._iter_children(node):
            if child.type == "expr_list":
                self._emit_expr_list(child)
        self._w('SEMI')
        self._close()

    def _emit_event_stmt(self, node: tree_sitter.Node) -> None:
        """Event statement: event name(args);"""
        # The id and expr_list live inside an event_hdr child.
        hdr = None
        for k in node.children:
            if not k.is_extra and k.type == "event_hdr":
                hdr = k
                break
        name = self._find_name(hdr) if hdr else ""
        self._open(f'EVENT-STMT {_quote(name)}')
        self._kw("event")
        if hdr:
            for child in self._iter_children(hdr):
                if child.type == "id":
                    pass  # already extracted for tag
                elif child.type == "expr_list":
                    self._open('ARGS')
                    self._w('LPAREN')
                    self._emit_expr_list(child)
                    self._w('RPAREN')
                    self._close()
        self._emit_extras_in(node)
        self._w('SEMI')
        self._close()

    def _emit_switch(self, node: tree_sitter.Node) -> None:
        self._open('SWITCH')
        self._kw("switch")
        for child in self._iter_children(node):
            if not child.is_named:
                text = self._text(child)
                if text == "(":
                    self._w('LPAREN')
                elif text == ")":
                    self._w('RPAREN')
                elif text == "{":
                    self._w('LBRACE')
                elif text == "}":
                    self._w('RBRACE')
            elif child.type == "expr":
                self._emit_expr(child)
            elif child.type == "case_list":
                self._emit_case_list(child)
        self._close()

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
                self._kw("case")
                if exprs:
                    self._open('VALUES')
                    self._emit_expr_list(exprs)
                    self._close()
                self._w('COLON')
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
                self._w('KEYWORD "default"')
                self._w('COLON')
                if stmts:
                    self._emit_stmt_list(stmts)
                    i += 1
                self._close()
            else:
                i += 1

    def _emit_when(self, node: tree_sitter.Node) -> None:
        self._open('WHEN')
        self._kw("when")
        state = "init"
        in_timeout = False
        for child in self._iter_children(node):
            if not child.is_named:
                text = self._text(child)
                if text == "timeout":
                    self._kw("timeout")
                    state = "timeout"
                elif text == "(":
                    self._w('LPAREN')
                elif text == ")":
                    self._w('RPAREN')
                elif text == "{":
                    self._w('LBRACE')
                elif text == "}":
                    self._w('RBRACE')
                continue
            if state == "init":
                if child.type == "expr":
                    self._emit_expr(child)
                elif child.type == "stmt":
                    self._open('BODY')
                    self._emit_stmt(child)
                    self._close()
                    state = "after_body"
            elif state == "after_body":
                pass
            elif state == "timeout":
                if child.type == "expr":
                    self._open('TIMEOUT')
                    in_timeout = True
                    self._emit_expr(child)
                elif child.type == "stmt_list":
                    self._open('BODY')
                    self._emit_stmt_list(child)
                    self._close()
        if in_timeout:
            self._close()
        self._close()

    def _emit_add_delete(self, node: tree_sitter.Node, keyword: str) -> None:
        self._open(keyword.upper())
        self._kw(keyword)
        for child in self._iter_children(node):
            if child.type == "expr":
                self._emit_expr(child)
        self._w('SEMI')
        self._close()

    def _emit_assert(self, node: tree_sitter.Node) -> None:
        self._open('ASSERT')
        self._kw("assert")
        for child in self._iter_children(node):
            if child.type == "expr":
                self._emit_expr(child)
        self._w('SEMI')
        self._close()

    # ------------------------------------------------------------------
    # Preprocessor
    # ------------------------------------------------------------------

    def _emit_preproc(self, node: tree_sitter.Node) -> None:
        kids = self._children(node)
        directive = self._text(kids[0]) if kids else "?"
        args = [k for k in kids[1:] if k.is_named]
        is_cond = directive in ("@if", "@ifdef", "@ifndef")
        if args:
            arg_strs = " ".join(_quote(self._text(a)) for a in args)
            if is_cond:
                self._open(f'PREPROC-COND {_quote(directive)} {arg_strs}')
                self._w('LPAREN')
                self._w('RPAREN')
                self._close()
            else:
                self._w(f'PREPROC {_quote(directive)} {arg_strs}')
        else:
            self._w(f'PREPROC {_quote(directive)}')

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
