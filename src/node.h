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
//
// Nodes with a layout specification are formatted declaratively
// via BuildLayout + beam search.  Other nodes (tokens, markers,
// preproc) use the default fallback.

class Layout;
using LayoutPtr = std::shared_ptr<Layout>;
using LayoutVec = std::vector<LayoutPtr>;
extern const LayoutPtr null_node;

class Layout {
public:
	Layout(Tag tag) : tag(tag) {}
	Layout(Tag tag, const LayoutItems& li) : tag(tag), layout(li) {}

	Candidates Format(const FmtContext& ctx) const;

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

	const LayoutVec& Children() const { return children; }
	LayoutVec& Children() { return children; }

	// Direct positional child access.
	const LayoutPtr& Child(size_t i) const { return children[i]; }

	// Positional child access with tag verification.
	const LayoutPtr& Child(size_t i, Tag t) const;

	// Find a child node by tag, or null LayoutPtr if absent.
	const LayoutPtr& FindOptChild(Tag tag) const;

	// Find a required child node by tag.  Aborts if not found.
	const LayoutPtr& FindChild(Tag tag) const;

	// Find a child by tag, starting after the given child.
	const LayoutPtr& FindChild(Tag tag, const LayoutPtr& after) const;

	// Collect non-token, non-comment children.
	LayoutVec ContentChildren() const;

	// Same but there must be at least n or throw an exception.
	LayoutVec ContentChildren(const char* name, int n) const;

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
	const LayoutVec& PreMarkers() const { return pre_markers; }
	void AddPreMarker(LayoutPtr m)
		{ pre_markers.push_back(std::move(m)); }

	void AddArg(std::string a) { args.push_back(std::move(a)); }
	void AddChild(LayoutPtr child)
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
	const LayoutPtr& FindTypeChild() const;

	// Compute functions for declarative BuildLayout resolution.
	LayoutItem ComputeRetType(ComputeCtx& cctx, const FmtContext& ctx) const;
	LayoutItem ComputeParamType(ComputeCtx& cctx, const FmtContext& ctx) const;
	LayoutItem ComputeOfType(ComputeCtx& cctx, const FmtContext& ctx) const;
	LayoutItem ComputeEnumBody(ComputeCtx& cctx, const FmtContext& ctx) const;
	LayoutItem ComputeRecordBody(ComputeCtx& cctx, const FmtContext& ctx) const;
	LayoutItem ComputeElseFollowOn(ComputeCtx& cctx, const FmtContext& ctx) const;
	LayoutItem ComputeFuncRet(ComputeCtx& cctx, const FmtContext& ctx) const;
	LayoutItem ComputeFuncAttrs(ComputeCtx& cctx, const FmtContext& ctx) const;
	LayoutItem ComputeFuncBody(ComputeCtx& cctx, const FmtContext& ctx) const;
	LayoutItem ComputeLambdaPrefix(ComputeCtx& cctx, const FmtContext& ctx) const;
	LayoutItem ComputeLambdaRet(ComputeCtx& cctx, const FmtContext& ctx) const;
	LayoutItem ComputeLambdaBody(ComputeCtx& cctx, const FmtContext& ctx) const;
	LayoutItem ComputeSwitchExpr(ComputeCtx& cctx, const FmtContext& ctx) const;
	LayoutItem ComputeSwitchCases(ComputeCtx& cctx, const FmtContext& ctx) const;
	Candidates ComputeDecl(ComputeCtx& cctx, const FmtContext& ctx) const;

	// Format an ATTR-LIST node as a single string.
	Formatting FormatAttrList(const FmtContext& ctx) const;

	// Format an ATTR-LIST node as individual attr strings.
	std::vector<Formatting> FormatAttrStrings(const FmtContext& ctx) const;

	// Format a BODY node: Whitesmith block or indented single stmt.
	FmtPtr FormatBodyText(const FmtContext& ctx) const;

	// Format a Whitesmith-style braced block.
	Formatting FormatWhitesmithBlock(const FmtContext& ctx) const;

	// Preprocessor directive formatting and depth control.
	FmtPtr FormatText() const;
	bool OpensDepth() const;
	bool ClosesDepth() const;
	bool AtColumnZero() const;

	// Debug: print tree to stdout.
	void Dump(int indent = 0) const;

private:
	Tag tag;
	LayoutItems layout;
	std::vector<std::string> args;
	LayoutVec children;
	std::string trailing_comment;
	std::vector<std::string> pre_comments;
	LayoutVec pre_markers;
	bool has_block = false;
};

// Factory: creates a Layout with optional layout items based on tag.
LayoutPtr MakeNode(Tag tag);
