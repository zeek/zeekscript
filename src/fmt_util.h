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
struct ArgComment
	{
	LayoutPtr arg;
	std::string comment;	// trailing: empty or " # ..."
	std::vector<std::string> leading;	// leading comments before item
	LayoutPtr comma;			// preceding COMMA token, if any

	bool HasBreak() const
		{
		if ( ! comment.empty() || ! leading.empty() )
			return true;
		if ( comma && comma->MustBreakAfter() )
			return true;
		return arg->MustBreakBefore() || arg->MustBreakAfter();
		}
	};

using ArgComments = std::vector<ArgComment>;

bool has_breaks(const ArgComments& items);

ArgComments collect_args(const LayoutVec& children);

Candidate format_args_flat(const ArgComments& items, const FmtContext& ctx);

Candidate format_args_fill(const ArgComments& items, int align_col, int indent,
                         const FmtContext& first_line_ctx, int trail = 0);

Candidates flat_or_fill(const Formatting& prefix, const Formatting& open,
                      const Formatting& close, const Formatting& suffix,
                      const ArgComments& items, const FmtContext& ctx,
                      const std::string& open_comment = "",
                      const std::string& close_prefix = "");

Candidate format_args_vertical(const Formatting& open, const Formatting& close,
                             const ArgComments& items, const FmtContext& ctx,
                             bool trailing_comma = false);

Formatting format_stmt_list(const LayoutVec& nodes, const FmtContext& ctx,
                           bool skip_leading_blanks = false);
