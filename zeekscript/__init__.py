import os
import sys

from importlib.resources import files, as_file

try:
    import tree_sitter
except ImportError:
    print('This package requires the tree_sitter package.')
    sys.exit(1)

__version__ = '0.1.1'
__all__ = ['error', 'formatter', 'tree']

from .error import *
from .formatter import *
from .tree import *


class OutputStream:
    """A column-aware wrapper for output streams."""
    def __init__(self, ostream):
        self._ostream = ostream
        self._col = 0 # 0-based column the next character goes into.

    def write(self, data):
        for chunk in data.splitlines(keepends=True):
            if chunk.endswith(b'\n'):
                # Remove any trailing whitespace
                chunk = chunk.rstrip() + b'\n'

            try:
                if self._ostream == sys.stdout:
                    self._ostream.buffer.write(chunk)
                else:
                    self._ostream.write(chunk)
            except BrokenPipeError:
                #  https://docs.python.org/3/library/signal.html#note-on-sigpipe:
                devnull = os.open(os.devnull, os.O_WRONLY)
                os.dup2(devnull, sys.stdout.fileno())
                sys.exit(1)

            self._col += len(chunk)
            if chunk.endswith(b'\n'):
                self._col = 0

    def get_column(self):
        return self._col


class Parser:
    """A class to abstract the tree_sitter.Parser."""

    TS_PARSER = None # A tree_sitter.Parser singleton

    def __init__(self):
        Parser.load_parser()

    def parse(self, text):
        """Returns a tree_sitter.Tree for the given script text."""
        return Parser.TS_PARSER.parse(text)

    @classmethod
    def load_parser(cls):
        if cls.TS_PARSER is None:
            # Python voodoo to access the contained bindings library
            # regardless of how we're loading the package. Details:
            # https://importlib-resources.readthedocs.io/en/latest/using.html#file-system-or-zip-file
            source = files(__package__).joinpath('zeek-language.so')
            with as_file(source) as lib:
                zeek_lang = tree_sitter.Language(lib, 'zeek')
            cls.TS_PARSER = tree_sitter.Parser()
            cls.TS_PARSER.set_language(zeek_lang)


class Script:
    """Representation of a single Zeek script file."""
    def __init__(self, fname, ofname=None):
        # The file name to read the Zeek script from
        self.name = fname
        # The file's full content
        self.source = None
        # The tree-sitter parse tree for the script
        self.tree = None
        # The root node of our cloned (and malleable) tree
        self.root = None
        # The output file name -- None if stdout
        self.ofname = ofname

    def parse(self):
        """Parses the script and creates the internal concrete syntax tree.

        Raises zeekscript.FileError when the input file cannot be read, and
        zeekscript.ParserError when the file didn't parse correctly.
        """
        try:
            with open(self.name, 'rb') as hdl:
                self.source = hdl.read()
        except OSError as err:
            raise FileError(str(err)) from err

        tree = Parser().parse(self.source)
        if tree.root_node and tree.root_node.has_error:
            # There's no succinct error summary via tree-sitter, so compute
            # one. Traverse the tree to call out the topmost ERROR node.
            for node, _ in self._visit(tree.root_node):
                if node.type == 'ERROR':
                    snippet = self.source[node.start_byte:node.end_byte]
                    if len(snippet) > 50:
                        snippet = snippet[:50] + b'[...]'
                    msg = 'cannot parse line {}, col {}: "{}"'.format(
                        # +1 here because tree-sitter counts lines from 0
                        # but few editors do. Less clear with columns.
                        node.start_point[0] + 1, node.start_point[1],
                        snippet.decode('UTF-8'))
                    line = self.source.split(b'\n')[node.start_point[0]]
                    line = line.decode('UTF-8')
                    raise ParserError(msg, line)

        self.tree = tree
        self._clone_tree()
        self._patchup_tree()

    def traverse(self):
        """Depth-first iterator for the script's syntax tree.

        This yields a tuple (node, indent) with a Node instance (ours, not
        tree_sitter's) and its indentation level.
        """
        assert self.root is not None, 'call Script.parse() before Script.traverse()'
        for node, indent in self._visit(self.root):
            yield node, indent

    def __getitem__(self, key):
        """Accessor to the script source text.

        This simplifies accessing specific text chunks in the source.
        """
        return self.source.__getitem__(key)

    def format(self):
        """Formats the script and writes out the result.

        The output destination is as configured in the constructor: stdout or to a file.
        """
        assert self.root is not None, 'call Script.parse() before Script.format()'
        with open(self.ofname, 'wb') if self.ofname else sys.stdout as ostream:
            fclass, _ = Formatter.lookup(self.root)
            formatter = fclass(self, self.root, OutputStream(ostream))
            formatter.format()

    def _visit(self, node):
        """A tree-traversing generator.

        Yields a tuple of (node, indentation depth) for every visited
        node. Works for zeekscript Nodes as well as tree-sitter's tree nodes.
        """
        queue = [(node, 0)]
        while queue:
            node, indent = queue.pop(0)
            yield node, indent
            for child in reversed(node.children):
                queue.insert(0, (child, indent+1))

    def _clone_tree(self):
        # The tree_sitter tree isn't malleable from Python. This clones it to a
        # tree of our own Node class that we can alter freely.
        def make_node(node):
            new_node = Node()
            new_node.start_byte, new_node.end_byte = node.start_byte, node.end_byte
            new_node.start_point, new_node.end_point = node.start_point, node.end_point
            new_node.is_named = node.is_named
            new_node.type = node.type

            for child in node.children:
                new_child = make_node(child)
                new_child.parent = new_node
                new_node.children.append(new_child)
                if len(new_node.children) > 1:
                    new_node.children[-2].next_sibling = new_node.children[-1]
                    new_node.children[-1].prev_sibling = new_node.children[-2]

            return new_node

        self.root = make_node(self.tree.root_node)

    def _patchup_tree(self):
        """Tweak the syntax tree to simplify our processing."""

        # Move any dangling zeekygen_prev_comments down into the tree so they
        # directly child-follow the node that the comment refers to. For
        # example, this turns ...
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
            idx = 0 # Iterate manually since not every iteration advances idx
            while idx < len(node.children):
                child = node.children[idx]
                # If this child is a ##< comment and the previous child has
                # children, move the comment down to the kid's kids.
                if (idx > 0 and child.type == 'zeekygen_prev_comment' and
                    node.children[idx-1].children):

                    # Cut out comment from sibling relationships:
                    child.prev_sibling.next_sibling = child.next_sibling
                    if child.next_sibling:
                        child.next_sibling.prev_sibling = child.prev_sibling

                    # Move comment to new location:
                    node.children.pop(idx)
                    node.children[idx-1].children.append(child)

                    # Adjust sibling relationship in new location:
                    if len(node.children[idx-1].children) > 1:
                        node.children[idx-1].children[-2].next_sibling = child
                        child.prev_sibling = node.children[idx-1].children[-2]
                else:
                    idx += 1
