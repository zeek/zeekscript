class Node:
    """A relative of tree_sitter.Node.

    This class represents nodes in the syntax tree. A subset of its members
    match those of tree_sitter.Node. Instances provide separate navigational
    data structures for abstract and concrete nodes (such as comments, newlines)
    in the source, and a few helper methods.

    We don't inherit from tree_sitter.Node because we can't: doing so yields
    TypeError: type 'tree_sitter.Node' is not an acceptable base type. One also
    cannot instantiate its nodes manually.
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
        self.end_byte = 0
        self.start_point = (0, 0)
        self.end_point = (0, 0)
        self.is_named = False
        self.is_missing = False
        self.has_error = False
        self.type = None

        # Additions over the TS node members below:

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
        #   <minor comment>  (CST)
        #   <nl>             (CST)
        #   <minor comment>  (CST)
        #   <nl>             (CST)
        #   <expr>           (AST)
        #   <minor_comment>  (CST)
        #   <nl>             (CST)
        #
        # ... then the members of prev_cst_siblings are ...
        #
        #   [ <minor comment>, <nl>, <minor comment>, <nl>]
        #
        # ... and those of next_cst_siblings are:
        #
        # [ <minor comment>, <nl>]
        #
        self.prev_cst_siblings = []
        self.next_cst_siblings = []

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
