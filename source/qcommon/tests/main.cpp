#include "boundsbuildertest.h"
#include "stringviewtest.h"
#include <QCoreApplication>

int main( int argc, char **argv ) {
	QCoreApplication app( argc, argv );
	(void)std::setlocale( LC_ALL, "C" );

	int result = 0;

	{
		StringViewTest stringViewTest;
		result |= QTest::qExec( &stringViewTest, argc, argv );
	}

	{
		BoundsBuilderTest boundsBuilderTest;
		result |= QTest::qExec( &boundsBuilderTest, argc, argv );
	}

	return result;
}
