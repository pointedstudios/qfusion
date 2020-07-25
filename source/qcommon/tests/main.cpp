#include "boundsbuildertest.h"
#include "bufferedreadertest.h"
#include "staticstringtest.h"
#include "stringsplittertest.h"
#include "stringviewtest.h"
#include "tonumtest.h"
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

	{
		BufferedReaderTest bufferedReaderTest;
		result |= QTest::qExec( &bufferedReaderTest, argc, argv );
	}

	{
		ToNumTest toNumTest;
		result |= QTest::qExec( &toNumTest, argc, argv );
	}

	return result;
}
