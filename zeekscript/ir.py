"""Document IR and resolver for zeekscript pretty-printing.

Implements a Wadler/Lindig-style document algebra. Formatters build Doc
trees from the parse tree; the resolver lays them out, choosing between
flat (single-line) and broken (multi-line) rendering for each Group.

Indentation is tab/space hybrid: Nest adds tab levels for block
structure, Align sets space-based continuation to the current column.
"""

from __future__ import annotations

import os
from dataclasses import dataclass
from typing import Sequence, Union

# --- Configuration ---

TAB_SIZE = 8
MAX_WIDTH = 80
MAX_ALIGN_COL = 60   # Alignment beyond this falls back to FALLBACK_INDENT
FALLBACK_INDENT = 4  # Spaces when alignment is too deep


# --- Document IR types ---

@dataclass(frozen=True, slots=True)
class Text:
    """Literal text. Must not contain newlines."""
    s: str

class Line:
    """Space when flat, newline+indent when broken."""
    __slots__ = ()
    def __repr__(self) -> str:
        return "LINE"

class SoftLine:
    """Nothing when flat, newline+indent when broken."""
    __slots__ = ()
    def __repr__(self) -> str:
        return "SOFTLINE"

class HardLine:
    """Always a newline+indent."""
    __slots__ = ()
    def __repr__(self) -> str:
        return "HARDLINE"

class Column0Line:
    """Always a newline to column 0 (no indentation)."""
    __slots__ = ()
    def __repr__(self) -> str:
        return "COLUMN0LINE"

@dataclass(frozen=True, slots=True)
class Concat:
    """Sequence of documents."""
    docs: tuple[Doc, ...]

@dataclass(frozen=True, slots=True)
class Nest:
    """Add n tab indentation levels for the inner document."""
    n: int
    doc: Doc

@dataclass(frozen=True, slots=True)
class Align:
    """Align continuation lines to the current column (using spaces after tabs)."""
    doc: Doc

@dataclass(frozen=True, slots=True)
class Group:
    """Try flat (single line); if it doesn't fit, break."""
    doc: Doc

@dataclass(frozen=True, slots=True)
class IfBreak:
    """Choose between two documents based on enclosing group's mode."""
    broken: Doc
    flat: Doc

@dataclass(frozen=True, slots=True)
class Fill:
    """Pack items with separators, breaking only when the next item won't fit.

    docs alternates: item, sep, item, sep, ..., item.
    Each sep is rendered flat if the following item fits, broken otherwise.
    """
    docs: tuple[Doc, ...]

@dataclass(frozen=True, slots=True)
class Dedent:
    """Reset indentation to column 0 for the inner document."""
    doc: Doc


Doc = Union[Text, Line, SoftLine, HardLine, Column0Line, Concat, Nest, Align, Group, IfBreak, Fill, Dedent]


# --- Singletons and common tokens ---

LINE = Line()
SOFTLINE = SoftLine()
HARDLINE = HardLine()
COLUMN0LINE = Column0Line()
EMPTY = Text("")
SPACE = Text(" ")


# --- Constructors ---

def text(s: str) -> Text:
    return Text(s)


def concat(*docs: Doc) -> Doc:
    """Concatenate documents, flattening nested Concats and dropping empties."""
    flat: list[Doc] = []
    for d in docs:
        if isinstance(d, Concat):
            flat.extend(d.docs)
        elif isinstance(d, Text) and not d.s:
            continue
        else:
            flat.append(d)
    if not flat:
        return EMPTY
    if len(flat) == 1:
        return flat[0]
    return Concat(tuple(flat))


def nest(n: int, doc: Doc) -> Nest:
    return Nest(n, doc)


def align(doc: Doc) -> Align:
    return Align(doc)


def group(doc: Doc) -> Group:
    return Group(doc)


def if_break(broken: Doc, flat: Doc) -> IfBreak:
    return IfBreak(broken, flat)


def fill(*docs: Doc) -> Fill:
    return Fill(tuple(docs))


def dedent(doc: Doc) -> Dedent:
    return Dedent(doc)


def join(sep: Doc, docs: Sequence[Doc]) -> Doc:
    """Join documents with a separator between each pair."""
    if not docs:
        return EMPTY
    parts: list[Doc] = [docs[0]]
    for d in docs[1:]:
        parts.append(sep)
        parts.append(d)
    return concat(*parts)


def intersperse(sep: Doc, docs: Sequence[Doc]) -> list[Doc]:
    """Like join but returns a flat list — useful for building Fill docs."""
    if not docs:
        return []
    result: list[Doc] = [docs[0]]
    for d in docs[1:]:
        result.append(sep)
        result.append(d)
    return result


# --- Indentation state (internal) ---

@dataclass(frozen=True, slots=True)
class _Indent:
    tabs: int = 0
    spaces: int = 0

    def render(self) -> str:
        return "\t" * self.tabs + " " * self.spaces

    def width(self) -> int:
        return self.tabs * TAB_SIZE + self.spaces


# --- Resolver ---

_FLAT = 0
_BREAK = 1


def _flat_width(doc: Doc, remaining: int) -> int | None:
    """Compute width of doc rendered in flat mode.

    Returns None if it exceeds remaining or contains a HardLine.
    """
    stack: list[Doc] = [doc]
    w = 0
    while stack:
        if w > remaining:
            return None
        d = stack.pop()
        if isinstance(d, Text):
            w += len(d.s)
        elif isinstance(d, Line):
            w += 1  # space in flat mode
        elif isinstance(d, SoftLine):
            pass    # nothing in flat mode
        elif isinstance(d, (HardLine, Column0Line)):
            return None  # forces break
        elif isinstance(d, Concat):
            for sub in reversed(d.docs):
                stack.append(sub)
        elif isinstance(d, (Nest, Align, Dedent)):
            stack.append(d.doc)
        elif isinstance(d, Group):
            stack.append(d.doc)
        elif isinstance(d, IfBreak):
            stack.append(d.flat)
        elif isinstance(d, Fill):
            for sub in reversed(d.docs):
                stack.append(sub)
    return w


def resolve(doc: Doc, max_width: int = MAX_WIDTH) -> bytes:
    """Resolve a document IR tree into formatted bytes output."""
    nl = os.linesep
    parts: list[str] = []
    col = 0

    # Stack entries: (indent_state, flat_or_break_mode, doc_node)
    stack: list[tuple[_Indent, int, Doc]] = [(_Indent(), _BREAK, doc)]

    while stack:
        indent, mode, d = stack.pop()

        if isinstance(d, Text):
            parts.append(d.s)
            col += len(d.s)

        elif isinstance(d, Line):
            if mode == _FLAT:
                parts.append(" ")
                col += 1
            else:
                parts.append(nl)
                ind = indent.render()
                parts.append(ind)
                col = indent.width()

        elif isinstance(d, SoftLine):
            if mode != _FLAT:
                parts.append(nl)
                ind = indent.render()
                parts.append(ind)
                col = indent.width()

        elif isinstance(d, HardLine):
            parts.append(nl)
            ind = indent.render()
            parts.append(ind)
            col = indent.width()

        elif isinstance(d, Column0Line):
            # If the last meaningful output was a newline (possibly followed
            # by whitespace-only indent), reuse it instead of adding another.
            # This prevents blank lines when a HARDLINE precedes a COLUMN0LINE.
            if parts:
                # Check if we're at the start of a line (only whitespace since last nl)
                tail = parts[-1]
                if tail.strip() == "" and len(parts) >= 2 and parts[-2].endswith(nl):
                    # Remove the indent-only part, we're going to column 0
                    parts.pop()
                    col = 0
                else:
                    parts.append(nl)
                    col = 0
            else:
                col = 0

        elif isinstance(d, Concat):
            for sub in reversed(d.docs):
                stack.append((indent, mode, sub))

        elif isinstance(d, Nest):
            ni = _Indent(indent.tabs + d.n, indent.spaces)
            stack.append((ni, mode, d.doc))

        elif isinstance(d, Align):
            sp = max(0, col - indent.tabs * TAB_SIZE)
            if col >= MAX_ALIGN_COL:
                sp = FALLBACK_INDENT
            ni = _Indent(indent.tabs, sp)
            stack.append((ni, mode, d.doc))

        elif isinstance(d, Dedent):
            stack.append((_Indent(), mode, d.doc))

        elif isinstance(d, Group):
            if mode == _FLAT:
                # Already flat — stay flat
                stack.append((indent, _FLAT, d.doc))
            else:
                fw = _flat_width(d.doc, max_width - col)
                if fw is not None and col + fw <= max_width:
                    stack.append((indent, _FLAT, d.doc))
                else:
                    stack.append((indent, _BREAK, d.doc))

        elif isinstance(d, IfBreak):
            if mode == _FLAT:
                stack.append((indent, mode, d.flat))
            else:
                stack.append((indent, mode, d.broken))

        elif isinstance(d, Fill):
            # Pack items, breaking separators only when the next item
            # won't fit.  docs alternates: item, sep, item, sep, ..., item.
            # Convert to: first_item, Group(sep+item), Group(sep+item), ...
            # Each Group independently decides flat vs break.
            if d.docs:
                to_push: list[tuple[_Indent, int, Doc]] = []
                to_push.append((indent, mode, d.docs[0]))
                for i in range(1, len(d.docs), 2):
                    sep = d.docs[i]
                    item = d.docs[i + 1] if i + 1 < len(d.docs) else EMPTY
                    to_push.append((indent, mode, Group(Concat((sep, item)))))
                for entry in reversed(to_push):
                    stack.append(entry)

    # Post-process: strip trailing whitespace from each line, ensure
    # the output ends with exactly one newline.
    result = "".join(parts)
    lines = result.split(nl)
    lines = [line.rstrip() for line in lines]
    result = nl.join(lines)
    if result and not result.endswith(nl):
        result += nl
    return result.encode("UTF-8")
