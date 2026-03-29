#pragma once

#include <memory>
#include <string>
#include <vector>

// A node in the representation tree.  Each node has:
//   - tag:      semantic type (e.g. "BINARY-OP", "GLOBAL-DECL")
//   - args:     zero or more quoted string arguments
//   - children: zero or more child nodes (from a { } block)
//
// Leaf nodes like  IDENTIFIER "x"  have args but no children.
// Container nodes like  CALL { ... }  have children (and
// possibly args).
// Bare markers like  SEMI  or  BLANK  have neither.

class Node {
public:
	using NodeVec = std::vector<std::unique_ptr<Node>>;

	Node(std::string tag) : tag(std::move(tag)) {}

	const std::string& Tag() const { return tag; }
	const std::vector<std::string>& Args() const
		{ return args; }
	const NodeVec& Children() const
		{ return children; }

	void AddArg(std::string a)
		{ args.push_back(std::move(a)); }
	void AddChild(std::unique_ptr<Node> child)
		{ children.push_back(std::move(child)); }

	// Convenience: i-th arg, or empty string if absent.
	const std::string& Arg(size_t i = 0) const;

	bool HasBlock() const { return has_block; }
	void SetHasBlock() { has_block = true; }

	bool HasChildren() const
		{ return ! children.empty(); }

	// Debug: print tree to stdout.
	void Dump(int indent = 0) const;

private:
	std::string tag;
	std::vector<std::string> args;
	NodeVec children;
	bool has_block = false;
};
