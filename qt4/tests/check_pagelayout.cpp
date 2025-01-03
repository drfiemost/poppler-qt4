#include <QtTest/QtTest>

#include <poppler-qt4.h>

class TestPageLayout: public QObject
{
    Q_OBJECT
private slots:
    void checkNone();
    void checkSingle();
    void checkFacing();
};

void TestPageLayout::checkNone()
{
    Poppler::Document *doc;
    doc = Poppler::Document::load(TESTDATADIR "/unittestcases/UseNone.pdf");
    QVERIFY( doc );
  
    QCOMPARE( doc->pageLayout(), Poppler::Document::NoLayout );

    delete doc;
}

void TestPageLayout::checkSingle()
{
    Poppler::Document *doc;
    doc = Poppler::Document::load(TESTDATADIR "/unittestcases/FullScreen.pdf");
    QVERIFY( doc );
  
    QCOMPARE( doc->pageLayout(), Poppler::Document::SinglePage );

    delete doc;
}

void TestPageLayout::checkFacing()
{
    Poppler::Document *doc;
    doc = Poppler::Document::load(TESTDATADIR "/unittestcases/doublepage.pdf");
    QVERIFY( doc );

    QCOMPARE( doc->pageLayout(), Poppler::Document::TwoPageRight );

    delete doc;
}

QTEST_MAIN(TestPageLayout)
#include "moc_check_pagelayout.cpp"

