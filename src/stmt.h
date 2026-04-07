#pragma once

#include "layout.h"
#include "node.h"

// Preprocessor directives.  Provide FormatText() for
// FormatStmtList and depth-control queries.
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
