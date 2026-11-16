#include "core/material_script.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace vibestudio {
namespace {

struct Text {
	Q_DECLARE_TR_FUNCTIONS(VibeStudioMaterials)
};

// ---------------------------------------------------------------------------
// Tokens
// ---------------------------------------------------------------------------

enum class TokenType {
	Word,
	Number,
	String,
	Punctuation,
};

struct Token {
	QString text;
	int start = 0;
	int end = 0;
	int line = 1;
	int column = 1;
	// A line break separates this token from the one before it.
	bool newlineBefore = true;
	TokenType type = TokenType::Word;

	[[nodiscard]] bool is(const char* value) const
	{
		return type != TokenType::String && text.compare(QLatin1String(value), Qt::CaseInsensitive) == 0;
	}
	[[nodiscard]] bool isBrace(QChar brace) const
	{
		return type != TokenType::String && text.size() == 1 && text.at(0) == brace;
	}
};

struct LineIndex {
	QVector<int> starts;

	explicit LineIndex(const QString& text)
	{
		starts.push_back(0);
		for (int index = 0; index < text.size(); ++index) {
			if (text.at(index) == QLatin1Char('\n')) {
				starts.push_back(index + 1);
			}
		}
	}
	int lineOf(int offset) const
	{
		const auto found = std::upper_bound(starts.cbegin(), starts.cend(), offset);
		return static_cast<int>(found - starts.cbegin());
	}
	int columnOf(int offset) const
	{
		const int line = lineOf(offset);
		return offset - starts.at(line - 1) + 1;
	}
};

// Line and column of each token start. Tokens come in text order, so one
// forward walk replaces a binary search per token.
struct LineTracker {
	const QString& text;
	int position = 0;
	int line = 1;
	int lineStart = 0;

	void place(Token* token)
	{
		const QChar* data = text.constData();
		while (position < token->start) {
			if (data[position] == QLatin1Char('\n')) {
				++line;
				lineStart = position + 1;
			}
			++position;
		}
		token->line = line;
		token->column = token->start - lineStart + 1;
	}
};

// Quake III's COM_ParseExt: whitespace-separated words, quoted strings that
// may span lines, `//` comments and `/* */` comments. A block comment does
// not count as a line break, as in the engine.
QVector<Token> lexQuake3(const QString& text)
{
	QVector<Token> tokens;
	tokens.reserve(text.size() / 6);
	LineTracker lines {text};
	const int size = text.size();
	int position = 0;
	bool sawNewline = true;
	while (position < size) {
		const QChar c = text.at(position);
		if (c == QLatin1Char('\n')) {
			sawNewline = true;
			++position;
			continue;
		}
		if (c.unicode() <= 32) {
			++position;
			continue;
		}
		if (c == QLatin1Char('/') && position + 1 < size && text.at(position + 1) == QLatin1Char('/')) {
			while (position < size && text.at(position) != QLatin1Char('\n')) {
				++position;
			}
			continue;
		}
		if (c == QLatin1Char('/') && position + 1 < size && text.at(position + 1) == QLatin1Char('*')) {
			position += 2;
			while (position + 1 < size && !(text.at(position) == QLatin1Char('*') && text.at(position + 1) == QLatin1Char('/'))) {
				++position;
			}
			position = std::min(size, position + 2);
			continue;
		}
		Token token;
		token.start = position;
		token.newlineBefore = sawNewline;
		sawNewline = false;
		if (c == QLatin1Char('"')) {
			++position;
			const int contentStart = position;
			while (position < size && text.at(position) != QLatin1Char('"')) {
				++position;
			}
			token.text = text.mid(contentStart, position - contentStart);
			token.type = TokenType::String;
			if (position < size) {
				++position;
			}
		} else {
			while (position < size && text.at(position).unicode() > 32) {
				++position;
			}
			token.text = text.mid(token.start, position - token.start);
			token.type = TokenType::Word;
		}
		token.end = position;
		lines.place(&token);
		tokens.push_back(std::move(token));
	}
	return tokens;
}

bool isNameStart(QChar c)
{
	// With LEXFL_ALLOWPATHNAMES a name may also start with a path character,
	// so `time/2` is one name (a bad term) and `/2` after a number is one too.
	return c.isLetter() || c == QLatin1Char('_') || c == QLatin1Char('/') || c == QLatin1Char('\\') || c == QLatin1Char(':')
		|| c == QLatin1Char('.');
}

bool isNameChar(QChar c)
{
	// idLexer::ReadName with LEXFL_ALLOWPATHNAMES.
	return c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('/') || c == QLatin1Char('\\')
		|| c == QLatin1Char(':') || c == QLatin1Char('.');
}

// Doom 3's idLexer with the decl flags: names may hold path characters,
// punctuation is its own token, and line breaks only matter to the few
// keywords that read "on line".
QVector<Token> lexDoom3(const QString& text)
{
	QVector<Token> tokens;
	tokens.reserve(text.size() / 5);
	LineTracker lines {text};
	const int size = text.size();
	int position = 0;
	bool sawNewline = true;
	// The two-character operators: && || >= <= == !=.
	const auto pairAt = [&](int at) {
		if (at + 1 >= size) {
			return false;
		}
		const char16_t first = text.at(at).unicode();
		const char16_t second = text.at(at + 1).unicode();
		return (first == u'&' && second == u'&') || (first == u'|' && second == u'|')
			|| (second == u'=' && (first == u'>' || first == u'<' || first == u'=' || first == u'!'));
	};
	while (position < size) {
		const QChar c = text.at(position);
		if (c == QLatin1Char('\n')) {
			sawNewline = true;
			++position;
			continue;
		}
		if (c.isSpace() || c.unicode() < 32) {
			++position;
			continue;
		}
		if (c == QLatin1Char('/') && position + 1 < size && text.at(position + 1) == QLatin1Char('/')) {
			while (position < size && text.at(position) != QLatin1Char('\n')) {
				++position;
			}
			continue;
		}
		if (c == QLatin1Char('/') && position + 1 < size && text.at(position + 1) == QLatin1Char('*')) {
			position += 2;
			while (position + 1 < size && !(text.at(position) == QLatin1Char('*') && text.at(position + 1) == QLatin1Char('/'))) {
				if (text.at(position) == QLatin1Char('\n')) {
					sawNewline = true;
				}
				++position;
			}
			position = std::min(size, position + 2);
			continue;
		}
		Token token;
		token.start = position;
		token.newlineBefore = sawNewline;
		sawNewline = false;
		if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
			const QChar quote = c;
			++position;
			const int contentStart = position;
			while (position < size && text.at(position) != quote && text.at(position) != QLatin1Char('\n')) {
				++position;
			}
			token.text = text.mid(contentStart, position - contentStart);
			token.type = TokenType::String;
			if (position < size && text.at(position) == quote) {
				++position;
			}
		} else if (c.isDigit() || (c == QLatin1Char('.') && position + 1 < size && text.at(position + 1).isDigit())) {
			if (c == QLatin1Char('0') && position + 1 < size && (text.at(position + 1) == QLatin1Char('x') || text.at(position + 1) == QLatin1Char('X'))) {
				position += 2;
				while (position < size && (text.at(position).isDigit() || QStringLiteral("abcdefABCDEF").contains(text.at(position)))) {
					++position;
				}
			} else {
				while (position < size && text.at(position).isDigit()) {
					++position;
				}
				if (position < size && text.at(position) == QLatin1Char('.')) {
					++position;
					while (position < size && text.at(position).isDigit()) {
						++position;
					}
				}
				if (position + 1 < size && (text.at(position) == QLatin1Char('e') || text.at(position) == QLatin1Char('E'))
					&& (text.at(position + 1).isDigit()
						|| ((text.at(position + 1) == QLatin1Char('-') || text.at(position + 1) == QLatin1Char('+')) && position + 2 < size
							&& text.at(position + 2).isDigit()))) {
					position += 2;
					while (position < size && text.at(position).isDigit()) {
						++position;
					}
				}
			}
			token.text = text.mid(token.start, position - token.start);
			token.type = TokenType::Number;
		} else if (isNameStart(c)) {
			while (position < size && isNameChar(text.at(position))) {
				++position;
			}
			token.text = text.mid(token.start, position - token.start);
			token.type = TokenType::Word;
		} else {
			token.type = TokenType::Punctuation;
			if (pairAt(position)) {
				token.text = text.mid(position, 2);
				position += 2;
			} else {
				token.text = QString(c);
				++position;
			}
		}
		token.end = position;
		lines.place(&token);
		tokens.push_back(std::move(token));
	}
	return tokens;
}

// For every brace token the index of its partner, -1 when it has none.
// Quoted strings never count, as in SkipBracedSection.
QVector<int> matchBraces(const QVector<Token>& tokens)
{
	QVector<int> match(tokens.size(), -1);
	QVector<int> open;
	for (int index = 0; index < tokens.size(); ++index) {
		const Token& token = tokens.at(index);
		if (token.isBrace(QLatin1Char('{'))) {
			open.push_back(index);
		} else if (token.isBrace(QLatin1Char('}')) && !open.isEmpty()) {
			const int partner = open.takeLast();
			match[partner] = index;
			match[index] = partner;
		}
	}
	return match;
}

double toNumber(const QString& text, bool* ok = nullptr)
{
	bool converted = false;
	double value = text.trimmed().toDouble(&converted);
	if (!converted) {
		// atof semantics: the longest numeric prefix, else zero.
		QString prefix;
		for (const QChar c : text.trimmed()) {
			if (c.isDigit() || c == QLatin1Char('.') || ((c == QLatin1Char('-') || c == QLatin1Char('+')) && prefix.isEmpty())) {
				prefix.append(c);
			} else {
				break;
			}
		}
		value = prefix.toDouble(&converted);
		if (!converted) {
			value = 0.0;
		}
		converted = false;
	}
	if (ok) {
		*ok = converted;
	}
	return std::isfinite(value) ? value : 0.0;
}

QString numberText(double value)
{
	if (std::abs(value - std::round(value)) < 1e-9 && std::abs(value) < 1e9) {
		return QString::number(static_cast<qint64>(std::llround(value)));
	}
	QString text = QString::number(value, 'f', 6);
	while (text.endsWith(QLatin1Char('0'))) {
		text.chop(1);
	}
	if (text.endsWith(QLatin1Char('.'))) {
		text.chop(1);
	}
	return text;
}

// ---------------------------------------------------------------------------
// Shared parser state
// ---------------------------------------------------------------------------

class ParserBase {
public:
	ParserBase(const QString& text, MaterialScript* script)
		: m_text(text), m_lines(text), m_script(script)
	{
	}

protected:
	// Both engines find where each definition ends by brace matching before
	// they parse it (idDeclFile::LoadAndParse, ScanAndLoadShaderFiles), so a
	// mistake inside one definition never reaches the next. The parser reads
	// a definition or a stage with the limit set to its matched closing
	// brace: tokens from the limit on are out of reach.
	[[nodiscard]] bool atEnd() const { return m_position >= m_limit; }
	const Token* peek(int ahead = 0) const
	{
		const int index = m_position + ahead;
		return index >= 0 && index < m_limit ? &m_tokens.at(index) : nullptr;
	}
	void lex(QVector<Token> tokens)
	{
		m_tokens = std::move(tokens);
		m_match = matchBraces(m_tokens);
		m_limit = static_cast<int>(m_tokens.size());
		m_position = 0;
	}
	// The matched closing brace of the opening brace at `open`, -1 if none.
	[[nodiscard]] int closingBrace(int open) const { return open >= 0 && open < m_match.size() ? m_match.at(open) : -1; }
	class Limit {
	public:
		Limit(ParserBase* parser, int limit)
			: m_parser(parser), m_saved(parser->m_limit)
		{
			m_parser->m_limit = std::min(limit, m_saved);
		}
		~Limit() { m_parser->m_limit = m_saved; }
		Limit(const Limit&) = delete;
		Limit& operator=(const Limit&) = delete;

	private:
		ParserBase* m_parser;
		int m_saved;
	};
	const Token* take()
	{
		return atEnd() ? nullptr : &m_tokens.at(m_position++);
	}
	// The next token when it is on the current line.
	const Token* takeOnLine()
	{
		const Token* next = peek();
		if (!next || next->newlineBefore) {
			return nullptr;
		}
		++m_position;
		return next;
	}
	[[nodiscard]] bool nextOnLine() const
	{
		const Token* next = peek();
		return next && !next->newlineBefore;
	}
	MaterialSourceSpan spanOf(int start, int end) const
	{
		MaterialSourceSpan span;
		span.start = start;
		span.end = end;
		span.line = m_lines.lineOf(start);
		span.endLine = m_lines.lineOf(std::max(start, end - 1));
		return span;
	}
	void diagnose(MaterialDefinition* definition, MaterialDiagnosticSeverity severity, const QString& code, const QString& message,
		const Token* at, int stage = -1)
	{
		MaterialDiagnostic diagnostic;
		diagnostic.severity = severity;
		diagnostic.code = code;
		diagnostic.message = message;
		diagnostic.stage = stage;
		if (at) {
			diagnostic.line = at->line;
			diagnostic.column = at->column;
			diagnostic.length = at->end - at->start;
		}
		if (definition) {
			diagnostic.material = definition->name;
			definition->diagnostics.push_back(diagnostic);
		} else {
			m_script->diagnostics.push_back(diagnostic);
		}
	}
	void reject(MaterialDefinition* definition, const QString& reason, const Token* at, int stage = -1)
	{
		if (definition->engineRejection.isEmpty()) {
			definition->engineRejection = reason;
		}
		diagnose(definition, MaterialDiagnosticSeverity::Error, QStringLiteral("engine-rejects"), reason, at, stage);
	}
	// Builds a directive from the keyword token through the last consumed
	// token.
	MaterialDirective directiveFrom(int keywordIndex, int endIndex) const
	{
		MaterialDirective directive;
		const Token& keyword = m_tokens.at(keywordIndex);
		directive.keyword = keyword.text;
		int end = keyword.end;
		if (endIndex > keywordIndex + 1) {
			directive.arguments.reserve(endIndex - keywordIndex - 1);
		}
		for (int index = keywordIndex + 1; index < endIndex; ++index) {
			directive.arguments << m_tokens.at(index).text;
			end = m_tokens.at(index).end;
		}
		if (endIndex > keywordIndex + 1) {
			const int argumentStart = m_tokens.at(keywordIndex + 1).start;
			directive.argumentText = m_text.mid(argumentStart, end - argumentStart);
		}
		directive.span = spanOf(keyword.start, end);
		return directive;
	}

	const QString& m_text;
	LineIndex m_lines;
	MaterialScript* m_script = nullptr;
	QVector<Token> m_tokens;
	QVector<int> m_match;
	int m_position = 0;
	int m_limit = 0;
};

// ---------------------------------------------------------------------------
// Quake III
// ---------------------------------------------------------------------------

// tr_shader.c ParseSort names and their numbers.
const QHash<QString, double>& quake3SortValues()
{
	static const QHash<QString, double> values {
		{QStringLiteral("portal"), 1},
		{QStringLiteral("sky"), 2},
		{QStringLiteral("opaque"), 3},
		{QStringLiteral("decal"), 4},
		{QStringLiteral("seethrough"), 5},
		{QStringLiteral("banner"), 6},
		{QStringLiteral("underwater"), 8},
		{QStringLiteral("additive"), 10},
		{QStringLiteral("nearest"), 16},
	};
	return values;
}

// The infoParms[] surface parameters that q3map and the engine read.
const QStringList& quake3SurfaceParms()
{
	static const QStringList parms {
		QStringLiteral("water"), QStringLiteral("slime"), QStringLiteral("lava"), QStringLiteral("playerclip"),
		QStringLiteral("monsterclip"), QStringLiteral("nodrop"), QStringLiteral("nonsolid"), QStringLiteral("origin"),
		QStringLiteral("trans"), QStringLiteral("detail"), QStringLiteral("structural"), QStringLiteral("areaportal"),
		QStringLiteral("clusterportal"), QStringLiteral("donotenter"), QStringLiteral("fog"), QStringLiteral("sky"),
		QStringLiteral("lightfilter"), QStringLiteral("alphashadow"), QStringLiteral("hint"), QStringLiteral("slick"),
		QStringLiteral("noimpact"), QStringLiteral("nomarks"), QStringLiteral("ladder"), QStringLiteral("nodamage"),
		QStringLiteral("metalsteps"), QStringLiteral("flesh"), QStringLiteral("nosteps"), QStringLiteral("nodraw"),
		QStringLiteral("pointlight"), QStringLiteral("nolightmap"), QStringLiteral("nodlight"), QStringLiteral("dust"),
		QStringLiteral("botclip"), QStringLiteral("antiportal"), QStringLiteral("skip"),
	};
	return parms;
}

// The keywords each Quake III derivative adds to ParseShader (general) and
// ParseStage, from their GPL releases' tr_shader.c: iortcw (MP), id's Enemy
// Territory and OpenJK (rd-vanilla). Quake III Arena drops a shader using
// any of them. Jedi Knight also takes every stage keyword starting "ss" (the
// surface-sprite options).
const QStringList& wolfensteinGlobalKeywords()
{
	static const QStringList keywords {
		QStringLiteral("nomipmap"), QStringLiteral("fogvars"), QStringLiteral("skyfogvars"), QStringLiteral("waterfogvars"),
		QStringLiteral("sunshader"), QStringLiteral("lightgridmulamb"), QStringLiteral("lightgridmuldir"), QStringLiteral("nofog"),
		QStringLiteral("allowcompress"), QStringLiteral("nocompress"),
	};
	return keywords;
}

const QStringList& wolfensteinStageKeywords()
{
	static const QStringList keywords {
		QStringLiteral("map16"), QStringLiteral("map32"), QStringLiteral("mapcomp"), QStringLiteral("mapnocomp"),
		QStringLiteral("animmapcomp"), QStringLiteral("animmapnocomp"), QStringLiteral("fog"),
	};
	return keywords;
}

const QStringList& enemyTerritoryGlobalKeywords()
{
	static const QStringList keywords = wolfensteinGlobalKeywords()
		+ QStringList {QStringLiteral("distancecull"), QStringLiteral("implicitmap"), QStringLiteral("implicitmask"), QStringLiteral("implicitblend")};
	return keywords;
}

const QStringList& enemyTerritoryStageKeywords()
{
	static const QStringList keywords = wolfensteinStageKeywords() + QStringList {QStringLiteral("lightmap")};
	return keywords;
}

const QStringList& jediKnightGlobalKeywords()
{
	static const QStringList keywords {
		QStringLiteral("material"), QStringLiteral("sun"), QStringLiteral("surfacelight"), QStringLiteral("lightcolor"),
		QStringLiteral("deform"), QStringLiteral("noglfog"), QStringLiteral("notc"), QStringLiteral("hitlocation"),
		QStringLiteral("hitmaterial"),
	};
	return keywords;
}

const QStringList& jediKnightStageKeywords()
{
	static const QStringList keywords {
		QStringLiteral("clampanimmap"), QStringLiteral("oneshotanimmap"), QStringLiteral("glow"), QStringLiteral("surfacesprites"),
		QStringLiteral("ssfademax"), QStringLiteral("ssfadescale"), QStringLiteral("ssvariance"), QStringLiteral("sshangdown"),
		QStringLiteral("ssanyangle"), QStringLiteral("ssfaceup"), QStringLiteral("sswind"), QStringLiteral("sswindidle"),
		QStringLiteral("ssvertskew"), QStringLiteral("ssfxduration"), QStringLiteral("ssfxgrow"), QStringLiteral("ssfxalpharange"),
		QStringLiteral("ssfxweather"),
	};
	return keywords;
}

bool dialectHasGlobalKeyword(MaterialDialect dialect, const QString& key)
{
	switch (dialect) {
	case MaterialDialect::Wolfenstein:
		return wolfensteinGlobalKeywords().contains(key);
	case MaterialDialect::EnemyTerritory:
		// ParseShader takes any keyword starting "implicit" as implicitMap.
		return enemyTerritoryGlobalKeywords().contains(key) || key.startsWith(QStringLiteral("implicit"));
	case MaterialDialect::JediKnight:
		return jediKnightGlobalKeywords().contains(key);
	default:
		return false;
	}
}

bool dialectHasStageKeyword(MaterialDialect dialect, const QString& key)
{
	switch (dialect) {
	case MaterialDialect::Wolfenstein:
		return wolfensteinStageKeywords().contains(key);
	case MaterialDialect::EnemyTerritory:
		return enemyTerritoryStageKeywords().contains(key);
	case MaterialDialect::JediKnight:
		return jediKnightStageKeywords().contains(key) || key.startsWith(QStringLiteral("ss"));
	default:
		return false;
	}
}

// The derivative a keyword Quake III does not know comes from, for messages;
// Automatic when none has it.
MaterialDialect dialectOwningKeyword(const QString& key, bool stage)
{
	for (MaterialDialect dialect : {MaterialDialect::Wolfenstein, MaterialDialect::EnemyTerritory, MaterialDialect::JediKnight}) {
		if (stage ? dialectHasStageKeyword(dialect, key) : dialectHasGlobalKeyword(dialect, key)) {
			return dialect;
		}
	}
	return MaterialDialect::Automatic;
}

// What a game calls itself in messages: "Quake III" for both Arena dialects,
// as the engine's own warnings do.
QString dialectShortName(MaterialDialect dialect)
{
	switch (dialect) {
	case MaterialDialect::Wolfenstein:
		return Text::tr("Return to Castle Wolfenstein");
	case MaterialDialect::EnemyTerritory:
		return Text::tr("Enemy Territory");
	case MaterialDialect::JediKnight:
		return Text::tr("Jedi Knight");
	default:
		return Text::tr("Quake III");
	}
}

const QStringList& quake3StageKeywordList()
{
	static const QStringList keywords {
		QStringLiteral("map"), QStringLiteral("clampmap"), QStringLiteral("animmap"), QStringLiteral("videomap"),
		QStringLiteral("alphafunc"), QStringLiteral("depthfunc"), QStringLiteral("detail"), QStringLiteral("blendfunc"),
		QStringLiteral("rgbgen"), QStringLiteral("alphagen"), QStringLiteral("tcgen"), QStringLiteral("texgen"),
		QStringLiteral("tcmod"), QStringLiteral("depthwrite"),
	};
	return keywords;
}

const QStringList& quake3GlobalKeywordList()
{
	static const QStringList keywords {
		QStringLiteral("qer_editorimage"), QStringLiteral("qer_trans"), QStringLiteral("qer_nocarve"), QStringLiteral("qer_alphafunc"),
		QStringLiteral("q3map_sun"), QStringLiteral("q3map_surfacelight"), QStringLiteral("q3map_lightimage"),
		QStringLiteral("q3map_lightsubdivide"), QStringLiteral("q3map_globaltexture"), QStringLiteral("q3map_backsplash"),
		QStringLiteral("q3map_nolightmap"), QStringLiteral("q3map_novertexshadows"), QStringLiteral("q3map_flare"),
		QStringLiteral("deformvertexes"), QStringLiteral("tesssize"), QStringLiteral("clamptime"), QStringLiteral("surfaceparm"),
		QStringLiteral("nomipmaps"), QStringLiteral("nopicmip"), QStringLiteral("polygonoffset"), QStringLiteral("entitymergable"),
		QStringLiteral("fogparms"), QStringLiteral("portal"), QStringLiteral("skyparms"), QStringLiteral("light"),
		QStringLiteral("cull"), QStringLiteral("sort"),
	};
	return keywords;
}

// NameToSrcBlendMode and NameToDstBlendMode, the same in both engines: each
// side takes its own set of GL factors.
bool blendSourceFactor(const QString& name, MaterialBlendFactor* factor)
{
	static const QSet<QString> names {QStringLiteral("GL_ONE"), QStringLiteral("GL_ZERO"), QStringLiteral("GL_DST_COLOR"),
		QStringLiteral("GL_ONE_MINUS_DST_COLOR"), QStringLiteral("GL_SRC_ALPHA"), QStringLiteral("GL_ONE_MINUS_SRC_ALPHA"),
		QStringLiteral("GL_DST_ALPHA"), QStringLiteral("GL_ONE_MINUS_DST_ALPHA"), QStringLiteral("GL_SRC_ALPHA_SATURATE")};
	return names.contains(name.toUpper()) && materialBlendFactorFromId(name, factor);
}

bool blendDestinationFactor(const QString& name, MaterialBlendFactor* factor)
{
	static const QSet<QString> names {QStringLiteral("GL_ONE"), QStringLiteral("GL_ZERO"), QStringLiteral("GL_SRC_ALPHA"),
		QStringLiteral("GL_ONE_MINUS_SRC_ALPHA"), QStringLiteral("GL_DST_ALPHA"), QStringLiteral("GL_ONE_MINUS_DST_ALPHA"),
		QStringLiteral("GL_SRC_COLOR"), QStringLiteral("GL_ONE_MINUS_SRC_COLOR")};
	return names.contains(name.toUpper()) && materialBlendFactorFromId(name, factor);
}

class Quake3Parser final : public ParserBase {
public:
	using ParserBase::ParserBase;

	void setDialect(MaterialDialect dialect) { m_dialect = dialect; }

	void parse()
	{
		lex(lexQuake3(m_text));
		while (!atEnd()) {
			const int nameIndex = m_position;
			const Token* name = take();
			if (name->isBrace(QLatin1Char('{')) || name->isBrace(QLatin1Char('}'))) {
				MaterialDiagnostic diagnostic;
				diagnostic.severity = MaterialDiagnosticSeverity::Error;
				diagnostic.code = QStringLiteral("unexpected-brace");
				diagnostic.message = Text::tr("A brace appears where a shader name was expected.");
				diagnostic.line = name->line;
				diagnostic.column = name->column;
				m_script->diagnostics.push_back(diagnostic);
				noteBraceProblem(name->text, name->line);
				if (name->isBrace(QLatin1Char('{'))) {
					skipBracedSection(nameIndex);
				}
				continue;
			}
			parseShader(nameIndex);
		}
		applyBrokenFileRule();
	}

private:
	// Jumps past the section the brace at `open` starts; to the end when it
	// has no closing brace.
	void skipBracedSection(int open)
	{
		const int close = closingBrace(open);
		m_position = close >= 0 ? close + 1 : m_limit;
	}

	// ioquake3's ScanAndLoadShaderFiles checks every shader's braces first
	// and ignores the whole file when one is missing ("Ignoring shader file
	// %s. Shader \"%s\" on line %d missing opening/closing brace"), so no
	// shader in it loads. iortcw does the same for a missing opening brace
	// only (a missing closing brace only drops its own common.shader). Quake
	// III 1.32, Enemy Territory 2.60b and OpenJK have no such check.
	struct BraceProblem {
		QString name;
		int line = 0;
	};
	std::optional<BraceProblem> m_openingProblem;
	std::optional<BraceProblem> m_closingProblem;

	void noteBraceProblem(const QString& name, int line, bool opening = true)
	{
		std::optional<BraceProblem>& slot = opening ? m_openingProblem : m_closingProblem;
		if (!slot) {
			slot = BraceProblem {name, line};
		}
	}

	void applyBrokenFileRule()
	{
		std::optional<BraceProblem> problem;
		if (m_dialect == MaterialDialect::Quake3) {
			problem = m_openingProblem ? m_openingProblem : m_closingProblem;
			if (m_openingProblem && m_closingProblem && m_closingProblem->line < m_openingProblem->line) {
				problem = m_closingProblem;
			}
		} else if (m_dialect == MaterialDialect::Wolfenstein) {
			problem = m_openingProblem;
		}
		if (!problem) {
			return;
		}
		const QString reason = Text::tr("%1 ignores this whole file: shader '%2' on line %3 is missing a brace.")
								   .arg(dialectShortName(m_dialect), problem->name)
								   .arg(problem->line);
		MaterialDiagnostic diagnostic;
		diagnostic.severity = MaterialDiagnosticSeverity::Error;
		diagnostic.code = QStringLiteral("script-ignored");
		diagnostic.message = reason;
		diagnostic.line = problem->line;
		m_script->diagnostics.push_back(diagnostic);
		for (MaterialDefinition& definition : m_script->materials) {
			if (definition.engineRejection.isEmpty()) {
				definition.engineRejection = reason;
			}
		}
	}

	void parseShader(int nameIndex)
	{
		const Token& nameToken = m_tokens.at(nameIndex);
		m_implicitKind.clear();
		m_implicitImage.clear();
		MaterialDefinition definition;
		definition.name = nameToken.text;
		definition.engine = MaterialEngine::Quake3;
		definition.kind = QStringLiteral("shader");
		definition.sourcePath = m_script->path;
		definition.nameSpan = spanOf(nameToken.start, nameToken.end);
		const Token* open = peek();
		if (!open || !open->isBrace(QLatin1Char('{'))) {
			// ParseShader warns "expecting '{'" and the shader is not loaded.
			reject(&definition, Text::tr("Quake III expects '{' after the shader name and does not load this shader."), open ? open : &nameToken);
			noteBraceProblem(nameToken.text, nameToken.line);
			if (open) {
				take();
			}
			definition.span = spanOf(nameToken.start, open ? open->end : nameToken.end);
			m_script->materials.push_back(std::move(definition));
			return;
		}
		const int openIndex = m_position;
		take();
		const int close = closingBrace(openIndex);
		const int bodyStart = open->start;
		int stageCount = 0;
		bool closed = false;
		int bodyEnd = open->end;
		{
			// A matched body is read up to its closing brace and no further.
			std::optional<Limit> limit;
			if (close >= 0) {
				limit.emplace(this, close);
			}
			while (!atEnd()) {
				const int keywordIndex = m_position;
				const Token* token = take();
				if (token->isBrace(QLatin1Char('}'))) {
					// Only reached when a line skip swallowed this brace's
					// partner: the engine's shader ends here.
					closed = true;
					bodyEnd = token->end;
					break;
				}
				if (token->isBrace(QLatin1Char('{'))) {
					if (stageCount >= 8) {
						reject(&definition, Text::tr("Quake III allows at most 8 stages (MAX_SHADER_STAGES) and drops this shader."), token, stageCount);
					}
					parseStage(&definition, keywordIndex, stageCount);
					++stageCount;
					continue;
				}
				parseGlobal(&definition, keywordIndex);
			}
		}
		if (close >= 0) {
			closed = true;
			bodyEnd = m_tokens.at(close).end;
			m_position = close + 1;
		}
		if (!closed) {
			reject(&definition, Text::tr("The shader has no closing brace; Quake III stops reading it."), &nameToken);
			noteBraceProblem(nameToken.text, nameToken.line, false);
			bodyEnd = m_tokens.isEmpty() ? nameToken.end : m_tokens.last().end;
		}
		definition.bodySpan = spanOf(bodyStart, bodyEnd);
		definition.span = spanOf(nameToken.start, bodyEnd);
		applyImplicitStages(&definition);
		finishShader(&definition);
		m_script->materials.push_back(std::move(definition));
	}

	// Enemy Territory's SetImplicitShaderStages for a lightmapped surface:
	// the lightmap, then the image multiplied in; masked or blended, the image
	// first, then the lightmap where it drew (depthFunc equal). Scripted
	// stages are left as they are.
	void applyImplicitStages(MaterialDefinition* definition) const
	{
		if (m_implicitKind.isEmpty() || !definition->stages.isEmpty()) {
			return;
		}
		QString image = m_implicitImage.isEmpty() || m_implicitImage == QStringLiteral("-") ? definition->name : m_implicitImage;
		if (QFileInfo(image).suffix().isEmpty()) {
			image += QStringLiteral(".tga");
		}
		const MaterialDefinition defaults = implicitQuake3Material(definition->name, MaterialSurfaceContext::World, image);
		if (m_implicitKind == QStringLiteral("map") || defaults.stages.size() != 2) {
			definition->stages = defaults.stages;
		} else {
			MaterialStage texture = defaults.stages.at(1);
			MaterialStage lightmap = defaults.stages.at(0);
			texture.index = 0;
			texture.blend = MaterialBlend();
			texture.depthWrite = true;
			if (m_implicitKind == QStringLiteral("mask")) {
				texture.alphaTest = MaterialAlphaTest::GreaterEqual128;
			} else {
				texture.blend.source = MaterialBlendFactor::SourceAlpha;
				texture.blend.destination = MaterialBlendFactor::OneMinusSourceAlpha;
				texture.blend.explicitBlend = true;
			}
			lightmap.index = 1;
			lightmap.blend.source = MaterialBlendFactor::DestinationColor;
			lightmap.blend.destination = MaterialBlendFactor::Zero;
			lightmap.blend.explicitBlend = true;
			lightmap.depthFunc = MaterialDepthFunc::Equal;
			lightmap.depthWrite = false;
			definition->stages = {texture, lightmap};
			// "if ( implicitCullType && !shader.cullType )": front-sided is the
			// unset value, so the implicit two-sided cull wins over it.
			if (definition->cull == MaterialCull::Front) {
				definition->cull = MaterialCull::None;
			}
		}
		MaterialDiagnostic note;
		note.severity = MaterialDiagnosticSeverity::Info;
		note.code = QStringLiteral("implicit-stages");
		note.message = Text::tr("The stages come from the implicit keyword; write stage blocks to edit them one by one.");
		note.material = definition->name;
		note.line = definition->span.line;
		definition->diagnostics.push_back(note);
	}

	QString m_implicitKind;
	QString m_implicitImage;
	MaterialDialect m_dialect = MaterialDialect::Quake3;

	// Consumes the rest of the line, as SkipRestOfLine does, warning when a
	// brace is swallowed with it.
	int skipRestOfLine(MaterialDefinition* definition, int stage)
	{
		while (nextOnLine()) {
			const Token* token = take();
			if (token->isBrace(QLatin1Char('{')) || token->isBrace(QLatin1Char('}'))) {
				diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("brace-swallowed"),
					Text::tr("Quake III skips the rest of this line, including this brace."), token, stage);
			}
		}
		// The block's own closing brace on the same line is out of reach
		// here, but the engine's line skip swallows it too.
		if (m_position == m_limit && m_limit < m_tokens.size() && !m_tokens.at(m_limit).newlineBefore) {
			diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("brace-swallowed"),
				Text::tr("Quake III skips the rest of this line, including this brace."), &m_tokens.at(m_limit), stage);
		}
		return m_position;
	}

	static bool parseWave(const QStringList& words, int first, MaterialWave* wave)
	{
		if (words.size() < first + 5) {
			return false;
		}
		MaterialWaveFunction function = MaterialWaveFunction::Sin;
		if (!materialWaveFunctionFromId(words.at(first), &function)) {
			return false;
		}
		wave->function = function;
		wave->base = toNumber(words.at(first + 1));
		wave->amplitude = toNumber(words.at(first + 2));
		wave->phase = toNumber(words.at(first + 3));
		wave->frequency = toNumber(words.at(first + 4));
		return true;
	}

	// ParseVector: "( a b c )". Like the engine, the numbers read before a
	// missing one or a missing ")" are kept.
	static bool parseVector(const QStringList& words, int first, int count, double* out)
	{
		if (words.value(first) != QStringLiteral("(")) {
			return false;
		}
		for (int index = 0; index < count; ++index) {
			if (first + 1 + index >= words.size()) {
				return false;
			}
			out[index] = toNumber(words.at(first + 1 + index));
		}
		return words.value(first + count + 1) == QStringLiteral(")");
	}

	QStringList takeLineWords()
	{
		QStringList words;
		while (nextOnLine()) {
			words << take()->text;
		}
		return words;
	}

	void parseGlobal(MaterialDefinition* definition, int keywordIndex)
	{
		const Token& keyword = m_tokens.at(keywordIndex);
		const QString key = keyword.text.toLower();
		bool recognised = true;
		bool previewIgnored = false;
		if (key.startsWith(QStringLiteral("qer"))) {
			// Editor-only keys: ParseShader skips the rest of the line.
			previewIgnored = true;
			const Token* argument = peek();
			if (key == QStringLiteral("qer_editorimage") && argument && !argument->newlineBefore) {
				definition->editorImage = argument->text;
			} else if (key == QStringLiteral("qer_trans") && argument && !argument->newlineBefore) {
				definition->editorTransparency = toNumber(argument->text);
			}
			skipRestOfLine(definition, -1);
		} else if (key.startsWith(QStringLiteral("q3map"))) {
			previewIgnored = true;
			skipRestOfLine(definition, -1);
		} else if (key == QStringLiteral("deformvertexes")) {
			parseDeform(definition, keywordIndex);
		} else if (key == QStringLiteral("tesssize")) {
			const Token* size = peek();
			if (size && !size->newlineBefore) {
				definition->tessSize = toNumber(size->text);
			}
			skipRestOfLine(definition, -1);
		} else if (key == QStringLiteral("clamptime")) {
			takeOnLine();
		} else if (key == QStringLiteral("surfaceparm")) {
			const Token* parm = takeOnLine();
			if (!parm) {
				diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("missing-argument"),
					Text::tr("surfaceparm needs a parameter name."), &keyword);
			} else {
				const QString name = parm->text.toLower();
				if (!quake3SurfaceParms().contains(name)) {
					diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("unknown-surfaceparm"),
						Text::tr("Unknown surface parameter '%1'; Quake III and q3map2 ignore it.").arg(parm->text), parm);
				} else if (!definition->surfaceParms.contains(name)) {
					definition->surfaceParms << name;
				}
			}
		} else if (key == QStringLiteral("nomipmaps")) {
			definition->noMipMaps = true;
			definition->noPicMip = true;
		} else if (key == QStringLiteral("nopicmip")) {
			definition->noPicMip = true;
		} else if (key == QStringLiteral("polygonoffset")) {
			definition->polygonOffset = true;
			definition->polygonOffsetValue = 1.0;
		} else if (key == QStringLiteral("entitymergable")) {
			definition->flags << QStringLiteral("entitymergable");
		} else if (key == QStringLiteral("fogparms")) {
			const QStringList words = takeLineWords();
			double color[3] = {0.0, 0.0, 0.0};
			if (!parseVector(words, 0, 3, color)) {
				// ParseShader returns qfalse when the colour does not parse.
				reject(definition, Text::tr("fogParms needs a colour in parentheses, fogParms ( r g b ) distance; Quake III drops this shader."),
					&keyword);
			} else if (words.size() < 6) {
				diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("missing-argument"),
					Text::tr("fogParms is missing the distance to opaque."), &keyword);
			} else {
				definition->fog.present = true;
				definition->fog.color = {color[0], color[1], color[2]};
				definition->fog.distanceToOpaque = toNumber(words.at(5));
			}
		} else if (key == QStringLiteral("portal")) {
			definition->portal = true;
			definition->sort = QStringLiteral("portal");
			definition->sortValue = 1;
		} else if (key == QStringLiteral("skyparms")) {
			const QStringList words = takeLineWords();
			// ParseSkyParms returns before marking the shader a sky when a
			// parameter is missing.
			if (words.size() < 3) {
				diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("missing-argument"),
					Text::tr("skyParms needs a far box, a cloud height and a near box (use - for none); Quake III does not draw this shader as a sky."),
					&keyword);
			} else {
				definition->sky.present = true;
				definition->sky.farBox = words.value(0);
				const double height = toNumber(words.value(1));
				definition->sky.cloudHeight = height == 0.0 ? 512.0 : height;
				definition->sky.nearBox = words.value(2);
			}
		} else if (key == QStringLiteral("light")) {
			takeOnLine();
			previewIgnored = true;
		} else if (key == QStringLiteral("cull")) {
			const Token* side = takeOnLine();
			if (!side) {
				diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("missing-argument"),
					Text::tr("cull needs none, back or front."), &keyword);
			} else {
				const QString value = side->text.toLower();
				if (value == QStringLiteral("none") || value == QStringLiteral("twosided") || value == QStringLiteral("disable")) {
					definition->cull = MaterialCull::None;
				} else if (value == QStringLiteral("back") || value == QStringLiteral("backside") || value == QStringLiteral("backsided")) {
					definition->cull = MaterialCull::Back;
				} else if (value == QStringLiteral("front")) {
					diagnose(definition, MaterialDiagnosticSeverity::Info, QStringLiteral("bad-arguments"),
						Text::tr("'cull front' is not a Quake III keyword; it warns and keeps the default, which already draws front faces."), side);
				} else {
					diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("bad-arguments"),
						Text::tr("Unknown cull side '%1'; Quake III keeps the default.").arg(side->text), side);
				}
			}
		} else if (key == QStringLiteral("sort")) {
			const Token* value = takeOnLine();
			if (!value) {
				diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("missing-argument"),
					Text::tr("sort needs a name or a number."), &keyword);
			} else {
				const auto named = quake3SortValues().constFind(value->text.toLower());
				definition->sort = value->text;
				definition->sortValue = named != quake3SortValues().constEnd() ? *named : toNumber(value->text);
			}
		} else if (dialectHasGlobalKeyword(m_dialect, key)) {
			parseDialectGlobal(definition, keywordIndex, &previewIgnored);
		} else {
			recognised = false;
			const MaterialDialect owner = dialectOwningKeyword(key, false);
			reject(definition,
				owner != MaterialDialect::Automatic
					? Text::tr("'%1' is a %2 keyword; %3 does not know it, drops this shader and draws its default.")
						  .arg(keyword.text, dialectShortName(owner), dialectShortName(m_dialect))
					: Text::tr("Unknown general shader parameter '%1'; %2 drops this shader and draws its default.")
						  .arg(keyword.text, dialectShortName(m_dialect)),
				&keyword);
			skipRestOfLine(definition, -1);
		}
		MaterialDirective directive = directiveFrom(keywordIndex, m_position);
		directive.recognised = recognised;
		directive.previewIgnored = previewIgnored;
		definition->directives.push_back(directive);
	}

	// A general keyword the dialect's game adds to Quake III's.
	void parseDialectGlobal(MaterialDefinition* definition, int keywordIndex, bool* previewIgnored)
	{
		const QString key = m_tokens.at(keywordIndex).text.toLower();
		if (key == QStringLiteral("nomipmap")) {
			definition->noMipMaps = true;
			definition->noPicMip = true;
		} else if (key.startsWith(QStringLiteral("implicit"))) {
			// Enemy Territory (tr_shader.c ParseShader): any keyword starting
			// "implicit" draws an image, or the shader's own name for "-",
			// with the engine's default stages: implicitMask alpha-tested and
			// implicitBlend blended, both two-sided.
			m_implicitKind = key == QStringLiteral("implicitmask") ? QStringLiteral("mask")
				: key == QStringLiteral("implicitblend")			 ? QStringLiteral("blend")
																	 : QStringLiteral("map");
			const Token* image = takeOnLine();
			m_implicitImage = image ? image->text : QString();
			skipRestOfLine(definition, -1);
		} else if (key == QStringLiteral("deform")) {
			// Jedi Knight's other spelling of deformVertexes.
			parseDeform(definition, keywordIndex);
		} else if (key == QStringLiteral("material")) {
			// Jedi Knight surface material: footsteps, impacts, decals.
			if (const Token* type = takeOnLine()) {
				definition->surfaceType = type->text;
			}
			skipRestOfLine(definition, -1);
			*previewIgnored = true;
		} else {
			// Fog, sun, light grid, texture compression, distance culling,
			// surface lights and hit materials act on the map or the game, not
			// on how the surface itself looks.
			skipRestOfLine(definition, -1);
			*previewIgnored = true;
		}
	}

	void parseDeform(MaterialDefinition* definition, int keywordIndex)
	{
		const Token& keyword = m_tokens.at(keywordIndex);
		const QStringList words = takeLineWords();
		MaterialDeform deform;
		deform.directive = definition->directives.size();
		const QString type = words.value(0).toLower();
		bool ok = true;
		if (type == QStringLiteral("wave")) {
			deform.kind = MaterialDeformKind::Wave;
			const double div = toNumber(words.value(1));
			// ParseDeform: a zero spread falls back to 100 with a warning.
			if (div == 0.0) {
				diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("bad-arguments"),
					Text::tr("deformVertexes wave needs a non-zero spread; Quake III uses 100."), &keyword);
				deform.spread = 100.0;
			} else {
				deform.spread = 1.0 / div;
			}
			ok = parseWave(words, 2, &deform.wave);
		} else if (type == QStringLiteral("normal")) {
			deform.kind = MaterialDeformKind::Normal;
			deform.wave.amplitude = toNumber(words.value(1));
			deform.wave.frequency = toNumber(words.value(2));
			ok = words.size() >= 3;
		} else if (type == QStringLiteral("bulge")) {
			deform.kind = MaterialDeformKind::Bulge;
			deform.values = {toNumber(words.value(1)), toNumber(words.value(2)), toNumber(words.value(3))};
			ok = words.size() >= 4;
		} else if (type == QStringLiteral("move")) {
			deform.kind = MaterialDeformKind::Move;
			deform.values = {toNumber(words.value(1)), toNumber(words.value(2)), toNumber(words.value(3))};
			ok = parseWave(words, 4, &deform.wave);
		} else if (type == QStringLiteral("autosprite")) {
			deform.kind = MaterialDeformKind::Autosprite;
		} else if (type == QStringLiteral("autosprite2")) {
			deform.kind = MaterialDeformKind::Autosprite2;
		} else if (type == QStringLiteral("projectionshadow")) {
			deform.kind = MaterialDeformKind::ProjectionShadow;
		} else if (type.startsWith(QStringLiteral("text"))) {
			// Q_stricmpn( token, "text", 4 ): any digit after it, 0 otherwise.
			deform.kind = MaterialDeformKind::Text;
		} else {
			diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("bad-arguments"),
				Text::tr("Unknown deformVertexes type '%1'; Quake III ignores it.").arg(words.value(0)), &keyword);
			return;
		}
		if (!ok) {
			diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("bad-arguments"),
				Text::tr("deformVertexes %1 is missing parameters.").arg(type), &keyword);
			return;
		}
		if ((deform.kind == MaterialDeformKind::Wave || deform.kind == MaterialDeformKind::Move)
			&& deform.wave.function == MaterialWaveFunction::Noise) {
			reject(definition, Text::tr("Only rgbGen wave accepts noise; Quake III stops with an error here."), &keyword);
			return;
		}
		if (definition->deforms.size() >= 3) {
			diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("too-many"),
				Text::tr("Quake III supports at most 3 deformVertexes per shader; this one is ignored."), &keyword);
			return;
		}
		definition->deforms.push_back(deform);
	}

	void parseStage(MaterialDefinition* definition, int openIndex, int stageIndex)
	{
		const Token& open = m_tokens.at(openIndex);
		MaterialStage stage;
		stage.index = stageIndex;
		bool closed = false;
		int end = open.end;
		bool blendSeen = false;
		const int close = closingBrace(openIndex);
		{
			std::optional<Limit> limit;
			if (close >= 0) {
				limit.emplace(this, close);
			}
			while (!atEnd()) {
				const int keywordIndex = m_position;
				const Token* token = take();
				if (token->isBrace(QLatin1Char('}'))) {
					closed = true;
					end = token->end;
					break;
				}
				if (token->isBrace(QLatin1Char('{'))) {
					// ParseStage reads it as an unknown parameter; the stage
					// still ends at its own closing brace.
					reject(definition, Text::tr("A stage cannot contain another stage; Quake III drops this shader."), token, stageIndex);
					const int nestedClose = closingBrace(keywordIndex);
					m_position = nestedClose >= 0 ? nestedClose + 1 : m_limit;
					stage.directives.push_back(directiveFrom(keywordIndex, keywordIndex + 1));
					continue;
				}
				parseStageKeyword(definition, &stage, keywordIndex, &blendSeen);
			}
		}
		if (close >= 0) {
			closed = true;
			end = m_tokens.at(close).end;
			m_position = close + 1;
		}
		if (!closed) {
			reject(definition, Text::tr("A stage has no closing brace; Quake III drops this shader."), &open, stageIndex);
			end = m_tokens.isEmpty() ? open.end : m_tokens.last().end;
		}
		stage.span = spanOf(open.start, end);
		finishStage(&stage, blendSeen);
		definition->stages.push_back(std::move(stage));
	}

	void parseStageKeyword(MaterialDefinition* definition, MaterialStage* stage, int keywordIndex, bool* blendSeen)
	{
		const Token& keyword = m_tokens.at(keywordIndex);
		const QString key = keyword.text.toLower();
		const int stageIndex = stage->index;
		bool recognised = true;
		const auto missing = [&](const QString& what) {
			reject(definition, Text::tr("Missing parameter for '%1'; Quake III drops this shader.").arg(what), &keyword, stageIndex);
		};
		bool previewIgnored = false;
		if (key == QStringLiteral("map") || key == QStringLiteral("clampmap")) {
			parseMapImage(definition, stage, keyword, key == QStringLiteral("clampmap"));
		} else if (key == QStringLiteral("animmap")) {
			parseAnimMap(definition, stage, keyword);
		} else if (key == QStringLiteral("videomap")) {
			const Token* video = takeOnLine();
			if (!video) {
				missing(keyword.text);
			} else {
				stage->imageKind = MaterialImageKind::Video;
				stage->imagePath = video->text;
			}
		} else if (key == QStringLiteral("alphafunc")) {
			const Token* function = takeOnLine();
			if (!function) {
				missing(keyword.text);
			} else {
				const QString value = function->text.toUpper();
				if (value == QStringLiteral("GT0")) {
					stage->alphaTest = MaterialAlphaTest::Greater0;
				} else if (value == QStringLiteral("LT128")) {
					stage->alphaTest = MaterialAlphaTest::Less128;
				} else if (value == QStringLiteral("GE128")) {
					stage->alphaTest = MaterialAlphaTest::GreaterEqual128;
				} else {
					diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("bad-arguments"),
						Text::tr("Invalid alphaFunc '%1'; use GT0, LT128 or GE128.").arg(function->text), function, stageIndex);
				}
			}
		} else if (key == QStringLiteral("depthfunc")) {
			const Token* function = takeOnLine();
			if (!function) {
				missing(keyword.text);
			} else if (function->text.compare(QStringLiteral("equal"), Qt::CaseInsensitive) == 0) {
				stage->depthFunc = MaterialDepthFunc::Equal;
			} else if (function->text.compare(QStringLiteral("lequal"), Qt::CaseInsensitive) == 0) {
				stage->depthFunc = MaterialDepthFunc::LessEqual;
			} else {
				diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("bad-arguments"),
					Text::tr("Unknown depthFunc '%1'; use lequal or equal.").arg(function->text), function, stageIndex);
			}
		} else if (key == QStringLiteral("detail")) {
			stage->detail = true;
		} else if (key == QStringLiteral("blendfunc")) {
			if (parseBlendFunc(definition, stage, keyword)) {
				*blendSeen = true;
			}
		} else if (key == QStringLiteral("rgbgen")) {
			parseColorGen(definition, stage, keyword, false);
		} else if (key == QStringLiteral("alphagen")) {
			parseColorGen(definition, stage, keyword, true);
		} else if (key == QStringLiteral("tcgen") || key == QStringLiteral("texgen")) {
			const QStringList words = takeLineWords();
			const QString source = words.value(0).toLower();
			stage->tcGen.explicitlySet = true;
			if (source == QStringLiteral("environment")) {
				stage->tcGen.source = MaterialTexCoordSource::Environment;
			} else if (source == QStringLiteral("lightmap")) {
				stage->tcGen.source = MaterialTexCoordSource::Lightmap;
			} else if (source == QStringLiteral("texture") || source == QStringLiteral("base")) {
				stage->tcGen.source = MaterialTexCoordSource::Base;
			} else if (source == QStringLiteral("vector")) {
				// The stage starts zeroed; what ParseVector read stays.
				double s[3] = {0, 0, 0};
				double t[3] = {0, 0, 0};
				const bool first = parseVector(words, 1, 3, s);
				if (!first || !parseVector(words, 6, 3, t)) {
					diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("bad-arguments"),
						Text::tr("tcGen vector needs two vectors: ( sx sy sz ) ( tx ty tz )."), &keyword, stageIndex);
				}
				stage->tcGen.source = MaterialTexCoordSource::Vector;
				stage->tcGen.vectorS = {s[0], s[1], s[2]};
				stage->tcGen.vectorT = {t[0], t[1], t[2]};
			} else {
				// The engine warns and leaves the default coordinates.
				stage->tcGen.explicitlySet = false;
				diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("bad-arguments"),
					Text::tr("Unknown tcGen '%1'.").arg(words.value(0)), &keyword, stageIndex);
			}
		} else if (key == QStringLiteral("tcmod")) {
			parseTcMod(definition, stage, keywordIndex);
		} else if (key == QStringLiteral("depthwrite")) {
			stage->depthWrite = true;
			stage->depthWriteExplicit = true;
		} else if (dialectHasStageKeyword(m_dialect, key)) {
			parseDialectStage(definition, stage, keyword, &previewIgnored);
		} else {
			recognised = false;
			const MaterialDialect owner = dialectOwningKeyword(key, true);
			reject(definition,
				owner != MaterialDialect::Automatic
					? Text::tr("'%1' is a %2 keyword; %3 does not know it, drops this shader and draws its default.")
						  .arg(keyword.text, dialectShortName(owner), dialectShortName(m_dialect))
					: Text::tr("Unknown stage keyword '%1'; %2 drops this shader and draws its default.").arg(keyword.text, dialectShortName(m_dialect)),
				&keyword, stageIndex);
			takeLineWords();
		}
		MaterialDirective directive = directiveFrom(keywordIndex, m_position);
		directive.recognised = recognised;
		directive.previewIgnored = previewIgnored;
		if (key == QStringLiteral("tcmod") && !stage->tcMods.isEmpty() && stage->tcMods.last().directive == -2) {
			stage->tcMods.last().directive = stage->directives.size();
		}
		stage->directives.push_back(directive);
	}

	// map and clampmap: an image, $lightmap or $whiteimage.
	void parseMapImage(MaterialDefinition* definition, MaterialStage* stage, const Token& keyword, bool clamp)
	{
		const Token* image = takeOnLine();
		if (!image) {
			reject(definition, Text::tr("Missing parameter for '%1'; Quake III drops this shader.").arg(keyword.text), &keyword, stage->index);
			return;
		}
		const QString name = image->text;
		stage->clamp = clamp;
		if (name.compare(QStringLiteral("$lightmap"), Qt::CaseInsensitive) == 0) {
			// The lightmap coordinates are only a default (finishStage): an
			// explicit tcGen wins, written before or after the map.
			stage->imageKind = MaterialImageKind::Lightmap;
		} else if (name.compare(QStringLiteral("$whiteimage"), Qt::CaseInsensitive) == 0 || name.compare(QStringLiteral("*white"), Qt::CaseInsensitive) == 0) {
			stage->imageKind = MaterialImageKind::White;
		} else {
			stage->imageKind = MaterialImageKind::File;
			stage->imagePath = name;
		}
	}

	void parseAnimMap(MaterialDefinition* definition, MaterialStage* stage, const Token& keyword)
	{
		const Token* frequency = takeOnLine();
		if (!frequency) {
			reject(definition, Text::tr("Missing parameter for '%1'; Quake III drops this shader.").arg(keyword.text), &keyword, stage->index);
			return;
		}
		stage->imageKind = MaterialImageKind::Animation;
		stage->animationFrequency = toNumber(frequency->text);
		while (nextOnLine()) {
			const Token* frame = take();
			if (stage->animationFrames.size() < 8) {
				stage->animationFrames << frame->text;
			} else {
				diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("too-many"),
					Text::tr("animMap uses at most 8 images; Quake III ignores '%1'.").arg(frame->text), frame, stage->index);
			}
		}
		if (stage->animationFrames.isEmpty()) {
			stage->imageKind = MaterialImageKind::None;
		}
	}

	// A stage keyword the dialect's game adds to Quake III's. The preview
	// draws as a 32-bit display without texture compression would.
	void parseDialectStage(MaterialDefinition* definition, MaterialStage* stage, const Token& keyword, bool* previewIgnored)
	{
		const QString key = keyword.text.toLower();
		if (key == QStringLiteral("map32") || key == QStringLiteral("mapnocomp")) {
			parseMapImage(definition, stage, keyword, false);
		} else if (key == QStringLiteral("animmapnocomp")) {
			parseAnimMap(definition, stage, keyword);
		} else if (key == QStringLiteral("map16") || key == QStringLiteral("mapcomp") || key == QStringLiteral("animmapcomp")) {
			// Read only at 16-bit colour or with compressed textures.
			takeLineWords();
			*previewIgnored = true;
		} else if (key == QStringLiteral("lightmap")) {
			// Enemy Territory: a clamped image drawn with lightmap coordinates.
			parseMapImage(definition, stage, keyword, true);
			if (!stage->tcGen.explicitlySet) {
				stage->tcGen.source = MaterialTexCoordSource::Lightmap;
			}
		} else if (key == QStringLiteral("clampanimmap") || key == QStringLiteral("oneshotanimmap")) {
			parseAnimMap(definition, stage, keyword);
			stage->clamp = key == QStringLiteral("clampanimmap");
			stage->animationOnce = key == QStringLiteral("oneshotanimmap");
		} else if (key == QStringLiteral("glow")) {
			stage->glow = true;
			*previewIgnored = true;
		} else if (key == QStringLiteral("surfacesprites")) {
			stage->surfaceSprites = true;
			takeLineWords();
			*previewIgnored = true;
		} else {
			// fog on/off and the surface-sprite options.
			takeLineWords();
			*previewIgnored = true;
		}
	}

	// False when the line is ignored: ParseStage warns about a missing
	// parameter and goes on with the next line, keeping the shader.
	bool parseBlendFunc(MaterialDefinition* definition, MaterialStage* stage, const Token& keyword)
	{
		const Token* first = takeOnLine();
		if (!first) {
			diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("missing-argument"),
				Text::tr("blendFunc has no parameter; Quake III ignores this line."), &keyword, stage->index);
			return false;
		}
		const QString shorthand = first->text.toLower();
		if (shorthand == QStringLiteral("add")) {
			stage->blend.explicitBlend = true;
			stage->blend.source = MaterialBlendFactor::One;
			stage->blend.destination = MaterialBlendFactor::One;
			stage->blend.written = first->text;
			return true;
		}
		if (shorthand == QStringLiteral("filter")) {
			stage->blend.explicitBlend = true;
			stage->blend.source = MaterialBlendFactor::DestinationColor;
			stage->blend.destination = MaterialBlendFactor::Zero;
			stage->blend.written = first->text;
			return true;
		}
		if (shorthand == QStringLiteral("blend")) {
			stage->blend.explicitBlend = true;
			stage->blend.source = MaterialBlendFactor::SourceAlpha;
			stage->blend.destination = MaterialBlendFactor::OneMinusSourceAlpha;
			stage->blend.written = first->text;
			return true;
		}
		const Token* second = takeOnLine();
		if (!second) {
			// The engine has already taken the source factor but skips the
			// depth-mask change, so the stage still writes depth; the preview
			// ignores the whole line.
			diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("missing-argument"),
				Text::tr("blendFunc needs two GL factors or add, filter or blend; Quake III ignores this line."), &keyword, stage->index);
			return false;
		}
		stage->blend.explicitBlend = true;
		stage->blend.written = first->text + QLatin1Char(' ') + second->text;
		MaterialBlendFactor source = MaterialBlendFactor::One;
		MaterialBlendFactor destination = MaterialBlendFactor::One;
		// NameToSrcBlendMode / NameToDstBlendMode warn and fall back to GL_ONE.
		if (!blendSourceFactor(first->text, &source)) {
			source = MaterialBlendFactor::One;
			diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("bad-arguments"),
				Text::tr("Unknown blend source '%1'; Quake III uses GL_ONE.").arg(first->text), first, stage->index);
		}
		if (!blendDestinationFactor(second->text, &destination)) {
			destination = MaterialBlendFactor::One;
			diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("bad-arguments"),
				Text::tr("Unknown blend destination '%1'; Quake III uses GL_ONE.").arg(second->text), second, stage->index);
		}
		stage->blend.source = source;
		stage->blend.destination = destination;
		return true;
	}

	void parseColorGen(MaterialDefinition* definition, MaterialStage* stage, const Token& keyword, bool alpha)
	{
		const QStringList words = takeLineWords();
		if (words.isEmpty()) {
			// ParseStage warns "missing parameters" and keeps the shader.
			diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("missing-argument"),
				Text::tr("%1 has no parameter; Quake III ignores this line.").arg(keyword.text), &keyword, stage->index);
			return;
		}
		MaterialColorGen& gen = alpha ? stage->alphaGen : stage->rgbGen;
		gen.explicitlySet = true;
		const QString source = words.first().toLower();
		const auto bad = [&]() {
			diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("bad-arguments"),
				Text::tr("%1 %2 has missing or invalid parameters.").arg(keyword.text, words.first()), &keyword, stage->index);
		};
		if (source == QStringLiteral("wave")) {
			gen.source = MaterialColorSource::Wave;
			if (!parseWave(words, 1, &gen.wave)) {
				bad();
			} else if (alpha && gen.wave.function == MaterialWaveFunction::Noise) {
				// TableForFunc has no noise table: only rgbGen may use noise.
				reject(definition, Text::tr("Only rgbGen wave accepts noise; Quake III stops with an error here."), &keyword, stage->index);
			}
		} else if (source == QStringLiteral("const")) {
			gen.source = MaterialColorSource::Constant;
			if (alpha) {
				if (words.size() < 2) {
					bad();
				}
				gen.constantAlpha = toNumber(words.value(1));
			} else {
				// VectorClear first: a malformed colour is black where it
				// is missing.
				double color[3] = {0, 0, 0};
				if (!parseVector(words, 1, 3, color)) {
					bad();
				}
				gen.constant = {color[0], color[1], color[2]};
			}
		} else if (source == QStringLiteral("identity")) {
			gen.source = MaterialColorSource::Identity;
		} else if (!alpha && source == QStringLiteral("identitylighting")) {
			gen.source = MaterialColorSource::IdentityLighting;
		} else if (source == QStringLiteral("entity")) {
			gen.source = MaterialColorSource::Entity;
		} else if (source == QStringLiteral("oneminusentity")) {
			gen.source = MaterialColorSource::OneMinusEntity;
		} else if (source == QStringLiteral("vertex")) {
			gen.source = MaterialColorSource::Vertex;
			// rgbGen vertex also sets alphaGen vertex when none was given.
			if (!alpha && !stage->alphaGen.explicitlySet) {
				stage->alphaGen.source = MaterialColorSource::Vertex;
			}
		} else if (!alpha && source == QStringLiteral("exactvertex")) {
			gen.source = MaterialColorSource::ExactVertex;
		} else if (!alpha && source == QStringLiteral("lightingdiffuse")) {
			gen.source = MaterialColorSource::LightingDiffuse;
		} else if (source == QStringLiteral("oneminusvertex")) {
			gen.source = MaterialColorSource::OneMinusVertex;
		} else if (alpha && source == QStringLiteral("lightingspecular")) {
			gen.source = MaterialColorSource::LightingSpecular;
		} else if (alpha && source == QStringLiteral("portal")) {
			gen.source = MaterialColorSource::Portal;
			if (words.size() < 2) {
				diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("missing-argument"),
					Text::tr("alphaGen portal has no range; Quake III uses 256."), &keyword, stage->index);
				gen.portalRange = 256.0;
			} else {
				gen.portalRange = toNumber(words.at(1));
			}
			definition->portal = true;
		} else {
			gen.explicitlySet = false;
			diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("bad-arguments"),
				Text::tr("Unknown %1 '%2'; Quake III ignores it.").arg(keyword.text, words.first()), &keyword, stage->index);
		}
	}

	void parseTcMod(MaterialDefinition* definition, MaterialStage* stage, int keywordIndex)
	{
		const Token& keyword = m_tokens.at(keywordIndex);
		const QStringList words = takeLineWords();
		if (words.isEmpty()) {
			diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("missing-argument"),
				Text::tr("tcMod needs a type."), &keyword, stage->index);
			return;
		}
		if (stage->tcMods.size() >= 4) {
			// TR_MAX_TEXMODS: the engine stops with ERR_DROP.
			reject(definition, Text::tr("More than 4 tcMods in one stage; Quake III stops with an error."), &keyword, stage->index);
			return;
		}
		MaterialTexMod mod;
		mod.directive = -2;
		const QString type = words.first().toLower();
		bool ok = true;
		if (type == QStringLiteral("turb")) {
			mod.kind = MaterialTexModKind::Turbulent;
			ok = words.size() >= 5;
			mod.wave.function = MaterialWaveFunction::Sin;
			mod.wave.base = toNumber(words.value(1));
			mod.wave.amplitude = toNumber(words.value(2));
			mod.wave.phase = toNumber(words.value(3));
			mod.wave.frequency = toNumber(words.value(4));
		} else if (type == QStringLiteral("scale")) {
			mod.kind = MaterialTexModKind::Scale;
			ok = words.size() >= 3;
			mod.values[0] = toNumber(words.value(1));
			mod.values[1] = toNumber(words.value(2));
		} else if (type == QStringLiteral("scroll")) {
			mod.kind = MaterialTexModKind::Scroll;
			ok = words.size() >= 3;
			mod.values[0] = toNumber(words.value(1));
			mod.values[1] = toNumber(words.value(2));
		} else if (type == QStringLiteral("stretch")) {
			mod.kind = MaterialTexModKind::Stretch;
			ok = parseWave(words, 1, &mod.wave);
			if (ok && mod.wave.function == MaterialWaveFunction::Noise) {
				reject(definition, Text::tr("Only rgbGen wave accepts noise; Quake III stops with an error here."), &keyword, stage->index);
			}
		} else if (type == QStringLiteral("transform")) {
			mod.kind = MaterialTexModKind::Transform;
			ok = words.size() >= 7;
			for (int index = 0; index < 6; ++index) {
				mod.values[static_cast<size_t>(index)] = toNumber(words.value(index + 1));
			}
		} else if (type == QStringLiteral("rotate")) {
			mod.kind = MaterialTexModKind::Rotate;
			ok = words.size() >= 2;
			mod.values[0] = toNumber(words.value(1));
		} else if (type == QStringLiteral("entitytranslate")) {
			mod.kind = MaterialTexModKind::EntityTranslate;
		} else if (type == QStringLiteral("swap") && m_dialect == MaterialDialect::EnemyTerritory) {
			mod.kind = MaterialTexModKind::Swap;
		} else {
			diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("bad-arguments"),
				Text::tr("Unknown tcMod '%1'; Quake III ignores it.").arg(words.first()), &keyword, stage->index);
			return;
		}
		if (!ok) {
			diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("missing-argument"),
				Text::tr("tcMod %1 is missing parameters.").arg(words.first()), &keyword, stage->index);
		}
		stage->tcMods.push_back(mod);
	}

	static void finishStage(MaterialStage* stage, bool blendSeen)
	{
		// ParseStage: GL_ONE GL_ZERO means no blending at all.
		const bool replace = !blendSeen || stage->blend.isOpaqueReplace();
		if (replace) {
			stage->blend.source = MaterialBlendFactor::One;
			stage->blend.destination = MaterialBlendFactor::Zero;
		}
		// Blended stages leave the depth buffer alone unless depthWrite says
		// otherwise.
		if (!stage->depthWriteExplicit) {
			stage->depthWrite = replace;
		}
		// The default colour: identityLighting unless the source factor reads
		// the destination.
		if (!stage->rgbGen.explicitlySet) {
			const MaterialBlendFactor source = stage->blend.source;
			stage->rgbGen.source = (replace || source == MaterialBlendFactor::One || source == MaterialBlendFactor::SourceAlpha)
				? MaterialColorSource::IdentityLighting
				: MaterialColorSource::Identity;
		}
		if (!stage->alphaGen.explicitlySet && stage->alphaGen.source != MaterialColorSource::Vertex) {
			stage->alphaGen.source = MaterialColorSource::Identity;
		}
		if (stage->imageKind == MaterialImageKind::Lightmap && !stage->tcGen.explicitlySet) {
			stage->tcGen.source = MaterialTexCoordSource::Lightmap;
		}
	}

	static void finishShader(MaterialDefinition* definition)
	{
		// FinishShader: a sky always sorts as the environment; polygonOffset
		// makes an unsorted shader a decal; otherwise the first drawn stage
		// that blends while stage 1 blends too sets it, seeThrough when that
		// stage writes depth, else blend0; then opaque. A shader without
		// stages that is no sky is a fog shader whatever its sort said.
		if (definition->sky.present) {
			definition->sort = QStringLiteral("sky");
			definition->sortValue = 2;
		}
		if (definition->sort.isEmpty() && definition->polygonOffset) {
			definition->sort = QStringLiteral("decal");
			definition->sortValue = 4;
		}
		if (definition->sort.isEmpty() && !definition->stages.isEmpty() && !definition->stages.first().blend.isOpaqueReplace()) {
			for (const MaterialStage& stage : definition->stages) {
				if (stage.imageKind == MaterialImageKind::None || stage.blend.isOpaqueReplace()) {
					continue;
				}
				definition->sort = stage.depthWrite ? QStringLiteral("seeThrough") : QStringLiteral("blend0");
				definition->sortValue = stage.depthWrite ? 5 : 9;
				break;
			}
		}
		if (definition->sort.isEmpty()) {
			definition->sort = QStringLiteral("opaque");
			definition->sortValue = 3;
		}
		if (definition->stages.isEmpty() && !definition->sky.present) {
			definition->sort = QStringLiteral("fog");
			definition->sortValue = 7;
		}
		for (const MaterialStage& stage : definition->stages) {
			if (stage.imageKind == MaterialImageKind::None) {
				// FinishShader turns such a stage off and keeps the rest.
				MaterialDiagnostic diagnostic;
				diagnostic.severity = MaterialDiagnosticSeverity::Warning;
				diagnostic.code = QStringLiteral("stage-without-image");
				diagnostic.message = Text::tr("Stage %1 has no image; Quake III skips it.").arg(stage.index + 1);
				diagnostic.material = definition->name;
				diagnostic.stage = stage.index;
				diagnostic.line = stage.span.line;
				definition->diagnostics.push_back(diagnostic);
			}
		}
	}
};

// ---------------------------------------------------------------------------
// Doom 3
// ---------------------------------------------------------------------------

const QStringList& doom3SurfaceParms()
{
	// infoParms[] in Material.cpp: content and surface flags, surface types.
	static const QStringList parms {
		QStringLiteral("solid"), QStringLiteral("water"), QStringLiteral("playerclip"), QStringLiteral("monsterclip"),
		QStringLiteral("moveableclip"), QStringLiteral("ikclip"), QStringLiteral("blood"), QStringLiteral("trigger"),
		QStringLiteral("aassolid"), QStringLiteral("aasobstacle"), QStringLiteral("flashlight_trigger"), QStringLiteral("nonsolid"),
		QStringLiteral("nullnormal"), QStringLiteral("areaportal"), QStringLiteral("qer_nocarve"), QStringLiteral("discrete"),
		QStringLiteral("nofragment"), QStringLiteral("slick"), QStringLiteral("collision"), QStringLiteral("noimpact"),
		QStringLiteral("nodamage"), QStringLiteral("ladder"), QStringLiteral("nosteps"), QStringLiteral("metal"),
		QStringLiteral("stone"), QStringLiteral("flesh"), QStringLiteral("wood"), QStringLiteral("cardboard"),
		QStringLiteral("liquid"), QStringLiteral("glass"), QStringLiteral("plastic"), QStringLiteral("ricochet"),
		QStringLiteral("surftype10"), QStringLiteral("surftype11"), QStringLiteral("surftype12"), QStringLiteral("surftype13"),
		QStringLiteral("surftype14"), QStringLiteral("surftype15"),
	};
	return parms;
}

const QStringList& doom3SurfaceTypes()
{
	static const QStringList types {QStringLiteral("metal"), QStringLiteral("stone"), QStringLiteral("flesh"), QStringLiteral("wood"),
		QStringLiteral("cardboard"), QStringLiteral("liquid"), QStringLiteral("glass"), QStringLiteral("plastic"),
		QStringLiteral("ricochet"), QStringLiteral("surftype10"), QStringLiteral("surftype11"), QStringLiteral("surftype12"),
		QStringLiteral("surftype13"), QStringLiteral("surftype14"), QStringLiteral("surftype15")};
	return types;
}

const QStringList& doom3GlobalFlags()
{
	static const QStringList flags {QStringLiteral("noshadows"), QStringLiteral("noselfshadow"), QStringLiteral("forceshadows"),
		QStringLiteral("noportalfog"), QStringLiteral("nooverlays"), QStringLiteral("forceoverlays"), QStringLiteral("translucent"),
		QStringLiteral("zeroclamp"), QStringLiteral("clamp"), QStringLiteral("alphazeroclamp"), QStringLiteral("forceopaque"),
		QStringLiteral("twosided"), QStringLiteral("backsided"), QStringLiteral("foglight"), QStringLiteral("blendlight"),
		QStringLiteral("ambientlight"), QStringLiteral("mirror"), QStringLiteral("nofog"), QStringLiteral("unsmoothedtangents"),
		QStringLiteral("suppressinsubview"), QStringLiteral("portalsky"), QStringLiteral("islightgemsurf")};
	return flags;
}

const QStringList& doom3GlobalKeywordList()
{
	static const QStringList keywords = [] {
		QStringList list {QStringLiteral("qer_editorimage"), QStringLiteral("description"), QStringLiteral("polygonoffset"),
			QStringLiteral("lightfalloffimage"), QStringLiteral("guisurf"), QStringLiteral("sort"), QStringLiteral("spectrum"),
			QStringLiteral("deform"), QStringLiteral("decalinfo"), QStringLiteral("renderbump"), QStringLiteral("diffusemap"),
			QStringLiteral("specularmap"), QStringLiteral("bumpmap"), QStringLiteral("decal_macro")};
		list += doom3GlobalFlags();
		list += doom3SurfaceParms();
		return list;
	}();
	return keywords;
}

const QStringList& doom3StageKeywordList()
{
	static const QStringList keywords {QStringLiteral("blend"), QStringLiteral("map"), QStringLiteral("remoterendermap"),
		QStringLiteral("mirrorrendermap"), QStringLiteral("xrayrendermap"), QStringLiteral("screen"), QStringLiteral("screen2"),
		QStringLiteral("glasswarp"), QStringLiteral("videomap"), QStringLiteral("soundmap"), QStringLiteral("cubemap"),
		QStringLiteral("cameracubemap"), QStringLiteral("ignorealphatest"), QStringLiteral("nearest"), QStringLiteral("linear"),
		QStringLiteral("clamp"), QStringLiteral("noclamp"), QStringLiteral("zeroclamp"), QStringLiteral("alphazeroclamp"),
		QStringLiteral("uncompressed"), QStringLiteral("highquality"), QStringLiteral("forcehighquality"), QStringLiteral("nopicmip"),
		QStringLiteral("vertexcolor"), QStringLiteral("inversevertexcolor"), QStringLiteral("privatepolygonoffset"),
		QStringLiteral("texgen"), QStringLiteral("scroll"), QStringLiteral("translate"), QStringLiteral("scale"),
		QStringLiteral("centerscale"), QStringLiteral("shear"), QStringLiteral("rotate"), QStringLiteral("maskred"),
		QStringLiteral("maskgreen"), QStringLiteral("maskblue"), QStringLiteral("maskalpha"), QStringLiteral("maskcolor"),
		QStringLiteral("maskdepth"), QStringLiteral("alphatest"), QStringLiteral("colored"), QStringLiteral("color"),
		QStringLiteral("red"), QStringLiteral("green"), QStringLiteral("blue"), QStringLiteral("alpha"), QStringLiteral("rgb"),
		QStringLiteral("rgba"), QStringLiteral("if"), QStringLiteral("program"), QStringLiteral("fragmentprogram"),
		QStringLiteral("vertexprogram"), QStringLiteral("vertexparm"), QStringLiteral("fragmentmap"), QStringLiteral("megatexture"),
		QStringLiteral("name")};
	return keywords;
}

// Doom 3 decl types other than material and table that may appear in a decl
// file; their bodies are skipped.
const QStringList& doom3OtherDeclTypes()
{
	static const QStringList types {QStringLiteral("skin"), QStringLiteral("sound"), QStringLiteral("entitydef"),
		QStringLiteral("mapdef"), QStringLiteral("fx"), QStringLiteral("particle"), QStringLiteral("articulatedfigure"),
		QStringLiteral("pda"), QStringLiteral("video"), QStringLiteral("audio"), QStringLiteral("email"), QStringLiteral("model"),
		QStringLiteral("export")};
	return types;
}

const QStringList& doom3ImageFunctions()
{
	static const QStringList functions {QStringLiteral("heightmap"), QStringLiteral("addnormals"), QStringLiteral("smoothnormals"),
		QStringLiteral("add"), QStringLiteral("scale"), QStringLiteral("invertalpha"), QStringLiteral("invertcolor"),
		QStringLiteral("makeintensity"), QStringLiteral("makealpha")};
	return functions;
}

class Doom3Parser final : public ParserBase {
public:
	using ParserBase::ParserBase;

	void parse()
	{
		lex(lexDoom3(m_text));
		while (!atEnd()) {
			const int startIndex = m_position;
			const Token* token = take();
			if (token->isBrace(QLatin1Char('{')) || token->isBrace(QLatin1Char('}'))) {
				MaterialDiagnostic diagnostic;
				diagnostic.severity = MaterialDiagnosticSeverity::Error;
				diagnostic.code = QStringLiteral("unexpected-brace");
				diagnostic.message = Text::tr("A brace appears where a decl name was expected.");
				diagnostic.line = token->line;
				diagnostic.column = token->column;
				m_script->diagnostics.push_back(diagnostic);
				if (token->isBrace(QLatin1Char('{'))) {
					skipBracedSection(startIndex);
				}
				continue;
			}
			const QString key = token->text.toLower();
			if (key == QStringLiteral("table")) {
				parseTable(startIndex);
			} else if (key == QStringLiteral("material")) {
				const int nameIndex = m_position;
				if (!take()) {
					break;
				}
				parseMaterial(startIndex, nameIndex);
			} else if (key == QStringLiteral("guide")) {
				parseGuide(startIndex);
			} else if (doom3OtherDeclTypes().contains(key) && peek() && !peek()->isBrace(QLatin1Char('{'))) {
				take();
				MaterialDiagnostic diagnostic;
				diagnostic.severity = MaterialDiagnosticSeverity::Info;
				diagnostic.code = QStringLiteral("other-decl");
				diagnostic.message = Text::tr("A %1 decl in a material file is skipped.").arg(token->text);
				diagnostic.line = token->line;
				diagnostic.column = token->column;
				m_script->diagnostics.push_back(diagnostic);
				if (peek() && peek()->isBrace(QLatin1Char('{'))) {
					skipBracedSection(m_position);
				}
			} else {
				parseMaterial(startIndex, startIndex);
			}
		}
	}

private:
	// Jumps past the section the brace at `open` starts; to the end when it
	// has no closing brace.
	void skipBracedSection(int open)
	{
		const int close = closingBrace(open);
		m_position = close >= 0 ? close + 1 : m_limit;
	}

	bool expect(MaterialDefinition* definition, const char* punctuation, const Token* near)
	{
		const Token* next = peek();
		if (next && next->type != TokenType::String && next->text == QLatin1String(punctuation)) {
			take();
			return true;
		}
		defaulted(definition, Text::tr("Expected '%1'.").arg(QLatin1String(punctuation)), next ? next : near);
		return false;
	}

	void defaulted(MaterialDefinition* definition, const QString& reason, const Token* at, int stage = -1)
	{
		if (m_lastReason.isEmpty()) {
			m_lastReason = reason;
		}
		reject(definition, Text::tr("%1 Doom 3 marks the material as defaulted.").arg(reason), at, stage);
	}

public:
	// One expression on its own, for the graph editor and the CLI.
	int parseStandaloneExpression(QVector<MaterialExpressionNode>* nodes, QString* error)
	{
		lex(lexDoom3(m_text));
		MaterialDefinition scratch;
		scratch.name = QStringLiteral("expression");
		scratch.engine = MaterialEngine::Doom3;
		if (m_tokens.isEmpty()) {
			if (error) {
				*error = Text::tr("The expression is empty.");
			}
			return -1;
		}
		const int root = parseExpression(&scratch, nullptr, -1);
		if (root < 0 || !scratch.engineRejection.isEmpty()) {
			if (error) {
				*error = m_lastReason.isEmpty() ? Text::tr("The expression could not be read.") : m_lastReason;
			}
			return -1;
		}
		if (!atEnd()) {
			if (error) {
				*error = Text::tr("Unexpected '%1' after the expression.").arg(peek()->text);
			}
			return -1;
		}
		const int offset = nodes->size();
		for (MaterialExpressionNode node : std::as_const(scratch.expressions)) {
			if (node.a >= 0) {
				node.a += offset;
			}
			if (node.b >= 0) {
				node.b += offset;
			}
			nodes->push_back(node);
		}
		return root + offset;
	}

private:
	QString m_lastReason;

	// A float that may be written with a leading minus (idLexer::ParseFloat).
	std::optional<double> parseFloat()
	{
		const Token* token = peek();
		if (!token) {
			return std::nullopt;
		}
		if (token->type == TokenType::Punctuation && token->text == QStringLiteral("-")) {
			const Token* number = peek(1);
			if (number && number->type == TokenType::Number) {
				m_position += 2;
				return -toNumber(number->text);
			}
			return std::nullopt;
		}
		if (token->type == TokenType::Number) {
			take();
			return toNumber(token->text);
		}
		return std::nullopt;
	}

	void parseTable(int startIndex)
	{
		const Token& keyword = m_tokens.at(startIndex);
		const Token* name = take();
		MaterialTable table;
		table.sourcePath = m_script->path;
		if (!name) {
			return;
		}
		table.name = name->text;
		const auto fail = [&](const QString& message, const Token* at) {
			MaterialDiagnostic diagnostic;
			diagnostic.severity = MaterialDiagnosticSeverity::Error;
			diagnostic.code = QStringLiteral("bad-table");
			diagnostic.message = message;
			diagnostic.line = at ? at->line : keyword.line;
			diagnostic.column = at ? at->column : keyword.column;
			m_script->diagnostics.push_back(diagnostic);
		};
		// SkipUntilString("{").
		while (!atEnd() && !peek()->isBrace(QLatin1Char('{'))) {
			take();
		}
		if (atEnd()) {
			fail(Text::tr("Table '%1' has no body.").arg(table.name), name);
			return;
		}
		const int open = m_position;
		take();
		const int close = closingBrace(open);
		bool ok = true;
		int end = name->end;
		std::optional<Limit> limit;
		if (close >= 0) {
			limit.emplace(this, close);
			end = m_tokens.at(close).end;
		}
		while (!atEnd()) {
			const Token* token = take();
			if (close < 0) {
				end = token->end;
			}
			if (token->isBrace(QLatin1Char('}'))) {
				break;
			}
			if (token->is("snap")) {
				table.snap = true;
			} else if (token->is("clamp")) {
				table.clamp = true;
			} else if (token->isBrace(QLatin1Char('{'))) {
				while (true) {
					const std::optional<double> value = parseFloat();
					if (!value) {
						fail(Text::tr("Table '%1' holds a value that is not a number.").arg(table.name), peek());
						ok = false;
						break;
					}
					table.values.push_back(*value);
					const Token* separator = take();
					if (!separator) {
						ok = false;
						break;
					}
					if (close < 0) {
						end = separator->end;
					}
					if (separator->isBrace(QLatin1Char('}'))) {
						break;
					}
					if (separator->text == QStringLiteral(",")) {
						continue;
					}
					fail(Text::tr("Table '%1' expects a comma or a closing brace.").arg(table.name), separator);
					ok = false;
					break;
				}
				if (!ok) {
					m_position = m_limit;
					break;
				}
			} else {
				fail(Text::tr("Unknown token '%1' in table '%2'.").arg(token->text, table.name), token);
				ok = false;
				m_position = m_limit;
				break;
			}
		}
		if (close >= 0) {
			limit.reset();
			m_position = close + 1;
		}
		table.span = spanOf(keyword.start, end);
		if (ok && table.values.isEmpty()) {
			fail(Text::tr("Table '%1' has no values.").arg(table.name), name);
		}
		if (ok) {
			m_script->tables.push_back(table);
		}
	}

	// Quake 4: `guide <name> <template>( args )`.
	void parseGuide(int startIndex)
	{
		const Token& keyword = m_tokens.at(startIndex);
		const Token* name = take();
		if (!name) {
			return;
		}
		MaterialDefinition definition;
		definition.name = name->text;
		definition.engine = MaterialEngine::Doom3;
		definition.kind = QStringLiteral("guide");
		definition.sourcePath = m_script->path;
		definition.nameSpan = spanOf(name->start, name->end);
		const Token* templateName = take();
		int end = name->end;
		if (templateName) {
			definition.guideTemplate = templateName->text;
			end = templateName->end;
			if (peek() && peek()->text == QStringLiteral("(")) {
				take();
				QString argument;
				int depth = 1;
				while (!atEnd()) {
					const Token* token = take();
					end = token->end;
					if (token->text == QStringLiteral("(")) {
						++depth;
					} else if (token->text == QStringLiteral(")")) {
						if (--depth == 0) {
							break;
						}
					} else if (token->text == QStringLiteral(",") && depth == 1) {
						definition.guideArguments << argument.trimmed();
						argument.clear();
						continue;
					}
					argument += (argument.isEmpty() ? QString() : QStringLiteral(" ")) + token->text;
				}
				if (!argument.trimmed().isEmpty()) {
					definition.guideArguments << argument.trimmed();
				}
			}
		}
		definition.span = spanOf(keyword.start, end);
		MaterialDiagnostic diagnostic;
		diagnostic.severity = MaterialDiagnosticSeverity::Info;
		diagnostic.code = QStringLiteral("guide");
		diagnostic.message = Text::tr("A Quake 4 guide instance of '%1'; the preview expands it when the guide template is loaded.")
								 .arg(definition.guideTemplate);
		diagnostic.material = definition.name;
		diagnostic.line = keyword.line;
		diagnostic.column = keyword.column;
		definition.diagnostics.push_back(diagnostic);
		m_script->materials.push_back(definition);
	}

	void parseMaterial(int startIndex, int nameIndex)
	{
		const Token& start = m_tokens.at(startIndex);
		const Token& name = m_tokens.at(nameIndex);
		MaterialDefinition definition;
		definition.name = name.text;
		definition.engine = MaterialEngine::Doom3;
		definition.kind = QStringLiteral("material");
		definition.sourcePath = m_script->path;
		definition.nameSpan = spanOf(name.start, name.end);
		const Token* open = peek();
		if (!open || !open->isBrace(QLatin1Char('{'))) {
			defaulted(&definition, Text::tr("Expected '{' after the material name."), open ? open : &name);
			definition.span = spanOf(start.start, name.end);
			m_script->materials.push_back(definition);
			return;
		}
		const int openIndex = m_position;
		take();
		// idDeclFile::LoadAndParse hands idMaterial only the text up to the
		// matched closing brace, or to the end of the file when there is
		// none.
		const int close = closingBrace(openIndex);
		int end = open->end;
		bool closed = false;
		{
			std::optional<Limit> limit;
			if (close >= 0) {
				limit.emplace(this, close);
			}
			while (!atEnd()) {
				if (!definition.engineRejection.isEmpty()) {
					// After the first error the engine stops reading the
					// material.
					m_position = m_limit;
					break;
				}
				const int keywordIndex = m_position;
				const Token* token = take();
				if (token->isBrace(QLatin1Char('}'))) {
					closed = true;
					end = token->end;
					break;
				}
				if (token->isBrace(QLatin1Char('{'))) {
					parseStage(&definition, keywordIndex);
					continue;
				}
				parseGlobal(&definition, keywordIndex);
			}
		}
		if (close >= 0) {
			closed = true;
			end = m_tokens.at(close).end;
			m_position = close + 1;
		}
		if (!closed) {
			defaulted(&definition, Text::tr("The material has no closing brace."), &name);
			end = m_tokens.isEmpty() ? name.end : m_tokens.last().end;
		}
		definition.bodySpan = spanOf(open->start, end);
		definition.span = spanOf(start.start, end);
		finishMaterial(&definition);
		m_script->materials.push_back(std::move(definition));
	}

	// R_ParsePastImageProgram: an image name or a function call.
	int parseImageProgram(MaterialDefinition* definition, const Token* near, int stage)
	{
		const Token* token = take();
		if (!token) {
			defaulted(definition, Text::tr("An image program is missing."), near, stage);
			return -1;
		}
		MaterialImageProgramNode node;
		const QString function = token->text.toLower();
		if (token->type != TokenType::String && doom3ImageFunctions().contains(function) && peek() && peek()->text == QStringLiteral("(")) {
			take();
			node.function = function;
			const int childLimit = (function == QStringLiteral("addnormals") || function == QStringLiteral("add")) ? 2 : 1;
			int numbers = 0;
			if (function == QStringLiteral("heightmap")) {
				numbers = 1;
			} else if (function == QStringLiteral("scale")) {
				numbers = 4;
			}
			for (int child = 0; child < childLimit; ++child) {
				if (child > 0 && !expect(definition, ",", token)) {
					return -1;
				}
				const int childIndex = parseImageProgram(definition, token, stage);
				if (childIndex < 0) {
					return -1;
				}
				node.children << childIndex;
			}
			for (int number = 0; number < numbers; ++number) {
				if (!expect(definition, ",", token)) {
					return -1;
				}
				const std::optional<double> value = parseFloat();
				if (!value) {
					defaulted(definition, Text::tr("%1() expects a number.").arg(token->text), peek() ? peek() : token, stage);
					return -1;
				}
				node.numbers << *value;
			}
			const Token* close = peek();
			if (!expect(definition, ")", token)) {
				return -1;
			}
			node.span = spanOf(token->start, close->end);
		} else {
			node.path = token->text;
			node.span = spanOf(token->start, token->end);
		}
		definition->imagePrograms.push_back(node);
		return definition->imagePrograms.size() - 1;
	}

	// ParseExpressionPriority / ParseTerm.
	int parseExpression(MaterialDefinition* definition, const Token* near, int stage)
	{
		return parsePriority(definition, 4, near, stage);
	}

	int parsePriority(MaterialDefinition* definition, int priority, const Token* near, int stage)
	{
		if (!definition->engineRejection.isEmpty()) {
			return -1;
		}
		if (priority == 0) {
			return parseTerm(definition, near, stage);
		}
		const int a = parsePriority(definition, priority - 1, near, stage);
		if (a < 0) {
			return -1;
		}
		// idMaterial compares the token text whatever its type: a lone `/`
		// lexes as a path name and still divides.
		const Token* token = peek();
		if (!token || token->type == TokenType::String) {
			return a;
		}
		static const QHash<QString, std::pair<int, MaterialExpressionOp>> operators {
			{QStringLiteral("*"), {1, MaterialExpressionOp::Multiply}},
			{QStringLiteral("/"), {1, MaterialExpressionOp::Divide}},
			{QStringLiteral("%"), {1, MaterialExpressionOp::Modulo}},
			{QStringLiteral("+"), {2, MaterialExpressionOp::Add}},
			{QStringLiteral("-"), {2, MaterialExpressionOp::Subtract}},
			{QStringLiteral(">"), {3, MaterialExpressionOp::Greater}},
			{QStringLiteral(">="), {3, MaterialExpressionOp::GreaterEqual}},
			{QStringLiteral("<"), {3, MaterialExpressionOp::Less}},
			{QStringLiteral("<="), {3, MaterialExpressionOp::LessEqual}},
			{QStringLiteral("=="), {3, MaterialExpressionOp::Equal}},
			{QStringLiteral("!="), {3, MaterialExpressionOp::NotEqual}},
			{QStringLiteral("&&"), {4, MaterialExpressionOp::And}},
			{QStringLiteral("||"), {4, MaterialExpressionOp::Or}},
		};
		const auto found = operators.constFind(token->text);
		if (found == operators.constEnd() || found->first != priority) {
			return a;
		}
		take();
		// The right operand is parsed at the same priority, so equal
		// priorities group to the right: a - b - c is a - (b - c).
		const int b = parsePriority(definition, priority, token, stage);
		if (b < 0) {
			return -1;
		}
		MaterialExpressionNode node;
		node.op = found->second;
		node.a = a;
		node.b = b;
		const MaterialSourceSpan left = definition->expressions.at(a).span;
		const MaterialSourceSpan right = definition->expressions.at(b).span;
		node.span = spanOf(left.start, right.end);
		definition->expressions.push_back(node);
		return definition->expressions.size() - 1;
	}

	int parseTerm(MaterialDefinition* definition, const Token* near, int stage)
	{
		const Token* token = take();
		if (!token) {
			defaulted(definition, Text::tr("An expression ends early."), near, stage);
			return -1;
		}
		MaterialExpressionNode node;
		node.span = spanOf(token->start, token->end);
		if (token->type == TokenType::Punctuation && token->text == QStringLiteral("(")) {
			const int inner = parseExpression(definition, token, stage);
			if (inner < 0) {
				return -1;
			}
			const Token* close = peek();
			if (!close || close->text != QStringLiteral(")")) {
				defaulted(definition, Text::tr("Expected ')' to close the expression."), close ? close : token, stage);
				return -1;
			}
			take();
			definition->expressions[inner].parenthesised = true;
			definition->expressions[inner].span = spanOf(token->start, close->end);
			return inner;
		}
		const QString key = token->text.toLower();
		if (token->type == TokenType::Word) {
			if (key == QStringLiteral("time")) {
				node.op = MaterialExpressionOp::Time;
			} else if (key.startsWith(QStringLiteral("parm")) && key.size() <= 6) {
				bool ok = false;
				const int index = key.mid(4).toInt(&ok);
				if (!ok || index < 0 || index > 11) {
					return tableTerm(definition, token, stage);
				}
				node.op = MaterialExpressionOp::Parm;
				node.index = index;
			} else if (key.startsWith(QStringLiteral("global")) && key.size() == 7) {
				bool ok = false;
				const int index = key.mid(6).toInt(&ok);
				if (!ok || index < 0 || index > 7) {
					return tableTerm(definition, token, stage);
				}
				node.op = MaterialExpressionOp::Global;
				node.index = index;
			} else if (key == QStringLiteral("fragmentprograms")) {
				node.op = MaterialExpressionOp::FragmentPrograms;
			} else if (key == QStringLiteral("sound")) {
				node.op = MaterialExpressionOp::Sound;
			} else {
				return tableTerm(definition, token, stage);
			}
			definition->expressions.push_back(node);
			return definition->expressions.size() - 1;
		}
		if (token->type == TokenType::Punctuation && token->text == QStringLiteral("-")) {
			// Only a number may follow a unary minus.
			const Token* number = take();
			if (!number || number->type != TokenType::Number) {
				defaulted(definition, Text::tr("Doom 3 only accepts a number after a minus sign."), number ? number : token, stage);
				return -1;
			}
			node.op = MaterialExpressionOp::Constant;
			node.value = -toNumber(number->text);
			node.span = spanOf(token->start, number->end);
			definition->expressions.push_back(node);
			return definition->expressions.size() - 1;
		}
		if (token->type == TokenType::Number) {
			node.op = MaterialExpressionOp::Constant;
			node.value = toNumber(token->text);
			definition->expressions.push_back(node);
			return definition->expressions.size() - 1;
		}
		defaulted(definition, Text::tr("Bad expression term '%1'.").arg(token->text), token, stage);
		return -1;
	}

	int tableTerm(MaterialDefinition* definition, const Token* token, int stage)
	{
		const Token* open = peek();
		if (!open || open->text != QStringLiteral("[")) {
			defaulted(definition, Text::tr("Bad expression term '%1': not a register or a table lookup.").arg(token->text), token, stage);
			return -1;
		}
		take();
		const int index = parseExpression(definition, open, stage);
		if (index < 0) {
			return -1;
		}
		const Token* close = peek();
		if (!close || close->text != QStringLiteral("]")) {
			defaulted(definition, Text::tr("Expected ']' after the table index."), close ? close : open, stage);
			return -1;
		}
		take();
		MaterialExpressionNode node;
		node.op = MaterialExpressionOp::Table;
		node.name = token->text;
		node.a = index;
		node.span = spanOf(token->start, close->end);
		definition->expressions.push_back(node);
		return definition->expressions.size() - 1;
	}

	void parseGlobal(MaterialDefinition* definition, int keywordIndex)
	{
		const Token& keyword = m_tokens.at(keywordIndex);
		const QString key = keyword.text.toLower();
		bool recognised = true;
		bool previewIgnored = false;
		if (key == QStringLiteral("qer_editorimage")) {
			previewIgnored = true;
			if (const Token* image = takeOnLine()) {
				definition->editorImage = image->text;
			}
			while (nextOnLine()) {
				take();
			}
		} else if (key == QStringLiteral("description")) {
			previewIgnored = true;
			if (const Token* text = takeOnLine()) {
				definition->description = text->text;
			}
		} else if (doom3SurfaceParms().contains(key)) {
			if (doom3SurfaceTypes().contains(key)) {
				definition->surfaceType = key;
			}
			if (!definition->surfaceParms.contains(key)) {
				definition->surfaceParms << key;
			}
			previewIgnored = true;
		} else if (key == QStringLiteral("polygonoffset")) {
			definition->polygonOffset = true;
			definition->polygonOffsetValue = 1.0;
			if (nextOnLine()) {
				if (const std::optional<double> value = parseFloat()) {
					definition->polygonOffsetValue = *value;
				} else {
					take();
				}
			}
		} else if (key == QStringLiteral("lightfalloffimage")) {
			const int program = parseImageProgram(definition, &keyword, -1);
			if (program >= 0) {
				definition->lightFalloffImage = materialImageProgramText(definition->imagePrograms, program);
			}
		} else if (key == QStringLiteral("guisurf")) {
			if (const Token* gui = takeOnLine()) {
				definition->guiSurface = gui->text;
			}
		} else if (key == QStringLiteral("sort")) {
			const Token* value = takeOnLine();
			if (!value) {
				// ParseSort: "missing sort parameter" defaults the material.
				defaulted(definition, Text::tr("sort needs a name or a number."), &keyword);
			} else {
				static const QHash<QString, double> sorts {
					{QStringLiteral("subview"), -3},
					{QStringLiteral("opaque"), 0},
					{QStringLiteral("decal"), 2},
					{QStringLiteral("far"), 3},
					{QStringLiteral("medium"), 4},
					{QStringLiteral("close"), 5},
					{QStringLiteral("almostnearest"), 6},
					{QStringLiteral("nearest"), 7},
					{QStringLiteral("postprocess"), 100},
					{QStringLiteral("portalsky"), 1},
				};
				definition->sort = value->text;
				const auto named = sorts.constFind(value->text.toLower());
				definition->sortValue = named != sorts.constEnd() ? *named : toNumber(value->text);
			}
		} else if (key == QStringLiteral("spectrum")) {
			takeOnLine();
			previewIgnored = true;
		} else if (key == QStringLiteral("deform")) {
			parseDeform(definition, keyword);
		} else if (key == QStringLiteral("decalinfo")) {
			// decalInfo <stay> <fade> ( r g b a ) ( r g b a )
			QStringList words;
			for (int count = 0; count < 14 && !atEnd() && !peek()->isBrace(QLatin1Char('{')) && !peek()->isBrace(QLatin1Char('}')); ++count) {
				words << take()->text;
				if (words.size() >= 2 && words.last() == QStringLiteral(")") && words.count(QStringLiteral(")")) == 2) {
					break;
				}
			}
			definition->decalInfo = words.join(QLatin1Char(' '));
			previewIgnored = true;
		} else if (key == QStringLiteral("renderbump")) {
			while (nextOnLine()) {
				take();
			}
			previewIgnored = true;
		} else if (key == QStringLiteral("diffusemap") || key == QStringLiteral("specularmap") || key == QStringLiteral("bumpmap")) {
			MaterialStage stage;
			stage.index = definition->stages.size();
			stage.role = key == QStringLiteral("diffusemap") ? MaterialStageRole::Diffuse
				: key == QStringLiteral("specularmap")       ? MaterialStageRole::Specular
															 : MaterialStageRole::Bump;
			stage.shorthandDirective = definition->directives.size();
			const int program = parseImageProgram(definition, &keyword, stage.index);
			setStageImage(definition, &stage, program);
			const int endIndex = m_position;
			stage.span = spanOf(keyword.start, endIndex > keywordIndex + 1 ? m_tokens.at(endIndex - 1).end : keyword.end);
			definition->stages.push_back(stage);
		} else if (key == QStringLiteral("decal_macro")) {
			definition->polygonOffset = true;
			definition->polygonOffsetValue = 1.0;
			definition->sort = QStringLiteral("decal");
			definition->sortValue = 2;
			for (const QString& flag : {QStringLiteral("discrete"), QStringLiteral("noshadows")}) {
				if (!definition->flags.contains(flag)) {
					definition->flags << flag;
				}
			}
		} else if (doom3GlobalFlags().contains(key)) {
			if (!definition->flags.contains(key)) {
				definition->flags << key;
			}
			if (key == QStringLiteral("twosided")) {
				definition->cull = MaterialCull::None;
			} else if (key == QStringLiteral("backsided")) {
				definition->cull = MaterialCull::Back;
			} else if (key == QStringLiteral("translucent")) {
				definition->translucent = true;
			} else if (key == QStringLiteral("foglight")) {
				definition->fogLight = true;
			} else if (key == QStringLiteral("blendlight")) {
				definition->blendLight = true;
			} else if (key == QStringLiteral("ambientlight")) {
				definition->ambientLight = true;
			} else if (key == QStringLiteral("mirror")) {
				definition->sort = QStringLiteral("subview");
				definition->sortValue = -3;
			}
		} else {
			recognised = false;
			defaulted(definition, Text::tr("Unknown general material parameter '%1'.").arg(keyword.text), &keyword);
		}
		MaterialDirective directive = directiveFrom(keywordIndex, m_position);
		directive.recognised = recognised;
		directive.previewIgnored = previewIgnored;
		definition->directives.push_back(directive);
	}

	void parseDeform(MaterialDefinition* definition, const Token& keyword)
	{
		const Token* type = take();
		if (!type) {
			defaulted(definition, Text::tr("deform needs a type."), &keyword);
			return;
		}
		MaterialDeform deform;
		deform.directive = definition->directives.size();
		const QString key = type->text.toLower();
		if (key == QStringLiteral("sprite") || key == QStringLiteral("tube")) {
			deform.kind = key == QStringLiteral("sprite") ? MaterialDeformKind::Sprite : MaterialDeformKind::Tube;
			definition->cull = MaterialCull::None;
			definition->flags << QStringLiteral("noshadows");
		} else if (key == QStringLiteral("flare")) {
			deform.kind = MaterialDeformKind::Flare;
			definition->cull = MaterialCull::None;
			deform.expressions[0] = parseExpression(definition, type, -1);
			definition->flags << QStringLiteral("noshadows");
		} else if (key == QStringLiteral("expand")) {
			deform.kind = MaterialDeformKind::Expand;
			deform.expressions[0] = parseExpression(definition, type, -1);
		} else if (key == QStringLiteral("move")) {
			deform.kind = MaterialDeformKind::MoveExpression;
			deform.expressions[0] = parseExpression(definition, type, -1);
		} else if (key == QStringLiteral("turbulent")) {
			deform.kind = MaterialDeformKind::Turbulent;
			const Token* table = take();
			if (!table) {
				defaulted(definition, Text::tr("deform turbulent needs a table name."), type);
				return;
			}
			deform.name = table->text;
			for (int index = 0; index < 3; ++index) {
				deform.expressions[static_cast<size_t>(index)] = parseExpression(definition, table, -1);
			}
		} else if (key == QStringLiteral("eyeball")) {
			deform.kind = MaterialDeformKind::EyeBall;
		} else if (key == QStringLiteral("particle") || key == QStringLiteral("particle2")) {
			deform.kind = key == QStringLiteral("particle") ? MaterialDeformKind::Particle : MaterialDeformKind::Particle2;
			const Token* decl = take();
			if (!decl) {
				defaulted(definition, Text::tr("deform %1 needs a particle decl.").arg(type->text), type);
				return;
			}
			deform.name = decl->text;
		} else {
			defaulted(definition, Text::tr("Bad deform type '%1'.").arg(type->text), type);
			return;
		}
		definition->deforms.push_back(deform);
	}

	void setStageImage(MaterialDefinition* definition, MaterialStage* stage, int program)
	{
		if (program < 0) {
			return;
		}
		const MaterialImageProgramNode& node = definition->imagePrograms.at(program);
		stage->imageProgram = program;
		if (!node.function.isEmpty()) {
			stage->imageKind = MaterialImageKind::Program;
			return;
		}
		const QString key = node.path.toLower();
		if (key == QStringLiteral("_white")) {
			stage->imageKind = MaterialImageKind::White;
		} else if (key == QStringLiteral("_currentrender") || key == QStringLiteral("_scratch")) {
			stage->imageKind = MaterialImageKind::RenderTarget;
			stage->imagePath = node.path;
		} else if (key.startsWith(QLatin1Char('_'))) {
			stage->imageKind = MaterialImageKind::Builtin;
			stage->imagePath = node.path;
		} else {
			stage->imageKind = MaterialImageKind::File;
			stage->imagePath = node.path;
		}
	}

	void parseStage(MaterialDefinition* definition, int openIndex)
	{
		const Token& open = m_tokens.at(openIndex);
		MaterialStage stage;
		stage.index = definition->stages.size();
		int end = open.end;
		bool closed = false;
		const int close = closingBrace(openIndex);
		{
			std::optional<Limit> limit;
			if (close >= 0) {
				limit.emplace(this, close);
			}
			while (!atEnd()) {
				if (!definition->engineRejection.isEmpty()) {
					// A defaulted material stops parsing; the stage still ends
					// at its own closing brace.
					m_position = m_limit;
					break;
				}
				const int keywordIndex = m_position;
				const Token* token = take();
				if (token->isBrace(QLatin1Char('}'))) {
					closed = true;
					end = token->end;
					break;
				}
				parseStageKeyword(definition, &stage, keywordIndex);
			}
		}
		if (close >= 0) {
			closed = true;
			end = m_tokens.at(close).end;
			m_position = close + 1;
		}
		if (!closed) {
			defaulted(definition, Text::tr("A stage has no closing brace."), &open, stage.index);
			end = m_tokens.isEmpty() ? open.end : m_tokens.last().end;
		}
		stage.span = spanOf(open.start, end);
		definition->stages.push_back(std::move(stage));
	}

	void parseTwoExpressions(MaterialDefinition* definition, MaterialStage* stage, const Token& keyword, MaterialTexModKind kind)
	{
		MaterialTexMod mod;
		mod.kind = kind;
		mod.expressions[0] = parseExpression(definition, &keyword, stage->index);
		if (mod.expressions[0] < 0 || !expect(definition, ",", &keyword)) {
			return;
		}
		mod.expressions[1] = parseExpression(definition, &keyword, stage->index);
		if (mod.expressions[1] < 0) {
			return;
		}
		mod.directive = stage->directives.size();
		stage->tcMods.push_back(mod);
	}

	void parseStageKeyword(MaterialDefinition* definition, MaterialStage* stage, int keywordIndex)
	{
		const Token& keyword = m_tokens.at(keywordIndex);
		const QString key = keyword.text.toLower();
		const int stageIndex = stage->index;
		bool recognised = true;
		bool previewIgnored = false;
		if (key == QStringLiteral("name")) {
			while (nextOnLine()) {
				take();
			}
			previewIgnored = true;
		} else if (key == QStringLiteral("blend")) {
			parseBlend(definition, stage, keyword);
		} else if (key == QStringLiteral("map")) {
			setStageImage(definition, stage, parseImageProgram(definition, &keyword, stageIndex));
		} else if (key == QStringLiteral("cubemap") || key == QStringLiteral("cameracubemap")) {
			const int program = parseImageProgram(definition, &keyword, stageIndex);
			if (program >= 0) {
				stage->imageProgram = program;
				stage->imageKind = MaterialImageKind::CubeMap;
				stage->imagePath = materialImageProgramText(definition->imagePrograms, program);
				stage->cameraCubeMap = key == QStringLiteral("cameracubemap");
			}
		} else if (key == QStringLiteral("remoterendermap") || key == QStringLiteral("mirrorrendermap") || key == QStringLiteral("xrayrendermap")) {
			parseFloat();
			parseFloat();
			stage->imageKind = MaterialImageKind::RenderTarget;
			stage->imagePath = key;
			if (key != QStringLiteral("remoterendermap")) {
				stage->tcGen.source = MaterialTexCoordSource::Screen;
			}
		} else if (key == QStringLiteral("screen")) {
			stage->tcGen.source = MaterialTexCoordSource::Screen;
			stage->tcGen.explicitlySet = true;
		} else if (key == QStringLiteral("screen2")) {
			stage->tcGen.source = MaterialTexCoordSource::Screen2;
			stage->tcGen.explicitlySet = true;
		} else if (key == QStringLiteral("glasswarp")) {
			stage->tcGen.source = MaterialTexCoordSource::GlassWarp;
			stage->tcGen.explicitlySet = true;
		} else if (key == QStringLiteral("videomap")) {
			const Token* file = take();
			if (file && file->is("loop")) {
				stage->videoLoop = true;
				file = take();
			}
			if (!file) {
				diagnose(definition, MaterialDiagnosticSeverity::Warning, QStringLiteral("missing-argument"),
					Text::tr("videoMap needs a file name."), &keyword, stageIndex);
			} else {
				stage->imageKind = MaterialImageKind::Video;
				stage->imagePath = file->text;
				stage->clamp = true;
			}
		} else if (key == QStringLiteral("soundmap")) {
			const Token* mode = take();
			stage->imageKind = MaterialImageKind::SoundMap;
			stage->imagePath = mode ? mode->text : QString();
		} else if (key == QStringLiteral("ignorealphatest")) {
			stage->ignoreAlphaTest = true;
		} else if (key == QStringLiteral("nearest")) {
			stage->nearest = true;
		} else if (key == QStringLiteral("linear")) {
			stage->nearest = false;
		} else if (key == QStringLiteral("clamp")) {
			stage->clamp = true;
		} else if (key == QStringLiteral("noclamp")) {
			stage->clamp = false;
			stage->zeroClamp = false;
			stage->alphaZeroClamp = false;
		} else if (key == QStringLiteral("zeroclamp")) {
			stage->zeroClamp = true;
		} else if (key == QStringLiteral("alphazeroclamp")) {
			stage->alphaZeroClamp = true;
		} else if (key == QStringLiteral("uncompressed") || key == QStringLiteral("highquality") || key == QStringLiteral("forcehighquality")
			|| key == QStringLiteral("nopicmip")) {
			previewIgnored = true;
		} else if (key == QStringLiteral("vertexcolor")) {
			stage->vertexColor = MaterialVertexColor::Vertex;
		} else if (key == QStringLiteral("inversevertexcolor")) {
			stage->vertexColor = MaterialVertexColor::InverseVertex;
		} else if (key == QStringLiteral("privatepolygonoffset")) {
			stage->privatePolygonOffset = 1.0;
			if (nextOnLine()) {
				if (const std::optional<double> value = parseFloat()) {
					stage->privatePolygonOffset = *value;
				}
			}
		} else if (key == QStringLiteral("texgen")) {
			const Token* source = take();
			const QString value = source ? source->text.toLower() : QString();
			stage->tcGen.explicitlySet = true;
			if (value == QStringLiteral("normal")) {
				stage->tcGen.source = MaterialTexCoordSource::Normal;
			} else if (value == QStringLiteral("reflect")) {
				stage->tcGen.source = MaterialTexCoordSource::Reflect;
			} else if (value == QStringLiteral("skybox")) {
				stage->tcGen.source = MaterialTexCoordSource::Skybox;
			} else if (value == QStringLiteral("wobblesky")) {
				stage->tcGen.source = MaterialTexCoordSource::WobbleSky;
				for (int index = 0; index < 3; ++index) {
					stage->tcGen.expressions[static_cast<size_t>(index)] = parseExpression(definition, source, stageIndex);
				}
			} else {
				// screen, screen2 and glassWarp are stage keywords of their own.
				defaulted(definition, Text::tr("Bad texGen '%1'; use normal, reflect, skybox or wobbleSky.").arg(source ? source->text : QString()),
					source ? source : &keyword, stageIndex);
			}
		} else if (key == QStringLiteral("scroll") || key == QStringLiteral("translate")) {
			parseTwoExpressions(definition, stage, keyword, MaterialTexModKind::Translate);
		} else if (key == QStringLiteral("scale")) {
			parseTwoExpressions(definition, stage, keyword, MaterialTexModKind::ScaleExpression);
		} else if (key == QStringLiteral("centerscale")) {
			parseTwoExpressions(definition, stage, keyword, MaterialTexModKind::CenterScale);
		} else if (key == QStringLiteral("shear")) {
			parseTwoExpressions(definition, stage, keyword, MaterialTexModKind::Shear);
		} else if (key == QStringLiteral("rotate")) {
			MaterialTexMod mod;
			mod.kind = MaterialTexModKind::RotateExpression;
			mod.expressions[0] = parseExpression(definition, &keyword, stageIndex);
			mod.directive = stage->directives.size();
			if (mod.expressions[0] >= 0) {
				stage->tcMods.push_back(mod);
			}
		} else if (key == QStringLiteral("maskred")) {
			stage->maskRed = true;
		} else if (key == QStringLiteral("maskgreen")) {
			stage->maskGreen = true;
		} else if (key == QStringLiteral("maskblue")) {
			stage->maskBlue = true;
		} else if (key == QStringLiteral("maskalpha")) {
			stage->maskAlpha = true;
		} else if (key == QStringLiteral("maskcolor")) {
			stage->maskRed = stage->maskGreen = stage->maskBlue = true;
		} else if (key == QStringLiteral("maskdepth")) {
			stage->maskDepth = true;
		} else if (key == QStringLiteral("alphatest")) {
			stage->alphaTest = MaterialAlphaTest::Expression;
			stage->alphaTestExpression = parseExpression(definition, &keyword, stageIndex);
		} else if (key == QStringLiteral("colored")) {
			for (int channel = 0; channel < 4; ++channel) {
				MaterialExpressionNode node;
				node.op = MaterialExpressionOp::Parm;
				node.index = channel;
				node.span = spanOf(keyword.start, keyword.end);
				definition->expressions.push_back(node);
				stage->colorExpressions[static_cast<size_t>(channel)] = definition->expressions.size() - 1;
			}
		} else if (key == QStringLiteral("color")) {
			for (int channel = 0; channel < 4; ++channel) {
				if (channel > 0 && !expect(definition, ",", &keyword)) {
					break;
				}
				stage->colorExpressions[static_cast<size_t>(channel)] = parseExpression(definition, &keyword, stageIndex);
			}
		} else if (key == QStringLiteral("red") || key == QStringLiteral("green") || key == QStringLiteral("blue") || key == QStringLiteral("alpha")) {
			const int channel = key == QStringLiteral("red") ? 0 : key == QStringLiteral("green") ? 1 : key == QStringLiteral("blue") ? 2 : 3;
			stage->colorExpressions[static_cast<size_t>(channel)] = parseExpression(definition, &keyword, stageIndex);
		} else if (key == QStringLiteral("rgb")) {
			const int expression = parseExpression(definition, &keyword, stageIndex);
			stage->colorExpressions[0] = stage->colorExpressions[1] = stage->colorExpressions[2] = expression;
		} else if (key == QStringLiteral("rgba")) {
			const int expression = parseExpression(definition, &keyword, stageIndex);
			stage->colorExpressions = {expression, expression, expression, expression};
		} else if (key == QStringLiteral("if")) {
			stage->conditionExpression = parseExpression(definition, &keyword, stageIndex);
		} else if (key == QStringLiteral("program") || key == QStringLiteral("fragmentprogram") || key == QStringLiteral("vertexprogram")
			|| key == QStringLiteral("megatexture")) {
			if (const Token* name = takeOnLine()) {
				if (key != QStringLiteral("fragmentprogram")) {
					stage->vertexProgram = name->text;
				}
				if (key != QStringLiteral("vertexprogram")) {
					stage->fragmentProgram = name->text;
				}
			}
			previewIgnored = true;
			// The renderer approximates Doom 3's stock heat haze and
			// bumpy environment programs and says so in its own notes.
			const QString program = stage->fragmentProgram.isEmpty() ? stage->vertexProgram : stage->fragmentProgram;
			const QString base = program.section(QLatin1Char('.'), 0, 0).toLower();
			if (!base.startsWith(QStringLiteral("heathaze")) && !base.startsWith(QStringLiteral("bumpyenvironment"))) {
				diagnose(definition, MaterialDiagnosticSeverity::Info, QStringLiteral("not-simulated"),
					Text::tr("ARB programs run on the graphics card in game; the preview draws the stage's image without them."), &keyword, stageIndex);
			}
		} else if (key == QStringLiteral("vertexparm")) {
			// ParseVertexParm: a register 0-3 (MAX_VERTEX_PARMS), then one to
			// four expressions. One value fills all four; a missing z is 0
			// and a missing w is 1.
			const Token* number = takeOnLine();
			const bool numeric = number && number->type == TokenType::Number;
			const int parm = numeric ? static_cast<int>(toNumber(number->text)) : -1;
			if (!numeric || parm < 0 || parm >= 4) {
				defaulted(definition, Text::tr("vertexParm needs a register number from 0 to 3."), number ? number : &keyword, stageIndex);
			} else {
				std::array<int, 4> expressions {-1, -1, -1, -1};
				expressions[0] = parseExpression(definition, &keyword, stageIndex);
				int count = 1;
				while (count < 4 && peek() && !peek()->newlineBefore && peek()->text == QStringLiteral(",")) {
					take();
					expressions[static_cast<size_t>(count)] = parseExpression(definition, &keyword, stageIndex);
					++count;
				}
				const auto constant = [&](double value) {
					MaterialExpressionNode node;
					node.op = MaterialExpressionOp::Constant;
					node.value = value;
					definition->expressions.push_back(node);
					return static_cast<int>(definition->expressions.size()) - 1;
				};
				if (count == 1) {
					expressions[1] = expressions[2] = expressions[3] = expressions[0];
				} else {
					if (count < 3) {
						expressions[2] = constant(0.0);
					}
					if (count < 4) {
						expressions[3] = constant(1.0);
					}
				}
				if (stage->vertexParms.size() <= parm) {
					stage->vertexParms.resize(parm + 1, {-1, -1, -1, -1});
				}
				stage->vertexParms[parm] = expressions;
			}
			previewIgnored = true;
		} else if (key == QStringLiteral("fragmentmap")) {
			// ParseFragmentMap: a unit 0-7 (MAX_FRAGMENT_IMAGES).
			const Token* number = takeOnLine();
			const bool numeric = number && number->type == TokenType::Number;
			const int unit = numeric ? static_cast<int>(toNumber(number->text)) : -1;
			if (!numeric || unit < 0 || unit >= 8) {
				defaulted(definition, Text::tr("fragmentMap needs a unit number from 0 to 7."), number ? number : &keyword, stageIndex);
			}
			int cube = 0;
			while (definition->engineRejection.isEmpty() && peek() && peek()->type == TokenType::Word
				&& QStringList {QStringLiteral("cubemap"), QStringLiteral("cameracubemap"), QStringLiteral("nearest"), QStringLiteral("linear"),
					   QStringLiteral("clamp"), QStringLiteral("noclamp"), QStringLiteral("zeroclamp"), QStringLiteral("alphazeroclamp"),
					   QStringLiteral("forcehighquality"), QStringLiteral("uncompressed"), QStringLiteral("highquality"),
					   QStringLiteral("nopicmip")}
					   .contains(peek()->text.toLower())) {
				const QString option = take()->text.toLower();
				cube = option == QStringLiteral("cubemap") ? 1 : option == QStringLiteral("cameracubemap") ? 2 : cube;
			}
			const int program = definition->engineRejection.isEmpty() ? parseImageProgram(definition, &keyword, stageIndex) : -1;
			if (definition->engineRejection.isEmpty() && unit >= 0 && unit < 8) {
				if (stage->fragmentMaps.size() <= unit) {
					stage->fragmentMaps.resize(unit + 1, -1);
					stage->fragmentMapCubes.resize(unit + 1, 0);
				}
				stage->fragmentMaps[unit] = program;
				stage->fragmentMapCubes[unit] = cube;
			}
			previewIgnored = true;
		} else {
			recognised = false;
			defaulted(definition, Text::tr("Unknown stage keyword '%1'.").arg(keyword.text), &keyword, stageIndex);
		}
		MaterialDirective directive = directiveFrom(keywordIndex, m_position);
		directive.recognised = recognised;
		directive.previewIgnored = previewIgnored;
		stage->directives.push_back(directive);
	}

	void parseBlend(MaterialDefinition* definition, MaterialStage* stage, const Token& keyword)
	{
		const Token* first = take();
		if (!first) {
			defaulted(definition, Text::tr("blend needs a mode."), &keyword, stage->index);
			return;
		}
		stage->blend.explicitBlend = true;
		stage->blend.written = first->text;
		const QString key = first->text.toLower();
		if (key == QStringLiteral("blend")) {
			stage->blend.source = MaterialBlendFactor::SourceAlpha;
			stage->blend.destination = MaterialBlendFactor::OneMinusSourceAlpha;
		} else if (key == QStringLiteral("add")) {
			stage->blend.source = MaterialBlendFactor::One;
			stage->blend.destination = MaterialBlendFactor::One;
		} else if (key == QStringLiteral("filter") || key == QStringLiteral("modulate")) {
			stage->blend.source = MaterialBlendFactor::DestinationColor;
			stage->blend.destination = MaterialBlendFactor::Zero;
		} else if (key == QStringLiteral("none")) {
			stage->blend.source = MaterialBlendFactor::Zero;
			stage->blend.destination = MaterialBlendFactor::One;
		} else if (key == QStringLiteral("bumpmap")) {
			stage->role = MaterialStageRole::Bump;
		} else if (key == QStringLiteral("diffusemap")) {
			stage->role = MaterialStageRole::Diffuse;
		} else if (key == QStringLiteral("specularmap")) {
			stage->role = MaterialStageRole::Specular;
		} else {
			// NameToSrcBlendMode / NameToDstBlendMode: an unknown factor
			// warns, uses GL_ONE and defaults the material.
			MaterialBlendFactor source = MaterialBlendFactor::One;
			if (!blendSourceFactor(first->text, &source)) {
				source = MaterialBlendFactor::One;
				defaulted(definition, Text::tr("Unknown blend source '%1'.").arg(first->text), first, stage->index);
			}
			if (!expect(definition, ",", first)) {
				return;
			}
			const Token* second = take();
			MaterialBlendFactor destination = MaterialBlendFactor::Zero;
			if (!second || !blendDestinationFactor(second->text, &destination)) {
				destination = MaterialBlendFactor::One;
				defaulted(definition, Text::tr("Unknown blend destination '%1'.").arg(second ? second->text : QString()), second ? second : first,
					stage->index);
			}
			stage->blend.source = source;
			stage->blend.destination = destination;
			stage->blend.written = first->text + QStringLiteral(", ") + (second ? second->text : QString());
		}
	}

	static void note(MaterialDefinition* definition, const QString& code, const QString& message)
	{
		MaterialDiagnostic diagnostic;
		diagnostic.severity = MaterialDiagnosticSeverity::Info;
		diagnostic.code = code;
		diagnostic.message = message;
		diagnostic.material = definition->name;
		diagnostic.line = definition->span.line;
		definition->diagnostics.push_back(diagnostic);
	}

	// What idMaterial::ParseMaterial settles after the body: implicit
	// interaction images, coverage and the default sort.
	void finishMaterial(MaterialDefinition* definition)
	{
		bool bump = false;
		bool diffuse = false;
		bool specular = false;
		bool reflect = false;
		bool currentRender = false;
		for (const MaterialStage& stage : definition->stages) {
			bump |= stage.role == MaterialStageRole::Bump;
			diffuse |= stage.role == MaterialStageRole::Diffuse;
			specular |= stage.role == MaterialStageRole::Specular;
			reflect |= stage.tcGen.source == MaterialTexCoordSource::Reflect;
			currentRender |= stage.imagePath.compare(QStringLiteral("_currentRender"), Qt::CaseInsensitive) == 0;
			// A program stage (newStage) draws its fragment maps; Doom 3 only
			// warns about a stage with no image and no program.
			const bool programStage = !stage.vertexProgram.isEmpty() || !stage.fragmentProgram.isEmpty();
			if (stage.imageKind == MaterialImageKind::None && !programStage && definition->engineRejection.isEmpty()) {
				MaterialDiagnostic diagnostic;
				diagnostic.severity = MaterialDiagnosticSeverity::Warning;
				diagnostic.code = QStringLiteral("stage-without-image");
				diagnostic.message = Text::tr("Stage %1 has no image; Doom 3 draws _default there.").arg(stage.index + 1);
				diagnostic.material = definition->name;
				diagnostic.stage = stage.index;
				diagnostic.line = stage.span.line;
				definition->diagnostics.push_back(diagnostic);
			}
		}
		if (!definition->engineRejection.isEmpty()) {
			return;
		}
		if ((diffuse || specular) && !bump) {
			note(definition, QStringLiteral("implicit-bump"), Text::tr("No bump map: Doom 3 lights this material with _flat."));
		}
		if (bump && !diffuse && !specular && !reflect) {
			note(definition, QStringLiteral("implicit-diffuse"), Text::tr("No diffuse map: Doom 3 lights this material with _white."));
		}
		// Coverage: the last of translucent, forceOpaque, mirror and a
		// stage's alphaTest in the text wins, as idMaterial sets it while it
		// parses; otherwise it follows the stages.
		QString coverage;
		int coverageAt = -1;
		const auto setCoverage = [&](const QString& value, int offset) {
			if (offset >= coverageAt) {
				coverage = value;
				coverageAt = offset;
			}
		};
		for (const MaterialDirective& directive : definition->directives) {
			if (directive.keyword.compare(QStringLiteral("translucent"), Qt::CaseInsensitive) == 0) {
				setCoverage(QStringLiteral("translucent"), directive.span.start);
			} else if (directive.keyword.compare(QStringLiteral("forceopaque"), Qt::CaseInsensitive) == 0
				|| directive.keyword.compare(QStringLiteral("mirror"), Qt::CaseInsensitive) == 0) {
				setCoverage(QStringLiteral("opaque"), directive.span.start);
			}
		}
		for (const MaterialStage& stage : definition->stages) {
			for (const MaterialDirective& directive : stage.directives) {
				if (directive.keyword.compare(QStringLiteral("alphatest"), Qt::CaseInsensitive) == 0) {
					setCoverage(QStringLiteral("perforated"), directive.span.start);
				}
			}
		}
		if (coverage.isEmpty()) {
			if (definition->stages.isEmpty()) {
				coverage = QStringLiteral("translucent");
			} else if (bump || diffuse || specular) {
				coverage = QStringLiteral("opaque");
			} else {
				const MaterialBlend& first = definition->stages.first().blend;
				const bool readsDestination = first.source == MaterialBlendFactor::DestinationColor
					|| first.source == MaterialBlendFactor::OneMinusDestinationColor || first.source == MaterialBlendFactor::DestinationAlpha
					|| first.source == MaterialBlendFactor::OneMinusDestinationAlpha;
				coverage = (first.destination != MaterialBlendFactor::Zero || readsDestination) ? QStringLiteral("translucent")
																								 : QStringLiteral("opaque");
			}
		}
		if (currentRender && !definition->hasFlag(QStringLiteral("portalsky"))) {
			coverage = QStringLiteral("translucent");
			if (definition->sort.isEmpty()) {
				definition->sort = QStringLiteral("postProcess");
				definition->sortValue = 100;
			}
		}
		definition->coverage = coverage;
		definition->translucent = coverage == QStringLiteral("translucent");
		if (definition->translucent && !definition->flags.contains(QStringLiteral("noshadows"))) {
			definition->flags << QStringLiteral("noshadows");
		}
		if (definition->sort.isEmpty()) {
			if (definition->polygonOffset) {
				definition->sort = QStringLiteral("decal");
				definition->sortValue = 2;
			} else if (definition->translucent) {
				definition->sort = QStringLiteral("medium");
				definition->sortValue = 4;
			} else {
				definition->sort = QStringLiteral("opaque");
				definition->sortValue = 0;
			}
		}
	}
};

// ---------------------------------------------------------------------------
// Expression printing
// ---------------------------------------------------------------------------

QString expressionText(const QVector<MaterialExpressionNode>& nodes, int root, bool forceParentheses)
{
	if (root < 0 || root >= nodes.size()) {
		return QStringLiteral("0");
	}
	const MaterialExpressionNode& node = nodes.at(root);
	QString text;
	switch (node.op) {
	case MaterialExpressionOp::Constant:
		text = numberText(node.value);
		break;
	case MaterialExpressionOp::Time:
		text = QStringLiteral("time");
		break;
	case MaterialExpressionOp::Parm:
		text = QStringLiteral("parm%1").arg(node.index);
		break;
	case MaterialExpressionOp::Global:
		text = QStringLiteral("global%1").arg(node.index);
		break;
	case MaterialExpressionOp::Sound:
		text = QStringLiteral("sound");
		break;
	case MaterialExpressionOp::FragmentPrograms:
		text = QStringLiteral("fragmentPrograms");
		break;
	case MaterialExpressionOp::Table:
		text = QStringLiteral("%1[ %2 ]").arg(node.name, expressionText(nodes, node.a, false));
		break;
	case MaterialExpressionOp::Negate: {
		const MaterialExpressionNode* operand = node.a >= 0 && node.a < nodes.size() ? &nodes.at(node.a) : nullptr;
		if (operand && operand->op == MaterialExpressionOp::Constant) {
			text = numberText(-operand->value);
		} else {
			text = QStringLiteral("( 0 - %1 )").arg(expressionText(nodes, node.a, true));
		}
		break;
	}
	default: {
		const int priority = materialExpressionOpPriority(node.op);
		const auto childNeedsParentheses = [&](int child, bool left) {
			if (child < 0 || child >= nodes.size()) {
				return false;
			}
			const MaterialExpressionNode& operand = nodes.at(child);
			if (operand.parenthesised) {
				return true;
			}
			const int childPriority = materialExpressionOpPriority(operand.op);
			if (childPriority == 0) {
				return false;
			}
			// Equal priorities group to the right, so only a left operand of
			// equal priority needs parentheses.
			return left ? childPriority >= priority : childPriority > priority;
		};
		const QString left = expressionText(nodes, node.a, childNeedsParentheses(node.a, true));
		const QString right = expressionText(nodes, node.b, childNeedsParentheses(node.b, false));
		text = QStringLiteral("%1 %2 %3").arg(left, materialExpressionOpToken(node.op), right);
		break;
	}
	}
	if (forceParentheses && materialExpressionOpPriority(node.op) > 0) {
		return QStringLiteral("( %1 )").arg(text);
	}
	return text;
}

// ---------------------------------------------------------------------------
// Editing helpers
// ---------------------------------------------------------------------------

int lineStartOf(const QString& text, int offset)
{
	const int newline = offset > 0 ? text.lastIndexOf(QLatin1Char('\n'), offset - 1) : -1;
	return newline + 1;
}

int lineEndOf(const QString& text, int offset)
{
	const int newline = text.indexOf(QLatin1Char('\n'), offset);
	return newline < 0 ? text.size() : newline;
}

QString indentationOf(const QString& text, int offset)
{
	const int start = lineStartOf(text, offset);
	int end = start;
	while (end < text.size() && (text.at(end) == QLatin1Char(' ') || text.at(end) == QLatin1Char('\t'))) {
		++end;
	}
	return text.mid(start, end - start);
}

bool onlyWhitespace(const QString& text, int from, int to)
{
	for (int index = from; index < to; ++index) {
		if (!text.at(index).isSpace()) {
			return false;
		}
	}
	return true;
}

bool whitespaceOrComment(const QString& text, int from, int to)
{
	int index = from;
	while (index < to && text.at(index).isSpace()) {
		++index;
	}
	return index >= to || text.mid(index, 2) == QStringLiteral("//");
}

QString newlineFor(const QString& text)
{
	return text.contains(QStringLiteral("\r\n")) ? QStringLiteral("\r\n") : QStringLiteral("\n");
}

// The text range that removing [start, end) should take with it: whole
// lines when nothing else shares them.
std::pair<int, int> removalRange(const QString& text, int start, int end)
{
	if (start < 0 || end < start || end > text.size()) {
		// A synthetic span: nothing to remove.
		return {0, 0};
	}
	const int lineStart = lineStartOf(text, start);
	int lineEnd = lineEndOf(text, end);
	if (onlyWhitespace(text, lineStart, start) && whitespaceOrComment(text, end, lineEnd)) {
		if (lineEnd < text.size()) {
			++lineEnd;
		}
		return {lineStart, lineEnd};
	}
	int trailing = end;
	while (trailing < text.size() && (text.at(trailing) == QLatin1Char(' ') || text.at(trailing) == QLatin1Char('\t'))) {
		++trailing;
	}
	return {start, trailing};
}

QString unitIndent(const QString& text)
{
	return text.contains(QStringLiteral("\n\t")) || !text.contains(QStringLiteral("\n    ")) ? QStringLiteral("\t") : QStringLiteral("    ");
}

struct Block {
	const QVector<MaterialDirective>* directives = nullptr;
	MaterialSourceSpan span; // braces inclusive
	bool shorthand = false;
};

// Stages with no text: Enemy Territory's implicitMap and friends make them.
QString implicitStageError(int stage)
{
	return Text::tr("Stage %1 comes from an implicit keyword such as implicitMap; write stage blocks to edit it.").arg(stage + 1);
}

bool blockFor(const MaterialDefinition& definition, int stage, Block* block, QString* error)
{
	if (stage < 0) {
		block->directives = &definition.directives;
		block->span = definition.bodySpan;
		return block->span.isValid();
	}
	if (stage >= definition.stages.size()) {
		*error = Text::tr("Stage %1 does not exist.").arg(stage + 1);
		return false;
	}
	const MaterialStage& target = definition.stages.at(stage);
	if (!target.span.isValid()) {
		*error = implicitStageError(stage);
		return false;
	}
	if (target.shorthandDirective >= 0) {
		*error = Text::tr("Stage %1 is a %2 shorthand; edit its global directive or expand it into a stage block.")
					 .arg(stage + 1)
					 .arg(definition.directives.value(target.shorthandDirective).keyword);
		return false;
	}
	block->directives = &target.directives;
	block->span = target.span;
	return block->span.isValid();
}

int findDirective(const QVector<MaterialDirective>& directives, const MaterialEdit& edit)
{
	if (edit.directive >= 0) {
		return edit.directive < directives.size() ? edit.directive : -1;
	}
	for (int index = 0; index < directives.size(); ++index) {
		if (directives.at(index).keyword.compare(edit.keyword, Qt::CaseInsensitive) == 0) {
			return index;
		}
	}
	return -1;
}

QString directiveText(const QString& keyword, const QString& arguments)
{
	const QString trimmed = arguments.trimmed();
	return trimmed.isEmpty() ? keyword : keyword + QLatin1Char(' ') + trimmed;
}

struct Patch {
	int start = 0;
	int end = 0;
	QString replacement;
};

QString applyPatch(const QString& text, const Patch& patch)
{
	QString result = text;
	result.replace(patch.start, patch.end - patch.start, patch.replacement);
	return result;
}

// Inserts a directive line into a block before directive `position` or, for
// -1, after the last one.
Patch insertionPatch(const QString& text, const Block& block, int position, const QString& line)
{
	const QString newline = newlineFor(text);
	const QVector<MaterialDirective>& directives = *block.directives;
	QString indent;
	if (!directives.isEmpty()) {
		indent = indentationOf(text, directives.first().span.start);
		if (lineStartOf(text, directives.first().span.start) == lineStartOf(text, block.span.start)) {
			indent = indentationOf(text, block.span.start) + unitIndent(text);
		}
	} else {
		indent = indentationOf(text, block.span.start) + unitIndent(text);
	}
	if (position >= 0 && position < directives.size()) {
		const int anchor = directives.at(position).span.start;
		const int lineStart = lineStartOf(text, anchor);
		if (onlyWhitespace(text, lineStart, anchor)) {
			return {lineStart, lineStart, indent + line + newline};
		}
		return {anchor, anchor, line + newline + indent};
	}
	// After the block's last content, before the closing brace.
	const int close = block.span.end - 1;
	const int closeLineStart = lineStartOf(text, close);
	if (onlyWhitespace(text, closeLineStart, close)) {
		return {closeLineStart, closeLineStart, indent + line + newline};
	}
	return {close, close, newline + indent + line + newline + indentationOf(text, block.span.start)};
}

MaterialEditResult finish(const MaterialScript& script, const QString& text, int changeStart, int changeEnd)
{
	MaterialEditResult result;
	result.text = text;
	// An edit that changes nothing (a move onto itself) keeps the parse.
	result.script = text == script.text ? script : parseMaterialScript(text, script.engine, script.path, script.dialect);
	result.ok = true;
	result.changeStart = changeStart;
	result.changeEnd = changeEnd;
	return result;
}

MaterialEditResult failure(const QString& message)
{
	MaterialEditResult result;
	result.ok = false;
	result.error = message;
	return result;
}

QString stageBlockText(const QString& text, const QString& indent, const QString& body)
{
	const QString newline = newlineFor(text);
	QString block = indent + QStringLiteral("{") + newline;
	const QString inner = indent + unitIndent(text);
	for (const QString& line : body.split(QLatin1Char('\n'))) {
		const QString trimmed = line.trimmed();
		if (!trimmed.isEmpty()) {
			block += inner + trimmed + newline;
		}
	}
	block += indent + QStringLiteral("}") + newline;
	return block;
}

QString stageIndent(const QString& text, const MaterialDefinition& definition)
{
	for (const MaterialStage& stage : definition.stages) {
		if (stage.shorthandDirective < 0 && stage.span.isValid()) {
			return indentationOf(text, stage.span.start);
		}
	}
	return indentationOf(text, definition.bodySpan.start) + unitIndent(text);
}

// A blocks's full-line text range [start, end) when it owns its lines.
std::pair<int, int> stageLines(const QString& text, const MaterialStage& stage)
{
	return removalRange(text, stage.span.start, stage.span.end);
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

MaterialEngine materialEngineForScriptPath(const QString& path)
{
	const QString suffix = QFileInfo(path).suffix().toLower();
	if (suffix == QStringLiteral("shader")) {
		return MaterialEngine::Quake3;
	}
	if (suffix == QStringLiteral("mtr")) {
		return MaterialEngine::Doom3;
	}
	return MaterialEngine::Unknown;
}

bool isMaterialScriptPath(const QString& path)
{
	return materialEngineForScriptPath(path) != MaterialEngine::Unknown;
}

MaterialDialect detectMaterialDialect(const QStringList& texts)
{
	// Keywords at the start of a line (after indentation), so shader names
	// such as textures/base/glow do not count; comments are skipped.
	static const QRegularExpression enemyTerritory(
		QStringLiteral("^[ \\t]*(implicitmap|implicitmask|implicitblend|distancecull|lightmap[ \\t]+\\S|tcmod[ \\t]+swap)\\b"),
		QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
	static const QRegularExpression jediKnight(
		QStringLiteral("^[ \\t]*(surfacesprites|ss(fademax|fadescale|variance|hangdown|anyangle|faceup|wind|windidle|vertskew|fxduration|fxgrow|"
					   "fxalpharange|fxweather)|glow|clampanimmap|oneshotanimmap|material|q3map_material|hitlocation|hitmaterial|notc|noglfog|"
					   "surfacelight|deform)\\b"),
		QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
	static const QRegularExpression wolfenstein(
		QStringLiteral("^[ \\t]*(fogvars|skyfogvars|waterfogvars|sunshader|lightgridmulamb|lightgridmuldir|nofog|allowcompress|nocompress|"
					   "nomipmap|map16|map32|mapcomp|mapnocomp|animmapcomp|animmapnocomp|fog[ \\t]+(on|off))\\b"),
		QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
	// Comments become spaces (line breaks kept), so keywords inside them
	// never count.
	const auto withoutComments = [](const QString& text) {
		if (!text.contains(QLatin1Char('/'))) {
			return text;
		}
		QString result = text;
		QChar* data = result.data();
		const int size = static_cast<int>(result.size());
		bool quoted = false;
		for (int index = 0; index < size; ++index) {
			if (data[index] == QLatin1Char('"')) {
				quoted = !quoted;
			} else if (data[index] == QLatin1Char('\n')) {
				quoted = false;
			}
			if (quoted || data[index] != QLatin1Char('/') || index + 1 >= size) {
				continue;
			}
			if (data[index + 1] == QLatin1Char('/')) {
				while (index < size && data[index] != QLatin1Char('\n')) {
					data[index++] = QLatin1Char(' ');
				}
			} else if (data[index + 1] == QLatin1Char('*')) {
				data[index++] = QLatin1Char(' ');
				data[index++] = QLatin1Char(' ');
				while (index < size && !(data[index] == QLatin1Char('*') && index + 1 < size && data[index + 1] == QLatin1Char('/'))) {
					if (data[index] != QLatin1Char('\n')) {
						data[index] = QLatin1Char(' ');
					}
					++index;
				}
				if (index < size) {
					data[index] = QLatin1Char(' ');
					if (index + 1 < size) {
						data[++index] = QLatin1Char(' ');
					}
				}
			}
		}
		return result;
	};
	int jedi = 0;
	int wolf = 0;
	bool et = false;
	for (const QString& source : texts) {
		const QString text = withoutComments(source);
		et = et || enemyTerritory.match(text).hasMatch();
		for (auto match = jediKnight.globalMatch(text); match.hasNext(); match.next()) {
			++jedi;
		}
		for (auto match = wolfenstein.globalMatch(text); match.hasNext(); match.next()) {
			++wolf;
		}
	}
	if (et) {
		return MaterialDialect::EnemyTerritory;
	}
	if (jedi > 0 || wolf > 0) {
		return jedi >= wolf ? MaterialDialect::JediKnight : MaterialDialect::Wolfenstein;
	}
	return MaterialDialect::Quake3;
}

MaterialDialect detectMaterialDialect(const QString& text)
{
	return detectMaterialDialect(QStringList {text});
}

MaterialScript parseMaterialScript(const QString& text, MaterialEngine engine, const QString& path, MaterialDialect dialect)
{
	MaterialScript script;
	script.path = path;
	script.text = text;
	script.engine = engine == MaterialEngine::Unknown ? materialEngineForScriptPath(path) : engine;
	if (script.engine == MaterialEngine::Unknown) {
		script.engine = MaterialEngine::Quake3;
	}
	if (script.engine == MaterialEngine::Doom3) {
		Doom3Parser parser(script.text, &script);
		parser.parse();
	} else {
		script.dialect = dialect == MaterialDialect::Automatic ? detectMaterialDialect(text) : dialect;
		Quake3Parser parser(script.text, &script);
		parser.setDialect(script.dialect);
		parser.parse();
	}
	// Same-name definitions inside one script: the first one is reported as
	// the one tools find; the library resolves overrides across scripts.
	QHash<QString, int> seen;
	for (int index = 0; index < script.materials.size(); ++index) {
		MaterialDefinition& definition = script.materials[index];
		const QString key = materialLookupKey(definition.name);
		const auto first = seen.constFind(key);
		if (first != seen.constEnd()) {
			MaterialDiagnostic diagnostic;
			diagnostic.severity = MaterialDiagnosticSeverity::Warning;
			diagnostic.code = QStringLiteral("duplicate-definition");
			diagnostic.message = Text::tr("'%1' is defined again; line %2 has the earlier definition.")
									 .arg(definition.name)
									 .arg(script.materials.at(*first).span.line);
			diagnostic.material = definition.name;
			diagnostic.line = definition.span.line;
			definition.diagnostics.push_back(diagnostic);
		} else {
			seen.insert(key, index);
		}
	}
	return script;
}

bool loadMaterialScript(const QString& path, MaterialScript* script, QString* error, MaterialEngine engine, MaterialDialect dialect)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = file.errorString();
		}
		return false;
	}
	constexpr qint64 limit = 32 * 1024 * 1024;
	if (file.size() > limit) {
		if (error) {
			*error = Text::tr("The script is larger than 32 MiB.");
		}
		return false;
	}
	const QByteArray bytes = file.readAll();
	QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
	QString text = decoder(bytes);
	if (decoder.hasError()) {
		text = QString::fromLatin1(bytes);
	}
	*script = parseMaterialScript(text, engine, path, dialect);
	return true;
}

int parseMaterialExpression(const QString& text, QVector<MaterialExpressionNode>* nodes, QString* error)
{
	MaterialScript scratch;
	scratch.engine = MaterialEngine::Doom3;
	scratch.text = text;
	Doom3Parser parser(scratch.text, &scratch);
	return parser.parseStandaloneExpression(nodes, error);
}

QString materialExpressionText(const QVector<MaterialExpressionNode>& nodes, int root)
{
	if (root >= 0 && root < nodes.size() && nodes.at(root).parenthesised && materialExpressionOpPriority(nodes.at(root).op) > 0) {
		return expressionText(nodes, root, true);
	}
	return expressionText(nodes, root, false);
}

QString materialImageProgramText(const QVector<MaterialImageProgramNode>& nodes, int root)
{
	if (root < 0 || root >= nodes.size()) {
		return QString();
	}
	const MaterialImageProgramNode& node = nodes.at(root);
	if (node.function.isEmpty()) {
		return node.path;
	}
	QStringList arguments;
	for (int child : node.children) {
		arguments << materialImageProgramText(nodes, child);
	}
	for (double number : node.numbers) {
		arguments << numberText(number);
	}
	return QStringLiteral("%1( %2 )").arg(node.function, arguments.join(QStringLiteral(", ")));
}

QStringList materialImageProgramImages(const QVector<MaterialImageProgramNode>& nodes, int root)
{
	QStringList images;
	if (root < 0 || root >= nodes.size()) {
		return images;
	}
	const MaterialImageProgramNode& node = nodes.at(root);
	if (node.function.isEmpty()) {
		images << node.path;
		return images;
	}
	for (int child : node.children) {
		images += materialImageProgramImages(nodes, child);
	}
	return images;
}

QStringList materialGlobalKeywords(MaterialEngine engine, MaterialDialect dialect)
{
	if (engine == MaterialEngine::Doom3) {
		return doom3GlobalKeywordList();
	}
	if (engine != MaterialEngine::Quake3) {
		return {};
	}
	QStringList keywords = quake3GlobalKeywordList();
	if (dialect == MaterialDialect::Wolfenstein) {
		keywords += wolfensteinGlobalKeywords();
	} else if (dialect == MaterialDialect::EnemyTerritory || dialect == MaterialDialect::Automatic) {
		keywords += enemyTerritoryGlobalKeywords();
	}
	if (dialect == MaterialDialect::JediKnight || dialect == MaterialDialect::Automatic) {
		keywords += jediKnightGlobalKeywords();
	}
	return keywords;
}

QStringList materialStageKeywords(MaterialEngine engine, MaterialDialect dialect)
{
	if (engine == MaterialEngine::Doom3) {
		return doom3StageKeywordList();
	}
	if (engine != MaterialEngine::Quake3) {
		return {};
	}
	QStringList keywords = quake3StageKeywordList();
	if (dialect == MaterialDialect::Wolfenstein) {
		keywords += wolfensteinStageKeywords();
	} else if (dialect == MaterialDialect::EnemyTerritory || dialect == MaterialDialect::Automatic) {
		keywords += enemyTerritoryStageKeywords();
	}
	if (dialect == MaterialDialect::JediKnight || dialect == MaterialDialect::Automatic) {
		keywords += jediKnightStageKeywords();
	}
	return keywords;
}

namespace {

struct KeywordHelp {
	MaterialEngine engine;
	bool stage;
	const char* keyword;
	const char* help;
};

// Shown in completion and the graph inspector. Translated where shown.
const KeywordHelp kKeywordHelp[] = {
	{MaterialEngine::Quake3, true, "map", QT_TRANSLATE_NOOP("VibeStudioMaterials", "The stage's image: a path, $lightmap or $whiteimage.")},
	{MaterialEngine::Quake3, true, "clampmap", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Like map, but the image does not repeat.")},
	{MaterialEngine::Quake3, true, "animmap", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Cycles up to 8 images at the given frames per second.")},
	{MaterialEngine::Quake3, true, "videomap", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Plays a RoQ video as the stage's image.")},
	{MaterialEngine::Quake3, true, "blendfunc", QT_TRANSLATE_NOOP("VibeStudioMaterials", "How the stage mixes with what is drawn: add, filter, blend, or two GL factors.")},
	{MaterialEngine::Quake3, true, "rgbgen", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Where the stage's colour comes from: identity, wave, const, vertex, entity, lightingDiffuse.")},
	{MaterialEngine::Quake3, true, "alphagen", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Where the stage's alpha comes from: wave, const, vertex, entity, lightingSpecular, portal.")},
	{MaterialEngine::Quake3, true, "tcgen", QT_TRANSLATE_NOOP("VibeStudioMaterials", "How texture coordinates are made: base, lightmap, environment, vector.")},
	{MaterialEngine::Quake3, true, "tcmod", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Moves the texture coordinates: scroll, scale, rotate, stretch, turb, transform. Applied in order.")},
	{MaterialEngine::Quake3, true, "alphafunc", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Discards pixels by alpha: GT0, LT128 or GE128.")},
	{MaterialEngine::Quake3, true, "depthfunc", QT_TRANSLATE_NOOP("VibeStudioMaterials", "lequal (default) or equal, to draw only where an earlier stage drew.")},
	{MaterialEngine::Quake3, true, "depthwrite", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Writes depth even when the stage blends.")},
	{MaterialEngine::Quake3, true, "detail", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Marks a detail texture that r_detailtextures can turn off.")},
	{MaterialEngine::Quake3, false, "surfaceparm", QT_TRANSLATE_NOOP("VibeStudioMaterials", "A compiler and game flag such as nonsolid, trans, water or sky.")},
	{MaterialEngine::Quake3, false, "cull", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Which faces draw: back, or none (twosided) for both; front faces are the default.")},
	{MaterialEngine::Quake3, false, "deformvertexes", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Moves vertices every frame: wave, normal, bulge, move, autosprite.")},
	{MaterialEngine::Quake3, false, "skyparms", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Sky box images, cloud height and inner box.")},
	{MaterialEngine::Quake3, false, "fogparms", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Fog volume colour and the distance at which it is opaque.")},
	{MaterialEngine::Quake3, false, "sort", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Draw order: portal, sky, opaque, decal, banner, underwater, additive, nearest or a number.")},
	{MaterialEngine::Quake3, false, "polygonoffset", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Pulls the surface towards the viewer so decals do not flicker.")},
	{MaterialEngine::Quake3, false, "nopicmip", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Keeps full resolution whatever r_picmip says.")},
	{MaterialEngine::Quake3, false, "nomipmaps", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Uses no mip levels at all.")},
	{MaterialEngine::Quake3, false, "portal", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Marks a mirror or portal surface.")},
	{MaterialEngine::Quake3, false, "qer_editorimage", QT_TRANSLATE_NOOP("VibeStudioMaterials", "The image level editors show for this shader.")},
	{MaterialEngine::Quake3, false, "tesssize", QT_TRANSLATE_NOOP("VibeStudioMaterials", "How finely q3map2 subdivides surfaces so vertex deforms look smooth.")},
	{MaterialEngine::Quake3, true, "glow", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Jedi Knight: the stage also lights the glow (bloom) pass.")},
	{MaterialEngine::Quake3, true, "surfacesprites", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Jedi Knight: scatters sprites such as grass over the surface instead of drawing the stage on it.")},
	{MaterialEngine::Quake3, true, "oneshotanimmap", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Jedi Knight: plays the images once and holds the last.")},
	{MaterialEngine::Quake3, true, "clampanimmap", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Jedi Knight: animMap with images that do not repeat.")},
	{MaterialEngine::Quake3, true, "lightmap", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Enemy Territory: a clamped image drawn with lightmap coordinates.")},
	{MaterialEngine::Quake3, true, "mapnocomp", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Wolfenstein: the image used while texture compression is off.")},
	{MaterialEngine::Quake3, true, "mapcomp", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Wolfenstein: the image used while texture compression is on.")},
	{MaterialEngine::Quake3, false, "implicitmap", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Enemy Territory: the engine's default stages for an image (- for the shader's own name).")},
	{MaterialEngine::Quake3, false, "fogvars", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Wolfenstein: the map's global fog colour and distance.")},
	{MaterialEngine::Quake3, false, "sunshader", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Wolfenstein: the shader drawn as the sun.")},
	{MaterialEngine::Quake3, false, "material", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Jedi Knight: the surface material for footsteps, impacts and decals.")},
	{MaterialEngine::Doom3, true, "blend", QT_TRANSLATE_NOOP("VibeStudioMaterials", "blend, add, filter, modulate, none, two GL factors, or diffusemap, bumpmap, specularmap for lighting.")},
	{MaterialEngine::Doom3, true, "map", QT_TRANSLATE_NOOP("VibeStudioMaterials", "The stage's image or image program, such as heightmap( image, 4 ).")},
	{MaterialEngine::Doom3, true, "cubemap", QT_TRANSLATE_NOOP("VibeStudioMaterials", "A cube map from six images with _px, _nx... suffixes.")},
	{MaterialEngine::Doom3, true, "cameracubemap", QT_TRANSLATE_NOOP("VibeStudioMaterials", "A cube map from six camera-style images (_forward, _back...).")},
	{MaterialEngine::Doom3, true, "texgen", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Generated coordinates: normal, reflect, skybox or wobbleSky; screen, screen2 and glassWarp are stage keywords.")},
	{MaterialEngine::Doom3, true, "scroll", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Offsets the coordinates by two expressions, such as scroll time * 0.1, 0.")},
	{MaterialEngine::Doom3, true, "translate", QT_TRANSLATE_NOOP("VibeStudioMaterials", "The same as scroll.")},
	{MaterialEngine::Doom3, true, "scale", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Scales the coordinates about the origin.")},
	{MaterialEngine::Doom3, true, "centerscale", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Scales the coordinates about the image centre.")},
	{MaterialEngine::Doom3, true, "shear", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Shears the coordinates about the image centre.")},
	{MaterialEngine::Doom3, true, "rotate", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Rotates about the image centre; the expression counts whole turns.")},
	{MaterialEngine::Doom3, true, "rgb", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Sets red, green and blue to one expression.")},
	{MaterialEngine::Doom3, true, "rgba", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Sets every channel to one expression.")},
	{MaterialEngine::Doom3, true, "color", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Four expressions: red, green, blue, alpha.")},
	{MaterialEngine::Doom3, true, "colored", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Colour from the entity's parm0 to parm3.")},
	{MaterialEngine::Doom3, true, "alphatest", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Discards pixels whose alpha is below the expression.")},
	{MaterialEngine::Doom3, true, "if", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Draws the stage only while the expression is not zero.")},
	{MaterialEngine::Doom3, true, "vertexcolor", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Multiplies by the vertex colour.")},
	{MaterialEngine::Doom3, true, "clamp", QT_TRANSLATE_NOOP("VibeStudioMaterials", "The image does not repeat.")},
	{MaterialEngine::Doom3, true, "zeroclamp", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Outside the image is black.")},
	{MaterialEngine::Doom3, false, "diffusemap", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Shorthand for a diffuse lighting stage.")},
	{MaterialEngine::Doom3, false, "bumpmap", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Shorthand for a normal map stage.")},
	{MaterialEngine::Doom3, false, "specularmap", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Shorthand for a specular lighting stage.")},
	{MaterialEngine::Doom3, false, "translucent", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Blended, not lit, and drawn after opaque surfaces.")},
	{MaterialEngine::Doom3, false, "twosided", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Draws both faces and casts no shadows.")},
	{MaterialEngine::Doom3, false, "noshadows", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Casts no shadows.")},
	{MaterialEngine::Doom3, false, "deform", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Changes geometry: sprite, tube, flare, expand, move, turbulent, eyeBall, particle.")},
	{MaterialEngine::Doom3, false, "sort", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Draw order: subview, opaque, decal, far, medium, close, almostNearest, nearest, postProcess.")},
	{MaterialEngine::Doom3, false, "lightfalloffimage", QT_TRANSLATE_NOOP("VibeStudioMaterials", "For a light: the image that fades it with distance.")},
	{MaterialEngine::Doom3, false, "polygonoffset", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Pulls the surface towards the viewer.")},
	{MaterialEngine::Doom3, false, "qer_editorimage", QT_TRANSLATE_NOOP("VibeStudioMaterials", "The image the editor shows.")},
	{MaterialEngine::Doom3, false, "decal_macro", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Polygon offset, discrete, decal sort and no shadows in one word.")},
	// Quake III keywords the list above leaves out.
	{MaterialEngine::Quake3, true, "texgen", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Another spelling of tcGen.")},
	{MaterialEngine::Quake3, false, "qer_trans", QT_TRANSLATE_NOOP("VibeStudioMaterials", "How see-through level editors draw this shader, from 0 to 1.")},
	{MaterialEngine::Quake3, false, "qer_nocarve", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Level editors do not cut brushes with this shader in CSG operations.")},
	{MaterialEngine::Quake3, false, "qer_alphafunc", QT_TRANSLATE_NOOP("VibeStudioMaterials", "How level editors alpha-test the editor image.")},
	{MaterialEngine::Quake3, false, "q3map_sun", QT_TRANSLATE_NOOP("VibeStudioMaterials", "q3map2: the sky's sun colour, brightness and direction.")},
	{MaterialEngine::Quake3, false, "q3map_surfacelight", QT_TRANSLATE_NOOP("VibeStudioMaterials", "q3map2: how much light the surface gives off.")},
	{MaterialEngine::Quake3, false, "q3map_lightimage", QT_TRANSLATE_NOOP("VibeStudioMaterials", "q3map2: the image whose colour the emitted light takes.")},
	{MaterialEngine::Quake3, false, "q3map_lightsubdivide", QT_TRANSLATE_NOOP("VibeStudioMaterials", "q3map2: the spacing of the light emitters on the surface.")},
	{MaterialEngine::Quake3, false, "q3map_globaltexture", QT_TRANSLATE_NOOP("VibeStudioMaterials", "q3map2: keeps texture coordinates in world space so they line up across surfaces.")},
	{MaterialEngine::Quake3, false, "q3map_backsplash", QT_TRANSLATE_NOOP("VibeStudioMaterials", "q3map2: the share and distance of light cast back onto the surface.")},
	{MaterialEngine::Quake3, false, "q3map_nolightmap", QT_TRANSLATE_NOOP("VibeStudioMaterials", "q3map2: no lightmap for this surface.")},
	{MaterialEngine::Quake3, false, "q3map_novertexshadows", QT_TRANSLATE_NOOP("VibeStudioMaterials", "q3map2: vertex lighting ignores shadows on this surface.")},
	{MaterialEngine::Quake3, false, "q3map_flare", QT_TRANSLATE_NOOP("VibeStudioMaterials", "q3map2: the flare shader drawn at the surface's lights.")},
	{MaterialEngine::Quake3, false, "clamptime", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Stops the shader's time-driven effects after this many seconds.")},
	{MaterialEngine::Quake3, false, "entitymergable", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Lets sprites of entities that share this shader be drawn together.")},
	{MaterialEngine::Quake3, false, "light", QT_TRANSLATE_NOOP("VibeStudioMaterials", "An old light value that the game ignores.")},
	{MaterialEngine::Quake3, false, "nomipmap", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Wolfenstein: another spelling of noMipMaps.")},
	{MaterialEngine::Quake3, false, "skyfogvars", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Wolfenstein: the fog colour and density against the sky.")},
	{MaterialEngine::Quake3, false, "waterfogvars", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Wolfenstein: the fog under water.")},
	{MaterialEngine::Quake3, false, "lightgridmulamb", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Wolfenstein: scales the light grid's ambient light.")},
	{MaterialEngine::Quake3, false, "lightgridmuldir", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Wolfenstein: scales the light grid's directed light.")},
	{MaterialEngine::Quake3, false, "nofog", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Wolfenstein: the surface is not fogged.")},
	{MaterialEngine::Quake3, false, "allowcompress", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Wolfenstein: the images may be compressed.")},
	{MaterialEngine::Quake3, false, "nocompress", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Wolfenstein: the images are never compressed.")},
	{MaterialEngine::Quake3, true, "map16", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Wolfenstein: the image used at 16-bit colour.")},
	{MaterialEngine::Quake3, true, "map32", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Wolfenstein: the image used at 32-bit colour.")},
	{MaterialEngine::Quake3, true, "animmapcomp", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Wolfenstein: the animMap used while texture compression is on.")},
	{MaterialEngine::Quake3, true, "animmapnocomp", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Wolfenstein: the animMap used while texture compression is off.")},
	{MaterialEngine::Quake3, true, "fog", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Wolfenstein: on or off, whether the stage is fogged.")},
	{MaterialEngine::Quake3, false, "distancecull", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Enemy Territory: the distances over which the surface fades out and is culled.")},
	{MaterialEngine::Quake3, false, "implicitmask", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Enemy Territory: like implicitMap, alpha-tested and two-sided.")},
	{MaterialEngine::Quake3, false, "implicitblend", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Enemy Territory: like implicitMap, blended and two-sided.")},
	{MaterialEngine::Quake3, false, "sun", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Jedi Knight: the sky's sun colour, brightness and direction.")},
	{MaterialEngine::Quake3, false, "surfacelight", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Jedi Knight: how much light the surface gives off.")},
	{MaterialEngine::Quake3, false, "lightcolor", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Jedi Knight: the colour of the light the surface gives off.")},
	{MaterialEngine::Quake3, false, "deform", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Jedi Knight: another spelling of deformVertexes.")},
	{MaterialEngine::Quake3, false, "noglfog", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Jedi Knight: no hardware fog on this surface.")},
	{MaterialEngine::Quake3, false, "notc", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Jedi Knight: the surface keeps no texture coordinates.")},
	{MaterialEngine::Quake3, false, "hitlocation", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Jedi Knight: the image that maps where a model is hit.")},
	{MaterialEngine::Quake3, false, "hitmaterial", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Jedi Knight: the image that maps the materials of a model's skin.")},
	{MaterialEngine::Quake3, true, "ssfademax", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Jedi Knight surface sprites: the distance at which they have faded out.")},
	{MaterialEngine::Quake3, true, "ssfadescale", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Jedi Knight surface sprites: how fast they fade with distance.")},
	{MaterialEngine::Quake3, true, "ssvariance", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Jedi Knight surface sprites: random variation of width and height.")},
	{MaterialEngine::Quake3, true, "sshangdown", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Jedi Knight surface sprites: they hang down from the surface.")},
	{MaterialEngine::Quake3, true, "ssanyangle", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Jedi Knight surface sprites: they grow from surfaces at any angle.")},
	{MaterialEngine::Quake3, true, "ssfaceup", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Jedi Knight surface sprites: they lie flat, facing up.")},
	{MaterialEngine::Quake3, true, "sswind", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Jedi Knight surface sprites: how much the wind moves them.")},
	{MaterialEngine::Quake3, true, "sswindidle", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Jedi Knight surface sprites: how much they sway without wind.")},
	{MaterialEngine::Quake3, true, "ssvertskew", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Jedi Knight surface sprites: how far their tops lean.")},
	{MaterialEngine::Quake3, true, "ssfxduration", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Jedi Knight surface sprite effects: how long each lasts.")},
	{MaterialEngine::Quake3, true, "ssfxgrow", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Jedi Knight surface sprite effects: how much each grows while it lasts.")},
	{MaterialEngine::Quake3, true, "ssfxalpharange", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Jedi Knight surface sprite effects: the alpha at start and end.")},
	{MaterialEngine::Quake3, true, "ssfxweather", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Jedi Knight surface sprite effects: they appear as weather.")},
	// Doom 3 keywords the list above leaves out.
	{MaterialEngine::Doom3, false, "description", QT_TRANSLATE_NOOP("VibeStudioMaterials", "A note for the editor; the game ignores it.")},
	{MaterialEngine::Doom3, false, "guisurf", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Draws a GUI on this surface: a .gui file, or entity, entity2, entity3 for the entity's own.")},
	{MaterialEngine::Doom3, false, "spectrum", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Light spectrum number: lights only light surfaces of the same spectrum.")},
	{MaterialEngine::Doom3, false, "decalinfo", QT_TRANSLATE_NOOP("VibeStudioMaterials", "For decals: seconds to stay, seconds to fade, start colour and end colour.")},
	{MaterialEngine::Doom3, false, "renderbump", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Options for the renderbump tool that makes normal maps; the game ignores it.")},
	{MaterialEngine::Doom3, false, "noselfshadow", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Casts shadows on other surfaces but not on itself.")},
	{MaterialEngine::Doom3, false, "forceshadows", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Casts shadows even where the material would not.")},
	{MaterialEngine::Doom3, false, "noportalfog", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Fog does not reach through this area portal.")},
	{MaterialEngine::Doom3, false, "nooverlays", QT_TRANSLATE_NOOP("VibeStudioMaterials", "No decals or other overlays on this surface.")},
	{MaterialEngine::Doom3, false, "forceoverlays", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Overlays even where they would not otherwise go.")},
	{MaterialEngine::Doom3, false, "zeroclamp", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Every image of the material is black outside its edges.")},
	{MaterialEngine::Doom3, false, "clamp", QT_TRANSLATE_NOOP("VibeStudioMaterials", "No image of the material repeats.")},
	{MaterialEngine::Doom3, false, "alphazeroclamp", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Every image of the material is transparent outside its edges.")},
	{MaterialEngine::Doom3, false, "forceopaque", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Treated as opaque whatever its stages do.")},
	{MaterialEngine::Doom3, false, "backsided", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Draws only the back faces.")},
	{MaterialEngine::Doom3, false, "foglight", QT_TRANSLATE_NOOP("VibeStudioMaterials", "For a light: it fogs what it reaches.")},
	{MaterialEngine::Doom3, false, "blendlight", QT_TRANSLATE_NOOP("VibeStudioMaterials", "For a light: its image is blended over what it reaches.")},
	{MaterialEngine::Doom3, false, "ambientlight", QT_TRANSLATE_NOOP("VibeStudioMaterials", "For a light: it lights evenly from all sides, without bump mapping.")},
	{MaterialEngine::Doom3, false, "mirror", QT_TRANSLATE_NOOP("VibeStudioMaterials", "The surface is a mirror.")},
	{MaterialEngine::Doom3, false, "nofog", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Fog lights do not fog this surface.")},
	{MaterialEngine::Doom3, false, "unsmoothedtangents", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Tangents are not smoothed between triangles.")},
	{MaterialEngine::Doom3, false, "suppressinsubview", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Not drawn in mirrors, remote cameras or other subviews.")},
	{MaterialEngine::Doom3, false, "portalsky", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Draws the portal sky's view through the surface.")},
	{MaterialEngine::Doom3, false, "islightgemsurf", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Marks the surface the light gem measures (a Dark Mod extension).")},
	{MaterialEngine::Doom3, true, "remoterendermap", QT_TRANSLATE_NOOP("VibeStudioMaterials", "The view of a remote camera, at the given width and height.")},
	{MaterialEngine::Doom3, true, "mirrorrendermap", QT_TRANSLATE_NOOP("VibeStudioMaterials", "The mirrored view, at the given width and height.")},
	{MaterialEngine::Doom3, true, "xrayrendermap", QT_TRANSLATE_NOOP("VibeStudioMaterials", "The view drawn with x-ray materials, at the given width and height.")},
	{MaterialEngine::Doom3, true, "screen", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Coordinates from the position on screen.")},
	{MaterialEngine::Doom3, true, "screen2", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Coordinates from the position on screen, another mapping.")},
	{MaterialEngine::Doom3, true, "glasswarp", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Screen coordinates bent as if seen through glass.")},
	{MaterialEngine::Doom3, true, "videomap", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Plays a RoQ video as the stage's image; loop repeats it.")},
	{MaterialEngine::Doom3, true, "soundmap", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Draws the sound playing nearby as a wave, or as bars with waveform.")},
	{MaterialEngine::Doom3, true, "ignorealphatest", QT_TRANSLATE_NOOP("VibeStudioMaterials", "The stage draws even where the material's alpha test fails.")},
	{MaterialEngine::Doom3, true, "nearest", QT_TRANSLATE_NOOP("VibeStudioMaterials", "The image is not filtered: hard pixels.")},
	{MaterialEngine::Doom3, true, "linear", QT_TRANSLATE_NOOP("VibeStudioMaterials", "The image is filtered smoothly (the default).")},
	{MaterialEngine::Doom3, true, "noclamp", QT_TRANSLATE_NOOP("VibeStudioMaterials", "The image repeats (the default).")},
	{MaterialEngine::Doom3, true, "alphazeroclamp", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Outside the image is transparent.")},
	{MaterialEngine::Doom3, true, "uncompressed", QT_TRANSLATE_NOOP("VibeStudioMaterials", "The image is never compressed.")},
	{MaterialEngine::Doom3, true, "highquality", QT_TRANSLATE_NOOP("VibeStudioMaterials", "The image is never compressed.")},
	{MaterialEngine::Doom3, true, "forcehighquality", QT_TRANSLATE_NOOP("VibeStudioMaterials", "The image is never compressed, even when image_ignoreHighQuality is set.")},
	{MaterialEngine::Doom3, true, "nopicmip", QT_TRANSLATE_NOOP("VibeStudioMaterials", "The image keeps full resolution whatever image_downSize says.")},
	{MaterialEngine::Doom3, true, "inversevertexcolor", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Multiplies by one minus the vertex colour.")},
	{MaterialEngine::Doom3, true, "privatepolygonoffset", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Pulls only this stage towards the viewer.")},
	{MaterialEngine::Doom3, true, "maskred", QT_TRANSLATE_NOOP("VibeStudioMaterials", "The stage does not write red.")},
	{MaterialEngine::Doom3, true, "maskgreen", QT_TRANSLATE_NOOP("VibeStudioMaterials", "The stage does not write green.")},
	{MaterialEngine::Doom3, true, "maskblue", QT_TRANSLATE_NOOP("VibeStudioMaterials", "The stage does not write blue.")},
	{MaterialEngine::Doom3, true, "maskalpha", QT_TRANSLATE_NOOP("VibeStudioMaterials", "The stage does not write alpha.")},
	{MaterialEngine::Doom3, true, "maskcolor", QT_TRANSLATE_NOOP("VibeStudioMaterials", "The stage writes no colour, only depth or alpha.")},
	{MaterialEngine::Doom3, true, "maskdepth", QT_TRANSLATE_NOOP("VibeStudioMaterials", "The stage does not write depth.")},
	{MaterialEngine::Doom3, true, "red", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Sets red to an expression.")},
	{MaterialEngine::Doom3, true, "green", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Sets green to an expression.")},
	{MaterialEngine::Doom3, true, "blue", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Sets blue to an expression.")},
	{MaterialEngine::Doom3, true, "alpha", QT_TRANSLATE_NOOP("VibeStudioMaterials", "Sets alpha to an expression.")},
	{MaterialEngine::Doom3, true, "program", QT_TRANSLATE_NOOP("VibeStudioMaterials", "An ARB vertex and fragment program for the stage, such as heatHaze.vfp.")},
	{MaterialEngine::Doom3, true, "vertexprogram", QT_TRANSLATE_NOOP("VibeStudioMaterials", "An ARB vertex program for the stage.")},
	{MaterialEngine::Doom3, true, "fragmentprogram", QT_TRANSLATE_NOOP("VibeStudioMaterials", "An ARB fragment program for the stage.")},
	{MaterialEngine::Doom3, true, "vertexparm", QT_TRANSLATE_NOOP("VibeStudioMaterials", "A program input 0-3: one to four expressions separated by commas.")},
	{MaterialEngine::Doom3, true, "fragmentmap", QT_TRANSLATE_NOOP("VibeStudioMaterials", "A program image 0-7, with options such as cubeMap before the image.")},
	{MaterialEngine::Doom3, true, "megatexture", QT_TRANSLATE_NOOP("VibeStudioMaterials", "A megatexture file drawn by its own program.")},
	{MaterialEngine::Doom3, true, "name", QT_TRANSLATE_NOOP("VibeStudioMaterials", "A name for the stage; the game ignores it.")},
};

} // namespace

QString materialKeywordHelp(MaterialEngine engine, const QString& keyword, bool stage)
{
	const QString key = keyword.trimmed().toLower();
	for (const KeywordHelp& entry : kKeywordHelp) {
		if (entry.engine == engine && entry.stage == stage && key == QLatin1String(entry.keyword)) {
			return QCoreApplication::translate("VibeStudioMaterials", entry.help);
		}
	}
	return QString();
}

bool isValidMaterialName(MaterialEngine engine, const QString& name, QString* error)
{
	const auto fail = [&](const QString& message) {
		if (error) {
			*error = message;
		}
		return false;
	};
	if (name.trimmed().isEmpty()) {
		return fail(Text::tr("A material name cannot be empty."));
	}
	const bool doom3 = engine == MaterialEngine::Doom3;
	const QVector<Token> tokens = doom3 ? lexDoom3(name) : lexQuake3(name);
	// One word that covers the whole name; braces and quotes are refused in
	// both engines even where a lexer would keep them inside a word.
	const bool oneWord = tokens.size() == 1 && tokens.first().type == TokenType::Word && tokens.first().start == 0
		&& tokens.first().end == name.size() && !name.contains(QLatin1Char('{')) && !name.contains(QLatin1Char('}'))
		&& !name.contains(QLatin1Char('"'));
	if (!oneWord) {
		return fail(doom3 ? Text::tr("A Doom 3 material name is one word: no spaces, quotes, braces or punctuation such as - + ( ) , and no digit first.")
						  : Text::tr("A Quake III shader name is one word: no spaces, braces or quotes, and no // or /* at the start."));
	}
	if (doom3) {
		const QString key = name.toLower();
		if (key == QStringLiteral("material") || key == QStringLiteral("table") || key == QStringLiteral("guide") || doom3OtherDeclTypes().contains(key)) {
			return fail(Text::tr("'%1' is a Doom 3 decl type; it cannot name a material.").arg(name));
		}
	}
	return true;
}

int checkMaterialTables(MaterialDefinition* definition, const std::function<const MaterialTable*(const QString&)>& findTable)
{
	if (!definition || definition->engine != MaterialEngine::Doom3 || !findTable) {
		return 0;
	}
	// Running again replaces the earlier results, the rejection included.
	for (int index = static_cast<int>(definition->diagnostics.size()) - 1; index >= 0; --index) {
		const MaterialDiagnostic& diagnostic = definition->diagnostics.at(index);
		if (diagnostic.code == QStringLiteral("missing-table") || diagnostic.code == QStringLiteral("stand-in-table")) {
			if (definition->engineRejection == diagnostic.message) {
				definition->engineRejection.clear();
			}
			definition->diagnostics.removeAt(index);
		}
	}
	// With the game's tables.mtr loaded, a name no script defines is a real
	// mistake; without it, it may be one of the game's own tables.
	const MaterialTable* sine = findTable(QStringLiteral("sinTable"));
	const bool stockTables = sine && !sine->generated;
	QSet<QString> reported;
	int missing = 0;
	for (const MaterialExpressionNode& node : std::as_const(definition->expressions)) {
		if (node.op != MaterialExpressionOp::Table) {
			continue;
		}
		const QString key = node.name.toLower();
		if (reported.contains(key)) {
			continue;
		}
		reported.insert(key);
		const MaterialTable* table = findTable(node.name);
		if (table && !table->generated) {
			continue;
		}
		MaterialDiagnostic diagnostic;
		diagnostic.material = definition->name;
		diagnostic.line = node.span.line;
		if (table) {
			diagnostic.severity = MaterialDiagnosticSeverity::Info;
			diagnostic.code = QStringLiteral("stand-in-table");
			diagnostic.message = Text::tr("'%1' is a generated stand-in because the game's tables.mtr is not loaded; the preview's values are close, not exact.")
									 .arg(node.name);
		} else {
			++missing;
			diagnostic.code = QStringLiteral("missing-table");
			if (stockTables) {
				diagnostic.severity = MaterialDiagnosticSeverity::Error;
				diagnostic.message = Text::tr("No loaded script defines the table '%1'; Doom 3 marks the material as defaulted.").arg(node.name);
				if (definition->engineRejection.isEmpty()) {
					definition->engineRejection = diagnostic.message;
				}
			} else {
				diagnostic.severity = MaterialDiagnosticSeverity::Warning;
				diagnostic.message =
					Text::tr("No loaded script defines the table '%1'; Doom 3 defaults the material unless the game's own tables define it.").arg(node.name);
			}
		}
		definition->diagnostics.push_back(diagnostic);
	}
	return missing;
}

QString materialEditKindId(MaterialEditKind kind)
{
	switch (kind) {
	case MaterialEditKind::SetDirective:
		return QStringLiteral("set");
	case MaterialEditKind::AddDirective:
		return QStringLiteral("add");
	case MaterialEditKind::RemoveDirective:
		return QStringLiteral("remove");
	case MaterialEditKind::AddStage:
		return QStringLiteral("add-stage");
	case MaterialEditKind::RemoveStage:
		return QStringLiteral("remove-stage");
	case MaterialEditKind::MoveStage:
		return QStringLiteral("move-stage");
	case MaterialEditKind::ReplaceDefinition:
		return QStringLiteral("replace-definition");
	case MaterialEditKind::AddDefinition:
		return QStringLiteral("add-definition");
	case MaterialEditKind::RemoveDefinition:
		return QStringLiteral("remove-definition");
	case MaterialEditKind::RenameDefinition:
		return QStringLiteral("rename");
	case MaterialEditKind::MoveDirective:
		return QStringLiteral("move");
	case MaterialEditKind::DuplicateDefinition:
		return QStringLiteral("duplicate");
	}
	return QStringLiteral("set");
}

bool materialEditKindFromId(const QString& id, MaterialEditKind* kind)
{
	static const QHash<QString, MaterialEditKind> kinds {
		{QStringLiteral("set"), MaterialEditKind::SetDirective},
		{QStringLiteral("add"), MaterialEditKind::AddDirective},
		{QStringLiteral("remove"), MaterialEditKind::RemoveDirective},
		{QStringLiteral("add-stage"), MaterialEditKind::AddStage},
		{QStringLiteral("remove-stage"), MaterialEditKind::RemoveStage},
		{QStringLiteral("move-stage"), MaterialEditKind::MoveStage},
		{QStringLiteral("replace-definition"), MaterialEditKind::ReplaceDefinition},
		{QStringLiteral("add-definition"), MaterialEditKind::AddDefinition},
		{QStringLiteral("remove-definition"), MaterialEditKind::RemoveDefinition},
		{QStringLiteral("rename"), MaterialEditKind::RenameDefinition},
		{QStringLiteral("move"), MaterialEditKind::MoveDirective},
		{QStringLiteral("duplicate"), MaterialEditKind::DuplicateDefinition},
	};
	const auto found = kinds.constFind(id.trimmed().toLower());
	if (found == kinds.constEnd()) {
		return false;
	}
	*kind = *found;
	return true;
}

MaterialEditResult applyMaterialEdit(const MaterialScript& script, const MaterialEdit& edit)
{
	const QString& text = script.text;
	const QString newline = newlineFor(text);
	if (edit.kind == MaterialEditKind::AddDefinition) {
		const QString body = edit.text.trimmed();
		if (body.isEmpty()) {
			return failure(Text::tr("The new definition is empty."));
		}
		QString prefix;
		if (!text.isEmpty() && !text.endsWith(QLatin1Char('\n'))) {
			prefix = newline;
		}
		if (!text.trimmed().isEmpty()) {
			prefix += newline;
		}
		QString addition = body;
		addition.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
		if (newline != QStringLiteral("\n")) {
			addition.replace(QStringLiteral("\n"), newline);
		}
		const int start = text.size();
		const QString updated = text + prefix + addition + newline;
		return finish(script, updated, start, updated.size());
	}
	const int materialIndex = script.indexOf(edit.material);
	if (materialIndex < 0) {
		return failure(Text::tr("No material named '%1' in this script.").arg(edit.material));
	}
	const MaterialDefinition& definition = script.materials.at(materialIndex);
	if (!definition.span.isValid()) {
		return failure(Text::tr("'%1' has no editable text.").arg(definition.name));
	}
	QString error;
	switch (edit.kind) {
	case MaterialEditKind::RenameDefinition:
	case MaterialEditKind::DuplicateDefinition: {
		const QString name = edit.text.trimmed();
		QString nameError;
		if (!isValidMaterialName(script.engine, name, &nameError)) {
			return failure(nameError);
		}
		if (materialLookupKey(name) != materialLookupKey(definition.name) && script.find(name)) {
			return failure(Text::tr("This script already defines %1.").arg(name));
		}
		// The new name must read back as the definition it names.
		const auto checked = [&](MaterialEditResult result) {
			if (result.ok && !result.script.find(name)) {
				return failure(Text::tr("'%1' does not read back as a material name.").arg(name));
			}
			return result;
		};
		if (edit.kind == MaterialEditKind::RenameDefinition) {
			const Patch patch {definition.nameSpan.start, definition.nameSpan.end, name};
			return checked(finish(script, applyPatch(text, patch), patch.start, patch.start + name.size()));
		}
		if (materialLookupKey(name) == materialLookupKey(definition.name)) {
			return failure(Text::tr("A copy needs a name of its own."));
		}
		// The copy keeps the original's text and layout, renamed, on the
		// lines after it.
		QString copy = text.mid(definition.span.start, definition.span.end - definition.span.start);
		copy.replace(definition.nameSpan.start - definition.span.start, definition.nameSpan.end - definition.nameSpan.start, name);
		const QString insertion = newline + newline + copy;
		const Patch patch {definition.span.end, definition.span.end, insertion};
		const int start = definition.span.end + static_cast<int>(newline.size()) * 2;
		return checked(finish(script, applyPatch(text, patch), start, start + static_cast<int>(copy.size())));
	}
	case MaterialEditKind::ReplaceDefinition: {
		QString replacement = edit.text.trimmed();
		replacement.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
		if (newline != QStringLiteral("\n")) {
			replacement.replace(QStringLiteral("\n"), newline);
		}
		const Patch patch {definition.span.start, definition.span.end, replacement};
		return finish(script, applyPatch(text, patch), patch.start, patch.start + replacement.size());
	}
	case MaterialEditKind::RemoveDefinition: {
		const auto [start, end] = removalRange(text, definition.span.start, definition.span.end);
		const Patch patch {start, end, QString()};
		return finish(script, applyPatch(text, patch), start, start);
	}
	case MaterialEditKind::SetDirective:
	case MaterialEditKind::AddDirective:
	case MaterialEditKind::RemoveDirective:
	case MaterialEditKind::MoveDirective: {
		Block block;
		if (!blockFor(definition, edit.stage, &block, &error)) {
			return failure(error.isEmpty() ? Text::tr("That block has no editable text.") : error);
		}
		const QVector<MaterialDirective>& directives = *block.directives;
		if (edit.kind == MaterialEditKind::AddDirective) {
			if (edit.keyword.trimmed().isEmpty()) {
				return failure(Text::tr("A directive needs a keyword."));
			}
			const Patch patch = insertionPatch(text, block, edit.position, directiveText(edit.keyword.trimmed(), edit.arguments));
			return finish(script, applyPatch(text, patch), patch.start, patch.start + patch.replacement.size());
		}
		const int index = findDirective(directives, edit);
		if (edit.kind == MaterialEditKind::SetDirective) {
			if (index < 0) {
				if (edit.keyword.trimmed().isEmpty()) {
					return failure(Text::tr("Directive %1 does not exist.").arg(edit.directive + 1));
				}
				const Patch patch = insertionPatch(text, block, -1, directiveText(edit.keyword.trimmed(), edit.arguments));
				return finish(script, applyPatch(text, patch), patch.start, patch.start + patch.replacement.size());
			}
			const MaterialDirective& directive = directives.at(index);
			const QString keyword = edit.keyword.trimmed().isEmpty() ? directive.keyword : edit.keyword.trimmed();
			const QString replacement = directiveText(keyword, edit.arguments);
			const Patch patch {directive.span.start, directive.span.end, replacement};
			return finish(script, applyPatch(text, patch), patch.start, patch.start + replacement.size());
		}
		if (index < 0) {
			return failure(Text::tr("No directive '%1' to change.").arg(edit.keyword.isEmpty() ? QString::number(edit.directive + 1) : edit.keyword));
		}
		const MaterialDirective& directive = directives.at(index);
		const auto [start, end] = removalRange(text, directive.span.start, directive.span.end);
		if (edit.kind == MaterialEditKind::RemoveDirective) {
			const Patch patch {start, end, QString()};
			return finish(script, applyPatch(text, patch), start, start);
		}
		// MoveDirective: remove, then insert before the target in the
		// re-parsed text.
		const QString line = text.mid(directive.span.start, directive.span.end - directive.span.start);
		int target = std::clamp(edit.position, 0, static_cast<int>(directives.size()) - 1);
		if (target == index) {
			return finish(script, text, directive.span.start, directive.span.end);
		}
		const QString removed = applyPatch(text, {start, end, QString()});
		MaterialScript intermediate = parseMaterialScript(removed, script.engine, script.path, script.dialect);
		const int intermediateIndex = intermediate.indexOf(definition.name);
		if (intermediateIndex < 0) {
			return failure(Text::tr("The directive could not be moved."));
		}
		Block moved;
		if (!blockFor(intermediate.materials.at(intermediateIndex), edit.stage, &moved, &error)) {
			return failure(error);
		}
		const Patch patch = insertionPatch(removed, moved, target < moved.directives->size() ? target : -1, line);
		return finish(script, applyPatch(removed, patch), patch.start, patch.start + patch.replacement.size());
	}
	case MaterialEditKind::AddStage: {
		if (!definition.bodySpan.isValid()) {
			return failure(Text::tr("'%1' has no body to add a stage to.").arg(definition.name));
		}
		const QString indent = stageIndent(text, definition);
		const QString block = stageBlockText(text, indent, edit.text);
		int insertAt = -1;
		QVector<const MaterialStage*> blocks;
		for (const MaterialStage& stage : definition.stages) {
			blocks << &stage;
		}
		if (edit.position >= 0 && edit.position < blocks.size()) {
			const MaterialStage& before = *blocks.at(edit.position);
			if (!before.span.isValid()) {
				return failure(implicitStageError(edit.position));
			}
			insertAt = lineStartOf(text, before.span.start);
			if (!onlyWhitespace(text, insertAt, before.span.start)) {
				insertAt = before.span.start;
				const Patch patch {insertAt, insertAt, block.trimmed() + newline + indentationOf(text, before.span.start)};
				return finish(script, applyPatch(text, patch), patch.start, patch.start + patch.replacement.size());
			}
		} else {
			const int close = definition.bodySpan.end - 1;
			insertAt = lineStartOf(text, close);
			if (!onlyWhitespace(text, insertAt, close)) {
				const Patch patch {close, close, newline + block + indentationOf(text, definition.bodySpan.start)};
				return finish(script, applyPatch(text, patch), patch.start, patch.start + patch.replacement.size());
			}
		}
		const Patch patch {insertAt, insertAt, block};
		return finish(script, applyPatch(text, patch), patch.start, patch.start + block.size());
	}
	case MaterialEditKind::RemoveStage: {
		if (edit.stage < 0 || edit.stage >= definition.stages.size()) {
			return failure(Text::tr("Stage %1 does not exist.").arg(edit.stage + 1));
		}
		const MaterialStage& stage = definition.stages.at(edit.stage);
		if (!stage.span.isValid()) {
			return failure(implicitStageError(edit.stage));
		}
		if (stage.shorthandDirective >= 0) {
			const MaterialDirective& directive = definition.directives.at(stage.shorthandDirective);
			const auto [start, end] = removalRange(text, directive.span.start, directive.span.end);
			return finish(script, applyPatch(text, {start, end, QString()}), start, start);
		}
		const auto [start, end] = stageLines(text, stage);
		return finish(script, applyPatch(text, {start, end, QString()}), start, start);
	}
	case MaterialEditKind::MoveStage: {
		if (edit.stage < 0 || edit.stage >= definition.stages.size()) {
			return failure(Text::tr("Stage %1 does not exist.").arg(edit.stage + 1));
		}
		const MaterialStage& stage = definition.stages.at(edit.stage);
		if (!stage.span.isValid()) {
			return failure(implicitStageError(edit.stage));
		}
		if (stage.shorthandDirective >= 0) {
			return failure(Text::tr("Stage %1 is a shorthand directive; it has no block to move.").arg(edit.stage + 1));
		}
		const int target = std::clamp(edit.position, 0, static_cast<int>(definition.stages.size()) - 1);
		if (target == edit.stage) {
			return finish(script, text, stage.span.start, stage.span.end);
		}
		for (const MaterialStage& other : definition.stages) {
			if (!other.span.isValid()) {
				return failure(implicitStageError(other.index));
			}
		}
		const auto [start, end] = stageLines(text, stage);
		const QString removed = applyPatch(text, {start, end, QString()});
		MaterialScript intermediate = parseMaterialScript(removed, script.engine, script.path, script.dialect);
		const int intermediateIndex = intermediate.indexOf(definition.name);
		// The block moves as written, comments and layout included, when it
		// has its lines to itself and the target does too.
		const bool ownLines = onlyWhitespace(text, lineStartOf(text, stage.span.start), stage.span.start)
			&& whitespaceOrComment(text, stage.span.end, lineEndOf(text, stage.span.end));
		if (intermediateIndex >= 0 && ownLines) {
			const MaterialDefinition& moved = intermediate.materials.at(intermediateIndex);
			int insertAt = -1;
			QString indent;
			if (target < moved.stages.size()) {
				const MaterialStage& before = moved.stages.at(target);
				const int lineStart = lineStartOf(removed, before.span.start);
				if (before.span.isValid() && onlyWhitespace(removed, lineStart, before.span.start)) {
					insertAt = lineStart;
					indent = indentationOf(removed, before.span.start);
				}
			} else if (moved.bodySpan.isValid()) {
				const int close = moved.bodySpan.end - 1;
				const int lineStart = lineStartOf(removed, close);
				if (onlyWhitespace(removed, lineStart, close)) {
					insertAt = lineStart;
					indent = stageIndent(removed, moved);
				}
			}
			if (insertAt >= 0) {
				QString raw = text.mid(start, end - start);
				if (!raw.endsWith(QLatin1Char('\n'))) {
					raw += newline;
				}
				const QString sourceIndent = indentationOf(text, stage.span.start);
				if (sourceIndent != indent) {
					// Swap the leading indentation, line by line.
					QStringList lines = raw.split(QLatin1Char('\n'));
					for (QString& line : lines) {
						if (line.startsWith(sourceIndent) && !line.trimmed().isEmpty()) {
							line = indent + line.mid(sourceIndent.size());
						}
					}
					raw = lines.join(QLatin1Char('\n'));
				}
				return finish(script, applyPatch(removed, {insertAt, insertAt, raw}), insertAt, insertAt + static_cast<int>(raw.size()));
			}
		}
		// The block shares a line with other text: re-insert its directives
		// as a fresh block.
		QString body;
		for (const MaterialDirective& directive : stage.directives) {
			body += text.mid(directive.span.start, directive.span.end - directive.span.start) + QLatin1Char('\n');
		}
		MaterialEdit insert;
		insert.kind = MaterialEditKind::AddStage;
		insert.material = definition.name;
		insert.text = body;
		insert.position = target;
		return applyMaterialEdit(intermediate, insert);
	}
	case MaterialEditKind::AddDefinition:
		break;
	}
	return failure(Text::tr("Unsupported edit."));
}

MaterialEditResult applyMaterialEdits(const MaterialScript& script, const QVector<MaterialEdit>& edits)
{
	MaterialEditResult result;
	result.ok = true;
	result.text = script.text;
	result.script = script;
	for (int index = 0; index < edits.size(); ++index) {
		MaterialEditResult step = applyMaterialEdit(result.script, edits.at(index));
		if (!step.ok) {
			step.error = Text::tr("Edit %1: %2").arg(index + 1).arg(step.error);
			return step;
		}
		result = step;
	}
	return result;
}

bool parseMaterialEditsJson(const QByteArray& json, const QString& defaultMaterial, QVector<MaterialEdit>* edits, QString* error)
{
	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
	if (parseError.error != QJsonParseError::NoError) {
		*error = Text::tr("Edits must be a JSON array: %1").arg(parseError.errorString());
		return false;
	}
	if (!document.isArray()) {
		*error = Text::tr("Edits must be a JSON array of edits; the file holds a JSON object.");
		return false;
	}
	const QJsonArray array = document.array();
	if (array.size() > 256) {
		*error = Text::tr("At most 256 edits are accepted at once.");
		return false;
	}
	for (int index = 0; index < array.size(); ++index) {
		if (!array.at(index).isObject()) {
			*error = Text::tr("Edit %1 is not an object.").arg(index + 1);
			return false;
		}
		const QJsonObject object = array.at(index).toObject();
		static const QSet<QString> known {QStringLiteral("op"), QStringLiteral("material"), QStringLiteral("stage"), QStringLiteral("directive"),
			QStringLiteral("keyword"), QStringLiteral("arguments"), QStringLiteral("position"), QStringLiteral("text")};
		for (const QString& key : object.keys()) {
			if (!known.contains(key)) {
				*error = Text::tr("Edit %1 has an unknown field '%2'.").arg(index + 1).arg(key);
				return false;
			}
		}
		// Text fields must be JSON strings when given: an array or a number
		// would otherwise read as empty text and edit the wrong thing.
		for (const QString& key : {QStringLiteral("op"), QStringLiteral("material"), QStringLiteral("keyword"), QStringLiteral("arguments"), QStringLiteral("text")}) {
			const QJsonValue value = object.value(key);
			if (!value.isUndefined() && !value.isString()) {
				*error = Text::tr("Edit %1: '%2' must be text.").arg(index + 1).arg(key);
				return false;
			}
		}
		MaterialEdit edit;
		if (!materialEditKindFromId(object.value(QStringLiteral("op")).toString(), &edit.kind)) {
			*error = Text::tr("Edit %1 has an unknown op '%2'.").arg(index + 1).arg(object.value(QStringLiteral("op")).toString());
			return false;
		}
		edit.material = object.value(QStringLiteral("material")).toString(defaultMaterial);
		// Stage, directive and position numbers are 1-based in JSON, like the
		// UI. Leaving one out means "none": the global block, the directive
		// named by its keyword, or the end of the block.
		const auto counted = [&](const QString& key, int* out) {
			const QJsonValue value = object.value(key);
			if (value.isUndefined()) {
				*out = -1;
				return true;
			}
			const double number = value.toDouble();
			if (!value.isDouble() || number != std::floor(number) || std::abs(number) > double(std::numeric_limits<int>::max())) {
				*error = Text::tr("Edit %1: '%2' must be an integer.").arg(index + 1).arg(key);
				return false;
			}
			if (number < 1) {
				if (key == QStringLiteral("stage")) {
					*error = Text::tr("Edit %1: '%2' counts from 1; leave it out for the material's global block.").arg(index + 1).arg(key);
				} else if (key == QStringLiteral("position")) {
					*error = Text::tr("Edit %1: '%2' counts from 1; leave it out for the end.").arg(index + 1).arg(key);
				} else {
					*error = Text::tr("Edit %1: '%2' counts from 1; leave it out to find the directive by its keyword.").arg(index + 1).arg(key);
				}
				return false;
			}
			*out = int(number) - 1;
			return true;
		};
		if (!counted(QStringLiteral("stage"), &edit.stage) || !counted(QStringLiteral("directive"), &edit.directive)
			|| !counted(QStringLiteral("position"), &edit.position)) {
			return false;
		}
		edit.keyword = object.value(QStringLiteral("keyword")).toString();
		edit.arguments = object.value(QStringLiteral("arguments")).toString();
		edit.text = object.value(QStringLiteral("text")).toString();
		edits->push_back(edit);
	}
	return true;
}

QString materialSurfaceContextId(MaterialSurfaceContext context)
{
	switch (context) {
	case MaterialSurfaceContext::World:
		return QStringLiteral("world");
	case MaterialSurfaceContext::Model:
		return QStringLiteral("model");
	case MaterialSurfaceContext::TwoD:
		return QStringLiteral("2d");
	}
	return QStringLiteral("world");
}

MaterialDefinition implicitQuake3Material(const QString& name, MaterialSurfaceContext context, const QString& image)
{
	MaterialDefinition definition;
	definition.name = name;
	definition.engine = MaterialEngine::Quake3;
	definition.kind = QStringLiteral("implicit");
	QString path = image.trimmed();
	if (path.isEmpty()) {
		path = name;
		if (QFileInfo(path).suffix().isEmpty()) {
			path += QStringLiteral(".tga");
		}
	}
	MaterialStage texture;
	texture.imageKind = MaterialImageKind::File;
	texture.imagePath = path;
	texture.rgbGen.explicitlySet = true;
	texture.alphaGen.source = MaterialColorSource::Identity;
	if (context == MaterialSurfaceContext::World) {
		// Two passes: the lightmap (scaled for identity light when loaded),
		// then the image multiplied in.
		MaterialStage lightmap;
		lightmap.index = 0;
		lightmap.imageKind = MaterialImageKind::Lightmap;
		lightmap.tcGen.source = MaterialTexCoordSource::Lightmap;
		lightmap.rgbGen.source = MaterialColorSource::Identity;
		lightmap.rgbGen.explicitlySet = true;
		definition.stages.push_back(lightmap);
		texture.index = 1;
		texture.blend.source = MaterialBlendFactor::DestinationColor;
		texture.blend.destination = MaterialBlendFactor::Zero;
		texture.blend.explicitBlend = true;
		texture.depthWrite = false;
		texture.rgbGen.source = MaterialColorSource::Identity;
	} else if (context == MaterialSurfaceContext::Model) {
		texture.rgbGen.source = MaterialColorSource::LightingDiffuse;
	} else {
		texture.rgbGen.source = MaterialColorSource::Identity;
		texture.blend.source = MaterialBlendFactor::SourceAlpha;
		texture.blend.destination = MaterialBlendFactor::OneMinusSourceAlpha;
		texture.blend.explicitBlend = true;
	}
	definition.stages.push_back(texture);
	definition.sort = QStringLiteral("opaque");
	definition.sortValue = 3;
	return definition;
}

QString implicitDoom3MaterialText(const QString& name)
{
	// MakeNameCanonical: lower case, forward slashes, no extension.
	QString canonical = name.trimmed().toLower();
	canonical.replace(QLatin1Char('\\'), QLatin1Char('/'));
	const int dot = canonical.lastIndexOf(QLatin1Char('.'));
	if (dot > canonical.lastIndexOf(QLatin1Char('/'))) {
		canonical.truncate(dot);
	}
	return QStringLiteral("material %1 // IMPLICITLY GENERATED\n{\n\t{\n\t\tblend blend\n\t\tcolored\n\t\tmap \"%1\"\n\t\tclamp\n\t}\n}\n").arg(canonical);
}

MaterialDefinition implicitDoom3Material(const QString& name)
{
	MaterialScript script = parseMaterialScript(implicitDoom3MaterialText(name), MaterialEngine::Doom3);
	MaterialDefinition definition = script.materials.isEmpty() ? MaterialDefinition() : script.materials.first();
	definition.name = name;
	definition.engine = MaterialEngine::Doom3;
	definition.kind = QStringLiteral("implicit");
	definition.span = {};
	definition.bodySpan = {};
	definition.nameSpan = {};
	definition.sourcePath.clear();
	return definition;
}

QVector<MaterialTemplate> materialTemplates(MaterialEngine engine)
{
	// Names and descriptions are translated on every call, so a language
	// chosen after the first call shows too.
	struct Entry {
		const char* id;
		MaterialEngine engine;
		const char* name;
		const char* description;
		const char* body;
	};
	static const QVector<Entry> entries = [] {
		QVector<Entry> list;
		const auto add = [&](const char* id, MaterialEngine engine, const char* name, const char* description, const char* body) {
			list.push_back({id, engine, name, description, body});
		};
		add("q3-lightmapped", MaterialEngine::Quake3, QT_TRANSLATE_NOOP("VibeStudioMaterials", "Lightmapped wall"),
			QT_TRANSLATE_NOOP("VibeStudioMaterials", "The standard two-pass world surface: lightmap, then the image multiplied in."),
			"%1\n{\n\tqer_editorimage %2\n\t{\n\t\tmap $lightmap\n\t\trgbGen identity\n\t}\n\t{\n\t\tmap %2\n\t\tblendFunc filter\n\t\trgbGen identity\n\t}\n}\n");
		add("q3-glow", MaterialEngine::Quake3, QT_TRANSLATE_NOOP("VibeStudioMaterials", "Pulsing glow"),
			QT_TRANSLATE_NOOP("VibeStudioMaterials", "A lit wall with an added light pass whose brightness follows a sine wave."),
			"%1\n{\n\tqer_editorimage %2\n\tq3map_surfacelight 500\n\t{\n\t\tmap $lightmap\n\t\trgbGen identity\n\t}\n\t{\n\t\tmap %2\n\t\tblendFunc filter\n\t\trgbGen identity\n\t}\n\t{\n\t\tmap %2\n\t\tblendFunc add\n\t\trgbGen wave sin 0.5 0.5 0 0.5\n\t}\n}\n");
		add("q3-liquid", MaterialEngine::Quake3, QT_TRANSLATE_NOOP("VibeStudioMaterials", "Rippling liquid"),
			QT_TRANSLATE_NOOP("VibeStudioMaterials", "Translucent water with turbulent, scrolling coordinates and gently moving vertices."),
			"%1\n{\n\tqer_editorimage %2\n\tqer_trans 0.5\n\tsurfaceparm trans\n\tsurfaceparm nonsolid\n\tsurfaceparm water\n\tcull disable\n\tdeformVertexes wave 64 sin 0 2 0 0.5\n\ttessSize 64\n\t{\n\t\tmap %2\n\t\tblendFunc blend\n\t\talphaGen const 0.6\n\t\ttcMod turb 0 0.1 0 0.2\n\t\ttcMod scroll 0.02 0.01\n\t}\n}\n");
		add("q3-sky", MaterialEngine::Quake3, QT_TRANSLATE_NOOP("VibeStudioMaterials", "Sky with clouds"),
			QT_TRANSLATE_NOOP("VibeStudioMaterials", "A sky box with two scrolling cloud layers."),
			"%1\n{\n\tqer_editorimage %2\n\tsurfaceparm sky\n\tsurfaceparm noimpact\n\tsurfaceparm nolightmap\n\tskyParms - 512 -\n\t{\n\t\tmap %2\n\t\ttcMod scale 3 2\n\t\ttcMod scroll 0.015 0.016\n\t\tdepthWrite\n\t}\n\t{\n\t\tmap %2\n\t\tblendFunc add\n\t\ttcMod scale 3 3\n\t\ttcMod scroll 0.005 0.003\n\t}\n}\n");
		add("q3-grate", MaterialEngine::Quake3, QT_TRANSLATE_NOOP("VibeStudioMaterials", "Alpha-tested grate"),
			QT_TRANSLATE_NOOP("VibeStudioMaterials", "A see-through grate: alpha test writes depth, then the lightmap draws only where it passed."),
			"%1\n{\n\tqer_editorimage %2\n\tsurfaceparm alphashadow\n\tsurfaceparm trans\n\tcull none\n\tnopicmip\n\t{\n\t\tmap %2\n\t\talphaFunc GE128\n\t\tdepthWrite\n\t\trgbGen identity\n\t}\n\t{\n\t\tmap $lightmap\n\t\trgbGen identity\n\t\tblendFunc filter\n\t\tdepthFunc equal\n\t}\n}\n");
		add("q3-envmap", MaterialEngine::Quake3, QT_TRANSLATE_NOOP("VibeStudioMaterials", "Shiny metal"),
			QT_TRANSLATE_NOOP("VibeStudioMaterials", "A lit surface with an environment-mapped reflection added."),
			"%1\n{\n\tqer_editorimage %2\n\t{\n\t\tmap $lightmap\n\t\trgbGen identity\n\t}\n\t{\n\t\tmap %2\n\t\tblendFunc filter\n\t\trgbGen identity\n\t}\n\t{\n\t\tmap %2\n\t\ttcGen environment\n\t\tblendFunc add\n\t\trgbGen const ( 0.25 0.25 0.25 )\n\t}\n}\n");
		add("q3-animmap", MaterialEngine::Quake3, QT_TRANSLATE_NOOP("VibeStudioMaterials", "Animated frames"),
			QT_TRANSLATE_NOOP("VibeStudioMaterials", "Cycles images at a fixed rate, such as fire or a screen."),
			"%1\n{\n\tqer_editorimage %2\n\tsurfaceparm nolightmap\n\t{\n\t\tanimMap 8 %2 %2 %2 %2\n\t\trgbGen identity\n\t}\n}\n");
		add("q3-flicker", MaterialEngine::Quake3, QT_TRANSLATE_NOOP("VibeStudioMaterials", "Flickering light"),
			QT_TRANSLATE_NOOP("VibeStudioMaterials", "A light panel that flickers with a noise wave."),
			"%1\n{\n\tqer_editorimage %2\n\tq3map_surfacelight 1000\n\tsurfaceparm nolightmap\n\t{\n\t\tmap %2\n\t\trgbGen wave noise 0.75 0.25 0 6\n\t}\n}\n");
		add("q3-fog", MaterialEngine::Quake3, QT_TRANSLATE_NOOP("VibeStudioMaterials", "Fog volume"),
			QT_TRANSLATE_NOOP("VibeStudioMaterials", "A fog brush that reaches full opacity at the given distance."),
			"%1\n{\n\tqer_editorimage %2\n\tqer_nocarve\n\tsurfaceparm trans\n\tsurfaceparm nonsolid\n\tsurfaceparm fog\n\tsurfaceparm nolightmap\n\tfogparms ( 0.4 0.45 0.5 ) 512\n}\n");
		add("q3-decal", MaterialEngine::Quake3, QT_TRANSLATE_NOOP("VibeStudioMaterials", "Decal"),
			QT_TRANSLATE_NOOP("VibeStudioMaterials", "A stain or marking drawn on top of walls without flicker."),
			"%1\n{\n\tqer_editorimage %2\n\tsurfaceparm nonsolid\n\tsurfaceparm nomarks\n\tsurfaceparm trans\n\tpolygonOffset\n\t{\n\t\tmap %2\n\t\tblendFunc filter\n\t\trgbGen identity\n\t}\n}\n");
		add("d3-standard", MaterialEngine::Doom3, QT_TRANSLATE_NOOP("VibeStudioMaterials", "Lit surface"),
			QT_TRANSLATE_NOOP("VibeStudioMaterials", "Diffuse, normal and specular maps lit by the scene's lights."),
			"%1\n{\n\tqer_editorimage %2\n\tdiffusemap %2\n\tbumpmap addnormals( %2_local, heightmap( %2_h, 4 ) )\n\tspecularmap %2_s\n}\n");
		add("d3-glow", MaterialEngine::Doom3, QT_TRANSLATE_NOOP("VibeStudioMaterials", "Glowing panel"),
			QT_TRANSLATE_NOOP("VibeStudioMaterials", "A lit surface with an added glow that pulses using sinTable."),
			"%1\n{\n\tqer_editorimage %2\n\tdiffusemap %2\n\tbumpmap %2_local\n\t{\n\t\tblend add\n\t\tmap %2_add\n\t\trgb 0.75 + 0.25 * sinTable[ time * 0.5 ]\n\t}\n}\n");
		add("d3-glass", MaterialEngine::Doom3, QT_TRANSLATE_NOOP("VibeStudioMaterials", "Reflective glass"),
			QT_TRANSLATE_NOOP("VibeStudioMaterials", "Translucent glass reflecting an environment cube map."),
			"%1\n{\n\tqer_editorimage %2\n\ttranslucent\n\tnoShadows\n\tglass\n\t{\n\t\tblend filter\n\t\tmap %2\n\t}\n\t{\n\t\tblend add\n\t\tcubeMap env/gen1\n\t\ttexgen reflect\n\t\trgb 0.2\n\t}\n}\n");
		add("d3-scroll", MaterialEngine::Doom3, QT_TRANSLATE_NOOP("VibeStudioMaterials", "Scrolling screen"),
			QT_TRANSLATE_NOOP("VibeStudioMaterials", "An unlit screen whose image scrolls over time."),
			"%1\n{\n\tqer_editorimage %2\n\tnoShadows\n\t{\n\t\tblend add\n\t\tmap %2\n\t\tscroll time * 0.1, 0\n\t}\n}\n");
		add("d3-light", MaterialEngine::Doom3, QT_TRANSLATE_NOOP("VibeStudioMaterials", "Light"),
			QT_TRANSLATE_NOOP("VibeStudioMaterials", "A light shader: a projected image faded by a falloff image, tinted by the light's colour."),
			"%1\n{\n\tlightFalloffImage makeintensity( lights/squarelight1a.tga )\n\t{\n\t\tmap %2\n\t\tcolored\n\t\tzeroClamp\n\t}\n}\n");
		add("d3-flicker-light", MaterialEngine::Doom3, QT_TRANSLATE_NOOP("VibeStudioMaterials", "Flickering light"),
			QT_TRANSLATE_NOOP("VibeStudioMaterials", "A light whose brightness follows a table that you can edit."),
			"%1\n{\n\tlightFalloffImage makeintensity( lights/squarelight1a.tga )\n\t{\n\t\tmap %2\n\t\tzeroClamp\n\t\tred parm0 * flickertable[ time * 3 ]\n\t\tgreen parm1 * flickertable[ time * 3 ]\n\t\tblue parm2 * flickertable[ time * 3 ]\n\t}\n}\n");
		add("d3-sky", MaterialEngine::Doom3, QT_TRANSLATE_NOOP("VibeStudioMaterials", "Sky box"),
			QT_TRANSLATE_NOOP("VibeStudioMaterials", "An unlit sky drawn from a camera cube map behind everything."),
			"%1\n{\n\tqer_editorimage %2\n\tnoShadows\n\tnoFragment\n\tnonsolid\n\tnoimpact\n\tforceOpaque\n\t{\n\t\tblend add\n\t\tcameraCubeMap %2\n\t\ttexgen skybox\n\t\ttexgen wobblesky 0 0 0\n\t}\n}\n");
		add("d3-decal", MaterialEngine::Doom3, QT_TRANSLATE_NOOP("VibeStudioMaterials", "Decal"),
			QT_TRANSLATE_NOOP("VibeStudioMaterials", "A marking drawn over walls, fading after a while."),
			"%1\n{\n\tqer_editorimage %2\n\tDECAL_MACRO\n\tdecalInfo 10 2 ( 1 1 1 1 ) ( 0 0 0 0 )\n\t{\n\t\tblend filter\n\t\tmap %2\n\t\tvertexColor\n\t}\n}\n");
		return list;
	}();
	QVector<MaterialTemplate> all;
	all.reserve(entries.size());
	for (const Entry& entry : entries) {
		all.push_back({QString::fromLatin1(entry.id), entry.engine, QCoreApplication::translate("VibeStudioMaterials", entry.name),
			QCoreApplication::translate("VibeStudioMaterials", entry.description), QString::fromLatin1(entry.body)});
	}
	if (engine == MaterialEngine::Unknown) {
		return all;
	}
	QVector<MaterialTemplate> filtered;
	for (const MaterialTemplate& entry : all) {
		if (entry.engine == engine) {
			filtered.push_back(entry);
		}
	}
	return filtered;
}

bool materialTemplateById(const QString& id, MaterialTemplate* out)
{
	for (const MaterialTemplate& entry : materialTemplates()) {
		if (entry.id.compare(id.trimmed(), Qt::CaseInsensitive) == 0) {
			if (out) {
				*out = entry;
			}
			return true;
		}
	}
	return false;
}

QString instantiateMaterialTemplate(const MaterialTemplate& materialTemplate, const QString& name, const QString& image)
{
	QString body = materialTemplate.body;
	QString imagePath = image.trimmed();
	if (imagePath.isEmpty()) {
		imagePath = name;
	}
	if (materialTemplate.engine == MaterialEngine::Doom3) {
		// Doom 3 images drop the extension.
		const QString suffix = QFileInfo(imagePath).suffix().toLower();
		if (suffix == QStringLiteral("tga") || suffix == QStringLiteral("jpg") || suffix == QStringLiteral("png")) {
			imagePath.chop(suffix.size() + 1);
		}
	} else if (materialTemplate.engine == MaterialEngine::Quake3 && QFileInfo(imagePath).suffix().isEmpty()) {
		imagePath += QStringLiteral(".tga");
	}
	body.replace(QStringLiteral("%1"), name.trimmed());
	body.replace(QStringLiteral("%2"), imagePath);
	if (materialTemplate.id == QStringLiteral("d3-flicker-light")) {
		body = QStringLiteral("table flickertable { { 1, 0.9, 1, 0.4, 1, 0.95, 1, 0.8, 1, 0.2, 1 } }\n\n") + body;
	}
	return body;
}

} // namespace vibestudio
