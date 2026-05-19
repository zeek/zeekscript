; Rules for formatting Spicy.
;
; Formatting is specified here in terms of tree-sitter nodes. We select nodes
; with tree-sitter queries[^1] and then attach topiary formatting rules[^2] in
; the captures.
;
; See the Development section in README.md for a workflow on how to modify or
; extend these rules.

; [^1]: https://tree-sitter.github.io/tree-sitter/using-parsers#pattern-matching-with-queries
; [^2]: https://github.com/tweag/topiary#design

; Comments are always followed by a linebreak.
[
  (minor_comment)
  (zeekygen_head_comment)
  (zeekygen_prev_comment)
  (zeekygen_next_comment)
] @append_hardline

; Comments are preceeded by a space.
(
  [
    (_)
    (nl) @do_nothing
  ]
  .
  [
    (minor_comment)
    (zeekygen_head_comment)
    (zeekygen_prev_comment)
    (zeekygen_next_comment)
  ] @prepend_space
)

; If we have multiple comments documenting an item with `##<` align them all.
(zeekygen_prev_comment) @multi_line_indent_all
