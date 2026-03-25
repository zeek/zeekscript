"""IR-based formatter for zeekscript.

Builds a Wadler/Lindig-style document IR from the parse tree, then resolves
it to formatted output. Replaces the hint/linebreaker machinery in
formatter.py + output.py with a single-pass tree-to-Doc builder and a
resolve step.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

from .ir import (
    COLUMN0LINE, EMPTY, HARDLINE, LINE, SOFTLINE, SPACE,
    Concat, Doc, HardLine,
    align, concat, fill, group, if_break, intersperse,
    join, nest, resolve, text,
)
from .node import Node

if TYPE_CHECKING:
    from .script import Script


# ---------------------------------------------------------------------------
# Helpers for inspecting nodes
# ---------------------------------------------------------------------------

def _tok(node: Node) -> str | None:
    """Token string of a terminal node, or None."""
    return node.type if not node.is_named else None


def _name(node: Node) -> str | None:
    """Grammar symbol name of a named node, or None."""
    return node.type if node.is_named else None


def _ch(node: Node, idx: int = 0) -> Node | None:
    """Return the idx-th non-error child, or None."""
    kids = node.nonerr_children
    return kids[idx] if idx < len(kids) else None


def _child_tok(node: Node, idx: int = 0) -> str | None:
    """Token string of the idx-th non-error child."""
    c = _ch(node, idx)
    return _tok(c) if c else None


def _child_name(node: Node, idx: int = 0) -> str | None:
    """Grammar name of the idx-th non-error child."""
    c = _ch(node, idx)
    return _name(c) if c else None


def _nch(node: Node) -> int:
    """Number of non-error children."""
    return len(node.nonerr_children)


def _source(node: Node, script: Script) -> bytes:
    """Raw source bytes for a node."""
    return script.get_content(*node.script_range())


def _source_str(node: Node, script: Script) -> str:
    return _source(node, script).decode("UTF-8", errors="replace")


def _is_comment(node: Node) -> bool:
    return node.is_named and node.type is not None and node.type.endswith("_comment")


def _is_nl(node: Node) -> bool:
    return node.is_named and node.type == "nl"


def _find_semi(kids: list[Node]) -> Node | None:
    """Find the ';' token in a list of children."""
    for k in kids:
        if _tok(k) == ";":
            return k
    return None


def _format_semi(kids: list[Node], script: Script) -> Doc:
    """Format a ';' token with its CST siblings (trailing comments)."""
    semi = _find_semi(kids)
    if semi is None:
        return text(";")
    return format_child(semi, script)


# ---------------------------------------------------------------------------
# Comment handling
# ---------------------------------------------------------------------------

def _format_prev_cst(node: Node, script: Script) -> Doc:
    """Format prev_cst_siblings (comments/newlines before an AST node)."""
    parts: list[Doc] = []
    nl_count = 0
    for sib in node.prev_cst_siblings:
        if sib.no_format is not None:
            continue
        if _is_nl(sib):
            nl_count += 1
        elif _is_comment(sib):
            if nl_count >= 2:
                parts.append(HARDLINE)  # blank line before comment
            nl_count = 0
            parts.append(text(_source_str(sib, script)))
            parts.append(HARDLINE)
        else:
            nl_count = 0
    # If there are 2+ trailing NLs (blank line between comments and the node),
    # emit the blank line.
    if nl_count >= 2 and parts:
        parts.append(HARDLINE)
    return concat(*parts)


def _format_next_cst(node: Node, script: Script) -> Doc:
    """Format next_cst_siblings (trailing comments after an AST node)."""
    parts: list[Doc] = []
    for sib in node.next_cst_siblings:
        if sib.no_format is not None:
            continue
        if sib.is_minor_comment():
            # End-of-line comment
            if sib.prev_cst_sibling and not sib.prev_cst_sibling.is_nl():
                parts.append(SPACE)
                parts.append(text(_source_str(sib, script)))
            else:
                # Own-line comment
                parts.append(HARDLINE)
                parts.append(text(_source_str(sib, script)))
        elif sib.is_zeekygen_prev_comment():
            # ##< comments - space then comment
            parts.append(SPACE)
            parts.append(text(_source_str(sib, script)))
        elif _is_comment(sib):
            parts.append(HARDLINE)
            parts.append(text(_source_str(sib, script)))
        # Skip newlines - they're handled by the structure
    return concat(*parts)


def _wants_blank_before(node: Node) -> bool:
    """True if there should be a blank line before this node.

    Returns True only when the blank line is NOT already handled by
    _format_prev_cst. When comments are present in prev_cst_siblings,
    _format_prev_cst handles all blank lines (both before first comment
    and between last comment and node), so this returns False.

    Only returns True for blank lines with no associated comments.
    """
    sibs = node.prev_cst_siblings
    has_comments = any(_is_comment(sib) for sib in sibs)
    if has_comments:
        # _format_prev_cst handles blank lines around comments
        return False

    nl_count = 0
    for sib in sibs:
        if _is_nl(sib):
            nl_count += 1
            if nl_count >= 2:
                return True
        else:
            nl_count = 0
    return False


def _has_comments(node: Node) -> bool:
    """True if node's subtree contains comments."""
    for child, _ in node.traverse(include_cst=True):
        if child == node:
            continue
        if _is_comment(child):
            return True
    return False


def _compact_length(node: Node) -> int:
    """Estimate single-line formatted length of a node's subtree."""
    length = 0
    comma_count = 0
    for child, _ in node.traverse(include_cst=True):
        if _is_nl(child):
            continue
        if not child.nonerr_children:  # leaf
            length += child.end_byte - child.start_byte
        if _tok(child) == ",":
            comma_count += 1
    return length + comma_count


# ---------------------------------------------------------------------------
# Core: format a single node, wrapping with CST siblings
# ---------------------------------------------------------------------------

def format_child(node: Node, script: Script) -> Doc:
    """Format a node including its CST siblings (comments, errors).

    This is the main entry point for formatting any AST node.
    """
    # Handle no-format annotations
    if isinstance(node.no_format, bytes):
        return text(node.no_format.decode("UTF-8", errors="replace"))
    if node.no_format is True:
        return EMPTY

    parts: list[Doc] = []

    # Leading CST (comments before this node)
    prev = _format_prev_cst(node, script)
    if prev != EMPTY:
        parts.append(prev)

    # The node itself
    parts.append(format_node(node, script))

    # Trailing CST (comments after this node)
    nxt = _format_next_cst(node, script)
    if nxt != EMPTY:
        parts.append(nxt)

    return concat(*parts)


def format_node(node: Node, script: Script) -> Doc:
    """Format a node (without CST siblings) by dispatching to type-specific formatter."""
    # Error nodes: preserve source
    if node.is_error():
        return _format_error(node, script)

    # Terminal/token nodes
    if not node.is_named:
        return text(_source_str(node, script))

    # Named node: dispatch
    ntype = node.type
    if ntype and ntype in _FORMATTERS:
        return _FORMATTERS[ntype](node, script)

    # Leaf named nodes (no children) - emit source
    if not node.children:
        return text(_source_str(node, script))

    # Default: space-separate children
    return _format_space_separated(node, script)


def _format_error(node: Node, script: Script) -> Doc:
    """Preserve error nodes mostly as-is."""
    if not node.children:
        return text(_source_str(node, script))
    # Format children that have structure, preserve gaps as raw source
    parts: list[Doc] = []
    start = node.start_byte
    for child in node.children:
        if child.children:
            if child.start_byte > start:
                raw = script.get_content(start, child.start_byte)
                parts.append(text(raw.decode("UTF-8", errors="replace")))
            parts.append(format_child(child, script))
            start = child.end_byte
        # Leaf error children: handled by gap filling
    # Remaining gap
    _, end = node.script_range(with_cst=True)
    if start < end:
        raw = script.get_content(start, end)
        parts.append(text(raw.decode("UTF-8", errors="replace")))
    return concat(*parts)


def _format_space_separated(node: Node, script: Script) -> Doc:
    """Default formatter: space-separate all children."""
    kids = node.nonerr_children
    if not kids:
        return text(_source_str(node, script))
    parts: list[Doc] = []
    for i, child in enumerate(kids):
        if i > 0:
            parts.append(SPACE)
        parts.append(format_child(child, script))
    return concat(*parts)


# ---------------------------------------------------------------------------
# Statement list / body helpers
# ---------------------------------------------------------------------------

def _is_preproc_item(node: Node) -> bool:
    """True if this node contains a preproc_directive as its first child."""
    if not node.nonerr_children:
        return False
    first = _child_name(node, 0)
    if first == "preproc_directive":
        return True
    # decl > stmt > preproc_directive
    if first == "stmt":
        return _is_preproc_item(node.nonerr_children[0])
    return False


def _preproc_token(node: Node) -> str | None:
    """Return the preproc keyword (@if, @endif, etc.) or None."""
    n = node
    while n and n.nonerr_children:
        first = n.nonerr_children[0]
        if _name(first) == "preproc_directive":
            kids = first.nonerr_children
            return _tok(kids[0]) if kids else None
        if _name(first) in ("stmt", "decl"):
            n = first
        else:
            break
    return None


def _format_body_items(nodes: list[Node], script: Script,
                       top_level: bool = False) -> Doc:
    """Format a list of statements/declarations, preserving blank lines.

    Each item ends with its own HARDLINE (except preproc stmts which have
    no trailing HARDLINE). Preproc directives output at column 0 via
    COLUMN0LINE transitions. At top level, content between @if/@endif gets
    an extra indent level via nest().
    """
    parts: list[Doc] = []
    prev_was_preproc = False
    preproc_depth = 0

    # Two-pass: first collect items with their preproc depth, then build doc.
    items: list[tuple[Node, int]] = []
    for node in nodes:
        is_preproc = _is_preproc_item(node)
        if is_preproc:
            tok = _preproc_token(node)
            if tok in ("@endif", "@else"):
                preproc_depth = max(0, preproc_depth - 1)
            items.append((node, preproc_depth))
            if tok in ("@if", "@ifdef", "@ifndef", "@else"):
                preproc_depth += 1
        else:
            items.append((node, preproc_depth))

    for i, (node, depth) in enumerate(items):
        is_preproc = _is_preproc_item(node)
        # Only apply preproc indent at top level (matching old formatter)
        effective_depth = depth if top_level else 0

        blank_before = i > 0 and _wants_blank_before(node)
        if blank_before:
            parts.append(HARDLINE)
        if is_preproc:
            parts.append(COLUMN0LINE)
            if effective_depth > 0:
                parts.append(text("\t" * effective_depth))
            parts.append(format_child(node, script))
        else:
            doc = format_child(node, script)
            if effective_depth > 0:
                if prev_was_preproc:
                    parts.append(nest(effective_depth, concat(HARDLINE, doc)))
                else:
                    parts.append(nest(effective_depth, doc))
            else:
                if prev_was_preproc:
                    parts.append(HARDLINE)
                parts.append(doc)
        prev_was_preproc = is_preproc
    if prev_was_preproc:
        parts.append(HARDLINE)
    return concat(*parts)


def _format_stmt_list(node: Node, script: Script) -> Doc:
    """Format a stmt_list node."""
    stmts = [c for c in node.nonerr_children if _name(c) == "stmt"]
    if not stmts:
        return EMPTY
    return _format_body_items(stmts, script)


def _format_whitesmith_block(stmts_doc: Doc) -> Doc:
    """Wrap statements in Whitesmith-style braces.

    Whitesmith: { and } indented to same level as body.
    Includes its own leading newline so the { gets proper indentation.
    stmts_doc is expected to end with HARDLINE (from last statement).
    """
    return nest(1, concat(
        HARDLINE,
        text("{"),
        HARDLINE,
        stmts_doc,
        text("}"),
    ))


def _format_curly_stmt_list(node: Node, script: Script, start_idx: int) -> tuple[Doc, int]:
    """Format '{' stmt_list '}' starting at start_idx in node.nonerr_children.

    Returns (doc, next_idx).
    """
    kids = node.nonerr_children
    idx = start_idx

    # Expect '{'
    assert _tok(kids[idx]) == "{"
    idx += 1

    # stmt_list (optional)
    stmts_doc = EMPTY
    if idx < len(kids) and _name(kids[idx]) == "stmt_list":
        # Include any CST siblings (comments between { and first stmt)
        pre_stmts = _format_prev_cst(kids[idx], script)
        kids[idx].prev_cst_siblings = []  # consumed
        stmts_doc = concat(pre_stmts, _format_stmt_list(kids[idx], script))
        idx += 1

    # '}'
    assert _tok(kids[idx]) == "}"

    # Check for comments before '}'
    close_brace = kids[idx]
    pre_close = _format_prev_cst(close_brace, script)
    close_brace.prev_cst_siblings = []  # consumed

    idx += 1

    if stmts_doc == EMPTY and pre_close == EMPTY:
        return text("{ }"), idx

    body = concat(stmts_doc, pre_close) if pre_close != EMPTY else stmts_doc
    return _format_whitesmith_block(body), idx


# ---------------------------------------------------------------------------
# Declaration formatters
# ---------------------------------------------------------------------------

def _format_source_file(node: Node, script: Script) -> Doc:
    """Top-level source_file: sequence of decls/stmts with blank line preservation."""
    items = [c for c in node.nonerr_children if _name(c) in ("decl", "stmt")]
    if not items:
        return EMPTY
    return _format_body_items(items, script, top_level=True)


def _format_decl(node: Node, script: Script) -> Doc:
    """A decl just wraps its single child."""
    kids = node.nonerr_children
    if len(kids) == 1:
        return format_child(kids[0], script)
    return _format_space_separated(node, script)


def _format_module_decl(node: Node, script: Script) -> Doc:
    # module <name> ;
    kids = node.nonerr_children
    return concat(
        text("module"),
        SPACE,
        format_child(kids[1], script),
        text(";"),
        HARDLINE,
    )


def _format_export_decl(node: Node, script: Script) -> Doc:
    # export { <decl>... }
    # Brace on same line, closing brace unindented
    kids = node.nonerr_children
    decls = [c for c in kids if _name(c) == "decl"]

    close_brace = kids[-1]
    pre_close = _format_prev_cst(close_brace, script)
    close_brace.prev_cst_siblings = []

    body = _format_body_items(decls, script)
    if pre_close != EMPTY:
        body = concat(body, HARDLINE, pre_close)

    return concat(
        text("export"),
        SPACE,
        text("{"),
        nest(1, concat(HARDLINE, body)),
        text("}"),
        HARDLINE,
    )


def _format_typed_initializer(kids: list[Node], start_idx: int, script: Script) -> tuple[Doc, int]:
    """Format [: <type>] [<initializer>] [<attr_list>] sequence.

    Returns (doc, next_idx).
    """
    idx = start_idx
    parts: list[Doc] = []

    has_explicit_type = idx < len(kids) and _tok(kids[idx]) == ":"
    if has_explicit_type:
        idx += 1  # skip ':'
        parts.append(text(":"))
        parts.append(SPACE)
        parts.append(format_child(kids[idx], script))  # <type>
        idx += 1

    if idx < len(kids) and _name(kids[idx]) == "initializer":
        parts.append(SPACE)
        # Transform { } to set()/table() when no explicit type
        transform_braces = not has_explicit_type
        parts.append(_format_initializer_node(kids[idx], script, transform_braces))
        idx += 1

    if idx < len(kids) and _name(kids[idx]) == "attr_list":
        parts.append(SPACE)
        parts.append(format_child(kids[idx], script))
        idx += 1

    return concat(*parts), idx


def _format_global_decl(node: Node, script: Script) -> Doc:
    # global/option/const/redef <id> [: <type>] [= <expr>] [<attr_list>] ;
    kids = node.nonerr_children
    keyword = _source_str(kids[0], script)
    id_doc = format_child(kids[1], script)

    typed_init, idx = _format_typed_initializer(kids, 2, script)
    # Semicolon - use format_child to pick up trailing comments
    semi = format_child(kids[idx], script) if idx < len(kids) and _tok(kids[idx]) == ";" else EMPTY

    # Wrap: keyword id [typed_init] ;
    # Alignment: continuation aligns one past the identifier
    inner = concat(id_doc, typed_init) if typed_init != EMPTY else id_doc
    return concat(
        text(keyword),
        SPACE,
        group(align(concat(inner, semi))),
        HARDLINE,
    )


def _format_type_decl(node: Node, script: Script) -> Doc:
    # type <id> : <type> [<attr_list>] ;
    kids = node.nonerr_children
    parts = [text("type"), SPACE]
    idx = 1
    parts.append(format_child(kids[idx], script))  # <id>
    idx += 1
    parts.append(text(":"))  # ':'
    idx += 1
    parts.append(SPACE)
    parts.append(format_child(kids[idx], script))  # <type>
    idx += 1
    if idx < len(kids) and _name(kids[idx]) == "attr_list":
        parts.append(SPACE)
        parts.append(format_child(kids[idx], script))
        idx += 1
    parts.append(text(";"))
    parts.append(HARDLINE)
    return concat(*parts)


def _format_type(node: Node, script: Script) -> Doc:
    """Format type nodes: record, enum, set, table, function, event, hook, etc."""
    kids = node.nonerr_children
    ct1 = _child_tok(node, 0)

    if ct1 == "record":
        return _format_type_record(node, script)
    elif ct1 == "enum":
        return _format_type_enum(node, script)
    elif ct1 == "set":
        return _format_type_set_or_table(node, script, "set")
    elif ct1 == "table":
        return _format_type_set_or_table(node, script, "table")
    elif ct1 == "function":
        # function<func_params> — no space before '('
        return concat(*[format_child(c, script) for c in kids])
    elif ct1 in ("event", "hook"):
        # event/hook ( [formal_args] )
        parts = [format_child(kids[0], script)]
        # '('
        if len(kids) > 1 and _tok(kids[1]) == "(":
            parts.append(text("("))
            inner_parts: list[Doc] = []
            idx = 2
            while idx < len(kids) and _tok(kids[idx]) != ")":
                inner_parts.append(format_child(kids[idx], script))
                idx += 1
            if inner_parts:
                parts.append(align(concat(*inner_parts, text(")"))))
            else:
                parts.append(text(")"))
        return concat(*parts)
    else:
        # vector of <type>, or other simple types
        return _format_space_separated(node, script)


def _format_type_record(node: Node, script: Script) -> Doc:
    # record { [type_spec...] }
    kids = node.nonerr_children
    type_specs = [c for c in kids if _name(c) == "type_spec"]

    if not type_specs:
        return concat(text("record"), SPACE, text("{ }"))

    body_parts: list[Doc] = []
    for i, ts in enumerate(type_specs):
        if i > 0:
            body_parts.append(HARDLINE)
            if _wants_blank_before(ts):
                body_parts.append(HARDLINE)
        body_parts.append(format_child(ts, script))

    body = concat(*body_parts)

    # Same-line brace: { on same line as record, } at base indent
    return concat(
        text("record"),
        SPACE,
        text("{"),
        nest(1, concat(HARDLINE, body)),
        HARDLINE,
        text("}"),
    )


def _format_type_enum(node: Node, script: Script) -> Doc:
    # enum { <enum_body> }
    kids = node.nonerr_children
    has_body = any(_name(c) == "enum_body" for c in kids)

    if not has_body:
        return concat(text("enum"), SPACE, text("{ }"))

    enum_body = next(c for c in kids if _name(c) == "enum_body")
    do_linebreak = _has_comments(node) or _compact_length(enum_body) > 80

    if do_linebreak:
        body_doc = _format_enum_body_multiline(enum_body, script)
        return concat(
            text("enum"),
            SPACE,
            text("{"),
            nest(1, concat(HARDLINE, body_doc)),
            HARDLINE,
            text("}"),
        )
    else:
        body_doc = _format_enum_body_inline(enum_body, script)
        return group(concat(
            text("enum"),
            SPACE,
            text("{"),
            SPACE,
            body_doc,
            SPACE,
            text("}"),
        ))


def _format_type_set_or_table(node: Node, script: Script, keyword: str) -> Doc:
    # set[type, ...] or table[type, ...] of <type>
    kids = node.nonerr_children
    parts = [text(keyword)]

    idx = 1  # skip keyword
    # '[' types ']'
    if idx < len(kids) and _tok(kids[idx]) == "[":
        idx += 1  # skip '['
        # Collect types between [ and ]
        type_docs: list[Doc] = []
        while idx < len(kids) and _tok(kids[idx]) != "]":
            if _name(kids[idx]) == "type":
                type_docs.append(format_child(kids[idx], script))
            # skip commas
            idx += 1
        idx += 1  # skip ']'

        sep = concat(text(","), LINE)
        type_list = join(sep, type_docs)
        bracket_content = group(concat(text("["), align(concat(type_list, text("]")))))
        parts.append(bracket_content)

    # 'of' <type> (for table)
    while idx < len(kids):
        parts.append(SPACE)
        parts.append(format_child(kids[idx], script))
        idx += 1

    return concat(*parts)


def _format_enum_body_multiline(node: Node, script: Script) -> Doc:
    """Format enum body with one element per line."""
    kids = node.nonerr_children
    parts: list[Doc] = []
    for child in kids:
        if _name(child) == "enum_body_elem":
            if parts:
                parts.append(HARDLINE)
            parts.append(format_child(child, script))
        elif _tok(child) == ",":
            parts.append(text(","))
    return concat(*parts)


def _format_enum_body_inline(node: Node, script: Script) -> Doc:
    """Format enum body on a single line."""
    kids = node.nonerr_children
    parts: list[Doc] = []
    for child in kids:
        if _name(child) == "enum_body_elem":
            if parts:
                parts.append(SPACE)
            parts.append(format_child(child, script))
        elif _tok(child) == ",":
            parts.append(text(","))
    return concat(*parts)


def _format_enum_body(node: Node, script: Script) -> Doc:
    """Dispatch for enum_body - used when it appears standalone."""
    # Context determines whether to use multiline or inline
    # By default, use inline and let the group decide
    return _format_enum_body_inline(node, script)


# ---------------------------------------------------------------------------
# Function declarations
# ---------------------------------------------------------------------------

def _format_func_decl(node: Node, script: Script) -> Doc:
    # <func_hdr> [preproc_directives...] <func_body>
    kids = node.nonerr_children
    parts: list[Doc] = []
    for child in kids:
        n = _name(child)
        if n == "func_hdr":
            parts.append(format_child(child, script))
        elif n == "preproc_directive":
            parts.append(HARDLINE)
            parts.append(format_child(child, script))
        elif n == "func_body":
            # func_body includes its own leading newline via Whitesmith block
            parts.append(format_child(child, script))
    parts.append(HARDLINE)
    return concat(*parts)


def _format_func_hdr(node: Node, script: Script) -> Doc:
    # Just wraps func/hook/event variant
    kids = node.nonerr_children
    if len(kids) == 1:
        return format_child(kids[0], script)
    return _format_space_separated(node, script)


def _format_func_hdr_variant(node: Node, script: Script) -> Doc:
    # [redef] function/hook/event <id> <func_params> [<attr_list>]
    kids = node.nonerr_children
    parts: list[Doc] = []
    idx = 0

    if _tok(kids[idx]) == "redef":
        parts.append(text("redef"))
        parts.append(SPACE)
        idx += 1

    parts.append(format_child(kids[idx], script))  # keyword
    parts.append(SPACE)
    idx += 1
    parts.append(format_child(kids[idx], script))  # <id>
    idx += 1
    parts.append(format_child(kids[idx], script))  # <func_params>
    idx += 1

    if idx < len(kids) and _name(kids[idx]) == "attr_list":
        parts.append(SPACE)
        parts.append(format_child(kids[idx], script))
        idx += 1

    return concat(*parts)


def _format_func_params(node: Node, script: Script) -> Doc:
    # ( [formal_args] ) [: <type>]
    kids = node.nonerr_children
    parts: list[Doc] = []
    idx = 0

    parts.append(text("("))  # '('
    idx += 1

    # Build the trailing content after args: ) [: <type>]
    trailing_parts: list[Doc] = [text(")")]
    args_idx = idx
    has_args = idx < len(kids) and _name(kids[idx]) == "formal_args"
    if has_args:
        idx += 1  # formal_args
        idx += 1  # ')'

    if idx < len(kids) and _tok(kids[idx]) == ":":
        trailing_parts.append(text(":"))
        idx += 1
        trailing_parts.append(SPACE)
        trailing_parts.append(format_child(kids[idx], script))  # <type>
        idx += 1

    trailing = concat(*trailing_parts)

    if has_args:
        # Pass trailing content (e.g. "): return_type") to formal_args so
        # fill accounts for it when deciding whether the last arg fits.
        args_node = kids[args_idx]
        pre = _format_prev_cst(args_node, script)
        args_node.prev_cst_siblings = []
        args_doc = _format_formal_args(args_node, script, trailing=trailing)
        parts.append(align(concat(pre, args_doc)))
    else:
        parts.append(trailing)

    return group(concat(*parts))


def _format_formal_args(node: Node, script: Script,
                        trailing: Doc = EMPTY) -> Doc:
    """Format formal args with fill-style wrapping.

    trailing is appended to the last arg so fill accounts for content
    that follows the args (like ): return_type) when deciding breaks.
    """
    kids = node.nonerr_children
    items: list[Doc] = []
    for child in kids:
        if _name(child) == "formal_arg":
            items.append(format_child(child, script))
    if items and trailing != EMPTY:
        items[-1] = concat(items[-1], trailing)
    sep = concat(text(","), LINE)
    return fill(*intersperse(sep, items))


def _format_formal_arg(node: Node, script: Script) -> Doc:
    # <id> : <type> [<attr_list>]
    kids = node.nonerr_children
    parts: list[Doc] = []
    idx = 0
    parts.append(format_child(kids[idx], script))  # <id>
    idx += 1
    parts.append(text(":"))  # ':'
    idx += 1
    parts.append(SPACE)
    parts.append(format_child(kids[idx], script))  # <type>
    idx += 1
    if idx < len(kids) and _name(kids[idx]) == "attr_list":
        parts.append(SPACE)
        parts.append(format_child(kids[idx], script))
    return concat(*parts)


def _format_func_body(node: Node, script: Script) -> Doc:
    # Whitesmith: newline, then { stmts }
    kids = node.nonerr_children
    # Find the '{' stmt_list '}' sequence
    stmts_doc = EMPTY
    for child in kids:
        if _name(child) == "stmt_list":
            pre_stmts = _format_prev_cst(child, script)
            child.prev_cst_siblings = []
            stmts_doc = concat(pre_stmts, _format_stmt_list(child, script))

    # Handle comments before '}'
    close_brace = kids[-1] if kids else None
    pre_close = EMPTY
    if close_brace:
        pre_close = _format_prev_cst(close_brace, script)
        close_brace.prev_cst_siblings = []

    if stmts_doc == EMPTY and pre_close == EMPTY:
        # Empty body still uses Whitesmith layout: newline + indented { }
        return nest(1, concat(HARDLINE, text("{ }")))

    body = concat(stmts_doc, pre_close) if pre_close != EMPTY else stmts_doc
    return _format_whitesmith_block(body)


def _format_type_spec(node: Node, script: Script) -> Doc:
    # <id> : <type> [<attr_list>] ;
    kids = node.nonerr_children
    parts: list[Doc] = []
    idx = 0
    parts.append(format_child(kids[idx], script))  # <id>
    idx += 1
    parts.append(text(":"))  # ':'
    idx += 1
    parts.append(SPACE)
    parts.append(format_child(kids[idx], script))  # <type>
    idx += 1
    if idx < len(kids) and _name(kids[idx]) == "attr_list":
        parts.append(SPACE)
        parts.append(format_child(kids[idx], script))
        idx += 1
    parts.append(text(";"))
    return concat(*parts)


# ---------------------------------------------------------------------------
# Redef declarations
# ---------------------------------------------------------------------------

def _format_redef_enum_decl(node: Node, script: Script) -> Doc:
    # redef enum <id> += { <enum_body> } ;
    kids = node.nonerr_children
    do_linebreak = _has_comments(node)

    parts = [text("redef"), SPACE, text("enum"), SPACE]
    idx = 2
    parts.append(format_child(kids[idx], script))  # <id>
    idx += 1
    parts.append(SPACE)
    parts.append(text("+="))  # '+='
    idx += 1
    parts.append(SPACE)
    parts.append(text("{"))  # '{'
    idx += 1

    # enum_body
    if idx < len(kids) and _name(kids[idx]) == "enum_body":
        enum_body = kids[idx]
        if not do_linebreak:
            do_linebreak = _compact_length(enum_body) > 60  # generous threshold

        if do_linebreak:
            body_doc = _format_enum_body_multiline(enum_body, script)
            parts.append(nest(1, concat(HARDLINE, body_doc)))
            parts.append(HARDLINE)
        else:
            body_doc = _format_enum_body_inline(enum_body, script)
            parts.append(SPACE)
            parts.append(body_doc)
            parts.append(SPACE)
        idx += 1

    parts.append(text("}"))
    idx += 1
    parts.append(text(";"))
    parts.append(HARDLINE)
    return concat(*parts)


def _format_redef_record_decl(node: Node, script: Script) -> Doc:
    # redef record <id>/<expr> +=/-= { type_spec... } [attr_list] ;
    # or: redef record <expr> -= <attr_list> ;
    kids = node.nonerr_children
    parts = [text("redef"), SPACE, text("record"), SPACE]
    idx = 2

    is_redef_attr = _name(kids[idx]) == "expr"
    parts.append(format_child(kids[idx], script))  # <id>/<expr>
    idx += 1
    parts.append(SPACE)
    parts.append(format_child(kids[idx], script))  # +=/-=
    idx += 1
    parts.append(SPACE)

    if is_redef_attr:
        # redef record expr -= attr_list ;
        while idx < len(kids):
            parts.append(format_child(kids[idx], script))
            if idx + 1 < len(kids):
                parts.append(SPACE)
            idx += 1
    else:
        parts.append(text("{"))  # '{'
        idx += 1
        # type_specs (don't end with HARDLINE, need separators)
        type_spec_docs: list[Doc] = []
        while idx < len(kids) and _name(kids[idx]) == "type_spec":
            type_spec_docs.append(format_child(kids[idx], script))
            idx += 1
        if type_spec_docs:
            body = join(HARDLINE, type_spec_docs)
            parts.append(nest(1, concat(HARDLINE, body)))
            parts.append(HARDLINE)
        parts.append(text("}"))  # '}'
        idx += 1
        if idx < len(kids) and _name(kids[idx]) == "attr_list":
            parts.append(SPACE)
            parts.append(format_child(kids[idx], script))
            idx += 1
        parts.append(text(";"))

    parts.append(HARDLINE)
    return concat(*parts)


# ---------------------------------------------------------------------------
# Statements
# ---------------------------------------------------------------------------

def _format_stmt(node: Node, script: Script) -> Doc:
    """Dispatch statement formatting based on first child."""
    kids = node.nonerr_children
    if not kids:
        return EMPTY

    ct1 = _child_tok(node, 0)
    cn1 = _child_name(node, 0)

    if ct1 == "{":
        return _format_stmt_block(node, script)
    elif ct1 in ("print", "event"):
        return _format_stmt_print_or_event(node, script)
    elif ct1 == "if":
        return _format_stmt_if(node, script)
    elif ct1 == "switch":
        return _format_stmt_switch(node, script)
    elif ct1 == "for":
        return _format_stmt_for(node, script)
    elif ct1 == "while":
        return _format_stmt_while(node, script)
    elif ct1 in ("next", "break", "fallthrough"):
        return _format_stmt_simple(node, script)
    elif ct1 == "return":
        return _format_stmt_return(node, script)
    elif ct1 in ("add", "delete"):
        return _format_stmt_set_mgmt(node, script)
    elif ct1 in ("local", "const"):
        return _format_stmt_local(node, script)
    elif ct1 == "when":
        return _format_stmt_when(node, script)
    elif cn1 == "index_slice":
        return _format_stmt_index_assign(node, script)
    elif cn1 == "expr":
        return _format_stmt_expr(node, script)
    elif cn1 == "preproc_directive":
        # No trailing HARDLINE — _format_body_items handles transitions
        return format_child(kids[0], script)
    elif ct1 == "assert":
        return _format_stmt_assert(node, script)
    elif ct1 == ";":
        return concat(text(";"), HARDLINE)
    else:
        # Fallback
        return concat(_format_space_separated(node, script), HARDLINE)


def _format_stmt_block(node: Node, script: Script) -> Doc:
    """Format a standalone { stmt_list } block statement.

    Standalone blocks (not control-flow bodies) use braces at the current
    indent level. Control-flow bodies use _format_indented_body which calls
    _format_curly_stmt_list for the Whitesmith style.
    """
    kids = node.nonerr_children
    idx = 0
    assert _tok(kids[idx]) == "{"
    idx += 1

    stmts_doc = EMPTY
    if idx < len(kids) and _name(kids[idx]) == "stmt_list":
        pre_stmts = _format_prev_cst(kids[idx], script)
        kids[idx].prev_cst_siblings = []
        stmts_doc = concat(pre_stmts, _format_stmt_list(kids[idx], script))
        idx += 1

    assert _tok(kids[idx]) == "}"
    close_brace = kids[idx]
    pre_close = _format_prev_cst(close_brace, script)
    close_brace.prev_cst_siblings = []

    if stmts_doc == EMPTY and pre_close == EMPTY:
        return concat(text("{ }"), HARDLINE)

    body = concat(stmts_doc, pre_close) if pre_close != EMPTY else stmts_doc
    return concat(text("{"), HARDLINE, body, text("}"), HARDLINE)


def _format_stmt_print_or_event(node: Node, script: Script) -> Doc:
    # print/event <expr_list>/<event_hdr> ;
    kids = node.nonerr_children
    keyword = _source_str(kids[0], script)
    parts = [text(keyword), SPACE]

    # Align continuation to after 'print '/'event '
    inner_parts: list[Doc] = []
    for child in kids[1:]:
        if _tok(child) == ";":
            inner_parts.append(text(";"))
        else:
            inner_parts.append(format_child(child, script))
    parts.append(align(concat(*inner_parts)))
    parts.append(HARDLINE)
    return concat(*parts)


def _is_brace_block(child: Node) -> bool:
    """Check if a stmt child is a {}-block."""
    return _child_tok(child, 0) == "{"


def _format_indented_body(child: Node, script: Script) -> Doc:
    """Format a statement body with proper indentation.

    Brace-blocks already include nest(1) from _format_whitesmith_block,
    so we emit them directly. Non-block bodies get wrapped in nest(1),
    with the trailing HARDLINE moved outside the nest so that any
    continuation (else, timeout) starts at the correct outer indent.
    """
    if _is_brace_block(child):
        # Use Whitesmith style directly — it includes its own nest(1).
        # Don't go through format_child/stmt dispatcher (which uses the
        # standalone block formatter).
        doc, _ = _format_curly_stmt_list(child, script, 0)
        # Add trailing HARDLINE so the stmt consistently ends with a newline
        # (Whitesmith blocks end with '}', not HARDLINE).
        return concat(doc, HARDLINE)
    body_doc = format_child(child, script)
    # Non-block stmts end with HARDLINE. Move it outside the nest so
    # the newline uses the outer indent level.
    stripped = _strip_trailing_hardline(body_doc)
    return concat(nest(1, concat(HARDLINE, stripped)), HARDLINE)


def _strip_trailing_hardline(doc: Doc) -> Doc:
    """Remove a trailing HARDLINE from a doc if present."""
    if isinstance(doc, HardLine):
        return EMPTY
    if isinstance(doc, Concat):
        docs = doc.docs
        if docs and isinstance(docs[-1], HardLine):
            return concat(*docs[:-1])
    return doc


def _is_compact_if(node: Node, script: Script) -> bool:
    """Check if this is an if-statement with body on same line in source."""
    kids = node.nonerr_children
    if not kids or _tok(kids[0]) != "if":
        return False
    if len(kids) > 5:  # has else clause
        return False
    if len(kids) < 5:
        return False
    body = kids[4]
    if _child_tok(body, 0) == "{":
        return False
    close_paren = kids[3]
    between = script.get_content(close_paren.end_byte, body.start_byte)
    return b"\n" not in between


def _has_adjacent_compact_if(node: Node, script: Script) -> bool:
    if node.prev_sibling and _name(node.prev_sibling) == "stmt" and _is_compact_if(node.prev_sibling, script):
        return True
    if node.next_sibling and _name(node.next_sibling) == "stmt" and _is_compact_if(node.next_sibling, script):
        return True
    return False


def _format_stmt_if(node: Node, script: Script) -> Doc:
    kids = node.nonerr_children
    parts: list[Doc] = []
    idx = 0

    parts.append(text("if"))  # 'if'
    idx += 1
    parts.append(SPACE)
    parts.append(text("("))  # '('
    idx += 1
    parts.append(SPACE)
    # Align condition to after '( '
    cond_doc = format_child(kids[idx], script)  # <expr>
    idx += 1
    parts.append(align(cond_doc))
    parts.append(SPACE)
    parts.append(text(")"))  # ')'
    idx += 1

    # Body
    body_child = kids[idx]
    idx += 1

    if _is_compact_if(node, script) and _has_adjacent_compact_if(node, script):
        # Compact if: body on same line
        parts.append(SPACE)
        parts.append(format_child(body_child, script))
    else:
        parts.append(_format_indented_body(body_child, script))

    # else clause
    if idx < len(kids) and _tok(kids[idx]) == "else":
        # Body always ends with HARDLINE (both brace-block and non-block)
        parts.append(text("else"))
        idx += 1
        if idx < len(kids):
            else_body = kids[idx]
            # else if: keep on same line
            if _child_tok(else_body, 0) == "if":
                parts.append(SPACE)
                parts.append(format_child(else_body, script))
            else:
                parts.append(_format_indented_body(else_body, script))

    return concat(*parts)


def _format_stmt_switch(node: Node, script: Script) -> Doc:
    kids = node.nonerr_children
    parts = [text("switch"), SPACE]
    idx = 1

    # expr is ( <inner_expr> ) — format with spaces inside parens
    expr_node = kids[idx]
    expr_kids = expr_node.nonerr_children
    parts.append(text("("))
    parts.append(SPACE)
    parts.append(format_child(expr_kids[1], script))  # inner expr
    parts.append(SPACE)
    parts.append(text(")"))
    idx += 1
    parts.append(SPACE)

    # Check for empty switch { }
    if _tok(kids[idx]) == "{":
        idx += 1
        if idx < len(kids) and _tok(kids[idx]) == "}":
            # Check if only whitespace between braces
            parts.append(text("{ }"))
            idx += 1
        else:
            parts.append(text("{"))
            # case_list
            if idx < len(kids) and _name(kids[idx]) == "case_list":
                parts.append(HARDLINE)
                parts.append(format_child(kids[idx], script))
                # case_list ends with HARDLINE from last stmt_list
                idx += 1
            else:
                parts.append(HARDLINE)
            parts.append(text("}"))
            idx += 1

    parts.append(HARDLINE)
    return concat(*parts)


def _format_stmt_for(node: Node, script: Script) -> Doc:
    kids = node.nonerr_children
    parts = [text("for"), SPACE, text("("), SPACE]
    idx = 2  # skip 'for' and '('

    # Loop variable(s): [id, id] or id
    loop_parts: list[Doc] = []
    while idx < len(kids) and _tok(kids[idx]) != ")" and _tok(kids[idx]) != "in":
        ct = _tok(kids[idx])
        if ct == "[":
            loop_parts.append(text("["))
        elif ct == "]":
            loop_parts.append(text("]"))
        elif ct == ",":
            loop_parts.append(text(","))
            loop_parts.append(SPACE)
        elif ct == "in":
            break
        else:
            loop_parts.append(format_child(kids[idx], script))
        idx += 1

    parts.append(align(concat(*loop_parts)))

    # 'in' <expr>
    if idx < len(kids) and _tok(kids[idx]) == "in":
        parts.append(SPACE)
        parts.append(text("in"))
        idx += 1
        parts.append(SPACE)
        parts.append(format_child(kids[idx], script))  # <expr>
        idx += 1

    parts.append(SPACE)
    parts.append(text(")"))  # ')'
    idx += 1

    # Body
    if idx < len(kids):
        parts.append(_format_indented_body(kids[idx], script))

    return concat(*parts)


def _format_stmt_while(node: Node, script: Script) -> Doc:
    kids = node.nonerr_children
    parts = [text("while"), SPACE, text("("), SPACE]
    idx = 2  # skip 'while' and '('

    # Condition
    cond_doc = format_child(kids[idx], script)
    parts.append(align(cond_doc))
    idx += 1
    parts.append(SPACE)
    parts.append(text(")"))  # ')'
    idx += 1

    # Body
    if idx < len(kids):
        parts.append(_format_indented_body(kids[idx], script))

    return concat(*parts)


def _format_stmt_simple(node: Node, script: Script) -> Doc:
    # next;  break;  fallthrough;
    kids = node.nonerr_children
    keyword = _source_str(kids[0], script)
    return concat(text(keyword), text(";"), HARDLINE)


def _format_stmt_return(node: Node, script: Script) -> Doc:
    kids = node.nonerr_children
    idx = 0
    parts = [text("return")]
    idx += 1

    # Optional 'when' form
    if idx < len(kids) and _tok(kids[idx]) == "when":
        parts.append(SPACE)
        when_doc = _format_when_clause(kids, idx, script)
        parts.append(when_doc)
        return concat(*parts)

    if idx < len(kids) and _name(kids[idx]) == "expr":
        parts.append(SPACE)
        parts.append(format_child(kids[idx], script))
        idx += 1

    parts.append(text(";"))
    parts.append(HARDLINE)
    return concat(*parts)


def _format_stmt_set_mgmt(node: Node, script: Script) -> Doc:
    # add/delete <expr> ;
    kids = node.nonerr_children
    keyword = _source_str(kids[0], script)
    return concat(
        text(keyword),
        SPACE,
        format_child(kids[1], script),
        text(";"),
        HARDLINE,
    )


def _format_stmt_local(node: Node, script: Script) -> Doc:
    # local/const <id> [: <type>] [= <expr>] [<attr_list>] ;
    kids = node.nonerr_children
    keyword = _source_str(kids[0], script)
    id_doc = format_child(kids[1], script)

    typed_init, idx = _format_typed_initializer(kids, 2, script)
    semi = text(";") if idx < len(kids) and _tok(kids[idx]) == ";" else EMPTY

    inner = concat(id_doc, typed_init) if typed_init != EMPTY else id_doc
    return concat(
        text(keyword),
        SPACE,
        group(align(concat(inner, semi))),
        HARDLINE,
    )


def _format_stmt_when(node: Node, script: Script) -> Doc:
    kids = node.nonerr_children
    return _format_when_clause(kids, 0, script)


def _format_when_clause(kids: list[Node], start_idx: int, script: Script) -> Doc:
    idx = start_idx
    parts = [text("when"), SPACE]
    idx += 1

    # Optional capture list
    if idx < len(kids) and _name(kids[idx]) == "capture_list":
        parts.append(format_child(kids[idx], script))
        parts.append(SPACE)
        idx += 1

    parts.append(text("("))  # '('
    idx += 1
    parts.append(SPACE)
    parts.append(format_child(kids[idx], script))  # <expr>
    idx += 1
    parts.append(SPACE)
    parts.append(text(")"))  # ')'
    idx += 1

    # Body
    if idx < len(kids):
        parts.append(_format_indented_body(kids[idx], script))
        idx += 1

    # timeout clause
    if idx < len(kids) and _tok(kids[idx]) == "timeout":
        # Body already ends with HARDLINE from _format_indented_body
        parts.append(text("timeout"))
        idx += 1
        parts.append(SPACE)
        parts.append(format_child(kids[idx], script))  # <expr>
        idx += 1
        # { stmt_list }
        if idx < len(kids):
            block_doc, idx = _format_curly_stmt_list_from_kids(kids, idx, script)
            parts.append(block_doc)

    parts.append(HARDLINE)
    return concat(*parts)


def _format_curly_stmt_list_from_kids(kids: list[Node], start_idx: int, script: Script) -> tuple[Doc, int]:
    """Format '{' stmt_list '}' from a flat list of child nodes."""
    idx = start_idx
    assert _tok(kids[idx]) == "{"
    idx += 1

    stmts_doc = EMPTY
    if idx < len(kids) and _name(kids[idx]) == "stmt_list":
        pre_stmts = _format_prev_cst(kids[idx], script)
        kids[idx].prev_cst_siblings = []
        stmts_doc = concat(pre_stmts, _format_stmt_list(kids[idx], script))
        idx += 1

    assert idx < len(kids) and _tok(kids[idx]) == "}"
    close_brace = kids[idx]
    pre_close = _format_prev_cst(close_brace, script)
    close_brace.prev_cst_siblings = []
    idx += 1

    if stmts_doc == EMPTY and pre_close == EMPTY:
        return text("{ }"), idx

    body = concat(stmts_doc, pre_close) if pre_close != EMPTY else stmts_doc
    return _format_whitesmith_block(body), idx


def _format_stmt_index_assign(node: Node, script: Script) -> Doc:
    # <index_slice> = <expr> ;
    kids = node.nonerr_children
    parts: list[Doc] = []
    for child in kids:
        if parts:
            parts.append(SPACE)
        parts.append(format_child(child, script))
    parts.append(HARDLINE)
    return concat(*parts)


def _format_stmt_expr(node: Node, script: Script) -> Doc:
    # <expr> ;
    kids = node.nonerr_children
    return concat(
        format_child(kids[0], script),
        _format_semi(kids, script),
        HARDLINE,
    )


def _format_stmt_assert(node: Node, script: Script) -> Doc:
    # assert <expr> [<assert_msg>] ;
    kids = node.nonerr_children
    parts = [text("assert"), SPACE]
    for child in kids[1:]:
        if _tok(child) == ";":
            parts.append(text(";"))
        elif _name(child) == "assert_msg":
            parts.append(format_child(child, script))
        else:
            parts.append(format_child(child, script))
    parts.append(HARDLINE)
    return concat(*parts)


# ---------------------------------------------------------------------------
# Expressions
# ---------------------------------------------------------------------------

def _format_expr(node: Node, script: Script) -> Doc:
    """Dispatch expression formatting."""
    kids = node.nonerr_children
    if not kids:
        return text(_source_str(node, script))

    cn1 = _child_name(node, 0)
    ct1 = _child_tok(node, 0)
    ct2 = _child_tok(node, 1)
    ct3 = _child_tok(node, 2)

    # Index: expr[...]
    if cn1 == "expr" and ct2 == "[":
        return _format_expr_index(node, script)
    # Record field: expr$field
    if cn1 == "expr" and ct2 == "$":
        return _format_expr_field_access(node, script)
    # Index slice: expr[a:b]
    if cn1 == "expr" and _child_name(node, 1) == "index_slice":
        return _format_expr_index_slice(node, script)
    # Negation: ! expr
    if ct1 == "!":
        return _format_expr_negation(node, script)
    # Unary: ++, --, ~, -, +, |
    if ct1 in ("|", "++", "--", "~", "-", "+"):
        return _format_expr_unary(node, script)
    # !in operator
    if cn1 == "expr" and ct2 == "!" and ct3 == "in":
        return _format_expr_not_in(node, script)
    # Initializers: { ... } or [ ... ]
    if ct1 in ("{", "["):
        return _format_expr_initializer(node, script)
    # Parenthesized: ( expr )
    if ct1 == "(":
        return _format_expr_paren(node, script)
    # Field assign: $name = expr
    if ct1 == "$" and ct3 == "=":
        return _format_expr_field_assign(node, script)
    # Field lambda: $name <begin_lambda> = <func_body>
    if ct1 == "$" and _child_name(node, 2) == "begin_lambda":
        return _format_expr_field_lambda(node, script)
    # copy(expr)
    if ct1 == "copy":
        return _format_expr_copy(node, script)
    # Has-field: expr?$field
    if ct2 == "?$":
        return _format_expr_has_field(node, script)
    # Anonymous function: function <begin_lambda> <func_body>
    if ct1 == "function":
        return _format_expr_anon_func(node, script)
    # Function/constructor calls: name(...)
    if ct2 == "(":
        return _format_expr_call(node, script)
    # Binary boolean: && ||
    if _nch(node) == 3 and ct2 in ("||", "&&"):
        return _format_expr_boolean(node, script)
    # Ternary: cond ? true : false
    if _nch(node) == 5 and ct2 == "?" and _child_tok(node, 3) == ":":
        return _format_expr_ternary(node, script)
    # Assignment: lhs = rhs
    if _nch(node) == 3 and ct2 in ("=", "+=", "-="):
        return _format_expr_assignment(node, script)
    # Binary operators
    if _nch(node) == 3 and ct2 in (
        "/", "*", "+", "-", "%",
        "==", "!=", "<", ">", "<=", ">=",
        "&", "|", "^", "~", "!~", "in",
    ):
        return _format_expr_binary(node, script)
    # Schedule
    if ct1 == "schedule":
        return _format_expr_schedule(node, script)

    # Default: space-separated
    return _format_space_separated(node, script)


def _format_expr_index(node: Node, script: Script) -> Doc:
    # <expr> '[' <expr_list> ']'
    kids = node.nonerr_children
    return concat(
        format_child(kids[0], script),  # <expr>
        text("["),
        align(concat(
            format_child(kids[2], script),  # <expr_list>
            text("]"),
        )),
    )


def _format_expr_field_access(node: Node, script: Script) -> Doc:
    # expr$field - never breaks
    kids = node.nonerr_children
    parts: list[Doc] = []
    for child in kids:
        parts.append(format_child(child, script))
    return concat(*parts)


def _format_expr_index_slice(node: Node, script: Script) -> Doc:
    kids = node.nonerr_children
    parts: list[Doc] = []
    for child in kids:
        parts.append(format_child(child, script))
    return concat(*parts)


def _format_expr_negation(node: Node, script: Script) -> Doc:
    # ! <expr>
    kids = node.nonerr_children
    return concat(text("!"), SPACE, format_child(kids[1], script))


def _format_expr_unary(node: Node, script: Script) -> Doc:
    # ++/--/~/- <expr> (no space)
    kids = node.nonerr_children
    parts: list[Doc] = []
    for child in kids:
        parts.append(format_child(child, script))
    return concat(*parts)


def _format_expr_not_in(node: Node, script: Script) -> Doc:
    # <expr> !in <expr>
    kids = node.nonerr_children
    return group(align(concat(
        format_child(kids[0], script),
        SPACE,
        text("!"),
        text("in"),
        SPACE,
        format_child(kids[3], script),
    )))


def _format_expr_initializer(node: Node, script: Script) -> Doc:
    # { expr_list } or [ expr_list ]
    kids = node.nonerr_children
    ct1 = _tok(kids[0])
    close = "}" if ct1 == "{" else "]"

    is_record = ct1 == "[" and _is_record_constructor(node)

    # Check if there's an expr_list
    has_content = len(kids) > 2 and _name(kids[1]) == "expr_list"

    if not has_content:
        # Empty: { } or [ ]
        return concat(text(ct1), SPACE, text(close))

    expr_list = kids[1]
    do_linebreak = _has_comments(node) or _compact_length(expr_list) > 80

    if is_record:
        return _format_record_constructor(node, script, do_linebreak)

    if do_linebreak and ct1 == "{":
        # Multi-line brace init
        body = _format_expr_list_multiline(expr_list, script)
        return concat(
            text(ct1),
            nest(1, concat(HARDLINE, body)),
            HARDLINE,
            text(close),
        )
    else:
        # Inline or wrapping
        body = _format_expr_list_inline(expr_list, script)
        return group(concat(
            text(ct1),
            align(concat(body, text(close))),
        ))


def _is_record_constructor(node: Node) -> bool:
    """True if this [..] is a record constructor ($field=val style)."""
    kids = node.nonerr_children
    if len(kids) < 2:
        return False
    expr_list = kids[1]
    if _name(expr_list) != "expr_list":
        return False
    for child in expr_list.nonerr_children:
        if _name(child) == "expr" and child.nonerr_children:
            ft = _tok(child.nonerr_children[0])
            return ft is not None and ft.startswith("$")
    return False


def _format_record_constructor(node: Node, script: Script, do_linebreak: bool) -> Doc:
    # [$field1=val1, $field2=val2, ...]
    kids = node.nonerr_children
    expr_list = kids[1]
    fields = [c for c in expr_list.nonerr_children if _name(c) == "expr"]

    field_docs: list[Doc] = []
    for f in fields:
        field_docs.append(format_child(f, script))

    sep = concat(text(","), LINE)

    if do_linebreak:
        # One field per line, aligned to after '['
        items = join(sep, field_docs)
        return concat(
            text("["),
            align(concat(items, text("]"))),
        )
    else:
        # All on one line if fits
        items = join(concat(text(","), SPACE), field_docs)
        return group(concat(
            text("["),
            align(concat(items, text("]"))),
        ))


def _format_expr_paren(node: Node, script: Script) -> Doc:
    # ( <expr> )
    kids = node.nonerr_children
    return concat(
        text("("),
        format_child(kids[1], script),
        text(")"),
    )


def _format_expr_field_assign(node: Node, script: Script) -> Doc:
    # $ <id> = <expr>  (4 children) — no spaces: $id=expr
    kids = node.nonerr_children
    return concat(
        text("$"),
        format_child(kids[1], script),  # <id>
        text("="),
        format_child(kids[3], script),  # <expr>
    )


def _format_expr_field_lambda(node: Node, script: Script) -> Doc:
    # $ <id> <begin_lambda> = <func_body>
    kids = node.nonerr_children
    parts: list[Doc] = []
    for i, child in enumerate(kids):
        if i > 0 and i >= 2:  # space before begin_lambda, =, func_body
            parts.append(SPACE)
        parts.append(format_child(child, script))
    return concat(*parts)


def _format_expr_copy(node: Node, script: Script) -> Doc:
    # copy ( <expr> )
    kids = node.nonerr_children
    return concat(
        text("copy"),
        text("("),
        format_child(kids[2], script),
        text(")"),
    )


def _format_expr_has_field(node: Node, script: Script) -> Doc:
    # <expr> ?$ <id> - never breaks
    kids = node.nonerr_children
    parts: list[Doc] = []
    for child in kids:
        parts.append(format_child(child, script))
    return concat(*parts)


def _format_expr_anon_func(node: Node, script: Script) -> Doc:
    # function<begin_lambda> <func_body>
    # No space between 'function' and begin_lambda (starts with '(')
    kids = node.nonerr_children
    parts: list[Doc] = []
    for i, child in enumerate(kids):
        if i > 0 and _name(child) != "begin_lambda":
            parts.append(SPACE)
        parts.append(format_child(child, script))
    return concat(*parts)


def _format_expr_call(node: Node, script: Script) -> Doc:
    # <expr/name> ( [expr_list] ) [attr_list]
    kids = node.nonerr_children
    parts: list[Doc] = []

    # Function name
    parts.append(format_child(kids[0], script))
    # '('
    parts.append(text("("))
    idx = 2  # skip name and '('

    # Arguments
    if idx < len(kids) and _name(kids[idx]) == "expr_list":
        args_doc = _format_expr_list_inline(kids[idx], script)
        close = text(")")
        # Align args to column after '('
        parts.append(align(concat(args_doc, close)))
        idx += 1
        idx += 1  # skip ')'
    else:
        parts.append(text(")"))
        idx += 1

    # Attributes
    if idx < len(kids) and _name(kids[idx]) == "attr_list":
        parts.append(SPACE)
        parts.append(format_child(kids[idx], script))

    return group(concat(*parts))


def _format_expr_boolean(node: Node, script: Script) -> Doc:
    # <expr> && <expr> or <expr> || <expr>
    kids = node.nonerr_children
    op = _source_str(kids[1], script)
    return group(align(concat(
        format_child(kids[0], script),
        SPACE,
        text(op),
        LINE,
        format_child(kids[2], script),
    )))


def _format_expr_ternary(node: Node, script: Script) -> Doc:
    # <expr> ? <expr> : <expr>
    kids = node.nonerr_children
    return group(align(concat(
        format_child(kids[0], script),
        SPACE,
        text("?"),
        LINE,
        format_child(kids[2], script),
        SPACE,
        text(":"),
        LINE,
        format_child(kids[4], script),
    )))


def _format_expr_assignment(node: Node, script: Script) -> Doc:
    # <expr> = <expr>
    kids = node.nonerr_children
    op = _source_str(kids[1], script)
    return group(align(concat(
        format_child(kids[0], script),
        SPACE,
        text(op),
        LINE,
        format_child(kids[2], script),
    )))


def _format_expr_binary(node: Node, script: Script) -> Doc:
    # <expr> op <expr>
    kids = node.nonerr_children
    op = _source_str(kids[1], script)
    return group(align(concat(
        format_child(kids[0], script),
        SPACE,
        text(op),
        LINE,
        format_child(kids[2], script),
    )))


def _format_expr_schedule(node: Node, script: Script) -> Doc:
    # schedule <expr> { <event_hdr> }
    kids = node.nonerr_children
    return group(concat(
        text("schedule"),
        SPACE,
        format_child(kids[1], script),
        SPACE,
        text("{"),
        SPACE,
        format_child(kids[3], script),
        SPACE,
        text("}"),
    ))


# ---------------------------------------------------------------------------
# Expression list formatters
# ---------------------------------------------------------------------------

def _format_expr_list(node: Node, script: Script) -> Doc:
    """Default expr_list formatter: comma-separated, wrappable."""
    return _format_expr_list_inline(node, script)


def _format_expr_list_inline(node: Node, script: Script) -> Doc:
    """Format expr_list as comma-separated items that wrap with fill."""
    kids = node.nonerr_children
    items: list[Doc] = []
    for child in kids:
        if _name(child) == "expr":
            items.append(format_child(child, script))
        # commas handled as separators

    if not items:
        return EMPTY

    sep = concat(text(","), LINE)
    return fill(*intersperse(sep, items))


def _format_expr_list_multiline(node: Node, script: Script) -> Doc:
    """Format expr_list with one element per line."""
    kids = node.nonerr_children
    items: list[Doc] = []
    for child in kids:
        if _name(child) == "expr":
            items.append(format_child(child, script))

    if not items:
        return EMPTY

    parts: list[Doc] = []
    for i, item in enumerate(items):
        if i > 0:
            parts.append(HARDLINE)
        parts.append(item)
        if i < len(items) - 1:
            parts.append(text(","))
    return concat(*parts)


# ---------------------------------------------------------------------------
# Other node formatters
# ---------------------------------------------------------------------------

def _format_initializer(node: Node, script: Script) -> Doc:
    return _format_initializer_node(node, script, transform_braces=False)


def _format_initializer_node(node: Node, script: Script, transform_braces: bool = False) -> Doc:
    # <init_class> <expr>  where init_class contains '=' or '+=' etc.
    kids = node.nonerr_children
    init_class = kids[0]  # contains the operator token
    op = _source_str(init_class, script)
    expr = kids[1]
    if transform_braces:
        expr_doc = _format_expr_with_brace_transform(expr, script)
    else:
        expr_doc = format_child(expr, script)
    return group(concat(
        text(op),
        LINE,
        expr_doc,
    ))


def _is_brace_init_table(node: Node) -> bool:
    """Returns True if this {..} expression is a table initializer."""
    kids = node.nonerr_children
    if len(kids) < 2:
        return False
    expr_list = kids[1]
    if _name(expr_list) != "expr_list":
        return False
    for child in expr_list.nonerr_children:
        if _name(child) == "expr":
            for sc in child.nonerr_children:
                if _tok(sc) == "=":
                    return True
    return False


def _format_expr_with_brace_transform(node: Node, script: Script) -> Doc:
    """Format an expression, transforming { } to set()/table() if applicable."""
    kids = node.nonerr_children
    if not kids or _tok(kids[0]) != "{":
        return format_child(node, script)

    # Determine constructor name
    constructor = "table" if _is_brace_init_table(node) else "set"

    # Get the content between { and }
    has_content = len(kids) > 2 and _name(kids[1]) == "expr_list"

    if not has_content:
        return text(constructor + "()")

    expr_list = kids[1]
    do_linebreak = _has_comments(node) or _compact_length(expr_list) > 80

    if do_linebreak:
        body = _format_expr_list_multiline(expr_list, script)
        return concat(
            text(constructor + "("),
            nest(1, concat(HARDLINE, body)),
            HARDLINE,
            text(")"),
        )
    else:
        body = _format_expr_list_inline(expr_list, script)
        return group(concat(
            text(constructor + "("),
            align(concat(body, text(")"))),
        ))


def _format_index_slice(node: Node, script: Script) -> Doc:
    # [ expr : expr ] or [expr:] or [:expr] or [:]
    # Space around ':' when either side is compound (not literal/constant/empty)
    kids = node.nonerr_children

    # Determine if we need spaces around ':'
    use_space = False
    for child in kids:
        if _name(child) is not None and len(child.children) > 1:
            use_space = True
            break

    parts: list[Doc] = []
    prev_was_colon = False
    for i, child in enumerate(kids):
        ct = _tok(child)
        if ct == "[":
            parts.append(text("["))
        elif ct == "]":
            parts.append(text("]"))
        elif ct == ":":
            if use_space and i > 1:  # space before ':' if there's content before
                parts.append(SPACE)
            parts.append(text(":"))
            prev_was_colon = True
        else:
            if prev_was_colon and use_space:
                parts.append(SPACE)
            parts.append(format_child(child, script))
            prev_was_colon = False
    return concat(*parts)


def _format_capture_list(node: Node, script: Script) -> Doc:
    # [capture, ...]
    kids = node.nonerr_children
    items: list[Doc] = []
    for child in kids:
        if _name(child) == "capture":
            items.append(format_child(child, script))
    return concat(
        text("["),
        join(concat(text(","), SPACE), items),
        text("]"),
    )


def _format_case_list(node: Node, script: Script) -> Doc:
    kids = node.nonerr_children
    parts: list[Doc] = []
    idx = 0
    while idx < len(kids):
        ct = _tok(kids[idx])
        if ct == "case":
            parts.append(text("case"))
            parts.append(SPACE)
            idx += 1
            # case value(s) and ':'
            case_parts: list[Doc] = []
            while idx < len(kids) and _tok(kids[idx]) != ":":
                case_parts.append(format_child(kids[idx], script))
                idx += 1
            parts.append(align(concat(*case_parts)))
            parts.append(text(":"))  # ':'
            idx += 1
        elif ct == "default":
            parts.append(text("default"))
            idx += 1
            parts.append(text(":"))
            idx += 1
        elif _name(kids[idx]) == "stmt_list":
            stmts_doc = _format_stmt_list(kids[idx], script)
            # Strip trailing HARDLINE from stmts and place it outside the
            # nest so the next case label starts at the correct indent.
            stripped = _strip_trailing_hardline(stmts_doc)
            parts.append(nest(1, concat(HARDLINE, stripped)))
            parts.append(HARDLINE)
            idx += 1
        else:
            parts.append(format_child(kids[idx], script))
            idx += 1
    return concat(*parts)


def _format_case_type_list(node: Node, script: Script) -> Doc:
    kids = node.nonerr_children
    parts: list[Doc] = []
    idx = 0
    while idx < len(kids):
        if _tok(kids[idx]) == "type":
            if parts:
                parts.append(SPACE)
            parts.append(text("type"))
            idx += 1
            parts.append(SPACE)
            parts.append(format_child(kids[idx], script))  # <type>
            idx += 1
            if idx < len(kids) and _tok(kids[idx]) == "as":
                parts.append(SPACE)
                parts.append(text("as"))
                idx += 1
                parts.append(SPACE)
                parts.append(format_child(kids[idx], script))  # <id>
                idx += 1
            if idx < len(kids) and _tok(kids[idx]) == ",":
                parts.append(text(","))
                idx += 1
        else:
            parts.append(format_child(kids[idx], script))
            idx += 1
    return concat(*parts)


def _format_event_hdr(node: Node, script: Script) -> Doc:
    # <expr> ( [expr_list] )
    kids = node.nonerr_children
    parts = [format_child(kids[0], script)]  # event name
    parts.append(text("("))
    idx = 2  # skip name and '('
    if idx < len(kids) and _name(kids[idx]) == "expr_list":
        args_doc = _format_expr_list_inline(kids[idx], script)
        parts.append(align(concat(args_doc, text(")"))))
        idx += 1
        idx += 1  # skip ')'
    else:
        parts.append(text(")"))
    return group(concat(*parts))


def _format_attr_list(node: Node, script: Script) -> Doc:
    """Format attribute list, deciding on spacing around '='."""
    kids = node.nonerr_children

    # Check if any attr's expression has embedded blanks
    use_spaces = False
    for child in kids:
        if _name(child) == "attr" and _attr_has_embedded_blanks(child):
            use_spaces = True
            break

    attrs: list[Doc] = []
    for child in kids:
        if _name(child) == "attr":
            attrs.append(_format_attr(child, script, use_spaces))

    return join(SPACE, attrs)


def _attr_has_embedded_blanks(node: Node) -> bool:
    """Check if an attr node's expression has embedded blanks."""
    kids = node.nonerr_children
    if len(kids) < 3:
        return False
    expr_node = kids[2]
    for child, _ in expr_node.traverse():
        if _name(child) == "interval":
            return True
        if _name(child) == "begin_lambda":
            return True
        if _name(child) == "expr" and len(child.nonerr_children) == 3:
            first = child.nonerr_children[0]
            third = child.nonerr_children[2]
            if _name(first) == "expr" and _name(third) == "expr":
                return True
    return False


def _format_attr(node: Node, script: Script, use_spaces: bool) -> Doc:
    kids = node.nonerr_children
    if len(kids) >= 3 and _tok(kids[1]) == "=":
        # &attr = expr
        parts = [format_child(kids[0], script)]
        if use_spaces:
            parts.append(SPACE)
        parts.append(text("="))
        if use_spaces:
            parts.append(SPACE)
        parts.append(format_child(kids[2], script))
        return concat(*parts)
    else:
        return format_child(kids[0], script) if kids else EMPTY


def _format_interval(node: Node, script: Script) -> Doc:
    # <number> <unit>
    kids = node.nonerr_children
    return concat(
        format_child(kids[0], script),
        SPACE,
        format_child(kids[1], script),
    )


def _format_no_space(node: Node, script: Script) -> Doc:
    """Concatenate children with no spaces (for subnet, etc.)."""
    kids = node.nonerr_children
    if not kids:
        return text(_source_str(node, script))
    return concat(*[format_child(c, script) for c in kids])


def _format_preproc_directive(node: Node, script: Script) -> Doc:
    # @if/@ifdef/@ifndef/@else/@endif ... - always at column 0, no linebreaking
    kids = node.nonerr_children
    parts: list[Doc] = []
    for i, child in enumerate(kids):
        if i > 0:
            parts.append(SPACE)
        parts.append(format_child(child, script))
    return concat(*parts)


def _format_pragma(node: Node, script: Script) -> Doc:
    return text(_source_str(node, script))


def _format_begin_lambda(node: Node, script: Script) -> Doc:
    # begin_lambda contains a single func_params child
    kids = node.nonerr_children
    return _format_func_params(kids[0], script)


def _format_assert_msg(node: Node, script: Script) -> Doc:
    # , <expr>
    kids = node.nonerr_children
    parts: list[Doc] = []
    for child in kids:
        if _tok(child) == ",":
            parts.append(text(","))
            parts.append(SPACE)
        else:
            parts.append(format_child(child, script))
    return concat(*parts)


# ---------------------------------------------------------------------------
# Formatter dispatch table
# ---------------------------------------------------------------------------

_FORMATTERS: dict[str, callable] = {
    "source_file": _format_source_file,
    "decl": _format_decl,
    "module_decl": _format_module_decl,
    "export_decl": _format_export_decl,
    "global_decl": _format_global_decl,
    "const_decl": _format_global_decl,
    "option_decl": _format_global_decl,
    "redef_decl": _format_global_decl,
    "type_decl": _format_type_decl,
    "type": _format_type,
    "type_spec": _format_type_spec,
    "func_decl": _format_func_decl,
    "func_hdr": _format_func_hdr,
    "func": _format_func_hdr_variant,
    "hook": _format_func_hdr_variant,
    "event": _format_func_hdr_variant,
    "func_params": _format_func_params,
    "formal_args": _format_formal_args,
    "formal_arg": _format_formal_arg,
    "func_body": _format_func_body,
    "enum_body": _format_enum_body,
    "redef_enum_decl": _format_redef_enum_decl,
    "redef_record_decl": _format_redef_record_decl,
    "stmt": _format_stmt,
    "stmt_list": _format_stmt_list,
    "expr": _format_expr,
    "expr_list": _format_expr_list,
    "index_slice": _format_index_slice,
    "capture_list": _format_capture_list,
    "case_list": _format_case_list,
    "case_type_list": _format_case_type_list,
    "event_hdr": _format_event_hdr,
    "attr_list": _format_attr_list,
    "initializer": _format_initializer,
    "interval": _format_interval,
    "subnet": _format_no_space,
    "preproc_directive": _format_preproc_directive,
    "pragma": _format_pragma,
    "begin_lambda": _format_begin_lambda,
    "assert_msg": _format_assert_msg,
    # Null/error nodes handled directly in format_node
}


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def format_script(script: Script, max_width: int = 80) -> bytes:
    """Format a parsed script using the IR-based formatter.

    Returns formatted bytes.
    """
    assert script.root is not None, "call Script.parse() before format_script()"
    script._scan_format_annotations()

    doc = format_child(script.root, script)
    return resolve(doc, max_width=max_width)
