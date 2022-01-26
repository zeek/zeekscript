import os
import pathlib
import sys

try:
    # We use the following helpers when available (starting with Python 3.9) to
    # locate the TS language .so shipped with the package. Without, we fall back
    # to using local path navigation and hope for the best.
    # https://importlib-resources.readthedocs.io/en/latest/using.html#file-system-or-zip-file
    from importlib.resources import files, as_file
except ImportError:
    def files(_):
        return pathlib.Path(os.path.dirname(os.path.realpath(__file__)))
    def as_file(source):
        return source

try:
    import tree_sitter
except ImportError:
    print('This package requires the tree_sitter package.')
    sys.exit(1)

__version__ = '0.1.1'
__all__ = ['error', 'formatter', 'node']

from .error import *
from .formatter import *
from .node import *


class OutputStream:
    """A column-aware, trailing-whitespace-stripping wrapper for output streams."""
    def __init__(self, ostream):
        self._ostream = ostream
        self._col = 0 # 0-based column the next character goes into.
        self._space_indent = 0

    def set_space_indent(self, num):
        self._space_indent = num

    def write(self, data):
        for chunk in data.splitlines(keepends=True):
            if chunk.endswith(Formatter.NL):
                # Remove any trailing whitespace
                chunk = chunk.rstrip() + Formatter.NL

            try:
                if self._ostream == sys.stdout:
                    # Must write string here, not bytes. An alternative is to
                    # use _ostream.buffer -- not sure how portable that is.
                    self._ostream.write(chunk.decode('UTF-8'))
                else:
                    self._ostream.write(chunk)
            except BrokenPipeError:
                #  https://docs.python.org/3/library/signal.html#note-on-sigpipe:
                devnull = os.open(os.devnull, os.O_WRONLY)
                os.dup2(devnull, sys.stdout.fileno())
                sys.exit(1)

            self._col += len(chunk)
            if chunk.endswith(Formatter.NL):
                self._col = 0

    def write_space_indent(self):
        if self._space_indent > 0:
            self.write(b' ' * 4 * self._space_indent)

    def get_column(self):
        return self._col


class Parser:
    """tree_sitter.Parser abstraction that takes care of loading the TS Zeek language."""
    TS_PARSER = None # A tree_sitter.Parser singleton

    def __init__(self):
        Parser.load_parser()

    def parse(self, text):
        """Returns a tree_sitter.Tree for the given script text.

        This tree may have errors, as indicated via its root node's has_error
        flag.
        """
        return Parser.TS_PARSER.parse(text)

    @classmethod
    def load_parser(cls):
        if cls.TS_PARSER is None:
            # Python voodoo to access the bindings library contained in this
            # package regardless of how we're loading the package. Details:
            # https://importlib-resources.readthedocs.io/en/latest/using.html#file-system-or-zip-file
            source = files(__package__).joinpath('zeek-language.so')
            with as_file(source) as lib:
                zeek_lang = tree_sitter.Language(str(lib), 'zeek')
            cls.TS_PARSER = tree_sitter.Parser()
            cls.TS_PARSER.set_language(zeek_lang)


class Script:
    """Representation of a single Zeek script file."""
    def __init__(self, fname):
        self.name = fname # The file name to read the Zeek script from
        self.source = None # The file's full content
        self.ts_tree = None # The tree-sitter parse tree for the script
        self.root = None # The root node of our cloned (and malleable) tree

    def parse(self):
        """Parses the script and creates the internal concrete syntax tree.

        Raises zeekscript.FileError when the input file cannot be read, and
        zeekscript.ParserError when the file didn't parse correctly.
        """
        try:
            # tree-sitter expects bytes, not strings, as input.
            if self.name == '-':
                self.source = sys.stdin.read().encode('UTF-8')
            else:
                with open(self.name, 'rb') as hdl:
                    self.source = hdl.read()
        except OSError as err:
            raise FileError(str(err)) from err

        self.ts_tree = Parser().parse(self.source)
        if self.ts_tree.root_node and self.ts_tree.root_node.has_error:
            self._raise_parser_error()

        self._clone_tree()
        self._patch_tree()

    def traverse(self, include_cst=False):
        """Depth-first iterator for the script's syntax tree.

        This yields a tuple (node, indent) with a Node instance (ours, not
        tree_sitter's) and its indentation level.
        """
        assert self.root is not None, 'call Script.parse() before Script.traverse()'
        for node, indent in self._visit(self.root, include_cst):
            yield node, indent

    def __getitem__(self, key):
        """Accessor to the script source text.

        This simplifies accessing specific text chunks in the source.
        """
        return self.source.__getitem__(key)

    def format(self, output=None):
        """Formats the script and writes out the result.

        The output destination can be one of three things: a filename, a file
        object, or None, which means stdout.
        """
        assert self.root is not None, 'call Script.parse() before Script.format()'

        def do_format(ostream):
            fclass = Formatter.lookup(self.root)
            formatter = fclass(self, self.root, OutputStream(ostream))
            formatter.format()

        if output is None:
            do_format(sys.stdout)
        elif isinstance(output, str):
            with open(output, 'wb') as ostream:
                do_format(ostream)
        else:
            # output should be a file-like object
            do_format(output)

    def _visit(self, node, include_cst=False):
        """A tree-traversing generator.

        Yields a tuple of (node, indentation depth) for every visited
        node. Works for zeekscript Nodes as well as tree-sitter's tree nodes.
        """
        queue = [(node, 0)]
        while queue:
            node, indent = queue.pop(0)

            # If the caller wants the CST, we now need to iterate any
            # preceeding/succeeding CST nodes this AST nodes has stored:
            if include_cst:
                for cst_node in node.prev_cst_siblings:
                    yield cst_node, indent

            yield node, indent

            if include_cst:
                for cst_node in node.next_cst_siblings:
                    yield cst_node, indent

            for child in reversed(node.children):
                queue.insert(0, (child, indent+1))

    def _raise_parser_error(self):
        """Raises zeekscript.ParserError with info on trouble in the parse tree."""

        # There's no succinct error summary via tree-sitter, so compute
        # one. Traverse the tree to call out the troubled node -- it will
        # either have type "ERROR" or its is_missing flag is set.

        msg = 'cannot parse document'
        line = None

        for node, _ in self._visit(self.ts_tree.root_node):
            if node.type != 'ERROR' and not node.is_missing:
                continue

            snippet = self.source[node.start_byte:node.end_byte]
            if len(snippet) > 50:
                snippet = snippet[:50] + b'[...]'

            if node.type == 'ERROR':
                msg = 'cannot parse line {}, col {}: "{}"'.format(
                    node.start_point[0], node.start_point[1],
                    snippet.decode('UTF-8'))
            elif node.is_missing:
                snippet = self.source[node.start_byte:node.end_byte]
                if len(snippet) > 50:
                    snippet = snippet[:50] + b'[...]'
                msg = 'missing grammer node "{}" on line {}, col {}'.format(
                    node.type, node.start_point[0], node.start_point[1])

            line = self.source.split(Formatter.NL)[node.start_point[0]]
            line = line.decode('UTF-8')
            break

        raise ParserError(msg, line)

    def _clone_tree(self):
        """Deep-copy the TS tree to one consisting of zeekscript.Node instances.

        The input is self.ts_tree, the output is our tree's root node at self.root.
        """
        # We don't operate directly on the TS tree, for two reasons: first, the
        # TS tree isn't malleable from Python. We can alter the structure of our
        # own tree freely, which helps during formatting. Second, we encode
        # additional metadata in the node structure.
        def make_node(node):
            new_node = Node()

            # Copy basic TS properties
            new_node.start_byte, new_node.end_byte = node.start_byte, node.end_byte
            new_node.start_point, new_node.end_point = node.start_point, node.end_point
            new_node.is_named = node.is_named
            new_node.is_missing = node.is_missing
            new_node.has_error = node.has_error
            new_node.type = node.type

            # Mark the node as AST-only if it's not a newline or comment.  Those
            # are extras (in TS terminology) that occur anywhere in the tree.
            if node.type != 'nl' and not node.type.endswith('_comment'):
                new_node.is_ast = True

            new_children = []

            # Set up state for all of the node's children. This recurses
            # so we build up our own tree.
            for child in node.children:
                new_child = make_node(child)

                # Fully link CST nodes so they can reason about their neighbors
                if new_children:
                    new_children[-1].next_cst_sibling = new_child
                    new_child.prev_cst_sibling = new_children[-1]

                new_children.append(new_child)
                new_child.parent = new_node

                # Only register AST nodes directly in the child list. This
                # allows expected indices, such as the expr in '[' <expr> ']' to
                # be the second child, to function regardless of comments or
                # newlines.
                if new_child.is_ast:
                    new_node.children.append(new_child)
                    if len(new_node.children) > 1:
                        new_node.children[-2].next_sibling = new_node.children[-1]
                        new_node.children[-1].prev_sibling = new_node.children[-2]

            # Corner case: if we have no AST nodes (only comments in a statement
            # block, for example), then create a dummy "null" node as AST node
            # to house those elements. This node has a null formatter so will
            # not produce any output.
            if new_children and not new_node.children:
                nullnode = Node()
                nullnode.is_named = True
                nullnode.type = 'nullnode'
                nullnode.is_ast = True
                new_node.children.append(nullnode)
                new_children.append(nullnode)

            # Now figure out where to "cut" the sequence of CST nodes around the
            # AST nodes. After an AST node we only allow a sequence of Zeekygen
            # prev comments, or any regular comment up to the next newline. The
            # rest gets associated with the subsequent AST node, unless there
            # isn't one.

            ast_node = None
            ast_nodes_remaining = len(new_node.children)
            prevs = []
            last_child = None

            for child in new_children:
                if ast_nodes_remaining == 0:
                    ast_node.next_cst_siblings.append(child)
                    child.ast_parent = ast_node
                    child.is_cst_next_node = True

                elif child.is_ast:
                    ast_nodes_remaining -= 1
                    child.prev_cst_siblings = prevs
                    for prev in prevs:
                        prev.ast_parent = child
                    prevs = []
                    ast_node = child

                elif not ast_node:
                    prevs.append(child)
                    child.is_cst_prev_node = True

                elif child.is_zeekygen_prev_comment():
                    # Accept ##< comments. Newlines control how often we do so.
                    ast_node.next_cst_siblings.append(child)
                    child.is_cst_next_node = True
                    child.ast_parent = ast_node

                elif child.is_minor_comment() and last_child and last_child.is_ast:
                    # Accept a minor comment if it directly follows the AST node.
                    ast_node.next_cst_siblings.append(child)
                    child.is_cst_next_node = True
                    child.ast_parent = ast_node

                elif child.is_nl() and last_child and last_child.is_comment():
                    ast_node.next_cst_siblings.append(child)
                    child.is_cst_next_node = True
                    child.ast_parent = ast_node

                else:
                    # Break the chain of CST nodes: this child becomes the start
                    # of the prev CST nodes for the next AST.
                    ast_node = None
                    prevs = [child]
                    child.is_cst_prev_node = True

                last_child = child

            return new_node

        self.root = make_node(self.ts_tree.root_node)

    def _patch_tree(self):
        """Tweak the syntax tree to simplify formatting."""
        # Move any dangling CST nodes (such as comments) down into the tree so
        # they directly child-follow the node that they refers to. For example,
        # this turns ...
        #
        #       type (138.11,138.14)
        #           int (138.11,138.14)
        #           ; (138.14,138.15)
        #       zeekygen_prev_comment (138.19,139.0) '##< A comment explaining the int\n'
        #       zeekygen_prev_comment (139.19,140.0) '##< continuing here.\n'
        #
        # ... into this:
        #
        #       type (138.11,138.14)
        #           int (138.11,138.14)
        #           ; (138.14,138.15)
        #           zeekygen_prev_comment (138.19,139.0) '##< A comment explaining the int\n'
        #           zeekygen_prev_comment (139.19,140.0) '##< continuing here.\n'
        #
        for node, _ in self.traverse():
            if node.next_cst_siblings and node.children:
                node.next_cst_sibling = None

                if node.children[-1].next_cst_siblings:
                    node.children[-1].next_cst_siblings[-1].next_cst_sibling = node.next_cst_siblings[0]
                    node.next_cst_siblings[0].prev_cst_sibling = node.children[-1].next_cst_siblings[-1]

                node.children[-1].next_cst_siblings += node.next_cst_siblings
                node.next_cst_siblings = []
