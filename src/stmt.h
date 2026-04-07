#pragma once

#include "layout.h"
#include "node.h"

// Base class for statement nodes with virtual Format.

class StmtNode : public Node {
public:
	StmtNode(Tag t) : Node(t) { }
	virtual Candidates Format(const FmtContext& ctx) const = 0;
};

// Preprocessor directives.  Not StmtNodes (no Candidates), but
// provide FormatText() for FormatStmtList and depth-control queries.
// PREPROC-COND always opens depth and sits at column 0.  Plain
// PREPROC uses the directive string for @else/@endif handling.

class PreprocBaseNode : public Node {
public:
	PreprocBaseNode(Tag t) : Node(t) { }
	virtual FmtPtr FormatText() const = 0;
	bool OpensDepth() const;
	bool ClosesDepth() const;
	bool AtColumnZero() const;
};

class PreprocNode : public PreprocBaseNode {
public:
	PreprocNode() : PreprocBaseNode(Tag::Preproc) { }
	FmtPtr FormatText() const override;
};

class PreprocCondNode : public PreprocBaseNode {
public:
	PreprocCondNode() : PreprocBaseNode(Tag::PreprocCond) { }
	FmtPtr FormatText() const override;
};

// Global/local declarations: keyword name [: type] [= init] [attrs] ;
// Children: [0]=KEYWORD [1]=SP [2]=IDENTIFIER
//   [optional DECL-TYPE, DECL-INIT, ATTR-LIST] SEMI

class DeclNode : public StmtNode {
public:
	DeclNode(Tag t) : StmtNode(t) { }
	Candidates Format(const FmtContext& ctx) const override;

	const NodePtr& TypeWrapper() const
		{ return FindOptChild(Tag::DeclType); }
	const NodePtr& InitWrapper() const
		{ return FindOptChild(Tag::DeclInit); }
};

// (SwitchNode removed - now a layout table entry)
