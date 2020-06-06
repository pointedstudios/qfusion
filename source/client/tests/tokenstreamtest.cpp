#include "tokenstreamtest.h"
#include "../../ref/materiallocal.h"

using wsw::operator""_asView;

static const char *tokenCharsData =
	"\tsurfaceparm nolightmap\n"
	"\tsurfaceparm nomarks\n";

static const QVector<TokenSpan> tokenSpans {
	{ 1, 11, 0 }, { 13, 10, 0 },
	{ 25, 11, 1 }, { 37, 7, 1 }
};

void TokenStreamTest::test_getNextToken() {
	TokenStream stream( tokenCharsData, tokenSpans.data(), tokenSpans.size() );
	QCOMPARE( stream.getCurrTokenNum(), 0 );
	QCOMPARE( stream.getNextToken(), std::optional( "surfaceparm"_asView ) );
	QCOMPARE( stream.getNextToken(), std::optional( "nolightmap"_asView ) );
	QCOMPARE( stream.getNextToken(), std::optional( "surfaceparm"_asView ) );
	QCOMPARE( stream.getNextToken(), std::optional( "nomarks"_asView ) );
	QCOMPARE( stream.getNextToken(), std::nullopt );
	QCOMPARE( stream.getCurrTokenNum(), 4 );
}

void TokenStreamTest::test_getNextTokenInLine() {
	TokenStream stream( tokenCharsData, tokenSpans.data(), tokenSpans.size() );
	QCOMPARE( stream.getNextTokenInLine(), std::optional( "surfaceparm"_asView ) );
	QCOMPARE( stream.getNextTokenInLine(), std::optional( "nolightmap"_asView ) );
	QCOMPARE( stream.getNextTokenInLine(), std::nullopt );
	// This forces the current line reset
	stream.setCurrTokenNum( stream.getCurrTokenNum() );
	QCOMPARE( stream.getNextTokenInLine(), std::optional( "surfaceparm"_asView ) );
	QCOMPARE( stream.getNextTokenInLine(), std::optional( "nomarks"_asView ) );
	QCOMPARE( stream.getNextTokenInLine(), std::nullopt );
	stream.setCurrTokenNum( 0 );
	QCOMPARE( stream.getNextTokenInLine(), std::optional( "surfaceparm"_asView ) );
	QCOMPARE( stream.getNextTokenInLine(), std::optional( "nolightmap"_asView ) );
	QCOMPARE( stream.getNextTokenInLine(), std::nullopt );
	stream.setCurrTokenNum( tokenSpans.size() );
	QCOMPARE( stream.getNextTokenInLine(), std::nullopt );
}

void TokenStreamTest::test_unGetToken() {
	TokenStream stream( tokenCharsData, tokenSpans.data(), tokenSpans.size() );
	QCOMPARE( stream.unGetToken(), false );
	QCOMPARE( stream.getNextToken(), std::optional( "surfaceparm"_asView ) );
	QCOMPARE( stream.unGetToken(), true );
	QCOMPARE( stream.getNextToken(), std::optional( "surfaceparm"_asView ) );
	QCOMPARE( stream.getNextToken(), std::optional( "nolightmap"_asView ) );
	QCOMPARE( stream.getNextTokenInLine(), std::nullopt );
	QCOMPARE( stream.unGetToken(), true );
	QCOMPARE( stream.getNextToken(), std::optional( "nolightmap"_asView ) );
	QCOMPARE( stream.getNextToken(), std::optional( "surfaceparm"_asView ) );
	QCOMPARE( stream.getNextToken(), std::optional( "nomarks"_asView ) );
	QCOMPARE( stream.getNextToken(), std::nullopt );
	QCOMPARE( stream.unGetToken(), true );
	QCOMPARE( stream.getNextToken(), std::optional( "nomarks"_asView ) );
}
