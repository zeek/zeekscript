#pragma once

#include "layout.h"
#include "node.h"

// Base class for expression nodes with virtual Format.

class ExprNode : public Node {
public:
	ExprNode(Tag t) : Node(t) { }
	virtual Candidates Format(const FmtContext& ctx) const = 0;
};

// Binary operators: shared lhs/op/rhs child structure.

class BinaryExprNode : public ExprNode {
public:
	BinaryExprNode(Tag t) : ExprNode(t) { }
};

class BinaryNode : public BinaryExprNode {
public:
	BinaryNode() : BinaryExprNode(Tag::BinaryOp) { }
	Candidates Format(const FmtContext& ctx) const override;
};

class BoolChainNode : public BinaryExprNode {
public:
	BoolChainNode() : BinaryExprNode(Tag::BoolChain) { }
	Candidates Format(const FmtContext& ctx) const override;
};

class DivNode : public BinaryExprNode {
public:
	DivNode() : BinaryExprNode(Tag::Div) { }
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

class IndexNode : public ExprNode {
public:
	IndexNode() : ExprNode(Tag::Index) { }
	Candidates Format(const FmtContext& ctx) const override;
};

class IndexLiteralNode : public ExprNode {
public:
	IndexLiteralNode() : ExprNode(Tag::IndexLiteral) { }
	Candidates Format(const FmtContext& ctx) const override;
};

class SliceNode : public ExprNode {
public:
	SliceNode() : ExprNode(Tag::Slice) { }
	Candidates Format(const FmtContext& ctx) const override;
};

class TernaryNode : public ExprNode {
public:
	TernaryNode() : ExprNode(Tag::Ternary) { }
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

// TYPE-PARAMETERIZED: table[k] of v, set[t], vector of t

class TypeParamNode : public ExprNode {
public:
	TypeParamNode() : ExprNode(Tag::TypeParameterized) { }
	Candidates Format(const FmtContext& ctx) const override;
};

// PARAM: name[: type]

class ParamNode : public ExprNode {
public:
	ParamNode() : ExprNode(Tag::Param) { }
	Candidates Format(const FmtContext& ctx) const override;
};
