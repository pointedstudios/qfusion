#include "stringviewtest.h"
#include <QCoreApplication>

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);

	StringViewTest stringViewTest;

	return QTest::qExec(&stringViewTest, argc, argv);
}
