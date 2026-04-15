#pragma once

// Declarative flat-or-split formatting: describe a sequence of pieces
// and one or more split points, and the interpreter tries flat first
// then generates split candidates.
//
// FmtStep, SplitAt, and FmtSteps are defined in layout.h so they
// can be stored in LayoutItem for the FlatSplit layout item kind.

#include "layout.h"

// Try flat layout first.  If overflow or if the flat form is
// multi-line, generate a split candidate for each SplitAt spec
// by breaking after the designated piece and re-formatting any
// Expr pieces after the break with the derived context.
// Returns flat + any split candidates.
//
// force_flat: sub-expressions are formatted at the base column
// rather than the accumulated position, so overflow is detected
// at this level rather than causing internal wrapping.
//
// offer_split: always include split candidates alongside flat,
// even when flat fits on one line.  Lets a parent search choose
// between the flat and split forms (e.g. FieldAccess, HasField).
// Also enables trail-aware sub-expression formatting.
Candidates flat_or_split(FmtSteps steps, const std::vector<SplitAt>& splits,
                         const FmtContext& ctx, bool force_flat = false,
                         bool offer_split = false);
