#include "tokensplittertest.h"
#include "../../ref/materiallocal.h"

static QVector<QString> getTokens( const char *data ) {
	QVector<QString> result;

	TokenSplitter splitter( data, std::strlen( data ) );
	while( !splitter.isAtEof() ) {
		if( auto maybeToken = splitter.fetchNextTokenInLine() ) {
			auto [off, len] = *maybeToken;
			result.append( QString::fromLatin1( data + off, len ) );
		}
	}

	return result;
}

void TokenSplitterTest::test_skipLineComment() {
	const char *data1 =
		"{\n"
		"//\n"
		"}\n";

	const auto tokens1 = getTokens( data1 );
	QCOMPARE( tokens1.size(), 2 );
	QCOMPARE( tokens1[0], "{" );
	QCOMPARE( tokens1[1], "}" );

	const char *data2 =
		"{//}\n"
		"//}\n"
		" }";

	const auto tokens2 = getTokens( data2 );
	QCOMPARE( tokens2.size(), 2 );
	QCOMPARE( tokens2[0], "{" );
	QCOMPARE( tokens2[1], "}" );
}

void TokenSplitterTest::test_skipMultiLineComment() {
	const char *data1 =
		"/*{\n"
		"    \n"
		"}*/\n";

	QVERIFY( getTokens( data1 ).isEmpty() );

	const char *data2 =
		"/*\n{}\n*/{/*\n"
		"{\"}\"}*/\n"
		"}/*\n*/\n";

	const auto tokens2 = getTokens( data2 );
	QCOMPARE( tokens2.size(), 2 );
	QCOMPARE( tokens2[0], "{" );
	QCOMPARE( tokens2[1], "}" );
}

void TokenSplitterTest::test_matchStringLiteral() {
	const char *data1 = "\n\"{/**///}\"\n";
	const auto tokens1 = getTokens( data1 );
	QCOMPARE( tokens1.size(), 1 );
	QCOMPARE( tokens1[0], "{/**///}" );

	const char *data2 =
		"\"\n"
		"\"\n"
		"\"\""
		"\"\n"
		"\"\n";

	const auto tokens2 = getTokens( data2 );
	QCOMPARE( tokens2.size(), 3 );
	QCOMPARE( tokens2[0], "\n" );
	QCOMPARE( tokens2[1], "" );
	QCOMPARE( tokens2[2], "\n" );
}

void TokenSplitterTest::test_match1Or2CharsTokens() {
	const char *data = "\n{/**/}//\n(\"== /*string literal*/ ==\")<<=/**/>!=>=\n==";
	const auto actualTokens = getTokens( data );
	const QVector<QString> expectedTokens = {
		"{", "}", "(",
		"== /*string literal*/ ==",  // This one is just to test quotes behaviour
		")", "<", "<=", ">", "!=", ">=", "=="
	};
	QCOMPARE( actualTokens, expectedTokens );
}

void TokenSplitterTest::test_realMaterialTokensExample1() {
	// reactors.shader
	// Most passes in the body are omitted
	const char *data =
		"textures/reactors/sky_s\n"
		"{\n"
		"\tqer_editorimage textures/blxbis/skyturq_scroll.tga\n"
		"\tsurfaceparm noimpact\n"
		"\tsurfaceparm nomarks\n"
		"\tsurfaceparm nolightmap\n"
		"\tsurfaceparm sky\n"
		"\n"
		"\tskyParms - 2048 -\n"
		"\n"
		"\t{\n"
		"\t\tmap textures/blxbis/skyturq_scroll2.tga\n"
		"\t\ttcMod scale 4 4\n"
		"\t\ttcMod scroll 0 -0.015\n"
		"\t\trgbgen const 0.2 0.2 0.2\n"
		"\t}\n"
		"\n"
		"}";

	const QVector<QString> expectedTokens = {
		"textures/reactors/sky_s",
		"{",
		"qer_editorimage", "textures/blxbis/skyturq_scroll.tga",
		"surfaceparm", "noimpact",
		"surfaceparm", "nomarks",
		"surfaceparm", "nolightmap",
		"surfaceparm", "sky",
		"skyParms", "-", "2048", "-",
		"{",
		"map", "textures/blxbis/skyturq_scroll2.tga",
		"tcMod", "scale", "4", "4",
		"tcMod", "scroll", "0", "-0.015",
		"rgbgen", "const", "0.2", "0.2", "0.2",
		"}",
		"}"
	};

	const auto actualTokens = getTokens( data );
	QCOMPARE( actualTokens, expectedTokens );
}

void TokenSplitterTest::test_realMaterialTokensExample2() {
	const char *data =
		"gfx/misc/electro_alpha\n"
		"{\n"
		"\tcull none\n"
		"\tnopicmip\n"
		"\tnomipmaps\n"
		"\tdeformVertexes autosprite2\n"
		"\t{\n"
		"\t\tmap gfx/misc/electro.tga\n"
		"\t\talphaGen vertex\n"
		"\t\trgbgen teamcolor 2\n"
		"\t\tblendFunc GL_SRC_ALPHA GL_ONE // blendfunc add the alphamasked part only\n"
		"\t}\n"
		"}\n"
		"\n";

	const QVector<QString> expectedTokens = {
		"gfx/misc/electro_alpha",
		"{",
		"cull", "none",
		"nopicmip",
		"nomipmaps",
		"deformVertexes", "autosprite2",
		"{",
		"map", "gfx/misc/electro.tga",
		"alphaGen", "vertex",
		"rgbgen", "teamcolor", "2",
		"blendFunc", "GL_SRC_ALPHA", "GL_ONE",
		"}",
		"}"
	};

	const auto actualTokens = getTokens( data );
	QCOMPARE( actualTokens, expectedTokens );
}