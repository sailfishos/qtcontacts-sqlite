/*
 * Copyright (C) 2013 Jolla Ltd. <matthew.vogt@jollamobile.com>
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

#include "../../util.h"
#include "../../../src/extensions/qtcontacts-extensions.h"
#include "../../../src/extensions/qtcontacts-extensions_impl.h"

class tst_PhoneNumber : public QObject
{
Q_OBJECT

public:
    tst_PhoneNumber();
    virtual ~tst_PhoneNumber();

public slots:
    void init();
    void initTestCase();
    void cleanup();
    void cleanupTestCase();

private slots:
    void normalization_data();
    void normalization();
};

tst_PhoneNumber::tst_PhoneNumber()
{
}

tst_PhoneNumber::~tst_PhoneNumber()
{
}

void tst_PhoneNumber::init()
{
}

void tst_PhoneNumber::initTestCase()
{
}

void tst_PhoneNumber::cleanup()
{
}

void tst_PhoneNumber::cleanupTestCase()
{
}

void tst_PhoneNumber::normalization_data()
{
    QTest::addColumn<QString>("number");
    QTest::addColumn<QString>("keepAll");
    QTest::addColumn<QString>("keepPunctuation");
    QTest::addColumn<QString>("keepDialString");
    QTest::addColumn<QString>("normalized");
    QTest::addColumn<bool>("valid");

    QTest::newRow("empty")
        << ""
        << ""
        << ""
        << ""
        << ""
        << false;

    QTest::newRow("simple")
        << "1234567890"
        << "1234567890"
        << "1234567890"
        << "1234567890"
        << "1234567890"
        << true;

    QTest::newRow("unicode digits")     // Non-ASCII digits converted to ASCII equivalents
        << QStringLiteral("1234") + QChar(0xff15) + QStringLiteral("6789") + QChar(0xff10)
        << "1234567890"
        << "1234567890"
        << "1234567890"
        << "1234567890"
        << true;

    QTest::newRow("short")
        << "12345"
        << "12345"
        << "12345"
        << "12345"
        << "12345"
        << true;

    QTest::newRow("bad")
        << "abcdefg"
        << ""
        << ""
        << ""
        << ""
        << false;

    QTest::newRow("bad trailing characters")
        << "1234567abcdefg"
        << "1234567"
        << "1234567"
        << "1234567"
        << "1234567"
        << false;

    QTest::newRow("bad leading characters")
        << "abcdefg1234567"
        << "1234567"
        << "1234567"
        << "1234567"
        << "1234567"
        << false;

    QTest::newRow("bad internal characters")
        << "12abc34defg567"
        << "1234567"
        << "1234567"
        << "1234567"
        << "1234567"
        << false;

    QTest::newRow("spaces")
        << " 123 456 7890 "
        << "123 456 7890"       // Internal spaces are kept as punctuation
        << "123 456 7890"
        << "1234567890"
        << "1234567890"
        << true;

    QTest::newRow("whitespace")
        << "\t  123 4567\r\n89\t0 "
        << "123 4567890"
        << "123 4567890"
        << "1234567890"
        << "1234567890"
        << false;               // Other than space, whitespace chars are invalid

    QTest::newRow("parentheses")
        << "(12)34[567]890"     // Note, we permit square brackets
        << "(12)34[567]890"
        << "(12)34[567]890"
        << "1234567890"
        << "1234567890"
        << true;

    QTest::newRow("invalid braces")
        << "12{34}[567]890"
        << "1234[567]890"
        << "1234[567]890"
        << "1234567890"
        << "1234567890"
        << false;

    QTest::newRow("punctuation")
        << "12-34.567.-890"
        << "12-34.567.-890"
        << "12-34.567.-890"
        << "1234567890"
        << "1234567890"
        << true;

    QTest::newRow("invalid punctuation")
        << "12_34,567,|890"
        << "1234567890"
        << "1234567890"
        << "1234567890"
        << "1234567890"
        << false;

    QTest::newRow("plus 1")
        << "+1234567890"
        << "+1234567890"
        << "+1234567890"
        << "+1234567890"
        << "+1234567890"
        << true;

    QTest::newRow("plus 2")
        << "(+12)34567890"
        << "(+12)34567890"
        << "(+12)34567890"
        << "+1234567890"
        << "+1234567890"
        << true;

    QTest::newRow("plus 3")
        << "  +[1 2] 34567890"
        << "+[1 2] 34567890"
        << "+[1 2] 34567890"
        << "+1234567890"
        << "+1234567890"
        << true;

    QTest::newRow("plus 4")
        << "+1234567890*1"
        << "+1234567890*1"
        << "+1234567890"
        << "+1234567890*1"
        << "+1234567890*1"
        << true;

    QTest::newRow("invalid plus 1")
        << "12345+67890"
        << "1234567890"
        << "1234567890"
        << "1234567890"
        << "1234567890"
        << false;

    QTest::newRow("invalid plus 2")
        << "1234567890+"
        << "1234567890"
        << "1234567890"
        << "1234567890"
        << "1234567890"
        << false;

    QTest::newRow("DTMF 1")
        << "1234567890p1"
        << "1234567890p1"
        << "1234567890"
        << "1234567890p1"
        << "1234567890p1"
        << true;

    QTest::newRow("DTMF 2")
        << "1234567890P1"
        << "1234567890P1"
        << "1234567890"
        << "1234567890P1"
        << "1234567890P1"
        << true;

    QTest::newRow("DTMF 3")
        << "1234567890w1"
        << "1234567890w1"
        << "1234567890"
        << "1234567890w1"
        << "1234567890w1"
        << true;

    QTest::newRow("DTMF 4")
        << "1234567890W1"
        << "1234567890W1"
        << "1234567890"
        << "1234567890W1"
        << "1234567890W1"
        << true;

    QTest::newRow("DTMF 5")
        << "1234567890x1"
        << "1234567890p1"       // 'x' is converted to 'p'
        << "1234567890"
        << "1234567890p1"
        << "1234567890p1"
        << true;

    QTest::newRow("DTMF 6")
        << "1234567890X1"
        << "1234567890p1"       // 'X' is converted to 'p'
        << "1234567890"
        << "1234567890p1"
        << "1234567890p1"
        << true;

    QTest::newRow("DTMF 7")
        << "1234567890#1"
        << "1234567890#1"
        << "1234567890"
        << "1234567890#1"
        << "1234567890#1"
        << true;

    QTest::newRow("DTMF 8")
        << "1234567890*1"
        << "1234567890*1"
        << "1234567890"
        << "1234567890*1"
        << "1234567890*1"
        << true;

    QTest::newRow("DTMF 9")
        << "1234567890w1p2x3#4*5"
        << "1234567890w1p2p3#4*5"
        << "1234567890"
        << "1234567890w1p2p3#4*5"
        << "1234567890w1p2p3#4*5"
        << true;

    QTest::newRow("DTMF 10")
        << " 1234567890 w1 p2 (x3) #4*5  "
        << "1234567890 w1 p2 (p3) #4*5"
        << "1234567890"
        << "1234567890w1p2p3#4*5"
        << "1234567890w1p2p3#4*5"
        << true;

    QTest::newRow("invalid DTMF 1")
        << "w12345"
        << "12345"
        << "12345"
        << "12345"
        << "12345"
        << false;

    QTest::newRow("invalid DTMF 2")
        << "w1p2x3"
        << "1p2p3"
        << "1"
        << "1p2p3"
        << "1p2p3"
        << false;

    QTest::newRow("invalid DTMF 3")
        << "1W2Y3"
        << "1W23"
        << "1"
        << "1W23"
        << "1W23"
        << false;

    QTest::newRow("invalid DTMF 4")
        << "Cowpox"
        << ""
        << ""
        << ""
        << ""
        << false;

    QTest::newRow("questionable DTMF") // Currently permitted
        << "*123#456"
        << "*123#456"
        << ""
        << "*123#456"
        << "*123#456"
        << true;

    QTest::newRow("control code 1")
        << "123 #31# 456"
        << "123 #31# 456"
        << "123"
        << "123#31#456"
        << "123#31#456"
        << true;

    QTest::newRow("control code 2")
        << "123 *31# 456"
        << "123 *31# 456"
        << "123"
        << "123*31#456"
        << "123*31#456"
        << true;

    QTest::newRow("control code 3")
        << "123# 3 (1# [456"
        << "123# 3 (1# [456"
        << "123"
        << "123#31#456"
        << "123#31#456"
        << true;

    QTest::newRow("control code 4")
        << "#31#-123456"
        << "#31#-123456"
        << ""
        << "#31#123456"
        << "#31#123456"
        << true;

    QTest::newRow("control code 5")
        << "*31# +123456"
        << "*31# +123456"
        << ""
        << "*31#+123456"
        << "*31#+123456"
        << true;

    QTest::newRow("control code 6")
        << "*31#123456#31#789"
        << "*31#123456#31#789"
        << ""
        << "*31#123456#31#789"
        << "*31#123456#31#789"
        << true;
}

void tst_PhoneNumber::normalization()
{
    QFETCH(QString, number);
    QFETCH(QString, keepAll);
    QFETCH(QString, keepPunctuation);
    QFETCH(QString, keepDialString);
    QFETCH(QString, normalized);
    QFETCH(bool, valid);

    using namespace QtContactsSqliteExtensions;

    QCOMPARE(normalizePhoneNumber(number, KeepPhoneNumberPunctuation | KeepPhoneNumberDialString), keepAll);
    QCOMPARE(normalizePhoneNumber(number, KeepPhoneNumberPunctuation), keepPunctuation);
    QCOMPARE(normalizePhoneNumber(number, KeepPhoneNumberDialString), keepDialString);
    QCOMPARE(normalizePhoneNumber(number, KeepPhoneNumberPunctuation | KeepPhoneNumberDialString | ValidatePhoneNumber).isEmpty(), !valid);

    for (int i = -1; i <= 1; ++i) {
        size_t maxLen(DefaultMaximumPhoneNumberCharacters + i);
        QVERIFY(normalized.endsWith(minimizePhoneNumber(number, maxLen)));
    }
}

QTEST_GUILESS_MAIN(tst_PhoneNumber)
#include "tst_phonenumber.moc"
