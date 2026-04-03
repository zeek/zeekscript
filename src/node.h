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
	virtual ~Node() = default;

	Tag GetTag() const { return tag; }
	const std::vector<std::string>& Args() const { return args; }
	const NodeVec& Children() const { return children; }
	NodeVec& Children() { return children; }

	// Find a child node by tag, or nullptr if absent.
	const Node* FindOptChild(Tag tag) const;

	// Find a required child node by tag.  Aborts if not found.
	const Node* FindChild(Tag tag) const;

	// Find a child by tag, starting after the given child.
	const Node* FindChild(Tag tag, const Node* after) const;

	// Collect non-token, non-comment children.
	std::vector<const Node*> ContentChildren() const;

	// Same but there must be at least n or throw an exception.
	std::vector<const Node*> ContentChildren(const char* name, int n) const;

	const std::string& TrailingComment() const { return trailing_comment; }
	void SetTrailingComment(std::string c)
		{ trailing_comment = " " + std::move(c); }

	const std::vector<std::string>& PreComments() const
		{ return pre_comments; }
	void AddPreComment(std::string c)
		{ pre_comments.push_back(std::move(c)); }
	bool MustBreakBefore() const { return ! pre_comments.empty(); }

	// Marker nodes (BLANK, etc.) that appeared between the
	// pre-comments and this node - preserved for round-trip.
	const Node::NodeVec& PreMarkers() const { return pre_markers; }
	void AddPreMarker(std::shared_ptr<Node> m)
		{ pre_markers.push_back(std::move(m)); }

	void AddArg(std::string a) { args.push_back(std::move(a)); }
	void AddChild(std::shared_ptr<Node> child)
		{ children.push_back(std::move(child)); }

	// Convenience: i-th arg, or empty string if absent.
	const std::string& Arg(size_t i = 0) const;

	// Complete text for this node: for tokens, the syntax
	// string + trailing comment; for atoms, the arg + trailing
	// comment; for composite nodes, just the trailing comment.
	std::string Text() const;

	// Width of this node's text in columns.
	int Width() const { return static_cast<int>(Text().size()); }

	// True if a line break must follow this node (has a
	// trailing comment).
	bool MustBreakAfter() const { return ! trailing_comment.empty(); }

	bool HasBlock() const { return has_block; }
	void SetHasBlock() { has_block = true; }

	bool HasChildren() const { return ! children.empty(); }

	// Debug: print tree to stdout.
	void Dump(int indent = 0) const;

private:
	Tag tag;
	std::vector<std::string> args;
	NodeVec children;
	std::string trailing_comment;
	std::vector<std::string> pre_comments;
	NodeVec pre_markers;
	bool has_block = false;
};

// Factory: creates the appropriate Node subclass based on tag.
std::shared_ptr<Node> MakeNode(Tag tag);
