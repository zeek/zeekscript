#pragma once

#include "formatter.h"
#include "node.h"

// Base class for condition-block statements (if, for, while).
// Provides shared formatting: keyword ( condition ) body.

class ConditionBlockNode : public Node {
public:
	ConditionBlockNode(Tag t) : Node(t) { }

	Candidates Format(const FmtContext& ctx) const;

protected:
	// Position of RPAREN child (4 for if/while, 7 for for).
	virtual int RParenPos() const { return 4; }

	// Returns formatted text for the condition between parens.
	virtual std::string BuildCondition(const FmtContext& cond_ctx) const;

	// Returns any follow-on text after the body (e.g., else clause).
	virtual std::string BuildFollowOn(const FmtContext& /* ctx */) const
		{ return ""; }
};

class IfNoElseNode : public ConditionBlockNode {
public:
	IfNoElseNode() : ConditionBlockNode(Tag::IfNoElse) { }
};

class IfElseNode : public ConditionBlockNode {
public:
	IfElseNode() : ConditionBlockNode(Tag::IfElse) { }

protected:
	std::string BuildFollowOn(const FmtContext& ctx) const override;
};

class ForNode : public ConditionBlockNode {
public:
	ForNode() : ConditionBlockNode(Tag::For) { }

protected:
	int RParenPos() const override { return 7; }
	std::string BuildCondition(
		const FmtContext& cond_ctx) const override;
};

class WhileNode : public ConditionBlockNode {
public:
	WhileNode() : ConditionBlockNode(Tag::While) { }
};
