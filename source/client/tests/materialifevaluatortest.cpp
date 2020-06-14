#include "materialifevaluatortest.h"
#include "../../ref/materiallocal.h"

void MaterialIfEvaluatorTest::test_valueExpr() {
	{
		MaterialIfEvaluator evaluator;
		evaluator.addBool( true );
		auto result = evaluator.exec();
		QVERIFY( result && *result );
	}
	{
		MaterialIfEvaluator evaluator;
		evaluator.addBool( false );
		auto result = evaluator.exec();
		QVERIFY( result && !*result );
	}
	{
		MaterialIfEvaluator evaluator;
		evaluator.addInt( 1337 );
		auto result = evaluator.exec();
		QVERIFY( result && *result );
	}
	{
		MaterialIfEvaluator evaluator;
		evaluator.addInt( 0 );
		auto result = evaluator.exec();
		QVERIFY( result && !*result );
	}
}

void MaterialIfEvaluatorTest::test_unaryExpr() {
	{
		MaterialIfEvaluator evaluator;
		evaluator.addUnaryNot();
		evaluator.addInt( 0 );
		auto result = evaluator.exec();
		QVERIFY( result && *result );
	}
	{
		MaterialIfEvaluator evaluator;
		evaluator.addLeftParen();
		evaluator.addBool( false );
		evaluator.addRightParen();
		auto result = evaluator.exec();
		QVERIFY( result && !*result );
	}
	{
		MaterialIfEvaluator evaluator;
		evaluator.addLeftParen();
		evaluator.addUnaryNot();
		evaluator.addLeftParen();
		evaluator.addBool( false );
		evaluator.addRightParen();
		evaluator.addRightParen();
		auto result = evaluator.exec();
		QVERIFY( result && *result );
	}
}

void MaterialIfEvaluatorTest::test_cmpOp() {
	const CmpOp ops[] = { CmpOp::LS, CmpOp::LE, CmpOp::GE, CmpOp::GT, CmpOp::NE, CmpOp::EQ };
	const std::function<bool(int, int)> preds[] = {
		[]( int x, int y ) { return x < y; },
		[]( int x, int y ) { return x <= y; },
		[]( int x, int y ) { return x >= y; },
		[]( int x, int y ) { return x > y; },
		[]( int x, int y ) { return x != y; },
		[]( int x, int y ) { return x == y; }
	};

	for( int i = -3; i <= 3; ++i ) {
		for( int j = -3; j <= 3; ++j ) {
			for( int k = 0; k < 6; ++k ) {
				MaterialIfEvaluator evaluator;
				evaluator.addInt( i );
				evaluator.addCmpOp( ops[k] );
				evaluator.addInt( j );
				auto result = evaluator.exec();
				QVERIFY( result && *result == preds[k]( i, j ) );
			}
		}
	}
}

void MaterialIfEvaluatorTest::test_logicOp() {
	const LogicOp ops[] = { LogicOp::And, LogicOp::Or };
	const std::function<bool(int, int)> preds[] = {
		[]( int x, int y ) { return (bool)x && (bool)y; },
		[]( int x, int y ) { return (bool)x || (bool)y; }
	};

	for( int i = -1; i <= 1; ++i ) {
		for( int j = -1; j <= 1; ++j ) {
			for( int k = 0; k < 2; ++k ) {
				MaterialIfEvaluator evaluator;
				evaluator.addInt( i );
				evaluator.addLogicOp( ops[k] );
				evaluator.addInt( j );
				auto result = evaluator.exec();
				QVERIFY( result && *result == preds[k]( i, j ) );
			}
		}
	}
}

void MaterialIfEvaluatorTest::test_fail_onExtraTokens() {
	MaterialIfEvaluator evaluator;
	evaluator.addInt( 0 );
	evaluator.addCmpOp( CmpOp::LE );
	evaluator.addInt( 1 );
	evaluator.addInt( 2 );
	QVERIFY( !evaluator.exec() );
}

void MaterialIfEvaluatorTest::test_fail_onUnbalancedParens() {
	MaterialIfEvaluator evaluator;
	evaluator.addLeftParen();
	evaluator.addLeftParen();
	evaluator.addBool( true );
	evaluator.addRightParen();
	evaluator.addLeftParen();
	QVERIFY( !evaluator.exec() );
}

void MaterialIfEvaluatorTest::test_fail_onMissingOperands() {
	{
		MaterialIfEvaluator evaluator;
		evaluator.addUnaryNot();
		QVERIFY( !evaluator.exec() );
	}
	{
		MaterialIfEvaluator evaluator;
		evaluator.addInt( 1 );
		evaluator.addCmpOp( CmpOp::NE );
		QVERIFY( !evaluator.exec() );
	}
	{
		MaterialIfEvaluator evaluator;
		evaluator.addBool( true );
		evaluator.addLogicOp( LogicOp::And );
		QVERIFY( !evaluator.exec() );
	}
}

void MaterialIfEvaluatorTest::test_complexExample() {
	MaterialIfEvaluator evaluator;
	evaluator.addUnaryNot();
	evaluator.addLeftParen();
	{
		{
			evaluator.addInt( 32 );
			evaluator.addCmpOp( CmpOp::GE );
			evaluator.addInt( 32 );
		}
		evaluator.addLogicOp( LogicOp::And );
		{
			evaluator.addInt( 33 );
			evaluator.addCmpOp( CmpOp::LE );
			evaluator.addInt( 30 );
		}
	}
	evaluator.addRightParen();
	auto result = evaluator.exec();
	QVERIFY( result && *result );
}