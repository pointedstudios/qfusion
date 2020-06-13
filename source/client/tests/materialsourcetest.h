#ifndef WSW_MATERIALSOURCETEST_H
#define WSW_MATERIALSOURCETEST_H

#include <QtTest/QtTest>

class MaterialSourceTest : public QObject {
	Q_OBJECT

private slots:
	void test_findPlaceholdersInToken();
	void test_preparePlaceholders();
	void test_expandTemplate();
	void test_realMaterialExample();
};

#endif
