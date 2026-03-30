#include "tag.h"

#include <unordered_map>

static const std::unordered_map<std::string, Tag> tag_map = {
	// Expressions
	{"BINARY-OP", Tag::BinaryOp},
	{"UNARY-OP", Tag::UnaryOp},
	{"TERNARY", Tag::Ternary},
	{"PAREN", Tag::Paren},
	{"CALL", Tag::Call},
	{"INDEX", Tag::Index},
	{"INDEX-LITERAL", Tag::IndexLiteral},
	{"SLICE", Tag::Slice},
	{"FIELD-ACCESS", Tag::FieldAccess},
	{"FIELD-ASSIGN", Tag::FieldAssign},
	{"BRACE-INIT", Tag::BraceInit},
	{"CONSTRUCTOR", Tag::Constructor},
	{"SCHEDULE", Tag::Schedule},
	{"KEYWORD-EXPR", Tag::KeywordExpr},
	{"LAMBDA", Tag::Lambda},
	{"WHEN-LOCAL", Tag::WhenLocal},

	// Expression atoms
	{"IDENTIFIER", Tag::Identifier},
	{"CONSTANT", Tag::Constant},
	{"INTERVAL", Tag::Interval},

	// Types
	{"TYPE-ATOM", Tag::TypeAtom},
	{"TYPE-PARAMETERIZED", Tag::TypeParameterized},
	{"TYPE-FUNC", Tag::TypeFunc},
	{"TYPE-RECORD", Tag::TypeRecord},
	{"TYPE-ENUM", Tag::TypeEnum},

	// Declarations
	{"GLOBAL-DECL", Tag::GlobalDecl},
	{"LOCAL-DECL", Tag::LocalDecl},
	{"TYPE-DECL", Tag::TypeDecl},
	{"FUNC-DECL", Tag::FuncDecl},
	{"EXPORT", Tag::ExportDecl},
	{"MODULE", Tag::ModuleDecl},
	{"REDEF-RECORD", Tag::RedefRecord},
	{"REDEF-ENUM", Tag::RedefEnum},

	// Statements
	{"EXPR-STMT", Tag::ExprStmt},
	{"IF", Tag::If},
	{"FOR", Tag::For},
	{"WHILE", Tag::While},
	{"SWITCH", Tag::Switch},
	{"WHEN", Tag::When},
	{"RETURN", Tag::Return},
	{"PRINT", Tag::Print},
	{"EVENT-STMT", Tag::EventStmt},
	{"ADD", Tag::Add},
	{"DELETE", Tag::Delete},
	{"BLOCK", Tag::Block},
	{"NEXT", Tag::Next},
	{"BREAK", Tag::Break},
	{"FALLTHROUGH", Tag::Fallthrough},

	// Statement/declaration parts
	{"ARGS", Tag::Args},
	{"SUBSCRIPTS", Tag::Subscripts},
	{"PARAMS", Tag::Params},
	{"PARAM", Tag::Param},
	{"RETURNS", Tag::Returns},
	{"BODY", Tag::Body},
	{"COND", Tag::Cond},
	{"ELSE", Tag::Else},
	{"ITERABLE", Tag::Iterable},
	{"VARS", Tag::Vars},
	{"CAPTURES", Tag::Captures},
	{"INIT", Tag::Init},
	{"TYPE", Tag::Type},
	{"EXPR", Tag::Expr},
	{"FIELD", Tag::Field},
	{"ENUM-VALUE", Tag::EnumValue},
	{"CASE", Tag::Case},
	{"DEFAULT", Tag::Default},
	{"VALUES", Tag::Values},
	{"TIMEOUT", Tag::Timeout},
	{"OF", Tag::Of},

	// Attributes
	{"ATTR", Tag::Attr},
	{"ATTR-LIST", Tag::AttrList},

	// Comments
	{"COMMENT-LEADING", Tag::CommentLeading},
	{"COMMENT-TRAILING", Tag::CommentTrailing},
	{"COMMENT-PREV", Tag::CommentPrev},

	// Markers
	{"SEMI", Tag::Semi},
	{"BLANK", Tag::Blank},
	{"RAW", Tag::Raw},

	// Preprocessor
	{"PREPROC", Tag::Preproc},

	// Fallback
	{"TOKEN", Tag::Token},
	{"UNKNOWN", Tag::Unknown},
	{"UNKNOWN-EXPR", Tag::UnknownExpr},
};

Tag TagFromString(const std::string& s)
	{
	auto it = tag_map.find(s);
	return it == tag_map.end() ? Tag::Unknown : it->second;
	}

const char* TagToString(Tag t)
	{
	static std::unordered_map<Tag, const char*> rm;

	if ( rm.empty() )
		for ( const auto& [str, tag] : tag_map )
			rm[tag] = str.c_str();

	auto it = rm.find(t);
	return it == rm.end() ? "UNKNOWN" : it->second;
	}
