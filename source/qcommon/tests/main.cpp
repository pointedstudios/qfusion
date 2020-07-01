#include "boundsbuildertest.h"
#include "staticstringtest.h"
#include "stringsplittertest.h"
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
		StaticStringTest staticStringTest;
		result |= QTest::qExec( &staticStringTest, argc, argv );
	}

	{
		StringSplitterTest stringSplitterTest;
		result |= QTest::qExec( &stringSplitterTest, argc, argv );
	}

	{
		BoundsBuilderTest boundsBuilderTest;
		result |= QTest::qExec( &boundsBuilderTest, argc, argv );
	}

	return result;
}
