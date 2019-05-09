/*
 * Copyright (C) 2014 Jolla Ltd. <richard.braakman@jollamobile.com>
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * "Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Nemo Mobile nor the names of its contributors
 *     may be used to endorse or promote products derived from this
 *     software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
 */

#define QT_STATICPLUGIN

#include <QtTest/QtTest>
#include "../../../src/engine/contactsdatabase.h"

class tst_Database  : public QObject
{
    Q_OBJECT

protected slots:
    void init();
    void cleanup();

private slots:
    void fromDateTimeString_data();
    void fromDateTimeString();
    void fromDateTimeString_speed();
    void fromDateTimeString_tz_speed();
    void fromDateTimeString_isodate_speed();

private:
    char *old_TZ;
};

void tst_Database::init()
{
    old_TZ = getenv("TZ");
    unsetenv("TZ");
}

void tst_Database::cleanup()
{
    if (old_TZ)
        setenv("TZ", old_TZ, 1);
    else
        unsetenv("TZ");
}

void tst_Database::fromDateTimeString_data()
{
    QTest::addColumn<QString>("datetime");
    QTest::addColumn<QString>("TZ");
    QTest::addColumn<int>("year");
    QTest::addColumn<int>("month");
    QTest::addColumn<int>("day");
    QTest::addColumn<int>("hour");
    QTest::addColumn<int>("minute");
    QTest::addColumn<int>("second");
    QTest::addColumn<int>("msec");

    QTest::newRow("base case")
        << "2014-08-12T14:22:09.334"
        << ""
        << 2014 << 8 << 12
        << 14 << 22 << 9 << 334;

    // Some older databases may contain values without fractional seconds
    QTest::newRow("no msec")
        << "2014-08-12T14:22:09"
        << ""
        << 2014 << 8 << 12
        << 14 << 22 << 9 << 0;

    // TZ should have no effect on result because the string is in UTC
    QTest::newRow("with TZ")
        << "2014-08-12T14:22:09.334"
        << "Europe/Helsinki"
        << 2014 << 8 << 12
        << 14 << 22 << 9 << 334;

    QTest::newRow("ancient") // timestamp long before epoch
        << "1890-02-04T11:07:12.123"
        << ""
        << 1890 << 2 << 4
        << 11 << 7 << 12 << 123;

    QTest::newRow("future") // past year-2038 problem
        << "2050-02-04T11:07:12.123"
        << ""
        << 2050 << 2 << 4
        << 11 << 7 << 12 << 123;

    // Try a timespec that does not exist in the given TZ
    // It should still be parsed correctly because the time is in UTC
    QTest::newRow("DST trap")
        << "2013-03-31T03:30:09.334"
        << "Europe/Helsinki"
        << 2013 << 3 << 31 
        << 3 << 30 << 9 << 334;
}

void tst_Database::fromDateTimeString()
{
    QFETCH(QString, datetime);
    QFETCH(QString, TZ);

    if (!TZ.isEmpty())
        setenv("TZ", qPrintable(TZ), 1);

    QDateTime actual = ContactsDatabase::fromDateTimeString(datetime);

    QVERIFY(actual.isValid());
    QCOMPARE(actual.timeSpec(), Qt::UTC);
    QTEST(actual.date().year(), "year");
    QTEST(actual.date().month(), "month");
    QTEST(actual.date().day(), "day");
    QTEST(actual.time().hour(), "hour");
    QTEST(actual.time().minute(), "minute");
    QTEST(actual.time().second(), "second");
    QTEST(actual.time().msec(), "msec");
}

void tst_Database::fromDateTimeString_speed()
{
    QString datetime("2014-08-12T14:22:09.334");

    QBENCHMARK {
        QDateTime actual = ContactsDatabase::fromDateTimeString(datetime);
    }
}

void tst_Database::fromDateTimeString_tz_speed()
{
    QString datetime("2014-08-12T14:22:09.334");

    setenv("TZ", ":/etc/localtime", 1); // this works around a weird glibc issue

    QBENCHMARK {
        QDateTime actual = ContactsDatabase::fromDateTimeString(datetime);
    }
}

// Compare with Qt upstream date parsing, to see if it's worth
// simplifying back to calling that.
void tst_Database::fromDateTimeString_isodate_speed()
{
    QString datetime("2014-08-12T14:22:09.334");

    QBENCHMARK {
        QDateTime rv = QDateTime::fromString(datetime, Qt::ISODate);
        rv.setTimeSpec(Qt::UTC);
    }
}

QTEST_GUILESS_MAIN(tst_Database)
#include "tst_database.moc"
