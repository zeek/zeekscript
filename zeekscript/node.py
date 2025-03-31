"""Node-related functionality."""

from __future__ import annotations

from collections.abc import Callable, Generator
from typing import TYPE_CHECKING, Literal

if TYPE_CHECKING:
    from .formatter import Formatter


class Node:
    """A relative of tree_sitter.Node.

    This class represents nodes in the syntax tree. A subset of its members
    match those of tree_sitter.Node. Instances provide separate navigational
    data structures for abstract and concrete nodes (such as comments, newlines)
    in the source, and a few helper methods.

    We don't inherit from tree_sitter.Node because we can't: doing so yields
    TypeError: type 'tree_sitter.Node' is not an acceptable base type. One also
    cannot instantiate its nodes manually.

    A distinction of CST and AST runs through this class. This distinction is
    not strictly the textbook one, where the AST abstracts away syntactical
    features not useful for language processing, and it also differs slightly
    from that used in the TS docs. In this class, the AST is the CST without any
    nodes that fall under TS's "extra" category, i.e., grammar constructs that
    can occur anywhere in the language, including named symbols. For Zeek,
    examples include newlines and comments.
    """

    def __init__(self) -> None:
        # AST navigation: these relationship omit nodes present only in the
        # concrete tree, such as comments and whitespace. The following members
        # are all present in the TS node type.
        self.children: list[Node] = []
        self.parent: Node | None = None
        self.prev_sibling: Node | None = None
        self.next_sibling: Node | None = None

        self.start_byte = 0
        self.end_byte = 0  # The first byte _after_ this node's content
        self.start_point: tuple[int, int] = (0, 0)
        self.end_point: tuple[int, int] = (0, 0)

        # This terminology stems from TreeSitter and means that a node is typed,
        # i.e., the root of a grammar rule. It is not a token.
        self.is_named: bool = False

        # In some cases TreeSitter can infer that a node is simply missing (such
        # as a trailing semicolon). Implies has_error, but does not lead to an
        # ERROR node higher up.
        self.is_missing = False

        # This bit indicates whether there's a parser problem somewhere below
        # this node. This problem can be of various forms, including parse
        # errors (node type "ERROR") or missing nodes. Any others?
        self.has_error = False

        # Consider name() or token() below instead of accessing this directly
        self.type: str | None = None

        # ---- Additions over the TS node members below ------------------------

        # A variant of self.children, consisting only of the non-ERROR nodes.
        # This subset is safe to navigate with expectation of node types
        # (e.g. "the first node should be a type, the second a ':", etc), since
        # it's free of CST and error nodes.
        self.nonerr_children: list[Node] = []

        # A zeekscript.Formatter attached with this node. Formatters
        # set this field as they get instantiated.
        self.formatter: Formatter | None = None

        # Whether this is an AST member
        self.is_ast: bool = False

        # For CST nodes, a link to the AST node they're grouped with.
        self.ast_parent: Node | None = None

        # For CST nodes, these flags indicate whether they come before or after
        # the AST node they're associated with.
        self.is_cst_prev_node: bool = False
        self.is_cst_next_node: bool = False

        # Direct previous/next links to nodes in the CST.
        self.prev_cst_sibling: Node | None = None
        self.next_cst_sibling: Node | None = None

        # If this is an AST node: full sequences of CST nodes preceding/
        # succeeding this node. These lists are in tree-order: if a tree node's
        # sequence of children is ...
        #
        #   <minor comment1> (CST)
        #   <nl>             (CST)
        #   <minor comment2> (CST)
        #   <nl>             (CST)
        #   <expr>           (AST)
        #   <minor_comment3> (CST)
        #   <nl>             (CST)
        #
        # ... then the members of prev_cst_siblings are ...
        #
        #   [ <minor comment1>, <nl>, <minor comment2>, <nl>]
        #
        # ... and those of next_cst_siblings are:
        #
        # [ <minor comment3>, <nl>]
        #
        self.prev_cst_siblings: list[Node] = []
        self.next_cst_siblings: list[Node] = []

        # Two arrays for AST nodes that represent any directly
        # preceding/succeeding ERROR nodes.
        self.prev_error_siblings: list[Node] = []
        self.next_error_siblings: list[Node] = []

    def __eq__(self, other: object) -> bool:
        """Two nodes are equal when the cover the same range in the script."""
        return (
            isinstance(other, Node)
            and self.start_byte == other.start_byte
            and self.end_byte == other.end_byte
        )

    def name(self) -> str | None:
        """Returns the type of a named node.

        For nodes that are named in the grammar (such as expressions or
        statements), this returns their type (e.g. "expr", "stmt", "decl"). When
        this isn't a named node, returns None.
        """
        return self.type if self.is_named else None

    def token(self) -> str | None:
        """Returns token string if this is an unnamed (terminal) node.

        This is the complement to name(): when this node is a terminal/token
        (e.g. "print", "if", ";") this function returns its string. Otherwise
        returns None.
        """
        return self.type if not self.is_named else None

    def script_range(self, with_cst: bool = False) -> tuple[int, int]:
        """Returns this node's start/end byte indices in the script, as a tuple.

        By default this ignores potential CST nodes associated with this node
        (preceding or succeeding it), but their ranges get included when
        with_cst is True.
        """
        start, end = self.start_byte, self.end_byte

        if with_cst:
            node: Node | None = self
            while node:
                if node.prev_cst_siblings:
                    start = max(start, node.prev_cst_siblings[0].start_byte)
                    # No need to dig into child nodes, they won't have
                    # any earlier content.
                    break
                node = node.children[0] if node.children else None

            node = self
            while node:
                if node.next_cst_siblings:
                    end = max(end, node.next_cst_siblings[-1].end_byte)
                    # No need to dig into child nodes, they won't have
                    # any later content.
                    break
                node = node.children[-1] if node.children else None

        return start, end

    def traverse(
        self,
        include_cst: bool = False,
        predicate: Callable[[Node], bool] | None = None,
    ) -> Generator[tuple[Node, int]]:
        """A tree-traversing generator for this node and its subtree.

        Yields a tuple of (node, nesting level) for every visited node. When
        include_cst is True, traversal also includes CST nodes. When predicate
        is not None, it filters visited nodes: a function taking a visited node
        as input, it returns a Boolen that determines whether to return the node
        or not.
        """
        queue: list[tuple[Node, int]] = [(self, 0)]

        def always_true(_: Node) -> Literal[True]:
            return True

        # Default the filter to accept-all if absent
        if predicate is None:
            predicate = always_true

        while queue:
            node, nesting = queue.pop(0)

            # If the caller wants the CST, we now need to iterate any
            # preceeding/succeeding CST nodes this AST nodes has stored:
            if include_cst:
                for cst_node in node.prev_cst_siblings:
                    if predicate(cst_node):
                        yield cst_node, nesting

            if predicate(node):
                yield node, nesting

            if include_cst:
                for cst_node in node.next_cst_siblings:
                    if predicate(cst_node):
                        yield cst_node, nesting

            for child in reversed(node.children):
                queue.insert(0, (child, nesting + 1))

    def is_error(self) -> bool:
        """Returns True if this node summarizes a parsing error.

        This currently refers to nodes with type string "ERROR" (i.e., nodes
        that group problematic content under them, possibly alongside correctly
        parsed material).
        """
        return self.is_named and bool(self.type) and self.type == "ERROR"

    def is_nl(self) -> bool:
        """Returns True if this is a newline."""
        return self.is_named and (self.type is not None) and self.type == "nl"

    def is_comment(self) -> bool:
        """Returns True if this is any kind of comment."""
        return (
            self.is_named and (self.type is not None) and self.type.endswith("_comment")
        )

    def is_minor_comment(self) -> bool:
        """Returns True if this is a minor comment ("# foo")."""
        return (
            self.is_named and (self.type is not None) and self.type == "minor_comment"
        )

    def is_zeekygen_prev_comment(self) -> bool:
        """Returns True if this is a Zeekygen "##<" comment."""
        return (
            self.is_named
            and (self.type is not None)
            and self.type == "zeekygen_prev_comment"
        )

    def has_property(self, predicate: Callable[[Node], bool]) -> bool:
        """Returns a predicate's outcome for this node.

        This catches attribute and index errors, simplifying predicate
        evaluation for the caller because you do not need to check e.g. if
        self.children exists or has the expected number of entries.
        """
        try:
            return predicate(self)
        except (AttributeError, IndexError):
            return False

    def has_only_whitespace_before(self) -> bool:
        """True if this node is has only whitespace before it at its tree level.

        This predicate scans the preceding CST node siblings, verifying that
        they are all whitespace. The scan stops when it hits an AST node, which
        counts as success. Absence of preceding CST nodes is also success.
        """
        res = self.find_prev_cst_sibling(lambda node: not node.is_nl())
        return res is None or res.is_ast

    def find_prev_cst_sibling(self, predicate: Callable[[Node], bool]) -> Node | None:
        """Retrieve first preceding CST sibling matching a predicate.

        The predicate is a function taking a single Node and returning T or F.
        Returns sibling satisfying the predicate, or None when search fails.

        Note that this search does not stop after exhausting the node's
        prev_cst_nodes list, i.e., the CST nodes grouped with this node. It
        continues through the entire sibling level in the parse tree.
        """
        node: Node | None = self.prev_cst_sibling
        while node:
            if predicate(node):
                return node
            node = node.prev_cst_sibling
        return None
