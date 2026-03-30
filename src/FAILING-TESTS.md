# C++ Formatter Failing Tests (67 pass, 112 fail as of 2026-03-30)

## By category (sorted by count)

### Line-breaking / layout quality (27)
Call args, assignments, binary ops not splitting at overflow.
test011 test014 test029 test035 test044 test061 test089 test093
test099 test101 test104 test105 test108 test125 test126 test127
test128 test130 test131 test133 test135 test136 test137 test138
test140 test146 test156

### Comment handling (16)
Comments dropped, mispositioned, or rendered as `/* COMMENT-xxx */`.
test001 test002 test016 test018 test026 test032 test045 test057
test088 test109 test112 test113 test114 test132 test134 test167

### CONSTRUCTOR support (8)
`table(...)`, `set(...)`, `vector(...)` constructors emit placeholder.
test038 test144 test153 test154 test177 test178 test179 test040

### ?$ and |...| operator formatting (6)
Spaces inserted around `?$`; `|...|` renders wrong.
test022 test023 test058 test059 test067 test116

### &group attr on func-decl (6)
Event/hook `&group=` attribute dropped.
test049 test050 test051 test052 test053 test068

### Slice formatting (6)
Spaces around `:` in slices wrong.
test075 test076 test078 test079 test080 test103

### BRACE-INIT support (5)
Untyped `{ expr_list }` initializers emit placeholder.
test086 test087 test143 test145 test155

### NO-FORMAT directives (5)
`#@ NO-FORMAT` / `#@ BEGIN-NO-FORMAT` not honored.
test017 test111 test112 test113 test114

### LAMBDA support (3)
Lambda expressions emit placeholder.
test094 test095 test096

### Ternary layout (3)
Ternary `? :` doesn't split across lines well.
test157 test158 test159

### Param-list wrapping (3)
Long parameter lists in func/event decls not wrapping.
test053 test060 test065

### Vertical call-arg layout (3)
Multi-element set/redef not formatted one-per-line with trailing commas.
test041 test097 test098

### Enum tweaks (2)
Trailing comma missing, or short enums not collapsed to one line.
test047 test048

### Type-decl attr-list layout (4)
Attributes on typed declarations not wrapping correctly.
test015 test066 test106 test173

### INDEX-LITERAL trailing comma (2)
Missing trailing comma in `[1, ]`.
test084 test085

### Record field attr wrapping (2)
Field type + attrs not breaking across lines.
test139 test174

### For/while trailing comment (2)
Comment after condition dropped.
test162 test171

### Miscellaneous (1 each)
- test124: PRINT with multiple exprs (only first printed)
- test054: EVENT-STMT bare event without args
- test056: TYPE-FUNC params not wrapping
- test161: ASSERT keyword not supported
- test027: Switch case values not wrapping
- test102: Pattern literal emits `/* UNKNOWN-EXPR */`
- test115: Extra spaces inside PAREN
- test019: Missing blank line between else-if blocks
- test005: Constant with embedded ops
- test163: `#@` annotation after if condition dropped
- test176: Type-decl attr layout

## Notes
- Some tests appear in multiple categories; listed under primary failure.
- "Line-breaking / layout quality" is broad -- individual tests may
  become tractable as specific node types get better formatting.
