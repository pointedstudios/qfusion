#include "materialsourcetest.h"
#include "../../ref/materiallocal.h"

using wsw::operator""_asView;

void MaterialSourceTest::test_findPlaceholdersInToken() {
	wsw::Vector<PlaceholderSpan> placeholders;

	MaterialSource::findPlaceholdersInToken( ""_asView, 1, placeholders );
	QCOMPARE( placeholders.size(), 0 );

	MaterialSource::findPlaceholdersInToken( "gfx/misc/misc.tga"_asView, 1, placeholders );
	QCOMPARE( placeholders.size(), 0 );

	MaterialSource::findPlaceholdersInToken( "gfx/$17$15/misc.tga"_asView, 1, placeholders );
	QCOMPARE( placeholders.size(), 2 );

	{
		auto [tokenNum, offset, len, argNum] = placeholders[0];
		QCOMPARE( tokenNum, 1 );
		QCOMPARE( offset, 4 );
		QCOMPARE( len, 3 );
		QCOMPARE( argNum, 17 );
	}

	{
		auto [tokenNum, offset, len, argNum] = placeholders[1];
		QCOMPARE( tokenNum, 1 );
		QCOMPARE( offset, 7 );
		QCOMPARE( len, 3 );
		QCOMPARE( argNum, 15 );
	}

	placeholders.clear();
	MaterialSource::findPlaceholdersInToken( "$1gfx/misc/$23/$10/$3"_asView, 1, placeholders );
	QCOMPARE( placeholders.size(), 4 );

	{
		auto [tokenNum, offset, len, argNum] = placeholders[0];
		QCOMPARE( tokenNum, 1 );
		QCOMPARE( offset, 0 );
		QCOMPARE( len, 2 );
		QCOMPARE( argNum, 1 );
	}

	{
		auto [tokenNum, offset, len, argNum] = placeholders[1];
		QCOMPARE( tokenNum, 1 );
		QCOMPARE( offset, 11 );
		QCOMPARE( len, 3 );
		QCOMPARE( argNum, 23 );
	}

	{
		auto [tokenNum, offset, len, argNum] = placeholders[2];
		QCOMPARE( tokenNum, 1 );
		QCOMPARE( offset, 15 );
		QCOMPARE( len, 3 );
		QCOMPARE( argNum, 10 );
	}

	{
		auto [tokenNum, offset, len, argNum] = placeholders[3];
		QCOMPARE( tokenNum, 1 );
		QCOMPARE( offset, 19 );
		QCOMPARE( len, 2 );
		QCOMPARE( argNum, 3 );
	}
}

void MaterialSourceTest::test_preparePlaceholders() {
	TokenSpan spans[] = {
		{ 0, 1, 0 },
		{ 3, 6, 1 },
		{ 10, 8, 1 },
		{ 19, 4, 1 },
		{ 24, 1, 2 }
	};

	MaterialFileContents contents;
	contents.data = "{\n span$3 sp$1an$2 span\n}";
	contents.dataSize = std::strlen( contents.data );
	contents.spans = spans;
	contents.numSpans = 5;

	{
		MaterialSource source;
		source.m_fileContents = &contents;
		source.m_tokenSpansOffset = 3;
		source.m_numTokens = 1;

		auto maybePlaceholders = source.preparePlaceholders();
		QVERIFY( !maybePlaceholders.has_value() );
	}

	{
		MaterialSource source;
		source.m_fileContents = &contents;
		source.m_tokenSpansOffset = 0;
		source.m_numTokens = 5;

		auto maybePlaceholders = source.preparePlaceholders();
		QVERIFY( maybePlaceholders.has_value() );

		auto placeholders = *maybePlaceholders;
		QCOMPARE( placeholders.size(), 3 );

		{
			auto [tokenNum, offset, len, argNum] = placeholders[0];
			QCOMPARE( tokenNum, 1 );
			QCOMPARE( offset, 4 );
			QCOMPARE( len, 2 );
			QCOMPARE( argNum, 3 );
		}
		{
			auto [tokenNum, offset, len, argNum] = placeholders[1];
			QCOMPARE( tokenNum, 2 );
			QCOMPARE( offset, 2 );
			QCOMPARE( len, 2 );
			QCOMPARE( argNum, 1 );
		}
		{
			auto [tokenNum, offset, len, argNum] = placeholders[2];
			QCOMPARE( tokenNum, 2 );
			QCOMPARE( offset, 6 );
			QCOMPARE( len, 2 );
			QCOMPARE( argNum, 2 );
		}
	}
}

static QString getTokenString( const TokenSpan &span, const char *baseBuffer, const wsw::String &expansionBuffer ) {
	if( span.offset < 0 ) {
		return QString::fromUtf8( expansionBuffer.data() - span.offset, span.len );
	}
	return QString::fromUtf8( baseBuffer + span.offset, span.len );
}

void MaterialSourceTest::test_expandTemplate() {
	TokenSpan spans[] = {
		{ 1, 1, 0 },
		{ 4, 4, 1 },
		{ 9, 3, 1 },
		{ 13, 6, 2 },
		{ 20, 1, 3 },
	};

	MaterialFileContents contents;
	contents.data = "\n{\n span @$3\n$1/$2@\n}";
	contents.dataSize = std::strlen( contents.data );
	contents.spans = spans;
	contents.numSpans = 5;

	MaterialSource source;
	source.m_fileContents = &contents;
	source.m_tokenSpansOffset = 0;
	source.m_numTokens = 5;

	const wsw::StringView args[] = { "first"_asView, "second"_asView, "third"_asView };

	wsw::String expansionBuffer;
	wsw::Vector<TokenSpan> resultingTokens;
	const bool expansionResult = source.expandTemplate( args, 3, expansionBuffer, resultingTokens );

	QVERIFY( expansionResult );
	QCOMPARE( resultingTokens.size(), 5 );
	QCOMPARE( getTokenString( resultingTokens[0], contents.data, expansionBuffer ), "{" );
	QCOMPARE( getTokenString( resultingTokens[1], contents.data, expansionBuffer ), "span" );
	QCOMPARE( getTokenString( resultingTokens[2], contents.data, expansionBuffer ), "@third" );
	QCOMPARE( getTokenString( resultingTokens[3], contents.data, expansionBuffer ), "first/second@" );
	QCOMPARE( getTokenString( resultingTokens[4], contents.data, expansionBuffer ), "}" );
}

void MaterialSourceTest::test_realMaterialExample() {
	const char *data =
		"simpleitem_Template\n"
		"{\n"
		"\tnopicmip\n"
		"\tentityMergable\t\t// allow all the sprites to be merged together\n"
		"\t{\n"
		"\t\tmap gfx/simpleitems/$1/$2.tga\n"
		"\t\tblendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA\n"
		"\t\talphagen entity\n"
		"\t}\n"
		"}";

	const auto dataSize = std::strlen( data );
	TokenSplitter splitter( data, dataSize );
	wsw::Vector<TokenSpan> spans;
	uint32_t lineNum = 0;
	while( !splitter.isAtEof() ) {
		if( auto maybeToken = splitter.fetchNextTokenInLine() ) {
			auto [off, len] = *maybeToken;
			spans.push_back( { (int32_t)off, len, lineNum } );
		}
		lineNum++;
	}

	MaterialFileContents contents;
	contents.data = data;
	contents.dataSize = dataSize;
	contents.spans = spans.data();
	contents.dataSize = spans.size();

	MaterialSource source;
	source.m_fileContents = &contents;
	source.m_tokenSpansOffset = 0;
	source.m_numTokens = spans.size();

	wsw::String expansionBuffer;
	wsw::Vector<TokenSpan> resultingTokenSpans;
	const wsw::StringView args[] = { "weapon"_asView, "plasma"_asView };
	const bool expansionResult = source.expandTemplate( args, 2, expansionBuffer, resultingTokenSpans );

	QStringList actualTokenStrings;
	for( const TokenSpan &span : resultingTokenSpans ) {
		actualTokenStrings.append( getTokenString( span, data, expansionBuffer.data() ) );
	}

	QStringList expectedTokenStrings {
		"simpleitem_Template",
		"{",
		"nopicmip",
		"entityMergable",
		"{",
		"map", "gfx/simpleitems/weapon/plasma.tga",
		"blendFunc", "GL_SRC_ALPHA", "GL_ONE_MINUS_SRC_ALPHA",
		"alphagen", "entity",
		"}",
		"}"
	};

	QVERIFY( expansionResult );
	QCOMPARE( actualTokenStrings, expectedTokenStrings );
}
