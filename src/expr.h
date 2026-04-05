#pragma once

#include "layout.h"
#include "node.h"

// Base class for expression nodes with virtual Format.

class ExprNode : public Node {
public:
	ExprNode(Tag t) : Node(t) { }
	virtual Candidates Format(const FmtContext& ctx) const = 0;
};

// Atoms: IDENTIFIER, CONSTANT, TYPE-ATOM

class AtomNode : public ExprNode {
public:
	AtomNode(Tag t) : ExprNode(t) { }
	Candidates Format(const FmtContext& ctx) const override;
};

// INTERVAL

class IntervalNode : public ExprNode {
public:
	IntervalNode() : ExprNode(Tag::Interval) { }
	Candidates Format(const FmtContext& ctx) const override;
};

// Prefix operators: shared operand/op child structure.

class PrefixExprNode : public ExprNode {
public:
	PrefixExprNode(Tag t) : ExprNode(t) { }
protected:
	// Format child 1 bracketed by children 0 and 2.
	Candidates FormatBracketed(const FmtContext& ctx) const;
	// Format "op[sep]child(1)" - shared by Negation and Unary.
	Candidates FormatPrefix(const FmtContext& ctx,
	                        const std::string& sep) const;
};

class CardinalityNode : public PrefixExprNode {
public:
	CardinalityNode() : PrefixExprNode(Tag::Cardinality) { }
	Candidates Format(const FmtContext& ctx) const override;
};

class NegationNode : public PrefixExprNode {
public:
	NegationNode() : PrefixExprNode(Tag::Negation) { }
	Candidates Format(const FmtContext& ctx) const override;
};

class UnaryNode : public PrefixExprNode {
public:
	UnaryNode() : PrefixExprNode(Tag::UnaryOp) { }
	Candidates Format(const FmtContext& ctx) const override;
};

// Binary operators: shared lhs/op/rhs child structure.

class BinaryExprNode : public ExprNode {
public:
	BinaryExprNode(Tag t) : ExprNode(t) { }
protected:
	// Format "lhs sep op sep rhs" with optional split after op.
	// split_multiline: if false, skip split when either side is
	// multi-line (used by Div for subnet notation).
	Candidates FormatBinaryOp(const FmtContext& ctx,
	                          const std::string& sep,
	                          bool split_multiline) const;
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

class HasFieldNode : public BinaryExprNode {
public:
	HasFieldNode() : BinaryExprNode(Tag::HasField) { }
	Candidates Format(const FmtContext& ctx) const override;
};

class DivNode : public BinaryExprNode {
public:
	DivNode() : BinaryExprNode(Tag::Div) { }
	Candidates Format(const FmtContext& ctx) const override;
};

// Compound expressions

class FieldAccessNode : public ExprNode {
public:
	FieldAccessNode() : ExprNode(Tag::FieldAccess) { }
	Candidates Format(const FmtContext& ctx) const override;
};

class FieldAssignNode : public ExprNode {
public:
	FieldAssignNode() : ExprNode(Tag::FieldAssign) { }
	Candidates Format(const FmtContext& ctx) const override;
};

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

class ParenNode : public PrefixExprNode {
public:
	ParenNode() : PrefixExprNode(Tag::Paren) { }
	Candidates Format(const FmtContext& ctx) const override;
};

class ScheduleNode : public ExprNode {
public:
	ScheduleNode() : ExprNode(Tag::Schedule) { }
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
	virtual std::string BuildPrefix(const FmtContext& ctx) const;
	int ParamsPos() const
		{ return GetTag() == Tag::LambdaCaptures ? 3 : 2; }
	Candidates FormatLambda(const std::string& prefix,
	                        const FmtContext& ctx) const;
};

class LambdaCapturesNode : public LambdaNode {
public:
	LambdaCapturesNode() : LambdaNode(Tag::LambdaCaptures) { }
protected:
	std::string BuildPrefix(const FmtContext& ctx) const override;
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

// TYPE-FUNC: event(params), function(params): rettype

class TypeFuncNode : public ExprNode {
public:
	TypeFuncNode() : ExprNode(Tag::TypeFunc) { }
	Candidates Format(const FmtContext& ctx) const override;
};
