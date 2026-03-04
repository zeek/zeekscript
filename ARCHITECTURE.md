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

## Potential Refactoring Opportunities

### High Priority

#### 1. Extract Statement Type Handlers from StmtFormatter

**Current state**: `StmtFormatter.format()` is ~205 lines handling 14+ statement types in one method with if/elif chains.

**Suggestion**: Extract into separate methods or handler classes:

```python
class StmtFormatter:
    def format(self):
        handlers = {
            "if": self._format_if_stmt,
            "switch": self._format_switch_stmt,
            "for": self._format_for_stmt,
            "while": self._format_while_stmt,
            "when": self._format_when_stmt,
            "return": self._format_return_stmt,
            "{": self._format_block_stmt,
            # ...
        }
        handler = handlers.get(self._get_child_token(), self._format_default)
        handler()

    def _format_if_stmt(self):
        # ~50 lines of focused if-statement logic
        ...

    def _format_switch_stmt(self):
        # ~30 lines of focused switch logic
        ...
```

**Benefits**:
- Each method is testable in isolation
- Easier to understand and modify specific statement types
- Reduces cyclomatic complexity
- Clear extension point for new statement types

#### 2. Extract Expression Type Handlers from ExprFormatter

**Current state**: `ExprFormatter.format()` is ~259 lines handling 13+ expression types.

**Suggestion**: Same dispatch pattern as StmtFormatter:

```python
class ExprFormatter:
    def format(self):
        # Dispatch based on expression structure
        if self._is_index_expr():
            self._format_index_expr()
        elif self._is_call_expr():
            self._format_call_expr()
        elif self._is_binary_op():
            self._format_binary_op()
        elif self._is_constructor():
            self._format_constructor()
        # ...

    def _format_index_expr(self):
        # Focused indexing logic
        ...

    def _format_constructor(self):
        # Focused constructor logic (currently ~80 lines)
        ...
```

**Alternative**: Use handler classes for complex expression types:

```python
class ConstructorExprHandler:
    def __init__(self, formatter: ExprFormatter):
        self.f = formatter

    def format(self):
        # All constructor formatting logic here
        ...
```

#### 3. Decompose OutputStream._flush_line()

**Current state**: 500+ lines with 4 nested helper functions and deep nesting (5+ levels).

**Suggestion**: Extract into a LineBreaker class or separate methods:

```python
class LineBreaker:
    def __init__(self, items: list[Output], line_limit: int = 80):
        self.items = items
        self.line_limit = line_limit

    def find_break_points(self) -> list[BreakPoint]:
        # Currently nested function, ~100 lines
        ...

    def filter_break_points(self, break_points: list[BreakPoint]) -> list[BreakPoint]:
        # Currently nested function, ~80 lines
        ...

    def choose_break(self, break_points: list[BreakPoint]) -> int:
        # Currently nested function, ~60 lines
        ...

    def apply_break(self, break_index: int) -> tuple[bytes, list[Output]]:
        # Apply chosen break, return line and remaining items
        ...
```

### Medium Priority

#### 4. Consolidate Predicate Methods in ExprFormatter

**Current state**: Multiple similar predicates with repeated patterns:

```python
def _is_binary_boolean(self) -> bool:
    return len(self.node.nonerr_children) == 3 and \
           self._get_child_token(offset=1, absolute=True) in ("||", "&&")

def _is_binary_operator(self) -> bool:
    return len(self.node.nonerr_children) == 3 and \
           self._get_child_token(offset=1, absolute=True) in ("/", "*", "+", "-", "%", ...)

def _is_assignment(self) -> bool:
    return len(self.node.nonerr_children) == 3 and \
           self._get_child_token(offset=1, absolute=True) in ("=", "+=", "-=", ...)
```

**Suggestion**: Single parameterized method:

```python
BINARY_BOOLEAN_OPS = {"||", "&&"}
BINARY_ARITHMETIC_OPS = {"/", "*", "+", "-", "%", ...}
ASSIGNMENT_OPS = {"=", "+=", "-=", ...}

def _is_binary_op_of(self, operators: set[str]) -> bool:
    return (len(self.node.nonerr_children) == 3 and
            self._get_child_token(offset=1, absolute=True) in operators)

# Usage
if self._is_binary_op_of(BINARY_BOOLEAN_OPS):
    ...
```

#### 5. Clarify Parameter Types

**Current state**: Some parameters have misleading types:

```python
def _format_child(self, child: Node | None = None,
                  indent: int = False,  # Actually boolean!
                  hints: Hint = Hint.NONE) -> None:
```

**Suggestion**: Fix type annotation or use dataclass:

```python
@dataclass
class FormatOptions:
    indent: bool = False
    hints: Hint = Hint.NONE

def _format_child(self, child: Node | None = None,
                  opts: FormatOptions | None = None) -> None:
```

### Lower Priority

#### 6. Explicit Formatter Registration

**Current state**: NodeMapper uses naming convention for auto-discovery.

**Trade-off**: Convention is convenient but can be surprising. Explicit registration would be clearer:

```python
# Current (implicit)
class ModuleDeclFormatter(Formatter):  # Auto-registered for "module_decl"
    ...

# Alternative (explicit)
@register_formatter("module_decl")
class ModuleDeclFormatter(Formatter):
    ...
```

#### 7. Document CST/AST Distinction

The dual navigation (AST vs CST) is a key design decision but not well documented in code comments. Consider adding a design doc section to node.py explaining:
- Why both are needed
- When to use which
- How tree patching works

#### 8. State Management in OutputStream

**Current state**: Many mutable flags create complex state machine:
- `_col`, `_tab_indent`, `_align_column`
- `_use_linebreaks`, `_use_tab_indent`, `_use_space_align`
- `_current_hints`

**Consideration**: These interact in non-obvious ways. Could benefit from:
- State transition diagram in comments
- Invariant assertions
- Or restructuring to reduce mutable state

---

## Testing Considerations

When refactoring:

1. **Preserve snapshot tests**: The `test_samples.py` snapshots catch formatting regressions
2. **Add unit tests for extracted methods**: Each `_format_*_stmt()` method should have targeted tests
3. **Test edge cases**: Empty blocks, single-element constructs, deeply nested expressions
4. **Verify comment preservation**: Comments must not be lost or misplaced

## Summary

| Issue | Priority | Effort | Impact |
|-------|----------|--------|--------|
| Extract StmtFormatter handlers | High | Medium | High maintainability improvement |
| Extract ExprFormatter handlers | High | Medium | High maintainability improvement |
| Decompose _flush_line() | High | High | Improves debuggability |
| Consolidate predicates | Medium | Low | Reduces duplication |
| Fix parameter types | Medium | Low | Improves clarity |
| Explicit formatter registration | Low | Low | Minor clarity improvement |
| Document CST/AST | Low | Low | Improves onboarding |

The architecture is fundamentally sound. The main opportunities are reducing method size and improving separation of concerns within the existing structure.
