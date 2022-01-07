class Node:
    """A subset of tree_sitter.Node.

    We use this to build a mutable clone of the tree-sitter parse tree,
    and add some convenience functions.
    """
    def __init__(self):
        self.children = []
        self.parent = None
        self.prev_sibling = None
        self.next_sibling = None
        self.start_byte = 0
        self.end_byte = 0
        self.start_point = (0, 0)
        self.end_point = (0, 0)
        self.is_named = False
        self.type = None

    def is_comment(self):
        return self.type and self.type.endswith('_comment')

    def is_post_comment(self):
        return self.type and self.type == 'zeekygen_prev_comment'
