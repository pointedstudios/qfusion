#ifndef WSW_TOKENSTREAMTEST_H
#define WSW_TOKENSTREAMTEST_H

#include <QtTest/QtTest>

class TokenStreamTest : public QObject {
	Q_OBJECT

private slots:
	void test_getNextToken();
	void test_getNextTokenInLine();
	void test_unGetToken();
};

#endif
