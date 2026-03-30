# C++ Formatter Failing Tests (88 pass, 91 fail as of 2026-03-30)

## By category (sorted by count)

### Line-breaking / layout quality (25)
Call args, assignments, binary ops not splitting at overflow.
test011 test014 test029 test044 test061 test089 test093
test099 test101 test104 test105 test108 test125 test126 test127
test128 test130 test133 test135 test136 test137 test138
test140 test156

### Comment handling (16)
Comments dropped, mispositioned, or rendered as `/* COMMENT-xxx */`.
test001 test002 test016 test018 test026 test032 test045 test057
test088 test109 test112 test113 test114 test132 test134 test167

### ?$ and |...| operator formatting (6)
Spaces inserted around `?$`; `|...|` renders wrong.
test022 test023 test058 test059 test067 test116

### Slice formatting (6)
Spaces around `:` in slices wrong.
test075 test076 test078 test079 test080 test103

### NO-FORMAT directives (5)
`#@ NO-FORMAT` / `#@ BEGIN-NO-FORMAT` not honored.
test017 test111 test112 test113 test114

### BRACE-INIT support (5)
Untyped `{ expr_list }` initializers emit placeholder.
test086 test087 test143 test145 test155

### Type-decl attr-list layout (4)
Attributes on typed declarations not wrapping correctly.
test015 test066 test106 test173

### LAMBDA support (3)
Lambda expressions emit placeholder.
test094 test095 test096

### Ternary layout (3)
Ternary `? :` doesn't split across lines well.
test157 test158 test159

### Vertical call-arg layout (3)
Multi-element set/redef not formatted one-per-line with trailing commas.
test041 test097 test098

### Param-list wrapping (2)
Long parameter lists in func/event decls not wrapping.
test060 test065

### Enum tweaks (2)
Trailing comma missing, or short enums not collapsed to one line.
test047 test048

### INDEX-LITERAL trailing comma (2)
Missing trailing comma in `[1, ]`.
test084 test085

### Record field attr wrapping (2)
Field type + attrs not breaking across lines.
test139 test174

### For/while trailing comment (2)
Comment after condition dropped.
test162 test171

### CONSTRUCTOR with LAMBDA attr (1)
test038 (needs LAMBDA support first)

### Miscellaneous (1 each)
- test005: Constant with embedded ops
- test019: Missing blank line between else-if blocks
- test021: Layout issue
- test027: Switch case values not wrapping
- test040: CALL wrapping (set() args should use indent, not alignment)
- test054: EVENT-STMT bare event without args
- test056: TYPE-FUNC params not wrapping
- test102: Pattern literal emits `/* UNKNOWN-EXPR */`
- test115: Extra spaces inside PAREN
- test124: PRINT with multiple exprs (only first printed)
- test161: ASSERT keyword not supported
- test163: `#@` annotation after if condition dropped
- test176: Type-decl attr layout

## Notes
- Some tests appear in multiple categories; listed under primary failure.
- "Line-breaking / layout quality" is broad -- individual tests may
  become tractable as specific node types get better formatting.

## Session progress (2026-03-30)
- Started: 65 pass, 114 fail
- After trailing comment fix (emit_ast.py _iter_children): 67 pass, 112 fail
  - Fixed: test129, test141
- After &group attr + param wrapping: 73 pass, 106 fail
  - Fixed: test049, test050, test051, test052, test053, test068
  - Removed "&group attr on func-decl" category (all 6 fixed)
  - Removed test053 from "Param-list wrapping" (now passes)
- After greedy-fill arg wrapping + binary-op fix: 80 pass, 99 fail
  - Fixed: test035, test061, test089, test099, test131, test146, test156 (line-breaking)
- After CONSTRUCTOR support: 88 pass, 91 fail
  - Fixed: test144, test153, test154, test177, test178, test179 (constructor)
  - Fixed: test035, test146 (multi-line init value metrics in FormatDecl)
  - Removed "CONSTRUCTOR support" category (6 of 8 fixed; test038 needs LAMBDA, test040 is CALL wrapping)
