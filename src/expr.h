#pragma once

#include "layout.h"
#include "node.h"

// Base class for expression nodes with virtual Format.

class ExprNode : public Node {
public:
	ExprNode(Tag t) : Node(t) { }
	virtual Candidates Format(const FmtContext& ctx) const = 0;
};

// Boolean chain: operands are direct children (flattened),
// formatted with fill layout.

class BoolChainNode : public ExprNode {
public:
	BoolChainNode() : ExprNode(Tag::BoolChain) { }
	Candidates Format(const FmtContext& ctx) const override;
};

// Compound expressions

class CallNode : public ExprNode {
public:
	CallNode() : ExprNode(Tag::Call) { }
	Candidates Format(const FmtContext& ctx) const override;
};

class ConstructorNode : public ExprNode {
public:
	ConstructorNode() : ExprNode(Tag::Constructor) { }
	Candidates Format(const FmtContext& ctx) const override;
};

class IndexLiteralNode : public ExprNode {
public:
	IndexLiteralNode() : ExprNode(Tag::IndexLiteral) { }
	Candidates Format(const FmtContext& ctx) const override;
};

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


