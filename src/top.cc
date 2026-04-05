#include <cstdio>

#include "fmt_util.h"

Candidates format_expr(const Node& node, const FmtContext& ctx)
	{
	return node.Format(ctx);
	}

Candidates format_node(const Node& node, const FmtContext& ctx)
	{
	return node.Format(ctx);
	}

// Collect all trailing comments from node fields.
static void collect_trailing(const Node& node,
                            std::vector<std::string>& out)
	{
	if ( ! node.TrailingComment().empty() )
		out.push_back(node.TrailingComment());
	for ( const auto& c : node.Children() )
		collect_trailing(*c, out);
	}

// Check that every trailing comment appears on a line that has
// preceding content - never as a standalone line.
static void warn_standalone_trailing(const std::string& output,
                                   const NodeVec& nodes)
	{
	std::vector<std::string> trailing;
	for ( const auto& n : nodes )
		collect_trailing(*n, trailing);

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

	auto result = format_stmt_list(nodes, ctx);
	warn_standalone_trailing(result, nodes);
	return result;
	}
