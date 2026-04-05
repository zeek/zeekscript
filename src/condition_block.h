#pragma once

#include "layout.h"
#include "node.h"

// Base class for condition-block statements (if, for, while).
// Provides shared formatting: keyword ( condition ) body.

class ConditionBlockNode : public Node {
public:
	ConditionBlockNode(Tag t) : Node(t) { }

	Candidates Format(const FmtContext& ctx) const;

protected:
	int RParenPos() const
		{ return GetTag() == Tag::For ? 7 : 4; }

	// Returns formatted text for the condition between parens.
	virtual Formatting BuildCondition(const FmtContext& cond_ctx) const;

	// Returns any follow-on text after the body (e.g., else clause).
	virtual FmtPtr BuildFollowOn(const FmtContext& /* ctx */) const
		{ return std::make_shared<Formatting>(""); }
};

class IfNoElseNode : public ConditionBlockNode {
public:
	IfNoElseNode() : ConditionBlockNode(Tag::IfNoElse) { }
};

class IfElseNode : public ConditionBlockNode {
public:
	IfElseNode() : ConditionBlockNode(Tag::IfElse) { }

protected:
	FmtPtr BuildFollowOn(const FmtContext& ctx) const override;
};

class ForNode : public ConditionBlockNode {
public:
	ForNode() : ConditionBlockNode(Tag::For) { }

protected:
	Formatting BuildCondition(const FmtContext& cond_ctx) const override;
};

class WhileNode : public ConditionBlockNode {
public:
	WhileNode() : ConditionBlockNode(Tag::While) { }
};
