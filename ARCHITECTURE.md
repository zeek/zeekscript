# Zeekscript Formatter Architecture

This document captures architectural observations and potential refactoring opportunities for the zeekscript formatter codebase.

## Overview

The codebase follows a tree-traversal-based formatting pipeline:

```
Input Script (bytes)
    ↓
Script.parse() [script.py]
    ├→ Parse with Tree-Sitter → ts_tree
    ├→ Clone tree → custom Node tree (AST/CST distinction)
    └→ Patch tree (restructure for formatting)
    ↓
Script.format()
    ├→ Lookup Formatter class for root node via NodeMapper
    ├→ Recursively format tree via Formatter.format()
    └→ Write output to OutputStream
    ↓
OutputStream [output.py]
    ├→ Buffer formatted output
    ├→ Apply line wrapping (80-char target)
    ├→ Manage indentation and alignment
    └→ Strip trailing whitespace
    ↓
Output (bytes)
```

### Main Modules

| Module | Purpose | Size |
|--------|---------|------|
| `script.py` | Parse pipeline, tree construction/patching | ~220 lines |
| `formatter.py` | 39 formatter classes + NodeMapper registry | ~1100 lines |
| `output.py` | Output stream with line-breaking logic | ~700 lines |
| `node.py` | Custom Node wrapper around tree-sitter nodes | ~300 lines |

## Key Abstractions

### Hints (Flag Enum)

Hints signal line-breaking and formatting preferences from formatters to output stream:

```python
class Hint(enum.Flag):
    NONE = 0
    GOOD_AFTER_LB      # Line-break before this preferred (operators stay at end)
    NO_LB_BEFORE       # Never break before this (e.g., closing parens)
    NO_LB_AFTER        # Never break after this (e.g., opening parens)
    ZERO_WIDTH         # Doesn't contribute to line length
    COMPLEX_BLOCK      # Block complex enough to warrant multi-line
    INIT_ELEMENT       # Tab indent on wrap for multi-element initializers
    INIT_LENIENT       # Use lenient line length (don't wrap unless very long)
    ATTR_SPACES        # Spaces around '=' in attributes
    BRACE_TO_CONSTRUCTOR  # Transform {..} to set()/table()
```

Usage pattern:
- Formatters set hints when writing: `_format_child(hints=Hint.NO_LB_BEFORE)`
- OutputStream carries hints in `Output` objects
- Line-breaking algorithm checks hints to make decisions

### Node & Tree Structure

The custom `Node` class wraps tree-sitter nodes with dual navigation:
- **AST navigation**: `children`, `parent`, `prev_sibling`, `next_sibling` (skip comments/newlines)
- **CST navigation**: `prev_cst_sibling`, `next_cst_sibling`, `prev_cst_siblings[]`, `next_cst_siblings[]`

Tree patching groups CST nodes (comments) with their associated AST nodes.

### NodeMapper Registry

Dynamic formatter lookup via naming convention:
- Explicit mappings checked first
- Then convention-based: `"module_decl"` → `ModuleDeclFormatter`
- Falls back to base `Formatter` class

## Formatter Class Hierarchy

```
Formatter (base, ~21 methods)
├── NullFormatter (no-op)
├── ErrorFormatter (preserves error content)
├── LineFormatter (space-sep + newline)
│   ├── PreprocDirectiveFormatter
│   └── PragmaFormatter
├── SpaceSeparatedFormatter
│   ├── TypeFormatter + ComplexSequenceFormatterMixin
│   └── ExprFormatter + ComplexSequenceFormatterMixin
├── TypedInitializerFormatter
│   ├── GlobalDeclFormatter
│   └── StmtFormatter
├── ExprListFormatter + ComplexSequenceFormatterMixin
├── RedefEnumDeclFormatter + ComplexSequenceFormatterMixin
├── FuncDeclFormatter
│   ├── FuncHdrFormatter
│   ├── FuncParamsFormatter
│   └── FuncBodyFormatter
├── EventHdrFormatter
├── NlFormatter
├── CommentFormatter
│   ├── MinorCommentFormatter
│   └── ZeekygenCommentFormatter
└── ... (others)
```

### Mixin: ComplexSequenceFormatterMixin

Provides `has_comments()` method for multi-line layout decisions. Used by formatters that need to check if comments are present to decide between compact and expanded formatting.

## Line-Breaking Algorithm

Located in `OutputStream._flush_line()`, the algorithm:

1. **find_break_points()**: Collect viable break positions
   - Track nesting depth (parens/brackets/braces)
   - Mark comma positions (preferred for arg lists)
   - Identify GOOD_AFTER_LB tokens (operators)

2. **filter_break_points()**: Prefer breaks at outer nesting levels
   - Check for GOOD_AFTER_LB breaks first
   - Find minimum non-zero nesting depth
   - At each depth, prefer commas over other positions

3. **choose_break()**: Pick best single break point
   - Find breaks where both lines fit under 80 chars
   - Prefer GOOD_AFTER_LB breaks
   - Choose break that balances lines
   - Verify break is "worthwhile"

---

## Completed Refactoring

The following structural improvements have been applied:

- **StmtFormatter**: `format()` is now a ~25-line dispatcher delegating to 16 `_format_*` methods
- **ExprFormatter**: `format()` is now a ~35-line dispatcher delegating to 22 `_format_*` methods
- **LineBreaker class**: Extracted from `_flush_line()` — nested closures became class methods (`find_break_points`, `filter_break_points`, `choose_break`, `_write_linebreak`), main loop became `run()`
- **`_compact_length()`**: Deduplicated inline compact-length estimators in ExprFormatter
- **Alignment context managers**: `aligned_to` / `aligned_to_if_unset` replaced 18 save/restore pairs
- **BreakPoint/FilteredBreak NamedTuples**: Replaced raw tuples in line-breaker
- **Type fixes**: `_format_child(indent: bool)`, `Node.__hash__`, `Node.no_format` attribute
- **Dead code removal**: Unused debug print, unused variable

## Potential Further Improvements

### Medium Priority

#### Consolidate Predicate Methods in ExprFormatter

Multiple predicates share the pattern `len(nonerr_children) == 3 and token(offset=1) in (...)`:
`_is_binary_boolean`, `_is_binary_operator`, `_is_assignment`. Could unify with a parameterized
`_is_binary_op_of(operators)` method.

### Lower Priority

#### Document CST/AST Distinction

The dual navigation (AST vs CST) is a key design decision but not well documented in code
comments. Could add a design doc section to node.py.

#### OutputStream State Management

Many mutable flags (`_col`, `_tab_indent`, `_align_column`, `_use_linebreaks`,
`_use_tab_indent`, `_use_space_align`, `_current_hints`) interact in non-obvious ways.
Could benefit from invariant assertions or a state transition diagram.
