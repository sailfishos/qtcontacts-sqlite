/*
 * Copyright (c) 2019 Open Mobile Platform LLC.
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

#include <QtGlobal>

#include <QtTest/QtTest>

#include <QContactManager>
#include <QContact>
#include <QContactName>
#include <QContactDisplayLabel>
#include <QContactPhoneNumber>
#include <QContactEmailAddress>
#include <QContactHobby>
#include <QContactOrganization>

#include "contactmanagerengine.h"

#include "qtcontacts-extensions.h"
#include "qtcontacts-extensions_manager_impl.h"
#include "qcontactsearchfilterrequest.h"
#include "qcontactsearchfilterrequest_impl.h"

QTCONTACTS_USE_NAMESPACE

Q_DECLARE_METATYPE(QList<QContactId>)
Q_DECLARE_METATYPE(QList<QContact>)
Q_DECLARE_METATYPE(QList<QContactSearchFilterRequest::SearchFilter>)

class tst_SearchFilterRequest : public QObject
{
    Q_OBJECT

public:
    tst_SearchFilterRequest();
    ~tst_SearchFilterRequest();

public slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

private slots:
    void testSearchFilterRequest_data();
    void testSearchFilterRequest();

private:
    QContactManager *m_cm;
    QSet<QContactId> m_createdIds;
};

namespace {
    QList<QContact> testContacts() {
        QContact c1, c2, c3, c4, c5, c6;
        QContactName n1, n2, n3, n4, n5, n6;
        QContactPhoneNumber p1, p2, p3, p4, p5, p6;
        QContactEmailAddress e1, e2, e3, e4, e5, e6;
        QContactHobby h1, h2, h3, h4, h5, h6;
        QContactOrganization o1, o2, o3, o4, o5, o6;

        n1.setLastName("Anderson");
        n1.setFirstName("Arnold");
        p1.setNumber("12345678");
        e1.setEmailAddress("arnold@anderson.tld");
        h1.setHobby("Trampoline Bouncing");
        o1.setName("Exercise Inc.");
        c1.saveDetail(&n1);
        c1.saveDetail(&p1);
        c1.saveDetail(&e1);
        c1.saveDetail(&h1);
        c1.saveDetail(&o1);

        n2.setLastName("Brokk");
        n2.setFirstName("Bradley");
        p2.setNumber("22345678");
        e2.setEmailAddress("bradley@brokk.tld");
        h2.setHobby("Cricket");
        o2.setName("Cricket Australia");
        c2.saveDetail(&n2);
        c2.saveDetail(&p2);
        c2.saveDetail(&e2);
        c2.saveDetail(&h2);
        c2.saveDetail(&o2);

        n3.setLastName("Crocket");
        n3.setFirstName("Charlie");
        p3.setNumber("33345678");
        e3.setEmailAddress("charlie@crocket.tld");
        h3.setHobby("Badminton");
        o3.setName("Fishy Business");
        c3.saveDetail(&n3);
        c3.saveDetail(&p3);
        c3.saveDetail(&e3);
        c3.saveDetail(&h3);
        c3.saveDetail(&o3);

        n4.setLastName("Dulth");
        n4.setFirstName("Daniel");
        p4.setNumber("44445678");
        e4.setEmailAddress("daniel@dulth.tld");
        h4.setHobby("Eating");
        o4.setName("Aromatic Foods Inc.");
        c4.saveDetail(&n4);
        c4.saveDetail(&p4);
        c4.saveDetail(&e4);
        c4.saveDetail(&h4);
        c4.saveDetail(&o4);

        n5.setLastName("Epping");
        n5.setFirstName("Finn");
        p5.setNumber("55555678");
        e5.setEmailAddress("finn@epping.tld");
        h5.setHobby("Dance");
        o5.setName("Unemployed");
        c5.saveDetail(&n5);
        c5.saveDetail(&p5);
        c5.saveDetail(&e5);
        c5.saveDetail(&h5);
        c5.saveDetail(&o5);

        n6.setLastName("Farrell");
        n6.setFirstName("Ernest");
        p6.setNumber("66666678");
        e6.setEmailAddress("ernest@farrell.tld");
        h6.setHobby("Bungie Jumping");
        o6.setName("Bungie Experiences Inc");
        c6.saveDetail(&n6);
        c6.saveDetail(&p6);
        c6.saveDetail(&e6);
        c6.saveDetail(&h6);
        c6.saveDetail(&o6);

        QList<QContact> retn;
        retn << c1 << c2 << c3 << c4 << c5 << c6;
        return retn;
    }
}

tst_SearchFilterRequest::tst_SearchFilterRequest()
{
    qRegisterMetaType<QContactId>("QContactId");
    qRegisterMetaType<QList<QContactId> >("QList<QContactId>");

    QMap<QString, QString> parameters;
    parameters.insert(QString::fromLatin1("autoTest"), QString::fromLatin1("true"));
    parameters.insert(QString::fromLatin1("mergePresenceChanges"), QString::fromLatin1("true"));
    m_cm = new QContactManager(QString::fromLatin1("org.nemomobile.contacts.sqlite"), parameters);
    QTest::qWait(250); // creating self contact etc will cause some signals to be emitted.  ignore them.
    connect(m_cm, &QContactManager::contactsAdded, [this] (const QList<QContactId> &ids) {
        for (const QContactId &id : ids) {
            this->m_createdIds.insert(id);
        }
    });
}

tst_SearchFilterRequest::~tst_SearchFilterRequest()
{
    QTest::qWait(250); // wait for signals.
    if (!m_createdIds.isEmpty()) {
        m_cm->removeContacts(m_createdIds.toList());
        m_createdIds.clear();
    }
    delete m_cm;
}

void tst_SearchFilterRequest::initTestCase()
{
}

void tst_SearchFilterRequest::init()
{
}

void tst_SearchFilterRequest::cleanupTestCase()
{
    QTest::qWait(250); // wait for signals.
    if (!m_createdIds.isEmpty()) {
        m_cm->removeContacts(m_createdIds.toList());
        m_createdIds.clear();
    }
}

void tst_SearchFilterRequest::cleanup()
{
    QTest::qWait(250); // wait for signals.
    if (!m_createdIds.isEmpty()) {
        m_cm->removeContacts(m_createdIds.toList());
        m_createdIds.clear();
    }
}

void tst_SearchFilterRequest::testSearchFilterRequest_data()
{
    QTest::addColumn<QList<QContact> >("contacts");
    QTest::addColumn<QList<QContactSearchFilterRequest::SearchFilter> >("searchFilters");
    QTest::addColumn<QString>("searchFilterValue");
    QTest::addColumn<QList<int> >("expected");

    const QList<QContact> contacts = testContacts();

    //--------------

    QContactSearchFilterRequest::SearchField firstNameField;
    firstNameField.detailType = QContactName::Type;
    firstNameField.field = QContactName::FieldFirstName;

    QContactSearchFilterRequest::SearchField lastNameField;
    lastNameField.detailType = QContactName::Type;
    lastNameField.field = QContactName::FieldLastName;

    QContactSearchFilterRequest::SearchField phoneField;
    phoneField.detailType = QContactPhoneNumber::Type;
    phoneField.field = QContactPhoneNumber::FieldNumber;

    QContactSearchFilterRequest::SearchField emailField;
    emailField.detailType = QContactEmailAddress::Type;
    emailField.field = QContactEmailAddress::FieldEmailAddress;

    QContactSearchFilterRequest::SearchField hobbyField;
    hobbyField.detailType = QContactHobby::Type;
    hobbyField.field = QContactHobby::FieldHobby;

    //--------------

    QContactSearchFilterRequest::SearchFilter flnswFilter;
    flnswFilter.fields.append(firstNameField);
    flnswFilter.fields.append(lastNameField);
    flnswFilter.matchFlags = QContactFilter::MatchStartsWith | QContactFilter::MatchFixedString;

    QContactSearchFilterRequest::SearchFilter flncFilter;
    flncFilter.fields.append(firstNameField);
    flncFilter.fields.append(lastNameField);
    flncFilter.matchFlags = QContactFilter::MatchContains | QContactFilter::MatchFixedString;

    QContactSearchFilterRequest::SearchFilter fnswFilter;
    fnswFilter.fields.append(firstNameField);
    fnswFilter.matchFlags = QContactFilter::MatchStartsWith | QContactFilter::MatchFixedString;

    QContactSearchFilterRequest::SearchFilter lnswFilter;
    lnswFilter.fields.append(lastNameField);
    lnswFilter.matchFlags = QContactFilter::MatchStartsWith | QContactFilter::MatchFixedString;

    QContactSearchFilterRequest::SearchFilter hswFilter;
    hswFilter.fields.append(hobbyField);
    hswFilter.matchFlags = QContactFilter::MatchStartsWith | QContactFilter::MatchFixedString;

    QContactSearchFilterRequest::SearchFilter hcFilter;
    hcFilter.fields.append(hobbyField);
    hcFilter.matchFlags = QContactFilter::MatchContains | QContactFilter::MatchFixedString;

    QContactSearchFilterRequest::SearchFilter anycFilter;
    anycFilter.fields.append(firstNameField);
    anycFilter.fields.append(lastNameField);
    anycFilter.fields.append(phoneField);
    anycFilter.fields.append(emailField);
    anycFilter.fields.append(hobbyField);
    anycFilter.matchFlags = QContactFilter::MatchContains | QContactFilter::MatchFixedString;

    //--------------

    // c4 has Daniel Dulth, c1 has Arnold, c2 has Bradley.
    QList<QContactSearchFilterRequest::SearchFilter> FnswLnswFlnc;
    FnswLnswFlnc << fnswFilter << lnswFilter << flncFilter;
    QTest::newRow("first name starts with, last name starts with, first or last name contains D")
        << contacts << FnswLnswFlnc << "D" << (QList<int>() << 3 << 0 << 1);

    // c4 has Daniel Dulth, c1 has Arnold, c2 has Bradley, c5 has Dance, c3 has Badminton.
    QList<QContactSearchFilterRequest::SearchFilter> FnswLnswFlncHswHc;
    FnswLnswFlncHswHc << fnswFilter << lnswFilter << flncFilter << hswFilter << hcFilter;
    QTest::newRow("first name starts with, last name starts with, first or last name contains, hobby starts with, hobby contains D")
        << contacts << FnswLnswFlncHswHc << "D" << (QList<int>() << 3 << 0 << 1 << 4 << 2);

    // c4 has Daniel Dulth, c1 has Arnold, c2 has Bradley, c5 has Dance, c3 has Badminton, c6 has ernest@farrell.tld.
    QList<QContactSearchFilterRequest::SearchFilter> FnswLnswFlncHswHcAnyc;
    FnswLnswFlncHswHcAnyc << fnswFilter << lnswFilter << flncFilter << hswFilter << hcFilter << anycFilter;
    QTest::newRow("first name starts with, last name starts with, first or last name contains, hobby starts with, hobby contains, any contains D")
        << contacts << FnswLnswFlncHswHcAnyc << "D" << (QList<int>() << 3 << 0 << 1 << 4 << 2 << 5);

    // c6 has Ernest Farrell,c5 has Finn Epping, c4 has Eating.
    QList<QContactSearchFilterRequest::SearchFilter> FnswLnswHsw;
    FnswLnswHsw << fnswFilter << lnswFilter << hswFilter;
    QTest::newRow("first name starts with, last name starts with, hobby starts with E")
        << contacts << FnswLnswHsw << "E" << (QList<int>() << 5 << 4 << 3);

    // c6 has Ernest Farrell, c5 has Finn Epping.
    QList<QContactSearchFilterRequest::SearchFilter> LnswFnswHc;
    LnswFnswHc << lnswFilter << fnswFilter << hcFilter;
    QTest::newRow("last name starts with, first name starts with, hobby contains F")
        << contacts << LnswFnswHc << "F" << (QList<int>() << 5 << 4);

    // c5 has Finn Epping, c6 has Ernest Farrell.
    QList<QContactSearchFilterRequest::SearchFilter> FnswLnswHc;
    FnswLnswHc << fnswFilter << lnswFilter << hcFilter;
    QTest::newRow("first name starts with, last name starts with, hobby contains F")
        << contacts << FnswLnswHc << "F" << (QList<int>() << 4 << 5);

    // the sort order of this one is undefined, as either result could sort before the other.
    // here we assume that it will be returned in save order, which works for this simple
    // test case but will not be true in general.
    QList<QContactSearchFilterRequest::SearchFilter> FlnswHc;
    FlnswHc << flnswFilter << hcFilter;
    QTest::newRow("first or last name starts with, hobby contains F")
        << contacts << FlnswHc << "F" << (QList<int>() << -4 << -5);

    // here we pass in the "default" search filters, which is what clients will typically want to use.
    // c2 has Bradley, c6 has Bungie experiences, c3 has fishy Business
    QTest::newRow("default search filters, B")
        << contacts << QContactSearchFilterRequest::defaultSearchFilters() << "B" << (QList<int>() << 1 << 5 << 2);

    // here we pass in the "default" search filters, which is what clients will typically want to use.
    // c6 has Ernest, c5 has Epping, c1 has Exercise Inc, c2 has bradlEy, c3 has charliE, c4 has daniEl.
    QTest::newRow("default search filters, E")
        << contacts << QContactSearchFilterRequest::defaultSearchFilters() << "E" << (QList<int>() << 5 << 4 << 0 << -1 << -2 << -3);

    // here we pass in the "default" search filters, which is what clients will typically want to use.
    // c2 has "22345678", c1 has "12345678"
    QTest::newRow("default search filters, 2")
        << contacts << QContactSearchFilterRequest::defaultSearchFilters() << "2" << (QList<int>() << 1 << 0);
}

void tst_SearchFilterRequest::testSearchFilterRequest()
{
    QFETCH(QList<QContact>, contacts);
    QFETCH(QList<QContactSearchFilterRequest::SearchFilter>, searchFilters);
    QFETCH(QString, searchFilterValue);
    QFETCH(QList<int>, expected);

    QHash<int, QContact> storedContacts;
    for (int i = 0; i < contacts.size(); ++i) {
        QContact saveC = contacts.at(i);
        QVERIFY(m_cm->saveContact(&saveC));
        storedContacts.insert(i, saveC);
    }

    QContactSearchFilterRequest *req = new QContactSearchFilterRequest(this);
    req->setManager(m_cm);
    req->setSearchFilters(searchFilters);
    req->setSearchFilterValue(searchFilterValue);
    req->start();
    req->waitForFinished();

    QList<QContact> results = req->contacts();
    QCOMPARE(results.size(), expected.size());

    auto buildExpectedName = [storedContacts](int expectedIndex) {
        const QString expectedFirst = storedContacts.value(expectedIndex).detail<QContactName>().value(QContactName::FieldFirstName).toString();
        const QString expectedLast = storedContacts.value(expectedIndex).detail<QContactName>().value(QContactName::FieldLastName).toString();
        return QStringLiteral("%1 %2").arg(expectedFirst, expectedLast);
    };

    auto buildActualName = [results](int index) {
        const QString actualFirst = results.at(index).detail<QContactName>().value(QContactName::FieldFirstName).toString();
        const QString actualLast = results.at(index).detail<QContactName>().value(QContactName::FieldLastName).toString();
        return QStringLiteral("%1 %2").arg(actualFirst, actualLast);
    };

    QStringList unorderedExpectedNames;
    QStringList unorderedActualNames;
    for (int i = 0; i < expected.size(); ++i) {
        const int expectedIndex = expected[i];
        if (expectedIndex < 0) {
            // we use negative values to notify that the order is undefined.
            // in this case, just capture the values without requiring order.
            unorderedExpectedNames.append(buildExpectedName(qAbs(expectedIndex)));
            unorderedActualNames.append(buildActualName(i));
        } else {
            const QString expectedName = buildExpectedName(expectedIndex);
            const QString actualName = buildActualName(i);
            if (actualName != expectedName) {
                qWarning() << "Not matching at index" << i;
                QCOMPARE(actualName, expectedName);
            }
        }
    }

    // ensure that the unordered expectations are found in actual.
    QCOMPARE(unorderedActualNames.size(), unorderedExpectedNames.size());
    for (const QString &expectedName : unorderedExpectedNames) {
        QVERIFY(unorderedActualNames.contains(expectedName));
    }
}

QTEST_MAIN(tst_SearchFilterRequest)
#include "tst_searchfilterrequest.moc"
