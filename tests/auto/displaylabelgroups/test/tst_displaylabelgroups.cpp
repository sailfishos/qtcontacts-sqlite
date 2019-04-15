/*
 * Copyright (C) 2019 Jolla Ltd. <chris.adams@jollamobile.com>
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
#include <QContactHobby>

#include <private/qcontactmanager_p.h>
#include "contactmanagerengine.h"

#include "qtcontacts-extensions.h"

QTCONTACTS_USE_NAMESPACE

Q_DECLARE_METATYPE(QList<QContactId>)

class tst_DisplayLabelGroups : public QObject
{
    Q_OBJECT

public:
    tst_DisplayLabelGroups();
    ~tst_DisplayLabelGroups();

public slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

private slots:
    void testDisplayLabelGroups();

private:
    QContactManager *m_cm;
    QSet<QContactId> m_createdIds;
};

tst_DisplayLabelGroups::tst_DisplayLabelGroups()
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

tst_DisplayLabelGroups::~tst_DisplayLabelGroups()
{
    QTest::qWait(250); // wait for signals.
    if (!m_createdIds.isEmpty()) {
        m_cm->removeContacts(m_createdIds.toList());
        m_createdIds.clear();
    }
    delete m_cm;
}

void tst_DisplayLabelGroups::initTestCase()
{
}

void tst_DisplayLabelGroups::init()
{
}

void tst_DisplayLabelGroups::cleanupTestCase()
{
    QTest::qWait(250); // wait for signals.
    if (!m_createdIds.isEmpty()) {
        m_cm->removeContacts(m_createdIds.toList());
        m_createdIds.clear();
    }
}

void tst_DisplayLabelGroups::cleanup()
{
    QTest::qWait(250); // wait for signals.
    if (!m_createdIds.isEmpty()) {
        m_cm->removeContacts(m_createdIds.toList());
        m_createdIds.clear();
    }
}

#define DETERMINE_ACTUAL_ORDER_AND_GROUPS \
    do { \
        actualOrder.clear(); \
        actualGroups.clear(); \
        for (const QContact &c : sorted) { \
            actualOrder += c.detail<QContactPhoneNumber>().number(); \
            if (c.detail<QContactPhoneNumber>().number().isEmpty()) { \
                actualOrder += c.detail<QContactHobby>().hobby(); \
            } \
            actualGroups += c.detail<QContactDisplayLabel>().value(QContactDisplayLabel__FieldLabelGroup).toString(); \
        } \
    } while (0)

void tst_DisplayLabelGroups::testDisplayLabelGroups()
{
    // this test relies on the display label grouping
    // semantics provided by the testdlggplugin.

    // create some contacts
    QContact c1, c2, c3, c4, c5, c6, c7, c8, c9;
    QContactName n1, n2, n3, n4, n5, n6, n7, n8, n9;
    QContactDisplayLabel d1, d2, d3, d4, d5, d6, d7, d8, d9;
    QContactPhoneNumber p1, p2, p3, p4, p5, p6, p7, p8, p9;

    n1.setLastName("A"); // length=1, so group='1'
    n1.setFirstName("Test");
    d1.setLabel("Test A Contact");
    p1.setNumber("1");
    c1.saveDetail(&n1);
    c1.saveDetail(&d1);
    c1.saveDetail(&p1);

    n2.setLastName("BBBBB"); // length=5, so group='5'
    n2.setFirstName("Test");
    d2.setLabel("Test B Contact");
    p2.setNumber("2");
    c2.saveDetail(&n2);
    c2.saveDetail(&d2);
    c2.saveDetail(&p2);

    n3.setLastName("CCCCCCCC"); // length=8, so group='E'
    n3.setFirstName("Test");
    d3.setLabel("Test C Contact");
    p3.setNumber("3");
    c3.saveDetail(&n3);
    c3.saveDetail(&d3);
    c3.saveDetail(&p3);

    n4.setLastName("DDDDDDD"); // length=7, so group='O'
    n4.setFirstName("Test");
    d4.setLabel("Test D Contact");
    p4.setNumber("4");
    c4.saveDetail(&n4);
    c4.saveDetail(&d4);
    c4.saveDetail(&p4);

    n5.setLastName("EEE"); // length=3, so group='3'
    n5.setFirstName("Test");
    d5.setLabel("Test E Contact");
    p5.setNumber("5");
    c5.saveDetail(&n5);
    c5.saveDetail(&d5);
    c5.saveDetail(&p5);

    n6.setLastName(""); // length=0, so group='Z'
    n6.setFirstName("");
    d6.setLabel("");
    p6.setNumber("");
    c6.saveDetail(&n6);
    c6.saveDetail(&d6);
    c6.saveDetail(&p6);
    // phone number can be used to generate a display label
    // so don't use that.  but hobby will not!  so use that.
    QContactHobby h6;
    h6.setHobby("6");
    c6.saveDetail(&h6);

    n7.setLastName("GGGGGG"); // length=6, so group='E'
    n7.setFirstName("Aardvark");  // should first-name sort before c3 and c7.
    d7.setLabel("Test G Contact");
    p7.setNumber("7");
    c7.saveDetail(&n7);
    c7.saveDetail(&d7);
    c7.saveDetail(&p7);

    n8.setLastName("HHHH"); // length=4, so group='4'
    n8.setFirstName("Test");
    d8.setLabel("Test H Contact");
    p8.setNumber("8");
    c8.saveDetail(&n8);
    c8.saveDetail(&d8);
    c8.saveDetail(&p8);

    n9.setLastName("CCCCCCCC"); // length = 8, so group='E'; same as c3.
    n9.setFirstName("Abel");  // should first-name sort before c3 but after c7.
    d9.setLabel("Test I Contact");
    p9.setNumber("9");
    c9.saveDetail(&n9);
    c9.saveDetail(&d9);
    c9.saveDetail(&p9);

    // store them to the database
    QVERIFY(m_cm->saveContact(&c1));
    QVERIFY(m_cm->saveContact(&c2));
    QVERIFY(m_cm->saveContact(&c3));
    QVERIFY(m_cm->saveContact(&c4));
    QVERIFY(m_cm->saveContact(&c5));
    QVERIFY(m_cm->saveContact(&c6));
    QVERIFY(m_cm->saveContact(&c7));
    QVERIFY(m_cm->saveContact(&c8));
    QVERIFY(m_cm->saveContact(&c9));

    // Ensure that they sort as we expect the test plugin to sort them.
    // Note that because we only have a single sort order defined,
    // any contacts which have the same display label group
    // may be returned in any order by the backend.
    QContactSortOrder displayLabelGroupSort;
    displayLabelGroupSort.setDetailType(QContactDisplayLabel::Type, QContactDisplayLabel__FieldLabelGroup);
    QList<QContact> sorted = m_cm->contacts(displayLabelGroupSort);
    QString actualOrder, actualGroups;
    DETERMINE_ACTUAL_ORDER_AND_GROUPS;
    // fixup for potential ambiguity in sort order.  3, 7 and 9 all sort equally.
    actualOrder.replace(QChar('7'), QChar('3'));
    actualOrder.replace(QChar('9'), QChar('3'));
    QCOMPARE(actualOrder,  QStringLiteral("615824333"));
    QCOMPARE(actualGroups, QStringLiteral("Z1345OEEE"));

    // Now sort by display label group followed by last name.
    // We expect the same sorting as display-group-only sorting,
    // except that contact 9's last name causes it to be sorted before contact 7.
    // The ordering between 3 and 9 is not disambiguated by the sort order.
    QContactSortOrder lastNameSort;
    lastNameSort.setDetailType(QContactName::Type, QContactName::FieldLastName);
    sorted = m_cm->contacts(QList<QContactSortOrder>() << displayLabelGroupSort << lastNameSort);
    DETERMINE_ACTUAL_ORDER_AND_GROUPS;
    // fixup for potential ambiguity in sort order.  3 and 9 sort equally.
    actualOrder.replace(QChar('9'), QChar('3'));
    QCOMPARE(actualOrder,  QStringLiteral("615824337"));
    QCOMPARE(actualGroups, QStringLiteral("Z1345OEEE"));

    // Now sort by display label group followed by first name.
    // We expect the same sorting as display-group-only sorting,
    // except that contact 7's first name causes it to be sorted before contact 3 and contact 9,
    // and contact 9's first name causes it to be sorted before contact 3.
    QContactSortOrder firstNameSort;
    firstNameSort.setDetailType(QContactName::Type, QContactName::FieldFirstName);
    sorted = m_cm->contacts(QList<QContactSortOrder>() << displayLabelGroupSort << firstNameSort);
    DETERMINE_ACTUAL_ORDER_AND_GROUPS;
    QCOMPARE(actualOrder,  QStringLiteral("615824793"));
    QCOMPARE(actualGroups, QStringLiteral("Z1345OEEE"));

    // Now sort by display label group followed by last name followed by first name.
    // We expect the same sorting as display-group-only sorting,
    // except that contact 9's last name causes it to be sorted before contact 7,
    // and contact 9 should sort before contact 3 due to the first name.
    sorted = m_cm->contacts(QList<QContactSortOrder>() << displayLabelGroupSort << lastNameSort << firstNameSort);
    DETERMINE_ACTUAL_ORDER_AND_GROUPS;
    QCOMPARE(actualOrder,  QStringLiteral("615824937"));
    QCOMPARE(actualGroups, QStringLiteral("Z1345OEEE"));

    // Now add a contact which has a special name such that the test
    // display label group generator plugin will generate a group
    // for it which was previously "unknown".
    // We expect that group to be added before '#' but after other groups.
    typedef QtContactsSqliteExtensions::ContactManagerEngine EngineType;
    EngineType *cme = dynamic_cast<EngineType *>(QContactManagerData::managerData(m_cm)->m_engine);
    const QStringList oldContactDisplayLabelGroups = cme->displayLabelGroups();
    QSignalSpy dlgcSpy(cme, SIGNAL(displayLabelGroupsChanged(QStringList)));

    QContact c10, c11;
    QContactName n10, n11;
    QContactDisplayLabel d10, d11;
    QContactPhoneNumber p10, p11;

    n10.setLastName("10ten"); // first letter is digit, should be in #.
    n10.setFirstName("Ten");
    d10.setLabel("Test J Contact");
    p10.setNumber("J");
    c10.saveDetail(&n10);
    c10.saveDetail(&d10);
    c10.saveDetail(&p10);

    n11.setLastName("tst_displaylabelgroups_unknown_dlg"); // special case, group &.
    n11.setFirstName("Eleven");
    d11.setLabel("Test K Contact");
    p11.setNumber("K");
    c11.saveDetail(&n11);
    c11.saveDetail(&d11);
    c11.saveDetail(&p11);

    QVERIFY(m_cm->saveContact(&c10));
    QVERIFY(m_cm->saveContact(&c11));

    // ensure that the resultant sort order is expected
    sorted = m_cm->contacts(QList<QContactSortOrder>() << displayLabelGroupSort << lastNameSort << firstNameSort);
    DETERMINE_ACTUAL_ORDER_AND_GROUPS;
    QCOMPARE(actualOrder,  QStringLiteral("615824937KJ"));
    QCOMPARE(actualGroups, QStringLiteral("Z1345OEEE&#"));

    // should have received signal that display label groups have changed.
    QTest::qWait(250);
    QCOMPARE(dlgcSpy.count(), 1);
    QStringList expected(oldContactDisplayLabelGroups);
    expected.insert(expected.size() - 1, QStringLiteral("&")); // & group should have been inserted before #.
    QList<QVariant> data = dlgcSpy.takeFirst();
    QCOMPARE(data.first().value<QStringList>(), expected);
}

QTEST_MAIN(tst_DisplayLabelGroups)
#include "tst_displaylabelgroups.moc"
