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
    def __init__(self):
        # AST navigation: these relationship omit nodes present only in the
        # concrete tree, such as comments and whitespace. The following members
        # are all present in the TS node type.
        self.children = []
        self.parent = None
        self.prev_sibling = None
        self.next_sibling = None

        self.start_byte = 0
        self.end_byte = 0 # The first byte _after_ this node's content
        self.start_point = (0, 0)
        self.end_point = (0, 0)

        # This terminology stems from TreeSitter and means that a node is typed,
        # i.e., the root of a grammar rule. It is not a token.
        self.is_named = False

        # In some cases TreeSitter can infer that a node is simply missing (such
        # as a trailing semicolon). Implies has_error, but does not lead to an
        # ERROR node higher up.
        self.is_missing = False

        # This bit indicates whether there's a parser problem somewhere below
        # this node. This problem can be of various forms, including parse
        # errors (node type "ERROR") or missing nodes. Any others?
        self.has_error = False

        # Consider name() or token() below instead of accessing this directly
        self.type = None

        # Additions over the TS node members below:

        # A zeekscript.Formatter attached with this node. Formatters
        # set this field as they get instantiated.
        self.formatter = None

        # Whether this is an AST member
        self.is_ast = False

        # For CST nodes, a link to the AST node they're grouped with.
        self.ast_parent = None

        # For CST nodes, these flags indicate whether they come before or after
        # the AST node they're associated with.
        self.is_cst_prev_node = False
        self.is_cst_next_node = False

        # Direct previous/next links to nodes in the CST.
        self.prev_cst_sibling = None
        self.next_cst_sibling = None

        # If this is an AST node: full sequences of CST nodes preceeding/
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
        self.prev_cst_siblings = []
        self.next_cst_siblings = []

    def name(self):
        """Returns the type of a named node.

        For nodes that are named in the grammar (such as expressions or
        statements), this returns their type (e.g. "expr", "stmt", "decl"). When
        this isn't a named node, returns None.
        """
        return self.type if self.is_named else None

    def token(self):
        """Returns token string if this is an unnamed (terminal) node.

        This is the complement to name(): when this node is a terminal/token
        (e.g. "print", "if", ";") this function returns its string. Otherwise
        returns None.
        """
        return self.type if not self.is_named else None

    def script_range(self, with_cst=False):
        """Returns this node's start/end byte indices in the script, as a tuple.

        By default this ignores potential CST nodes associated with this node
        (preceding or succeeding it), but their ranges get included when
        with_cst is True.
        """
        start, end = self.start_byte, self.end_byte

        if with_cst:
            node = self
            while node:
                if node.prev_cst_siblings:
                    if node.prev_cst_siblings[0].start_byte < start:
                        start = node.prev_cst_siblings[0].start_byte
                    # No need to dig into child nodes, they won't have
                    # any earlier content.
                    break
                node = node.children[0] if node.children else None

            node = self
            while node:
                if node.next_cst_siblings:
                    if node.next_cst_siblings[-1].end_byte > end:
                        end = node.next_cst_siblings[-1].end_byte
                    # No need to dig into child nodes, they won't have
                    # any later content.
                    break
                node = node.children[-1] if node.children else None

        return start, end

    def is_error(self):
        """Returns True iff this node summarizes a parsing error.

        This currently refers to nodes with type string "ERROR" (i.e., nodes
        that group problematic content under them, possibly alongside correctly
        parsed material).
        """
        return self.is_named and self.type and self.type == 'ERROR'

    def is_nl(self):
        """Returns True iff this is a newline."""
        return self.is_named and self.type and self.type == 'nl'

    def is_comment(self):
        """Returns True iff this is any kind of comment."""
        return self.is_named and self.type and self.type.endswith('_comment')

    def is_minor_comment(self):
        """Returns True iff this is a minor comment ("# foo")."""
        return self.is_named and self.type and self.type == 'minor_comment'

    def is_zeekygen_prev_comment(self):
        """Returns True iff this is a Zeekygen "##<" comment."""
        return self.is_named and self.type and self.type == 'zeekygen_prev_comment'

    def has_property(self, predicate):
        """Returns a predicate's outcome for this node.

        This catches attribute and index errors, simplifying predicate
        evaluation for the caller because you do not need to check e.g. if
        self.children exists or has the expected number of entries.
        """
        try:
            return predicate(self)
        except (AttributeError, IndexError):
            return False

    def find_prev_cst_sibling(self, predicate):
        """Retrieve first preceeding CST sibling matching a predicate.

        The predicate is a function taking a single Node and returning T or F.
        Returns sibling satisfying the predicate, or None when search fails.
        """
        node = self.prev_cst_sibling
        while node:
            if predicate(node):
                return node
            node = node.prev_cst_sibling
        return None

    def find_next_cst_sibling(self, predicate):
        """Retrieve first succeeding CST sibling matching a predicate.

        The predicate is a function taking a single Node and returning T or F.
        Returns sibling satisfying the predicate, or None when search fails.
        """
        node = self.next_cst_sibling
        while node:
            if predicate(node):
                return node
            node = node.next_cst_sibling
        return None
