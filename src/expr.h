#pragma once

#include "layout.h"
#include "node.h"

// Base class for expression nodes with virtual Format.

class ExprNode : public Node {
public:
	ExprNode(Tag t) : Node(t) { }
	virtual Candidates Format(const FmtContext& ctx) const = 0;
};

// Compound expressions

class LambdaNode : public ExprNode {
public:
	LambdaNode() : ExprNode(Tag::Lambda) { }
	LambdaNode(Tag t) : ExprNode(t) { }
	Candidates Format(const FmtContext& ctx) const override;
protected:
	virtual Formatting BuildPrefix(const FmtContext& ctx) const;
	int ParamsPos() const
		{ return GetTag() == Tag::LambdaCaptures ? 3 : 2; }
	Candidates FormatLambda(const Formatting& prefix,
	                        const FmtContext& ctx) const;
};

class LambdaCapturesNode : public LambdaNode {
public:
	LambdaCapturesNode() : LambdaNode(Tag::LambdaCaptures) { }
protected:
	Formatting BuildPrefix(const FmtContext& ctx) const override;
};


