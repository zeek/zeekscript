#pragma once

#include "tag.h"

#include <memory>
#include <string>
#include <vector>

// A node in the representation tree.  Each node has:
//   - tag:      semantic type (e.g. Tag::BinaryOp)
//   - args:     zero or more string arguments
//   - children: zero or more child nodes (from { } block)
//
// Leaf nodes like  IDENTIFIER "x"  have args, no children.
// Container nodes like  CALL { ... }  have children (and
// possibly args).
// Bare markers like  SEMI  or  BLANK  have neither.

class Node {
public:
	using NodeVec = std::vector<std::shared_ptr<Node>>;

	Node(Tag tag) : tag(tag) {}

	Tag GetTag() const { return tag; }
	const std::vector<std::string>& Args() const
		{ return args; }
	const NodeVec& Children() const
		{ return children; }
	NodeVec& Children()
		{ return children; }

	const std::string& TrailingComment() const
		{ return trailing_comment; }
	void SetTrailingComment(std::string c)
		{ trailing_comment = " " + std::move(c); }

	void AddArg(std::string a)
		{ args.push_back(std::move(a)); }
	void AddChild(std::shared_ptr<Node> child)
		{ children.push_back(std::move(child)); }

	// Convenience: i-th arg, or empty string if absent.
	const std::string& Arg(size_t i = 0) const;

	// Complete text for this node: for tokens, the syntax
	// string + trailing comment; for atoms, the arg + trailing
	// comment; for composite nodes, just the trailing comment.
	std::string Text() const;

	// True if a line break must follow this node (has a
	// trailing comment).
	bool MustBreak() const
		{ return ! trailing_comment.empty(); }

	bool HasBlock() const { return has_block; }
	void SetHasBlock() { has_block = true; }

	bool HasChildren() const
		{ return ! children.empty(); }

	// Debug: print tree to stdout.
	void Dump(int indent = 0) const;

private:
	Tag tag;
	std::vector<std::string> args;
	NodeVec children;
	std::string trailing_comment;
	bool has_block = false;
};
