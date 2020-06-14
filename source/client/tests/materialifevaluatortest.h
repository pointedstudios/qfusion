#ifndef WSW_MATERIALIFEVALUATORTEST_H
#define WSW_MATERIALIFEVALUATORTEST_H

#include <QtTest/QtTest>

class MaterialIfEvaluatorTest : public QObject {
	Q_OBJECT

private slots:
	void test_valueExpr();
	void test_unaryExpr();
	void test_cmpOp();
	void test_logicOp();
	void test_fail_onExtraTokens();
	void test_fail_onUnbalancedParens();
	void test_fail_onMissingOperands();
	void test_complexExample();
};

#endif
