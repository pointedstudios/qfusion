#ifndef WSW_TOKENSPLITTERTEST_H
#define WSW_TOKENSPLITTERTEST_H

#include <QtTest/QtTest>

class TokenSplitterTest : public QObject {
	Q_OBJECT

private slots:
	void test_skipLineComment();
	void test_skipMultiLineComment();
	void test_matchStringLiteral();
	void test_match1Or2CharsTokens();
	void test_realMaterialTokensExample1();
	void test_realMaterialTokensExample2();
};

#endif
