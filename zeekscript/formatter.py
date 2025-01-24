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

import enum
import inspect
import os
import sys


class NodeMapper:
    """Maps symbol names in the TS grammar (e.g "module_decl") to formatter classes."""

    def __init__(self):
        self._map = {}

    def register(self, symbol_name, klass):
        """Map a given symbol name to a given formatter class."""
        self._map[symbol_name] = klass

    def get(self, symbol_name):
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

    def _find_class(self, symbol_name):
        """Establishes symbol type -> Formatter class mapping.

        For example, this will try to resolve symbol type "module_decl" as
        ModuleDeclFormatter. When such a class exists, this adds a mapping to
        the internal _map so we don't have to resolve next time.
        """
        name_parts = [part.title() for part in symbol_name.split("_")]
        derived = "".join(name_parts) + "Formatter"

        def pred(mem):
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


class Formatter:
    # Our newline bytestring
    NL = os.linesep.encode("UTF-8")

    def __init__(self, script, node, ostream, indent=0, hints=None):
        """Formatter constructor.

        The script argument is the zeekscript.Script instance we're
        formatting. node is a zeekscript.Node, and the actual syntax tree
        element that this formatter instance will format. ostream is a
        zeekscript.OutputStream that we're writing the formatting to. The indent
        argument, an integer, tracks the number of indentation levels we're
        currently writing at.
        """
        self.script = script
        self.node = node
        self.ostream = ostream
        self.indent = indent
        self.hints = hints or Hint.NONE

        # AST child node index for iteration
        self._cidx = 0

        # Hook us into the node
        node.formatter = self

    def format(self):
        """Default formatting for a tree node

        This simply writes out children as per their own formatting, and writes
        out tokens directly.
        """
        if self.node.children:
            self._format_children()
        else:
            self._format_token()

    def _next_child(self):
        try:
            node = self.node.nonerr_children[self._cidx]
            self._cidx += 1
            return node
        except IndexError:
            return None

    def _format_child_impl(self, node, indent, hints=None):
        fclass = Formatter.lookup(node)
        formatter = fclass(
            self.script,
            node,
            self.ostream,
            indent=self.indent + int(indent),
            hints=hints,
        )
        formatter.format()

    def _format_child(self, child=None, indent=False, hints=None):
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

    def _format_child_range(self, num, hints=None, first_hints=None):
        """Format a given number of children of the node.

        Using this function ensures that no line breaks can happen between the
        requested children. "num" is the number of children to format, "hint" is
        a set of hints to tuck onto every child, and "first_hints" is an
        additional possible hint set for the first child only. (There's
        currently no indent flag, since the concept doesn't make much sense for
        a sequence of children. This might change in the future.)
        """
        hints = hints or Hint.NONE
        first_hints = first_hints or Hint.NONE

        if num <= 0:
            return

        if num == 1:
            # Single element: general and first-element hinting
            self._format_child(hints=hints | first_hints)
            return

        # First element of multiple: general hinting; first-element hinting;
        # avoid line breaks after the element.
        self._format_child(hints=hints | first_hints | Hint.NO_LB_AFTER)

        # Inner elements: general hinting; avoid line breaks
        for _ in range(num - 2):
            self._format_child(hints=hints | Hint.NO_LB_AFTER)

        # Last element: general hinting; avoid line break before
        self._format_child(hints=hints | Hint.NO_LB_BEFORE)

    def _format_children(self, sep=None):
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

    def _format_token(self):
        self._write(self.script.get_content(*self.node.script_range()))

    def _format_curly_statement_list(self, indent=True):
        """Format a child sequence of '{' <stmt_list>? '}'

        The statement list is optional, and we need to take into account
        comments to tweak the layout to "{ }" if there's really nothing between
        the braces. The need to indent depends on the caller's context.
        """
        self._format_child(indent=indent, hints=Hint.NO_LB_BEFORE)  # '{'

        # Shorten braces to "{ }" if there is at most whitespace between them.
        if (
            self._get_child_token() == "}"
            and self._get_child().has_only_whitespace_before()
        ):
            self._write_sp()
            self._format_child()  # '}'
            return

        self._write_nl()

        if self._get_child_type() == "stmt_list":
            self._format_child(indent=indent)  # <stmt_list>
            self._write_nl()
        self._format_child(indent=indent)  # '}'

    def _write(self, data, raw=False):
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

    def _write_indent(self):
        if self.ostream.get_column() == 0:
            self.ostream.write_tab_indent(self)
            self.ostream.write_space_align(self)
            return True
        return False

    def _write_sp(self, num=1):
        self._write(b" " * num)

    def _write_nl(self, num=1, force=False, is_midline=False):
        # It's rare that we really want to write newlines multiple times in
        # a row. If we just wrote one, don't do so again unless forced.
        # Still adjust space-alignment mode for the next write, though.
        if self.ostream.get_column() == 0 and not force:
            self.ostream.use_space_align(is_midline)
            return

        self._write(self.NL * num)

        # It's key here that space alignment mode is set after we write,
        # otherwise we cannot cancel its effect upon a second NL because
        # indentation/alignment will have already happened.
        self.ostream.use_space_align(is_midline)

    def _children_remaining(self):
        """Returns number of children of this node not yet visited."""
        return len(self.node.nonerr_children[self._cidx :])

    def _get_child(self, offset=0, absolute=False):
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

        return None

    def _get_child_type(self, offset=0, absolute=False):
        """Like _get_child(), but returns the TS type string ("decl", "stmt", etc).

        The returned type might refer to a named node or a literal token. Use
        _get_child_name() or _get_child_token() when possible, to avoid
        confusion between named and token nodes.

        Returns None when no matching node exists.
        """
        try:
            return self._get_child(offset, absolute).type
        except AttributeError:
            return None

    def _get_child_name(self, offset=0, absolute=False):
        """Like _get_child_type(), but for named nodes.

        Returns None of the child isn't a named node or no matching node exists.
        """
        try:
            return self._get_child(offset, absolute).name()
        except AttributeError:
            return None

    def _get_child_token(self, offset=0, absolute=False):
        """Like _get_child_type(), but for terminal nodes.

        Returns None of the child doesn't represent a plain token or no matching
        node exists.
        """
        try:
            return self._get_child(offset, absolute).token()
        except AttributeError:
            return None

    @staticmethod
    def register(symbol_name, klass):
        return MAP.register(symbol_name, klass)

    @staticmethod
    def lookup(node):
        """Formatter lookup for a zeekscript.Node, based on its type information."""
        # If we're looking up a token node, always use the fallback formatter,
        # which writes it out directly.  This ensures that we don't confuse a
        # node.type of the same name, e.g. a variable called 'decl'.
        if not node.is_named:
            return Formatter
        return MAP.get(node.type)


class NullFormatter(Formatter):
    """The null formatter doesn't output anything."""

    def format(self):
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

    def format(self):
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

    def format(self):
        if self.node.children:
            self._format_children(b" ")
            self._write_nl()
        else:
            self._format_token()


class SpaceSeparatedFormatter(Formatter):
    """This formatter simply separates all nodes with a space."""

    def format(self):
        if self.node.children:
            self._format_children(b" ")
        else:
            self._format_token()


class PreprocDirectiveFormatter(LineFormatter):
    """@if and friends don't get indented or line-broken."""

    def format(self):
        self.ostream.use_tab_indent(False)
        self.ostream.use_linebreaks(False)
        super().format()
        self.ostream.use_tab_indent(True)
        self.ostream.use_linebreaks(True)


class PragmaFormatter(LineFormatter):
    def format(self):
        self._format_token()


class ModuleDeclFormatter(Formatter):
    def format(self):
        self._format_child()  # 'module'
        self._write_sp()
        self._format_child_range(2)  # <name> ';'
        self._write_nl()


class ExportDeclFormatter(Formatter):
    def format(self):
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

    def _format_typed_initializer(self):
        if self._get_child_token() == ":":
            self._format_child(hints=Hint.NO_LB_AFTER)  # ':'
            self._write_sp()
            self._format_child()  # <type>

        if self._get_child_name() == "initializer":
            self._write_sp()
            self._format_child()  # <initializer>

        if self._get_child_name() == "attr_list":
            self._write_sp()
            self._format_child()


class GlobalDeclFormatter(TypedInitializerFormatter):
    """A formatter for the global-like symbols (global, option, const, simple
    value redefs), which all layout similarly.
    """

    def format(self):
        self._format_child()  # "global", "option", etc
        self._write_sp()
        self._format_child()  # <id>
        self._format_typed_initializer()
        self._format_child(hints=Hint.NO_LB_BEFORE)  # ';'
        self._write_nl()


class ComplexSequenceFormatterMixin:
    """A mixin to figure out whether to line-break the (remaining) children.

    The idea here is to determine if any one child is "complex". If so, the
    entire sequence is to be written in individual lines. The formatting of
    these lines doesn't happen in this mixin, only the complexity decision.

    The default complexity decision looks for comments in the subtree.
    """

    def is_complex(self):
        return self.is_complex_node(self.node)

    def is_complex_node(self, node):
        for child, _ in node.traverse(include_cst=True):
            if child == node:  # Skip the start node itself
                continue
            if child.is_comment():
                return True

            # This logic used to be in place specifically for { ... }
            # blocks initializing enums. It seems too strict, but can
            # be revisited later.
            # if (n.name() == 'expr' and
            #    (len(n.nonerr_children) != 1 or
            #     n.nonerr_children[0].name() not in ('id', 'constant', 'pattern'))):
            #    return True

        return False


class InitializerFormatter(Formatter):
    def format(self):
        # This is just space-separation, really. I'm leaving the class in place
        # for now since I think initializer handling isn't fully settled.
        self._format_child()  # '=', '+=', etc
        self._write_sp()
        self._format_child()  # <expr>


class EnumBodyFormatterMixin(ComplexSequenceFormatterMixin):
    """A mixin that knows when to break an enum_body onto lines."""

    def _format_curly_enum_body(self):
        """Formats an '{' <enum_body> '}' sequence."""
        do_linebreak = self.is_complex()  # Must call before we consume '{'

        self._format_child()  # '{'

        if do_linebreak:
            self._write_nl()
            self._format_child(indent=True, hints=Hint.COMPLEX_BLOCK)  # enum_body
            self._write_nl()
        else:
            self._write_sp()
            self._format_child(indent=True)  # enum_body
            self._write_sp()

        self._format_child()  # '}'


class RedefEnumDeclFormatter(Formatter, EnumBodyFormatterMixin):
    def format(self):
        self._format_child()  # 'redef'
        self._write_sp()
        self._format_child()  # 'enum'
        self._write_sp()
        self._format_child()  # <id>
        self._write_sp()
        self._format_child()  # '+='
        self._write_sp()
        self._format_curly_enum_body()
        self._format_child(hints=Hint.NO_LB_BEFORE)  # ';'
        self._write_nl()


class RedefRecordDeclFormatter(Formatter):
    def format(self):
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
    def format(self):
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


class TypeFormatter(SpaceSeparatedFormatter, EnumBodyFormatterMixin):
    def format(self):
        if self._get_child_token() == "set":
            self._format_child()  # 'set'
            self._format_typelist()  # '[' ... ']'

        elif self._get_child_token() == "table":
            self._format_child()  # 'table'
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
            self._format_child()  # 'enum'
            self._write_sp()
            self._format_curly_enum_body()

        elif self._get_child_token() == "function":
            self._format_child_range(2)  # 'function' <func_params>

        elif self._get_child_token() in ["event", "hook"]:
            self._format_child()  # 'event'/'hook'
            self._format_child(hints=Hint.NO_LB_BEFORE)  # '('
            if self._get_child_name() == "formal_args":
                self._format_child()
            self._format_child(hints=Hint.NO_LB_BEFORE)  # ')'

        else:
            # Format anything else with plain space separation, e.g. "vector of foo"
            super().format()

    def _format_typelist(self):
        self._format_child(hints=Hint.NO_LB_BEFORE)  # '['
        while self._get_child_name() == "type":
            self._format_child()  # <type>
            if self._get_child_token() == ",":
                self._format_child(hints=Hint.NO_LB_BEFORE)  # ','
                self._write_sp()
        self._format_child(hints=Hint.NO_LB_BEFORE)  # ']'


class TypeSpecFormatter(Formatter):
    def format(self):
        self._format_child(hints=Hint.NO_LB_AFTER)  # <id>
        self._format_child(hints=Hint.NO_LB_AFTER)  # ':'
        self._write_sp()
        self._format_child()  # <type>
        if self._get_child_name() == "attr_list":
            self._write_sp()
            self._format_child()
        self._format_child(hints=Hint.NO_LB_BEFORE)  # ';'
        self._write_nl()


class EnumBodyFormatter(Formatter):
    def format(self):
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
    def format(self):
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
    def format(self):
        self._format_child()  # <func>, <hook>, or <event>


class FuncHdrVariantFormatter(Formatter):
    def format(self):
        if self._get_child_token() == "redef":
            self._format_child()  # 'redef'
            self._write_sp()
        self._format_child()  # 'function', 'hook', or 'event'
        self._write_sp()
        self._format_child()  # <id>
        self._format_child()  # <func_params>
        if self._get_child_name() == "attr_list":
            self._write_sp()
            self._format_child()  # <attr_list>


class FuncParamsFormatter(Formatter):
    def format(self):
        self._format_child(hints=Hint.NO_LB_BEFORE)  # '('
        if self._get_child_name() == "formal_args":
            self._format_child()  # <formal_args>
        self._format_child(hints=Hint.NO_LB_BEFORE)  # ')'
        if self._get_child_token() == ":":
            self._format_child(hints=Hint.NO_LB_AFTER)  # ':'
            self._write_sp()
            self._format_child()  # <type>


class FuncBodyFormatter(Formatter):
    def format(self):
        self._write_nl()
        self._format_curly_statement_list()


class FormalArgsFormatter(Formatter):
    def format(self):
        while self._get_child_name() == "formal_arg":
            self._format_child()  # <formal_arg>
            if self._get_child():
                self._format_child(hints=Hint.NO_LB_BEFORE)  # ',' or ';'
                self._write_sp()


class FormalArgFormatter(Formatter):
    def format(self):
        self._format_child(hints=Hint.NO_LB_AFTER)  # <id>
        self._format_child(hints=Hint.NO_LB_AFTER)  # ':'
        self._write_sp()
        self._format_child()  # <type>
        if self._get_child_name() == "attr_list":
            self._write_sp()
            self._format_child()  # <attr_list>


class IndexSliceFormatter(Formatter):
    def format(self):
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
    def format(self):
        self._format_child(hints=Hint.NO_LB_BEFORE)  # '['
        while self._get_child_name() == "capture":
            self._format_child()  # <capture>
            if self._get_child_token() == ",":
                self._format_child(hints=Hint.NO_LB_BEFORE)  # ','
                self._write_sp()
        self._format_child(hints=Hint.NO_LB_BEFORE)  # ']'


class StmtFormatter(TypedInitializerFormatter):
    def _format_stmt_block(self):
        """Helper for formatting a block of statements.

        This may either be an { ... } block or a single-line statement.
        """
        self._write_nl()
        self._format_child(indent=True)  # <stmt>
        self._write_nl()

    def _format_when(self):
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

    def format(self):
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
            self._format_child_range(2)  # <expr_list>/<event_hdr> ';'
            self._write_nl()

        elif start_token == "if":
            self._format_child()  # 'if'
            self._write_sp()
            self._format_child(hints=Hint.NO_LB_BEFORE)  # '('
            self._write_sp()
            self._format_child()  # <expr>
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

            self._format_stmt_block()

            # An else-block also requires special treatment
            if self._get_child_token() == "else":
                self._format_child()  # 'else'

                # Special treatment of "else if": we keep those on the same
                # line, since otherwise, a switch-case-like cascade of if-else
                # would get progressively more indented.
                if self._get_child().has_property(
                    lambda n: n.nonerr_children[0].token() == "if"
                ):
                    self._write_sp()
                    self._format_child()  # <stmt>
                else:
                    self._format_stmt_block()

        elif start_token == "switch":
            self._format_child()  # 'switch'
            self._write_sp()
            self._format_child()  # <expr>
            self._write_nl()
            self._format_child(indent=True)  # '{'
            # Shorten braces to "{ }" if there is at most whitespace between them.
            if (
                self._get_child_token() == "}"
                and self._get_child().has_only_whitespace_before()
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
            self._write_sp()
            self._format_child(hints=Hint.NO_LB_BEFORE)  # ')'
            self._format_stmt_block()  # <stmt>

        elif start_token == "while":
            self._format_child()  # 'while'
            self._write_sp()
            self._format_child(hints=Hint.NO_LB_BEFORE)  # '('
            self._write_sp()
            self._format_child()  # <expr>
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
            self._format_child()  # <id>
            self._format_typed_initializer()
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
    def format(self):
        if Hint.COMPLEX_BLOCK in self.hints or self.is_complex():
            while self._get_child_name() == "expr":
                self._format_child(indent=True)  # <expr>
                if self._get_child():
                    self._format_child(hints=Hint.NO_LB_BEFORE)  # ','
                    self._write_nl()
        else:
            while self._get_child_name() == "expr":
                self._format_child()  # <expr>
                if self._get_child():
                    self._format_child(hints=Hint.NO_LB_BEFORE)  # ','
                    self._write_sp()


class CaseListFormatter(Formatter):
    def format(self):
        while self._get_child():
            if self._get_child_token() == "case":
                self._format_child()  # 'case'
                self._write_sp()
                self._format_child_range(2)  # <expr_list> or <case_type_list>, ':'
            else:
                self._format_child_range(2)  # 'default' ':'
            self._write_nl()
            if self._get_child_name() == "stmt_list":
                self._format_child(indent=True)  # <stmt_list>


class CaseTypeListFormatter(Formatter):
    def format(self):
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
    def format(self):
        self._format_child()  # <id>
        self._format_child(hints=Hint.NO_LB_BEFORE)  # '('
        if self._get_child_name() == "expr_list":
            self._format_child()  # <expr_list>
        self._format_child(hints=Hint.NO_LB_BEFORE)  # ')'


class AssertMsgFormatter(SpaceSeparatedFormatter):
    pass


class ExprFormatter(SpaceSeparatedFormatter, ComplexSequenceFormatterMixin):
    # Like statments, expressions aren't currently broken into specific symbol
    # types, so we use helpers or parse into them to identify what particular
    # kind of expression we're facing.

    def _is_binary_boolean(self):
        """Predicate, returns true if this an || or && expression."""
        return len(self.node.nonerr_children) == 3 and self._get_child_token(
            offset=1, absolute=True
        ) in ("||", "&&")

    def _is_string_concat(self):
        """Predicate, returns true if this a <string> + <string> expression."""

        def is_constant_expr(node):
            return (
                node.name() == "expr"
                and len(node.nonerr_children) == 1
                and node.nonerr_children[0].name() == "constant"
                and len(node.nonerr_children[0].nonerr_children) == 1
                and node.nonerr_children[0].nonerr_children[0].name() == "string"
            )

        def is_concat_expr(node):
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

    def _is_expr_chain_of(self, formatter_predicate):
        """Predicate, returns true if the given predicate is true for all
        formatters from this expression up to the first non-expression.
        This helps identify chains of similar expressions, per the above
        predicates.
        """
        node = self.node

        while (
            node
            and isinstance(node.formatter, ExprFormatter)
            and formatter_predicate(node.formatter)
        ):
            node = node.parent

        return node and not isinstance(node.formatter, ExprFormatter)

    def format(self):
        cn1, cn2, _ = (self._get_child_name(offset=n) for n in (0, 1, 2))
        ct1, ct2, ct3 = (self._get_child_token(offset=n) for n in (0, 1, 2))

        if cn1 == "expr" and ct2 == "[":
            self._format_child()  # <expr>
            self._format_child(hints=Hint.NO_LB_BEFORE | Hint.NO_LB_AFTER)  # '['
            self._format_child()  # <expr_list>
            self._format_child(hints=Hint.NO_LB_BEFORE)  # ']'

        elif cn1 == "expr" and ct2 == "$":
            self._format_child()
            self._format_child(hints=Hint.NO_LB_BEFORE | Hint.NO_LB_AFTER)
            while self._get_child():
                self._format_child()

        elif cn1 == "expr" and cn2 == "index_slice":
            while self._get_child():
                self._format_child()

        elif ct1 == "!":
            # Negation looks better when spaced apart
            self._format_child(hints=Hint.NO_LB_AFTER)
            self._write_sp()
            self._format_child()

        elif ct1 in ["|", "++", "--", "~", "-", "+"]:
            # No space when those operators are involved
            self._format_child(hints=Hint.NO_LB_AFTER)
            while self._get_child():
                self._format_child()

        elif cn1 == "expr" and ct2 == "!" and ct3 == "in":
            self._format_child()  # <expr>
            self._write_sp()
            self._format_child(hints=Hint.NO_LB_AFTER)  # '!'
            self._format_child()  # 'in'
            self._write_sp()
            self._format_child()  # <expr>

        elif ct1 in ("{", "["):
            # Vector/table/set initializers: '['/'{' <expr_list> ']'/'}'
            do_linebreak = self.is_complex()  # Must call before we consume opener
            self._format_child(hints=Hint.NO_LB_BEFORE)  # '{' / '['
            if self._get_child_name() == "expr_list":
                if do_linebreak:
                    self._write_nl()
                    self._format_child(hints=Hint.COMPLEX_BLOCK)  # expr_list
                    self._write_nl()
                else:
                    self._write_sp()
                    self._format_child()  # expr_list
                    self._write_sp()
            else:
                # Just a space when the initializer list has no members.
                self._write_sp()

            self._format_child()  # '}' / ']'

        elif ct1 == "(":
            self._format_child(hints=Hint.NO_LB_BEFORE)  # '('
            self._write_sp()
            self._format_child(hints=Hint.NO_LB_AFTER)  # <expr>
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
            self._format_child_range(3)  # <expr> '$?' <expr>

        elif ct1 == "function":
            self._format_child_range(2)  # 'function' <begin_lambda>
            self._write_sp()
            self._format_child()  # <func_body>

        elif ct2 == "(":
            # initializers such as table(...)
            self._format_child()  # 'table' etc
            self._format_child(hints=Hint.NO_LB_BEFORE)  # '('
            if self._get_child_name() == "expr_list":
                self._format_child()
            self._format_child(hints=Hint.NO_LB_BEFORE)  # ')'
            if self._get_child_name() == "attr_list":
                self._write_sp()
                self._format_child()

        elif self._is_binary_boolean():
            # For Boolean AND/OR, check if this is a toplevel sequence of them,
            # and if so, recommend the operator for linebreaks. ("toplevel"
            # means that this must be AND/OR and all parent expressions must be,
            # up to something that isn't an expression -- a statement, for
            # example.)
            #
            # We do this so we can line-break complex boolean expressions so
            # that each toplevel one ends on a new line, starting with the
            # boolean operand. OutputStream's handling of the GOOD_AFTER_LB
            # hint implements this.
            hints = None

            if self._is_expr_chain_of(ExprFormatter._is_binary_boolean):
                # Okay! It's AND/ORs all the way up to something not an expr.
                hints = Hint.GOOD_AFTER_LB

            self._format_child()  # <expr>
            self._write_sp()
            self._format_child(hints=hints)  # '&&' / '||'
            self._write_sp()
            self._format_child()  # <expr>

        elif self._is_string_concat():
            # This helps OutputStream nicely align long strings broken into
            # substrings concatenated by "+".
            self._format_child()  # <expr>
            self._write_sp()
            self._format_child(hints=Hint.GOOD_AFTER_LB)  # '+'
            self._write_sp()
            self._format_child()  # <expr>

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

    def format(self):
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


class AttrFormatter(Formatter):
    def format(self):
        if self._get_child_token(offset=1) == "=":
            # The range ensures we keep this on one line
            self._format_child_range(3)
        else:
            self._format_child()


class CommentFormatter(Formatter):
    """Base class for any kind of comment."""

    def __init__(self, script, node, ostream, indent=0, hints=None):
        super().__init__(script, node, ostream, indent, hints)
        self.hints |= Hint.ZERO_WIDTH  # Comments never count toward line length


class MinorCommentFormatter(CommentFormatter):
    def format(self):
        node = self.node

        # Preserve separator to previous node if any.
        if node.prev_cst_sibling:
            if node.prev_cst_sibling.is_nl():
                # Keep newlines verbatim.
                self._write_nl()
            else:
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
    def format(self):
        self._format_token()
        self._write_nl()


class ZeekygenPrevCommentFormatter(CommentFormatter):
    """A formatter for Zeekygen comments that refer to earlier items (##<)."""

    def __init__(self, script, node, ostream, indent=0, hints=None):
        super().__init__(script, node, ostream, indent, hints)
        self.column = 0  # Start column of this comment.

    def format(self):
        # Handle indent explicitly here because of the transparent handling of
        # all comments. If we don't call this, nothing may force the indent for
        # the comment if it's the only thing on the line.
        self._write_indent()

        # If, newlines aside, another ##< comment came before us, space-align us
        # to the same start column of that comment.
        pnode = self.node.find_prev_cst_sibling(lambda n: not n.is_nl())
        if pnode and pnode.is_zeekygen_prev_comment():
            self._write_sp(pnode.formatter.column - self.ostream.get_column())
        else:
            self._write_sp()

        # Record the output column so potential subsequent Zeekygen
        # comments can use the same alignment.
        self.column = self.ostream.get_column()

        # Write comment itself
        self._format_token()

        # If this has another ##< comment after it, write the newline.
        try:
            if (
                self.node.next_cst_sibling.is_nl()
                and self.node.next_cst_sibling.next_cst_sibling.is_zeekygen_prev_comment()
            ):
                self._write_nl()
        except AttributeError:
            pass


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
Formatter.register("attr_list", SpaceSeparatedFormatter)
Formatter.register("interval", Formatter)
Formatter.register("enum_body_elem", SpaceSeparatedFormatter)

Formatter.register("zeekygen_head_comment", ZeekygenCommentFormatter)
Formatter.register("zeekygen_next_comment", ZeekygenCommentFormatter)

Formatter.register("nullnode", NullFormatter)
