#include <cstdio>

#include "fmt_internal.h"

Candidates FormatExpr(const Node& node, const FmtContext& ctx)
	{
	return node.Format(ctx);
	}

Candidates FormatNode(const Node& node, const FmtContext& ctx)
	{
	return node.Format(ctx);
	}

// Collect all trailing comments from node fields.
static void CollectTrailing(const Node& node,
                            std::vector<std::string>& out)
	{
	if ( ! node.TrailingComment().empty() )
		out.push_back(node.TrailingComment());
	for ( const auto& c : node.Children() )
		CollectTrailing(*c, out);
	}

// Check that every trailing comment appears on a line that has
// preceding content - never as a standalone line.
static void WarnStandaloneTrailing(const std::string& output,
                                   const NodeVec& nodes)
	{
	std::vector<std::string> trailing;
	for ( const auto& n : nodes )
		CollectTrailing(*n, trailing);

	for ( const auto& text : trailing )
		{
		auto pos = output.find(text);
		if ( pos == std::string::npos )
			{
			fprintf(stderr, "warning: trailing comment dropped: "
			        "%s\n", text.c_str());
			continue;
			}

		// Find the start of this line.
		auto sol = output.rfind('\n', pos);
		sol = (sol == std::string::npos) ? 0 : sol + 1;

		// Check whether there is non-whitespace before the comment.
		bool has_content = false;
		for ( auto i = sol; i < pos; ++i )
			if ( output[i] != ' ' && output[i] != '\t' )
				{
				has_content = true;
				break;
				}

		if ( ! has_content )
			fprintf(stderr, "warning: trailing comment on its own "
			        "line: %s\n", text.c_str());
		}
	}

std::string Format(const NodeVec& nodes)
	{
	static constexpr int MAX_WIDTH = 80;
	FmtContext ctx(0, 0, MAX_WIDTH);

	auto result = FormatStmtList(nodes, ctx);
	WarnStandaloneTrailing(result, nodes);
	return result;
	}
