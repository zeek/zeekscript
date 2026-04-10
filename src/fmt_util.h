#pragma once

// Shared formatting utilities: overflow helpers, arg collection,
// flat/fill/vertical layout, and statement list formatting.

#include <string>
#include <vector>

#include "layout.h"
#include "tag.h"

// Helpers.
int ovf(int candidate_w, const FmtContext& ctx);
int ovf_no_trail(int candidate_w, const FmtContext& ctx);
int fit_col(int align_col, int w, int max_col);
// Arg lists with trailing comments.
struct ArgItem
	{
	LayoutPtr arg;
	std::vector<std::string> leading;	// leading comments before item
	LayoutPtr comma;			// preceding COMMA token, if any

	bool HasBreak() const
		{
		if ( ! leading.empty() )
			return true;
		if ( comma && comma->MustBreakAfter() )
			return true;
		return arg->MustBreakBefore() || arg->MustBreakAfter();
		}
	};

using ArgItems = std::vector<ArgItem>;

bool has_breaks(const ArgItems& items);

ArgItems collect_args(const LayoutVec& children,
                      LayoutPtr* dangling_comma = nullptr);

Candidate format_args_flat(const ArgItems& items, const FmtContext& ctx);

Candidate format_args_fill(const ArgItems& items, int align_col, int indent,
                         const FmtContext& first_line_ctx, int trail = 0,
                         const LayoutPtr& dangling_comma = nullptr);

Candidates flat_or_fill(const Formatting& prefix, const Formatting& open,
                      const Formatting& close, const Formatting& suffix,
                      const ArgItems& items, const FmtContext& ctx,
                      const std::string& open_comment = "",
                      const std::string& close_prefix = "",
                      const LayoutPtr& dangling_comma = nullptr);

Candidate format_args_vertical(const Formatting& open, const Formatting& close,
                             const ArgItems& items, const FmtContext& ctx,
                             bool trailing_comma = false,
                             const LayoutPtr& dangling_comma = nullptr);

Formatting format_stmt_list(const LayoutVec& nodes, const FmtContext& ctx,
                           bool skip_leading_blanks = false);
