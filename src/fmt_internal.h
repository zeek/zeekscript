#pragma once

// Internal header shared across formatter translation units.
// Not part of the public API - use formatter.h for that.

#include <string>
#include <vector>

#include "formatter.h"
#include "node.h"
#include "tag.h"

// Pre-comment / pre-marker emission.
std::string EmitPreComments(const Node& node, const std::string& pad);

// Helpers.
int Ovf(int candidate_w, const FmtContext& ctx);
int OvfNoTrail(int candidate_w, const FmtContext& ctx);
int FitCol(int align_col, int w, int max_col);
int CountLines(const std::string& s);
int LastLineLen(const std::string& s);
int TextOverflow(const std::string& text, int start_col, int max_col);
int MaxLineOverflow(const std::string& text, int start_col, int max_col);

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

bool HasBreaks(const ArgComments& items);

ArgComments CollectArgs(const NodeVec& children);

Candidate FormatArgsFlat(const ArgComments& items, const FmtContext& ctx);

Candidate FormatArgsFill(const ArgComments& items, int align_col, int indent,
                         const FmtContext& first_line_ctx);

Candidates FlatOrFill(const std::string& prefix, const std::string& open,
                      const std::string& close, const std::string& suffix,
                      const ArgComments& items, const FmtContext& ctx,
                      const std::string& open_comment = "",
                      const std::string& close_prefix = "");

Candidate FormatArgsVertical(const std::string& open, const std::string& close,
                             const ArgComments& items, const FmtContext& ctx,
                             bool trailing_comma = false);

// type.cc
const Node* FindTypeChild(const Node& node);
std::string FormatAttrList(const Node& node, const FmtContext& ctx);
std::vector<std::string> FormatAttrStrings(const Node& node,
                                           const FmtContext& ctx);

// stmt.cc
std::string FormatStmtList(const NodeVec& nodes, const FmtContext& ctx,
                           bool skip_leading_blanks = false);
