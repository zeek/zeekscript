# Zeek Formatting Rules

Extracted from the current formatter implementation. Organized by how
directly they map to a Wadler/Lindig document IR.

## Direct mappings

**1. Space-separated tokens with newline termination.**
`module`, `type_decl`, `pragma`, most simple statements (`next;`, `break;`,
`return expr;`, `add/delete expr;`, `local/const id: type = val;`).
→ `concat(keyword, SPACE, ..., text(";"), HARDLINE)`

**2. Block indentation.**
Function bodies, if/for/while bodies, stmt_list inside `{ }`.
→ `nest(1, concat(HARDLINE, body))`

**3. Braces on same line (no Whitesmith).**
`export {`, `record {`, `enum {`, `switch {`.
→ `concat(text("{"), nest(1, ...), HARDLINE, text("}"))`

**4. K&R function layout.**
Header on one line, then newline, then `{ body }`.
→ `concat(header, HARDLINE, body_block)`

**5. Parenthesized alignment.**
Function params, function calls, `if ( )`, `for ( )`, `while ( )`, event
headers all align continuation to the column after `(`.
→ `concat(text("("), align(concat(args, text(")"))))`

**6. Comma-separated lists.**
formal_args, expr_lists, case_type_lists. Space after comma, break at commas.
→ `join(concat(text(","), LINE), items)` inside a `group`

**7. Binary operators.**
`a + b`, `a == b`, etc. Space around operator, prefer breaking after operator
(operator stays at end of line).
→ `group(concat(lhs, SPACE, op, LINE, rhs))` with alignment

**8. Boolean operators (&&, ||).**
Same as binary ops but with stronger break preference when they form a chain.
→ Same pattern, naturally handled by nested groups.

**9. Ternary.**
`cond ? true_expr : false_expr` with alignment.
→ `group(concat(cond, SPACE, text("?"), LINE, true_expr, SPACE, text(":"), LINE, false_expr))` with align

**10. Record field access.**
`expr$field` never breaks.
→ `concat(expr, text("$"), field)` — no LINE, no Group

**11. `?$` field check.**
Same, never breaks.
→ `concat(expr, text("?$"), id)`

**12. Unary operators.**
`!expr` (with space), `++expr`, `--expr`, `~expr` (no space).
→ `concat(text("!"), SPACE, expr)` or `concat(text("++"), expr)`

**13. String concatenation.**
`"str1" + "str2"`, prefer breaking after `+`.
→ Same as binary op pattern.

**14. Empty braces.**
`{ }` when nothing between braces.
→ `text("{ }")`

**15. Attributes.**
`&read_expire=5min`, space-separated. `=` is spaced or not depending on
whether any attr value has embedded blanks (intervals, binary exprs).
→ Scan once for embedded-blank check, then format accordingly.

**16. Preprocessor directives.**
`@if`, `@ifdef`, etc. Not indented, no line-breaking. Content inside gets
one tab level.
→ `concat(text("@ifdef ..."), HARDLINE, nest(1, body), HARDLINE, text("@endif"))`

**17. Intervals.**
`5 min` with space between number and unit.
→ `concat(number, SPACE, unit)`

**18. `else if` kept on same line.**
Prevents cascading indentation.
→ `concat(text("else"), SPACE, format_if(...))`

**19. Module decl.**
`module Foo;`
→ `concat(text("module"), SPACE, name, text(";"), HARDLINE)`

**20. Copy expr.**
`copy(expr)` — no break between copy and paren.
→ `concat(text("copy"), text("("), expr, text(")"))`

## Modest-effort mappings

**21. Global/const/option/redef declarations.**
`global foo: type = val &attrs;`. Typed-initializer aligns one past the
identifier. Attr_list wraps to its own line if type is long.
→ Group with align. Attr-wrapping becomes an `if_break` choice.

**22. Enum formatting.**
Single-line if fits, one-per-line if not.
→ Group with `if_break`: broken = one-per-line with nest, flat = inline.

**23. Record type.**
`record { field: type; ... }` — always multi-line when fields present.
→ `concat(text("record {"), nest(1, concat(HARDLINE, fields...)), HARDLINE, text("}"))`

**24. `set[...]` and `table[...]` type lists.**
Keep on one line if fits, allow wrapping if not.
→ `group(concat(text("["), align(type_list), text("]")))` — wrapping from group.

**25. Initializer (`= expr`).**
Space after `=`, alignment for continuation. Old code had complex logic for
unbreakable long RHS and shallower alignment — in the IR model the group
simply won't find a break and `Align`'s MAX_ALIGN_COL fallback handles it.

**26. Redef enum.**
`redef enum Foo += { ... };` — same body logic as rule 22.

**27. Redef record.**
`redef record Foo += { ... };` or `redef record expr -= attrs;`.
→ Structural, straightforward.

**28. Switch/case.**
`switch (expr) { case ...: stmts }` with case labels at base level, body
nested.

**29. When/timeout.**
`when (expr) { body } timeout expr { body }`.
→ Structural, like if/else.

**30. Schedule.**
`schedule expr { event_hdr }`.
→ `group(concat(text("schedule"), SPACE, expr, SPACE, text("{"), SPACE, event_hdr, SPACE, text("}")))`

**31. Blank line preservation.**
Sequences of newlines in the source collapse to at most one blank line
between statements, except at block start/end.
→ Pre-process CST newline nodes; emit `HARDLINE, HARDLINE` for a blank line.

## Rules needing more thought

**32. Record constructor (`[$field=val, ...]`) formatting.**
The gnarliest area. Multiple interacting behaviors:
- If all fields fit on one line: `[$f1=v1, $f2=v2]`
- If not: one field per line, aligned to column after `[`
- The `[` stays inline if the first field fits, breaks to new line if
  alignment would be too deep
- Fields with nested function calls wrap internally and don't count toward
  the break-before-`[` decision
- Comments between fields must align with `$` fields
- Multiple short fields can pack on one line

In the IR model, most simplifies: `group(concat(text("["), align(fill(fields)),
text("]")))`. The break-before-`[` is an outer group decision. Field-packing
+ comment-alignment still needs explicit formatter logic. **Medium complexity.**

**33. `ExprListFormatter` in function calls.**
Interleaved concerns in the old code:
- Record-style args (`$field=val`) with explicit layout and deep-alignment
  fallback
- Lambda args get their own line
- Record constructor args keep `[` inline when first field fits
- One-per-line mode with tab indent and lenient line length

In the IR model: `fill(items)` inside `align` after `(`. Lambdas use
`if_break(HARDLINE, LINE)` as separator. Record constructors are nested
groups. Deep-alignment fallback handled by `Align`'s MAX_ALIGN_COL.
**Medium-to-hard** due to number of interacting cases.

**34. Function/event header with `&group` attributes.**
Attr_list after `)` may need its own line when params wrap, with possible
under-indentation when alignment is too deep.
→ `group(concat(keyword, SPACE, name, params, attr_or_nothing))`. When
group breaks, attr goes on its own aligned line. Under-indentation handled
by `Align`'s MAX_ALIGN_COL fallback. **May need custom `if_break` for
attr placement.**

**35. Compact if-statements.**
Preserved when original source had body on same line AND there's an adjacent
compact-if (switch-case-like pattern). Source-layout-preserving heuristic.
→ Formatter reads source layout to decide. Not hard, just requires source
inspection.

**36. Brace-to-constructor transform.**
`{ val1, val2 }` → `set(val1, val2)` or `table(...)` when no explicit type.
→ Formatter just emits different tokens. Simple bookkeeping.

**37. Comments.**
Association step (CST → AST) is reused from `script.py`. Then:
- Leading comments: `concat(comment, HARDLINE, node)`
- Trailing comments: `concat(node, SPACE, comment)`
- MinorComment: end-of-line vs own-line distinction based on prev_cst_sibling
- ZeekygenPrevComment (##<): align to each other — needs column tracking
  or pre-computed alignment widths

Most maps to straightforward IR. The ##< alignment is **medium complexity**;
rest is straightforward.

**38. NO-FORMAT annotations.**
Already handled in `script.py`. Formatter emits raw content.
→ Simple.
