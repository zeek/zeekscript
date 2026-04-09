#include <cstdio>

#include "fmt_util.h"

Candidates format_expr(const Layout& node, const FmtContext& ctx)
	{
	return node.Format(ctx);
	}

Candidates format_node(const Layout& node, const FmtContext& ctx)
	{
	return node.Format(ctx);
	}

// Collect all trailing comments from node fields.
static void collect_trailing(const Layout& node,
                            std::vector<std::string>& out)
	{
	if ( ! node.TrailingComment().empty() )
		out.push_back(node.TrailingComment());
	for ( const auto& c : node.Children() )
		collect_trailing(*c, out);
	}

// Check that every trailing comment appears on a line that has
// preceding content - never as a standalone line.  Uses a simple
// string search, so a duplicate comment could match the wrong
// occurrence.
static void warn_standalone_trailing(const std::string& output,
                                   const LayoutVec& nodes)
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

// Post-process: shift overflowing lines left by removing leading
// spaces (never tabs), so the line ends at column max_col - 1.
// Stops at indent_col + 1 (one past the indent level).
static std::string reduce_overflow(const std::string& text, int max_col)
	{
	std::string result;
	size_t pos = 0;

	while ( pos < text.size() )
		{
		auto nl = text.find('\n', pos);
		auto line = text.substr(pos, nl == std::string::npos
					? std::string::npos : nl - pos);
		pos = (nl == std::string::npos) ? text.size() : nl + 1;

		// Count leading tabs and spaces.
		int n_tabs = 0;
		size_t i = 0;
		while ( i < line.size() && line[i] == '\t' )
			{ ++n_tabs; ++i; }

		int n_spaces = 0;
		while ( i < line.size() && line[i] == ' ' )
			{ ++n_spaces; ++i; }

		int indent_col = n_tabs * INDENT_WIDTH;
		int content_col = indent_col + n_spaces;
		int content_len = static_cast<int>(line.size()) - n_tabs
				- n_spaces;
		int line_end = content_col + content_len;

		// Shift left to end at max_col - 1, but keep at
		// least indent_col + 1 as the starting column.
		if ( line_end > max_col && n_spaces > 0 )
			{
			int ovf = line_end - max_col + 1;
			int removable = std::min(ovf, n_spaces - 1);

			if ( removable > 0 && removable >= ovf )
				line = line.substr(0, n_tabs) +
					std::string(n_spaces - removable, ' ') +
					line.substr(n_tabs + n_spaces);
			}

		result += line;
		if ( nl != std::string::npos )
			result += '\n';
		}

	return result;
	}

std::string Format(const LayoutVec& nodes, bool raw)
	{
	static constexpr int MAX_WIDTH = 80;
	FmtContext ctx(0, 0, MAX_WIDTH);

	auto result = format_stmt_list(nodes, ctx);
	warn_standalone_trailing(result.Str(), nodes);

	if ( raw )
		return result.Str();

	return reduce_overflow(result.Str(), MAX_WIDTH);
	}
