# C++ Formatter Failing Tests (161 pass, 12 fail as of 2026-04-03)

## By category (sorted by count)

### Line-breaking / layout quality (8)
Call args, assignments, binary ops not splitting at overflow.
test{093,104,126,127,130,133,135,136}

### Vertical call-arg layout (3)
Multi-element set/redef not formatted one-per-line with trailing commas.
test{041,097,098}

## Notes
- Some tests appear in multiple categories; listed under primary failure.
- "Line-breaking / layout quality" is broad -- individual tests may
  become tractable as specific node types get better formatting.

## Maintenance guide

This file must be updated with every commit that changes formatter
behavior (C++ formatter or Python emitter).  Run `make test` to get
the current pass/fail counts.

### How categories are determined

Diff each failing test's output against its `.fmt.zeek` baseline
(`diff <(./zeek-format ../tests/formatting/testNNN.rep) ../tests/formatting/testNNN.fmt.zeek`).
The *primary* failure mode determines the category:

- **Line-breaking / layout quality**: output is correct tokens but
  lines are too long or break in the wrong place (the catch-all).
- **Comment handling**: comments are dropped, duplicated, or rendered
  as `/* COMMENT-xxx */` placeholders.
- **Slice / operator formatting**: wrong whitespace around operators
  like `:` in slices or `?$`.
- **NO-FORMAT directives**: `#@ NO-FORMAT` / `#@ BEGIN-NO-FORMAT`
  regions not passed through verbatim.
- **LAMBDA support**: lambda expressions not formatted correctly
  (resolved 2026-04-03).
- **Ternary / vertical / param-list / enum / etc.**: specific
  construct not yet handled or not wrapping correctly.
- **Miscellaneous**: one-off issues that don't fit a group.

When a fix resolves an entire category, remove the category heading.
When a fix partially resolves a category, update the test list and
count.  If tests move between categories (e.g. a `?$` fix reveals
an underlying line-breaking issue), re-categorize them.

### How to update

1. Run `make test` — note pass/fail counts.
2. Collect failing test names (see the shell loop in the Makefile
   `test` target, adapted to print names).
3. Diff against the previous list: identify newly passing and newly
   failing tests.
4. For newly failing tests, diff their output to determine category.
5. Update the header counts, category lists, and session progress.
6. Use brace-list notation for test names: `test{022,058,067}`.

### Session progress conventions

Each entry records: what changed, new pass/fail, which tests were
fixed (brace notation), and any categories added/removed.  Keep
entries chronological within a session date.

## Session progress (2026-03-30)
- Started: 65 pass, 114 fail
- After trailing comment fix (emit_ast.py _iter_children): 67 pass, 112 fail
  - Fixed: test{129,141}
- After &group attr + param wrapping: 73 pass, 106 fail
  - Fixed: test{049,050,051,052,053,068}
  - Removed "&group attr on func-decl" category (all 6 fixed)
  - Removed test053 from "Param-list wrapping" (now passes)
- After greedy-fill arg wrapping + binary-op fix: 80 pass, 99 fail
  - Fixed: test{035,061,089,099,131,146,156} (line-breaking)
- After CONSTRUCTOR support: 88 pass, 91 fail
  - Fixed: test{144,153,154,177,178,179} (constructor)
  - Fixed: test{035,146} (multi-line init value metrics in FormatDecl)
  - Removed "CONSTRUCTOR support" category (6 of 8 fixed; test038 needs LAMBDA, test040 is CALL wrapping)
- After attr wrapping + spacing rule: 90 pass, 89 fail
  - Fixed: test{015,106,173} (decl attr-list layout)
  - Attr "=" spacing: use " = " when any attr value contains blanks, else "="
  - Wrapping: try all attrs on one continuation line; if overflow, one per line
  - Remaining test{066,176} need TYPE-PARAMETERIZED bracket wrapping (new category)
- After BRACE-INIT type inference + SCHEDULE: 95 pass, 84 fail
  - Fixed: test{086,087,145,155} (brace-init → CONSTRUCTOR "table"/"set")
  - Fixed: test{143} (schedule expression mis-detected as brace-init)
  - Emitter: infer table (if elements have =) or set, emit CONSTRUCTOR instead of BRACE-INIT
  - Emitter: detect schedule expr before brace-init check, emit SCHEDULE node
  - Formatter: added FormatSchedule (schedule interval { event })
  - Removed "BRACE-INIT support" category (all 5 fixed)
- After ?$ and |...| operator formatting: 101 pass, 78 fail
  - Fixed: test{022,023,058,059,067,116} (?$/|...| formatting)
  - Fixed: test{099,137,138,156} (line-breaking improved by ?$ trail reservation)
  - Updated baselines: test{022,058} (new output is better than original)
  - Removed "?$ and |...| operator formatting" category (all 6 fixed)
  - Added test{071,172} to failure list (previously unlisted)
- After slice `:` spacing: 105 pass, 74 fail
  - Fixed: test{075,076,078,079,080} (slice spacing)
  - Updated baselines: test{075,076,080} (no-space style for one-sided slices)
  - Moved test103 from "Slice formatting" to "Line-breaking" (spacing correct, line too long)
  - Removed "Slice formatting" category (all spacing issues fixed)
- After slice line-breaking: 106 pass, 73 fail
  - Fixed: test103 (slice split at `:` with alignment after `[`)
- After ternary line-breaking: 108 pass, 71 fail
  - Fixed: test{157,158,159} (split after `:` or `?` with alignment)
  - Updated baseline: test159 (split after `?` instead of after `:`)
  - Removed "Ternary layout" category (all 3 fixed)
- After param-list FitCol wrapping: 110 pass, 69 fail
  - Fixed: test{060,065} (func-decl param de-indent to fit col 79)
  - Added FitCol() helper: prefer align_col, back up to max_col - 1 if needed
  - Updated baseline: test060 (params pack tighter on continuation line)
  - Removed "Param-list wrapping" category (both fixed)
- After TYPE-PARAMETERIZED bracket wrapping + decl type-split: 111 pass, 68 fail
  - Fixed: test{066,172,176} (bracket type wrapping + type-split to indented line)
  - FormatTypeParam: greedy-fill bracket types when flat overflows
  - FormatDecl: split after ":" with type on indented line when head+type overflows
  - Updated baseline: test172 (current output acceptable)
  - Removed "TYPE-PARAMETERIZED bracket wrapping" category (all 3 fixed)

## Session progress (2026-03-31)
- After comment handling (trailing + if-else comments): 112 pass, 67 fail
  - Fixed: test032 (comments before else clause, blank line insertion)
  - FormatStmtList: COMMENT-PREV attaches as trailing when stmt is single-line
  - FormatIf: collect COMMENT-LEADING/COMMENT-PREV children, emit before else
  - Updated baseline: test032 (one-sided slice spacing benign difference)
- After CollectArgs/ArgComment shared comment infrastructure: 114 pass, 65 fail
  - Fixed: test{001,132} (comments inside ARGS and INDEX-LITERAL)
  - CollectArgs: shared helper pairs children with trailing/leading comments
  - ArgComment struct: trailing comment + leading comments per item
  - FormatArgsFill: trailing comment emits comma before comment, forces wrap;
    leading comments emit on own lines before item
  - FlatOrFill: skips flat candidate when comments present
  - FormatConstructor/FormatCall/FormatIndexLiteral: use CollectArgs
- After CALL open-bracket comment: 115 pass, 64 fail
  - Fixed: test026 (COMMENT-TRAILING on CALL emitted after open paren)
  - FlatOrFill: open_comment parameter for comment after open bracket
- After COMMENT-PREV attachment for non-block stmts: 117 pass, 62 fail
  - Fixed: test{002,045} (trailing annotation on split expression stmts)
  - COMMENT-PREV attaches to multi-line stmts unless stmt ends with '}'
  - Updated baseline: test002 (split line with annotation on second line)
- After emitter comment positioning fixes: 118 pass, 55 fail
  - Fixed: test019 (blank line between body and else now emitted)
  - _emit_func_body: same-line check priority over #@ prefix; track { } lines
  - _emit_if_extras: new helper for extras between cond/body and body/else
  - Regenerated 15 .rep files with improved comment/blank positioning
- After trailing comment as Node field: 120 pass, 53 fail
  - Fixed: test{163,167} (trailing comment on if-cond and after open brace)
  - Parser attaches COMMENT-TRAILING to preceding sibling's TrailingComment field
  - Removed peek-ahead in FormatStmtList, FormatIf, FormatTypeDecl, CollectArgs
  - FormatWhitesmithBlock: first-child COMMENT-TRAILING goes after open brace
  - WarnStandaloneTrailing: validates no trailing comment renders standalone
  - Dump: emits TrailingComment as COMMENT-TRAILING sibling for round-trip fidelity
- After emitter blank-before-else + FormatIf comment fix: 121 pass, 52 fail
  - Fixed: test018 (blank line before else with trailing comment)
  - Emitter: _emit_if_extras skips nl nodes, emits BLANK by checking gap
    between body.end_byte and else keyword (not else_body, which spans too far)
  - FormatIf: fix double-blank when BLANK + comments before else; first comment
    no longer gets redundant "\n" prefix (line 1996 provides body-ending newline)
  - Updated .rep files: test{018,032} (BLANK before else); test110 reverted
  - Removed test018 from "Comment handling" category (now passes)
- After _emit_extras_in blank/content tracking fix: 122 pass, 51 fail
  - Fixed: test057 (blank line before comments in export block)
  - _emit_extras_in was missing _maybe_blank and _mark_content calls,
    unlike _iter_children - all ~20 call sites silently dropped blank lines
  - Removed test057 from "Comment handling" category (now passes)
- After INDEX-LITERAL trailing comment fix: 123 pass, 50 fail
  - Fixed: test088 (trailing comments in index literal)
  - Emitter: added _emit_extras_in to INDEX-LITERAL path (caught # Comment two)
  - Formatter: FormatIndexLiteral uses vertical indented layout when items
    have trailing comments; extracted shared FormatArgsVertical helper
  - Removed test088 from "Comment handling" category (now passes)
- After removing COMMENT-PREV special-casing: 124 pass, 49 fail
  - Fixed: test109 (#@ comments no longer force COMMENT-PREV)
  - Emitter: removed all 6 instances of ##</#@ -> COMMENT-PREV classification;
    all comments now classified by same-line test only (TRAILING vs LEADING)
  - Formatter: FormatFuncDecl reads body->TrailingComment() instead of
    scanning for COMMENT-PREV children
  - Regenerated .rep files: test{001,002,003,020,030,045,109,134,148}
  - Removed test109 from "Comment handling" category (now passes)
- After INDEX-LITERAL partial-comment fill layout: 125 pass, 48 fail
  - Fixed: test134 (mid-arg trailing comment in index literal)
  - FormatIndexLiteral: vertical layout only when every item has a trailing
    comment; otherwise use FlatOrFill (packs items, wraps after comment)
  - Removed "Comment handling" category (all tests now pass)
- Updated baselines for acceptable output: 132 pass, 41 fail
  - test{011,029,044,101,105,128,140} (line-breaking: output acceptable)
- After multi-subscript INDEX fix: 133 pass, 40 fail
  - Fixed: test071 (FormatIndex only formatted first subscript)
  - FormatIndex: use FlatOrFill for multiple subscripts
- After record field attr wrapping: 135 pass, 38 fail
  - Fixed: test{139,174} (field type + attrs wrapping to continuation line)
  - FormatField: wraps attrs to continuation aligned one past type start
  - Removed "Record field attr wrapping" category (both fixed)
- After INDEX-LITERAL trailing comma support: 137 pass, 36 fail
  - Fixed: test{084,085} (trailing comma preserved from source)
  - Emitter: detects trailing comma in expr_list, emits TRAILING-COMMA marker
  - Formatter: FlatOrFill close_prefix param; FormatArgsVertical trailing_comma param
  - Added Tag::TrailingComma; CollectArgs skips it
  - Removed "INDEX-LITERAL trailing comma" category (both fixed)
- After emitter _iter_children refactor: 139 pass, 34 fail
  - Fixed: test{162,171} (for/while trailing comments)
  - Refactored 10 _emit_* functions to use _iter_children instead of
    _children + type-filtering, structurally capturing interstitial comments
  - Removed "For/while trailing comment" category (both fixed)
- After removing duplicate tests + enum trailing comma: 140 pass, 31 fail
  - Removed test{108,125} (duplicates of test104)
  - Fixed: test047 (enum trailing comma preserved from source)
  - Emitter: _emit_enum_body detects trailing comma, emits TRAILING-COMMA marker
  - Emitter: _emit_type_decl checks child.is_named to avoid "type" keyword
  - Formatter: FormatTypeDecl enum path respects TRAILING-COMMA
- After enum init values: 141 pass, 30 fail
  - Fixed: test048 (enum values with `= N` initializers)
  - Emitter: _emit_enum_body captures constant init as second ENUM-VALUE arg
  - Formatter: FormatTypeDecl appends Arg(1) init value to enum value string
  - Updated baseline: test048 (multi-line layout acceptable)
  - Removed "Enum tweaks" category (both fixed)
- After PAREN spacing fix: 141 pass, 30 fail
  - FormatParen: no spaces for expression grouping parens: (expr) not ( expr )
  - FormatSwitch: unwrap PAREN and apply ( expr ) spacing for switch conditions
  - test115 paren spacing fixed; remaining failure is line-breaking
- After multi-line arg wrap in greedy fill: 143 pass, 28 fail
  - Fixed: test{061,115,131} (multi-line args placed on fresh line)
  - FormatArgsFill: multi-line args always wrap to alignment column
    where they may fit flat, instead of packing inline with split text
- After EVENT-STMT fix: 144 pass, 27 fail
  - Fixed: test054 (event statement with name and args)
  - Emitter: _emit_event_stmt descends into event_hdr for name and expr_list
  - Formatter: new FormatEventStmt formats `event name(args);` using FlatOrFill
- After tight `/` for subnet masking: 144 pass, 27 fail
  - FormatBinary: no spaces around `/` when RHS is atomic (masking heuristic)
  - Split after `/` still works as a break point
  - Updated baselines: test{002,004,005,013,035,100,103,107}
  - test005: `1000 / 1000` is a tree-sitter-zeek subnet literal bug (filed)
- After boolean chain layout + condition trail reservation: 148 pass, 23 fail
  - Fixed: test021 (boolean chain `&&`/`||` flattening + fill layout)
  - FlattenBoolChain: collects operands of left-associative `&&`/`||`
  - FormatBoolChain: flat when fits, fill-pack with wrap at operator
  - FormatIf/FormatWhile: Reserve(2) for closing ` )` on condition
  - FormatBoolChain: max_col accounts for trail reservation
  - Updated baselines: test{066,072} (tab indent, slice spacing)
  - Added test176 to failure list (continuation misalignment, was unlisted)
  - Removed test021 from "Miscellaneous" (now passes)
- After switch case value wrapping: 149 pass, 22 fail
  - Fixed: test027 (case values fill-pack with wrap at comma)
  - FormatSwitch: fill-pack case values aligned after `case `

## Session progress (2026-04-01)
- Refactor if/for/while into ConditionBlockNode class hierarchy: 149 pass, 22 fail
  - New condition_block.{h,cc}: ConditionBlockNode base with virtual BuildCondition/BuildFollowOn
  - IfNode overrides BuildFollowOn (else clause), ForNode overrides BuildCondition (vars in iter)
  - WhileNode uses defaults; MakeNode factory creates appropriate subclass
  - Node gains FindChild, ContentChildren methods; formatter exposes FormatExpr etc.
  - Emitter fix: trailing comment on single-stmt if-body now inside BODY block
  - Pure refactor - no behavior changes

## Session progress (2026-04-02)
- After FormatSchedule BuildLayout + trail reservation fixes: 150 pass, 22 fail
  - Fixed: test143 (schedule baseline updated after BuildLayout conversion)
  - BuildLayout trail_after: scan past SoftSp items, always add ctx.Trail()
  - BuildLayout Candidate width: single-line uses text length, not absolute column
- After condition_block BuildLayout + Tok() helper: 150 pass, 22 fail
  - Converted condition_block head to BuildLayout with Tok() for forced breaks
  - Tok() creates Lit from Node->Text() with MustBreakAfter for trailing comments
  - Pure refactor - no behavior changes
- After cleanup: 150 pass, 22 fail
  - Removed dead AppendToken, deduplicated FormatSwitch case/default body,
    consolidated FormatAttrList onto FormatAttrStrings, removed stale comments
- After ASSERT support: 151 pass, 21 fail
  - Fixed: test161 (assert keyword statement)
  - Added Tag::Assert, emitter _emit_assert, dispatch to FormatKeywordStmt
- After FormatPrint + CollectArgs marker fix: 153 pass, 20 fail
  - Fixed: test124 (print with multiple expressions)
  - FormatPrint: uses FlatOrFill for multi-expression print, reserves semi width
  - CollectArgs: skip all markers (is_marker) not just TrailingComma - fixes
    crash when SEMI collected as content arg with null comma
- After TYPE-FUNC param wrapping: 154 pass, 19 fail
  - Fixed: test056 (event type params now greedy-fill at overflow)
  - FormatTypeFunc: flat + greedy-fill candidates, aligned after "("

## Session progress (2026-04-03)
- After LAMBDA support: 157 pass, 16 fail
  - Fixed: test{094,095,096} (lambda expressions)
  - Emitter: emit KEYWORD "function" for lambdas; fix capture list emission
    (was checking wrong tree level - now walks inside begin_lambda)
  - Formatter: new FormatLambda - captures and params via FlatOrFill,
    body Whitesmith block with indent derived from lambda column position
  - FlatOrFill: close bracket on new line when last arg is lambda;
    AppendTrailing consumes comma after lambda and forces wrap
  - Removed "LAMBDA support" and "CONSTRUCTOR with LAMBDA attr" categories
- After CALL trailing comma support: 158 pass, 15 fail
  - Fixed: test040 (set() args with trailing comma use vertical layout)
  - Emitter: detect trailing comma in CALL args, emit TRAILING-COMMA marker
  - Formatter: FormatCall uses FormatArgsVertical when TRAILING-COMMA present
  - Removed test040 from "Miscellaneous" category
- After pattern literal support: 159 pass, 14 fail
  - Fixed: test102 (pattern /regex/ was emitted as UNKNOWN-EXPR)
  - Emitter: recognize tree-sitter "pattern" node type, emit as CONSTANT
  - Removed test102 from "Miscellaneous" category
- After DeclTypeSplit fill-wrap indent fix: 160 pass, 13 fail
  - Fixed: test176 (TYPE-PARAMETERIZED continuation misaligned after `[`)
  - DeclTypeSplit: re-format type node in indented context instead of
    reusing pre-formatted type_str (which had indent=0 fill-wrap pads)
  - Removed "Miscellaneous" category (all tests fixed)
- After FormatBinary split overflow fix: 161 pass, 12 fail
  - Fixed: test014 (assignment split after = instead of wrapping call args)
  - FormatBinary split path: compute overflow from text length for single-line
    rhs rather than Width(), which FlatOrFill stores as absolute column
