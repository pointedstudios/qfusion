#include <QCoreApplication>
#include "tokensplittertest.h"
#include "tokenstreamtest.h"

int main( int argc, char **argv ) {
	QCoreApplication app( argc, argv );

	int result = 0;

	{
		TokenSplitterTest tokenSplitterTest;
		result |= QTest::qExec( &tokenSplitterTest, argc, argv );
	}

	{
		TokenStreamTest tokenStreamTest;
		result |= QTest::qExec( &tokenStreamTest, argc, argv );
	}

	return result;
}