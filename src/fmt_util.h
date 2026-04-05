#pragma once

// Shared formatting utilities: overflow helpers, arg collection,
// flat/fill/vertical layout, and statement list formatting.

#include <string>
#include <vector>

#include "layout.h"
#include "node.h"
#include "tag.h"

// Helpers.
int ovf(int candidate_w, const FmtContext& ctx);
int ovf_no_trail(int candidate_w, const FmtContext& ctx);
int fit_col(int align_col, int w, int max_col);
int count_lines(const std::string& s);
int last_line_len(const std::string& s);
int text_overflow(const std::string& text, int start_col, int max_col);
int max_line_overflow(const std::string& text, int start_col, int max_col);

// Arg lists with trailing comments.
struct ArgComment
	{
	const Node* arg;
	std::string comment;	// trailing: empty or " # ..."
	std::vector<std::string> leading;	// leading comments before item
	const Node* comma = nullptr;	// preceding COMMA token, if any

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

ArgComments collect_args(const NodeVec& children);

Candidate format_args_flat(const ArgComments& items, const FmtContext& ctx);

Candidate format_args_fill(const ArgComments& items, int align_col, int indent,
                         const FmtContext& first_line_ctx);

Candidates flat_or_fill(const std::string& prefix, const std::string& open,
                      const std::string& close, const std::string& suffix,
                      const ArgComments& items, const FmtContext& ctx,
                      const std::string& open_comment = "",
                      const std::string& close_prefix = "");

Candidate format_args_vertical(const std::string& open, const std::string& close,
                             const ArgComments& items, const FmtContext& ctx,
                             bool trailing_comma = false);

Formatting format_stmt_list(const NodeVec& nodes, const FmtContext& ctx,
                           bool skip_leading_blanks = false);
