/*-------------------------------------------------------------------------
 *
 * parser_digest.cpp
 *	  SQL digest: reduces a statement's tokens to a template.
 *
 * The scanner (core_yylex) tokenizes the statement, and DigestBuilder reduces
 * the tokens by the rules of MySQL's STATEMENT_DIGEST_TEXT() (sql/sql_digest.cc,
 * digest_add_token):
 *
 *   R1  a '+' / '-' that follows an expression-starting token and precedes a
 *       number is a unary sign and is folded into the literal
 *   R2  every literal becomes '?', and "?, ?" / "LIST, ?" collapse into LIST
 *   R3  "( ? )" becomes RowSingle, "( LIST )" becomes RowMulti; comma
 *       separated rows collapse into Row*List; "IN ROW" becomes InExpr
 *   R4  NULL is a literal unless it directly follows IS [NOT]
 *
 * Keywords are upper-cased; identifiers keep their case and are back-quoted,
 * so quoted and unquoted spellings of a name digest alike. The scanner drops
 * comments and whitespace.
 *
 * Any input gets a template. CopyAndRepairForScanner() rewrites MySQL syntax
 * the scanner does not know and closes what truncation left open; if the
 * scanner still rejects the input, the tokens read before that are digested.
 *
 *-------------------------------------------------------------------------
 */
#include "sql_digest_pg.hpp"

#include "pg_functions.hpp"
#include "parser/gramparse.hpp"

#include <climits>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

/* kwlist.hpp defines ScanKeywords as a const array with internal linkage, so
 * the table passed to scanner_init() is instantiated here, as in keywords.cpp. */
#include "parser/kwlist.hpp"

namespace duckdb_sql_digest_pgquery {

/* Exported by the flex-generated scanner but declared in no header. */
size_t core_yyget_leng(core_yyscan_t yyscanner);

namespace {

// Reduced token kinds and how each is rendered. (CamelCase because ALL_CAPS names
// such as IDENT are bison token macros from gram.hpp.)
enum class DigestTokenKind : uint8_t {
	Other,         // keyword, operator or punctuation; stored and rendered upper-cased
	Ident,         // identifier; rendered back-quoted, case preserved
	Literal,       // ?
	List,          // ?, ?, ...            ->  ...
	RowSingle,     // (?)                  ->  (?)
	RowSingleList, // (?), (?), ...        ->  (?) followed by the "/* , ... */" marker
	RowMulti,      // (?, ?, ...)          ->  (...)
	RowMultiList,  // (...), (...), ...    ->  (...) followed by the "/* , ... */" marker
	InExpr         // IN (?) or IN (...)   ->  IN (...)
};

struct DigestToken {
	DigestTokenKind kind;
	uint32_t offset; /* into DigestBuilder::pool; Other and Ident only */
	uint32_t length;
};

/*
 * Tokens after which a '+' or '-' is a unary sign: MySQL's m_start_expr set
 * (sql/gen_lex_token.cc), matched by spelling since the token numbers differ.
 * Only keywords, operators and punctuation match (see IsStartExpr()); seven of
 * these words are not keywords in this scanner (every, starts, ends, return,
 * elseif, while, xor), so "a XOR -1" keeps the sign MySQL would fold.
 */
const char *const START_EXPR_TOKENS[] = {
    "(",        ",",       "EVERY", "AT",     "STARTS", "ENDS", "DEFAULT", "RETURN", "IF",  "ELSEIF",
    "CASE",     "WHEN",    "WHILE", "UNTIL",  "SELECT", "OR",   "||",      "XOR",    "AND", "&&",
    "NOT",      "BETWEEN", "LIKE",  "REGEXP", "|",      "&",    "<<",      ">>",     "+",   "-",
    "INTERVAL", "*",       "/",     "%",      "DIV",    "MOD",  "^",
};

bool IsStartExprSpelling(const char *text, size_t length) {
	for (auto candidate : START_EXPR_TOKENS) {
		if (strlen(candidate) == length && memcmp(candidate, text, length) == 0) {
			return true;
		}
	}
	return false;
}

char AsciiToLower(char c) {
	return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

char AsciiToUpper(char c) {
	return (c >= 'a' && c <= 'z') ? static_cast<char>(c - ('a' - 'A')) : c;
}

class DigestBuilder {
public:
	DigestBuilder() {
		/* Enough for typical statements; both grow on demand and keep their
		 * capacity across Reset(). */
		tokens.reserve(128);
		pool.reserve(512);
	}

	void Reset() {
		tokens.clear();
		pool.clear();
	}

	/* keyword / operator / punctuation, stored upper-cased */
	void PushOther(const char *text, size_t length) {
		Push(DigestTokenKind::Other, text, length, true);
	}

	void PushIdent(const char *text, size_t length) {
		Push(DigestTokenKind::Ident, text, length, false);
	}

	/* R2 */
	void PushLiteral() {
		auto n = tokens.size();
		if (n >= 2 && IsOther(tokens[n - 1], ",") &&
		    (tokens[n - 2].kind == DigestTokenKind::Literal || tokens[n - 2].kind == DigestTokenKind::List)) {
			tokens.resize(n - 2);
			PushKind(DigestTokenKind::List);
			return;
		}
		PushKind(DigestTokenKind::Literal);
	}

	/* R1 then R2: numbers absorb the unary signs in front of them */
	void PushNumericLiteral() {
		while (true) {
			auto n = tokens.size();
			if (n >= 2 && (IsOther(tokens[n - 1], "-") || IsOther(tokens[n - 1], "+")) && IsStartExpr(tokens[n - 2])) {
				tokens.pop_back();
				continue;
			}
			break;
		}
		PushLiteral();
	}

	/* R3 */
	void PushCloseParen() {
		auto n = tokens.size();
		if (n >= 2 && IsOther(tokens[n - 2], "(")) {
			auto inner = tokens[n - 1].kind;
			if (inner == DigestTokenKind::Literal || inner == DigestTokenKind::List) {
				auto row = inner == DigestTokenKind::Literal ? DigestTokenKind::RowSingle : DigestTokenKind::RowMulti;
				auto row_list =
				    inner == DigestTokenKind::Literal ? DigestTokenKind::RowSingleList : DigestTokenKind::RowMultiList;
				tokens.resize(n - 2);
				n = tokens.size();
				if (n >= 2 && IsOther(tokens[n - 1], ",") &&
				    (tokens[n - 2].kind == row || tokens[n - 2].kind == row_list)) {
					tokens.resize(n - 2);
					PushKind(row_list);
				} else if (n >= 1 && IsOther(tokens[n - 1], "IN")) {
					tokens.pop_back();
					PushKind(DigestTokenKind::InExpr);
				} else {
					PushKind(row);
				}
				return;
			}
		}
		PushOther(")", 1);
	}

	/*
	 * A ';' at the end is punctuation rather than part of the statement, so
	 * "SELECT 1" and "SELECT 1;" share a digest.  One in the middle separates
	 * statements and is kept: the whole input is digested as a single template.
	 */
	void StripTrailingSemicolons() {
		while (!tokens.empty() && IsOther(tokens.back(), ";")) {
			tokens.pop_back();
		}
	}

	/* R4 approximation: NULL keeps its keyword role only in IS [NOT] NULL */
	bool NullIsKeyword() const {
		auto n = tokens.size();
		if (n >= 1 && IsOther(tokens[n - 1], "IS")) {
			return true;
		}
		return n >= 2 && IsOther(tokens[n - 1], "NOT") && IsOther(tokens[n - 2], "IS");
	}

	void Render(std::string &out) const {
		static constexpr size_t ESTIMATED_RENDER_OVERHEAD_PER_TOKEN = 16;

		out.clear();
		out.reserve(pool.size() + tokens.size() * ESTIMATED_RENDER_OVERHEAD_PER_TOKEN);
		for (size_t i = 0; i < tokens.size(); i++) {
			if (i > 0) {
				out += ' ';
			}
			auto &token = tokens[i];
			const char *text = pool.data() + token.offset;
			switch (token.kind) {
			case DigestTokenKind::Other:
				out.append(text, token.length);
				break;
			case DigestTokenKind::Ident:
				out += '`';
				if (memchr(text, '`', token.length) == nullptr) {
					out.append(text, token.length);
				} else {
					for (uint32_t j = 0; j < token.length; j++) {
						if (text[j] == '`') {
							out += '`';
						}
						out += text[j];
					}
				}
				out += '`';
				break;
			case DigestTokenKind::Literal:
				out += '?';
				break;
			case DigestTokenKind::List:
				AppendLiteral(out, "...");
				break;
			case DigestTokenKind::RowSingle:
				AppendLiteral(out, "(?)");
				break;
			case DigestTokenKind::RowSingleList:
				AppendLiteral(out, "(?) /* , ... */");
				break;
			case DigestTokenKind::RowMulti:
				AppendLiteral(out, "(...)");
				break;
			case DigestTokenKind::RowMultiList:
				AppendLiteral(out, "(...) /* , ... */");
				break;
			case DigestTokenKind::InExpr:
				AppendLiteral(out, "IN (...)");
				break;
			}
		}
	}

private:
	template <size_t N>
	static void AppendLiteral(std::string &out, const char (&text)[N]) {
		out.append(text, N - 1);
	}

	void Push(DigestTokenKind kind, const char *text, size_t length, bool fold_case) {
		DigestToken token;
		token.kind = kind;
		token.offset = static_cast<uint32_t>(pool.size());
		token.length = static_cast<uint32_t>(length);
		if (fold_case) {
			for (size_t i = 0; i < length; i++) {
				pool += AsciiToUpper(text[i]);
			}
		} else {
			pool.append(text, length);
		}
		tokens.push_back(token);
	}

	void PushKind(DigestTokenKind kind) {
		DigestToken token;
		token.kind = kind;
		token.offset = 0;
		token.length = 0;
		tokens.push_back(token);
	}

	bool IsOther(const DigestToken &token, const char *text) const {
		if (token.kind != DigestTokenKind::Other) {
			return false;
		}
		auto length = strlen(text);
		return token.length == length && memcmp(pool.data() + token.offset, text, length) == 0;
	}

	bool IsStartExpr(const DigestToken &token) const {
		return token.kind == DigestTokenKind::Other && IsStartExprSpelling(pool.data() + token.offset, token.length);
	}

	std::vector<DigestToken> tokens;
	std::string pool;
};

/* MySQL hex/bit literals: 0xFF / 0b101.  The scanner splits them into the
 * integer 0 followed by an identifier; the identifier is dropped when it is
 * adjacent to that 0 and looks like the digits of such a literal. */
bool IsHexOrBitLiteralTail(const char *text, size_t length) {
	if (length < 2) {
		return false;
	}
	char marker = AsciiToLower(text[0]);
	if (marker == 'x') {
		for (size_t i = 1; i < length; i++) {
			char c = AsciiToLower(text[i]);
			if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
				return false;
			}
		}
		return true;
	}
	if (marker == 'b') {
		for (size_t i = 1; i < length; i++) {
			if (text[i] != '0' && text[i] != '1') {
				return false;
			}
		}
		return true;
	}
	return false;
}

bool IsIdentifierChar(unsigned char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$' ||
	       c >= 0x80;
}

struct DollarQuote {
	bool present;            /* a dollar quote starts at the position examined */
	bool terminated;         /* its closing delimiter was found */
	size_t delimiter_length; /* length of "$$" or "$tag$" */
	size_t end;              /* index just past the closing delimiter */
};

/*
 * Examine the '$' at `text[i]`: does it open a dollar-quoted string ($$...$$ or
 * $tag$...$tag$), and where does that string end?  A '$' that merely continues
 * an identifier (a$b) opens nothing; the caller rules that case out.
 */
DollarQuote ScanDollarQuote(const std::string &text, size_t length, size_t i) {
	DollarQuote quote = {false, false, 0, i};
	size_t tag_end = i + 1;
	if (tag_end < length &&
	    (IsIdentifierChar(static_cast<unsigned char>(text[tag_end])) &&
	     !(text[tag_end] >= '0' && text[tag_end] <= '9')) &&
	    text[tag_end] != '$') {
		while (tag_end < length && IsIdentifierChar(static_cast<unsigned char>(text[tag_end])) &&
		       text[tag_end] != '$') {
			tag_end++;
		}
	}
	if (tag_end >= length || text[tag_end] != '$') {
		return quote;
	}
	quote.present = true;
	quote.delimiter_length = tag_end + 1 - i;
	size_t close = text.find(text.data() + i, tag_end + 1, quote.delimiter_length);
	if (close == std::string::npos) {
		return quote;
	}
	quote.terminated = true;
	quote.end = close + quote.delimiter_length;
	return quote;
}

bool SpellingIs(const char *text, size_t length, const char *word) {
	if (strlen(word) != length) {
		return false;
	}
	for (size_t i = 0; i < length; i++) {
		if (AsciiToLower(text[i]) != word[i]) {
			return false;
		}
	}
	return true;
}

/* Bytes CopyAndRepairForScanner() stops at; all others are skipped with one
 * table lookup each. */
struct CopyStopBytes {
	bool at[256] = {};
	CopyStopBytes() {
		for (unsigned char c : {'\'', '"', '`', '#', '$', '-', '/'}) {
			at[c] = true;
		}
	}
};
const CopyStopBytes COPY_STOPS;
/*
 * Copies the statement into `out` (NUL terminated) in a form the scanner can
 * lex to the end. Rewrites keep the length of what they replace, so token
 * locations still refer to the original text:
 *
 *   'O\'Brien'  ->  'O''Brien'   backslash-escaped quote -> doubled quote
 *   "O\"Brien"  ->  "O""Brien"
 *   # comment   ->  blanks       MySQL line comment
 *   NUL         ->  blank
 *
 * Quotes inside identifiers and comments are skipped, so they are not taken for
 * string delimiters. A statement cut off inside a string, identifier, dollar
 * quote or block comment gets the missing terminator appended, so the truncated
 * token still lexes; a truncated literal still becomes '?'. Nothing marks a
 * repaired statement, so it can share a template with the complete one.
 */
void CopyAndRepairForScanner(const char *query, size_t length, std::string &out) {
	out.assign(query, length);
	/* scanner_init() measures its input with strlen(), so an embedded NUL would
	 * silently cut the statement short. */
	for (size_t nul = out.find('\0'); nul != std::string::npos; nul = out.find('\0', nul + 1)) {
		out[nul] = ' ';
	}
	size_t i = 0;
	while (i < length) {
		while (i < length && !COPY_STOPS.at[static_cast<unsigned char>(out[i])]) {
			i++;
		}
		if (i >= length) {
			break;
		}
		char c = out[i];
		if (c == '\'' || c == '"') {
			char quote = c;
			bool closed = false;
			i++;
			while (i < length) {
				if (out[i] == '\\' && i + 1 < length) {
					if (out[i + 1] == quote) {
						out[i] = quote;
					}
					i += 2;
					continue;
				}
				if (out[i] == quote) {
					if (i + 1 < length && out[i + 1] == quote) {
						i += 2;
						continue;
					}
					i++;
					closed = true;
					break;
				}
				i++;
			}
			if (!closed) {
				/* without this the scanner would either reject the statement or,
				 * for double quotes, drop the tail silently: xdq has no <<EOF>> rule */
				out += quote;
				return;
			}
		} else if (c == '`') {
			bool closed = false;
			i++;
			while (i < length) {
				if (out[i] == '`') {
					if (i + 1 < length && out[i + 1] == '`') {
						i += 2;
						continue;
					}
					i++;
					closed = true;
					break;
				}
				i++;
			}
			if (!closed) {
				out += '`';
				return;
			}
		} else if (c == '#') {
			while (i < length && out[i] != '\n') {
				out[i++] = ' ';
			}
		} else if (c == '$' && (i == 0 || !IsIdentifierChar(static_cast<unsigned char>(out[i - 1])))) {
			/* dollar-quoted string; a '$' inside an identifier (a$b) is not one */
			auto quote = ScanDollarQuote(out, length, i);
			if (!quote.present) {
				i++;
			} else if (quote.terminated) {
				i = quote.end;
			} else {
				/* substr() copies before the append, so `out` is not read while it grows */
				out += out.substr(i, quote.delimiter_length);
				return;
			}
		} else if (c == '-' && i + 1 < length && out[i + 1] == '-') {
			while (i < length && out[i] != '\n') {
				i++;
			}
		} else if (c == '/' && i + 1 < length && out[i + 1] == '*') {
			i += 2;
			bool closed = false;
			while (i + 1 < length) {
				if (out[i] == '*' && out[i + 1] == '/') {
					i += 2;
					closed = true;
					break;
				}
				i++;
			}
			if (!closed) {
				out += "*/";
				return;
			}
		} else {
			i++;
		}
	}
}

/* Per-thread nesting depth of DigestMemoryScope; the arena in pg_functions.cpp
 * is per thread as well. */
struct ArenaScope {
	int depth = 0; /* open DigestMemoryScopes */
};

ArenaScope &CurrentArenaScope() {
	static __thread ArenaScope scope;
	return scope;
}

/*
 * Prepares the arena for one statement: allocates it (and frees it afterwards)
 * when no DigestMemoryScope is open, otherwise rewinds it to its first block.
 * palloc() never reuses memory within an arena, hence the rewind.
 */
struct ArenaForStatement {
	bool owned;
	ArenaForStatement() : owned(CurrentArenaScope().depth == 0) {
		if (owned) {
			pg_parser_init();
		} else {
			pg_parser_reset();
		}
	}
	~ArenaForStatement() {
		if (owned) {
			pg_parser_cleanup();
		}
	}
};

struct ScannerGuard {
	core_yyscan_t scanner;
	explicit ScannerGuard(core_yyscan_t scanner_p) : scanner(scanner_p) {
	}
	~ScannerGuard() {
		scanner_finish(scanner);
	}
};

/*
 * Runs the scanner over `input` and reduces its tokens into `builder`.
 *
 * Throws what the scanner throws on input it rejects. `builder` then still
 * holds the tokens read before that: it copies every token's text, and the
 * guards release the scanner and the arena while the exception unwinds.
 */
void LexIntoDigest(const std::string &input, DigestBuilder &builder) {
	ArenaForStatement arena;

	core_yy_extra_type extra = {};
	ScannerGuard scanner(scanner_init(input.c_str(), &extra, ScanKeywords, NumScanKeywords));

	// Track the scanner's 0 + identifier split of MySQL 0x... / 0b... literals.
	bool previous_is_zero = false;
	int previous_location = -1;

	while (true) {
		core_YYSTYPE value;
		YYLTYPE location = 0;
		int token = core_yylex(&value, &location, scanner.scanner);
		if (token == 0) {
			break;
		}

		const char *text = extra.scanbuf + location;
		size_t text_length = core_yyget_leng(scanner.scanner);
		bool is_zero = false;

		switch (token) {
		case ICONST:
			is_zero = text_length == 1 && text[0] == '0';
			builder.PushNumericLiteral();
			break;
		case FCONST:
		case BCONST:
		case XCONST:
			builder.PushNumericLiteral();
			break;
		case SCONST:
		case PARAM:
		case '?':
			builder.PushLiteral();
			break;
		case IDENT: {
			/* value.str is only valid until the next core_yylex() call */
			size_t ident_length = strlen(value.str);
			if (previous_is_zero && previous_location + 1 == location &&
			    IsHexOrBitLiteralTail(value.str, ident_length)) {
				break; /* the '0' already stands for the whole literal */
			}
			builder.PushIdent(value.str, ident_length);
			break;
		}
		case ')':
			builder.PushCloseParen();
			break;
		default:
			if (SpellingIs(text, text_length, "null")) {
				if (builder.NullIsKeyword()) {
					builder.PushOther(text, text_length);
				} else {
					builder.PushLiteral();
				}
			} else if (SpellingIs(text, text_length, "true") || SpellingIs(text, text_length, "false")) {
				builder.PushLiteral();
			} else {
				builder.PushOther(text, text_length);
			}
			break;
		}

		previous_is_zero = is_zero;
		previous_location = location;
	}
}

} // namespace

/* Buffers reused across Normalize() calls. */
struct DigestNormalizer::State {
	std::string input;
	DigestBuilder builder;

	State() {
		input.reserve(512);
	}
};

DigestNormalizer::DigestNormalizer() : state(new State()) {
}

DigestNormalizer::~DigestNormalizer() = default;

DigestMemoryScope::DigestMemoryScope() {
	/* counted only once pg_parser_init() has succeeded: if it throws, the
	 * destructor does not run, so depth must still read 0 */
	auto &scope = CurrentArenaScope();
	if (scope.depth == 0) {
		pg_parser_init();
	}
	scope.depth++;
}

DigestMemoryScope::~DigestMemoryScope() {
	if (--CurrentArenaScope().depth == 0) {
		pg_parser_cleanup();
	}
}

void DigestNormalizer::Normalize(const char *query, size_t length, std::string &result) {
	/* Token locations are ints and the scanner copies the whole statement, so
	 * anything past INT_MAX is dropped rather than rejected. */
	if (length > static_cast<size_t>(INT_MAX)) {
		length = INT_MAX;
	}

	/* a NUL terminated copy the scanner can lex to the end */
	std::string &input = state->input;
	CopyAndRepairForScanner(query, length, input);

	DigestBuilder &builder = state->builder;
	builder.Reset();
	try {
		LexIntoDigest(input, builder);
	} catch (std::runtime_error &) {
		/*
		 * The scanner rejected the statement (an invalid Unicode escape, an
		 * operator longer than 64 bytes, an empty `` identifier) and cannot
		 * resume: the template is built from the tokens read before that.
		 */
	}

	builder.StripTrailingSemicolons();
	/* input without tokens (empty, blank, only comments) renders as "" */
	builder.Render(result);
}

} // namespace duckdb_sql_digest_pgquery
