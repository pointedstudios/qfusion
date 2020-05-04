#include "boundsbuildertest.h"
#include "../../gameshared/q_math.h"

#include <QtGui/QVector3D>

#ifdef WSW_USE_SSE2
#undef WSW_USE_SSE2
#endif

static const vec3_t testedPoints1[] {
	{ -10, 3, 89 }, { -7, 7, -42 }, { 84, 95, 72 }, { -108, -4, 73 }
};

static const vec3_t expectedMins1 { -108, -4, -42 };
static const vec3_t expectedMaxs1 { 84, 95, 89 };

static const vec3_t testedPoints2[] {
	{ -32, 78, 67 }, { 84, 87, -34 }, { 85, 11, 86 }, { -5, -6, -111 }
};

static const vec3_t expectedMins2 { -32, -6, -111 };
static const vec3_t expectedMaxs2 { 85, 87, 86 };

QVector3D toQV( const float *v ) {
	return QVector3D( v[0], v[1], v[2] );
}

QVector3D toQV( __m128 v ) {
	alignas( 16 ) float tmp[4];
	_mm_store_ps( tmp, v );
	return QVector3D( v[0], v[1], v[2] );
}

void BoundsBuilderTest::test_generic() {
	{
		BoundsBuilder builder;
		for( const float *p: testedPoints1 ) {
			builder.addPoint( p );
		}
		{
			vec3_t actualMins, actualMaxs;
			builder.storeTo( actualMins, actualMaxs );
			QCOMPARE( toQV( actualMins ), toQV( expectedMins1 ) );
			QCOMPARE( toQV( actualMaxs ), toQV( expectedMaxs1 ) );
		}
		{
			vec3_t actualMins, actualMaxs;
			builder.storeToWithAddedEpsilon( actualMins, actualMaxs, 1.0f );
			QCOMPARE( toQV( actualMins ), toQV( expectedMins1 ) - QVector3D( 1, 1, 1 ) );
			QCOMPARE( toQV( actualMaxs ), toQV( expectedMaxs1 ) + QVector3D( 1, 1, 1 ) );
		}
	}
	{
		BoundsBuilder builder;
		for( const float *p: testedPoints2 ) {
			builder.addPoint( p );
		}
		{
			vec3_t actualMins, actualMaxs;
			builder.storeTo( actualMins, actualMaxs );
			QCOMPARE( toQV( actualMins ), toQV( expectedMins2 ) );
			QCOMPARE( toQV( actualMaxs ), toQV( expectedMaxs2 ) );
		}
		{
			vec3_t actualMins, actualMaxs;
			builder.storeToWithAddedEpsilon( actualMins, actualMaxs, 1.0f );
			QCOMPARE( toQV( actualMins ), toQV( expectedMins2 ) - QVector3D( 1, 1, 1 ) );
			QCOMPARE( toQV( actualMaxs ), toQV( expectedMaxs2 ) + QVector3D( 1, 1, 1 ) );
		}
	}
}

#define WSW_USE_SSE2

void BoundsBuilderTest::test_sse2() {
	{
		BoundsBuilder builder;
		for( const float *p: testedPoints1 ) {
			builder.addPoint( p );
		}
		{
			vec3_t actualMins, actualMaxs;
			builder.storeTo( actualMins, actualMaxs );
			QCOMPARE( toQV( actualMins ), toQV( expectedMins1 ) );
			QCOMPARE( toQV( actualMaxs ), toQV( expectedMaxs1 ) );
		}
		{
			vec3_t actualMins, actualMaxs;
			builder.storeToWithAddedEpsilon( actualMins, actualMaxs, 1.0f );
			QCOMPARE( toQV( actualMins ), toQV( expectedMins1 ) - QVector3D( 1, 1, 1 ) );
			QCOMPARE( toQV( actualMaxs ), toQV( expectedMaxs1 ) + QVector3D( 1, 1, 1 ) );
		}
	}
	{
		BoundsBuilder builder;
		for( const float *p: testedPoints2 ) {
			builder.addPoint( p );
		}
		{
			vec3_t actualMins, actualMaxs;
			builder.storeTo( actualMins, actualMaxs );
			QCOMPARE( toQV( actualMins ), toQV( expectedMins2 ) );
			QCOMPARE( toQV( actualMaxs ), toQV( expectedMaxs2 ) );
		}
		{
			vec3_t actualMins, actualMaxs;
			builder.storeToWithAddedEpsilon( actualMins, actualMaxs, 1.0f );
			QCOMPARE( toQV( actualMins ), toQV( expectedMins2 ) - QVector3D( 1, 1, 1 ) );
			QCOMPARE( toQV( actualMaxs ), toQV( expectedMaxs2 ) + QVector3D( 1, 1, 1 ) );
		}
	}
}

void BoundsBuilderTest::test_sse2Specific() {
	{
		BoundsBuilder builder;
		for( const float *p: testedPoints1 ) {
			builder.addPoint( _mm_setr_ps( p[0], p[1], p[2], 0.5f ) );
		}
		{
			__m128 actualMins, actualMaxs;
			builder.storeTo( &actualMins, &actualMaxs );
			QCOMPARE( toQV( actualMins ), toQV( expectedMins1 ) );
			QCOMPARE( toQV( actualMaxs ), toQV( expectedMaxs1 ) );
		}
		{
			__m128 actualMins, actualMaxs;
			builder.storeToWithAddedEpsilon( &actualMins, &actualMaxs, 1.0f );
			QCOMPARE( toQV( actualMins ), toQV( expectedMins1 ) - QVector3D( 1, 1, 1 ) );
			QCOMPARE( toQV( actualMaxs ), toQV( expectedMaxs1 ) + QVector3D( 1, 1, 1 ) );
		}
	}
	{
		BoundsBuilder builder;
		for( const float *p: testedPoints2 ) {
			builder.addPoint( p );
		}
		{
			__m128 actualMins, actualMaxs;
			builder.storeTo( &actualMins, &actualMaxs );
			QCOMPARE( toQV( actualMins ), toQV( expectedMins2 ) );
			QCOMPARE( toQV( actualMaxs ), toQV( expectedMaxs2 ) );
		}
		{
			__m128 actualMins, actualMaxs;
			builder.storeToWithAddedEpsilon( &actualMins, &actualMaxs, 1.0f );
			QCOMPARE( toQV( actualMins ), toQV( expectedMins2 ) - QVector3D( 1, 1, 1 ) );
			QCOMPARE( toQV( actualMaxs ), toQV( expectedMaxs2 ) + QVector3D( 1, 1, 1 ) );
		}
	}
}
