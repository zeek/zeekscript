#pragma once

// Declarative flat-or-split formatting: describe a sequence of pieces
// and one or more split points, and the interpreter tries flat first
// then generates split candidates.
//
// FmtStep, SplitAt, and FmtSteps are defined in layout.h so they
// can be stored in LayoutItem for the FlatSplit layout item kind.

#include "layout.h"

// Try flat layout first.  If overflow, generate a split candidate
// for each SplitAt spec by breaking after the designated piece and
// re-formatting any Expr pieces after the break with the derived
// context.  Returns flat + any split candidates.
//
// force_flat: when true, sub-expressions in the flat layout are
// formatted at the base column rather than the accumulated
// position.  This prevents them from wrapping internally and
// ensures overflow is detected at this level.
Candidates flat_or_split(FmtSteps steps, const std::vector<SplitAt>& splits,
                         const FmtContext& ctx, bool force_flat = false,
                         bool always_split = false);
