"""A class hierarchy for formatting a zeekscript.Node tree.

The root class, zeekscript.Formatter, provides methods for formatting a
zeekscript.Node to a zeekscript.OutputStream, including basic operations such as
writing spaces and newlines. Derivations specialize by formatting specific
node/symbol types. The NodeMapper class maps symbol type names to formatters.

The code frequently distinguishes abstract and concrete syntax trees (ASTs vs
CSTs). By this we mean the difference between nodes resulting from regular
production rules in the grammar vs "extra" rules. Tree-Sitter's notion of
"extra" rules covers constructs that can occur anywhere in the text. In the Zeek
grammar this includes newlines as well as comments (including Zeekygen
comments). The CST features such elements, whereas the AST does not. You can
examine the difference by playing with `zeek-script parse ...` vs `zeek-script
parse --concrete`.
"""

from __future__ import annotations

import enum
import inspect
import os
import sys
from collections.abc import Callable
from typing import TYPE_CHECKING, Protocol

from zeekscript.node import Node

if TYPE_CHECKING:
    from zeekscript.output import OutputStream
    from zeekscript.script import Script


class NodeMapper:
    """Maps symbol names in the TS grammar (e.g "module_decl") to formatter classes."""

    def __init__(self) -> None:
        self._map: dict[str, type[Formatter]] = {}

    def register(self, symbol_name: str, klass: type[Formatter]) -> None:
        """Map a given symbol name to a given formatter class."""
        self._map[symbol_name] = klass

    def get(self, symbol_name: str) -> type[Formatter]:
        """Returns a Formatter class for a given symbol name.

        If an explicit mapping was established earlier, this returns its
        result. Otherwise, it tries to map the symbol name to a corresponding
        class name ("module_decl" -> "ModuleDeclFormatter"). When this fails as
        well, it falls back to returning the Formatter class.
        """
        if symbol_name in self._map:
            return self._map[symbol_name]

        self._find_class(symbol_name)

        if symbol_name in self._map:
            return self._map[symbol_name]

        return Formatter

    def _find_class(self, symbol_name: str) -> None:
        """Establishes symbol type -> Formatter class mapping.

        For example, this will try to resolve symbol type "module_decl" as
        ModuleDeclFormatter. When such a class exists, this adds a mapping to
        the internal _map so we don't have to resolve next time.
        """
        name_parts = [part.title() for part in symbol_name.split("_")]
        derived = "".join(name_parts) + "Formatter"

        def pred(mem: object) -> bool:
            return inspect.isclass(mem) and mem.__name__ == derived

        classes = inspect.getmembers(sys.modules[__name__], pred)

        if classes:
            self._map[symbol_name] = classes[0][1]


MAP = NodeMapper()


# ---- Symbol formatters -------------------------------------------------------


class Hint(enum.Flag):
    """Linebreak hinting when we write out otherwise formatted lines.

    The formatters provide these hints based on their surrounding context.  On
    occasion, hinting is also used to pass flags from higher-level (in the tree)
    to lower-level Formatters.
    """

    NONE = enum.auto()
    GOOD_AFTER_LB = enum.auto()  # A linebreak before this item is encouraged.
    NO_LB_BEFORE = enum.auto()  # Never line-break before this item.
    NO_LB_AFTER = enum.auto()  # Never line-break after this item.
    ZERO_WIDTH = enum.auto()  # This item doesn't contribute to line length.
    COMPLEX_BLOCK = enum.auto()  # A {}-block is complex enough to linebreak
    INIT_ELEMENT = enum.auto()  # Element in multi-line initializer (tab indent if wrapping)
    INIT_LENIENT = enum.auto()  # Use lenient line length (don't wrap unless very long)
    ATTR_SPACES = enum.auto()  # Use spaces around '=' in attributes
    BRACE_TO_CONSTRUCTOR = enum.auto()  # Transform { } to set( ) or table( )
    SPACED_PARENS = enum.auto()  # Add spaces inside parentheses: ( expr )


class Formatter:
    # Our newline bytestring
    NL = os.linesep.encode("UTF-8")

    def __init__(
        self,
        script: Script,
        node: Node,
        ostream: OutputStream,
        indent: int = 0,
        hints: Hint = Hint.NONE,
    ):
        """Formatter constructor.

        The script argument is the zeekscript.Script instance we're
        formatting. node is a zeekscript.Node, and the actual syntax tree
        element that this formatter instance will format. ostream is a
        zeekscript.OutputStream that we're writing the formatting to. The indent
        argument, an integer, tracks the number of indentation levels we're
        currently writing at.
        """
        self.script = script
        self.node: Node = node
        self.ostream = ostream
        self.indent = indent
        self.hints: Hint = hints or Hint.NONE

        # AST child node index for iteration
        self._cidx = 0

        # Hook us into the node
        node.formatter = self

    def format(self) -> None:
        """Default formatting for a tree node

        This simply writes out children as per their own formatting, and writes
        out tokens directly.
        """
        if self.node.children:
            self._format_children()
        else:
            self._format_token()

    def _next_child(self) -> Node | None:
        try:
            node = self.node.nonerr_children[self._cidx]
            self._cidx += 1
            return node
        except IndexError:
            return None

    def _format_child_impl(
        self, node: Node, indent: int, hints: Hint = Hint.NONE
    ) -> None:
        fclass = Formatter.lookup(node)
        formatter = fclass(
            self.script,
            node,
            self.ostream,
            indent=self.indent + int(indent),
            hints=hints,
        )
        formatter.format()

    def _format_child(
        self, child: Node | None = None, indent: int = False, hints: Hint = Hint.NONE
    ) -> None:
        if child is None:
            child = self._next_child()
        if child is None:
            return

        # XXX Pretty subtle that we handle the child's surrounding context here,
        # in the parent. Might have to refactor in the future.

        # If the node has any preceding errors, render these out first.  Do
        # this via _format_child(), not _format_child_impl(), since the error
        # nodes are full-blown AST nodes potentially with their own CST
        # neighborhood.
        for node in child.prev_error_siblings:
            self._format_child(node, indent)

        for node in child.prev_cst_siblings:
            self._format_child_impl(node, indent)

        # The hints apply to AST (not CST) nodes, so now:
        self._format_child_impl(child, indent, hints)

        for node in child.next_cst_siblings:
            self._format_child_impl(node, indent)

        # Mirroring the above, handle any trailing errors last.
        for node in child.next_error_siblings:
            self._format_child(node, indent)

    def _format_child_range(self, num: int, hints: Hint | None = None) -> None:
        """Format a given number of children of the node.

        Using this function ensures that no line breaks can happen between the
        requested children. "num" is the number of children to format, "hint" is
        a set of hints to tuck onto every child, and "first_hints" is an
        additional possible hint set for the first child only. (There's
        currently no indent flag, since the concept doesn't make much sense for
        a sequence of children. This might change in the future.)
        """
        hints = hints or Hint.NONE

        if num <= 0:
            return

        if num == 1:
            # Single element: general and first-element hinting
            self._format_child(hints=hints)
            return

        # First element of multiple: general hinting; first-element hinting;
        # avoid line breaks after the element.
        self._format_child(hints=hints | Hint.NO_LB_AFTER)

        # Inner elements: general hinting; avoid line breaks
        for _ in range(num - 2):
            self._format_child(hints=hints | Hint.NO_LB_AFTER)

        # Last element: general hinting; avoid line break before
        self._format_child(hints=hints | Hint.NO_LB_BEFORE)

    def _format_children(self, sep: bytes | str | None = None) -> None:
        """Format all children of the node.

        sep is an optional separator string placed between every child. The
        function propagates any layouting hint in effect for this instance to
        the first child, so the hint does not get lost on the path down the
        tree.
        """
        if self._children_remaining():
            self._format_child(hints=self.hints)

        while self._children_remaining():
            if sep is not None:
                self._write(sep)
            self._format_child()

    def _format_token(self) -> None:
        self._write(self.script.get_content(*self.node.script_range()))

    def _format_curly_statement_list(self, indent: bool = True) -> None:
        """Format a child sequence of '{' <stmt_list>? '}'

        The statement list is optional, and we need to take into account
        comments to tweak the layout to "{ }" if there's really nothing between
        the braces. The need to indent depends on the caller's context.
        """
        self._format_child(indent=indent, hints=Hint.NO_LB_BEFORE)  # '{'

        # Shorten braces to "{ }" if there is at most whitespace between them.
        if self._get_child_token() == "}":
            if child := self._get_child():
                if child.has_only_whitespace_before():
                    self._write_sp()
                    self._format_child()  # '}'
                    return

        self._write_nl()

        if self._get_child_type() == "stmt_list":
            self._format_child(indent=indent)  # <stmt_list>
            self._write_nl()
        self._format_child(indent=indent)  # '}'

    def _write(self, data: bytes | str, raw: bool = False) -> None:
        if isinstance(data, str):
            data = data.encode("UTF-8")

        # Transparently indent at the beginning of lines, but only if we're not
        # writing a newline anyway.
        if not data.startswith(self.NL) and self._write_indent():
            # We just indented. Don't write any additional whitespace at the
            # beginning now. Such whitespace might exist from spacing that
            # would result without the presence of interrupting comments.
            data = data.lstrip()

        self.ostream.write(data, self, raw)

    def _write_indent(self) -> bool:
        if self.ostream.get_column() == 0:
            self.ostream.write_tab_indent(self)
            self.ostream.write_space_align(self)
            return True
        return False

    def _write_sp(self, num: int = 1) -> None:
        self._write(b" " * num)

    def _write_nl(
        self, num: int = 1, force: bool = False, is_midline: bool = False
    ) -> None:
        # It's rare that we really want to write newlines multiple times in
        # a row. If we just wrote one, don't do so again unless forced.
        # Still adjust space-alignment mode for the next write, though.
        if self.ostream.get_column() == 0 and not force:
            self.ostream.use_space_align(is_midline)
            return

        # Set space alignment BEFORE writing newline for midline breaks.
        # This ensures _align_column is preserved during flush.
        if is_midline:
            self.ostream.use_space_align(True)

        self._write(self.NL * num)

        # For non-midline breaks, set after write to allow canceling
        # the effect upon a second NL.
        if not is_midline:
            self.ostream.use_space_align(False)

    def _children_remaining(self) -> int:
        """Returns number of children of this node not yet visited."""
        return len(self.node.nonerr_children[self._cidx :])

    def _get_child(self, offset: int = 0, absolute: bool = False) -> Node | None:
        """Accessor for child nodes, without adjusting the offset index.

        Without additional options, it returns the current child Node instance,
        ignoring any comment or other CST nodes. When using the offset argument,
        returns children before/after the current child. When absolute is True,
        ignores the current child index and uses absolute indexing, starting
        from 0.

        When the resulting index isn't valid, returns None.
        """
        cidx = 0 if absolute else self._cidx

        try:
            return self.node.nonerr_children[cidx + offset]
        except IndexError:
            return None

    def _get_child_type(self, offset: int = 0, absolute: bool = False) -> str | None:
        """Like _get_child(), but returns the TS type string ("decl", "stmt", etc).

        The returned type might refer to a named node or a literal token. Use
        _get_child_name() or _get_child_token() when possible, to avoid
        confusion between named and token nodes.

        Returns None when no matching node exists.
        """
        if child := self._get_child(offset, absolute):
            return child.type

        return None

    def _get_child_name(self, offset: int = 0, absolute: bool = False) -> str | None:
        """Like _get_child_type(), but for named nodes.

        Returns None of the child isn't a named node or no matching node exists.
        """
        if child := self._get_child(offset, absolute):
            return child.name()

        return None

    def _get_child_token(self, offset: int = 0, absolute: bool = False) -> str | None:
        """Like _get_child_type(), but for terminal nodes.

        Returns None of the child doesn't represent a plain token or no matching
        node exists.
        """
        if child := self._get_child(offset, absolute):
            return child.token()

        return None

    @staticmethod
    def register(symbol_name: str, klass: type[Formatter]) -> None:
        return MAP.register(symbol_name, klass)

    @staticmethod
    def lookup(node: Node) -> type[Formatter]:
        """Formatter lookup for a zeekscript.Node, based on its type information."""
        # If we're looking up a token node, always use the fallback formatter,
        # which writes it out directly.  This ensures that we don't confuse a
        # node.type of the same name, e.g. a variable called 'decl'.
        if not node.is_named or node.type is None:
            return Formatter
        return MAP.get(node.type)


class NullFormatter(Formatter):
    """The null formatter doesn't output anything."""

    def format(self) -> None:
        pass


class ErrorFormatter(Formatter):
    """This formatter handles parser errors reported by TreeSitter.

    This is pretty tricky. Since we cannot really know what triggered the error,
    this formatter preserves the node's byte range unmodified, expect for any
    ranges covered by child nodes with their own nontrivial formatting (which
    currently means child nodes that have children). This suits the behavior of
    error nodes, which often still have functional child nodes, but surround
    them with broken content.

    If we rendered error nodes more simply, we'd either miss out on possible
    formatting, introduce formatting that makes unpleasing changes (simple
    space-separation will often look wrong, for example), or accidentally run
    content together that the parser needs to remain whitespace-separated to
    parse correctly.

    To avoid merging the output directly with surrounding content (potentially
    breaking subsequent script-parsing), this formatter adds a surrounding space
    before and after the output. This could be optimized later via the notion of
    an optional space that gets ignored when neighbored by other whitespace.
    """

    def format(self) -> None:
        if not self.node.children:
            content = self.script.get_content(*self.node.script_range())
            self._write_sp()
            self._write(content, raw=True)
            self._write_sp()
            return

        start, _ = self.node.script_range()
        _, end = self.node.script_range(with_cst=True)

        # Before and after the error node's script range there may be
        # non-newline whitespace skipped by the parser. It's tempting to tuck
        # that onto the output, to preserve it. But, since formatting might
        # itself tuck on whitespace, this can lead to repeated formatting
        # continuously adding whitespace. So we ignore such space.

        self._write_sp()

        for node in self.node.children:
            node_start, node_end = node.script_range()
            if node.children:
                if node_start > start:
                    content = self.script.get_content(start, node_start)
                    self._write(content, raw=True)

                self._format_child(child=node)
                start = node_end

        if start < end:
            content = self.script.get_content(start, end)
            self._write(content, raw=True)

        self._write_sp()


class LineFormatter(Formatter):
    """This formatter separates all nodes with space and terminates with a newline."""

    def format(self) -> None:
        if self.node.children:
            self._format_children(b" ")
            self._write_nl()
        else:
            self._format_token()


class SpaceSeparatedFormatter(Formatter):
    """This formatter simply separates all nodes with a space."""

    def format(self) -> None:
        if self.node.children:
            self._format_children(b" ")
        else:
            self._format_token()


class PreprocDirectiveFormatter(LineFormatter):
    """@if and friends don't get indented or line-broken.

    For @if/@ifdef/@ifndef at top level, we indent content inside the block.
    This is done by tracking preproc depth in OutputStream.
    """

    def format(self) -> None:
        # Check what kind of directive this is
        first_token = self._get_child_token()
        is_block_start = first_token in ("@if", "@ifdef", "@ifndef")
        is_block_end = first_token == "@endif"
        is_block_else = first_token == "@else"

        # Exit preproc block BEFORE formatting @endif/@else (so they're not indented)
        if is_block_end or is_block_else:
            self.ostream.exit_preproc_block()

        self.ostream.use_tab_indent(False)
        self.ostream.use_linebreaks(False)
        super().format()
        self.ostream.use_tab_indent(True)
        self.ostream.use_linebreaks(True)

        # Enter preproc block AFTER formatting @if/@else (so content is indented)
        if is_block_start or is_block_else:
            self.ostream.enter_preproc_block()


class PragmaFormatter(LineFormatter):
    def format(self) -> None:
        self._format_token()


class ModuleDeclFormatter(Formatter):
    def format(self) -> None:
        self._format_child()  # 'module'
        self._write_sp()
        self._format_child_range(2)  # <name> ';'
        self._write_nl()


class ExportDeclFormatter(Formatter):
    def format(self) -> None:
        # No Whitesmith here: "{" on same line, closing "}" unindented.
        self._format_child()  # 'export'
        self._write_sp()
        self._format_child(hints=Hint.NO_LB_BEFORE)  # '{'
        self._write_nl()
        while self._get_child_name() == "decl":
            self._format_child(indent=True)
        self._format_child()  # '}'
        self._write_nl()


class TypedInitializerFormatter(Formatter):
    """Helper for common construct that's not a separate symbol in the grammar:
    [:<type>] [<initializer] [attributes]
    """

    def _format_typed_initializer(self, align_col: int = 0) -> None:
        # Track whether there's an explicit type - if so, don't transform {..} to set()/table()
        has_explicit_type = self._get_child_token() == ":"

        # Set alignment for continuation lines (one space past identifier)
        if align_col > 0:
            self.ostream.set_align_column(align_col)

        if has_explicit_type:
            self._format_child()  # ':'
            self._write_sp()
            self._format_child(hints=Hint.GOOD_AFTER_LB)  # <type> - preferred break point

        if self._get_child_name() == "initializer":
            self._write_sp()
            # Only transform {..} to set()/table() when type can be inferred (no explicit type)
            init_hints = Hint.BRACE_TO_CONSTRUCTOR if not has_explicit_type else Hint.NONE
            init_hints |= Hint.GOOD_AFTER_LB  # Also a preferred break point
            self._format_child(hints=init_hints)  # <initializer>

        if self._get_child_name() == "attr_list":
            self._write_sp()
            self._format_child()

        # Reset alignment
        if align_col > 0:
            self.ostream.set_align_column(0)


class GlobalDeclFormatter(TypedInitializerFormatter):
    """A formatter for the global-like symbols (global, option, const, simple
    value redefs), which all layout similarly.
    """

    def format(self) -> None:
        self._format_child()  # "global", "option", etc
        self._write_sp()
        # Capture column where identifier starts for alignment
        id_col = self.ostream.get_visual_column()
        self._format_child()  # <id>
        self._format_typed_initializer(align_col=id_col + 1)
        self._format_child(hints=Hint.NO_LB_BEFORE)  # ';'
        self._write_nl()


class FormatterProtocol(Protocol):
    """Helper class for type checking defining Formatter interface accessible to mixins."""

    @property
    def node(self) -> Node: ...

    def _format_child(self, child: Node | None, indent: int, hints: Hint) -> None: ...

    def _write_nl(self, num: int, force: bool, is_midline: bool) -> None: ...

    def _write_sp(self, num: int = 1) -> None: ...


class HasCommentsProtocol(Protocol):
    def has_comments(self) -> bool: ...


class ComplexSequenceFormatterMixinProtocol(
    HasCommentsProtocol, FormatterProtocol, Protocol
):
    pass


class ComplexSequenceFormatterMixin(HasCommentsProtocol):
    """A mixin providing has_comments() for line-break decisions.

    When comments are present in a construct, we use multi-line layout to
    preserve them. The formatting of these lines doesn't happen in this
    mixin, only the comment detection.
    """

    def has_comments(self: FormatterProtocol) -> bool:
        """Check if this node contains comments."""
        for child, _ in self.node.traverse(include_cst=True):
            if child == self.node:
                continue
            if child.is_comment():
                return True
        return False


class InitializerFormatter(Formatter):
    def format(self) -> None:
        self._format_child()  # '=', '+=', etc
        self._write_sp()
        # Forward hints (like BRACE_TO_CONSTRUCTOR) from parent to expr
        self._format_child(hints=self.hints)  # <expr>


class RedefEnumDeclFormatter(Formatter, ComplexSequenceFormatterMixin):
    def format(self) -> None:
        self._format_child()  # 'redef'
        self._write_sp()
        self._format_child()  # 'enum'
        self._write_sp()
        self._format_child()  # <id>
        self._write_sp()
        self._format_child()  # '+='
        self._write_sp()

        # Format the body - linebreak if comments present (checked before consuming '{')
        do_linebreak = self.has_comments()

        self._format_child()  # '{'

        # Multi-element redefs should linebreak as well
        if child := self._get_child():
            do_linebreak |= len(child.children) > 1

        if do_linebreak:
            self._write_nl()
            self._format_child(indent=True, hints=Hint.COMPLEX_BLOCK)  # enum_body
            self._write_nl()
        else:
            self._write_sp()
            self._format_child(indent=True)  # enum_body
            self._write_sp()

        self._format_child()  # '}'

        self._format_child(hints=Hint.NO_LB_BEFORE)  # ';'
        self._write_nl()


class RedefRecordDeclFormatter(Formatter):
    def format(self) -> None:
        self._format_child()  # 'redef'
        self._write_sp()
        self._format_child()  # 'record'
        self._write_sp()

        # We could either be change fields in a type, or change attributes on a
        # field. In the first case we would hold an id here, else and expr.
        is_redef_attr = self._get_child_name() == "expr"
        self._format_child()  # <id>/<expr>

        self._write_sp()
        self._format_child()  # '+='/'-='
        self._write_sp()

        if is_redef_attr:
            self._format_children(sep=" ")
        else:
            self._format_child()  # '{'
            self._write_nl()
            while self._get_child_name() == "type_spec":
                self._format_child(indent=True)
            self._format_child()  # '}'
            if self._get_child_name() == "attr_list":
                self._write_sp()
                self._format_child()  # <attr_list>
            self._format_child(hints=Hint.NO_LB_BEFORE)  # ';'
        self._write_nl()


class TypeDeclFormatter(Formatter):
    def format(self) -> None:
        self._format_child()  # 'type'
        self._write_sp()
        self._format_child_range(2)  # <id> ':'
        self._write_sp()
        self._format_child()  # <type>
        if self._get_child_name() == "attr_list":
            self._write_sp()
            self._format_child()  # <attr_list>
        self._format_child(hints=Hint.NO_LB_BEFORE)  # ';'
        self._write_nl()


class TypeFormatter(SpaceSeparatedFormatter, ComplexSequenceFormatterMixin):
    def format(self) -> None:
        if self._get_child_token() == "set":
            self._format_child(hints=Hint.NO_LB_AFTER | self.hints)  # 'set'
            self._format_typelist()  # '[' ... ']'

        elif self._get_child_token() == "table":
            self._format_child(hints=Hint.NO_LB_AFTER | self.hints)  # 'table'
            self._format_typelist()  # '[' ... ']'
            self._write_sp()
            self._format_child()  # 'of'
            self._write_sp()
            self._format_child()  # <type>

        elif self._get_child_token() == "record":
            # No Whitesmith here: "{" on same line, closing "}" unindented.
            self._format_child()  # 'record',
            self._write_sp()
            self._format_child()  # '{'

            if self._get_child_name() == "type_spec":  # any number of type_specs
                self._write_nl()
                while self._get_child_name() == "type_spec":
                    self._format_child(indent=True)
            else:
                self._write_sp()  # empty record, keep on one line

            self._format_child()  # '}'

        elif self._get_child_token() == "enum":
            # No Whitesmith here: "{" on same line, closing "}" unindented.
            # Check for comments before consuming '{' so has_comments sees full content
            do_linebreak = self.has_comments()
            self._format_child()  # 'enum'
            self._write_sp()
            self._format_child()  # '{'

            if do_linebreak:
                self._write_nl()
                self._format_child(indent=True, hints=Hint.COMPLEX_BLOCK)  # enum_body
                self._write_nl()
            else:
                self._write_sp()
                # Align wrapped enum values to column after '{ '
                saved_align = self.ostream.get_align_column()
                self.ostream.set_align_column(self.ostream.get_visual_column())
                self._format_child(indent=True)  # enum_body
                self.ostream.set_align_column(saved_align)
                self._write_sp()

            self._format_child()  # '}'

        elif self._get_child_token() == "function":
            self._format_child_range(2, hints=self.hints)  # 'function' <func_params>

        elif self._get_child_token() in ["event", "hook"]:
            self._format_child(hints=self.hints)  # 'event'/'hook' - propagate incoming hints
            self._format_child(hints=Hint.NO_LB_BEFORE)  # '('
            # Align wrapped parameters to the column after the '('
            saved_align = self.ostream.get_align_column()
            self.ostream.set_align_column(self.ostream.get_visual_column())
            if self._get_child_name() == "formal_args":
                self._format_child()
            self._format_child(hints=Hint.NO_LB_BEFORE)  # ')'
            self.ostream.set_align_column(saved_align)

        else:
            # Format anything else with plain space separation, e.g. "vector of foo"
            super().format()

    def _format_typelist(self) -> None:
        # Prevent line breaks inside type brackets - keep table[...] and set[...] intact
        self._format_child(hints=Hint.NO_LB_BEFORE | Hint.NO_LB_AFTER)  # '['
        while self._get_child_name() == "type":
            self._format_child(hints=Hint.NO_LB_AFTER)  # <type>
            if self._get_child_token() == ",":
                self._format_child(hints=Hint.NO_LB_BEFORE | Hint.NO_LB_AFTER)  # ','
                self._write_sp()
        self._format_child(hints=Hint.NO_LB_BEFORE | Hint.NO_LB_AFTER)  # ']'


class TypeSpecFormatter(Formatter):
    def format(self) -> None:
        self._format_child(hints=Hint.NO_LB_AFTER)  # <id>
        self._format_child(hints=Hint.NO_LB_AFTER)  # ':'
        self._write_sp()
        self._format_child()  # <type>
        if self._get_child_name() == "attr_list":
            self._write_sp()
            # Set alignment for attr_list so line breaks align properly
            saved_align = self.ostream.get_align_column()
            self.ostream.set_align_column(self.ostream.get_visual_column())
            self._format_child()
            self.ostream.set_align_column(saved_align)
        self._format_child(hints=Hint.NO_LB_BEFORE)  # ';'
        self._write_nl()


class EnumBodyFormatter(Formatter):
    def format(self) -> None:
        if Hint.COMPLEX_BLOCK in self.hints:
            # Treat this as a "complex": break every value onto a new line.
            while self._get_child():
                self._format_child()  # enum_body_elem
                # ',' is optional at the end of the list:
                if self._get_child():
                    self._format_child(hints=Hint.NO_LB_BEFORE)
                self._write_nl()
        else:
            # Keep on a single line. We may still linewrap later.
            while self._get_child():
                self._format_child()  # enum_body_elem
                # ',' is optional at the end of the list:
                if self._get_child():
                    self._format_child(hints=Hint.NO_LB_BEFORE)
                if self._get_child():
                    self._write_sp()


class FuncDeclFormatter(Formatter):
    def format(self) -> None:
        self._format_child()  # <func_hdr>
        if self._get_child_name() == "preproc_directive":
            self._write_nl()
            while self._get_child_name() == "preproc_directive":
                self._format_child()  # <preproc_directive>
                self._write_nl()
        # This newline produces K&R style functions/events/hooks:
        self._write_nl()
        self._format_child()  # <func_body>
        self._write_nl()


class FuncHdrFormatter(Formatter):
    def format(self) -> None:
        self._format_child()  # <func>, <hook>, or <event>


class FuncHdrVariantFormatter(Formatter):
    def format(self) -> None:
        if self._get_child_token() == "redef":
            self._format_child()  # 'redef'
            self._write_sp()
        self._format_child()  # 'function', 'hook', or 'event'
        self._write_sp()
        self._format_child()  # <id>
        self._format_child()  # <func_params>
        if self._get_child_name() == "attr_list":
            # If the unbroken header already exceeds 80 columns, the params
            # will wrap.  Put the attr_list on its own continuation line so
            # it aligns with the first parameter instead of being tacked on
            # after the closing paren.
            if self.ostream.get_visual_column() > self.ostream.MAX_LINE_LEN:
                # If alignment + attr_list width would exceed 80, reduce
                # alignment so the attr_list ends near column 78.
                align = self.ostream.get_align_column()
                attr_node = self._get_child()
                attr_width = len(
                    self.script.get_content(*attr_node.script_range()).split(b"\n")[0]
                )
                if align + attr_width > self.ostream.MAX_LINE_LEN:
                    target_end = self.ostream.MAX_LINE_LEN - 2
                    tab_col = self.ostream._tab_indent * self.ostream.TAB_SIZE
                    new_align = max(tab_col + self.ostream.SPACE_INDENT,
                                   target_end - attr_width)
                    self.ostream.set_align_column(new_align)
                self._write_nl(is_midline=True)
            else:
                self._write_sp()
            # Mark attr_list as preferred break point - if line needs wrapping,
            # prefer breaking before the attribute over breaking inside arguments.
            # Alignment column is still set from func_params, so attr_list aligns
            # with the first parameter.
            self._format_child(hints=Hint.GOOD_AFTER_LB)  # <attr_list>
        # Clear alignment now that func header (including any attr_list) is done
        self.ostream.set_align_column(0)


class FuncParamsFormatter(Formatter):
    def format(self) -> None:
        self._format_child(hints=Hint.NO_LB_BEFORE)  # '('
        # Align wrapped arguments to the column after the '('
        self.ostream.set_align_column(self.ostream.get_visual_column())
        if self._get_child_name() == "formal_args":
            self._format_child()  # <formal_args>
        self._format_child(hints=Hint.NO_LB_BEFORE)  # ')'
        # Don't clear alignment here - FuncHdrVariantFormatter needs it for
        # trailing attr_list alignment. It will clear alignment after attr_list.
        if self._get_child_token() == ":":
            self._format_child(hints=Hint.NO_LB_AFTER)  # ':'
            self._write_sp()
            self._format_child()  # <type>


class FuncBodyFormatter(Formatter):
    def format(self) -> None:
        self._write_nl()
        self._format_curly_statement_list()


class FormalArgsFormatter(Formatter):
    def format(self) -> None:
        while self._get_child_name() == "formal_arg":
            self._format_child()  # <formal_arg>
            if self._get_child():
                self._format_child(hints=Hint.NO_LB_BEFORE)  # ',' or ';'
                self._write_sp()


class FormalArgFormatter(Formatter):
    def format(self) -> None:
        self._format_child(hints=Hint.NO_LB_AFTER)  # <id>
        self._format_child(hints=Hint.NO_LB_AFTER)  # ':'
        self._write_sp()
        self._format_child()  # <type>
        if self._get_child_name() == "attr_list":
            self._write_sp()
            self._format_child()  # <attr_list>


class IndexSliceFormatter(Formatter):
    def format(self) -> None:
        # If any of the limits is a compound node and not a literal, constant,
        # or empty, surround the `:` of the slice with spaces. This mirrors
        # black's style for Python.
        use_space_around_colon = any(
            map(lambda x: len(x.children) > 1, self.node.children)
        )

        self._format_child(hints=Hint.NO_LB_BEFORE)  # '['

        while self._get_child_token() != "]":
            self._format_child()

            # Do not add extra space after end index.
            if self._get_child_token() != "]" and use_space_around_colon:
                self._write_sp()

        self._format_child(hints=Hint.NO_LB_BEFORE)  # ']'


class CaptureListFormatter(Formatter):
    def format(self) -> None:
        self._format_child(hints=Hint.NO_LB_BEFORE)  # '['
        while self._get_child_name() == "capture":
            self._format_child()  # <capture>
            if self._get_child_token() == ",":
                self._format_child(hints=Hint.NO_LB_BEFORE)  # ','
                self._write_sp()
        self._format_child(hints=Hint.NO_LB_BEFORE)  # ']'


class StmtFormatter(TypedInitializerFormatter):
    def _format_stmt_block(self) -> None:
        """Helper for formatting a block of statements.

        This may either be an { ... } block or a single-line statement.
        """
        self._write_nl()
        self._format_child(indent=True)  # <stmt>
        self._write_nl()

    def _is_compact_if(self, node: Node) -> bool:
        """Check if node is an if statement with body on the same line.

        Returns True if:
        - Node is an if statement (not if-else)
        - Body is not a {}-block
        - Original source has no newline between ) and the body
        """
        if node.name() != "stmt" or not node.nonerr_children:
            return False
        if node.nonerr_children[0].token() != "if":
            return False
        # Check for else clause - if present, not compact
        if len(node.nonerr_children) > 5:  # if ( expr ) stmt else ...
            return False
        # Check if body is a {}-block
        body = node.nonerr_children[4]
        if body.nonerr_children and body.nonerr_children[0].token() == "{":
            return False
        # Check for newline between ) and body in the source
        close_paren = node.nonerr_children[3]
        between = self.script.get_content(close_paren.end_byte, body.start_byte)
        return b"\n" not in between

    def _has_adjacent_compact_if(self, node: Node) -> bool:
        """Check if node has an adjacent sibling that is also a compact if."""
        if node.prev_sibling and self._is_compact_if(node.prev_sibling):
            return True
        if node.next_sibling and self._is_compact_if(node.next_sibling):
            return True
        return False

    def _format_when(self) -> None:
        self._format_child()  # 'when'
        self._write_sp()
        if self._get_child_name() == "capture_list":
            self._format_child()  # <capture_list>
            self._write_sp()
        self._format_child(hints=Hint.NO_LB_BEFORE)  # '('
        self._write_sp()
        self._format_child()  # <expr>
        self._write_sp()
        self._format_child(hints=Hint.NO_LB_BEFORE)  # ')'
        self._format_stmt_block()

        if self._get_child_token() == "timeout":
            self._format_child()  # 'timeout'
            self._write_sp()
            self._format_child()  # <expr>
            self._write_nl()
            self._format_curly_statement_list()  # '{' <stmt_list> '}'
            self._write_nl()

    def format(self) -> None:
        # Statements aren't currently broken down into more specific symbol
        # types in the grammar, so we just examine their beginning.
        start_name, start_token = self._get_child_name(), self._get_child_token()

        if start_token == "{":
            # We don't have to do anything re. Whitesmith here: if this needs
            # to be indented, the caller has already ensured so via indent=True.
            self._format_curly_statement_list(indent=False)  # '{' <stmt_list> '}'

        elif start_token in ["print", "event"]:
            self._format_child()  # 'print'/'event'
            self._write_sp()
            # Align wrapped arguments to column after 'print '/'event '
            saved_align = self.ostream.get_align_column()
            self.ostream.set_align_column(self.ostream.get_visual_column())
            self._format_child_range(2)  # <expr_list>/<event_hdr> ';'
            self.ostream.set_align_column(saved_align)
            self._write_nl()

        elif start_token == "if":
            self._format_child()  # 'if'
            self._write_sp()
            self._format_child(hints=Hint.NO_LB_BEFORE)  # '('
            self._write_sp()
            # Align wrapped conditionals to after '( '
            saved_align = self.ostream.get_align_column()
            self.ostream.set_align_column(self.ostream.get_visual_column())
            self._format_child()  # <expr>
            self.ostream.set_align_column(saved_align)
            self._write_sp()
            self._format_child(hints=Hint.NO_LB_BEFORE)  # ')'

            # Our if-statement layout is either
            #
            #   if ( foo )
            #           bar();
            #   ...
            #
            # or
            #
            #   if ( foo )
            #           {
            #           bar();
            #           }
            #   ...
            #
            # We need to establish whether the subsequent statement is a
            # {}-block: if it's not, we write a newline and need to indent,
            # because {}-blocks take care of indentation as another statement
            # type (higher up in this function).
            #
            # Exception: preserve compact if-statement style when:
            # 1. Original source has body on same line as condition
            # 2. There's at least one adjacent compact if (forms a block)
            # This allows switch-case-like patterns to stay compact.

            if (
                self._is_compact_if(self.node)
                and self._has_adjacent_compact_if(self.node)
            ):
                self._write_sp()
                self._format_child()  # <stmt> on same line
            else:
                self._format_stmt_block()

            # An else-block also requires special treatment
            if self._get_child_token() == "else":
                self._format_child()  # 'else'

                # Special treatment of "else if": we keep those on the same
                # line, since otherwise, a switch-case-like cascade of if-else
                # would get progressively more indented.
                if child := self._get_child():
                    if child.has_property(
                        lambda n: n.nonerr_children[0].token() == "if"
                    ):
                        self._write_sp()
                        self._format_child()  # <stmt>
                    else:
                        self._format_stmt_block()
                else:
                    self._format_stmt_block()

        elif start_token == "switch":
            self._format_child()  # 'switch'
            self._write_sp()
            self._format_child(hints=Hint.SPACED_PARENS)  # <expr>
            self._write_nl()
            self._format_child(indent=True)  # '{'

            # Shorten braces to "{ }" if there is at most whitespace between them.
            child = self._get_child()
            if (
                self._get_child_token() == "}"
                and child
                and child.has_only_whitespace_before()
            ):
                self._write_sp()
                self._format_child()  # '}'
            else:
                if self._get_child_name() == "case_list":
                    self._write_nl()
                    self._format_child(indent=True)  # <case_list>
                self._write_nl()
                self._format_child(indent=True)  # '}'
            self._write_nl()

        elif start_token == "for":
            self._format_child()  # 'for'
            self._write_sp()
            self._format_child(hints=Hint.NO_LB_BEFORE)  # '('
            self._write_sp()
            # Align wrapped for-loop content to column after '( '
            saved_align = self.ostream.get_align_column()
            self.ostream.set_align_column(self.ostream.get_visual_column())
            if self._get_child_token() == "[":
                self._format_child(hints=Hint.NO_LB_BEFORE)  # '['
                while self._get_child_token() != "]":
                    self._format_child()  # <id>
                    if self._get_child_token() == ",":
                        self._format_child(hints=Hint.NO_LB_BEFORE)  # ','
                        self._write_sp()
                self._format_child(hints=Hint.NO_LB_BEFORE)  # ']'
            else:
                self._format_child()  # <id>

            while self._get_child_token() == ",":
                self._format_child(hints=Hint.NO_LB_BEFORE)  # ','
                self._write_sp()
                self._format_child()  # <id>
            self._write_sp()
            self._format_child()  # 'in'
            self._write_sp()
            self._format_child()  # <expr>
            self.ostream.set_align_column(saved_align)
            self._write_sp()
            self._format_child(hints=Hint.NO_LB_BEFORE)  # ')'
            self._format_stmt_block()  # <stmt>

        elif start_token == "while":
            self._format_child()  # 'while'
            self._write_sp()
            self._format_child(hints=Hint.NO_LB_BEFORE)  # '('
            self._write_sp()
            # Align wrapped conditionals to after '( '
            saved_align = self.ostream.get_align_column()
            self.ostream.set_align_column(self.ostream.get_visual_column())
            self._format_child()  # <expr>
            self.ostream.set_align_column(saved_align)
            self._write_sp()
            self._format_child(hints=Hint.NO_LB_BEFORE)  # ')'
            self._format_stmt_block()  # <stmt>

        elif start_token in ["next", "break", "fallthrough"]:
            self._format_child_range(2)  # loop control statement, ';'
            self._write_nl()

        elif start_token == "return":
            self._format_child()  # 'return'
            # There's also an optional 'return" before when statements,
            # so detour in that case and be done.
            if self._get_child_token() == "when":
                self._write_sp()
                self._format_when()
                return
            if self._get_child_name() == "expr":
                self._write_sp()
                self._format_child()  # <expr>
            self._format_child(hints=Hint.NO_LB_BEFORE)  # ';'
            self._write_nl()

        elif start_token in ["add", "delete"]:
            self._format_child()  # set management
            self._write_sp()
            self._format_child_range(2)  # <expr> ';'
            self._write_nl()

        elif start_token in ["local", "const"]:
            self._format_child()  # 'local'/'const'
            self._write_sp()
            # Capture column where identifier starts for alignment
            id_col = self.ostream.get_visual_column()
            self._format_child()  # <id>
            self._format_typed_initializer(align_col=id_col + 1)
            self._format_child(hints=Hint.NO_LB_BEFORE)  # ';'
            self._write_nl()

        elif start_token == "when":
            self._format_when()

        elif start_name == "index_slice":
            self._format_child()  # <index_slice>
            self._write_sp()
            self._format_child()  # '='
            self._write_sp()
            self._format_child_range(2)  # <expr> ';'
            self._write_nl()

        elif start_name == "expr":
            self._format_child_range(2)  # <expr> ';'
            self._write_nl()

        elif start_name == "preproc_directive":
            self._format_child()  # <preproc_directive>
            self._write_nl()

        elif start_token == "assert":
            self._format_child()  # 'assert'
            self._write_sp()
            self._format_children()
            self._write_nl()

        elif start_token == ";":
            self._format_child(hints=Hint.NO_LB_BEFORE)  # ';'
            self._write_nl()


class ExprListFormatter(Formatter, ComplexSequenceFormatterMixin):
    # Minimum element count to enable lenient line length for initializer elements.
    # With fewer elements, elements will wrap normally but with tab indent.
    MIN_ELEMENTS_FOR_LENIENT_WRAP = 4
    # Minimum element count to apply any initializer element hints at all.
    # Single-element expr_lists (like print arguments) shouldn't use these hints.
    MIN_ELEMENTS_FOR_INIT_HINTS = 2

    def _should_align_record_args(self) -> bool:
        """Check if record-style args ($field=value) should each be on their own line.

        Returns True if:
        1. Arguments are record-style ($field=value)
        2. At least one argument contains a nested call (set, table, etc.) that
           is likely to wrap to multiple lines
        """
        exprs = [c for c in self.node.children if c.name() == "expr"]
        if len(exprs) < 2:
            return False

        # Check if first element is record-style ($field=...)
        first_children = list(exprs[0].nonerr_children)
        if not (first_children and first_children[0].token() and
                first_children[0].token().startswith("$")):
            return False

        # Check if any element contains a nested call (set(...), table(...), etc.)
        # that is likely to wrap
        for expr in exprs:
            for child, _ in expr.traverse():
                if child == expr:
                    continue
                # Look for function-style calls: <expr> '(' ...
                if child.name() == "expr":
                    children = list(child.nonerr_children)
                    if len(children) >= 2 and children[1].token() == "(":
                        # Check if call has multiple arguments (likely to wrap)
                        for c in children:
                            if c.name() == "expr_list":
                                args = [x for x in c.children if x.name() == "expr"]
                                if len(args) >= 3:
                                    return True
        return False

    def format(self) -> None:
        # Only use one-per-line formatting when explicitly requested via COMPLEX_BLOCK
        # (for constructors with comments or that exceed line length).
        # Function calls should use normal wrapping.
        if Hint.COMPLEX_BLOCK in self.hints:
            # Count elements to decide hint behavior:
            # - With many elements (>=4): suppress wrapping (INIT_LENIENT) + tab indent
            # - With few elements (2-3): normal wrapping + tab indent (INIT_ELEMENT)
            # - With single element: no special hints (normal wrapping + space indent)
            num_elements = sum(1 for c in self.node.children if c.name() == "expr")
            use_init_hints = num_elements >= self.MIN_ELEMENTS_FOR_INIT_HINTS
            use_lenient_length = num_elements >= self.MIN_ELEMENTS_FOR_LENIENT_WRAP

            while self._get_child_name() == "expr":
                # INIT_ELEMENT: set for tab indent on wrap (only if multiple elements)
                # INIT_LENIENT: set for lenient line length (only if many elements)
                if use_init_hints:
                    if use_lenient_length:
                        self.ostream.set_hints(Hint.INIT_ELEMENT | Hint.INIT_LENIENT)
                    else:
                        self.ostream.set_hints(Hint.INIT_ELEMENT)
                self._format_child(indent=True)  # <expr>
                self.ostream.set_hints(None)
                if self._get_child():
                    self._format_child(hints=Hint.NO_LB_BEFORE)  # ','
                    self._write_nl()
        else:
            # Check if this is record-style arguments ($field=value) with nested calls
            # that are likely to wrap. If so, put each argument on its own aligned line.
            force_align_args = self._should_align_record_args()
            align_col = self.ostream.get_align_column() if force_align_args else 0

            while self._get_child_name() == "expr":
                self._format_child()  # <expr>
                if self._get_child():
                    self._format_child(hints=Hint.NO_LB_BEFORE)  # ','
                    if force_align_args and align_col > 0:
                        # Write newline with alignment to first argument
                        tab_col = self.indent * 8
                        space_count = max(0, align_col - tab_col)
                        self._write(self.NL + b"\t" * self.indent + b" " * space_count)
                    else:
                        self._write_sp()


class CaseListFormatter(Formatter):
    def format(self) -> None:
        while self._get_child():
            if self._get_child_token() == "case":
                self._format_child()  # 'case'
                self._write_sp()
                # Align wrapped case values to column after 'case '
                saved_align = self.ostream.get_align_column()
                self.ostream.set_align_column(self.ostream.get_visual_column())
                self._format_child_range(2)  # <expr_list> or <case_type_list>, ':'
                self.ostream.set_align_column(saved_align)
            else:
                self._format_child_range(2)  # 'default' ':'
            self._write_nl()
            if self._get_child_name() == "stmt_list":
                self._format_child(indent=True)  # <stmt_list>


class CaseTypeListFormatter(Formatter):
    def format(self) -> None:
        while self._get_child_token() == "type":
            self._format_child()  # 'type'
            self._write_sp()
            self._format_child()  # <type>
            if self._get_child_token() == "as":
                self._write_sp()
                self._format_child()  # 'as'
                self._write_sp()
                self._format_child()  # <id>
            if self._get_child_token() == ",":
                self._format_child(hints=Hint.NO_LB_BEFORE)  # ','
                self._write_sp()


class EventHdrFormatter(Formatter):
    """Formats event call headers with proper alignment for wrapped arguments.

    The event_hdr node structure is: <expr> '(' [expr_list] ')'
    Align continuation lines to the column after the opening parenthesis.
    """

    def format(self) -> None:
        self._format_child()  # <expr> (event name)
        self._format_child(hints=Hint.NO_LB_BEFORE)  # '('
        # Align wrapped arguments to the column after the '('
        saved_align = self.ostream.get_align_column()
        self.ostream.set_align_column(self.ostream.get_visual_column())
        if self._get_child_name() == "expr_list":
            self._format_child()  # <expr_list>
        self._format_child(hints=Hint.NO_LB_BEFORE)  # ')'
        self.ostream.set_align_column(saved_align)


class AssertMsgFormatter(SpaceSeparatedFormatter):
    pass


class ExprFormatter(SpaceSeparatedFormatter, ComplexSequenceFormatterMixin):
    # Like statments, expressions aren't currently broken into specific symbol
    # types, so we use helpers or parse into them to identify what particular
    # kind of expression we're facing.

    def _is_binary_boolean(self) -> bool:
        """Predicate, returns true if this an || or && expression."""
        return len(self.node.nonerr_children) == 3 and self._get_child_token(
            offset=1, absolute=True
        ) in ("||", "&&")

    def _is_inside_binary_boolean(self) -> bool:
        """Returns true if any parent expression is a boolean (|| or &&)."""
        node: Node | None = self.node.parent
        while node:
            if isinstance(node.formatter, ExprFormatter) and node.formatter._is_binary_boolean():
                return True
            if not isinstance(node.formatter, ExprFormatter):
                break
            node = node.parent
        return False

    def _is_binary_operator(self) -> bool:
        """Predicate, returns true if this is a binary operator expression.

        Includes arithmetic, comparison, bitwise, pattern match, and membership operators.
        Boolean (&&, ||) and !in are handled separately.
        """
        return len(self.node.nonerr_children) == 3 and self._get_child_token(
            offset=1, absolute=True
        ) in (
            # Arithmetic
            "/", "*", "+", "-", "%",
            # Comparison
            "==", "!=", "<", ">", "<=", ">=",
            # Bitwise
            "&", "|", "^",
            # Pattern match
            "~", "!~",
            # Membership
            "in",
        )

    def _is_assignment(self) -> bool:
        """Predicate, returns true if this is an assignment expression."""
        return len(self.node.nonerr_children) == 3 and self._get_child_token(
            offset=1, absolute=True
        ) in ("=", "+=", "-=")

    def _is_ternary(self) -> bool:
        """Predicate, returns true if this is a ternary (? :) expression."""
        return (
            len(self.node.nonerr_children) == 5
            and self._get_child_token(offset=1, absolute=True) == "?"
            and self._get_child_token(offset=3, absolute=True) == ":"
        )

    def _is_string_concat(self) -> bool:
        """Predicate, returns true if this a <string> + <string> expression."""

        def is_constant_expr(node: Node) -> bool:
            return (
                node.name() == "expr"
                and len(node.nonerr_children) == 1
                and node.nonerr_children[0].name() == "constant"
                and len(node.nonerr_children[0].nonerr_children) == 1
                and node.nonerr_children[0].nonerr_children[0].name() == "string"
            )

        def is_concat_expr(node: Node) -> bool:
            return (
                node.name() == "expr"
                and len(node.nonerr_children) == 3
                and (
                    is_constant_expr(node.nonerr_children[0])
                    or is_concat_expr(node.nonerr_children[0])
                )
                and node.nonerr_children[1].token() == "+"
                and (
                    is_constant_expr(node.nonerr_children[2])
                    or is_concat_expr(node.nonerr_children[2])
                )
            )

        return is_concat_expr(self.node)

    def _is_expr_chain_of(
        self, formatter_predicate: Callable[[ExprFormatter], bool]
    ) -> bool:
        """Predicate, returns true if the given predicate is true for all
        formatters from this expression up to the first non-expression.
        This helps identify chains of similar expressions, per the above
        predicates.
        """
        node: Node | None = self.node

        while (
            node
            and isinstance(node.formatter, ExprFormatter)
            and formatter_predicate(node.formatter)
        ):
            node = node.parent

        return (node is not None) and not isinstance(node.formatter, ExprFormatter)

    def _is_brace_init_table(self) -> bool:
        """Returns True if this {..} expression is a table initializer.

        Table initializers contain entries with assignment patterns like
        [idx] = val. Set initializers are just values without assignments.
        Returns False if we can't determine or if it's a set.
        """
        # Find the expr_list child (should be at index 1 after '{')
        if len(self.node.nonerr_children) < 2:
            return False

        expr_list = self.node.nonerr_children[1]
        if expr_list.name() != "expr_list":
            return False

        # Check if any expr child has an '=' assignment pattern
        # Table entries have patterns like: [idx] = val  or  idx = val
        for child in expr_list.nonerr_children:
            if child.name() != "expr":
                continue
            # Look for '=' token in the expr's children
            for i, subchild in enumerate(child.nonerr_children):
                if subchild.token() == "=":
                    return True

        return False

    def _format_record_fields(self, one_per_line: bool) -> None:
        """Format record constructor fields with alignment.

        Args:
            one_per_line: If True, respect source line breaks but fit multiple
                fields per line when they fit within 80 columns. If False,
                allow natural line wrapping.
        """
        expr_list = self._get_child()
        if not expr_list:
            return

        # Get alignment column (should be set to after '[' by caller)
        align_col = self.ostream.get_align_column()
        tab_col = self.indent * 8  # TAB_SIZE = 8
        max_col = 80

        # Collect fields and their associated comments
        fields = []  # List of (expr_node, comment_text_or_None)
        children = list(expr_list.nonerr_children)
        i = 0
        while i < len(children):
            child = children[i]
            if child.name() == "expr":
                fields.append((child, None))
                i += 1
            elif child.token() == ",":
                # Check for attached comment
                comment = None
                for sib in child.next_cst_siblings:
                    if sib.is_comment():
                        comment = self.script.get_content(*sib.script_range())
                        break
                    elif not sib.is_nl():
                        break
                if fields:
                    # Attach comment to previous field
                    prev_expr, _ = fields[-1]
                    fields[-1] = (prev_expr, comment)
                i += 1
            else:
                i += 1

        # Format fields, fitting multiple per line when possible
        first = True
        for idx, (expr_node, comment) in enumerate(fields):
            is_last = idx == len(fields) - 1

            if not first:
                # Estimate field length to decide if it fits on current line
                field_src = self.script.get_content(*expr_node.script_range())
                field_len = len(field_src) + 2  # +2 for ", "
                if comment:
                    field_len += len(comment) + 1  # +1 for space before comment

                current_col = self.ostream.get_visual_column()
                fits = current_col + field_len <= max_col

                if one_per_line and not fits:
                    # Start new line with alignment
                    space_count = max(0, align_col - tab_col)
                    indent_str = self.NL + b"\t" * self.indent + b" " * space_count
                    self.ostream.write(indent_str, self)
                    # Re-set alignment after newline (flush clears it)
                    self.ostream.set_align_column(align_col)
                else:
                    self._write_sp()

            self._format_child(child=expr_node)

            if not is_last:
                self._write(",")
                if comment:
                    self._write_sp()
                    self._write(comment)

            first = False

        self._next_child()  # Consume the expr_list we just formatted

    def _is_record_constructor(self) -> bool:
        """Returns True if this [...] expression is a record constructor.

        Record constructors have $field=value expressions inside.
        Vector literals have regular expressions without leading $.
        """
        # Find the expr_list child (should be at index 1 after '[')
        if len(self.node.nonerr_children) < 2:
            return False

        expr_list = self.node.nonerr_children[1]
        if expr_list.name() != "expr_list":
            return False

        # Check if first expr starts with $ (record field assignment)
        for child in expr_list.nonerr_children:
            if child.name() == "expr" and child.nonerr_children:
                first_token = child.nonerr_children[0].token()
                if first_token == "$":
                    return True
            break  # Only check the first expression

        return False

    def format(self) -> None:
        cn1, cn2, _ = (self._get_child_name(offset=n) for n in (0, 1, 2))
        ct1, ct2, ct3 = (self._get_child_token(offset=n) for n in (0, 1, 2))

        if cn1 == "expr" and ct2 == "[":
            self._format_child()  # <expr>
            self._format_child(hints=Hint.NO_LB_BEFORE | Hint.NO_LB_AFTER)  # '['
            self._format_child()  # <expr_list>
            self._format_child(hints=Hint.NO_LB_BEFORE)  # ']'

        elif cn1 == "expr" and ct2 == "$":
            # Never break on record $ operator - keep expr$field together
            self._format_child(hints=Hint.NO_LB_AFTER)
            self._format_child(hints=Hint.NO_LB_BEFORE | Hint.NO_LB_AFTER)
            while self._get_child():
                self._format_child(hints=Hint.NO_LB_BEFORE)

        elif cn1 == "expr" and cn2 == "index_slice":
            while self._get_child():
                self._format_child()

        elif ct1 == "!":
            # Negation looks better when spaced apart
            # Propagate GOOD_AFTER_LB so line-breaker prefers breaking before this expr
            first_hints = Hint.NO_LB_AFTER | (self.hints & Hint.GOOD_AFTER_LB)
            self._format_child(hints=first_hints)
            self._write_sp()
            self._format_child()

        elif ct1 in ["|", "++", "--", "~", "-", "+"]:
            # No space when those operators are involved
            # Propagate GOOD_AFTER_LB so line-breaker prefers breaking before this expr
            first_hints = Hint.NO_LB_AFTER | (self.hints & Hint.GOOD_AFTER_LB)
            self._format_child(hints=first_hints)
            while self._get_child():
                self._format_child()

        elif cn1 == "expr" and ct2 == "!" and ct3 == "in":
            # '!in' binary operator - handle like other binary operators
            saved_align = self.ostream.get_align_column()
            inside_boolean = self._is_inside_binary_boolean()
            if saved_align == 0:
                self.ostream.set_align_column(self.ostream.get_visual_column())
            self._format_child(hints=self.hints)  # <expr> - propagate incoming hints
            self._write_sp()
            self._format_child(hints=Hint.NO_LB_AFTER)  # '!'
            self._format_child()  # 'in'
            self._write_sp()
            hints = Hint.NONE if inside_boolean else Hint.GOOD_AFTER_LB
            self._format_child(hints=hints)  # <expr>
            if saved_align == 0:
                self.ostream.set_align_column(0)

        elif ct1 in ["{", "["]:
            # Vector/table/set initializers: '{'/'[' <expr_list> '}'/']'
            # Use multi-line if comments present or content is too long
            do_linebreak = self.has_comments()
            if not do_linebreak:
                # Estimate compact length to decide if multi-line needed
                compact_len = 0
                for child, _ in self.node.traverse(include_cst=True):
                    if child.is_nl():
                        continue
                    if not child.nonerr_children:  # leaf node
                        compact_len += child.end_byte - child.start_byte
                if compact_len > 80:
                    do_linebreak = True

            # When BRACE_TO_CONSTRUCTOR hint is set, transform { } to set( ) or table( )
            transform_brace = (
                ct1 == "{" and Hint.BRACE_TO_CONSTRUCTOR in self.hints
            )

            # Record constructors [$field=val, ...] use alignment-based formatting
            # like function calls, not one-per-line
            is_record = ct1 == "[" and self._is_record_constructor()

            if transform_brace:
                # Determine if table or set based on whether entries have = assignments
                constructor = "table" if self._is_brace_init_table() else "set"
                self._next_child()  # Skip the '{' token (don't format it)
                self._write(constructor + "(")
            else:
                self._format_child(hints=Hint.NO_LB_BEFORE)  # '{'/'['

            # Only format inner content if there's an expr_list (not empty)
            if self._get_child_name() == "expr_list":
                if is_record:
                    # Record constructor: format fields with alignment to first $field.
                    saved_align = self.ostream.get_align_column()
                    self.ostream.set_align_column(self.ostream.get_visual_column())
                    # Has comments or too long: one field per line
                    # Otherwise: allow multiple fields per line
                    self._format_record_fields(one_per_line=do_linebreak)
                    self.ostream.set_align_column(saved_align)
                elif do_linebreak:
                    self._write_nl()
                    self._format_child(hints=Hint.COMPLEX_BLOCK)  # Inner expression(s)
                    self._write_nl()
                else:
                    self._format_child()  # Inner expression(s)

            if transform_brace:
                self._next_child()  # Skip the '}' token (don't format it)
                self._write(")")
            else:
                self._format_child()  # '}'/']'

        elif ct1 == "(":
            # Propagate GOOD_AFTER_LB (if set on this expression) to the '(' token
            # so the line-breaker knows to prefer breaking before it. This keeps
            # operators like || and + at the end of line 1. When GOOD_AFTER_LB is
            # set, don't add NO_LB_BEFORE since that would prevent the desired break.
            spaced = Hint.SPACED_PARENS in self.hints
            paren_hints = self.hints
            if Hint.GOOD_AFTER_LB not in self.hints:
                paren_hints |= Hint.NO_LB_BEFORE
            self._format_child(hints=paren_hints)  # '('
            if spaced:
                self._write_sp()
            self._format_child(hints=Hint.NO_LB_AFTER)  # <expr>
            if spaced:
                self._write_sp()
            self._format_child()  # ')'

        elif ct1 == "$" and ct3 == "=":
            self._format_child_range(4)  # '$'<id> = <expr>

        elif ct1 == "$":  # The function version, with possible capture
            self._format_child_range(2)  # '$'<id>
            self._write_sp()
            self._format_child(
                hints=Hint.NO_LB_BEFORE | Hint.NO_LB_AFTER
            )  # <begin_lambda>
            self._write_sp()
            self._format_child(hints=Hint.NO_LB_BEFORE)  # '='
            self._write_sp()
            self._format_child()  # <func_body>

        elif ct1 == "copy":
            self._format_child()  # 'copy'
            self._format_child(hints=Hint.NO_LB_BEFORE)  # '('
            self._format_child_range(2)  # <expr> ')'

        elif ct2 == "?$":
            # Never break on record ?$ operator - keep expr?$field together
            self._format_child(hints=Hint.NO_LB_AFTER)  # <expr>
            self._format_child(hints=Hint.NO_LB_BEFORE | Hint.NO_LB_AFTER)  # '?$'
            self._format_child(hints=Hint.NO_LB_BEFORE)  # <id>

        elif ct1 == "function":
            self._format_child_range(2)  # 'function' <begin_lambda>
            self._write_sp()
            self._format_child()  # <func_body>

        elif ct2 == "(":
            # Constructor calls like table(...), set(...), vector(...)
            # or regular function calls like Broker::publish(...)
            is_constructor = ct1 in ("table", "set", "vector", "record")

            # For constructors, use one-per-line if:
            # 1. There are comments to preserve, OR
            # 2. The content is too long to fit on a single line (>80 chars)
            # This produces consistent output regardless of input formatting.
            do_linebreak = False
            if is_constructor:
                if self.has_comments():
                    do_linebreak = True
                else:
                    # Estimate compact length by summing leaf node lengths.
                    # Add 1 for space after each comma.
                    compact_len = 0
                    comma_count = 0
                    for child, _ in self.node.traverse(include_cst=True):
                        if child.is_nl():
                            continue
                        # Count leaf nodes (no children means leaf)
                        if not child.nonerr_children:
                            compact_len += child.end_byte - child.start_byte
                        if child.token() == ",":
                            comma_count += 1
                    compact_len += comma_count  # Space after each comma
                    # If constructor can't fit on one line, use one-per-line format.
                    if compact_len > 80:
                        do_linebreak = True

            self._format_child(hints=self.hints)  # 'table' etc or function name expr
            # Set alignment to column after '(' BEFORE formatting it, so any
            # CST siblings (like comments) can use the alignment for continuations
            saved_align = self.ostream.get_align_column()
            self.ostream.set_align_column(self.ostream.get_visual_column() + 1)
            self._format_child(hints=Hint.NO_LB_BEFORE)  # '('
            if self._get_child_name() == "expr_list":
                if do_linebreak:
                    self._write_nl()
                    self._format_child(hints=Hint.COMPLEX_BLOCK)
                    self._write_nl()
                else:
                    self._format_child()
            self._format_child(hints=Hint.NO_LB_BEFORE)  # ')'
            self.ostream.set_align_column(saved_align)
            if self._get_child_name() == "attr_list":
                self._write_sp()
                self._format_child()

        elif self._is_binary_boolean():
            # For Boolean AND/OR, check if this is a toplevel sequence of them,
            # and if so, recommend linebreaks before the following operand.
            # ("toplevel" means that this must be AND/OR and all parent
            # expressions must be, up to something that isn't an expression --
            # a statement, for example.)
            #
            # We do this so we can line-break complex boolean expressions so
            # that each line ends with the boolean operator, and the following
            # operand starts on the next line.
            hints = Hint.NONE

            if self._is_expr_chain_of(ExprFormatter._is_binary_boolean):
                # Okay! It's AND/ORs all the way up to something not an expr.
                hints = Hint.GOOD_AFTER_LB

            # Set alignment for continuations when no outer alignment exists
            saved_align = self.ostream.get_align_column()
            if saved_align == 0:
                self.ostream.set_align_column(self.ostream.get_visual_column())
            self._format_child(hints=self.hints)  # <expr> - propagate incoming hints
            self._write_sp()
            self._format_child()  # '&&' / '||'
            self._write_sp()
            self._format_child(hints=hints)  # <expr>
            if saved_align == 0:
                self.ostream.set_align_column(0)

        elif self._is_string_concat():
            # This helps OutputStream nicely align long strings broken into
            # substrings concatenated by "+". We put the hint on the second
            # operand so linebreaks put '+' at end of line, not start of next.
            # Set alignment for continuations when no outer alignment exists.
            saved_align = self.ostream.get_align_column()
            if saved_align == 0:
                self.ostream.set_align_column(self.ostream.get_visual_column())
            self._format_child()  # <expr>
            self._write_sp()
            self._format_child()  # '+'
            self._write_sp()
            self._format_child(hints=Hint.GOOD_AFTER_LB)  # <expr>
            if saved_align == 0:
                self.ostream.set_align_column(0)

        elif self._is_binary_operator():
            # For binary operators (arithmetic, comparison, etc.), set GOOD_AFTER_LB
            # so that when breaking, the operator stays at end of line 1.
            # However, filter_break_points deprioritizes these vs commas.
            # Only set alignment when no outer alignment exists.
            saved_align = self.ostream.get_align_column()
            if saved_align == 0:
                self.ostream.set_align_column(self.ostream.get_visual_column())
            self._format_child(hints=self.hints)  # <expr> - propagate incoming hints
            self._write_sp()
            self._format_child()  # operator
            self._write_sp()
            self._format_child(hints=Hint.GOOD_AFTER_LB)  # <expr>
            if saved_align == 0:
                self.ostream.set_align_column(0)

        elif self._is_assignment():
            # Assignment expressions: prefer breaking after '=' operator
            # Set alignment for continuation at one indent level (8 spaces) up
            saved_align = self.ostream.get_align_column()
            self._format_child(hints=self.hints)  # LHS <expr>
            self._write_sp()
            self._format_child()  # '=' or '+=' or '-='
            self._write_sp()
            tab_col = self.indent * 8  # TAB_SIZE = 8
            self.ostream.set_align_column(tab_col + 8)  # One indent level up
            self._format_child(hints=Hint.GOOD_AFTER_LB)  # RHS <expr>
            self.ostream.set_align_column(saved_align)

        elif self._is_ternary():
            # Ternary (? :) expressions: prefer breaking after :
            # After ?: indent 8 spaces extra
            # After :: align with the true-branch expression
            saved_align = self.ostream.get_align_column()
            tab_col = self.indent * 8  # TAB_SIZE = 8
            self._format_child(hints=self.hints)  # condition <expr>
            self._write_sp()
            self._format_child()  # '?'
            self._write_sp()
            # Set alignment for break after '?' - 8 spaces extra
            self.ostream.set_align_column(tab_col + 8)
            true_col = self.ostream.get_visual_column()  # Remember position of true expr
            self._format_child(hints=Hint.GOOD_AFTER_LB)  # true <expr>
            self._write_sp()
            self._format_child(hints=Hint.GOOD_AFTER_LB)  # ':'
            self._write_sp()
            # Set alignment for break after ':' - align with true expr
            self.ostream.set_align_column(true_col)
            self._format_child(hints=Hint.GOOD_AFTER_LB)  # false <expr>
            self.ostream.set_align_column(saved_align)

        elif ct1 == "schedule":
            # schedule <expr> { <event_hdr> }
            self._format_child()  # 'schedule'
            self._write_sp()
            # Set alignment before interval so if line wraps, continuation aligns
            saved_align = self.ostream.get_align_column()
            self.ostream.set_align_column(self.ostream.get_visual_column())
            self._format_child()  # <expr> (interval)
            self._write_sp()
            self._format_child(hints=Hint.NO_LB_AFTER)  # '{'
            self._write_sp()
            self._format_child()  # <event_hdr>
            self._write_sp()
            self._format_child()  # '}'
            self.ostream.set_align_column(saved_align)

        else:
            # Fall back to simple space-separation
            super().format()


class NlFormatter(Formatter):
    """Newline formatting.

    Newlines get eliminated at the beginning or end of a sequence of child nodes
    (because such leading and trailing whitespace looks weird), while repeated
    newlines in mid-sequence are preserved but reduced to no more than one blank
    line.
    """

    def format(self) -> None:
        node = self.node
        # If this has another newline after it, do nothing.
        if node.next_cst_sibling and node.next_cst_sibling.is_nl():
            return

        # Write a single newline for any sequence of blank lines in the input,
        # unless this sequence is at the beginning or end of the sequence.

        if not node.next_cst_sibling or node.next_cst_sibling.token() == "}":
            # It's at the end of a NL sequence.
            return

        if node.prev_cst_sibling and node.prev_cst_sibling.is_nl():
            # It's a NL sequence.
            while node.prev_cst_sibling and node.prev_cst_sibling.is_nl():
                node = node.prev_cst_sibling

            if node.prev_cst_sibling and node.prev_cst_sibling.token() != "{":
                # There's something other than whitspace before this sequence.
                self._write_nl(force=True)


class AttrListFormatter(Formatter):
    """Formats attribute lists, deciding whether to use spaces around '=' in attrs.

    If any attribute's expression will have embedded blanks (e.g., intervals like
    '5 min', or binary expressions), then all attributes use spaces around '='.
    Otherwise, attributes are formatted without spaces around '='.
    """

    @staticmethod
    def _expr_has_embedded_blanks(node: Node) -> bool:
        """Check if an expression node will have embedded blanks when formatted."""
        for child, _ in node.traverse():
            # Intervals are formatted as "5 min" with a space
            if child.name() == "interval":
                return True
            # Function expressions have spaces in their signatures
            if child.name() == "begin_lambda":
                return True
            # Binary expressions have spaces around operators.
            # Pattern: expr with 3 children where first and third are both expr
            # (as opposed to function calls which are: id, '(', ... , ')')
            if child.name() == "expr" and len(child.nonerr_children) == 3:
                first = child.nonerr_children[0]
                third = child.nonerr_children[2]
                if first.name() == "expr" and third.name() == "expr":
                    return True
        return False

    def format(self) -> None:
        # Check if any attr has an expression with embedded blanks
        use_spaces = False
        for child in self.node.children:
            if child.name() == "attr":
                # Check if this attr has an '=' (meaning it has an expression)
                attr_children = child.nonerr_children
                if len(attr_children) >= 3:
                    # Structure: &attr_name '=' expr
                    expr_node = attr_children[2]
                    if self._expr_has_embedded_blanks(expr_node):
                        use_spaces = True
                        break

        # Format each attr with appropriate hints
        hints = Hint.ATTR_SPACES if use_spaces else Hint.NONE
        first = True
        while self._get_child_name() == "attr":
            if not first:
                self._write_sp()
            # Propagate self.hints (like GOOD_AFTER_LB) to first attr so line-breaker
            # knows this is a preferred break point
            child_hints = hints | self.hints if first else hints
            self._format_child(hints=child_hints)
            first = False


class AttrFormatter(Formatter):
    def format(self) -> None:
        if self._get_child_token(offset=1) == "=":
            # Format with or without spaces around '=' based on hint
            use_spaces = Hint.ATTR_SPACES in self.hints
            # Propagate hints (like GOOD_AFTER_LB) to first token
            # Keep &attr=expr together - don't allow breaks around '='
            self._format_child(hints=self.hints | Hint.NO_LB_AFTER)  # &attr_name
            if use_spaces:
                self._write_sp()
            self._format_child(hints=Hint.NO_LB_BEFORE | Hint.NO_LB_AFTER)  # '='
            if use_spaces:
                self._write_sp()
            self._format_child(hints=Hint.NO_LB_BEFORE)  # expr
        else:
            self._format_child(hints=self.hints)


class CommentFormatter(Formatter):
    """Base class for any kind of comment."""

    def __init__(
        self,
        script: Script,
        node: Node,
        ostream: OutputStream,
        indent: int = 0,
        hints: Hint = Hint.NONE,
    ) -> None:
        super().__init__(script, node, ostream, indent, hints)
        # Comments starting with #@ are annotations that must stay on specific
        # lines, so they don't count toward line length. Other comments do.
        comment_text = script.get_content(*node.script_range())
        if comment_text.startswith(b"#@"):
            self.hints |= Hint.ZERO_WIDTH


class MinorCommentFormatter(CommentFormatter):
    def format(self) -> None:
        node = self.node

        # Preserve separator to previous node if any.
        if node.prev_cst_sibling:
            if node.prev_cst_sibling.is_nl():
                # Keep newlines verbatim.
                self._write_nl()
            else:
                # End-of-line comment: don't allow line break before the comment.
                # This keeps the comment attached to the preceding token.
                self.hints |= Hint.NO_LB_BEFORE
                # If we are on the same line, normalize to exactly one space.
                self._write_sp()

        self._format_token()  # Write comment itself

        # If there's nothing or a newline before us, then this comment spans the
        # whole line and we write a regular newline.
        if node.prev_cst_sibling is None or node.prev_cst_sibling.is_nl():
            self._write_nl()
        else:
            self._write_nl(is_midline=True)


class ZeekygenCommentFormatter(CommentFormatter):
    def format(self) -> None:
        self._format_token()
        self._write_nl()


class ZeekygenPrevCommentFormatter(CommentFormatter):
    """A formatter for Zeekygen comments that refer to earlier items (##<)."""

    def __init__(
        self,
        script: Script,
        node: Node,
        ostream: OutputStream,
        indent: int = 0,
        hints: Hint = Hint.NONE,
    ) -> None:
        super().__init__(script, node, ostream, indent, hints)
        self.column = 0  # Start column of this comment.

    def format(self) -> None:
        # Handle indent explicitly here because of the transparent handling of
        # all comments. If we don't call this, nothing may force the indent for
        # the comment if it's the only thing on the line.
        self._write_indent()

        # If, newlines aside, another ##< comment came before us, space-align us
        # to the same start column of that comment.
        pnode = self.node.find_prev_cst_sibling(lambda n: not n.is_nl())
        if (pnode is not None) and pnode.is_zeekygen_prev_comment():
            assert pnode.formatter
            assert isinstance(pnode.formatter, ZeekygenPrevCommentFormatter)
            self._write_sp(pnode.formatter.column - self.ostream.get_column())
        else:
            self._write_sp()

        # Record the output column so potential subsequent Zeekygen
        # comments can use the same alignment.
        self.column = self.ostream.get_column()

        # Write comment itself
        self._format_token()

        # If this has another ##< comment after it, write the newline.
        if next_sibling := self.node.next_cst_sibling:
            if next_sibling.is_nl():
                if next_next_sibling := next_sibling.next_cst_sibling:
                    if next_next_sibling.is_zeekygen_prev_comment():
                        self._write_nl()


# ---- Explicit mappings for grammar symbols to formatters ---------------------
#
# NodeMapper.get() retrieves formatters not listed here by mapping symbol
# names to class names, e.g. module_decl -> ModuleDeclFormatter.

Formatter.register("const_decl", GlobalDeclFormatter)
Formatter.register("global_decl", GlobalDeclFormatter)
Formatter.register("option_decl", GlobalDeclFormatter)
Formatter.register("redef_decl", GlobalDeclFormatter)

Formatter.register("func", FuncHdrVariantFormatter)
Formatter.register("hook", FuncHdrVariantFormatter)
Formatter.register("event", FuncHdrVariantFormatter)

Formatter.register("capture", SpaceSeparatedFormatter)
Formatter.register("attr_list", AttrListFormatter)
Formatter.register("enum_body_elem", SpaceSeparatedFormatter)


class IntervalFormatter(Formatter):
    """Formats interval constants with a space between number and unit."""

    def format(self) -> None:
        self._format_child()  # <integer>
        self._write_sp()
        self._format_child()  # <time_unit>


Formatter.register("interval", IntervalFormatter)

Formatter.register("zeekygen_head_comment", ZeekygenCommentFormatter)
Formatter.register("zeekygen_next_comment", ZeekygenCommentFormatter)

Formatter.register("nullnode", NullFormatter)
