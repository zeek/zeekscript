#pragma once

#include "layout.h"
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

class Node;
using NodePtr = std::shared_ptr<Node>;
using NodeVec = std::vector<NodePtr>;
extern const NodePtr null_node;

class Node {
public:
	Node(Tag tag) : tag(tag) {}
	virtual ~Node() = default;
	virtual Candidates Format(const FmtContext& ctx) const;

	// Layout combinator: resolves integer LayoutItems as tok(Child(i))
	// before delegating to the beam-search layout engine.
	Candidates BuildLayout(LayoutItems items,
	                       const FmtContext& ctx) const;

	Tag GetTag() const { return tag; }
	bool IsLambda() const { return is_lambda(tag); }
	bool IsMarker() const { return is_marker(tag); }
	bool IsToken() const { return is_token(tag); }
	bool IsType() const { return is_type_tag(tag); }

	const std::vector<std::string>& Args() const { return args; }

	const NodeVec& Children() const { return children; }
	NodeVec& Children() { return children; }

	// Direct positional child access.
	const NodePtr& Child(size_t i) const { return children[i]; }

	// Positional child access with tag verification.
	const NodePtr& Child(size_t i, Tag t) const;

	// Find a child node by tag, or null NodePtr if absent.
	const NodePtr& FindOptChild(Tag tag) const;

	// Find a required child node by tag.  Aborts if not found.
	const NodePtr& FindChild(Tag tag) const;

	// Find a child by tag, starting after the given child.
	const NodePtr& FindChild(Tag tag, const NodePtr& after) const;

	// Collect non-token, non-comment children.
	NodeVec ContentChildren() const;

	// Same but there must be at least n or throw an exception.
	NodeVec ContentChildren(const char* name, int n) const;

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
	const NodeVec& PreMarkers() const { return pre_markers; }
	void AddPreMarker(NodePtr m)
		{ pre_markers.push_back(std::move(m)); }

	void AddArg(std::string a) { args.push_back(std::move(a)); }
	void AddChild(NodePtr child)
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

	// Emit pre-comments and pre-markers as indented lines.
	FmtPtr EmitPreComments(const std::string& pad) const;

	// Find the first type child (TypeAtom, TypeParameterized, TypeFunc).
	const NodePtr& FindTypeChild() const;

	// Compute functions for declarative BuildLayout resolution.
	FmtPtr ComputeRetType(ComputeCtx& cctx, const FmtContext& ctx) const;

	// Format an ATTR-LIST node as a single string.
	Formatting FormatAttrList(const FmtContext& ctx) const;

	// Format an ATTR-LIST node as individual attr strings.
	std::vector<Formatting> FormatAttrStrings(const FmtContext& ctx) const;

	// Format a BODY node: Whitesmith block or indented single stmt.
	FmtPtr FormatBodyText(const FmtContext& ctx) const;

	// Format a Whitesmith-style braced block.
	Formatting FormatWhitesmithBlock(const FmtContext& ctx) const;

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

// A node whose Format is purely declarative: just runs BuildLayout
// on a fixed sequence of LayoutItems.
class LayoutNode : public Node {
public:
	LayoutNode(Tag t, const LayoutItems& items) : Node(t), layout(items) {}
	Candidates Format(const FmtContext& ctx) const override
		{ return BuildLayout(layout, ctx); }

private:
	LayoutItems layout;
};

// Factory: creates the appropriate Node subclass based on tag.
NodePtr MakeNode(Tag tag);
