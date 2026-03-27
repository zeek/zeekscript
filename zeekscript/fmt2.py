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
    Align, AlignCapped, Concat, Dedent, DedentSpaces, Doc, Fill, Group,
    HardLine, IfBreak, Line, Nest, SoftLine, Text,
    MAX_ALIGN_COL, MAX_WIDTH, TAB_SIZE, _flat_width,
    align, align_capped, concat, dedent, dedent_spaces, fill, group, if_break,
    intersperse, join, nest, resolve, text,
)
from .node import Node

if TYPE_CHECKING:
    from .script import Script


# ---------------------------------------------------------------------------
# Helpers for inspecting Doc trees
# ---------------------------------------------------------------------------

def _can_break(doc: Doc) -> bool:
    """True if doc contains any break points (Line, SoftLine, Fill, IfBreak).

    A doc that cannot break is "atomic" — it renders identically in flat
    and break modes.  Used to decide whether an enclosing construct needs
    its own break point (e.g. assignment breaking at '=').
    """
    stack: list[Doc] = [doc]
    while stack:
        d = stack.pop()
        if isinstance(d, (Line, SoftLine, HardLine, IfBreak)):
            return True
        if isinstance(d, Fill) and len(d.docs) > 1:
            return True
        if isinstance(d, Fill):
            stack.extend(d.docs)
            continue
        elif isinstance(d, Concat):
            stack.extend(d.docs)
        elif isinstance(d, (Group, Nest, Align, Dedent, DedentSpaces, AlignCapped)):
            stack.append(d.doc)
    return False


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


def _inject_trailing_into_fill(doc: Doc, suffix: Doc) -> tuple[Doc, bool]:
    """Inject suffix into the last item of the innermost Fill in doc.

    Walks through Group, Concat (rightmost), and Align to find a Fill,
    then appends suffix to its last item.  When the Fill is inside a
    Concat with trailing siblings (like a closing ')'), those siblings
    are absorbed into the fill's last item too (before the suffix).
    This ensures the fill's last group accounts for trailing content
    in its flat-width check.  Returns (new_doc, True) on success.
    """
    from .ir import Align, Fill
    if isinstance(doc, Fill):
        docs = list(doc.docs)
        if docs:
            docs[-1] = concat(docs[-1], suffix)
            return Fill(tuple(docs)), True
        return doc, False
    if isinstance(doc, Group):
        new_child, ok = _inject_trailing_into_fill(doc.doc, suffix)
        if ok:
            return Group(new_child), True
    if isinstance(doc, Align):
        new_child, ok = _inject_trailing_into_fill(doc.doc, suffix)
        if ok:
            return Align(new_child), True
    if isinstance(doc, Concat):
        docs = list(doc.docs)
        for i in range(len(docs) - 1, -1, -1):
            # Collect trailing siblings — these will be absorbed into
            # the fill's last item along with the suffix.
            trailing = docs[i + 1:]
            combined = concat(*trailing, suffix) if trailing else suffix
            new_child, ok = _inject_trailing_into_fill(docs[i], combined)
            if ok:
                before = docs[:i]
                if before:
                    return Concat(tuple(before + [new_child])), True
                return new_child, True
    return doc, False


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
    zeekygen_prev_parts: list[Doc] = []
    nl_count = 0
    # Preproc decls don't end with HARDLINE, so own-line comments in
    # their next_cst need an explicit line break.
    needs_nl = _is_preproc_item(node)

    for sib in node.next_cst_siblings:
        if sib.no_format is not None:
            continue
        if sib.is_zeekygen_prev_comment():
            # Collect consecutive ##< comments for aligned output
            if zeekygen_prev_parts:
                zeekygen_prev_parts.append(HARDLINE)
            zeekygen_prev_parts.append(text(_source_str(sib, script)))
            nl_count = 0
            continue
        if _is_nl(sib) and zeekygen_prev_parts:
            # Skip NLs between consecutive ##< comments
            continue
        if _is_nl(sib):
            nl_count += 1
            continue
        # Flush any collected ##< comments before processing other nodes
        if zeekygen_prev_parts:
            parts.append(SPACE)
            parts.append(align(concat(*zeekygen_prev_parts)))
            zeekygen_prev_parts = []
        if sib.is_minor_comment():
            # End-of-line comment (comment follows code on same source line)
            if sib.prev_cst_sibling and not sib.prev_cst_sibling.is_nl():
                parts.append(SPACE)
                parts.append(text(_source_str(sib, script)))
            else:
                # Own-line comment: preserve blank line before it
                if nl_count >= 2:
                    parts.append(HARDLINE)
                if needs_nl and nl_count >= 1:
                    parts.append(HARDLINE)
                parts.append(text(_source_str(sib, script)))
        elif _is_comment(sib):
            if nl_count >= 2:
                parts.append(HARDLINE)
            if needs_nl and nl_count >= 1:
                parts.append(HARDLINE)
            parts.append(text(_source_str(sib, script)))
        nl_count = 0

    # Flush trailing ##< comments
    if zeekygen_prev_parts:
        parts.append(SPACE)
        parts.append(align(concat(*zeekygen_prev_parts)))

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


def _blank_line_in_source(prev_node: Node, curr_node: Node,
                          source: bytes) -> bool:
    """True if the source between two nodes contains a blank line.

    This catches blank lines that _wants_blank_before misses when the
    previous node has a trailing comment whose next_cst_siblings consume
    a NL that would otherwise appear in curr_node's prev_cst_siblings.
    """
    gap = source[prev_node.end_byte:curr_node.start_byte]
    return b"\n\n" in gap or b"\r\n\r\n" in gap


def _prev_cst_has_blank_line(node: Node) -> bool:
    """True if _format_prev_cst would emit any blank line for this node.

    Checks both blank lines before the first comment (2+ NLs leading up to it)
    and blank lines after the last comment (2+ trailing NLs when comments exist).
    Used to avoid double blank lines when _blank_line_in_source also detects one.
    """
    nl_count = 0
    has_comments = False
    for sib in node.prev_cst_siblings:
        if _is_nl(sib):
            nl_count += 1
        elif _is_comment(sib):
            if nl_count >= 2:
                return True
            has_comments = True
            nl_count = 0
        else:
            nl_count = 0
    # Trailing NLs after last comment (line 160 of _format_prev_cst)
    return has_comments and nl_count >= 2


def _has_lambda(node: Node) -> bool:
    """True if node contains a begin_lambda child (anonymous function)."""
    for child in (node.nonerr_children or []):
        if _name(child) == "begin_lambda":
            return True
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
        raw = node.no_format.decode("UTF-8", errors="replace")
        # Statement/decl nodes end with HARDLINE in normal formatting;
        # preserve this so _format_body_items gets proper newlines.
        ntype = node.type
        if ntype in ("stmt", "decl"):
            raw = raw.rstrip("\n\r")
            return concat(text(raw), HARDLINE)
        return text(raw)
    if node.no_format is True:
        return EMPTY

    parts: list[Doc] = []

    # Leading error siblings (error nodes before this node)
    for err in node.prev_error_siblings:
        parts.append(_format_error(err, script))
        parts.append(SPACE)

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

    # Trailing error siblings (error nodes after this node)
    for err in node.next_error_siblings:
        parts.append(SPACE)
        parts.append(_format_error(err, script))

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
        # Fallback: check source for blank lines missed by _wants_blank_before
        # (e.g. when previous stmt has trailing comment consuming a NL)
        if not blank_before and i > 0:
            prev = items[i - 1][0]
            if (_blank_line_in_source(prev, node, script.source)
                    and not _prev_cst_has_blank_line(node)):
                blank_before = True
        if blank_before:
            parts.append(HARDLINE)
        if is_preproc and not blank_before:
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
    # Handle no-format annotation on the stmt_list itself
    # (happens when a single-statement body has trailing #@ NO-FORMAT)
    if isinstance(getattr(node, 'no_format', None), bytes):
        raw = node.no_format.decode("UTF-8", errors="replace").rstrip("\n\r")
        return concat(text(raw), HARDLINE)

    stmts = [c for c in node.nonerr_children if _name(c) == "stmt"]
    if not stmts:
        return EMPTY
    return _format_body_items(stmts, script)


def _format_same_line_brace_block(body: Doc) -> Doc:
    """Format a same-line brace block: { on current line, } at base indent.

    Used for record, enum, redef-record, export — where { stays on the
    same line as the keyword and } aligns with the keyword.
    """
    return concat(
        text("{"),
        nest(1, concat(HARDLINE, body)),
        HARDLINE,
        text("}"),
    )


def _format_whitesmith_block(stmts_doc: Doc, open_brace_comment: Doc = EMPTY) -> Doc:
    """Wrap statements in Whitesmith-style braces.

    Whitesmith: { and } indented to same level as body.
    Includes its own leading newline so the { gets proper indentation.
    stmts_doc is expected to end with HARDLINE (from last statement).
    open_brace_comment: trailing comment on the '{' (e.g., "# Start tracking.").
    """
    return nest(1, concat(
        HARDLINE,
        text("{"),
        open_brace_comment,
        HARDLINE,
        stmts_doc,
        text("}"),
    ))


def _parse_curly_block(kids: list[Node], start_idx: int, script: Script
                       ) -> tuple[Doc, Doc, Doc, int]:
    """Parse '{' stmt_list '}' from kids starting at start_idx.

    Extracts open-brace comments, statement body, and pre-close comments.
    Returns (open_brace_comment, stmts_doc, pre_close, next_idx).
    """
    idx = start_idx

    open_brace = kids[idx]
    assert _tok(open_brace) == "{"
    open_brace_comment = _format_next_cst(open_brace, script)
    open_brace.next_cst_siblings = []
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
    idx += 1

    return open_brace_comment, stmts_doc, pre_close, idx


def _format_curly_stmt_list(node: Node, script: Script, start_idx: int) -> tuple[Doc, int]:
    """Format '{' stmt_list '}' as Whitesmith block starting at start_idx.

    Returns (doc, next_idx).
    """
    obc, stmts_doc, pre_close, idx = _parse_curly_block(
        node.nonerr_children, start_idx, script)

    if stmts_doc == EMPTY and pre_close == EMPTY and obc == EMPTY:
        return text("{ }"), idx

    body = concat(stmts_doc, pre_close) if pre_close != EMPTY else stmts_doc
    return _format_whitesmith_block(body, obc), idx


# ---------------------------------------------------------------------------
# Declaration formatters
# ---------------------------------------------------------------------------

def _format_source_file(node: Node, script: Script) -> Doc:
    """Top-level source_file: sequence of decls/stmts with blank line preservation."""
    items = [c for c in node.nonerr_children if _name(c) in ("decl", "stmt")]
    if not items:
        # Comment/error-only file: comments live as prev_cst on a nullnode
        # child; error nodes live as prev_error_siblings.
        parts: list[Doc] = []
        for child in node.nonerr_children:
            for err in child.prev_error_siblings:
                parts.append(_format_error(err, script))
            cst = _format_prev_cst(child, script)
            if cst != EMPTY:
                parts.append(cst)
            cst = _format_next_cst(child, script)
            if cst != EMPTY:
                parts.append(cst)
            for err in child.next_error_siblings:
                parts.append(_format_error(err, script))
        return concat(*parts) if parts else EMPTY
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
        _format_semi(kids, script),
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
        _format_same_line_brace_block(_strip_trailing_hardline(body)),
        HARDLINE,
    )


def _type_constructor_name(type_node: Node) -> str | None:
    """Extract constructor name (vector/table/set) from a type node."""
    kids = type_node.nonerr_children
    if not kids:
        return None
    first = kids[0].type
    if first in ("vector", "table", "set"):
        return first
    return None


def _format_typed_initializer(kids: list[Node], start_idx: int, script: Script) -> tuple[Doc, int]:
    """Format [: <type>] [<initializer>] [<attr_list>] sequence.

    Returns (doc, next_idx).
    """
    idx = start_idx
    parts: list[Doc] = []

    type_name = None
    has_explicit_type = idx < len(kids) and _tok(kids[idx]) == ":"
    type_doc = EMPTY
    if has_explicit_type:
        idx += 1  # skip ':'
        type_node = kids[idx]
        type_name = _type_constructor_name(type_node)
        type_doc = format_child(type_node, script)  # <type>
        idx += 1

    init_doc = EMPTY
    is_constructor_init = False
    if idx < len(kids) and _name(kids[idx]) == "initializer":
        # Transform { } to constructor() form when type is known,
        # or auto-detect set/table when no explicit type
        if not has_explicit_type:
            constructor = ""  # sentinel: auto-detect from content
        else:
            constructor = type_name  # None if type isn't vector/table/set
        # Detect if the initializer is a constructor form (brace or
        # set/vector/table call) — attr_list placement differs.
        if constructor is not None:
            init_kids = kids[idx].nonerr_children
            if len(init_kids) >= 2:
                expr_kids = init_kids[1].nonerr_children
                if expr_kids and _tok(expr_kids[0]) in ("{", "set", "vector", "table"):
                    is_constructor_init = True
        init_doc = concat(SPACE, _format_initializer_node(
            kids[idx], script, constructor=constructor))
        idx += 1

    attr_doc = EMPTY
    if idx < len(kids) and _name(kids[idx]) == "attr_list":
        attr_doc = format_child(kids[idx], script)
        idx += 1

    if has_explicit_type:
        parts.append(text(":"))
        parts.append(SPACE)
        if attr_doc != EMPTY and not is_constructor_init:
            # align() captures column at the type keyword.  The inner
            # group() independently checks whether attr fits on the
            # current line; if not, SOFTLINE breaks to align indent
            # and text(" ") gives 1-space offset past the type keyword.
            # The inner align() around attr_doc ensures that when attrs
            # wrap among themselves, they all align to the same column.
            parts.append(align(concat(
                type_doc, init_doc,
                group(concat(SOFTLINE, text(" "), align(attr_doc))),
            )))
        else:
            # Constructor initializer or no attr: attr follows the
            # closing ')' directly with a space (no type-column align).
            parts.append(type_doc)
            parts.append(init_doc)
            if attr_doc != EMPTY:
                parts.append(SPACE)
                parts.append(attr_doc)
    else:
        parts.append(init_doc)
        if attr_doc != EMPTY:
            parts.append(SPACE)
            parts.append(attr_doc)

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
    parts.append(_format_semi(kids, script))
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
            blank = _wants_blank_before(ts)
            if not blank and not _prev_cst_has_blank_line(ts):
                blank = _blank_line_in_source(
                    type_specs[i - 1], ts, script.source)
            body_parts.append(HARDLINE)
            if blank:
                body_parts.append(HARDLINE)
        body_parts.append(format_child(ts, script))

    body = concat(*body_parts)

    return concat(text("record"), SPACE, _format_same_line_brace_block(body))


def _format_type_enum(node: Node, script: Script) -> Doc:
    # enum { <enum_body> }
    kids = node.nonerr_children
    has_body = any(_name(c) == "enum_body" for c in kids)

    if not has_body:
        return concat(text("enum"), SPACE, text("{ }"))

    enum_body = next(c for c in kids if _name(c) == "enum_body")
    do_linebreak = _has_comments(node) or _compact_length(enum_body) > 80

    if do_linebreak:
        pre_body = _format_prev_cst(enum_body, script)
        enum_body.prev_cst_siblings = []
        body_doc = _format_enum_body_multiline(enum_body, script)
        body = concat(pre_body, body_doc)
        return concat(text("enum"), SPACE, _format_same_line_brace_block(body))
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
        bracket_content = group(concat(
            text("["),
            align(concat(fill(*intersperse(sep, type_docs)), text("]"))),
        ))
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
            parts.append(format_child(child, script))
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
    # Check for attr_list to pass into func_params for aligned wrapping.
    attr_suffix = EMPTY
    attr_suffix_outside = EMPTY
    if (idx + 1 < len(kids) and _name(kids[idx + 1]) == "attr_list"):
        attr_doc = format_child(kids[idx + 1], script)
        # Compute paren column to decide if attr fits at paren alignment.
        prefix_w = sum(_flat_width(p, MAX_WIDTH) or 0 for p in parts) + 1  # +1 for '('
        attr_w = _flat_width(attr_doc, MAX_WIDTH) or MAX_WIDTH
        if prefix_w + attr_w < MAX_WIDTH:
            # Attr fits at paren-column alignment
            attr_suffix = concat(LINE, attr_doc)
        else:
            # Attr too long for paren alignment — right-align so the
            # attr line ends near MAX_WIDTH.  dedent() resets indent
            # to col 0 so we can position with exact spaces.
            indent_col = max(TAB_SIZE, MAX_WIDTH - attr_w - 3)
            attr_suffix_outside = dedent(concat(
                LINE, text(" " * indent_col), attr_doc))

    parts.append(_format_func_params(kids[idx], script,
                                     attr_suffix=attr_suffix,
                                     attr_suffix_outside=attr_suffix_outside))
    idx += 1

    if idx < len(kids) and _name(kids[idx]) == "attr_list":
        idx += 1  # already handled via attr_suffix

    return concat(*parts)


def _format_func_params(node: Node, script: Script,
                        attr_suffix: Doc = EMPTY,
                        attr_suffix_outside: Doc = EMPTY) -> Doc:
    # ( [formal_args] ) [: <type>] [attr_suffix]
    kids = node.nonerr_children
    parts: list[Doc] = []
    idx = 0

    parts.append(text("("))  # '('
    idx += 1

    # Build the trailing content after args: ) [: <type>]
    args_idx = idx
    has_args = idx < len(kids) and _name(kids[idx]) == "formal_args"
    if has_args:
        idx += 1  # formal_args
        close_paren = kids[idx]
        idx += 1  # ')'
    else:
        close_paren = kids[idx]
        idx += 1  # ')'
    trailing_parts: list[Doc] = [format_child(close_paren, script)]

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
        parts.append(align(concat(pre, args_doc, attr_suffix)))
        # attr_suffix_outside is placed outside align() for cases where
        # paren-column alignment would exceed 80 columns.
        parts.append(attr_suffix_outside)
    else:
        parts.append(concat(trailing, attr_suffix, attr_suffix_outside))

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
    return fill(*intersperse(sep, items), shrink_overflow=True)


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
    obc, stmts_doc, pre_close, _ = _parse_curly_block(kids, 0, script)

    if stmts_doc == EMPTY and pre_close == EMPTY and obc == EMPTY:
        # Empty body still uses Whitesmith layout: newline + indented { }
        return nest(1, concat(HARDLINE, text("{ }")))

    body = concat(stmts_doc, pre_close) if pre_close != EMPTY else stmts_doc
    return _format_whitesmith_block(body, obc)


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
    type_doc = format_child(kids[idx], script)  # <type>
    idx += 1
    if idx < len(kids) and _name(kids[idx]) == "attr_list":
        attr_doc = format_child(kids[idx], script)
        idx += 1
        semi_doc = _format_semi(kids, script)
        # align() captures column at type keyword; group allows
        # attrs+semi to break to continuation when line overflows
        # (e.g. trailing comment pushes past 80 columns).
        parts.append(align(concat(
            type_doc,
            group(concat(SOFTLINE, text(" "), align(concat(attr_doc, semi_doc)))),
        )))
    else:
        parts.append(type_doc)
        parts.append(_format_semi(kids, script))
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
    idx += 1  # skip '{'

    # enum_body
    if idx < len(kids) and _name(kids[idx]) == "enum_body":
        enum_body = kids[idx]
        if not do_linebreak:
            do_linebreak = _compact_length(enum_body) > 60  # generous threshold

        if do_linebreak:
            pre_body = _format_prev_cst(enum_body, script)
            enum_body.prev_cst_siblings = []
            body_doc = _format_enum_body_multiline(enum_body, script)
            parts.append(_format_same_line_brace_block(concat(pre_body, body_doc)))
        else:
            body_doc = _format_enum_body_inline(enum_body, script)
            parts.append(group(concat(
                text("{"), SPACE, body_doc, SPACE, text("}"),
            )))
        idx += 1
    else:
        parts.append(text("{"))
        parts.append(text("}"))

    idx += 1  # skip '}'
    parts.append(_format_semi(kids, script))
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
        idx += 1  # skip '{'
        # type_specs (don't end with HARDLINE, need separators)
        # Preserve blank lines between fields when present in source.
        type_specs: list[Node] = []
        while idx < len(kids) and _name(kids[idx]) == "type_spec":
            type_specs.append(kids[idx])
            idx += 1
        if type_specs:
            body_parts: list[Doc] = [format_child(type_specs[0], script)]
            for i in range(1, len(type_specs)):
                blank = (_wants_blank_before(type_specs[i])
                         or (not _prev_cst_has_blank_line(type_specs[i])
                             and _blank_line_in_source(type_specs[i - 1],
                                                       type_specs[i],
                                                       script.source)))
                if blank:
                    body_parts.append(HARDLINE)
                body_parts.append(HARDLINE)
                body_parts.append(format_child(type_specs[i], script))
            body = concat(*body_parts)
            parts.append(_format_same_line_brace_block(body))
        else:
            parts.append(text("{ }"))
        idx += 1  # skip '}'
        if idx < len(kids) and _name(kids[idx]) == "attr_list":
            parts.append(SPACE)
            parts.append(format_child(kids[idx], script))
            idx += 1
        parts.append(_format_semi(kids, script))

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
        return concat(format_child(kids[0], script), HARDLINE)
    else:
        # Fallback
        return concat(_format_space_separated(node, script), HARDLINE)


def _format_stmt_block(node: Node, script: Script) -> Doc:
    """Format a standalone { stmt_list } block statement.

    Standalone blocks (not control-flow bodies) use braces at the current
    indent level. Control-flow bodies use _format_indented_body which calls
    _format_curly_stmt_list for the Whitesmith style.
    """
    obc, stmts_doc, pre_close, _ = _parse_curly_block(
        node.nonerr_children, 0, script)

    if stmts_doc == EMPTY and pre_close == EMPTY and obc == EMPTY:
        return concat(text("{ }"), HARDLINE)

    body = concat(stmts_doc, pre_close) if pre_close != EMPTY else stmts_doc
    return concat(text("{"), obc, HARDLINE, body, text("}"), HARDLINE)


def _format_stmt_print_or_event(node: Node, script: Script) -> Doc:
    # print/event <expr_list>/<event_hdr> ;
    kids = node.nonerr_children
    keyword = _source_str(kids[0], script)
    parts = [text(keyword), SPACE]

    # Align continuation to after 'print '/'event '
    inner_parts: list[Doc] = []
    for child in kids[1:]:
        if _tok(child) == ";":
            inner_parts.append(format_child(child, script))
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


def _format_paren_expr(
    kids: list[Node], idx: int, script: Script, use_align: bool = True,
) -> tuple[Doc, int]:
    """Format ( SPACE expr SPACE ) with format_child on ')' for CST siblings.

    idx should point at the '(' child. Returns (doc, new_idx).
    """
    idx += 1  # skip '('
    expr_doc = format_child(kids[idx], script)
    idx += 1
    close_doc = format_child(kids[idx], script)  # ')' — preserves trailing comments
    idx += 1
    inner = align(expr_doc) if use_align else expr_doc
    return concat(text("("), SPACE, inner, SPACE, close_doc), idx


def _format_stmt_if(node: Node, script: Script) -> Doc:
    kids = node.nonerr_children
    parts: list[Doc] = []
    idx = 0

    parts.append(text("if"))  # 'if'
    idx += 1
    parts.append(SPACE)
    paren_doc, idx = _format_paren_expr(kids, idx, script)
    parts.append(paren_doc)

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
        # Body always ends with HARDLINE (both brace-block and non-block).
        # Use format_child so CST siblings (comments before else) are emitted.
        # Preserve blank line before else when present in source.
        # The blank line's newlines may be split across the body stmt's
        # next_cst_siblings and the else's prev_cst_siblings, so check
        # the raw source between the body's AST end and the else keyword.
        # Skip this when else has comment CST siblings — _format_prev_cst
        # handles blank lines around comments itself.
        else_node = kids[idx]
        has_else_comment = any(_is_comment(s) for s in else_node.prev_cst_siblings)
        if not has_else_comment:
            gap = script.source[body_child.end_byte:else_node.start_byte]
            if gap.count(b"\n") >= 2:
                parts.append(HARDLINE)
        parts.append(format_child(else_node, script))
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

    # expr may be ( <inner_expr> ) or bare expr
    expr_node = kids[idx]
    expr_kids = expr_node.nonerr_children
    if _tok(expr_kids[0]) == "(":
        # Parenthesized: add spaces inside parens
        parts.append(text("("))
        parts.append(SPACE)
        parts.append(format_child(expr_kids[1], script))
        parts.append(SPACE)
        parts.append(text(")"))
    else:
        # Bare expression
        parts.append(format_child(expr_node, script))
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
    parts.append(format_child(kids[idx], script))  # ')' — use format_child for trailing comments
    idx += 1

    # Body
    if idx < len(kids):
        parts.append(_format_indented_body(kids[idx], script))

    return concat(*parts)


def _format_stmt_while(node: Node, script: Script) -> Doc:
    kids = node.nonerr_children
    parts = [text("while"), SPACE]
    idx = 1  # skip 'while'

    paren_doc, idx = _format_paren_expr(kids, idx, script)
    parts.append(paren_doc)

    # Body
    if idx < len(kids):
        parts.append(_format_indented_body(kids[idx], script))

    return concat(*parts)


def _format_stmt_simple(node: Node, script: Script) -> Doc:
    # next;  break;  fallthrough;
    kids = node.nonerr_children
    keyword = _source_str(kids[0], script)
    return concat(text(keyword), _format_semi(kids, script), HARDLINE)


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

    parts.append(_format_semi(kids, script))
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
        _format_semi(kids, script),
        HARDLINE,
    )


def _format_stmt_local(node: Node, script: Script) -> Doc:
    # local/const <id> [: <type>] [= <expr>] [<attr_list>] ;
    kids = node.nonerr_children
    keyword = _source_str(kids[0], script)
    id_doc = format_child(kids[1], script)

    typed_init, idx = _format_typed_initializer(kids, 2, script)
    semi = format_child(kids[idx], script) if idx < len(kids) and _tok(kids[idx]) == ";" else EMPTY

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

    paren_doc, idx = _format_paren_expr(kids, idx, script, use_align=False)
    parts.append(paren_doc)

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
    """Format '{' stmt_list '}' as Whitesmith block from a flat kids list."""
    obc, stmts_doc, pre_close, idx = _parse_curly_block(kids, start_idx, script)

    if stmts_doc == EMPTY and pre_close == EMPTY:
        return text("{ }"), idx

    body = concat(stmts_doc, pre_close) if pre_close != EMPTY else stmts_doc
    return _format_whitesmith_block(body, obc), idx


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
    expr_doc = format_child(kids[0], script)
    semi_doc = _format_semi(kids, script)
    # Include the semi (and any trailing comment) inside the expression's
    # fill so the fill's last group accounts for trailing content.
    # Skip injection for #@ annotation comments — they should not count
    # toward line length.
    semi_node = _find_semi(kids)
    has_annotation = semi_node is not None and any(
        _source_str(sib, script).startswith("#@")
        for sib in (semi_node.next_cst_siblings or [])
        if _is_comment(sib)
    )
    if not has_annotation:
        merged, ok = _inject_trailing_into_fill(expr_doc, semi_doc)
        if ok:
            return concat(merged, HARDLINE)
    return concat(expr_doc, semi_doc, HARDLINE)


def _format_stmt_assert(node: Node, script: Script) -> Doc:
    # assert <expr> [<assert_msg>] ;
    kids = node.nonerr_children
    parts = [text("assert"), SPACE]
    for child in kids[1:]:
        if _tok(child) == ";":
            parts.append(format_child(child, script))
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

    cn0 = _child_name(node, 0)
    tok0 = _child_tok(node, 0)
    tok1 = _child_tok(node, 1)
    tok2 = _child_tok(node, 2)

    # Index: expr[...]
    if cn0 == "expr" and tok1 == "[":
        return _format_expr_index(node, script)
    # Record field: expr$field
    if cn0 == "expr" and tok1 == "$":
        return _format_expr_field_access(node, script)
    # Index slice: expr[a:b]
    if cn0 == "expr" and _child_name(node, 1) == "index_slice":
        return _format_expr_index_slice(node, script)
    # Negation: ! expr
    if tok0 == "!":
        return _format_expr_negation(node, script)
    # Unary: ++, --, ~, -, +, |
    if tok0 in ("|", "++", "--", "~", "-", "+"):
        return _format_no_space(node, script)
    # !in operator: tree-sitter gives 4 children (expr ! in expr)
    if cn0 == "expr" and tok1 == "!" and tok2 == "in":
        return _format_expr_not_in(node, script)
    # Initializers: { ... } or [ ... ]
    if tok0 in ("{", "["):
        return _format_expr_initializer(node, script)
    # Parenthesized: ( expr )
    if tok0 == "(":
        return _format_expr_paren(node, script)
    # Field assign: $name = expr
    if tok0 == "$" and tok2 == "=":
        return _format_expr_field_assign(node, script)
    # Field lambda: $name <begin_lambda> = <func_body>
    if tok0 == "$" and _child_name(node, 2) == "begin_lambda":
        return _format_expr_field_lambda(node, script)
    # copy(expr)
    if tok0 == "copy":
        return _format_expr_copy(node, script)
    # Has-field: expr?$field — never breaks, but must be grouped
    # with its child so the child's group accounts for trailing ?$id
    if tok1 == "?$":
        return _format_expr_has_field(node, script)
    # Anonymous function: function <begin_lambda> <func_body>
    if tok0 == "function":
        return _format_expr_anon_func(node, script)
    # Function/constructor calls: name(...)
    if tok1 == "(":
        return _format_expr_call(node, script)
    # Binary boolean: && ||
    if _nch(node) == 3 and tok1 in ("||", "&&"):
        return _format_expr_boolean(node, script)
    # Ternary: cond ? true : false
    if _nch(node) == 5 and tok1 == "?" and _child_tok(node, 3) == ":":
        return _format_expr_ternary(node, script)
    # Assignment: lhs = rhs
    if _nch(node) == 3 and tok1 in ("=", "+=", "-="):
        return _format_expr_assignment(node, script)
    # Binary operators
    if _nch(node) == 3 and tok1 in (
        "/", "*", "+", "-", "%",
        "==", "!=", "<", ">", "<=", ">=",
        "&", "|", "^", "~", "!~", "in",
    ):
        return _format_expr_binary(node, script)
    # Schedule
    if tok0 == "schedule":
        return _format_expr_schedule(node, script)

    # Default: space-separated
    return _format_space_separated(node, script)


def _format_expr_index(node: Node, script: Script) -> Doc:
    # <expr> '[' <expr_list> ']'
    kids = node.nonerr_children
    return concat(
        format_child(kids[0], script),  # <expr>
        text("["),
        align_capped(concat(
            format_child(kids[2], script),  # <expr_list>
            text("]"),
        ), MAX_ALIGN_COL),
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


def _format_expr_has_field(node: Node, script: Script) -> Doc:
    # <expr> ?$ <id> — never breaks internally, but the suffix must be
    # accounted for in any fill() width decisions inside the child expr
    # (e.g. boolean chains where tree-sitter gives ?$ lower precedence
    # than ||).  Inject the suffix into the innermost fill so the fill's
    # last item includes ?$id in its flat-width check.
    kids = node.nonerr_children
    child_doc = format_child(kids[0], script)
    suffix = concat(*[format_child(c, script) for c in kids[1:]])

    # Try to inject suffix into innermost fill for width accounting.
    merged, ok = _inject_trailing_into_fill(child_doc, suffix)
    if ok:
        return merged

    # Fallback: if the child is Group(inner), extend the inner doc
    # so the group's flat/break decision accounts for the full width.
    if isinstance(child_doc, Group):
        return Group(concat(child_doc.doc, suffix))

    return concat(child_doc, suffix)


def _format_expr_initializer(node: Node, script: Script) -> Doc:
    # { expr_list } or [ expr_list ]
    kids = node.nonerr_children
    ct1 = _tok(kids[0])
    close = "}" if ct1 == "{" else "]"

    is_record = ct1 == "[" and _is_record_constructor(node)

    # Check if there's an expr_list
    has_content = len(kids) > 2 and _name(kids[1]) == "expr_list"

    if not has_content:
        # Empty: [] or {}
        return concat(text(ct1), text(close))

    expr_list = kids[1]
    do_linebreak = _has_comments(node) or _compact_length(expr_list) > 80

    if is_record:
        return _format_record_constructor(node, script, do_linebreak)

    has_comments = _has_comments(node)

    if has_comments:
        # Comments force one-per-line layout with tab indentation.
        # dedent_spaces escapes enclosing align() so items nest at
        # pure tab level, not tab+spaces.
        body = _format_expr_list_multiline(expr_list, script)
        return concat(
            text(ct1),
            dedent_spaces(concat(
                nest(1, concat(HARDLINE, body)),
                HARDLINE,
                text(close),
            )),
        )
    elif do_linebreak and ct1 == "{":
        # Multi-line brace init (long, no comments)
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


def _widest_record_field(node: Node, script: Script) -> int:
    """Return the flat width of the widest $field=val in a record constructor."""
    kids = node.nonerr_children
    if len(kids) < 2 or _name(kids[1]) != "expr_list":
        return 0
    widest = 0
    for child in kids[1].nonerr_children:
        if _name(child) == "expr":
            w = _flat_width(format_child(child, script), MAX_WIDTH)
            if w is not None and w > widest:
                widest = w
    return widest


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

    # Use fill() with LINE separators so fields can wrap when the
    # enclosing context (e.g., a function call) needs line breaks.
    # fill() packs as many fields per line as fit.
    # After a field that will definitely span multiple lines (flat
    # width would overflow at any realistic alignment column and has
    # break points), use HARDLINE so the next field starts on a fresh
    # line.  Record constructors always sit inside "[" plus at least
    # one tab of nesting, so we use TAB_SIZE + 1 as a minimum column
    # estimate to catch fields that are under MAX_WIDTH in isolation
    # but overflow once alignment is applied.
    min_col = TAB_SIZE + 1
    fill_parts: list[Doc] = [field_docs[0]]
    for i in range(1, len(field_docs)):
        prev = field_docs[i - 1]
        prev_fw = _flat_width(prev, MAX_WIDTH)
        if _can_break(prev) and (prev_fw is None or prev_fw + min_col >= MAX_WIDTH):
            fill_parts.append(concat(text(","), HARDLINE))
        else:
            fill_parts.append(concat(text(","), LINE))
        fill_parts.append(field_docs[i])
    items = fill(*fill_parts)
    return concat(
        text("["),
        align(concat(items, text("]"))),
    )


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



def _format_expr_anon_func(node: Node, script: Script) -> Doc:
    # function<begin_lambda> [: <type>] <func_body>
    # No space between 'function' and begin_lambda (starts with '(')
    # func_body uses dedent() to escape enclosing align() contexts,
    # so the Whitesmith block gets tab-based indentation.
    kids = node.nonerr_children
    parts: list[Doc] = []
    for i, child in enumerate(kids):
        if _name(child) == "func_body":
            # func_body already starts with HARDLINE; no SPACE needed.
            parts.append(dedent_spaces(format_child(child, script)))
        else:
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
    open_paren = kids[1]
    has_open_comment = any(
        _is_comment(sib) for sib in open_paren.next_cst_siblings
    )
    idx = 2  # skip name and '('

    # Arguments
    if idx < len(kids) and _name(kids[idx]) == "expr_list":
        expr_list = kids[idx]
        # Check if any arg is a record constructor — if so, use SPACE
        # before it instead of LINE so the '[' stays on the same line
        # as the preceding arg and the record's fields wrap internally.
        has_record_arg = any(
            _name(c) == "expr" and _is_record_constructor(c)
            for c in expr_list.nonerr_children
        )
        if has_record_arg:
            # Pass call prefix width so record can decide LINE vs SPACE
            call_prefix_w = (_flat_width(parts[0], MAX_WIDTH) or 0) + 1  # name + '('
            args_doc = _format_expr_list_with_record(
                expr_list, script, call_prefix_w=call_prefix_w)
        else:
            args_doc = _format_expr_list_inline(expr_list, script, shrink_overflow=True)
        close = text(")")
        if has_open_comment:
            # Comment after '(' forces args to next line, aligned after '('
            comment_doc = _format_next_cst(open_paren, script)
            open_paren.next_cst_siblings = []
            parts.append(text("("))
            parts.append(align(concat(comment_doc, HARDLINE, args_doc, close)))
        else:
            parts.append(format_child(open_paren, script))
            parts.append(align(concat(args_doc, close)))
        idx += 1
        idx += 1  # skip ')'
    else:
        parts.append(format_child(open_paren, script))
        parts.append(text(")"))
        idx += 1

    # Attributes
    if idx < len(kids) and _name(kids[idx]) == "attr_list":
        parts.append(SPACE)
        parts.append(format_child(kids[idx], script))

    return group(concat(*parts))


_BOOLEAN_OPS = frozenset({"&&", "||"})


def _collect_boolean_chain(node: Node, script: Script) -> list[tuple[Doc, str | None]]:
    """Collect a left-recursive chain of boolean operators (&&, ||).

    Returns [(operand_doc, op_after), ...] where the last has op_after=None.
    Only chains same-precedence operators: stops flattening when op changes.
    """
    kids = node.nonerr_children
    op = _source_str(kids[1], script)
    lhs = kids[0]
    rhs = kids[2]

    lhs_kids = lhs.nonerr_children if lhs.nonerr_children else []
    if (len(lhs_kids) == 3
            and _name(lhs) == "expr"
            and _source_str(lhs_kids[1], script) == op):
        chain = _collect_boolean_chain(lhs, script)
    else:
        chain = [(format_child(lhs, script), None)]

    last_doc, _ = chain[-1]
    chain[-1] = (last_doc, op)
    chain.append((format_child(rhs, script), None))
    return chain


def _format_expr_boolean(node: Node, script: Script) -> Doc:
    # <expr> && <expr> or <expr> || <expr>
    # Flatten chains of same operator into fill() for greedy packing.
    kids = node.nonerr_children
    op = _source_str(kids[1], script)

    chain = _collect_boolean_chain(node, script)
    fill_items: list[Doc] = []
    for i, (operand, chain_op) in enumerate(chain):
        if i > 0:
            fill_items.append(LINE)
        if chain_op is not None:
            fill_items.append(concat(operand, SPACE, text(chain_op)))
        else:
            fill_items.append(operand)
    return group(align(fill(*fill_items)))


def _format_expr_ternary(node: Node, script: Script) -> Doc:
    # <expr> ? <expr> : <expr>
    kids = node.nonerr_children
    cond = format_child(kids[0], script)
    true_expr = format_child(kids[2], script)
    false_expr = format_child(kids[4], script)

    cond_w = _flat_width(cond, MAX_WIDTH) or MAX_WIDTH
    true_w = _flat_width(true_expr, MAX_WIDTH) or MAX_WIDTH

    if cond_w + true_w + 5 <= MAX_WIDTH:
        # "cond ? true :" can fit on one line. Keep ? inline and use
        # align() after "? " so a : break aligns false with true.
        return concat(
            cond, SPACE, text("?"), SPACE,
            align(group(concat(
                true_expr, SPACE, text(":"),
                LINE,
                false_expr,
            ))),
        )
    else:
        # "cond ? true :" is too wide — use fill for independent ? and
        # : breaks, both aligning to the condition column.
        return align(fill(
            concat(cond, SPACE, text("?")),
            LINE,
            concat(true_expr, SPACE, text(":")),
            LINE,
            false_expr,
        ))


def _rhs_align_depth(node: Node, script: Script) -> int:
    """Estimate how many chars into a RHS expr the first align() point is.

    For function calls like AzureEPData(...), returns len("AzureEPData(")
    since align() activates after the '('.  For other expressions, returns 0.
    """
    kids = node.nonerr_children
    if not kids:
        return 0
    # Unwrap single-child expr wrappers
    while len(kids) == 1 and _name(kids[0]) == "expr":
        kids = kids[0].nonerr_children
    # Function call: name + '('
    if len(kids) >= 2 and _tok(kids[1]) == "(":
        name_w = _flat_width(format_node(kids[0], script), MAX_WIDTH) or 0
        return name_w + 1  # name + '('
    return 0


def _format_expr_assignment(node: Node, script: Script) -> Doc:
    # <expr> = <expr>  or  <expr> += <expr>  etc.
    kids = node.nonerr_children
    op = _source_str(kids[1], script)
    lhs = format_child(kids[0], script)
    rhs = format_child(kids[2], script)
    if _can_break(rhs):
        # RHS has internal break points.  If the RHS fits flat at a
        # nested indent, allow breaking at '=' when the combined
        # LHS + RHS alignment depth would risk MISINDENTATION.
        # This is estimated by eq_offset (LHS + ' = ') plus the
        # RHS's additional alignment depth (e.g. a function call
        # name adds its width before '(' creates an align).
        rhs_fw = _flat_width(rhs, MAX_WIDTH)
        if rhs_fw is not None and rhs_fw + TAB_SIZE < MAX_WIDTH:
            lhs_fw = _flat_width(lhs, MAX_WIDTH) or MAX_WIDTH
            eq_offset = lhs_fw + len(op) + 2
            # Estimate how deep the RHS's first alignment goes —
            # for function calls, it's the function name + '('.
            rhs_node = kids[2]
            rhs_align_depth = _rhs_align_depth(rhs_node, script)
            if eq_offset + rhs_align_depth >= MAX_ALIGN_COL - TAB_SIZE * 3:
                return group(concat(lhs, SPACE, text(op),
                                    nest(1, dedent_spaces(concat(LINE, rhs)))))
        # Align after '= ' — internal groups handle wrapping.
        return concat(lhs, SPACE, text(op), SPACE, align(rhs))
    # Atomic RHS — allow breaking at '='.
    return group(concat(lhs, SPACE, text(op),
                        nest(1, dedent_spaces(concat(LINE, rhs)))))


# Operators that form chains and should be flattened into a single group.
_CHAINABLE_OPS = frozenset({"+", "-", "*", "/", "%"})


def _collect_binary_chain(node: Node, script: Script) -> list[tuple[Doc, str | None]]:
    """Collect a left-recursive chain of chainable binary operators.

    Returns [(operand_doc, op_after), ...] where the last entry has op_after=None.
    E.g. for `a + b + c - d`: [(a, "+"), (b, "+"), (c, "-"), (d, None)].
    """
    kids = node.nonerr_children
    op = _source_str(kids[1], script)
    lhs = kids[0]
    rhs = kids[2]

    # Check if LHS is also a chainable binary — if so, flatten
    lhs_kids = lhs.nonerr_children if lhs.nonerr_children else []
    if (len(lhs_kids) == 3
            and _name(lhs) == "expr"
            and _source_str(lhs_kids[1], script) in _CHAINABLE_OPS):
        chain = _collect_binary_chain(lhs, script)
    else:
        chain = [(format_child(lhs, script), None)]

    # Set the operator on the last entry, then append rhs
    last_doc, _ = chain[-1]
    chain[-1] = (last_doc, op)
    chain.append((format_child(rhs, script), None))
    return chain


def _format_expr_binary(node: Node, script: Script) -> Doc:
    # <expr> op <expr>
    kids = node.nonerr_children
    op = _source_str(kids[1], script)

    # Flatten chains of arithmetic operators into a single fill group.
    # fill() packs as many operands per line as fit, only breaking when needed.
    if op in _CHAINABLE_OPS:
        chain = _collect_binary_chain(node, script)
        # Build fill items: [operand1 op1, LINE, operand2 op2, LINE, ..., operandN]
        fill_items: list[Doc] = []
        for i, (operand, chain_op) in enumerate(chain):
            if i > 0:
                fill_items.append(LINE)
            if chain_op is not None:
                fill_items.append(concat(operand, SPACE, text(chain_op)))
            else:
                fill_items.append(operand)
        return align(fill(*fill_items))

    return group(align(concat(
        format_child(kids[0], script),
        SPACE,
        text(op),
        LINE,
        format_child(kids[2], script),
    )))


def _format_expr_not_in(node: Node, script: Script) -> Doc:
    # <expr> ! in <expr> — tree-sitter splits !in into two tokens
    kids = node.nonerr_children
    return group(align(concat(
        format_child(kids[0], script),
        SPACE,
        text("!in"),
        LINE,
        format_child(kids[3], script),
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


def _format_expr_list_with_record(node: Node, script: Script,
                                   call_prefix_w: int = 0) -> Doc:
    """Format expr_list where one arg is a record constructor.

    Uses SPACE before the record constructor arg when fields fit at
    the resulting alignment column, LINE when they'd overflow.
    """
    kids = node.nonerr_children
    items: list[Doc] = []
    commas: list[Doc] = []
    is_record: list[bool] = []
    record_nodes: list[Node | None] = []
    for child in kids:
        if _name(child) == "expr":
            items.append(format_child(child, script))
            is_rec = _is_record_constructor(child)
            is_record.append(is_rec)
            record_nodes.append(child if is_rec else None)
        elif _tok(child) == ",":
            commas.append(format_child(child, script))

    if not items:
        return EMPTY

    # Compute flat width of args before each record constructor to
    # decide whether fields would overflow if '[' stays inline.
    pre_width = call_prefix_w
    fill_parts: list[Doc] = [items[0]]
    pre_width += _flat_width(items[0], MAX_WIDTH) or 0
    for i in range(1, len(items)):
        comma_doc = commas[i - 1] if i - 1 < len(commas) else text(",")
        if is_record[i]:
            # Check: would widest field overflow if '[' stays here?
            # Column after ", [" = pre_width + 2 (", ") + 1 ("[")
            bracket_col = pre_width + 3
            rec_node = record_nodes[i]
            widest = _widest_record_field(rec_node, script)
            if bracket_col + widest > MAX_WIDTH - TAB_SIZE:
                fill_parts.append(concat(comma_doc, LINE))
            else:
                fill_parts.append(concat(comma_doc, SPACE))
        else:
            fill_parts.append(concat(comma_doc, LINE))
        fill_parts.append(items[i])
        pre_width += 2 + (_flat_width(items[i], MAX_WIDTH) or 0)
    return fill(*fill_parts)


def _format_expr_list_inline(node: Node, script: Script, shrink_overflow: bool = False) -> Doc:
    """Format expr_list as comma-separated items that wrap with fill."""
    kids = node.nonerr_children
    items: list[Doc] = []
    for child in kids:
        if _name(child) == "expr":
            items.append(format_child(child, script))
        # commas handled as separators

    if not items:
        return EMPTY

    # Detect trailing comma (comma after last expr with no following expr)
    has_trailing_comma = kids and _tok(kids[-1]) == ","

    # Build fill parts, using format_child on comma nodes to pick up
    # any CST siblings (e.g. #@ annotation comments).
    fill_parts: list[Doc] = [items[0]]
    comma_idx = 0
    for child in kids:
        if _tok(child) == ",":
            comma_doc = format_child(child, script)
            comma_idx += 1
            if has_trailing_comma and comma_idx == len(items):
                # Trailing comma — not a separator between items
                fill_parts.append(comma_doc)
                fill_parts.append(SPACE)
                break
            fill_parts.append(concat(comma_doc, LINE))
            if comma_idx < len(items):
                fill_parts.append(items[comma_idx])
    return fill(*fill_parts, shrink_overflow=shrink_overflow)


def _format_expr_list_multiline(node: Node, script: Script) -> Doc:
    """Format expr_list with one element per line."""
    kids = node.nonerr_children
    parts: list[Doc] = []
    for child in kids:
        if _name(child) == "expr":
            if parts:
                parts.append(HARDLINE)
            parts.append(format_child(child, script))
        elif _tok(child) == ",":
            parts.append(format_child(child, script))
    return concat(*parts)


# ---------------------------------------------------------------------------
# Other node formatters
# ---------------------------------------------------------------------------

def _format_initializer(node: Node, script: Script) -> Doc:
    return _format_initializer_node(node, script)


def _format_initializer_node(node: Node, script: Script,
                             constructor: str | None = None) -> Doc:
    # <init_class> <expr>  where init_class contains '=' or '+=' etc.
    # constructor: None = no transform, "" = auto-detect, "vector"/"table"/"set" = use that name
    kids = node.nonerr_children
    init_class = kids[0]  # contains the operator token
    op = _source_str(init_class, script)
    expr = kids[1]
    if constructor is not None:
        expr_doc = _format_expr_with_brace_transform(
            expr, script, constructor or None)
        # For very wide pass-through expressions (not actual constructors),
        # use nest(1) for shallow continuation instead of keeping together.
        expr_kids = expr.nonerr_children
        first_tok = _tok(expr_kids[0]) if expr_kids else None
        if first_tok not in ("{", "[", "set", "vector", "table"):
            expr_w = _flat_width(expr_doc, 500)
            if expr_w is not None and expr_w > 3 * MAX_WIDTH:
                return group(concat(
                    text(op),
                    nest(1, dedent_spaces(concat(LINE, expr_doc))),
                ))
            # Atomic RHS (no internal break points) — allow breaking
            # at '=' so the expression can move to the next line.
            if not _can_break(expr_doc):
                return group(concat(
                    text(op),
                    nest(1, dedent_spaces(concat(LINE, expr_doc))),
                ))
        # Constructor or breakable expression: keep "= expr" together
        return concat(text(op), SPACE, expr_doc)
    else:
        expr_doc = format_child(expr, script)
        # Lambda RHS: func_body has HARDLINE which would force the group
        # to break LINE after '='. Keep '= function(...)' together instead.
        if _has_lambda(expr):
            return concat(text(op), SPACE, expr_doc)
        # Align continuation to column after '= '. When the group
        # breaks, if_break inserts a newline aligned to after '= '.
        return group(concat(
            text(op),
            SPACE,
            align(concat(
                if_break(broken=LINE, flat=EMPTY),
                expr_doc,
            )),
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


def _format_constructor_layout(constructor: str, expr_list: Node,
                               has_comments: bool, script: Script) -> Doc:
    """Layout for set(...), vector(...), table(...) constructors.

    Multiline: items indented one tab past the enclosing block, ')' at
    the enclosing block's indent level (uses dedent_spaces to escape
    align() but preserve tab nesting).
    Inline: aligned after '('.
    """
    do_linebreak = has_comments or _compact_length(expr_list) > 80

    if do_linebreak:
        body = _format_expr_list_multiline(expr_list, script)
        return concat(
            text(constructor + "("),
            dedent_spaces(concat(
                nest(1, concat(HARDLINE, body)),
                HARDLINE,
                text(")"),
            )),
        )
    else:
        body = _format_expr_list_inline(expr_list, script)
        return group(concat(
            text(constructor + "("),
            align(concat(body, text(")"))),
        ))


def _format_constructor_call(node: Node, script: Script,
                             constructor: str) -> Doc:
    """Format set(...), vector(...), table(...) already in constructor form."""
    kids = node.nonerr_children
    has_content = any(_name(c) == "expr_list" for c in kids)
    if not has_content:
        return text(constructor + "()")

    expr_list = next(c for c in kids if _name(c) == "expr_list")
    return _format_constructor_layout(constructor, expr_list,
                                      _has_comments(node), script)


def _format_expr_with_brace_transform(node: Node, script: Script,
                                      constructor: str | None = None) -> Doc:
    """Format an expression, transforming { } to constructor() form.

    Also handles expressions that are already in constructor() form
    (e.g., set(...), vector(...), table(...)) by applying the same
    dedent multiline layout.
    """
    kids = node.nonerr_children
    if not kids:
        return format_child(node, script)

    # Already in constructor() form: set(...), vector(...), table(...)
    first_tok = _tok(kids[0])
    if first_tok in ("set", "vector", "table") and len(kids) >= 3 and _tok(kids[1]) == "(":
        return _format_constructor_call(node, script, first_tok)

    if first_tok != "{":
        return format_child(node, script)

    # Determine constructor name from content if not provided
    if constructor is None:
        constructor = "table" if _is_brace_init_table(node) else "set"

    # Get the content between { and }
    has_content = len(kids) > 2 and _name(kids[1]) == "expr_list"
    if not has_content:
        return text(constructor + "()")

    return _format_constructor_layout(constructor, kids[1],
                                      _has_comments(node), script)


def _format_index_slice(node: Node, script: Script) -> Doc:
    # [ expr : expr ] or [expr:] or [:expr] or [:]
    # Space around ':' when either side is compound (not literal/constant/empty)
    # Breaks after ':' when too long; RHS aligns with LHS (after '[')
    kids = node.nonerr_children

    # Determine if we need spaces around ':'
    use_space = False
    for child in kids:
        if _name(child) is not None and len(child.children) > 1:
            use_space = True
            break

    lhs_doc = EMPTY
    rhs_doc = EMPTY
    seen_colon = False
    for i, child in enumerate(kids):
        ct = _tok(child)
        if ct in ("[", "]"):
            continue
        elif ct == ":":
            seen_colon = True
        elif seen_colon:
            rhs_doc = format_child(child, script)
        else:
            lhs_doc = format_child(child, script)

    # Build: [ align( lhs : LINE rhs ] )
    colon_sp = SPACE if use_space and lhs_doc != EMPTY else EMPTY
    inner = concat(lhs_doc, colon_sp, text(":"))
    if rhs_doc != EMPTY:
        if use_space:
            inner = concat(inner, LINE, rhs_doc)
        else:
            inner = concat(inner, rhs_doc)

    return group(concat(text("["), align(concat(inner, text("]")))))


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

    if len(attrs) <= 1:
        return attrs[0] if attrs else EMPTY
    # Wrap in a group so attrs break independently when they overflow,
    # without being forced to break by an enclosing group's break mode.
    return group(join(LINE, attrs))


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
    # @if/@ifdef/@ifndef/@else/@endif ... - always at column 0, no linebreaking.
    # Use format_child for CST siblings (comments) but build a flat text
    # representation to prevent line-wrapping of expressions.
    kids = node.nonerr_children
    parts: list[Doc] = []
    for i, child in enumerate(kids):
        if i > 0:
            parts.append(SPACE)
        # Format children as plain text to avoid group/align line-breaking,
        # but process CST siblings (comments) normally.
        cst_doc = _format_next_cst(child, script)
        parts.append(text(_source_str(child, script)))
        if cst_doc != EMPTY:
            parts.append(cst_doc)
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
    "expr_list": _format_expr_list_inline,
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
