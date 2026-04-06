#include "tag.h"

#include <unordered_map>

struct TagEntry {
	Tag tag;
	bool token;  // true for syntactic tokens (COMMA, LPAREN, etc.)
};

static const std::unordered_map<std::string, TagEntry> tag_map = {
	// Expressions
	{"BINARY-OP", {Tag::BinaryOp, false}},
	{"BOOL-CHAIN", {Tag::BoolChain, false}},
	{"CARDINALITY", {Tag::Cardinality, false}},
	{"DIV", {Tag::Div, false}},
	{"HAS-FIELD", {Tag::HasField, false}},
	{"NEGATION", {Tag::Negation, false}},
	{"UNARY-OP", {Tag::UnaryOp, false}},
	{"TERNARY", {Tag::Ternary, false}},
	{"PAREN", {Tag::Paren, false}},
	{"CALL", {Tag::Call, false}},
	{"INDEX", {Tag::Index, false}},
	{"INDEX-LITERAL", {Tag::IndexLiteral, false}},
	{"SLICE", {Tag::Slice, false}},
	{"FIELD-ACCESS", {Tag::FieldAccess, false}},
	{"FIELD-ASSIGN", {Tag::FieldAssign, false}},
	{"BRACE-INIT", {Tag::BraceInit, false}},
	{"CONSTRUCTOR", {Tag::Constructor, false}},
	{"SCHEDULE", {Tag::Schedule, false}},
	{"KEYWORD-EXPR", {Tag::KeywordExpr, false}},
	{"LAMBDA", {Tag::Lambda, false}},
	{"LAMBDA-CAPTURES", {Tag::LambdaCaptures, false}},
	{"WHEN-LOCAL", {Tag::WhenLocal, false}},

	// Expression atoms
	{"IDENTIFIER", {Tag::Identifier, false}},
	{"CONSTANT", {Tag::Constant, false}},
	{"INTERVAL", {Tag::Interval, false}},

	// Types
	{"TYPE-ATOM", {Tag::TypeAtom, false}},
	{"TYPE-PARAMETERIZED", {Tag::TypeParameterized, false}},
	{"TYPE-FUNC", {Tag::TypeFunc, false}},
	{"TYPE-RECORD", {Tag::TypeRecord, false}},
	{"TYPE-ENUM", {Tag::TypeEnum, false}},

	// Declarations
	{"GLOBAL-DECL", {Tag::GlobalDecl, false}},
	{"LOCAL-DECL", {Tag::LocalDecl, false}},
	{"DECL-TYPE", {Tag::DeclType, false}},
	{"DECL-INIT", {Tag::DeclInit, false}},
	{"TYPE-DECL", {Tag::TypeDecl, false}},
	{"TYPEDECL-ALIAS", {Tag::TypeDeclAlias, false}},
	{"TYPEDECL-ENUM", {Tag::TypeDeclEnum, false}},
	{"TYPEDECL-RECORD", {Tag::TypeDeclRecord, false}},
	{"FUNC-DECL", {Tag::FuncDecl, false}},
	{"EXPORT", {Tag::ExportDecl, false}},
	{"MODULE", {Tag::ModuleDecl, false}},
	{"REDEF-RECORD", {Tag::RedefRecord, false}},
	{"REDEF-ENUM", {Tag::RedefEnum, false}},

	// Statements
	{"EXPR-STMT", {Tag::ExprStmt, false}},
	{"IF-NO-ELSE", {Tag::IfNoElse, false}},
	{"IF-WITH-ELSE", {Tag::IfElse, false}},
	{"FOR", {Tag::For, false}},
	{"WHILE", {Tag::While, false}},
	{"SWITCH", {Tag::Switch, false}},
	{"WHEN", {Tag::When, false}},
	{"RETURN", {Tag::Return, false}},
	{"RETURN-VOID", {Tag::ReturnVoid, false}},
	{"PRINT", {Tag::Print, false}},
	{"EVENT-STMT", {Tag::EventStmt, false}},
	{"ADD", {Tag::Add, false}},
	{"DELETE", {Tag::Delete, false}},
	{"ASSERT", {Tag::Assert, false}},
	{"BLOCK", {Tag::Block, false}},

	// Statement/declaration parts
	{"ARGS", {Tag::Args, false}},
	{"SUBSCRIPTS", {Tag::Subscripts, false}},
	{"PARAMS", {Tag::Params, false}},
	{"PARAM", {Tag::Param, false}},
	{"RETURNS", {Tag::Returns, false}},
	{"BODY", {Tag::Body, false}},
	{"ELSE-IF", {Tag::ElseIf, false}},
	{"ELSE-BODY", {Tag::ElseBody, false}},
	{"ITERABLE", {Tag::Iterable, false}},
	{"VARS", {Tag::Vars, false}},
	{"CAPTURES", {Tag::Captures, false}},
	{"FIELD", {Tag::Field, false}},
	{"ENUM-VALUE", {Tag::EnumValue, false}},
	{"CASE", {Tag::Case, false}},
	{"DEFAULT", {Tag::Default, false}},
	{"VALUES", {Tag::Values, false}},
	{"TIMEOUT", {Tag::Timeout, false}},
	{"OF", {Tag::Of, false}},

	// Attributes
	{"ATTR", {Tag::Attr, false}},
	{"ATTR-LIST", {Tag::AttrList, false}},

	// Comments
	{"COMMENT-LEADING", {Tag::CommentLeading, false}},
	{"COMMENT-TRAILING", {Tag::CommentTrailing, false}},

	// Syntactic tokens
	{"COMMA", {Tag::Comma, true}},
	{"LPAREN", {Tag::LParen, true}},
	{"RPAREN", {Tag::RParen, true}},
	{"LBRACE", {Tag::LBrace, true}},
	{"RBRACE", {Tag::RBrace, true}},
	{"LBRACKET", {Tag::LBracket, true}},
	{"RBRACKET", {Tag::RBracket, true}},
	{"COLON", {Tag::Colon, true}},
	{"KEYWORD", {Tag::Keyword, true}},
	{"OP", {Tag::Op, true}},
	{"ASSIGN", {Tag::Assign, true}},
	{"DOLLAR", {Tag::Dollar, true}},
	{"QUESTION", {Tag::Question, true}},
	{"SP", {Tag::Sp, true}},

	// Markers
	{"SEMI", {Tag::Semi, false}},
	{"BLANK", {Tag::Blank, false}},
	{"RAW", {Tag::Raw, false}},
	{"TRAILING-COMMA", {Tag::TrailingComma, false}},

	// Preprocessor
	{"PREPROC", {Tag::Preproc, false}},
	{"PREPROC-COND", {Tag::PreprocCond, false}},

	// Fallback
	{"TOKEN", {Tag::Token, false}},
	{"UNKNOWN", {Tag::Unknown, false}},
	{"UNKNOWN-EXPR", {Tag::UnknownExpr, false}},
};

Tag TagFromString(const std::string& s)
	{
	auto it = tag_map.find(s);
	return it == tag_map.end() ? Tag::Unknown : it->second.tag;
	}

const char* TagToString(Tag t)
	{
	static std::unordered_map<Tag, const char*> rm;

	if ( rm.empty() )
		for ( const auto& [str, entry] : tag_map )
			rm[entry.tag] = str.c_str();

	auto it = rm.find(t);
	return it == rm.end() ? "UNKNOWN" : it->second;
	}

bool is_token(Tag t)
	{
	static std::unordered_map<Tag, bool> tm;

	if ( tm.empty() )
		for ( const auto& [str, entry] : tag_map )
			tm[entry.tag] = entry.token;

	auto it = tm.find(t);
	return it != tm.end() && it->second;
	}
