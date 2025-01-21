"""Components implementing the CLI interface."""

import io
import os
import pathlib
import sys

import tree_sitter
import tree_sitter_zeek

from .error import FileError, ParserError
from .formatter import Formatter
from .node import Node
from .output import OutputStream


class Script:
    """Representation of a single Zeek script file."""

    def __init__(self, file):
        """Script constructor.

        The file argument can be a string providing a file name, a pathlib.Path
        providing a file name, or a file-like object. The filename/path "-"
        implies stdin.
        """
        self.file = file
        self.source = None  # The file's full content, once parsed
        self.ts_tree = None  # The tree-sitter parse tree for the script
        self.root = None  # The root node of our cloned (and malleable) tree

    def parse(self):
        """Parses the script and creates the internal concrete syntax tree.

        Raises zeekscript.FileError when the input file cannot be read, and
        zeekscript.ParserError when the file didn't parse at all.

        Returns True of parsing succeeded throughout, and False if the resulting
        parse tree has erroneous nodes.
        """
        try:
            if isinstance(self.file, (str | pathlib.Path)):
                if str(self.file) == "-":
                    # tree-sitter expects bytes, not strings, as input.
                    self.source = sys.stdin.read().encode("UTF-8")
                else:
                    with open(self.file, "rb") as hdl:
                        self.source = hdl.read()
            else:
                # Assume file-like object. Could check for the various io.*Base
                # types here, though we'll need to error out anyway if it's not
                # a file-like thing.
                self.source = self.file.read()
                # Need to ensure we have bytes now:
                if isinstance(self.source, str):
                    self.source = self.source.encode("UTF-8")
        except OSError as err:
            raise FileError(str(err)) from err

        parser = tree_sitter.Parser(tree_sitter.Language(tree_sitter_zeek.language()))
        self.ts_tree = parser.parse(self.source)

        if self.ts_tree is None or self.ts_tree.root_node is None:
            # This is a hard parse error and we need to bail. Smaller errors get
            # reported on individual nodes in the resulting tree, and we can
            # keep going.
            raise ParserError("cannot parse script")

        self._clone_tree()
        self._patch_tree()

        return not self.has_error()

    def has_error(self):
        """Predicate, returns True when parsing identified problems.

        Problems can be of several kinds: for grammatical errors, tree nodes
        have type string "ERROR". Missing nodes have their is_missing bit set,
        and for subtler errors their has_error bit is set. This function
        reports True when any of these conditions hold.
        """
        assert self.root is not None, "call Script.parse() before Script.has_error()"

        # Could cache this result while we don't support tree modifications
        for node, _ in self.root.traverse():
            if node.type == "ERROR" or node.is_missing or node.has_error:
                return True

        return False

    def get_error(self):
        """Return offending content line in the script, plus error message

        This traverses the parse tree and returns a triplet of:

        - The line on which a parse tree node has type "ERROR", has its
          is_missing bit set, or has its has_error bit set and has no children
          that also have it set. (This last part filters out the upward
          propagation of the error state through the tree, selecting the node
          that introduced the error.)

        - That line's number in the script.

        - An error message string that tries to explain the problem.
        """
        assert self.root is not None, "call Script.parse() before Script.get_error()"

        line, lineno, msg = None, None, None

        for node, _ in self.root.traverse():
            snippet = self.source[node.start_byte : node.end_byte]
            max_snippet_length = 50
            if len(snippet) > max_snippet_length:
                snippet = snippet[:max_snippet_length] + b"[...]"

            if node.type == "ERROR":
                msg = (
                    f"cannot parse line {node.start_point[0]}, col {node.start_point[1]}: "
                    f'"{snippet.decode("UTF-8")}"'
                )
            elif node.is_missing:
                msg = (
                    f'missing grammar node "{node.type}" on '
                    f"line {node.start_point[0]}, col {node.start_point[1]}"
                )
            elif node.has_error and (
                not node.children or not any(kid.has_error for kid in node.children)
            ):
                msg = (
                    f'grammar node "{node.type}" has error on '
                    f"line {node.start_point[0]}, col {node.start_point[1]}"
                )
            else:
                continue

            line = self.source.split(Formatter.NL)[node.start_point[0]]
            line = line.decode("UTF-8")
            lineno = node.start_point[0]
            break

        return line, lineno, msg

    def traverse(self, include_cst=False):
        """Depth-first iterator for the script's syntax tree.

        This yields a tuple (node, nesting) with a Node instance (ours, not
        tree_sitter's) and its integer nesting level: the root has nesting level
        0, its children have nesting level 1, their children have nesting level
        2, etc.
        """
        assert self.root is not None, "call Script.parse() before Script.traverse()"

        yield from self.root.traverse(include_cst)

    def __getitem__(self, key):
        """Accessor to the script source text.

        This simplifies accessing specific text chunks in the source.
        """
        assert self.root is not None, "call Script.parse() before accessing content"

        return self.source.__getitem__(key)

    def get_content(self, start_byte=None, end_byte=None):
        """Returns a region of this script's content.

        By default, this returns the entire content. start_byte and end_byte, if
        provided, are numerical byte offsets in the script, behaving as usual
        indices in slice notation.
        """
        assert self.root is not None, "call Script.parse() before Script.get_content()"

        return self.source[start_byte:end_byte]

    def format(self, output=None, enable_linebreaks=True):
        """Formats the script and writes out the result.

        The output destination can be one of three things: a filename, a file
        object, or None, which means stdout. enable_linebreaks, True by default,
        controls whether to use linebreaks at all.
        """
        assert self.root is not None, "call Script.parse() before Script.format()"

        def do_format(out):
            with OutputStream(out, enable_linebreaks) as ostream:
                fclass = Formatter.lookup(self.root)
                formatter = fclass(self, self.root, ostream)
                formatter.format()

        if output is None:
            do_format(sys.stdout)
        elif isinstance(output, str):
            with open(output, "wb") as ostream:
                do_format(ostream)
        else:
            # output should be a file-like object
            do_format(output)

    def write_tree(self, output=None, node_stringifier=None, include_cst=False):
        """Writes the script's parse tree to the given output.

        The output destination works as for Script.format().

        node_stringifier, if supplied, controls how a given tree node gets
        rendered to a string. It is a function that takes three arguments: a
        zeekscript.Node, the node's nesting level in the tree (an integer), and
        this script instance. When omitted, the function defaults to an internal
        implementation.

        include_cst controls whether the rendered tree shows only AST nodes
        (i.e., "proper" members of the grammar, excluding TS's "extra" nodes
        such as newlines and comments) or AST and CST nodes.
        """

        def node_str(node, nesting, script):
            content = ""
            if node.is_named:
                # Cap the amount of script payload we show ...
                content = script.source[node.start_byte : node.end_byte]
                maxlen = 100
                extra = ""

                if len(content) > maxlen:
                    extra = f"[+{len(content) - maxlen}]"
                    content = content[:maxlen]

                # ... and render it such that we get backslash-escapes.
                content = str(repr(content.decode("ascii", "ignore"))) + extra

            # CST node rendering. This only applies when the tree traversal
            # actually produces these nodes.
            cst_indicator = ""
            if not node.is_ast:
                if node.is_cst_prev_node:
                    cst_indicator = "v "
                if node.is_cst_next_node:
                    cst_indicator = "^ "

            errors = []
            err = ""
            if node.has_error:
                errors.append("error")
            if node.is_missing:
                errors.append("missing")
            if errors:
                err += "[" + ", ".join(errors) + "] "

            return (
                " " * (4 * nesting)
                + (
                    f"{cst_indicator}{node.type} "
                    f"({node.start_point[0]}.{node.start_point[1]},"
                    f"{node.end_point[0]}.{node.end_point[1]}) "
                    f"{err}{content}"
                ).rstrip()
            )

        def do_traverse(ostream):
            def encode(x):
                if isinstance(ostream, io.TextIOBase):
                    return x + os.linesep
                else:
                    return x.encode("UTF-8") + Formatter.NL

            stringifier = node_stringifier if node_stringifier else node_str
            for node, nesting in self.traverse(include_cst):
                ostream.write(encode(stringifier(node, nesting, self)))

        if output is None:
            do_traverse(sys.stdout)
        elif isinstance(output, str):
            with open(output, "w", encoding="utf-8") as ostream:
                do_traverse(ostream)
        else:
            # output should be a file-like object
            do_traverse(output)

    def _clone_tree(self):
        """Deep-copy the TS tree to one consisting of zeekscript.Node instances.

        The input is self.ts_tree, the output is our tree's root node at self.root.
        """
        # We don't operate directly on the TS tree, for two reasons: first, the
        # TS tree isn't malleable from Python. We can alter the structure of our
        # own tree freely, which helps during formatting. Second, we encode
        # additional metadata in the node structure.

        def make_nullnode():
            node = Node()
            node.is_named = True
            node.type = "nullnode"
            node.is_ast = True
            return node

        def make_node(node):
            new_node = Node()

            # Copy basic TS properties
            new_node.start_byte, new_node.end_byte = node.start_byte, node.end_byte
            new_node.start_point, new_node.end_point = node.start_point, node.end_point
            new_node.is_named = node.is_named
            new_node.is_missing = node.is_missing
            new_node.has_error = node.has_error
            new_node.type = node.type

            # Mark the node as AST-only if it's not a newline or comment. Those
            # are extras (in TS terminology) that occur anywhere in the tree.
            # Would be nice if we didn't need to itemize these manually.
            if node.type != "nl" and not node.type.endswith("_comment"):
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

            # Corner case: if the new node has no AST children (only comments in
            # a statement block, for example), then create a dummy "null" node
            # as AST node to house those nodes. This node maps to NullFormatter,
            # so will not produce any output.
            if new_children and not new_node.children:
                nullnode = make_nullnode()
                new_node.children.append(nullnode)
                new_children.append(nullnode)

            # Now figure out where to "cut" the sequence of CST nodes around the
            # AST nodes. Rules:
            #
            # - After an AST node we only allow a sequence of Zeekygen
            #   prev comments, or any regular comment up to the next newline.
            #
            # - The rest gets associated with the subsequent AST node, unless
            #   there isn't one.

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

                elif (
                    child.is_nl()
                    and last_child
                    and (
                        # Accept newline if it ends a comment or follows an error node.
                        last_child.is_comment() or last_child.is_error()
                    )
                ):
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

            # Final edits: if the node has any ERROR nodes as children, unhook
            # them from the sibling linkage and the parent's children list, to
            # ensure the formatters' child node reasoning remains sound.

            # Corner case: the new node only has ERROR children. We need to add
            # an AST null node to have something to link the errors to.
            if new_node.children and all(
                child.type == "ERROR" for child in new_node.children
            ):
                new_node.children.append(make_nullnode())
                new_node.children[-2].next_sibling = new_node.children[-1]
                new_node.children[-1].prev_sibling = new_node.children[-2]

            pending_errors = []

            for child in new_node.children:
                if child.type == "ERROR":
                    pending_errors.append(child)
                    continue

                new_node.nonerr_children.append(child)

                if pending_errors:
                    child.prev_error_siblings = pending_errors
                    pending_errors = []

            if pending_errors:
                new_node.nonerr_children[-1].next_error_siblings = pending_errors

            return new_node

        self.root = make_node(self.ts_tree.root_node)

    def _patch_tree(self):
        """Tweak the syntax tree to simplify formatting."""
        # Move any dangling CST nodes (such as comments) down into the tree so
        # they directly child-follow the node that they refer to. For example,
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
        # This simplifies reasoning about such comments in the context of the
        # directly preceding node, not some abstraction thereof.
        #
        for node, _ in self.traverse():
            if node.next_cst_siblings and node.children:
                node.next_cst_sibling = None

                if node.children[-1].next_cst_siblings:
                    node.children[-1].next_cst_siblings[
                        -1
                    ].next_cst_sibling = node.next_cst_siblings[0]
                    node.next_cst_siblings[0].prev_cst_sibling = node.children[
                        -1
                    ].next_cst_siblings[-1]

                node.children[-1].next_cst_siblings += node.next_cst_siblings
                node.next_cst_siblings = []
