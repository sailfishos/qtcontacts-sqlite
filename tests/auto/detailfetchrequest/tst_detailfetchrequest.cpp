/*
 * Copyright (C) 2019 Open Mobile Platform LLC.
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

#include "qtcontacts-extensions.h"
#include "qtcontacts-extensions_manager_impl.h"
#include "qcontactdetailfetchrequest.h"
#include "qcontactdetailfetchrequest_impl.h"

QTCONTACTS_USE_NAMESPACE

Q_DECLARE_METATYPE(QList<QContactId>)

class tst_DetailFetchRequest : public QObject
{
    Q_OBJECT

public:
    tst_DetailFetchRequest();
    ~tst_DetailFetchRequest();

public slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

private slots:
    void testDetailFetchRequest();

private:
    QContactManager *m_cm;
    QSet<QContactId> m_createdIds;
};

tst_DetailFetchRequest::tst_DetailFetchRequest()
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

tst_DetailFetchRequest::~tst_DetailFetchRequest()
{
    QTest::qWait(250); // wait for signals.
    if (!m_createdIds.isEmpty()) {
        m_cm->removeContacts(m_createdIds.toList());
        m_createdIds.clear();
    }
    delete m_cm;
}

void tst_DetailFetchRequest::initTestCase()
{
}

void tst_DetailFetchRequest::init()
{
}

void tst_DetailFetchRequest::cleanupTestCase()
{
    QTest::qWait(250); // wait for signals.
    if (!m_createdIds.isEmpty()) {
        m_cm->removeContacts(m_createdIds.toList());
        m_createdIds.clear();
    }
}

void tst_DetailFetchRequest::cleanup()
{
    QTest::qWait(250); // wait for signals.
    if (!m_createdIds.isEmpty()) {
        m_cm->removeContacts(m_createdIds.toList());
        m_createdIds.clear();
    }
}

void tst_DetailFetchRequest::testDetailFetchRequest()
{
    QContact c1, c2, c3;
    QContactName n1, n2, n3;
    QContactDisplayLabel d1, d2, d3;
    QContactPhoneNumber p1, p2, p3;
    QContactEmailAddress e1, e2, e3;
    QContactHobby h1, h2, h3;

    n1.setLastName("Angry");
    n1.setFirstName("Aardvark");
    d1.setLabel("Test A Contact");
    p1.setNumber("11111111");
    e1.setEmailAddress("angry@aardvark.tld");
    h1.setHobby("Acting");
    c1.saveDetail(&n1);
    c1.saveDetail(&d1);
    c1.saveDetail(&p1);
    c1.saveDetail(&e1);
    c1.saveDetail(&h1);

    n2.setLastName("Brigand");
    n2.setFirstName("Bradley");
    d2.setLabel("Test B Contact");
    p2.setNumber("22222222");
    e2.setEmailAddress("bradley@brigand.tld");
    h2.setHobby("Bungee");
    c2.saveDetail(&n2);
    c2.saveDetail(&d2);
    c2.saveDetail(&p2);
    c2.saveDetail(&e2);
    c2.saveDetail(&h2);

    n3.setLastName("Crispy");
    n3.setFirstName("Chip");
    d3.setLabel("Test C Contact");
    p3.setNumber("33333333");
    e3.setEmailAddress("chip@crispy.tld");
    h3.setHobby("Cooking");
    c3.saveDetail(&n3);
    c3.saveDetail(&d3);
    c3.saveDetail(&p3);
    c3.saveDetail(&e3);
    c3.saveDetail(&h3);

    // store the first two contacts to the database
    QVERIFY(m_cm->saveContact(&c1));
    QVERIFY(m_cm->saveContact(&c2));

    // perform a detail fetch request query and ensure we get the details we expect
    QContactSortOrder ascHobbySort;
    ascHobbySort.setDetailType(QContactHobby::Type, QContactHobby::FieldHobby);
    ascHobbySort.setDirection(Qt::AscendingOrder);
    QContactDetailFetchRequest *dfr = new QContactDetailFetchRequest;
    dfr->setManager(m_cm);
    dfr->setType(QContactHobby::Type);
    dfr->setSorting(QList<QContactSortOrder>() << ascHobbySort);
    dfr->start();
    QVERIFY(dfr->waitForFinished(5000));
    QList<QContactDetail> hobbies = dfr->details();

    // ensure that the returned details includes just h1, h2
    // and that they are returned in that (ascending) order.
    QCOMPARE(hobbies.size(), 2);
    QCOMPARE(hobbies[0].type(), QContactHobby::Type);
    QCOMPARE(hobbies[0].value(QContactHobby::FieldHobby).toString(), h1.hobby());
    QCOMPARE(hobbies[1].type(), QContactHobby::Type);
    QCOMPARE(hobbies[1].value(QContactHobby::FieldHobby).toString(), h2.hobby());

    // store the third contact to the database
    QVERIFY(m_cm->saveContact(&c3));

    // perform another detail fetch request
    // this time with a different sort order
    QContactSortOrder dscHobbySort;
    dscHobbySort.setDetailType(QContactHobby::Type, QContactHobby::FieldHobby);
    dscHobbySort.setDirection(Qt::DescendingOrder);
    dfr->setSorting(QList<QContactSortOrder>() << dscHobbySort);
    dfr->start();
    QVERIFY(dfr->waitForFinished(5000));
    hobbies = dfr->details();

    // ensure that the returned details includes h3, h2, h1.
    QCOMPARE(hobbies.size(), 3);
    QCOMPARE(hobbies[0].type(), QContactHobby::Type);
    QCOMPARE(hobbies[0].value(QContactHobby::FieldHobby).toString(), h3.hobby());
    QCOMPARE(hobbies[1].type(), QContactHobby::Type);
    QCOMPARE(hobbies[1].value(QContactHobby::FieldHobby).toString(), h2.hobby());
    QCOMPARE(hobbies[2].type(), QContactHobby::Type);
    QCOMPARE(hobbies[2].value(QContactHobby::FieldHobby).toString(), h1.hobby());
}

QTEST_MAIN(tst_DetailFetchRequest)
#include "tst_detailfetchrequest.moc"
