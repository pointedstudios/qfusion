#include <QCoreApplication>
#include "tokensplittertest.h"

int main( int argc, char **argv ) {
	QCoreApplication app( argc, argv );

	int result = 0;

	{
		TokenSplitterTest tokenSplitterTest;
		result |= QTest::qExec( &tokenSplitterTest, argc, argv );
	}

	return result;
}