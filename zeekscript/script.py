"""Components implementing the CLI interface."""

import io
import os
import pathlib
import sys
from collections.abc import Generator
from typing import IO, AnyStr, BinaryIO, TextIO

import tree_sitter
import tree_sitter_zeek

from .error import FileError
from .formatter import Formatter
from .node import Node
from .output import OutputStream

class Script:
    """Representation of a single Zeek script file."""

    def __init__(self, file: str | pathlib.Path | BinaryIO) -> None:
        """Script constructor.

        The file argument can be a string providing a file name, a pathlib.Path
        providing a file name, or a file-like object. The filename/path "-"
        implies stdin.
        """
        self.file = file
        self.source: bytes | None = None  # The file's full content, once parsed
        self.ts_tree: tree_sitter.Tree | None = (
            None  # The tree-sitter parse tree for the script
        )
        self.root: Node | None = (
            None  # The root node of our cloned (and malleable) tree
        )

    def parse(self) -> bool:
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

        self._clone_tree()
        self._patch_tree()

        return not self.has_error()

    def has_error(self) -> bool:
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

    def get_error(self) -> tuple[str | bytes | None, int | None, str | None]:
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
            else:
                continue

            line = self.source.split(Formatter.NL)[node.start_point[0]]
            line = line.decode("UTF-8")
            lineno = node.start_point[0]
            break

        return line, lineno, msg

    def traverse(self, include_cst: bool = False) -> Generator[tuple[Node, int]]:
        """Depth-first iterator for the script's syntax tree.

        This yields a tuple (node, nesting) with a Node instance (ours, not
        tree_sitter's) and its integer nesting level: the root has nesting level
        0, its children have nesting level 1, their children have nesting level
        2, etc.
        """
        assert self.root is not None, "call Script.parse() before Script.traverse()"

        yield from self.root.traverse(include_cst)

    def get_content(
        self, start_byte: int | None = None, end_byte: int | None = None
    ) -> bytes:
        """Returns a region of this script's content.

        By default, this returns the entire content. start_byte and end_byte, if
        provided, are numerical byte offsets in the script, behaving as usual
        indices in slice notation.
        """
        assert self.root is not None, "call Script.parse() before Script.get_content()"

        return self.source[start_byte:end_byte]

    @staticmethod
    def _is_annotation(text: bytes) -> bytes | None:
        """If text is a #@ annotation comment, return the annotation name."""
        for prefix in (b"#@ ", b"##@ "):
            if text.startswith(prefix):
                return text[len(prefix):]
        return None

    def _scan_format_annotations(self) -> None:
        """Scan comments for #@ formatting annotations and mark AST nodes.

        Sets node.no_format on affected AST nodes:
        - bytes value: raw source to output instead of formatting
        - True: node is inside a range already emitted by an earlier sibling

        Raises ValueError on unbalanced BEGIN/END or NO-FORMAT inside a range.
        """
        assert self.root is not None

        # First pass: find all annotation comments and validate.
        annotations: list[tuple[bytes, Node]] = []  # (kind, comment_node)
        begin_comment: Node | None = None

        for node, _ in self.root.traverse(include_cst=True):
            if not node.is_comment():
                continue
            text = self.get_content(*node.script_range()).strip()
            kind = self._is_annotation(text)
            if kind is None:
                continue
            if kind == b"NO-FORMAT":
                if begin_comment is not None:
                    raise ValueError(
                        f"#@ NO-FORMAT at line {node.start_point[0] + 1} "
                        f"inside #@ BEGIN-NO-FORMAT region "
                        f"(started at line {begin_comment.start_point[0] + 1})"
                    )
                annotations.append((kind, node))
            elif kind == b"BEGIN-NO-FORMAT":
                if begin_comment is not None:
                    raise ValueError(
                        f"Nested #@ BEGIN-NO-FORMAT at line {node.start_point[0] + 1} "
                        f"(previous at line {begin_comment.start_point[0] + 1})"
                    )
                begin_comment = node
                annotations.append((kind, node))
            elif kind == b"END-NO-FORMAT":
                if begin_comment is None:
                    raise ValueError(
                        f"#@ END-NO-FORMAT at line {node.start_point[0] + 1} "
                        f"without matching #@ BEGIN-NO-FORMAT"
                    )
                annotations.append((kind, node))
                begin_comment = None

        if begin_comment is not None:
            raise ValueError(
                f"#@ BEGIN-NO-FORMAT at line {begin_comment.start_point[0] + 1} "
                f"without matching #@ END-NO-FORMAT"
            )

        # Second pass: mark AST nodes.
        # Mark all annotation comments so CST sibling loops skip them.
        for _, comment in annotations:
            comment.no_format = True

        # Track line-start positions for BEGIN-NO-FORMAT ranges.
        # Keyed by comment node identity so we don't monkey-patch nodes.
        range_line_starts: dict[int, int] = {}

        for kind, comment in annotations:
            if kind == b"NO-FORMAT":
                # Find the AST node this comment is attached to.
                ast_node = comment.ast_parent
                if ast_node is None:
                    continue
                # Capture the AST node through trailing CST (the comment).
                # Include the annotation comment if it's a prev_cst_sibling
                # (comment-before-statement case), but not other whitespace.
                start = ast_node.start_byte
                for pcs in ast_node.prev_cst_siblings:
                    if pcs.start_byte == comment.start_byte:
                        start = comment.start_byte
                        break
                end = ast_node.end_byte
                check: Node | None = ast_node
                while check:
                    if check.next_cst_siblings:
                        end = max(end, check.next_cst_siblings[-1].end_byte)
                    check = check.children[-1] if check.children else None
                raw = self.source[start:end].lstrip()
                ast_node.no_format = raw

            elif kind == b"BEGIN-NO-FORMAT":
                # Find the raw range start (beginning of the line)
                line_start = comment.start_byte
                while line_start > 0 and self.source[line_start - 1:line_start] not in (b"\n", b"\r"):
                    line_start -= 1
                range_line_starts[id(comment)] = line_start

            elif kind == b"END-NO-FORMAT":
                # Find matching BEGIN
                begin = None
                for k, c in annotations:
                    if k == b"BEGIN-NO-FORMAT" and id(c) in range_line_starts:
                        begin = c
                # Should always find one (validated above)
                if begin is None:
                    continue
                raw = self.source[range_line_starts.pop(id(begin)):comment.end_byte]
                # Mark all AST nodes in the range
                first = True
                for node, _ in self.root.traverse():
                    if node.start_byte >= begin.start_byte and node.end_byte <= comment.end_byte:
                        if not node.children:
                            continue  # Skip leaf nodes, mark their parents
                        if first:
                            node.no_format = raw
                            first = False
                        else:
                            node.no_format = True  # Skip, already emitted

    def format(
        self, output: BinaryIO | TextIO | None = None, enable_linebreaks: bool = True,
        use_ir: bool = False,
    ) -> None:
        """Formats the script and writes out the result.

        The output destination can be one of three things: a filename, a file
        object, or None, which means stdout. enable_linebreaks, True by default,
        controls whether to use linebreaks at all. use_ir, False by default,
        selects the IR-based formatter (fmt2) instead of the classic one.
        """
        assert self.root is not None, "call Script.parse() before Script.format()"

        if use_ir:
            from .fmt2 import format_script
            result = format_script(self)
            if output is None:
                sys.stdout.buffer.write(result)
            elif isinstance(output, str):
                with open(output, "wb") as ostream:
                    ostream.write(result)
            else:
                output.write(result)
            return

        self._scan_format_annotations()

        def do_format(out: BinaryIO | TextIO) -> None:
            with OutputStream(out, enable_linebreaks) as ostream:
                assert self.root
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

    def write_tree(
        self, output: io.BytesIO | None = None, include_cst: bool = False
    ) -> None:
        """Writes the script's parse tree to the given output.

        The output destination works as for Script.format().

        include_cst controls whether the rendered tree shows only AST nodes
        (i.e., "proper" members of the grammar, excluding TS's "extra" nodes
        such as newlines and comments) or AST and CST nodes.
        """

        def node_str(node: Node, nesting: int, script: Script) -> str:
            content = ""
            if node.is_named:
                # Cap the amount of script payload we show ...
                data = script.source[node.start_byte : node.end_byte]
                maxlen = 100
                extra = ""

                if len(data) > maxlen:
                    extra = f"[+{len(data) - maxlen}]"
                    data = data[:maxlen]

                # ... and render it such that we get backslash-escapes.
                content = str(repr(data.decode("ascii", "ignore"))) + extra

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

        def do_traverse(ostream: IO[AnyStr]) -> None:
            for node, nesting in self.traverse(include_cst):
                text = node_str(node, nesting, self)

                if isinstance(ostream, io.TextIOBase):
                    ostream.write(text + os.linesep)
                else:
                    # Disable type checking here since mypy has trouble detecting that `sys.stdout` can be written bytes.
                    ostream.write(text.encode("UTF-8") + Formatter.NL)  # type: ignore

        if output is None:
            do_traverse(sys.stdout)
        elif isinstance(output, str):
            with open(output, "w", encoding="utf-8") as ostream:
                do_traverse(ostream)
        else:
            # output should be a file-like object
            do_traverse(output)

    def _clone_tree(self) -> None:
        """Deep-copy the TS tree to one consisting of zeekscript.Node instances.

        The input is self.ts_tree, the output is our tree's root node at self.root.
        """
        # We don't operate directly on the TS tree, for two reasons: first, the
        # TS tree isn't malleable from Python. We can alter the structure of our
        # own tree freely, which helps during formatting. Second, we encode
        # additional metadata in the node structure.

        def make_nullnode() -> Node:
            node = Node()
            node.is_named = True
            node.type = "nullnode"
            node.is_ast = True
            return node

        def make_node(node: tree_sitter.Node) -> Node:
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
            assert node.type
            if node.type != "nl" and not node.type.endswith("_comment"):
                new_node.is_ast = True

            new_children: list[Node] = []

            # Set up state for all of the node's children. This recurses
            # so we build up our own tree.
            for ts_child in node.children:
                new_child = make_node(ts_child)

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
            prevs: list[Node] = []
            last_child = None

            def append_cst_sibling(ast: Node, cst: Node) -> None:
                """Append a CST node to an AST node's next_cst_siblings list.

                This also updates prev_cst_sibling so the CST chain remains
                navigable in both directions. MinorCommentFormatter uses
                prev_cst_sibling to determine whether a comment is at end-of-line
                (preceded by code) or on its own line (preceded by newline).
                """
                if ast.next_cst_siblings:
                    cst.prev_cst_sibling = ast.next_cst_siblings[-1]
                else:
                    cst.prev_cst_sibling = ast
                ast.next_cst_siblings.append(cst)
                cst.ast_parent = ast
                cst.is_cst_next_node = True

            for child in new_children:
                if ast_nodes_remaining == 0:
                    assert ast_node
                    append_cst_sibling(ast_node, child)

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
                    append_cst_sibling(ast_node, child)

                elif child.is_minor_comment() and last_child and last_child.is_ast:
                    # Accept a minor comment if it directly follows the AST node.
                    append_cst_sibling(ast_node, child)

                elif (
                    child.is_nl()
                    and last_child
                    and (
                        # Accept newline if it ends a comment or follows an error node.
                        last_child.is_comment() or last_child.is_error()
                    )
                ):
                    append_cst_sibling(ast_node, child)

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

        assert self.ts_tree
        self.root = make_node(self.ts_tree.root_node)

    def _patch_tree(self) -> None:
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
                # Find where to split: push attached content (before standalone
                # comments on their own line) but keep standalone content.
                # Standalone = content after a newline (comments on their own line).
                split_idx = len(node.next_cst_siblings)  # Default: push all

                for i, sib in enumerate(node.next_cst_siblings):
                    if sib.is_nl():
                        # Check if there's non-NL content after this newline
                        has_content_after = any(
                            not s.is_nl() for s in node.next_cst_siblings[i + 1:]
                        )
                        if has_content_after:
                            # Split here: content after this NL is standalone
                            split_idx = i
                            break

                to_push = node.next_cst_siblings[:split_idx]
                to_keep = node.next_cst_siblings[split_idx:]

                if to_push:
                    node.next_cst_sibling = None

                    # Link the pushed CST nodes to the target's existing CST chain.
                    # Update prev_cst_sibling so formatters can navigate the chain
                    # (e.g., MinorCommentFormatter checks prev_cst_sibling to
                    # distinguish end-of-line vs standalone comments).
                    target = node.children[-1]
                    if target.next_cst_siblings:
                        target.next_cst_siblings[-1].next_cst_sibling = to_push[0]
                        to_push[0].prev_cst_sibling = target.next_cst_siblings[-1]
                    else:
                        to_push[0].prev_cst_sibling = target

                    target.next_cst_siblings += to_push

                node.next_cst_siblings = to_keep
