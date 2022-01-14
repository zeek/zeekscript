import inspect
import sys

class NodeMapper:
    def __init__(self):
        self._map = {}

    def register(self, symbol_name, klass, addl_args=None):
        self._map[symbol_name] = (klass, addl_args or {})

    def get(self, symbol_name):
        # If we have an explicit mapping, use it:
        if symbol_name in self._map:
            return self._map[symbol_name]

        # Try deriving from symbol_name:
        self._find_class(symbol_name)

        if symbol_name in self._map:
            return self._map[symbol_name]

        # Last straw: a default formatter:
        return Formatter, {}

    def _find_class(self, symbol_name):
        """Locates a Formatter class based on a symbol name.

        For example, this will try to resolve symbol name " module_decl" as
        ModuleDeclFormatter. When found, adds a mapping to the internal _map.
        """
        name_parts = [part.title() for part in symbol_name.split('_')]
        derived = ''.join(name_parts) + 'Formatter'
        pred = lambda mem: inspect.isclass(mem) and mem.__name__ == derived
        classes = inspect.getmembers(sys.modules[__name__], pred)

        if classes:
            self._map[symbol_name] = (classes[0][1], {})

MAP = NodeMapper()


# ---- Symbol formatters -------------------------------------------------------

class Formatter:
    def __init__(self, script, node, ostream, indent=0, parent=None):
        self._script = script
        self._node = node
        self._ostream = ostream

        # Number of tabs to indent with
        self._indent = indent

        # Child node index for iteration
        self._cidx = 0

        # Like tree_sitter.Node we use a parent/children structure, so a given
        # formatter can access formatting state in its surroundings.
        self.parent = parent
        self.prev = None # We currently need no self.next
        self.children = []

    def format(self):
        if self._node.children:
            self._format_children()
        else:
            self._format_token()

    def _next_child(self):
        try:
            node = self._node.children[self._cidx]
            self._cidx += 1
            return node
        except IndexError:
            return None

    def _format_child_impl(self, node, indent):
        fclass, addl_args = Formatter.lookup(node)
        formatter = fclass(self._script, node, self._ostream,
                           indent=self._indent + int(indent),
                           parent=self, **addl_args)

        if self.children:
            formatter.prev = self.children[-1]
        self.children.append(formatter)

        formatter.format()

    def _format_child(self, indent=False):
        node = self._next_child()

        for child in node.prev_cst_siblings:
            self._format_child_impl(child, indent=indent)

        self._format_child_impl(node, indent=indent)

        for child in node.next_cst_siblings:
            self._format_child_impl(child, indent=indent)

    def _format_child_range(self, num, indent=False):
        for _ in range(num):
            self._format_child(indent)

    def _format_children(self, sep=None, final=None):
        while self._children_remaining():
            self._format_child()
            if sep is not None and self._children_remaining() > 0:
                self._write(sep)
        if final is not None:
            self._write(final)

    def _format_token(self):
        buf = self._script[self._node.start_byte:self._node.end_byte]
        self._write(buf)

    def _write(self, data):
        if isinstance(data, str):
            data = data.encode('UTF-8')

        # Transparently indent at the beginning of lines, but only if we're not
        # writing a newline anyway.
        if not data.startswith(b'\n') and self._write_indent():
            # We just indented. Don't write any additional whitespace at the
            # beginning now. Such whitespace might exist from spacing that
            # would result without the presence of interrupting comments.
            data = data.lstrip()

        self._ostream.write(data)

    def _write_indent(self):
        if self._ostream.get_column() == 0:
            self._ostream.write(b'\t' * self._indent)
            self._ostream.write_space_indent()
            return True
        return False

    def _write_sp(self, num=1):
        self._write(b' ' * num)

    def _write_nl(self, num=1, force=False, is_midline=False):
        self._ostream.set_space_indent(is_midline)

        # It's rare that we really want to write newlines multiple times in a
        # row. Normally, if we just wrote one, don't do so again unless we
        # force.
        if self._ostream.get_column() == 0 and not force:
            return

        self._write(b'\n' * num)

    def _is_comment(self, offset=0):
        node = self._get_child(offset=offset, skip_comments=False)
        return node and node.is_comment()

    def _is_zeekygen_prev_comment(self, offset=0):
        node = self._get_child(offset=offset, skip_comments=False)
        return node and node.is_zeekygen_prev_comment()

    def _children_remaining(self):
        remaining = 0
        for child in self._node.children[self._cidx:]:
            if not child.is_comment():
                remaining += 1
        return remaining

    def _get_child(self, offset=0, skip_comments=True):
        """Accessor for child nodes, without adjusting the offset index.

        Without additional options, it returns the current child node, ignoring
        any comment nodes. When using the offset argument, returns children
        before/after the current child. If setting skip_comments=False, it does
        not skip comment nodes.
        """
        direction = 1 if offset >= 0 else -1
        offset = abs(offset)

        for child in self._node.children[self._cidx::direction]:
            if not skip_comments or not child.is_comment():
                if offset == 0:
                    return child
                offset -= 1

        return None

    def _get_child_type(self, offset=0, skip_comments=True):
        """Returns the type ("decl", "stmt", etc) of the current child node.

        When integer offset is provided, returns node before/after the current
        child (e.g., offset=-1 means the node before the current child).
        Transparently skips any comment nodes, unless skip_comments=False.
        Never adjusts the child offset index. The returned type might refer to
        a named node or a literal token, Returns '' when no matching node exists.
        """
        try:
            return self._get_child(offset, skip_comments).type
        except AttributeError:
            return ''

    @staticmethod
    def register(symbol_name, klass, addl_args=None):
        return MAP.register(symbol_name, klass, addl_args)

    @staticmethod
    def lookup(node):
        # If we're looking up a token node, automatically fall back
        # to a dummy formatter.
        if not node.is_named:
            return Formatter, {}
        return MAP.get(node.type)


class NullFormatter(Formatter):
    """The null formatter doesn't output anything.

    We add newlines manually throughout the tree, using existing ones only as a
    guideline.
    """
    def format(self):
        pass


class LineFormatter(Formatter):
    """This formatter separates all nodes with space and terminates with a newline."""
    def format(self):
        if self._node.children:
            self._format_children(b' ', b'\n')
        else:
            self._format_token()


class SpaceSeparatedFormatter(Formatter):
    """This formatter simply separates all nodes with a space."""
    def format(self):
        if self._node.children:
            self._format_children(b' ')
        else:
            self._format_token()


class TypechangeFormatter(Formatter):
    """A formatter that issues a newline after formatting when the next sibling
    node has a different type, or isn't among a given list of types. This helps
    with grouping "similar" nodes tightly, adding an extra newline only when a
    group finishes.
    """
    def __init__(self, script, node, ostream, indent=0, parent=None, typelist=None):
        super().__init__(script, node, ostream, indent, parent)
        self._typelist = typelist

    def format(self):
        super().format()
        if self._next_sibling_typechange():
            self._write_nl()

    def _next_sibling_typechange(self):
        # There's no type change when there's nothing to compare to, or that
        # thing isn't a complex node (for example, a '}' at the end of an export
        # block).
        if self._node.next_sibling is None or not self._node.next_sibling.is_named:
            return False

        nextsib = self._node.next_sibling

        if self._typelist and nextsib.type not in self._typelist:
            return False

        if nextsib.type == self._node.type:
            # For sequences of 'decl' this isn't very meaningful, so look at
            # the children to decide.
            try:
                if (self._node.type == 'decl' and
                    self._node.children[0].type != nextsib.children[0].type):
                    return True
            except IndexError:
                pass

            # Always separate uncommented record type definitions. (Comments
            # already trigger a separate newline, since they count as a type
            # change relative to decls.)
            def is_record_decl(node):
                try:
                    return (node.children[0].type == 'type_decl' and
                            node.children[0].children[3].children[0].type == 'record')
                except IndexError:
                    return False

            if is_record_decl(self._node) or is_record_decl(nextsib):
                return True

            # Always separate functions, events, hooks with extra whitespace
            def is_func_decl(node):
                try:
                    return node.type == 'decl' and node.children[0].type == 'func_decl'
                except IndexError:
                    return False

            if is_func_decl(self._node) or is_func_decl(nextsib):
                return True

            return False

        return True


class ModuleDeclFormatter(Formatter):
    def format(self):
        self._format_child() # 'module'
        self._write_sp()
        self._format_child_range(2) # <name> ';'
        self._write_nl()


class ExportDeclFormatter(Formatter):
    def format(self):
        self._format_child() # 'export'
        self._write_sp()
        self._format_child() # '{'
        self._write_nl()
        while self._get_child_type() == 'decl':
            self._format_child(indent=True)
        self._format_child() # '}'
        self._write_nl()


class TypedInitializerFormatter(Formatter):
    """Helper for common construct that's not a separate symbol in the grammar:
    [:<type>] [<initializer] [attributes]
    """
    def _format_typed_initializer(self):
        if self._get_child_type() == ':':
            self._format_child() # ':'
            self._write_sp()
            self._format_child() # <type>

        if self._get_child_type() == 'initializer':
            self._write_sp()
            self._format_child() # <initializer>

        if self._get_child_type() == 'attr_list':
            self._write_sp()
            self._format_child()


class GlobalDeclFormatter(TypedInitializerFormatter):
    """A formatter for the global-like symbols (global, option, const, simple
    value redefs), which all layout similarly.
    """
    def format(self):
        self._format_child() # "global", "option", etc
        self._write_sp()
        self._format_child() # <id>
        self._format_typed_initializer()
        self._format_child() # ';'
        self._write_nl()


class InitializerFormatter(Formatter):
    def format(self):
        if self._get_child_type() == 'init_class':
            self._format_child() # '=', '+=', etc
            self._write_sp()

        self._format_child() # <init>

class InitFormatter(Formatter):
    def format(self):
        if self._get_child_type() == '{':
            self._format_child() # '{'
            # Any number of expressions, comma-separated
            if self._get_child_type() == 'expr':
                self._write_nl()
                while self._get_child_type() == 'expr':
                    self._format_child(indent=True) # <expr>
                    if self._get_child_type() == ',':
                        self._format_child() # ','
                    self._write_nl()
            else:
                self._write_sp()
            self._format_child() # '}'
        else:
            self._format_child() # <expr>


class RedefEnumDeclFormatter(Formatter):
    def format(self):
        self._format_child() # 'redef'
        self._write_sp()
        self._format_child() # 'enum'
        self._write_sp()
        self._format_child() # <id>
        self._write_sp()
        self._format_child() # '+='
        self._write_sp()
        self._format_child() # '{'
        self._write_nl()
        self._format_child(indent=True) # enum_body
        self._format_child_range(2) # '}' ';'
        self._write_nl()


class RedefRecordDeclFormatter(Formatter):
    def format(self):
        self._format_child() # 'redef'
        self._write_sp()
        self._format_child() # 'record'
        self._write_sp()
        self._format_child() # <id>
        self._write_sp()
        self._format_child() # '+='
        self._write_sp()
        self._format_child() # '{'
        self._write_nl()
        while self._get_child_type() == 'type_spec': # any number of type_specs
            self._format_child(indent=True)
        self._format_child() # '}'
        if self._get_child_type() == 'attr_list':
            self._write_sp()
            self._format_child() # <attr_list>
        self._format_child() # ';'
        self._write_nl()


class TypeDeclFormatter(Formatter):
    def format(self):
        self._format_child() # 'type'
        self._write_sp()
        self._format_child_range(2) # <id> ':'
        self._write_sp()
        self._format_child() # <type>
        if self._get_child_type() == 'attr_list':
            self._write_sp()
            self._format_child() # <attr_list>
        self._format_child() # ';'
        self._write_nl()


class TypeFormatter(SpaceSeparatedFormatter):

    def format(self):
        if self._get_child_type() == 'set':
            self._format_child() # 'set'
            self._format_typelist() # '[' ... ']'

        elif self._get_child_type() == 'table':
            self._format_child() # 'table'
            self._format_typelist() # '[' ... ']'
            self._write_sp()
            self._format_child() # 'of'
            self._write_sp()
            self._format_child() # <type>

        elif self._get_child_type() == 'record':
            self._format_child() # 'record',
            self._write_sp()
            self._format_child() # '{'

            if self._get_child_type() == 'type_spec': # any number of type_specs
                self._write_nl()
                while self._get_child_type() == 'type_spec':
                    self._format_child(indent=True)
            else:
                self._write_sp() # empty record, keep on one line

            self._format_child() # '}'

        elif self._get_child_type() == 'enum':
            self._format_child() # 'enum'
            self._write_sp()
            self._format_child() # '{'
            self._write_nl()
            self._format_child(indent=True) # enum_body
            self._format_child() # '}'

        elif self._get_child_type() == 'function':
            self._format_child_range(2) # 'function' <func_params>

        elif self._get_child_type() in ['event', 'hook']:
            self._format_child_range(2) # 'event'/'hook' '('
            if self._get_child_type() == 'formal_args':
                self._format_child()
            self._format_child() # ')'

        else:
            # Format anything else with plain space separation, e.g. "vector of foo"
            super().format()

    def _format_typelist(self):
        self._format_child() # '['
        while self._get_child_type() == 'type':
            self._format_child() # <type>
            if self._get_child_type() == ',':
                self._format_child() # ','
                self._write_sp()
        self._format_child() # ']'


class TypeSpecFormatter(Formatter):
    def format(self):
        self._format_child_range(2) # <id> ':'
        self._write_sp()
        self._format_child() # <type>
        if self._get_child_type() == 'attr_list':
            self._write_sp()
            self._format_child()
        self._format_child() # ';'
        self._write_nl()


class EnumBodyFormatter(Formatter):
    def format(self):
        while self._get_child():
            self._format_child() # enum_body_elem
            if self._get_child():
                self._format_child() # ',' (optional at the end of the list)
            self._write_nl()


class FuncDeclFormatter(Formatter):
    def format(self):
        self._format_child() # <func_hdr>
        if self._get_child_type() == 'preproc':
            self._write_nl()
            while self._get_child_type() == 'preproc':
                self._format_child() # <preproc>
                self._write_nl()
        self._format_child() # <func_body>
        self._write_nl()

class FuncHdrFormatter(Formatter):
    def format(self):
        self._format_child() # <func>, <hook>, or <event>


class FuncHdrVariantFormatter(Formatter):
    def format(self):
        if self._get_child_type() == 'redef':
            self._format_child() # 'redef'
            self._write_sp()
        self._format_child() # 'function', 'hook', or 'event'
        self._write_sp()
        self._format_child() # <id>
        self._format_child() # <func_params>
        if self._get_child_type() == 'attr_list':
            self._write_sp()
            self._format_child() # <attr_list>


class FuncParamsFormatter(Formatter):
    def format(self):
        self._format_child() # '('
        if self._get_child_type() == 'formal_args':
            self._format_child() # <formal_args>
        self._format_child() # ')'
        if self._get_child_type() == ':':
            self._format_child() # ':'
            self._write_sp()
            self._format_child() # <type>


class FuncBodyFormatter(Formatter):
    def format(self):
        self._write_sp()
        self._format_child() # '{'
        if self._get_child_type() == 'stmt_list':
            self._write_nl()
            self._format_child(indent=True) # <stmt_list>
        else:
            self._write_sp()
        self._format_child() # '}'


class FormalArgsFormatter(Formatter):
    def format(self):
        while self._get_child_type() == 'formal_arg':
            self._format_child() # <formal_arg>
            if self._get_child():
                self._format_child() # ',' or ';'
                self._write_sp()


class FormalArgFormatter(Formatter):
    def format(self):
        self._format_child_range(2) # <id> ':'
        self._write_sp()
        self._format_child() # <type>
        if self._get_child_type() == 'attr_list':
            self._write_sp()
            self._format_child() # <attr_list>


class CaptureListFormatter(Formatter):
    def format(self):
        self._format_child() # '['
        while self._get_child_type() == 'capture':
            self._format_child() # <capture>
            if self._get_child_type() == ',':
                self._format_child() # ','
                self._write_sp()
        self._format_child() # ']'
        self._write_sp()


class StmtFormatter(TypedInitializerFormatter):
    def __init__(self, script, node, ostream, indent=0, parent=None):
        super().__init__(script, node, ostream, indent, parent)

        # It's an if/for/while statement with a "{ ... }" block
        self.has_curly_block = False

    def _child_is_curly_stmt(self):
        """Looks ahead to see if the upcoming statement is { ... }.
        This decides surrounding whitespace in some situations below.
        """
        try:
            res = self._get_child().children[0].type == '{'
            if res:
                self.has_curly_block = True
            return res
        except (AttributeError, IndexError):
            return False

    def _write_sp_or_nl(self):
        """Writes separator based on whether we have a curly block."""
        if self.has_curly_block:
            self._write_sp()
        else:
            self._write_nl()

    def _format_block(self):
        """Helper for formatting a statement that may be an { ... } block."""
        curly = self._child_is_curly_stmt()
        self._write_sp_or_nl()
        self._format_child(indent=not curly) # <stmt>
        if curly:
            self._write_nl()

    def _format_when(self):
        # XXX when-timeout is almost redundant with if-else, though the timeout
        # block differs in that curly braces are required. Could refactor a bit.
        self._format_child() # 'when'
        self._write_sp()
        self._format_child() # '('
        self._write_sp()
        self._format_child() # <expr>
        self._write_sp()
        self._format_child() # ')'

        curly = self._child_is_curly_stmt()
        self._write_sp_or_nl()
        self._format_child(indent=not curly) # <stmt>

        if self._get_child_type() == 'timeout':
            if curly:
                self._write_sp()
            self._format_child() # 'timeout'
            self._write_sp()
            self._format_child() # <expr>
            self._write_sp()
            self._format_child() # '{'
            self._write_nl()
            if self._get_child_type() == 'stmt_list':
                self._format_child(indent=True) # <stmt_list>
            self._format_child() # '}'
            self._write_nl()
        elif curly:
            self._write_nl() # Finish the when's curly block.

    def format(self):
        # Statements aren't currently broken down into more specific symbol
        # types in the grammer, so we just examine their beginning.
        start = self._get_child_type()
        if start == '{':
            self._format_child() # '{'
            if self._get_child_type() == 'stmt_list':
                self._write_nl()
                self._format_child(indent=True)
            else:
                self._write_sp()
            self._format_child() # '}'

        elif start in ['print', 'event']:
            self._format_child() # 'print'/'event'
            self._write_sp()
            self._format_child_range(2) # <expr_list>/<event_hdr> ';'
            self._write_nl()

        elif start == 'if':
            self._format_child() # 'if'
            self._write_sp()
            self._format_child() # '('
            self._write_sp()
            self._format_child() # <expr>
            self._write_sp()
            self._format_child() # ')'

            # We need to track whether the subsequent statement is a
            # curly-braces block for several reasons: we write a newline now
            # only when it's not, and we need to indent only if it's not such a
            # block (because it takes care of it internally). An else-block also
            # requires treatment.

            curly = self._child_is_curly_stmt()
            self._write_sp_or_nl()
            self._format_child(indent=not curly) # <stmt>

            if self._get_child_type() == 'else':
                if curly:
                    self._write_sp()
                self._format_child() # 'else'
                curly  = self._child_is_curly_stmt()
                self._write_sp_or_nl()
                self._format_child(indent=not curly) # <stmt>
                if curly:
                    self._write_nl()
            elif curly:
                self._write_nl() # Finish the if's curly block.

        elif start == 'switch':
            self._format_child() # 'switch'
            self._write_sp()
            self._format_child() # <expr>
            self._write_sp()
            self._format_child() # '{'
            if self._get_child_type() == 'case_list':
                self._format_child(indent=True) # <case_list>
            else:
                self._write_sp()
            self._format_child() # '}'
            self._write_nl()

        elif start == 'for':
            self._format_child() # 'for'
            self._write_sp()
            self._format_child() # '('
            self._write_sp()
            if self._get_child_type() == '[':
                self._format_child() # '['
                while self._get_child_type() != ']':
                    self._format_child() # <id>
                    if self._get_child_type() == ',':
                        self._format_child() # ','
                        self._write_sp()
                self._format_child() # ']'
            else:
                self._format_child() # <id>

            while self._get_child_type() == ',':
                self._format_child() # ','
                self._write_sp()
                self._format_child() # <id>
            self._write_sp()
            self._format_child() # 'in'
            self._write_sp()
            self._format_child() # <expr>
            self._write_sp()
            self._format_child() # ')'
            self._format_block() # <stmt>

        elif start == 'while':
            self._format_child() # 'while'
            self._write_sp()
            self._format_child() # '('
            self._write_sp()
            self._format_child() # <expr>
            self._write_sp()
            self._format_child() # ')'
            self._format_block() # <stmt>

        elif start in ['next', 'break', 'fallthrough']:
            self._format_child_range(2) # loop control statement, ';'
            self._write_nl()

        elif start == 'return':
            self._format_child() # 'return'
            # There's also an optional 'return" before when statements,
            # so detour in that case and be done.
            if self._get_child_type() == 'when':
                self._write_sp()
                self._format_when()
                return
            if self._get_child_type() == 'expr':
                self._write_sp()
                self._format_child() # <expr>
            self._format_child() # ';'
            self._write_nl()

        elif start in ['add', 'delete']:
            self._format_child() # set management
            self._write_sp()
            self._format_child_range(2) # <expr> ';'
            self._write_nl()

        elif start in ['local', 'const']:
            self._format_child() # 'local'/'const'
            self._write_sp()
            self._format_child() # <id>
            self._format_typed_initializer()
            self._format_child() # ';'
            self._write_nl()

        elif start == 'when':
            self._format_when()

        elif start == 'index_slice':
            self._format_child() # <index_slice>
            self._write_sp()
            self._format_child() # '='
            self._write_sp()
            self._format_child_range(2) # <expr> ';'
            self._write_nl()

        elif start == 'expr':
            self._format_child_range(2) # <expr> ';'
            self._write_nl()

        elif start == 'preproc':
            self._format_child() # <preproc>
            self._write_nl()

        elif start == ';':
            self._format_child() # ';'
            self._write_nl()


class ExprListFormatter(Formatter):
    def format(self):
        while self._get_child_type() == 'expr':
            self._format_child() # <expr>
            if self._get_child():
                self._format_child() # ','
                self._write_sp()


class CaseListFormatter(Formatter):
    def format(self):
        while self._get_child():
            if self._get_child_type() == 'case':
                self._format_child() # 'case'
                self._write_sp()
                self._format_child_range(2) # <expr_list> or <case_type_list>, ':'
            else:
                self._format_child_range(2) # 'default' ':'
            self._write_nl()
            if self._get_child_type() == 'stmt_list':
                self._format_child(indent=True) # <stmt_list>


class CaseTypeListFormatter(Formatter):
    def format(self):
        while self._get_child_type() == 'type':
            self._format_child() # 'type'
            self._write_sp()
            self._format_child() # <type>
            if self._get_child_type() == 'as':
                self._write_sp()
                self._format_child() # 'as'
                self._write_sp()
                self._format_child() # <id>
            if self._get_child_type() == ',':
                self._format_child() # ','
                self._write_sp()


class EventHdrFormatter(Formatter):
    def format(self):
        self._format_child() # <id>
        self._format_child() # '('
        if self._get_child_type() == 'expr_list':
            self._format_child() # <expr_list>
        self._format_child() # ')'


class ExprFormatter(SpaceSeparatedFormatter):
    # Like statments, expressions aren't currently broken into specific symbol
    # types, so we parse into them to identify how to layout them.
    def format(self):
        ct1, ct2, ct3 = [self._get_child_type(offset=n) for n in (0,1,2)]

        if ct1 == 'expr' and ct2 in ['[', 'index_slice', '$']:
            while self._get_child():
                self._format_child()

        elif ct1 in ['|', '++', '--', '!', '~', '-', '+']:
            # No space when those operators are involved
            while self._get_child():
                self._format_child()

        elif ct1 == 'expr' and ct2 == '!' and ct3 == 'in':
            self._format_child() # <expr>
            self._write_sp()
            self._format_child_range(2) # '!in'
            self._write_sp()
            self._format_child() # <expr>

        elif ct1 == '[':
            self._format_child() # '['
            if self._get_child_type() == 'expr_list':
                self._format_child() # <expr_list>
            else:
                self._write_sp()
            self._format_child() # ']

        elif ct1 == '$':
            self._format_child_range(2) # '$'<id>
            self._write_sp()
            super().format() # Handle rest space-separated

        elif ct1 == '(':
            self._format_child_range(3) # '(' <expr> ')'

        elif ct1 == 'copy':
            self._format_child_range(4) # 'copy' '(' <expr> ')'

        elif ct2 == '?$':
            self._format_child_range(3) # <expr> '$?' <expr>

        elif ct2 == '(':
            # initializers such as table(...)
            self._format_child_range(2) # 'table(' etc
            if self._get_child_type() == 'expr_list':
                self._format_child()
            self._format_child() # ')'
            if self._get_child_type() == 'attr_list':
                self._write_sp()
                self._format_child()

        else:
            # Fall back to simple space-separation
            super().format()



class NlFormatter(Formatter):
    def format(self):
        node = self._node
        # If this has another newline after it, do nothing.
        if node.next_cst_sibling and node.next_cst_sibling.is_nl():
            return

        # Write a single newline for any sequence of blank lines in the input,
        # unless this sequence is at the beginning or end of the sequence.

        if not node.next_cst_sibling or node.next_cst_sibling.type == '}':
            # It's at the end of a sequence.
            return

        if node.prev_cst_sibling and node.prev_cst_sibling.is_nl():
            # It's a sequence.
            while node.prev_cst_sibling and node.prev_cst_sibling.is_nl():
                node = node.prev_cst_sibling

            if node.prev_cst_sibling and node.prev_cst_sibling.type != '{':
                # There's something other than whitspace before this sequence.
                self._write_nl(force=True)


class MinorCommentFormatter(Formatter):
    def format(self):
        node = self._node
        # There's something before us and it's not a newline, then
        # separate this comment from it with a space:
        if node.prev_cst_sibling and not node.prev_cst_sibling.is_nl():
            self._write_sp()

        self._format_token() # Write comment itself

        # If there's nothing or a newline before us, then this comment spans the
        # whole line and we write a regular newline. Otherwise we indicate that
        # this newline is likely an interruption to the current line.
        if node.prev_cst_sibling is None or node.prev_cst_sibling.is_nl():
            self._write_nl()
        else:
            self._write_nl(is_midline=True)


class ZeekygenHeadCommentFormatter(TypechangeFormatter):
    def format(self):
        self._format_token()
        self._write_nl()
        if self._next_sibling_typechange():
            self._write_nl()


class ZeekygenNextCommentFormatter(Formatter):
    def format(self):
        self._format_token()
        self._write_nl()


class ZeekygenPrevCommentFormatter(Formatter):
    """A formatter for Zeekygen comments that refer to earlier items (##<)."""
    def __init__(self, script, node, ostream, indent=0, parent=None):
        super().__init__(script, node, ostream, indent, parent)
        self.column = 0 # Column at which this comment lives

    def format(self):
        # Handle indent explicitly here because of the transparent handling of
        # comments. If we don't call this, nothing may force the indent for the
        # comment if it's the only thing on the line.
        self._write_indent()

        if isinstance(self.prev, ZeekygenPrevCommentFormatter):
            self._write_sp(self.prev.column - self._ostream.get_column())
        else:
            self._write_sp()

        # Record the output column so potential subsequent Zeekygen
        # comments can use the same alignment.
        self.column = self._ostream.get_column()

        # Write comment itself
        self._format_token()

        # If this has another ##< comment after it, write the newline.
        try:
            if (self._node.next_cst_sibling.is_nl() and
                self._node.next_cst_sibling.next_cst_sibling.is_zeekygen_prev_comment()):
                self._write_nl()
        except AttributeError:
            pass


# ---- Explicit mappings for grammar symbols to formatters ---------------------
#
# NodeMapper.get() retrieves formatters not listed here by mapping symbol
# names to class names, e.g. module_decl -> ModuleDeclFormatter.

Formatter.register('preproc', LineFormatter)

Formatter.register('const_decl', GlobalDeclFormatter)
Formatter.register('global_decl', GlobalDeclFormatter)
Formatter.register('option_decl', GlobalDeclFormatter)
Formatter.register('redef_decl', GlobalDeclFormatter)

Formatter.register('func', FuncHdrVariantFormatter)
Formatter.register('hook', FuncHdrVariantFormatter)
Formatter.register('event', FuncHdrVariantFormatter)

Formatter.register('capture', SpaceSeparatedFormatter)
Formatter.register('attr_list', SpaceSeparatedFormatter)
Formatter.register('interval', SpaceSeparatedFormatter)

Formatter.register('nullnode', NullFormatter)
