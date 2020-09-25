/*
 * Copyright (C) 2013 - 2019 Jolla Ltd.
 * Copyright (C) 2020 Open Mobile Platform LLC.
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
#include "qtcontacts-extensions.h"

#include <QLocale>

static const QString aggregatesRelationship(relationshipString(QContactRelationship::Aggregates));

namespace {

static const char *addedColAccumulationSlot = SLOT(addColAccumulationSlot(QList<QContactCollectionId>));
static const char *changedColAccumulationSlot = SLOT(chgColAccumulationSlot(QList<QContactCollectionId>));
static const char *removedColAccumulationSlot = SLOT(remColAccumulationSlot(QList<QContactCollectionId>));
static const char *addedAccumulationSlot = SLOT(addAccumulationSlot(QList<QContactId>));
static const char *changedAccumulationSlot = SLOT(chgAccumulationSlot(QList<QContactId>));
static const char *removedAccumulationSlot = SLOT(remAccumulationSlot(QList<QContactId>));

QString detailProvenance(const QContactDetail &detail)
{
    return detail.value<QString>(QContactDetail::FieldProvenance);
}

QString detailProvenanceContact(const QContactDetail &detail)
{
    // The contact element is the first part up to ':'
    const QString provenance(detailProvenance(detail));
    return provenance.left(provenance.indexOf(QChar::fromLatin1(':')));
}

}

class tst_Aggregation : public QObject
{
    Q_OBJECT

public:
    tst_Aggregation();
    virtual ~tst_Aggregation();

public slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

public slots:
    void addColAccumulationSlot(const QList<QContactCollectionId> &ids);
    void chgColAccumulationSlot(const QList<QContactCollectionId> &ids);
    void remColAccumulationSlot(const QList<QContactCollectionId> &ids);
    void addAccumulationSlot(const QList<QContactId> &ids);
    void chgAccumulationSlot(const QList<QContactId> &ids);
    void remAccumulationSlot(const QList<QContactId> &ids);

private slots:
    void createSingleLocal();
    void createMultipleLocal();
    void createSingleLocalAndSingleSync();
    void createNonAggregable();

    void updateSingleLocal();
    void updateSingleAggregate();
    void updateAggregateOfLocalAndSync();
    void updateAggregateOfLocalAndModifiableSync();

    void compositionPrefersLocal();
    void uniquenessConstraints();

    void removeSingleLocal();
    void removeSingleAggregate();

    void alterRelationships();

    void aggregationHeuristic_data();
    void aggregationHeuristic();

    void regenerateAggregate();

    void detailUris();

    void correctDetails();

    void batchSemantics();

    void customSemantics();

    void changeLogFiltering();

    void deactivationSingle();
    void deactivationMultiple();

    void deletionSingle();
    void deletionMultiple();
    void deletionCollections();

/*
    void testSyncAdapter();
*/

    void testOOB();

private:
    void waitForSignalPropagation();

    QContactManager *m_cm;
    QSet<QContactCollectionId> m_addColAccumulatedIds;
    QSet<QContactCollectionId> m_chgColAccumulatedIds;
    QSet<QContactCollectionId> m_remColAccumulatedIds;
    QSet<QContactCollectionId> m_createdColIds;
    QSet<QContactId> m_addAccumulatedIds;
    QSet<QContactId> m_chgAccumulatedIds;
    QSet<QContactId> m_remAccumulatedIds;
    QSet<QContactId> m_createdIds;

    QByteArray aggregateAddressbookId()
    {
        return QtContactsSqliteExtensions::aggregateCollectionId(m_cm->managerUri()).localId();
    }

    QByteArray localAddressbookId()
    {
        return QtContactsSqliteExtensions::localCollectionId(m_cm->managerUri()).localId();
    }
};

tst_Aggregation::tst_Aggregation()
    : m_cm(0)
{
    QMap<QString, QString> parameters;
    parameters.insert(QString::fromLatin1("autoTest"), QString::fromLatin1("true"));
    parameters.insert(QString::fromLatin1("mergePresenceChanges"), QString::fromLatin1("true"));
    m_cm = new QContactManager(QString::fromLatin1("org.nemomobile.contacts.sqlite"), parameters);

    QTest::qWait(250); // creating self contact etc will cause some signals to be emitted.  ignore them.
    connect(m_cm, collectionsAddedSignal, this, addedColAccumulationSlot);
    connect(m_cm, collectionsChangedSignal, this, changedColAccumulationSlot);
    connect(m_cm, collectionsRemovedSignal, this, removedColAccumulationSlot);
    connect(m_cm, contactsAddedSignal, this, addedAccumulationSlot);
    connect(m_cm, contactsChangedSignal, this, changedAccumulationSlot);
    connect(m_cm, contactsRemovedSignal, this, removedAccumulationSlot);
}

tst_Aggregation::~tst_Aggregation()
{
}

void tst_Aggregation::initTestCase()
{
    registerIdType();

    /* Make sure the DB is empty */
    QContactCollectionFilter allCollections;
    m_cm->removeContacts(m_cm->contactIds(allCollections));
    waitForSignalPropagation();
}

void tst_Aggregation::init()
{
    m_addColAccumulatedIds.clear();
    m_chgColAccumulatedIds.clear();
    m_remColAccumulatedIds.clear();
    m_createdColIds.clear();
    m_addAccumulatedIds.clear();
    m_chgAccumulatedIds.clear();
    m_remAccumulatedIds.clear();
    m_createdIds.clear();
}

void tst_Aggregation::cleanupTestCase()
{
}

void tst_Aggregation::cleanup()
{
    QtContactsSqliteExtensions::ContactManagerEngine *cme = QtContactsSqliteExtensions::contactManagerEngine(*m_cm);
    QContactManager::Error err = QContactManager::NoError;

    waitForSignalPropagation();
    if (!m_createdIds.isEmpty()) {
        // purge them one at a time, to avoid "contacts from different collections in single batch" errors.
        for (const QContactId &cid : m_createdIds) {
            QContact doomed = m_cm->contact(cid);
            if (!doomed.id().isNull() && doomed.collectionId().localId() != aggregateAddressbookId()) {
                if (!m_cm->removeContact(cid)) {
                    qWarning() << "Failed to cleanup:" << QString::fromLatin1(cid.localId());
                }
                cme->clearChangeFlags(QList<QContactId>() << cid, &err);
            }
        }
        m_createdIds.clear();
    }
    if (!m_createdColIds.isEmpty()) {
        for (const QContactCollectionId &colId : m_createdColIds.toList()) {
            m_cm->removeCollection(colId);
            cme->clearChangeFlags(colId, &err);
        }
        m_createdColIds.clear();
    }
    cme->clearChangeFlags(QContactCollectionId(m_cm->managerUri(), localAddressbookId()), &err);
    waitForSignalPropagation();
}

void tst_Aggregation::waitForSignalPropagation()
{
    // Signals are routed via DBUS, so we need to wait for them to arrive
    QTest::qWait(50);
}

void tst_Aggregation::addColAccumulationSlot(const QList<QContactCollectionId> &ids)
{
    foreach (const QContactCollectionId &id, ids) {
        m_addColAccumulatedIds.insert(id);
        m_createdColIds.insert(id);
    }
}

void tst_Aggregation::chgColAccumulationSlot(const QList<QContactCollectionId> &ids)
{
    foreach (const QContactCollectionId &id, ids) {
        m_chgColAccumulatedIds.insert(id);
    }
}

void tst_Aggregation::remColAccumulationSlot(const QList<QContactCollectionId> &ids)
{
    foreach (const QContactCollectionId &id, ids) {
        m_remColAccumulatedIds.insert(id);
    }
}

void tst_Aggregation::addAccumulationSlot(const QList<QContactId> &ids)
{
    foreach (const QContactId &id, ids) {
        m_addAccumulatedIds.insert(id);
        m_createdIds.insert(id);
    }
}

void tst_Aggregation::chgAccumulationSlot(const QList<QContactId> &ids)
{
    foreach (const QContactId &id, ids) {
        m_chgAccumulatedIds.insert(id);
    }
}

void tst_Aggregation::remAccumulationSlot(const QList<QContactId> &ids)
{
    foreach (const QContactId &id, ids) {
        m_remAccumulatedIds.insert(id);
    }
}

void tst_Aggregation::createSingleLocal()
{
    QContactCollectionFilter allCollections;

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allCollections).size();

    // set up some signal spies
    QSignalSpy addSpy(m_cm, contactsAddedSignal);
    int addSpyCount = 0;

    // now add a new local contact (no collection specified == automatically local)
    QContact alice;

    QContactName an;
    an.setFirstName("Alice");
    an.setMiddleName("In");
    an.setLastName("Wonderland");
    alice.saveDetail(&an);

    QContactPhoneNumber aph;
    aph.setNumber("1234567");
    alice.saveDetail(&aph);

    QContactGender ag;
    ag.setGender(QContactGender::GenderFemale);
    alice.saveDetail(&ag);

    m_addAccumulatedIds.clear();

    QVERIFY(m_cm->saveContact(&alice));
    QTRY_VERIFY(addSpy.count() > addSpyCount);
    QTRY_COMPARE(m_addAccumulatedIds.size(), 2); // should have added local + aggregate
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(alice)));
    addSpyCount = addSpy.count();

    QCOMPARE(m_cm->contactIds().size(), aggCount + 1); // 1 extra aggregate contact
    aggCount = m_cm->contactIds().size();
    QCOMPARE(m_cm->contactIds(allCollections).size(), allCount + 2); // should have added local + aggregate
    allCount = m_cm->contactIds(allCollections).size();

    QList<QContact> allContacts = m_cm->contacts(allCollections);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    QContact localAlice;
    QContact aggregateAlice;
    bool foundLocalAlice = false;
    bool foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("1234567")) {
            if (curr.collectionId().localId() == localAddressbookId()) {
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(localAlice.id()));

    // Test the provenance of details
    QContactPhoneNumber localDetail(localAlice.detail<QContactPhoneNumber>());
    QContactPhoneNumber aggregateDetail(aggregateAlice.detail<QContactPhoneNumber>());
    QVERIFY(!detailProvenance(localDetail).isEmpty());
    QCOMPARE(detailProvenance(aggregateDetail), detailProvenance(localDetail));

    // A local contact should have a GUID, which is not promoted to the aggregate
    QVERIFY(!localAlice.detail<QContactGuid>().guid().isEmpty());
    QVERIFY(aggregateAlice.detail<QContactGuid>().guid().isEmpty());

    // Verify that gender is promoted
    QCOMPARE(localAlice.detail<QContactGender>().gender(), QContactGender::GenderFemale);
    QCOMPARE(aggregateAlice.detail<QContactGender>().gender(), QContactGender::GenderFemale);
}

void tst_Aggregation::createMultipleLocal()
{
    QContactCollectionFilter allCollections;

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allCollections).size();

    // set up some signal spies
    QSignalSpy addSpy(m_cm, contactsAddedSignal);
    int addSpyCount = 0;

    // now add two new local contacts (no collectionId specified == automatically local)
    QContact alice;
    QContact bob;

    QContactName an, bn;
    an.setFirstName("Alice2");
    an.setMiddleName("In");
    an.setLastName("Wonderland");
    alice.saveDetail(&an);
    bn.setFirstName("Bob2");
    bn.setMiddleName("The");
    bn.setLastName("Destroyer");
    bob.saveDetail(&bn);

    QContactPhoneNumber aph, bph;
    aph.setNumber("234567");
    alice.saveDetail(&aph);
    bph.setNumber("765432");
    bob.saveDetail(&bph);

    // add an explicit GUID to Bob
    const QString bobGuid("I am Bob");
    QContactGuid bg;
    bg.setGuid(bobGuid);
    bob.saveDetail(&bg);

    QList<QContact> saveList;
    saveList << alice << bob;
    m_addAccumulatedIds.clear();
    QVERIFY(m_cm->saveContacts(&saveList));
    QTRY_VERIFY(addSpy.count() > addSpyCount); // should have added local + aggregate for each
    alice = saveList.at(0); bob = saveList.at(1);
    QTRY_COMPARE(m_addAccumulatedIds.size(), 4);
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(alice)));
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(bob)));
    addSpyCount = addSpy.count();

    QCOMPARE(m_cm->contactIds().size(), aggCount + 2); // 2 extra aggregate contacts
    aggCount = m_cm->contactIds().size();
    QCOMPARE(m_cm->contactIds(allCollections).size(), allCount + 4); // should have added local + aggregate for each
    allCount = m_cm->contactIds(allCollections).size();

    QList<QContact> allContacts = m_cm->contacts(allCollections);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    QContact localAlice;
    QContact localBob;
    QContact aggregateAlice;
    QContact aggregateBob;
    bool foundLocalAlice = false;
    bool foundAggregateAlice = false;
    bool foundLocalBob = false;
    bool foundAggregateBob = false;
    foreach (const QContact &curr, allContacts) {
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice2")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("234567")) {
            if (curr.collectionId().localId() == localAddressbookId()) {
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        } else if (currName.firstName() == QLatin1String("Bob2")
                && currName.middleName() == QLatin1String("The")
                && currName.lastName() == QLatin1String("Destroyer")
                && currPhn.number() == QLatin1String("765432")) {
            if (curr.collectionId().localId() == localAddressbookId()) {
                localBob = curr;
                foundLocalBob = true;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateBob = curr;
                foundAggregateBob = true;
            }
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(foundLocalBob);
    QVERIFY(foundAggregateBob);
    QVERIFY(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(localAlice.id()));
    QVERIFY(!localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateBob.id()));
    QVERIFY(!aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(localBob.id()));
    QVERIFY(localBob.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateBob.id()));
    QVERIFY(aggregateBob.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(localBob.id()));
    QVERIFY(!localBob.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(!aggregateBob.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(localAlice.id()));

    // Test the provenance of details
    QContactPhoneNumber localAliceDetail(localAlice.detail<QContactPhoneNumber>());
    QContactPhoneNumber aggregateAliceDetail(aggregateAlice.detail<QContactPhoneNumber>());
    QVERIFY(!detailProvenance(localAliceDetail).isEmpty());
    QCOMPARE(detailProvenance(aggregateAliceDetail), detailProvenance(localAliceDetail));

    QContactPhoneNumber localBobDetail(localBob.detail<QContactPhoneNumber>());
    QContactPhoneNumber aggregateBobDetail(aggregateBob.detail<QContactPhoneNumber>());
    QVERIFY(!detailProvenance(localBobDetail).isEmpty());
    QCOMPARE(detailProvenance(aggregateBobDetail), detailProvenance(localBobDetail));
    QVERIFY(detailProvenance(localBobDetail) != detailProvenance(localAliceDetail));

    // Verify that the local consituents have GUIDs, but the aggregates don't
    QVERIFY(!localAlice.detail<QContactGuid>().guid().isEmpty());
    QVERIFY(!localBob.detail<QContactGuid>().guid().isEmpty());
    QCOMPARE(localBob.detail<QContactGuid>().guid(), bobGuid);
    QVERIFY(aggregateAlice.detail<QContactGuid>().guid().isEmpty());
    QVERIFY(aggregateBob.detail<QContactGuid>().guid().isEmpty());
}

void tst_Aggregation::createSingleLocalAndSingleSync()
{
    // here we create a local contact, and then save it
    // and then we create a "sync" contact, which should "match" it.
    // It should be related to the aggregate created for the sync.

    QContactCollectionFilter allCollections;

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allCollections).size();

    // set up some signal spies
    QSignalSpy addSpy(m_cm, contactsAddedSignal);
    QSignalSpy chgSpy(m_cm, contactsChangedSignal);
    int addSpyCount = 0;
    int chgSpyCount = 0;

    // now add a new local contact (no collectionId specified == automatically local)
    QContact alice;

    QContactName an;
    an.setFirstName("Alice3");
    an.setMiddleName("In");
    an.setLastName("Wonderland");
    alice.saveDetail(&an);

    QContactPhoneNumber aph;
    aph.setNumber("34567");
    alice.saveDetail(&aph);

    QContactEmailAddress aem;
    aem.setEmailAddress("alice@test.com");
    alice.saveDetail(&aem);

    m_addAccumulatedIds.clear();
    QVERIFY(m_cm->saveContact(&alice));
    QTRY_VERIFY(addSpy.count() > addSpyCount); // should have added local + aggregate
    QTRY_COMPARE(m_addAccumulatedIds.size(), 2);
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(alice)));
    addSpyCount = addSpy.count();

    QCOMPARE(m_cm->contactIds().size(), aggCount + 1); // 1 extra aggregate contact
    aggCount = m_cm->contactIds().size();
    QCOMPARE(m_cm->contactIds(allCollections).size(), allCount + 2); // should have added local + aggregate
    allCount = m_cm->contactIds(allCollections).size();

    QList<QContact> allContacts = m_cm->contacts(allCollections);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    QContact localAlice;
    QContact aggregateAlice;
    bool foundLocalAlice = false;
    bool foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactEmailAddress currEm = curr.detail<QContactEmailAddress>();
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice3")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("34567")
                && currEm.emailAddress() == QLatin1String("alice@test.com")) {
            if (curr.collectionId().localId() == localAddressbookId()) {
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(localAlice.id()));

    // now add the doppleganger from another sync source (remote addressbook)
    QContactCollection remoteAddressbook;
    remoteAddressbook.setMetaData(QContactCollection::KeyName, QStringLiteral("test"));
    remoteAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, "tst_aggregation");
    remoteAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID, 5);
    remoteAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH, "/addressbooks/test");
    QVERIFY(m_cm->saveCollection(&remoteAddressbook));

    QContact syncAlice;
    syncAlice.setCollectionId(remoteAddressbook.id());

    QContactName san;
    san.setFirstName(an.firstName());
    san.setMiddleName(an.middleName());
    san.setLastName(an.lastName());
    syncAlice.saveDetail(&san);

    QContactPhoneNumber saph;
    saph.setNumber(aph.number());
    syncAlice.saveDetail(&saph);

    QContactEmailAddress saem;
    saem.setEmailAddress(aem.emailAddress());
    syncAlice.saveDetail(&saem);

    QContactHobby sah; // this is a "new" detail which doesn't appear in the local contact.
    sah.setHobby(QLatin1String("tennis"));
    syncAlice.saveDetail(&sah);

    QContactSyncTarget sast;
    sast.setSyncTarget(QLatin1String("test"));
    syncAlice.saveDetail(&sast);

    // DON'T clear the m_addAccumulatedIds list here.
    // DO clear the m_chgAccumulatedIds list here, though.
    chgSpyCount = chgSpy.count();
    m_chgAccumulatedIds.clear();
    QVERIFY(m_cm->saveContact(&syncAlice));
    QTRY_VERIFY(addSpy.count() > addSpyCount); // should have added test but not an aggregate - aggregate already exists
    QTRY_VERIFY(chgSpy.count() > chgSpyCount); // should have updated the aggregate
    QTRY_COMPARE(m_addAccumulatedIds.size(), 3);
    QTRY_COMPARE(m_chgAccumulatedIds.size(), 1); // the aggregate should have been updated (with the hobby)
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(localAlice)));
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(aggregateAlice)));
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(syncAlice)));
    QVERIFY(m_chgAccumulatedIds.contains(ContactId::apiId(aggregateAlice)));
    addSpyCount = addSpy.count();

    QCOMPARE(m_cm->contactIds().size(), aggCount); // no extra aggregate contact
    aggCount = m_cm->contactIds().size();
    QCOMPARE(m_cm->contactIds(allCollections).size(), allCount + 1); // should have added test but not an aggregate
    allCount = m_cm->contactIds(allCollections).size();

    allContacts = m_cm->contacts(allCollections);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    QContact testAlice;
    bool foundTestAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactEmailAddress currEm = curr.detail<QContactEmailAddress>();
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice3")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("34567")
                && currEm.emailAddress() == QLatin1String("alice@test.com")) {
            if (curr.collectionId().localId() == localAddressbookId()) {
                localAlice = curr;
                foundLocalAlice = true;
            } else if (curr.collectionId() == remoteAddressbook.id()) {
                testAlice = curr;
                foundTestAlice = true;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundTestAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(testAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(localAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(testAlice.id()));

    // Verify the propagation of details
    QContactHobby localDetail(localAlice.detail<QContactHobby>());
    QContactHobby testDetail(testAlice.detail<QContactHobby>());
    QContactHobby aggregateDetail(aggregateAlice.detail<QContactHobby>());

    QCOMPARE(testDetail.value<QString>(QContactHobby::FieldHobby), QLatin1String("tennis")); // came from here
    QVERIFY(!detailProvenance(testDetail).isEmpty());
    QCOMPARE(aggregateDetail.value<QString>(QContactHobby::FieldHobby), QLatin1String("tennis")); // aggregated to here
    QCOMPARE(detailProvenance(aggregateDetail), detailProvenance(testDetail));
    QCOMPARE(localDetail.value<QString>(QContactHobby::FieldHobby), QString()); // local shouldn't get it
    QVERIFY(detailProvenance(localDetail).isEmpty());
}

void tst_Aggregation::createNonAggregable()
{
    QContactCollectionFilter allCollections;

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allCollections).size();

    // set up some signal spies
    QSignalSpy addSpy(m_cm, contactsAddedSignal);
    int addSpyCount = 0;

    // add a non-aggregable addressbook (e.g. application-specific addressbook).
    QContactCollection testAddressbook;
    testAddressbook.setMetaData(QContactCollection::KeyName, QStringLiteral("test"));
    testAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_AGGREGABLE, false);
    QVERIFY(m_cm->saveCollection(&testAddressbook));

    // now add a new non-aggregable contact
    QContact alice;
    alice.setCollectionId(testAddressbook.id());

    QContactName an;
    an.setFirstName("Alice");
    an.setMiddleName("In");
    an.setLastName("Wonderland");
    alice.saveDetail(&an);

    QContactPhoneNumber aph;
    aph.setNumber("34567");
    alice.saveDetail(&aph);

    QContactEmailAddress aem;
    aem.setEmailAddress("alice@test.com");
    alice.saveDetail(&aem);

    m_addAccumulatedIds.clear();
    QVERIFY(m_cm->saveContact(&alice));
    QTRY_VERIFY(addSpy.count() > addSpyCount);
    QTRY_COMPARE(m_addAccumulatedIds.size(), 1); // just 1, since no aggregate should be generated.
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(alice)));
    addSpyCount = addSpy.count();

    QCOMPARE(m_cm->contactIds().size(), aggCount);
    aggCount = m_cm->contactIds().size();
    QCOMPARE(m_cm->contactIds(allCollections).size(), allCount + 1); // should have added non-aggregable
    allCount = m_cm->contactIds(allCollections).size();

    QList<QContact> allContacts = m_cm->contacts(allCollections);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    QContact testAlice;
    bool foundTestAlice = false;
    bool foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactEmailAddress currEm = curr.detail<QContactEmailAddress>();
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("34567")
                && currEm.emailAddress() == QLatin1String("alice@test.com")) {
            if (curr.collectionId() == testAddressbook.id()) {
                testAlice = curr;
                foundTestAlice = true;
            } else {
                foundAggregateAlice = true;
            }
        }
    }

    QVERIFY(foundTestAlice);
    QVERIFY(!foundAggregateAlice); // should not be found, no aggregate should have been generated for it.
    QCOMPARE(testAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).size(), 0);

    // now add a new local contact (no collectionId specified == automatically local)
    QContact localAlice;

    QContactName lan;
    lan.setFirstName("Alice");
    lan.setMiddleName("In");
    lan.setLastName("Wonderland");
    localAlice.saveDetail(&lan);

    QContactHobby lah;
    lah.setHobby("tennis");
    localAlice.saveDetail(&lah);

    QContactEmailAddress laem;
    laem.setEmailAddress("alice@test.com");
    localAlice.saveDetail(&laem);

    QVERIFY(m_cm->saveContact(&localAlice));
    QTRY_VERIFY(addSpy.count() > addSpyCount); // should have added local + aggregate
    QTRY_COMPARE(m_addAccumulatedIds.size(), 3); // testAlice, localAlice, aggAlice.
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(localAlice)));
    addSpyCount = addSpy.count();

    QCOMPARE(m_cm->contactIds().size(), aggCount + 1); // 1 extra aggregate contact
    aggCount = m_cm->contactIds().size();
    QCOMPARE(m_cm->contactIds(allCollections).size(), allCount + 2); // should have added local + aggregate
    allCount = m_cm->contactIds(allCollections).size();

    allContacts = m_cm->contacts(allCollections);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    QContact locAlice, aggAlice;
    bool foundLocalAlice = false;
    foundTestAlice = false;
    foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactEmailAddress currEm = curr.detail<QContactEmailAddress>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currEm.emailAddress() == QLatin1String("alice@test.com")) {
            if (curr.collectionId() == testAddressbook.id()) {
                testAlice = curr;
                foundTestAlice = true;
            } else if (curr.collectionId().localId() == localAddressbookId()) {
                locAlice = curr;
                foundLocalAlice = true;
            } else {
                aggAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    // ensure that we have now found all contacts
    QVERIFY(foundTestAlice);
    QVERIFY(foundLocalAlice);
    QVERIFY(foundAggregateAlice);

    // ensure the local contact contains the content we expect.
    QCOMPARE(locAlice.detail<QContactName>().firstName(), localAlice.detail<QContactName>().firstName());
    QCOMPARE(locAlice.detail<QContactName>().middleName(), localAlice.detail<QContactName>().middleName());
    QCOMPARE(locAlice.detail<QContactName>().lastName(), localAlice.detail<QContactName>().lastName());
    QCOMPARE(locAlice.detail<QContactEmailAddress>().emailAddress(), localAlice.detail<QContactEmailAddress>().emailAddress());
    QCOMPARE(locAlice.detail<QContactHobby>().hobby(), localAlice.detail<QContactHobby>().hobby());
    QVERIFY(locAlice.detail<QContactPhoneNumber>().number().isEmpty());

    // ensure that the aggregate contact contains the content we expect.
    QCOMPARE(aggAlice.detail<QContactName>().firstName(), localAlice.detail<QContactName>().firstName());
    QCOMPARE(aggAlice.detail<QContactName>().middleName(), localAlice.detail<QContactName>().middleName());
    QCOMPARE(aggAlice.detail<QContactName>().lastName(), localAlice.detail<QContactName>().lastName());
    QCOMPARE(aggAlice.detail<QContactEmailAddress>().emailAddress(), localAlice.detail<QContactEmailAddress>().emailAddress());
    QCOMPARE(aggAlice.detail<QContactHobby>().hobby(), localAlice.detail<QContactHobby>().hobby());
    QVERIFY(aggAlice.detail<QContactPhoneNumber>().number().isEmpty());

    // and that it aggregates only localAlice
    QVERIFY(aggAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(localAlice.id()));
    QVERIFY(!aggAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(testAlice.id()));

    // now modify the local contact; this shouldn't result in testAlice details being aggregated into the aggregate.
    QContactNickname lnick;
    lnick.setNickname("Ally");
    localAlice = locAlice;
    QVERIFY(localAlice.saveDetail(&lnick));
    QVERIFY(m_cm->saveContact(&localAlice));
    aggAlice = m_cm->contact(aggAlice.id());
    QCOMPARE(aggAlice.detail<QContactNickname>().nickname(), localAlice.detail<QContactNickname>().nickname());
    QVERIFY(aggAlice.detail<QContactPhoneNumber>().number().isEmpty());

    // now modify the test contact; this shouldn't result in testAlice details being aggregated into the aggregate.
    QContactAvatar tav;
    tav.setImageUrl(QUrl(QStringLiteral("img://alice.in.wonderland.tld/avatar.png")));
    QVERIFY(testAlice.saveDetail(&tav));
    QVERIFY(m_cm->saveContact(&testAlice));
    aggAlice = m_cm->contact(aggAlice.id());
    QCOMPARE(aggAlice.detail<QContactNickname>().nickname(), localAlice.detail<QContactNickname>().nickname());
    QVERIFY(aggAlice.detail<QContactPhoneNumber>().number().isEmpty());
    QVERIFY(aggAlice.detail<QContactAvatar>().imageUrl().isEmpty());

    // nor should the relationships have changed.
    QVERIFY(aggAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(localAlice.id()));
    QVERIFY(!aggAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(testAlice.id()));
}

void tst_Aggregation::updateSingleLocal()
{
    QContactCollectionFilter allCollections;

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allCollections).size();

    // set up some signal spies
    QSignalSpy addSpy(m_cm, contactsAddedSignal);
    QSignalSpy chgSpy(m_cm, contactsChangedSignal);
    int addSpyCount = 0;
    int chgSpyCount = 0;

    // now add a new local contact (no synctarget specified == automatically local)
    QContact alice;

    QContactName an;
    an.setFirstName("Alice");
    an.setMiddleName("In");
    an.setLastName("Wonderland");
    alice.saveDetail(&an);

    QContactPhoneNumber aph;
    aph.setNumber("4567");
    alice.saveDetail(&aph);

    QContactHobby ah;
    ah.setHobby("tennis");
    alice.saveDetail(&ah);

    m_addAccumulatedIds.clear();
    QVERIFY(m_cm->saveContact(&alice));
    QTRY_VERIFY(addSpy.count() > addSpyCount);
    QTRY_COMPARE(m_addAccumulatedIds.size(), 2); // should have added local + aggregate
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(alice)));
    addSpyCount = addSpy.count();

    QCOMPARE(m_cm->contactIds().size(), aggCount + 1); // 1 extra aggregate contact
    aggCount = m_cm->contactIds().size();
    QCOMPARE(m_cm->contactIds(allCollections).size(), allCount + 2); // should have added local + aggregate
    allCount = m_cm->contactIds(allCollections).size();

    QList<QContact> allContacts = m_cm->contacts(allCollections);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    QContact localAlice;
    QContact aggregateAlice;
    bool foundLocalAlice = false;
    bool foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        QContactHobby currHobby = curr.detail<QContactHobby>();
        if (currName.firstName() == QLatin1String("Alice")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("4567")
                && currHobby.hobby() == QLatin1String("tennis")) {
            if (curr.collectionId().localId() == localAddressbookId()) {
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(localAlice.id()));

    // now update local alice.  The aggregate should get updated also.
    QContactEmailAddress ae; // add an email address.
    ae.setEmailAddress("alice4@test.com");
    QVERIFY(localAlice.saveDetail(&ae));
    QContactHobby rah = localAlice.detail<QContactHobby>(); // remove a hobby
    QVERIFY(localAlice.removeDetail(&rah));
    QContactPhoneNumber maph = localAlice.detail<QContactPhoneNumber>(); // modify a phone number
    maph.setNumber("4444");
    QVERIFY(localAlice.saveDetail(&maph));
    chgSpyCount = chgSpy.count();
    m_chgAccumulatedIds.clear();
    QVERIFY(m_cm->saveContact(&localAlice));
    QTRY_VERIFY(chgSpy.count() > chgSpyCount);
    QTRY_VERIFY(m_chgAccumulatedIds.contains(ContactId::apiId(localAlice)));
    QTRY_VERIFY(m_chgAccumulatedIds.contains(ContactId::apiId(aggregateAlice)));

    // reload them, and compare.
    localAlice = m_cm->contact(retrievalId(localAlice));
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));
    QCOMPARE(localAlice.details<QContactEmailAddress>().size(), 1);
    QCOMPARE(localAlice.details<QContactPhoneNumber>().size(), 1);
    QCOMPARE(localAlice.details<QContactHobby>().size(), 0);
    QCOMPARE(aggregateAlice.details<QContactEmailAddress>().size(), 1);
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().size(), 1);
    QCOMPARE(aggregateAlice.details<QContactHobby>().size(), 0);
    QCOMPARE(localAlice.detail<QContactEmailAddress>().value<QString>(QContactEmailAddress::FieldEmailAddress), QString::fromLatin1("alice4@test.com"));
    QVERIFY(!detailProvenance(localAlice.detail<QContactEmailAddress>()).isEmpty());
    QCOMPARE(aggregateAlice.detail<QContactEmailAddress>().value<QString>(QContactEmailAddress::FieldEmailAddress), QString::fromLatin1("alice4@test.com"));
    QCOMPARE(detailProvenance(aggregateAlice.detail<QContactEmailAddress>()), detailProvenance(localAlice.detail<QContactEmailAddress>()));
    QCOMPARE(localAlice.detail<QContactPhoneNumber>().value<QString>(QContactPhoneNumber::FieldNumber), QString::fromLatin1("4444"));
    QVERIFY(!detailProvenance(localAlice.detail<QContactPhoneNumber>()).isEmpty());
    QCOMPARE(aggregateAlice.detail<QContactPhoneNumber>().value<QString>(QContactPhoneNumber::FieldNumber), QString::fromLatin1("4444"));
    QCOMPARE(detailProvenance(aggregateAlice.detail<QContactPhoneNumber>()), detailProvenance(localAlice.detail<QContactPhoneNumber>()));
    QVERIFY(localAlice.detail<QContactHobby>().value<QString>(QContactHobby::FieldHobby).isEmpty());
    QVERIFY(aggregateAlice.detail<QContactHobby>().value<QString>(QContactHobby::FieldHobby).isEmpty());

    // now do an update with a definition mask.  We need to be certain that no masked details were lost.
    ae = localAlice.detail<QContactEmailAddress>();
    ae.setEmailAddress("alice4@test4.com");
    QVERIFY(localAlice.saveDetail(&ae));
    aph = localAlice.detail<QContactPhoneNumber>();
    QVERIFY(localAlice.removeDetail(&aph)); // removed, but since we don't include phone number in the definitionMask, shouldn't be applied
    QList<QContact> saveList;
    saveList << localAlice;
    QVERIFY(m_cm->saveContacts(&saveList, DetailList() << detailType<QContactEmailAddress>()));

    // reload them, and compare.
    localAlice = m_cm->contact(retrievalId(localAlice));
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));
    QCOMPARE(localAlice.detail<QContactEmailAddress>().value<QString>(QContactEmailAddress::FieldEmailAddress), QString::fromLatin1("alice4@test4.com"));
    QCOMPARE(localAlice.details<QContactEmailAddress>().size(), 1);
    QCOMPARE(aggregateAlice.detail<QContactEmailAddress>().value<QString>(QContactEmailAddress::FieldEmailAddress), QString::fromLatin1("alice4@test4.com"));
    QCOMPARE(aggregateAlice.details<QContactEmailAddress>().size(), 1);
    QCOMPARE(localAlice.detail<QContactPhoneNumber>().value<QString>(QContactPhoneNumber::FieldNumber), QString::fromLatin1("4444"));
    QCOMPARE(localAlice.details<QContactPhoneNumber>().size(), 1);
    QCOMPARE(aggregateAlice.detail<QContactPhoneNumber>().value<QString>(QContactPhoneNumber::FieldNumber), QString::fromLatin1("4444"));
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().size(), 1);
}

// we now require updates to occur to constituent contacts;
// any attempt to save to an aggregate contact will result in an error.
void tst_Aggregation::updateSingleAggregate()
{
    QContactCollectionFilter allCollections;

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allCollections).size();

    // set up some signal spies
    QSignalSpy addSpy(m_cm, contactsAddedSignal);
    QSignalSpy chgSpy(m_cm, contactsChangedSignal);
    int addSpyCount = 0;
    int chgSpyCount = 0;

    // now add a new local contact (no synctarget specified == automatically local)
    QContact alice;

    QContactName an;
    an.setFirstName("Alice");
    an.setMiddleName("In");
    an.setLastName("Wonderland");
    alice.saveDetail(&an);

    QContactPhoneNumber aph;
    aph.setNumber("567");
    alice.saveDetail(&aph);

    QContactHobby ah;
    ah.setHobby("tennis");
    alice.saveDetail(&ah);

    QContactNickname ak;
    ak.setNickname("Ally");
    alice.saveDetail(&ak);

    m_addAccumulatedIds.clear();
    QVERIFY(m_cm->saveContact(&alice));
    QTRY_VERIFY(addSpy.count() > addSpyCount);
    QTRY_COMPARE(m_addAccumulatedIds.size(), 2); // should have added local + aggregate
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(alice)));
    addSpyCount = addSpy.count();

    QCOMPARE(m_cm->contactIds().size(), aggCount + 1); // 1 extra aggregate contact
    aggCount = m_cm->contactIds().size();
    QCOMPARE(m_cm->contactIds(allCollections).size(), allCount + 2); // should have added local + aggregate
    allCount = m_cm->contactIds(allCollections).size();

    QList<QContact> allContacts = m_cm->contacts(allCollections);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    QContact localAlice;
    QContact aggregateAlice;
    bool foundLocalAlice = false;
    bool foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        QContactHobby currHobby = curr.detail<QContactHobby>();
        if (currName.firstName() == QLatin1String("Alice")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("567")
                && currHobby.hobby() == QLatin1String("tennis")) {
            if (curr.collectionId().localId() == localAddressbookId()) {
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(localAlice.id()));

    // now attempt update aggregate alice.  We expect the operation to fail.
    QContactEmailAddress ae; // add an email address
    ae.setEmailAddress("alice5@test.com");
    aggregateAlice.saveDetail(&ae);
    QContactHobby rah = aggregateAlice.detail<QContactHobby>(); // remove a hobby
    aggregateAlice.removeDetail(&rah);
    QContactPhoneNumber maph = aggregateAlice.detail<QContactPhoneNumber>(); // modify a phone number
    maph.setNumber("555");
    aggregateAlice.saveDetail(&maph);
    chgSpyCount = chgSpy.count();
    m_chgAccumulatedIds.clear();
    QVERIFY(!m_cm->saveContact(&aggregateAlice));
    QTest::qWait(250);
    QCOMPARE(chgSpy.count(), chgSpyCount);
    QVERIFY(!m_chgAccumulatedIds.contains(ContactId::apiId(localAlice)));
    QVERIFY(!m_chgAccumulatedIds.contains(ContactId::apiId(aggregateAlice)));

    // reload them, and compare.  ensure that no changes have occurred.
    localAlice = m_cm->contact(retrievalId(localAlice));
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));
    QCOMPARE(localAlice.details<QContactEmailAddress>().size(), 0);
    QCOMPARE(localAlice.details<QContactPhoneNumber>().size(), 1);
    QCOMPARE(localAlice.details<QContactHobby>().size(), 1);
    QCOMPARE(localAlice.details<QContactNickname>().size(), 1);
    QCOMPARE(aggregateAlice.details<QContactEmailAddress>().size(), 0);
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().size(), 1);
    QCOMPARE(aggregateAlice.details<QContactHobby>().size(), 1);
    QCOMPARE(aggregateAlice.details<QContactNickname>().size(), 1);
    QCOMPARE(localAlice.detail<QContactPhoneNumber>().value<QString>(QContactPhoneNumber::FieldNumber), QLatin1String("567"));
    QVERIFY(!detailProvenance(localAlice.detail<QContactPhoneNumber>()).isEmpty());
    QCOMPARE(aggregateAlice.detail<QContactPhoneNumber>().value<QString>(QContactPhoneNumber::FieldNumber), QLatin1String("567"));
    QCOMPARE(detailProvenance(aggregateAlice.detail<QContactPhoneNumber>()), detailProvenance(localAlice.detail<QContactPhoneNumber>()));
    QCOMPARE(localAlice.detail<QContactHobby>().value<QString>(QContactHobby::FieldHobby), QLatin1String("tennis"));
    QVERIFY(!detailProvenance(localAlice.detail<QContactHobby>()).isEmpty());
    QCOMPARE(aggregateAlice.detail<QContactHobby>().value<QString>(QContactHobby::FieldHobby), QLatin1String("tennis"));
    QCOMPARE(detailProvenance(aggregateAlice.detail<QContactHobby>()), detailProvenance(localAlice.detail<QContactHobby>()));
    QCOMPARE(localAlice.detail<QContactNickname>().value<QString>(QContactNickname::FieldNickname), QLatin1String("Ally"));
    QVERIFY(!detailProvenance(localAlice.detail<QContactNickname>()).isEmpty());
    QCOMPARE(aggregateAlice.detail<QContactNickname>().value<QString>(QContactNickname::FieldNickname), QLatin1String("Ally"));
    QCOMPARE(detailProvenance(aggregateAlice.detail<QContactNickname>()), detailProvenance(localAlice.detail<QContactNickname>()));
}

// we now require updates to occur to constituent contacts;
// any attempt to save to an aggregate contact will result in an error.
void tst_Aggregation::updateAggregateOfLocalAndSync()
{
    QContactCollection remoteAddressbook;
    remoteAddressbook.setMetaData(QContactCollection::KeyName, QStringLiteral("test"));
    remoteAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, "tst_aggregation");
    remoteAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID, 5);
    remoteAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH, "/addressbooks/test");
    QVERIFY(m_cm->saveCollection(&remoteAddressbook));

    // local alice
    QContact alice;
    QContactName an;
    an.setFirstName("Alice");
    an.setMiddleName("In");
    an.setLastName("PromotedLand");
    alice.saveDetail(&an);

    QContactPhoneNumber aph;
    aph.setNumber("11111");
    alice.saveDetail(&aph);

    QContactEmailAddress aem;
    aem.setEmailAddress("aliceP@test.com");
    alice.saveDetail(&aem);

    QContactNickname ak;
    ak.setNickname("Ally");
    alice.saveDetail(&ak);

    QVERIFY(m_cm->saveContact(&alice));

    // sync alice
    QContact syncAlice;
    syncAlice.setCollectionId(remoteAddressbook.id());

    QContactName san;
    san.setFirstName(an.firstName());
    san.setMiddleName(an.middleName());
    san.setLastName(an.lastName());
    syncAlice.saveDetail(&san);

    QContactEmailAddress saem;
    saem.setEmailAddress(aem.emailAddress());
    syncAlice.saveDetail(&saem);

    QContactHobby sah; // this is a "new" detail which doesn't appear in the local contact.
    sah.setHobby(QLatin1String("tennis"));
    syncAlice.saveDetail(&sah);

    QContactNote sanote; // this is a "new" detail which doesn't appear in the local contact.
    sanote.setNote(QLatin1String("noteworthy note"));
    syncAlice.saveDetail(&sanote);

    QContactSyncTarget sast;
    sast.setSyncTarget(QLatin1String("test"));
    syncAlice.saveDetail(&sast);

    QVERIFY(m_cm->saveContact(&syncAlice));

    // now grab the aggregate alice
    QContactRelationshipFilter aggf;
    setFilterContactId(aggf, alice.id());
    aggf.setRelatedContactRole(QContactRelationship::Second);
    setFilterType(aggf, QContactRelationship::Aggregates);
    QList<QContact> allAggregatesOfAlice = m_cm->contacts(aggf);
    QCOMPARE(allAggregatesOfAlice.size(), 1);
    QContact aggregateAlice = allAggregatesOfAlice.at(0);

    // now ensure that any attempt to modify the aggregate directly will fail.
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().size(), 1); // comes from the local contact
    QContactPhoneNumber maph = aggregateAlice.detail<QContactPhoneNumber>();
    QVERIFY((maph.accessConstraints() & QContactDetail::Irremovable) && (maph.accessConstraints() & QContactDetail::ReadOnly));
    maph.setNumber("11115");
    QVERIFY(!aggregateAlice.saveDetail(&maph));

    QCOMPARE(aggregateAlice.details<QContactEmailAddress>().size(), 1); // there are two, but since the values were identical, should only have one!
    QContactEmailAddress mem = aggregateAlice.detail<QContactEmailAddress>();
    QVERIFY((mem.accessConstraints() & QContactDetail::Irremovable) && (mem.accessConstraints() & QContactDetail::ReadOnly));
    mem.setEmailAddress("aliceP2@test.com");
    QVERIFY(!aggregateAlice.saveDetail(&mem));

    QCOMPARE(aggregateAlice.details<QContactHobby>().size(), 1); // comes from the sync contact
    QContactHobby rah = aggregateAlice.detail<QContactHobby>();
    QVERIFY(rah.accessConstraints() & QContactDetail::Irremovable);
    QVERIFY(rah.accessConstraints() & QContactDetail::ReadOnly);
    QVERIFY(!aggregateAlice.removeDetail(&rah)); // this should be irremovable, due to constraint on synced details

    QContactNote man = aggregateAlice.detail<QContactNote>();
    QVERIFY(man.accessConstraints() & QContactDetail::Irremovable);
    QVERIFY(man.accessConstraints() & QContactDetail::ReadOnly);
    man.setNote("modified note");
    QVERIFY(!aggregateAlice.saveDetail(&man)); // this should be read only, due to constraint on synced details

    // but the attempted modifications should fail, due to modifying an aggregate.
    QVERIFY(!m_cm->saveContact(&aggregateAlice));

    // re-retrieve and ensure we get what we expect.
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));
    QCOMPARE(aggregateAlice.details<QContactNickname>().size(), 1);     // comes from the local contact
    QVERIFY(!detailProvenance(aggregateAlice.detail<QContactNickname>()).isEmpty());
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().size(), 1);  // comes from the local contact
    QCOMPARE(detailProvenanceContact(aggregateAlice.detail<QContactPhoneNumber>()), detailProvenanceContact(aggregateAlice.detail<QContactNickname>()));
    QCOMPARE(aggregateAlice.detail<QContactPhoneNumber>().value<QString>(QContactPhoneNumber::FieldNumber), QString::fromLatin1("11111"));
    QCOMPARE(aggregateAlice.details<QContactHobby>().size(), 1);        // comes from the sync contact
    QVERIFY(!detailProvenance(aggregateAlice.detail<QContactHobby>()).isEmpty());
    QCOMPARE(aggregateAlice.detail<QContactHobby>().value<QString>(QContactHobby::FieldHobby), QString::fromLatin1("tennis"));
    QCOMPARE(aggregateAlice.details<QContactNote>().size(), 1);         // comes from the sync contact
    QCOMPARE(detailProvenanceContact(aggregateAlice.detail<QContactNote>()), detailProvenanceContact(aggregateAlice.detail<QContactHobby>()));
    QVERIFY(detailProvenanceContact(aggregateAlice.detail<QContactNote>()) != detailProvenanceContact(aggregateAlice.detail<QContactNickname>()));
    QCOMPARE(aggregateAlice.detail<QContactNote>().value<QString>(QContactNote::FieldNote), QString::fromLatin1("noteworthy note"));

    QList<QContactEmailAddress> aaems = aggregateAlice.details<QContactEmailAddress>();
    QCOMPARE(aaems.size(), 1); // values should be unchanged (and identical).
    QCOMPARE(aaems.at(0).emailAddress(), QLatin1String("aliceP@test.com"));
}

void tst_Aggregation::updateAggregateOfLocalAndModifiableSync()
{
    QContactCollection remoteAddressbook;
    remoteAddressbook.setMetaData(QContactCollection::KeyName, QStringLiteral("test"));
    remoteAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, "tst_aggregation");
    remoteAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID, 5);
    remoteAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH, "/addressbooks/test");
    QVERIFY(m_cm->saveCollection(&remoteAddressbook));

    QContactCollection remoteAddressbook2;
    remoteAddressbook2.setMetaData(QContactCollection::KeyName, QStringLiteral("trial"));
    remoteAddressbook2.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, "tst_aggregation");
    remoteAddressbook2.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID, 6);
    remoteAddressbook2.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH, "/addressbooks/trial");
    QVERIFY(m_cm->saveCollection(&remoteAddressbook2));

    // local alice
    QContact alice;
    {
        QContactName name;
        name.setFirstName("Alice");
        name.setMiddleName("In");
        name.setLastName("PromotedLand");
        alice.saveDetail(&name);

        QContactNickname nickname;
        nickname.setNickname("Ally");
        alice.saveDetail(&nickname);

        QContactPhoneNumber aph;
        aph.setNumber("11111");
        alice.saveDetail(&aph);
    }
    QVERIFY(m_cm->saveContact(&alice));

    const QContactName &localName(alice.detail<QContactName>());

    // first syncTarget alice
    QContact testAlice;
    testAlice.setCollectionId(remoteAddressbook.id());
    {
        QContactName name;
        name.setFirstName(localName.firstName());
        name.setMiddleName(localName.middleName());
        name.setLastName(localName.lastName());
        testAlice.saveDetail(&name);

        QContactRingtone ringtone;
        ringtone.setAudioRingtoneUrl(QUrl("http://example.org/crickets.mp3"));
        testAlice.saveDetail(&ringtone);

        QContactEmailAddress emailAddress;
        emailAddress.setEmailAddress("aliceP@test.com");
        emailAddress.setValue(QContactDetail__FieldModifiable, true);
        testAlice.saveDetail(&emailAddress);

        QContactNote note;
        note.setNote("noteworthy note");
        note.setValue(QContactDetail__FieldModifiable, true);
        testAlice.saveDetail(&note);

        QContactHobby hobby;
        hobby.setHobby("tennis");
        hobby.setValue(QContactDetail__FieldModifiable, false);
        testAlice.saveDetail(&hobby);

        QContactSyncTarget syncTarget;
        syncTarget.setSyncTarget("test");
        testAlice.saveDetail(&syncTarget);

        QVERIFY(m_cm->saveContact(&testAlice));
    }

    // second syncTarget alice
    QContact trialAlice;
    trialAlice.setCollectionId(remoteAddressbook2.id());
    {
        QContactName name;
        name.setFirstName(localName.firstName());
        name.setMiddleName(localName.middleName());
        name.setLastName(localName.lastName());
        trialAlice.saveDetail(&name);

        QContactTag tag;
        tag.setTag("Fiction");
        trialAlice.saveDetail(&tag);

        QContactEmailAddress emailAddress;
        emailAddress.setEmailAddress("alice@example.org");
        emailAddress.setValue(QContactDetail__FieldModifiable, true);
        trialAlice.saveDetail(&emailAddress);

        QContactOrganization organization;
        organization.setRole("CEO");
        organization.setValue(QContactDetail__FieldModifiable, true);
        trialAlice.saveDetail(&organization);

        QContactSyncTarget syncTarget;
        syncTarget.setSyncTarget("trial");
        trialAlice.saveDetail(&syncTarget);

        QVERIFY(m_cm->saveContact(&trialAlice));
    }

    // now grab the aggregate alice
    QContact aggregateAlice;
    {
        QContactRelationshipFilter filter;
        setFilterContactId(filter, alice.id());
        filter.setRelatedContactRole(QContactRelationship::Second);
        setFilterType(filter, QContactRelationship::Aggregates);
        QList<QContact> allAggregatesOfAlice = m_cm->contacts(filter);
        QCOMPARE(allAggregatesOfAlice.size(), 1);
        aggregateAlice = allAggregatesOfAlice.at(0);
    }

    // Verify the aggregate state
    QCOMPARE(aggregateAlice.details<QContactNickname>().size(), 1);
    QVERIFY(!detailProvenance(aggregateAlice.detail<QContactNickname>()).isEmpty());

    // Nickname found only in the local contact
    const QString localContact(detailProvenanceContact(aggregateAlice.detail<QContactNickname>()));

    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().size(), 1);
    QCOMPARE(detailProvenanceContact(aggregateAlice.detail<QContactPhoneNumber>()), localContact);

    QCOMPARE(aggregateAlice.details<QContactRingtone>().size(), 1);
    QVERIFY(!detailProvenance(aggregateAlice.detail<QContactRingtone>()).isEmpty());
    QVERIFY(detailProvenanceContact(aggregateAlice.detail<QContactRingtone>()) != localContact);

    // Ringtone found only in the 'test' contact
    const QString teabContact(detailProvenanceContact(aggregateAlice.detail<QContactRingtone>()));

    QCOMPARE(aggregateAlice.details<QContactEmailAddress>().size(), 2);
    QVERIFY(!detailProvenance(aggregateAlice.details<QContactEmailAddress>().at(0)).isEmpty());
    QVERIFY(detailProvenanceContact(aggregateAlice.details<QContactEmailAddress>().at(0)) != localContact);
    QVERIFY(!detailProvenance(aggregateAlice.details<QContactEmailAddress>().at(1)).isEmpty());
    QVERIFY(detailProvenanceContact(aggregateAlice.details<QContactEmailAddress>().at(1)) != localContact);
    QVERIFY(detailProvenanceContact(aggregateAlice.details<QContactEmailAddress>().at(0)) != detailProvenanceContact(aggregateAlice.details<QContactEmailAddress>().at(1)));

    QCOMPARE(aggregateAlice.details<QContactNote>().size(), 1);
    QCOMPARE(detailProvenanceContact(aggregateAlice.detail<QContactNote>()), teabContact);

    QCOMPARE(aggregateAlice.details<QContactHobby>().size(), 1);
    QCOMPARE(detailProvenanceContact(aggregateAlice.detail<QContactHobby>()), teabContact);

    QCOMPARE(aggregateAlice.details<QContactTag>().size(), 1);
    QVERIFY(!detailProvenance(aggregateAlice.detail<QContactTag>()).isEmpty());
    QVERIFY(detailProvenanceContact(aggregateAlice.detail<QContactTag>()) != localContact);
    QVERIFY(detailProvenanceContact(aggregateAlice.detail<QContactTag>()) != teabContact);

    // Tag found only in the 'trial' contact
    const QString trialContact(detailProvenanceContact(aggregateAlice.detail<QContactTag>()));

    QCOMPARE(aggregateAlice.details<QContactOrganization>().size(), 1);
    QCOMPARE(detailProvenanceContact(aggregateAlice.detail<QContactOrganization>()), trialContact);

    // Test the modifiability of the details

    // Aggregate details are not modifiable
    QCOMPARE(aggregateAlice.detail<QContactName>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(aggregateAlice.detail<QContactNickname>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(aggregateAlice.detail<QContactPhoneNumber>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(aggregateAlice.details<QContactEmailAddress>().at(0).value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(aggregateAlice.details<QContactEmailAddress>().at(1).value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(aggregateAlice.detail<QContactHobby>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(aggregateAlice.detail<QContactNote>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(aggregateAlice.detail<QContactOrganization>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(aggregateAlice.detail<QContactRingtone>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(aggregateAlice.detail<QContactTag>().value(QContactDetail__FieldModifiable).toBool(), false);

    // The test contact should have some modifiable fields
    testAlice = m_cm->contact(retrievalId(testAlice));
    QCOMPARE(testAlice.detail<QContactName>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(testAlice.detail<QContactRingtone>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(testAlice.detail<QContactEmailAddress>().value(QContactDetail__FieldModifiable).toBool(), true);
    QCOMPARE(testAlice.detail<QContactHobby>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(testAlice.detail<QContactNote>().value(QContactDetail__FieldModifiable).toBool(), true);

    // The trial contact should also have some modifiable fields
    trialAlice = m_cm->contact(retrievalId(trialAlice));
    QCOMPARE(trialAlice.detail<QContactName>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(trialAlice.detail<QContactTag>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(trialAlice.detail<QContactEmailAddress>().value(QContactDetail__FieldModifiable).toBool(), true);
    QCOMPARE(trialAlice.detail<QContactOrganization>().value(QContactDetail__FieldModifiable).toBool(), true);

    // Aggregate details which are promoted even from modifiable details are readonly
    QVERIFY((aggregateAlice.detail<QContactName>().accessConstraints() & QContactDetail::ReadOnly) > 0);
    QVERIFY((aggregateAlice.detail<QContactNickname>().accessConstraints() & QContactDetail::ReadOnly) > 0);
    QVERIFY((aggregateAlice.detail<QContactPhoneNumber>().accessConstraints() & QContactDetail::ReadOnly) > 0);
    QVERIFY((aggregateAlice.details<QContactEmailAddress>().at(0).accessConstraints() & QContactDetail::ReadOnly) > 0);
    QVERIFY((aggregateAlice.details<QContactEmailAddress>().at(1).accessConstraints() & QContactDetail::ReadOnly) > 0);
    QVERIFY((aggregateAlice.detail<QContactHobby>().accessConstraints() & QContactDetail::ReadOnly) > 0);
    QVERIFY((aggregateAlice.detail<QContactNote>().accessConstraints() & QContactDetail::ReadOnly) > 0);
    QVERIFY((aggregateAlice.detail<QContactOrganization>().accessConstraints() & QContactDetail::ReadOnly) > 0);
    QVERIFY((aggregateAlice.detail<QContactRingtone>().accessConstraints() & QContactDetail::ReadOnly) > 0);
    QVERIFY((aggregateAlice.detail<QContactTag>().accessConstraints() & QContactDetail::ReadOnly) > 0);

    // now ensure that attempts to modify the aggregate contact fail as expected.
    {
        // locally-originated detail
        QContactPhoneNumber phoneNumber = aggregateAlice.detail<QContactPhoneNumber>();
        phoneNumber.setNumber("22222");
        QVERIFY(!aggregateAlice.saveDetail(&phoneNumber));

        // sync constituent details
        foreach (QContactEmailAddress emailAddress, aggregateAlice.details<QContactEmailAddress>()) {
            if (emailAddress.emailAddress() == QString::fromLatin1("aliceP@test.com")) {
                emailAddress.setEmailAddress("aliceP2@test.com");
                QVERIFY(!aggregateAlice.saveDetail(&emailAddress));
            } else {
                emailAddress.setEmailAddress("alice2@example.org");
                QVERIFY(!aggregateAlice.saveDetail(&emailAddress));
            }
        }

        // sync constituent detail which is modifiable in constituent
        QContactNote note = aggregateAlice.detail<QContactNote>();
        QVERIFY(!aggregateAlice.removeDetail(&note));

        // sync constituent detail which is modifiable in constituent
        QContactOrganization organization = aggregateAlice.detail<QContactOrganization>();
        QVERIFY(!aggregateAlice.removeDetail(&organization));

        // sync constituent detail which is non-modifiable in constituent
        QContactHobby hobby = aggregateAlice.detail<QContactHobby>();
        hobby.setHobby("crochet");
        QVERIFY(!aggregateAlice.saveDetail(&hobby));
    }

    QVERIFY(!m_cm->saveContact(&aggregateAlice));
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));

    // ensure that no changes have occurred.

    QCOMPARE(aggregateAlice.details<QContactNickname>().size(), 1);
    QVERIFY(!detailProvenance(aggregateAlice.detail<QContactNickname>()).isEmpty());

    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().size(), 1);
    QCOMPARE(detailProvenanceContact(aggregateAlice.detail<QContactPhoneNumber>()), localContact);
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().at(0).number(), QString::fromLatin1("11111"));

    QList<QContactEmailAddress> aaeas = aggregateAlice.details<QContactEmailAddress>();
    QCOMPARE(aaeas.size(), 2);
    if (aaeas.at(0).emailAddress() == QString::fromLatin1("aliceP@test.com")) {
        QCOMPARE(detailProvenanceContact(aaeas.at(0)), teabContact);
        QCOMPARE(detailProvenanceContact(aaeas.at(1)), trialContact);
        QCOMPARE(aaeas.at(1).emailAddress(), QString::fromLatin1("alice@example.org"));
    } else {
        QCOMPARE(detailProvenanceContact(aaeas.at(0)), trialContact);
        QCOMPARE(aaeas.at(0).emailAddress(), QString::fromLatin1("alice@example.org"));
        QCOMPARE(detailProvenanceContact(aaeas.at(1)), teabContact);
        QCOMPARE(aaeas.at(1).emailAddress(), QString::fromLatin1("aliceP@test.com"));
    }

    QCOMPARE(aggregateAlice.details<QContactNote>().size(), 1);
    QCOMPARE(detailProvenanceContact(aggregateAlice.detail<QContactNote>()), teabContact);
    QCOMPARE(aggregateAlice.details<QContactNote>().at(0).note(), QString::fromLatin1("noteworthy note"));

    QList<QContactHobby> aahs = aggregateAlice.details<QContactHobby>();
    QCOMPARE(aahs.size(), 1);
    QCOMPARE(aggregateAlice.details<QContactHobby>().at(0).hobby(), QString::fromLatin1("tennis"));
    QCOMPARE(detailProvenanceContact(aahs.at(0)), teabContact);

    QCOMPARE(aggregateAlice.details<QContactOrganization>().size(), 1);
    QCOMPARE(detailProvenanceContact(aggregateAlice.detail<QContactOrganization>()), trialContact);
    QCOMPARE(aggregateAlice.details<QContactOrganization>().at(0).role(), QString::fromLatin1("CEO"));

    // Modifiability should be unaffected

    // Aggregate details are not modifiable
    QCOMPARE(aggregateAlice.detail<QContactName>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(aggregateAlice.detail<QContactPhoneNumber>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(aggregateAlice.details<QContactEmailAddress>().at(0).value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(aggregateAlice.details<QContactEmailAddress>().at(1).value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(aggregateAlice.detail<QContactHobby>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(aggregateAlice.detail<QContactRingtone>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(aggregateAlice.detail<QContactTag>().value(QContactDetail__FieldModifiable).toBool(), false);

    // The test contact should have some modifiable fields
    testAlice = m_cm->contact(retrievalId(testAlice));
    QCOMPARE(testAlice.detail<QContactName>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(testAlice.detail<QContactRingtone>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(testAlice.detail<QContactEmailAddress>().value(QContactDetail__FieldModifiable).toBool(), true);
    QCOMPARE(testAlice.detail<QContactHobby>().value(QContactDetail__FieldModifiable).toBool(), false);

    // The trial contact should also have some modifiable fields
    trialAlice = m_cm->contact(retrievalId(trialAlice));
    QCOMPARE(trialAlice.detail<QContactName>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(trialAlice.detail<QContactTag>().value(QContactDetail__FieldModifiable).toBool(), false);
    QCOMPARE(trialAlice.detail<QContactEmailAddress>().value(QContactDetail__FieldModifiable).toBool(), true);

    // Aggregate details which are promoted from modifiable details are still readonly
    QVERIFY((aggregateAlice.detail<QContactName>().accessConstraints() & QContactDetail::ReadOnly) > 0);
    QVERIFY((aggregateAlice.detail<QContactPhoneNumber>().accessConstraints() & QContactDetail::ReadOnly) > 0);
    QVERIFY((aggregateAlice.details<QContactEmailAddress>().at(0).accessConstraints() & QContactDetail::ReadOnly) > 0);
    QVERIFY((aggregateAlice.details<QContactEmailAddress>().at(1).accessConstraints() & QContactDetail::ReadOnly) > 0);
    QVERIFY((aggregateAlice.details<QContactHobby>().at(0).accessConstraints() & QContactDetail::ReadOnly) > 0);
    QVERIFY((aggregateAlice.detail<QContactRingtone>().accessConstraints() & QContactDetail::ReadOnly) > 0);
    QVERIFY((aggregateAlice.detail<QContactTag>().accessConstraints() & QContactDetail::ReadOnly) > 0);
}

void tst_Aggregation::compositionPrefersLocal()
{
    // Composed details should prefer the values of the local, where present
    QContactCollectionFilter allCollections;

    // Create the addressbook collections
    QContactCollection testCollection1, testCollection2, testCollection3;
    testCollection1.setMetaData(QContactCollection::KeyName, QStringLiteral("test1"));
    testCollection1.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, "tst_aggregation");
    testCollection2.setMetaData(QContactCollection::KeyName, QStringLiteral("test2"));
    testCollection2.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, "tst_aggregation");
    testCollection3.setMetaData(QContactCollection::KeyName, QStringLiteral("test3"));
    testCollection3.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, "tst_aggregation");
    testCollection3.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID, 5);
    testCollection3.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH, "/addressbooks/test3");
    QVERIFY(m_cm->saveCollection(&testCollection1));
    QVERIFY(m_cm->saveCollection(&testCollection2));
    QVERIFY(m_cm->saveCollection(&testCollection3));

    // These contacts should all be aggregated together
    QContact abContact1, abContact2, abContact3, localContact;

    QContactName n1;
    n1.setPrefix(QLatin1String("Supt."));
    n1.setFirstName(QLatin1String("Link"));
    n1.setMiddleName(QLatin1String("Alice"));
    n1.setLastName(QLatin1String("CompositionTester"));
    abContact1.saveDetail(&n1);

    abContact1.setCollectionId(testCollection1.id());
    QVERIFY(m_cm->saveContact(&abContact1));

    QContactName n2;
    n2.setFirstName(QLatin1String("Link"));
    n2.setMiddleName(QLatin1String("Bob"));
    n2.setLastName(QLatin1String("CompositionTester"));
    localContact.saveDetail(&n2);

    QVERIFY(m_cm->saveContact(&localContact));

    QContactName n3;
    n3.setFirstName(QLatin1String("Link"));
    n3.setMiddleName(QLatin1String("Charlie"));
    n3.setLastName(QLatin1String("CompositionTester"));
    n3.setSuffix(QLatin1String("Esq."));
    abContact2.saveDetail(&n3);

    abContact2.setCollectionId(testCollection2.id());
    QVERIFY(m_cm->saveContact(&abContact2));

    // Add a contact via synchronization
    QtContactsSqliteExtensions::ContactManagerEngine *cme = QtContactsSqliteExtensions::contactManagerEngine(*m_cm);
    QContactName n4;
    n4.setFirstName(QLatin1String("Link"));
    n4.setMiddleName(QLatin1String("Donatella"));
    n4.setLastName(QLatin1String("CompositionTester"));
    abContact3.saveDetail(&n4);
    QContactStatusFlags flags;
    flags.setFlag(QContactStatusFlags::IsAdded, true);
    abContact3.saveDetail(&flags, QContact::IgnoreAccessConstraints);
    QtContactsSqliteExtensions::ContactManagerEngine::ConflictResolutionPolicy policy(QtContactsSqliteExtensions::ContactManagerEngine::PreserveLocalChanges);
    QContactManager::Error err;
    QHash<QContactCollection*, QList<QContact> *> additions;
    QHash<QContactCollection*, QList<QContact> *> modifications;
    QList<QContact> modifiedContacts;
    modifiedContacts.append(abContact3); // in this case, the change is an addition.
    modifications.insert(&testCollection3, &modifiedContacts);
    QVERIFY(cme->storeChanges(
            &additions,                     // not adding any collections
            &modifications,
            QList<QContactCollectionId>(),  // not deleting any collections
            policy, true, &err));

    QList<QContact> allContacts = m_cm->contacts(allCollections);
    QContact abc1, abc2, abc3, l, a; // addressbook contacts 1,2,3, local, aggregate.
    foreach (const QContact &curr, allContacts) {
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Link") && currName.lastName() == QLatin1String("CompositionTester")) {
            if (curr.collectionId() == testCollection1.id()) {
                abc1 = curr;
            } else if (curr.collectionId() == testCollection2.id()) {
                abc2 = curr;
            } else if (curr.collectionId() == testCollection3.id()) {
                abc3 = curr;
            } else if (curr.collectionId().localId() == localAddressbookId()) {
                l = curr;
            } else if (curr.collectionId().localId() == aggregateAddressbookId()) {
                a = curr;
            }
        }
    }

    QVERIFY(abc1.id() != QContactId());
    QVERIFY(abc2.id() != QContactId());
    QVERIFY(abc3.id() != QContactId());
    QVERIFY(l.id() != QContactId());
    QVERIFY(a.id() != QContactId());
    QVERIFY(abc1.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(a.id()));
    QVERIFY(a.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(abc1.id()));
    QVERIFY(abc2.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(a.id()));
    QVERIFY(a.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(abc2.id()));
    QVERIFY(abc3.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(a.id()));
    QVERIFY(a.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(abc3.id()));
    QVERIFY(l.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(a.id()));
    QVERIFY(a.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(l.id()));

    // The name of the local contact should be prioritized in aggregation
    QContactName name(a.detail<QContactName>());
    QCOMPARE(name.middleName(), n2.middleName());

    // The name elements unspecified by the local should be filled by other constituents in indeterminate order
    QCOMPARE(name.prefix(), n1.prefix());
    QCOMPARE(name.suffix(), n3.suffix());

    // Change the names in non-local constituents
    n1 = abc1.detail<QContactName>();
    n1.setPrefix(QLatin1String("Dr."));
    n1.setMiddleName(QLatin1String("Enzo"));
    abc1.saveDetail(&n1);
    QVERIFY(m_cm->saveContact(&abc1));

    // Update with a definition mask
    n3 = abc2.detail<QContactName>();
    n3.setMiddleName(QLatin1String("Francois"));
    n3.setSuffix(QLatin1String("MBA"));
    abc2.saveDetail(&n3);
    QList<QContact> saveList;
    saveList.append(abc2);
    QVERIFY(m_cm->saveContacts(&saveList, QList<QContactDetail::DetailType>() << QContactName::Type));

    a = m_cm->contact(retrievalId(a));

    name = a.detail<QContactName>();
    QCOMPARE(name.middleName(), n2.middleName());
    QCOMPARE(name.prefix(), n1.prefix());
    QCOMPARE(name.suffix(), n3.suffix());

    // Update with a definition mask not including name (should not update, but local still has priority)
    QContactName n5 = abc2.detail<QContactName>();
    n5.setMiddleName(QLatin1String("Guillermo"));
    n5.setSuffix(QLatin1String("Ph.D"));
    abc2.saveDetail(&n5);
    saveList.clear();
    saveList.append(abc2);
    QVERIFY(m_cm->saveContacts(&saveList, QList<QContactDetail::DetailType>() << QContactAvatar::Type));

    a = m_cm->contact(retrievalId(a));

    name = a.detail<QContactName>();
    QCOMPARE(name.middleName(), n2.middleName());
    QCOMPARE(name.prefix(), n1.prefix());
    QCOMPARE(name.suffix(), n3.suffix());

    // Update via synchronization
    QContact modified;
    {
        QList<QContact> addedContacts;
        QList<QContact> modifiedContacts;
        QList<QContact> deletedContacts;
        QList<QContact> unmodifiedContacts;
        QVERIFY(cme->fetchContactChanges(
                    testCollection3.id(),
                    &addedContacts,
                    &modifiedContacts,
                    &deletedContacts,
                    &unmodifiedContacts,
                    &err));
        QCOMPARE(addedContacts.count(), 0);
        QCOMPARE(modifiedContacts.count(), 0);
        QCOMPARE(deletedContacts.count(), 0);
        QCOMPARE(unmodifiedContacts.count(), 1);
        modified = unmodifiedContacts.first();
    }

    n4 = modified.detail<QContactName>();
    n4.setMiddleName(QLatin1String("Hector"));
    modified.saveDetail(&n4);

    // mark the contact as modified for the sync operation.
    flags = modified.detail<QContactStatusFlags>();
    flags.setFlag(QContactStatusFlags::IsModified, true);
    modified.saveDetail(&flags, QContact::IgnoreAccessConstraints);

    modifications.clear();
    modifiedContacts.clear();
    modifiedContacts.append(modified);
    modifications.insert(&testCollection3, &modifiedContacts);
    QVERIFY(cme->storeChanges(
        &additions,                     // not adding any collections
        &modifications,
        QList<QContactCollectionId>(),  // not deleting any collections
        policy, true, &err));

    a = m_cm->contact(retrievalId(a));
    l = m_cm->contact(retrievalId(l));

    // The sync update will not update the local.
    // Since the local data is preferred for aggregation, the aggregate will not update.
    name = a.detail<QContactName>();
    QCOMPARE(name.middleName(), n2.middleName());
    QCOMPARE(name.prefix(), n1.prefix());
    QCOMPARE(name.suffix(), n3.suffix());
    name = l.detail<QContactName>();
    QCOMPARE(name.middleName(), n2.middleName()); // unchanged

    // Local changes override other changes
    n2 = l.detail<QContactName>();
    n2.setPrefix(QLatin1String("Monsignor"));
    n2.setMiddleName(QLatin1String("Isaiah"));
    l.saveDetail(&n2);
    QVERIFY(m_cm->saveContact(&l));

    a = m_cm->contact(retrievalId(a));

    name = a.detail<QContactName>();
    QCOMPARE(name.middleName(), n2.middleName());
    QCOMPARE(name.prefix(), n2.prefix());
    QCOMPARE(name.suffix(), n3.suffix());

    // Local details should still be preferred
    name = a.detail<QContactName>();
    QCOMPARE(name.middleName(), n2.middleName());
    QCOMPARE(name.prefix(), n2.prefix());
    QCOMPARE(name.suffix(), n3.suffix());
}

void tst_Aggregation::uniquenessConstraints()
{
    QContactCollectionFilter allCollections;

    // create a valid local contact.  An aggregate should be generated.
    QContact localAlice;
    QContactName an;
    an.setFirstName("Uniqueness");
    an.setLastName("Constraints");
    QVERIFY(localAlice.saveDetail(&an));
    QContactEmailAddress aem;
    aem.setEmailAddress("uniqueness@test.com");
    QVERIFY(localAlice.saveDetail(&aem));
    QContactGuid ag;
    ag.setGuid("first-unique-guid");
    QVERIFY(localAlice.saveDetail(&ag));
    QContactFavorite afav;
    afav.setFavorite(false);
    QVERIFY(localAlice.saveDetail(&afav));
    QVERIFY(m_cm->saveContact(&localAlice));

    QList<QContact> allContacts = m_cm->contacts(allCollections);
    QContact aggregateAlice;
    bool foundLocalAlice = false;
    bool foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactEmailAddress currEm = curr.detail<QContactEmailAddress>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Uniqueness")
                && currName.lastName() == QLatin1String("Constraints")
                && currEm.emailAddress() == QLatin1String("uniqueness@test.com")) {
            if (curr.collectionId().localId() == localAddressbookId()) {
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundAggregateAlice);

    // test uniqueness constraint of favorite detail.
    QCOMPARE(aggregateAlice.details<QContactFavorite>().size(), 1);
    afav = localAlice.detail<QContactFavorite>();
    QContactFavorite afav2;
    afav2.setFavorite(true);
    QVERIFY(localAlice.saveDetail(&afav2)); // this actually creates a second (in memory) favorite detail
    QCOMPARE(localAlice.details<QContactFavorite>().size(), 2);
    QVERIFY(!m_cm->saveContact(&localAlice)); // should fail, as Favorite is unique
    QVERIFY(localAlice.removeDetail(&afav2));
    afav = localAlice.detail<QContactFavorite>();
    afav.setFavorite(true);
    QVERIFY(localAlice.saveDetail(&afav));
    QCOMPARE(localAlice.details<QContactFavorite>().size(), 1);
    QVERIFY(m_cm->saveContact(&localAlice)); // should succeed.
    QCOMPARE(localAlice.details<QContactFavorite>().size(), 1);
    QVERIFY(m_cm->contact(retrievalId(aggregateAlice)).detail<QContactFavorite>().isFavorite());
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));

    // test uniqueness constraint of birthday detail.
    QDateTime aliceBirthday = QLocale::c().toDateTime("25/12/1950 01:23:45", "dd/MM/yyyy hh:mm:ss");
    QCOMPARE(aggregateAlice.details<QContactBirthday>().size(), 0);
    QContactBirthday abd;
    abd.setDateTime(aliceBirthday);
    QVERIFY(localAlice.saveDetail(&abd));
    QCOMPARE(localAlice.details<QContactBirthday>().size(), 1);
    QVERIFY(m_cm->saveContact(&localAlice));
    // now save another, should fail.
    QContactBirthday anotherBd;
    anotherBd.setDateTime(QDateTime::currentDateTime());
    QVERIFY(localAlice.saveDetail(&anotherBd));
    QCOMPARE(localAlice.details<QContactBirthday>().size(), 2);
    QVERIFY(!m_cm->saveContact(&localAlice)); // should fail, uniqueness.
    QVERIFY(localAlice.removeDetail(&anotherBd));
    QVERIFY(m_cm->saveContact(&localAlice)); // back to just one, should succeed.
    QVERIFY(m_cm->contact(retrievalId(aggregateAlice)).detail<QContactBirthday>().date() == aliceBirthday.date());

    // now save a different birthday in another contact aggregated into alice.
    QContactCollection testCollection1;
    testCollection1.setMetaData(QContactCollection::KeyName, QStringLiteral("test1"));
    QVERIFY(m_cm->saveCollection(&testCollection1));
    QContact testsyncAlice;
    testsyncAlice.setCollectionId(testCollection1.id());
    QContactBirthday tsabd;
    tsabd.setDateTime(aliceBirthday.addDays(-5));
    testsyncAlice.saveDetail(&tsabd);
    QContactName tsaname;
    tsaname.setFirstName(an.firstName());
    tsaname.setLastName(an.lastName());
    testsyncAlice.saveDetail(&tsaname);
    QContactEmailAddress tsaem;
    tsaem.setEmailAddress(aem.emailAddress());
    testsyncAlice.saveDetail(&tsaem);
    QContactNote tsanote;
    tsanote.setNote("noteworthy note");
    testsyncAlice.saveDetail(&tsanote);
    QContactSyncTarget tsast;
    tsast.setSyncTarget("test1");
    testsyncAlice.saveDetail(&tsast);
    QVERIFY(m_cm->saveContact(&testsyncAlice)); // should get aggregated into aggregateAlice.
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice)); // reload
    QCOMPARE(aggregateAlice.details<QContactBirthday>().size(), 1); // should still only have one birthday - local should take precedence.
    QCOMPARE(aggregateAlice.detail<QContactBirthday>().date(), aliceBirthday.date());
    QCOMPARE(aggregateAlice.detail<QContactNote>().note(), tsanote.note());
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));
    localAlice = m_cm->contact(retrievalId(localAlice));

    // test uniqueness constraint of name detail.
    QVERIFY(localAlice.details<QContactName>().size() == 1);
    QContactName anotherName;
    anotherName.setFirstName("Testing");
    QVERIFY(localAlice.saveDetail(&anotherName));
    QCOMPARE(localAlice.details<QContactName>().size(), 2);
    QVERIFY(!m_cm->saveContact(&localAlice));
    QVERIFY(localAlice.removeDetail(&anotherName));
    QCOMPARE(localAlice.details<QContactName>().size(), 1);
    anotherName = localAlice.detail<QContactName>();
    anotherName.setMiddleName("Middle");
    QVERIFY(localAlice.saveDetail(&anotherName));
    QVERIFY(m_cm->saveContact(&localAlice));
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));
    localAlice = m_cm->contact(retrievalId(localAlice));
    QCOMPARE(aggregateAlice.detail<QContactName>().firstName(), localAlice.detail<QContactName>().firstName());
    QCOMPARE(aggregateAlice.detail<QContactName>().middleName(), localAlice.detail<QContactName>().middleName());
    QCOMPARE(aggregateAlice.detail<QContactName>().lastName(), localAlice.detail<QContactName>().lastName());

    // test uniqueness (and non-promotion) constraint of sync target.
    QVERIFY(aggregateAlice.details<QContactSyncTarget>().size() == 0);
    QContactSyncTarget tsast2;
    tsast2.setSyncTarget("uniqueness");
    QVERIFY(testsyncAlice.saveDetail(&tsast2));
    QCOMPARE(testsyncAlice.details<QContactSyncTarget>().size(), 2);
    QVERIFY(!m_cm->saveContact(&testsyncAlice)); // uniqueness constraint fails.
    QVERIFY(testsyncAlice.removeDetail(&tsast2));
    QCOMPARE(testsyncAlice.details<QContactSyncTarget>().size(), 1);
    tsast2 = testsyncAlice.detail<QContactSyncTarget>();
    tsast2.setSyncTarget("uniqueness");
    QVERIFY(testsyncAlice.saveDetail(&tsast2));
    QVERIFY(m_cm->saveContact(&testsyncAlice)); // should now succeed.
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));
    QVERIFY(aggregateAlice.details<QContactSyncTarget>().size() == 0); // but not promoted to aggregate.
    localAlice = m_cm->contact(retrievalId(localAlice));
    QVERIFY(localAlice.details<QContactSyncTarget>().size() == 0); // and localAlice should never be affected by operations to testsyncAlice.

    // test uniqueness constraint of timestamp detail.
    // Timestamp is a bit special, since if no values exist, we don't synthesise it,
    // even though it exists in the main table.
    QDateTime testDt = QDateTime::currentDateTime();
    bool hasCreatedTs = false;
    if (testsyncAlice.details<QContactTimestamp>().size() == 0) {
        QContactTimestamp firstTs;
        firstTs.setCreated(testDt);
        QVERIFY(testsyncAlice.saveDetail(&firstTs));
        QVERIFY(m_cm->saveContact(&testsyncAlice));
        hasCreatedTs = true;
    }
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));
    QVERIFY(aggregateAlice.details<QContactTimestamp>().size() == 1);
    QContactTimestamp ats;
    ats.setLastModified(testDt);
    QVERIFY(testsyncAlice.saveDetail(&ats));
    QCOMPARE(testsyncAlice.details<QContactTimestamp>().size(), 2);
    QVERIFY(!m_cm->saveContact(&testsyncAlice));
    QVERIFY(testsyncAlice.removeDetail(&ats));
    QCOMPARE(testsyncAlice.details<QContactTimestamp>().size(), 1);
    ats = testsyncAlice.detail<QContactTimestamp>();
    ats.setLastModified(testDt);
    QVERIFY(testsyncAlice.saveDetail(&ats));

    QDateTime beforeWrite(QDateTime::currentDateTimeUtc());
    QTest::qWait(11);
    QVERIFY(m_cm->saveContact(&testsyncAlice));

    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));
    QVERIFY(aggregateAlice.details<QContactTimestamp>().size() == 1);
    QVERIFY(aggregateAlice.detail<QContactTimestamp>().lastModified() >= beforeWrite);
    QVERIFY(aggregateAlice.detail<QContactTimestamp>().lastModified() <= QDateTime::currentDateTimeUtc());
    if (hasCreatedTs) {
        QCOMPARE(aggregateAlice.detail<QContactTimestamp>().created(), testDt);
    }

    // GUID is no longer a singular detail
    QVERIFY(localAlice.details<QContactGuid>().size() == 1);
    QContactGuid ag2;
    ag2.setGuid("second-unique-guid");
    QVERIFY(localAlice.saveDetail(&ag2));
    QCOMPARE(localAlice.details<QContactGuid>().size(), 2);
    QVERIFY(m_cm->saveContact(&localAlice));

    localAlice = m_cm->contact(retrievalId(localAlice));
    QCOMPARE(localAlice.details<QContactGuid>().size(), 2);

    // GUIDs are not promoted
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));
    QCOMPARE(aggregateAlice.details<QContactGuid>().size(), 0);
}

void tst_Aggregation::removeSingleLocal()
{
    QContactCollectionFilter allCollections;

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allCollections).size();
    int oldAggCount = aggCount;
    int oldAllCount = allCount;

    // set up some signal spies
    QSignalSpy addSpy(m_cm, contactsAddedSignal);
    QSignalSpy remSpy(m_cm, contactsRemovedSignal);
    int addSpyCount = 0;
    int remSpyCount = 0;

    // now add a new local contact (no collectionId specified == automatically local)
    QContact alice;

    QContactName an;
    an.setFirstName("Alice");
    an.setMiddleName("In");
    an.setLastName("Wonderland");
    alice.saveDetail(&an);

    QContactPhoneNumber aph;
    aph.setNumber("67");
    alice.saveDetail(&aph);

    m_addAccumulatedIds.clear();
    QVERIFY(m_cm->saveContact(&alice));
    QTRY_VERIFY(addSpy.count() > addSpyCount);
    QTRY_COMPARE(m_addAccumulatedIds.size(), 2); // should have added local + aggregate
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(alice)));
    addSpyCount = addSpy.count();

    QCOMPARE(m_cm->contactIds().size(), aggCount + 1); // 1 extra aggregate contact
    aggCount = m_cm->contactIds().size();
    QCOMPARE(m_cm->contactIds(allCollections).size(), allCount + 2); // should have added local + aggregate
    allCount = m_cm->contactIds(allCollections).size();

    QList<QContact> allContacts = m_cm->contacts(allCollections);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    QContact localAlice;
    QContact aggregateAlice;
    bool foundLocalAlice = false;
    bool foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("67")) {
            if (curr.collectionId().localId() == localAddressbookId()) {
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(localAlice.id()));

    // now add another local contact.
    QContact bob;
    QContactName bn;
    bn.setFirstName("Bob7");
    bn.setMiddleName("The");
    bn.setLastName("Constructor");
    QContactPhoneNumber bp;
    bp.setNumber("777");
    bob.saveDetail(&bn);
    bob.saveDetail(&bp);
    QVERIFY(m_cm->saveContact(&bob));

    // we should have an extra aggregate (bob's) now too
    aggCount = m_cm->contactIds().size();

    // now remove local alice.  We expect that the "orphan" aggregate alice will also be removed.
    remSpyCount = remSpy.count();
    m_remAccumulatedIds.clear();
    QVERIFY(m_cm->removeContact(removalId(localAlice)));
    QTRY_VERIFY(remSpy.count() > remSpyCount);
    QTRY_VERIFY(m_remAccumulatedIds.contains(ContactId::apiId(localAlice)));
    QTRY_VERIFY(m_remAccumulatedIds.contains(ContactId::apiId(aggregateAlice)));

    // alice's aggregate contact should have been removed, bob's should not have.
    QCOMPARE(m_cm->contactIds().size(), (aggCount-1));

    // but bob should not have been removed.
    QVERIFY(m_cm->contactIds(allCollections).contains(ContactId::apiId(bob)));
    QList<QContact> stillExisting = m_cm->contacts(allCollections);
    bool foundBob = false;
    foreach (const QContact &c, stillExisting) {
        if (c.id() == bob.id()) {
            foundBob = true;
            break;
        }
    }
    QVERIFY(foundBob);

    // now remove bob.
    QVERIFY(m_cm->removeContact(removalId(bob)));
    QVERIFY(!m_cm->contactIds(allCollections).contains(ContactId::apiId(bob)));

    // should be back to our original counts
    int newAggCount = m_cm->contactIds().size();
    int newAllCount = m_cm->contactIds(allCollections).size();
    QCOMPARE(newAggCount, oldAggCount);
    QCOMPARE(newAllCount, oldAllCount);
}

void tst_Aggregation::removeSingleAggregate()
{
    QContactCollectionFilter allCollections;

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allCollections).size();
    int oldAggCount = aggCount;
    int oldAllCount = allCount;

    // set up some signal spies
    QSignalSpy addSpy(m_cm, contactsAddedSignal);
    QSignalSpy remSpy(m_cm, contactsRemovedSignal);
    int addSpyCount = 0;
    int remSpyCount = 0;

    // now add a new local contact (no collectionId specified == automatically local)
    QContact alice;

    QContactName an;
    an.setFirstName("Alice");
    an.setMiddleName("In");
    an.setLastName("Wonderland");
    alice.saveDetail(&an);

    QContactPhoneNumber aph;
    aph.setNumber("7");
    alice.saveDetail(&aph);

    m_addAccumulatedIds.clear();
    QVERIFY(m_cm->saveContact(&alice));
    QTRY_VERIFY(addSpy.count() > addSpyCount);
    QTRY_COMPARE(m_addAccumulatedIds.size(), 2); // should have added local + aggregate
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(alice)));
    addSpyCount = addSpy.count();

    QCOMPARE(m_cm->contactIds().size(), aggCount + 1); // 1 extra aggregate contact
    aggCount = m_cm->contactIds().size();
    QCOMPARE(m_cm->contactIds(allCollections).size(), allCount + 2); // should have added local + aggregate
    allCount = m_cm->contactIds(allCollections).size();

    QList<QContact> allContacts = m_cm->contacts(allCollections);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    QContact localAlice;
    QContact aggregateAlice;
    bool foundLocalAlice = false;
    bool foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("7")) {
            if (curr.collectionId().localId() == localAddressbookId()) {
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(localAlice.id()));

    // now add another local contact.
    QContact bob;
    QContactName bn;
    bn.setFirstName("Bob7");
    bn.setMiddleName("The");
    bn.setLastName("Constructor");
    QContactPhoneNumber bp;
    bp.setNumber("777");
    bob.saveDetail(&bn);
    bob.saveDetail(&bp);
    QVERIFY(m_cm->saveContact(&bob));

    // we should have an extra aggregate (bob's) now too
    aggCount = m_cm->contactIds().size();

    // now attempt to remove aggregate alice - should fail.
    remSpyCount = remSpy.count();
    m_remAccumulatedIds.clear();
    QVERIFY(!m_cm->removeContact(removalId(aggregateAlice)));
    QTest::qWait(50);
    QCOMPARE(remSpy.count(), remSpyCount);
    QVERIFY(!m_remAccumulatedIds.contains(ContactId::apiId(localAlice)));
    QVERIFY(!m_remAccumulatedIds.contains(ContactId::apiId(aggregateAlice)));

    // now attempt to remove local alice - should succeed, and her "orphan" aggregate should be removed also.
    QVERIFY(m_cm->removeContact(removalId(localAlice)));
    QTRY_VERIFY(remSpy.count() > remSpyCount);
    QTRY_VERIFY(m_remAccumulatedIds.contains(ContactId::apiId(localAlice)));
    QTRY_VERIFY(m_remAccumulatedIds.contains(ContactId::apiId(aggregateAlice)));

    // alice's aggregate contact should have been removed, bob's should not have.
    QCOMPARE(m_cm->contactIds().size(), (aggCount-1));

    // and bob should not have been removed.
    QVERIFY(m_cm->contactIds(allCollections).contains(ContactId::apiId(bob)));
    QList<QContact> stillExisting = m_cm->contacts(allCollections);
    bool foundBob = false;
    foreach (const QContact &c, stillExisting) {
        if (c.id() == bob.id()) {
            foundBob = true;
            break;
        }
    }
    QVERIFY(foundBob);

    // now remove bob.
    QVERIFY(m_cm->removeContact(removalId(bob)));
    QVERIFY(!m_cm->contactIds(allCollections).contains(ContactId::apiId(bob)));

    // should be back to our original counts
    int newAggCount = m_cm->contactIds().size();
    int newAllCount = m_cm->contactIds(allCollections).size();
    QCOMPARE(newAggCount, oldAggCount);
    QCOMPARE(newAllCount, oldAllCount);
}

void tst_Aggregation::alterRelationships()
{
    QContactCollectionFilter allCollections;

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allCollections).size();

    // set up some signal spies
    QSignalSpy addSpy(m_cm, contactsAddedSignal);
    QSignalSpy remSpy(m_cm, contactsRemovedSignal);
    int addSpyCount = 0;
    int remSpyCount = 0;

    // add two test collections
    QContactCollection testAddressbook;
    testAddressbook.setMetaData(QContactCollection::KeyName, QStringLiteral("test"));
    testAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, "tst_aggregation");
    testAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID, 5);
    testAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH, "/addressbooks/test");
    QVERIFY(m_cm->saveCollection(&testAddressbook));

    QContactCollection trialAddressbook;
    trialAddressbook.setMetaData(QContactCollection::KeyName, QStringLiteral("trial"));
    trialAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, "tst_aggregation");
    trialAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID, 6);
    trialAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH, "/addressbooks/trial");
    QVERIFY(m_cm->saveCollection(&trialAddressbook));

    // now add two new contacts (with different collection ids)
    QContact alice;
    alice.setCollectionId(testAddressbook.id());

    QContactName an;
    an.setMiddleName("Alice");
    an.setFirstName("test");
    an.setLastName("alterRelationships");
    alice.saveDetail(&an);

    // Add a detail with non-empty detail URI - during the alteration, a duplicate
    // of the linked detail URI will exist in each aggregate, until the obsolete
    // aggregate is removed
    QContactPhoneNumber ap;
    ap.setNumber("1234567");
    ap.setSubTypes(QList<int>() << QContactPhoneNumber::SubTypeMobile);
    ap.setDetailUri("alice-alterRelationships-phone");
    alice.saveDetail(&ap);

    QContact bob;
    bob.setCollectionId(trialAddressbook.id());

    QContactName bn;
    bn.setMiddleName("Bob");
    bn.setLastName("alterRelationships");
    bob.saveDetail(&bn);

    QContactPhoneNumber bp;
    bp.setNumber("2345678");
    bp.setSubTypes(QList<int>() << QContactPhoneNumber::SubTypeMobile);
    bp.setDetailUri("bob-alterRelationships-phone");
    bob.saveDetail(&bp);

    QContactEmailAddress bem;
    bem.setEmailAddress("bob@wonderland.tld");
    bob.saveDetail(&bem);

    m_addAccumulatedIds.clear();
    QVERIFY(m_cm->saveContact(&alice));
    QVERIFY(m_cm->saveContact(&bob));
    QTRY_VERIFY(addSpy.count() >= addSpyCount + 2);
    QTRY_COMPARE(m_addAccumulatedIds.size(), 4); // should have added locals + aggregates
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(alice)));
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(bob)));
    addSpyCount = addSpy.count();

    QCOMPARE(m_cm->contactIds().size(), aggCount + 2); // 2 extra aggregate contacts
    aggCount = m_cm->contactIds().size();
    QCOMPARE(m_cm->contactIds(allCollections).size(), allCount + 4); // should have added 2 normal + 2 aggregates
    allCount = m_cm->contactIds(allCollections).size();

    QContact localAlice;
    QContact localBob;
    QContact aggregateAlice;
    QContact aggregateBob;

    QList<QContact> allContacts = m_cm->contacts(allCollections);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    foreach (const QContact &curr, allContacts) {
        QContactName currName = curr.detail<QContactName>();
        if (currName.middleName() == QLatin1String("Alice") && currName.lastName() == QLatin1String("alterRelationships")) {
            if (curr.collectionId() == testAddressbook.id()) {
                localAlice = curr;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateAlice = curr;
            }
        } else if (currName.middleName() == QLatin1String("Bob") && currName.lastName() == QLatin1String("alterRelationships")) {
            if (curr.collectionId() == trialAddressbook.id()) {
                localBob = curr;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateBob = curr;
            }
        }
    }

    QVERIFY(localAlice.id() != QContactId());
    QVERIFY(localBob.id() != QContactId());
    QVERIFY(aggregateAlice.id() != QContactId());
    QVERIFY(aggregateBob.id() != QContactId());
    QVERIFY(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(localBob.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateBob.id()));
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(localAlice.id()));
    QVERIFY(aggregateBob.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(localBob.id()));

    // Remove the aggregation relationship for Bob
    QContactRelationship relationship;
    relationship = makeRelationship(QContactRelationship::Aggregates, aggregateBob.id(), localBob.id());
    QVERIFY(m_cm->removeRelationship(relationship));

    // The childless aggregate should have been removed
    QTRY_VERIFY(remSpy.count() > remSpyCount);
    QTRY_COMPARE(m_remAccumulatedIds.size(), 1);
    QVERIFY(m_remAccumulatedIds.contains(ContactId::apiId(aggregateBob)));
    remSpyCount = remSpy.count();

    // A new aggregate should have been generated
    QTRY_VERIFY(addSpy.count() > addSpyCount);
    QTRY_COMPARE(m_addAccumulatedIds.size(), 5);
    addSpyCount = addSpy.count();

    // Verify the relationships
    QContactId oldAggregateBobId = aggregateBob.id();

    localAlice = QContact();
    localBob = QContact();
    aggregateAlice = QContact();
    aggregateBob = QContact();

    allContacts = m_cm->contacts(allCollections);
    foreach (const QContact &curr, allContacts) {
        QContactName currName = curr.detail<QContactName>();
        if (currName.middleName() == QLatin1String("Alice") && currName.lastName() == QLatin1String("alterRelationships")) {
            if (curr.collectionId() == testAddressbook.id()) {
                localAlice = curr;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateAlice = curr;
            }
        } else if (currName.middleName() == QLatin1String("Bob") && currName.lastName() == QLatin1String("alterRelationships")) {
            if (curr.collectionId() == trialAddressbook.id()) {
                localBob = curr;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateBob = curr;
            }
        }
    }

    QVERIFY(localAlice.id() != QContactId());
    QVERIFY(localBob.id() != QContactId());
    QVERIFY(aggregateAlice.id() != QContactId());
    QVERIFY(aggregateBob.id() != QContactId());
    QVERIFY(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(localBob.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateBob.id()));
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(localAlice.id()));
    QVERIFY(aggregateBob.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(localBob.id()));
    QVERIFY(aggregateBob.id() != oldAggregateBobId);

    // Aggregate localBob into aggregateAlice
    relationship = makeRelationship(QContactRelationship::Aggregates, aggregateAlice.id(), localBob.id());
    QVERIFY(m_cm->saveRelationship(&relationship));

    // Remove the relationship between localBob and aggregateBob
    relationship = makeRelationship(QContactRelationship::Aggregates, aggregateBob.id(), localBob.id());
    QVERIFY(m_cm->removeRelationship(relationship));

    // The childless aggregate should have been removed
    QTRY_VERIFY(remSpy.count() > remSpyCount);
    QTRY_COMPARE(m_remAccumulatedIds.size(), 2);
    QVERIFY(m_remAccumulatedIds.contains(ContactId::apiId(aggregateBob)));
    remSpyCount = remSpy.count();

    // No new aggregate should have been generated
    waitForSignalPropagation();
    QCOMPARE(addSpy.count(), addSpyCount);
    QCOMPARE(m_addAccumulatedIds.size(), 5);

    // Verify the relationships
    localAlice = QContact();
    localBob = QContact();
    aggregateAlice = QContact();
    aggregateBob = QContact();

    allContacts = m_cm->contacts(allCollections);
    foreach (const QContact &curr, allContacts) {
        QContactName currName = curr.detail<QContactName>();
        if (currName.middleName() == QLatin1String("Alice") && currName.lastName() == QLatin1String("alterRelationships")) {
            if (curr.collectionId() == testAddressbook.id()) {
                localAlice = curr;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateAlice = curr;
            }
        } else if (currName.middleName() == QLatin1String("Bob") && currName.lastName() == QLatin1String("alterRelationships")) {
            if (curr.collectionId() == trialAddressbook.id()) {
                localBob = curr;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateBob = curr;
            }
        }
    }

    QVERIFY(localAlice.id() != QContactId());
    QVERIFY(localBob.id() != QContactId());
    QVERIFY(aggregateAlice.id() != QContactId());
    QCOMPARE(aggregateBob.id(), QContactId());
    QVERIFY(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(localBob.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(localAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(localBob.id()));

    // ensure that Bob's details have been promoted to the aggregateAlice contact.
    QCOMPARE(aggregateAlice.detail<QContactEmailAddress>().emailAddress(), localBob.detail<QContactEmailAddress>().emailAddress());
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().size(), 2);

    // Change Bob to have the same first and last name details as Alice
    bn = localBob.detail<QContactName>();
    bn.setFirstName("test");
    localBob.saveDetail(&bn);
    QVERIFY(m_cm->saveContact(&localBob));

    // Test removing a relationship from a multi-child aggregate
    relationship = makeRelationship(QContactRelationship::Aggregates, aggregateAlice.id(), localAlice.id());
    QVERIFY(m_cm->removeRelationship(relationship));

    // No aggregate will be removed
    waitForSignalPropagation();
    QCOMPARE(remSpy.count(), remSpyCount);
    QCOMPARE(m_remAccumulatedIds.size(), 2);

    // No new aggregate should have been generated, since the aggregation process will find
    // the existing aggregate as the best candidate (due to same first/last name)

    // Note - this test is failing with qt4; the match-finding query is failing to find the
    // existing match, due to some error in binding values that I can't work out right now...
    QCOMPARE(addSpy.count(), addSpyCount);
    QCOMPARE(m_addAccumulatedIds.size(), 5);

    // Verify that the relationships are unchanged
    localAlice = m_cm->contact(retrievalId(localAlice));
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));
    QVERIFY(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(localAlice.id()));

    // Create an IsNot relationship to prevent re-aggregation
    relationship = makeRelationship(QString::fromLatin1("IsNot"), aggregateAlice.id(), localAlice.id());
    QVERIFY(m_cm->saveRelationship(&relationship));

    // Now remove the aggregation again
    relationship = makeRelationship(QContactRelationship::Aggregates, aggregateAlice.id(), localAlice.id());
    QVERIFY(m_cm->removeRelationship(relationship));

    // No aggregate will be removed
    waitForSignalPropagation();
    QCOMPARE(remSpy.count(), remSpyCount);
    QCOMPARE(m_remAccumulatedIds.size(), 2);

    // A new aggregate should have been generated, since the aggregation can't use the existing match
    QTRY_VERIFY(addSpy.count() > addSpyCount);
    QTRY_COMPARE(m_addAccumulatedIds.size(), 6);
    addSpyCount = addSpy.count();

    // Verify that the relationships are updated
    localAlice = m_cm->contact(retrievalId(localAlice));
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));
    QCOMPARE(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).size(), 1);
    QVERIFY(!localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(!aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(localAlice.id()));

    // ensure that each aggregate contains the details from the respective constituent contact.
    aggregateBob = aggregateAlice; // this one now only aggregates Bob
    QCOMPARE(aggregateBob.details<QContactPhoneNumber>().size(), 1);
    QCOMPARE(aggregateBob.detail<QContactPhoneNumber>().number(), localBob.detail<QContactPhoneNumber>().number());
    QCOMPARE(aggregateBob.details<QContactEmailAddress>().size(), 1);
    QCOMPARE(aggregateBob.detail<QContactEmailAddress>().emailAddress(), localBob.detail<QContactEmailAddress>().emailAddress());
    aggregateAlice = m_cm->contact(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).first());
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().size(), 1);
    QCOMPARE(aggregateAlice.detail<QContactPhoneNumber>().number(), localAlice.detail<QContactPhoneNumber>().number());
    QCOMPARE(aggregateAlice.details<QContactEmailAddress>().size(), 0);

    // finally, create two new local addresbook contacts,
    // which we will later manually aggregate together.
    QContact localEdmond;
    QContactName en;
    en.setMiddleName("Edmond");
    en.setFirstName("test");
    en.setLastName("alterRelationshipsLocal");
    localEdmond.saveDetail(&en);
    QContactPhoneNumber ep;
    ep.setNumber("8765432");
    ep.setSubTypes(QList<int>() << QContactPhoneNumber::SubTypeMobile);
    ep.setDetailUri("edmond-alterRelationships-phone");
    localEdmond.saveDetail(&ep);
    QContactHobby eh;
    eh.setHobby("Surfing");
    localEdmond.saveDetail(&eh);
    QContactGuid eg;
    eg.setGuid("alterRelationships-Edmond");
    localEdmond.saveDetail(&eg);

    QContact localFred;
    QContactName fn;
    fn.setMiddleName("Fred");
    fn.setFirstName("trial");
    fn.setLastName("alterRelationshipsLocal");
    localFred.saveDetail(&fn);
    QContactPhoneNumber fp;
    fp.setNumber("9876543");
    fp.setSubTypes(QList<int>() << QContactPhoneNumber::SubTypeMobile);
    fp.setDetailUri("fred-alterRelationships-phone");
    localFred.saveDetail(&fp);
    QContactHobby fh;
    fh.setHobby("Bowling");
    localFred.saveDetail(&fh);
    QContactGuid fg;
    fg.setGuid("alterRelationships-Fred");
    localFred.saveDetail(&fg);

    QVERIFY(m_cm->saveContact(&localEdmond));
    QVERIFY(m_cm->saveContact(&localFred));

    // fetch the contacts, ensure we have what we expect.
    QContact aggregateEdmond, aggregateFred;
    allContacts = m_cm->contacts(allCollections);
    foreach (const QContact &curr, allContacts) {
        QContactName currName = curr.detail<QContactName>();
        if (currName.middleName() == QLatin1String("Alice") && currName.lastName() == QLatin1String("alterRelationships")) {
            if (curr.collectionId() == testAddressbook.id()) {
                localAlice = curr;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateAlice = curr;
            }
        } else if (currName.middleName() == QLatin1String("Bob") && currName.lastName() == QLatin1String("alterRelationships")) {
            if (curr.collectionId() == trialAddressbook.id()) {
                localBob = curr;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateBob = curr;
            }
        } else if (currName.middleName() == QLatin1String("Edmond") && currName.lastName() == QLatin1String("alterRelationshipsLocal")) {
            if (curr.collectionId().localId() == localAddressbookId()) {
                localEdmond = curr;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateEdmond = curr;
            }
        } else if (currName.middleName() == QLatin1String("Fred") && currName.lastName() == QLatin1String("alterRelationshipsLocal")) {
            if (curr.collectionId().localId() == localAddressbookId()) {
                localFred = curr;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateFred = curr;
            }
        }
    }

    QCOMPARE(localEdmond.collectionId().localId(), localAddressbookId());
    QCOMPARE(localFred.collectionId().localId(), localAddressbookId());
    QCOMPARE(aggregateEdmond.collectionId().localId(), aggregateAddressbookId());
    QCOMPARE(aggregateFred.collectionId().localId(), aggregateAddressbookId());

    remSpyCount = remSpy.count();

    // Aggregate localFred into aggregateEdmond
    relationship = makeRelationship(QContactRelationship::Aggregates, aggregateEdmond.id(), localFred.id());
    QVERIFY(m_cm->saveRelationship(&relationship));

    // Remove the relationship between localFred and aggregateFred
    relationship = makeRelationship(QContactRelationship::Aggregates, aggregateFred.id(), localFred.id());
    QVERIFY(m_cm->removeRelationship(relationship));

    // The childless aggregate should have been removed
    QTRY_VERIFY(remSpy.count() > remSpyCount);
    QVERIFY(m_remAccumulatedIds.contains(ContactId::apiId(aggregateFred)));
    remSpyCount = remSpy.count();

    // Reload the aggregateEdmond, and ensure that it has the required details.
    aggregateEdmond = m_cm->contact(aggregateEdmond.id());
    QCOMPARE(aggregateEdmond.details<QContactPhoneNumber>().size(), 2);
    QCOMPARE(aggregateEdmond.details<QContactHobby>().size(), 2);
    QVERIFY((aggregateEdmond.details<QContactPhoneNumber>().at(0).number() == ep.number()
                || aggregateEdmond.details<QContactPhoneNumber>().at(1).number() == ep.number())
            && (aggregateEdmond.details<QContactPhoneNumber>().at(0).number() == fp.number()
                || aggregateEdmond.details<QContactPhoneNumber>().at(1).number() == fp.number()));
    QVERIFY((aggregateEdmond.details<QContactHobby>().at(0).hobby() == eh.hobby()
                || aggregateEdmond.details<QContactHobby>().at(1).hobby() == eh.hobby())
            && (aggregateEdmond.details<QContactHobby>().at(0).hobby() == fh.hobby()
                || aggregateEdmond.details<QContactHobby>().at(1).hobby() == fh.hobby()));
}

void tst_Aggregation::aggregationHeuristic_data()
{
    QTest::addColumn<bool>("shouldAggregate");
    QTest::addColumn<QString>("aFirstName");
    QTest::addColumn<QString>("aMiddleName");
    QTest::addColumn<QString>("aLastName");
    QTest::addColumn<QString>("aNickname");
    QTest::addColumn<QString>("aGender");
    QTest::addColumn<QString>("aPhoneNumber");
    QTest::addColumn<QString>("aEmailAddress");
    QTest::addColumn<QString>("aOnlineAccount");
    QTest::addColumn<QString>("bFirstName");
    QTest::addColumn<QString>("bMiddleName");
    QTest::addColumn<QString>("bLastName");
    QTest::addColumn<QString>("bNickname");
    QTest::addColumn<QString>("bGender");
    QTest::addColumn<QString>("bPhoneNumber");
    QTest::addColumn<QString>("bEmailAddress");
    QTest::addColumn<QString>("bOnlineAccount");

    // shared details / family members
    QTest::newRow("shared email") << false // husband and wife, sharing email, should not get aggregated
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "unspecified" << "" << "gumboots@test.com" << ""
        << "Jillian" << "Anastacia Faith" << "Gumboots" << "Jilly" << "unspecified" << "" << "gumboots@test.com" << "";
    QTest::newRow("shared phone") << false // husband and wife, sharing phone, should not get aggregated
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "unspecified" << "111992888337" << "" << ""
        << "Jillian" << "Anastacia Faith" << "Gumboots" << "Jilly" << "unspecified" << "111992888337" << "" << "";
    QTest::newRow("shared phone+email") << false // husband and wife, sharing phone+email, should not get aggregated
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "unspecified" << "111992888337" << "gumboots@test.com" << ""
        << "Jillian" << "Anastacia Faith" << "Gumboots" << "Jilly" << "unspecified" << "111992888337" << "gumboots@test.com" << "";
    QTest::newRow("shared phone+email+account") << false // husband and wife, sharing phone+email+account, should not get aggregated
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "unspecified" << "111992888337" << "gumboots@test.com" << "gumboots@familysocial"
        << "Jillian" << "Anastacia Faith" << "Gumboots" << "Jilly" << "unspecified" << "111992888337" << "gumboots@test.com" << "gumboots@familysocial";

    // different contactable details / same name
    QTest::newRow("match name, different p/e/a") << true // identical name match is enough to match the contact
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "unspecified" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "unspecified" << "999118222773" << "freddy@test.net" << "fgumboots@coolsocial";
    QTest::newRow("match name insentive, different p/e/a") << true
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "unspecified" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "frederick" << "william preston" << "Gumboots" << "Freddy" << "unspecified" << "999118222773" << "freddy@test.net" << "fgumboots@coolsocial";
    QTest::newRow("match hyphenated name, different p/e/a") << true
        << "Frederick-Albert" << "William Preston" << "Gumboots" << "Freddy" << "unspecified" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "Frederick-Albert" << "William Preston" << "Gumboots" << "Freddy" << "unspecified" << "999118222773" << "freddy@test.net" << "fgumboots@coolsocial";
    QTest::newRow("match hyphenated name insensitive, different p/e/a") << true
        << "Frederick-Albert" << "William Preston" << "Gumboots" << "Freddy" << "unspecified" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "frederick-albert" << "william preston" << "Gumboots" << "Freddy" << "unspecified" << "999118222773" << "freddy@test.net" << "fgumboots@coolsocial";

    // identical contacts should be aggregated
    QTest::newRow("identical, complete") << true
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "unspecified" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "unspecified" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount";
    QTest::newRow("identical, -fname") << true
        << "" << "William Preston" << "Gumboots" << "Freddy" << "unspecified" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "" << "William Preston" << "Gumboots" << "Freddy" << "unspecified" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount";
    QTest::newRow("identical, -mname") << true
        << "Frederick" << "" << "Gumboots" << "Freddy" << "unspecified" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "Frederick" << "" << "Gumboots" << "Freddy" << "unspecified" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount";
    QTest::newRow("identical, -lname") << true
        << "Frederick" << "William Preston" << "" << "Freddy" << "unspecified" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "Frederick" << "William Preston" << "" << "Freddy" << "unspecified" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount";
    QTest::newRow("identical, -nick") << true
        << "Frederick" << "William Preston" << "Gumboots" << "" << "unspecified" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "Frederick" << "William Preston" << "Gumboots" << "" << "unspecified" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount";
    QTest::newRow("identical, -phone") << true
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "unspecified" << "" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "unspecified" << "" << "gumboots@test.com" << "freddy00001@socialaccount";
    QTest::newRow("identical, -email") << true
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "unspecified" << "111992888337" << "" << "freddy00001@socialaccount"
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "unspecified" << "111992888337" << "" << "freddy00001@socialaccount";
    QTest::newRow("identical, -account") << true
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "unspecified" << "111992888337" << "gumboots@test.com" << ""
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "unspecified" << "111992888337" << "gumboots@test.com" << "";
    QTest::newRow("identical, diff nick") << true
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "unspecified" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "Frederick" << "William Preston" << "Gumboots" << "Ricky" << "unspecified" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount";

    // f/l name differences should stop aggregation.  middle name doesn't count in the aggregation heuristic.
    QTest::newRow("fname different") << false
        << "Frederick" << "" << "Gumboots" << "" << "unspecified" << "111992888337" << "" << ""
        << "Jillian" << "" << "Gumboots" << "" << "unspecified" << "999118222773" << "" << "";
    QTest::newRow("lname different") << false
        << "Frederick" << "" << "Gumboots" << "" << "unspecified" << "111992888337" << "" << ""
        << "Frederick" << "" << "Galoshes" << "" << "unspecified" << "999118222773" << "" << "";

    // similarities in name, different contactable details
    QTest::newRow("similar name, different p/e/a") << false // Only the last names match; not enough
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "unspecified" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "" << "" << "Gumboots" << "" << "unspecified" << "999118222773" << "anastacia@test.net" << "agumboots@coolsocial";

    // Gender differences prevent aggregation
    QTest::newRow("no gender specified") << true
        << "Sam" << "" << "Gumboots" << "Freddy" << "unspecified" << "" << "" << ""
        << "Sam" << "" << "Gumboots" << "Freddy" << "unspecified" << "" << "" << "";
    QTest::newRow("one gender specified male") << true
        << "Sam" << "" << "Gumboots" << "Freddy" << "Male" << "" << "" << ""
        << "Sam" << "" << "Gumboots" << "Freddy" << "unspecified" << "" << "" << "";
    QTest::newRow("one gender specified female") << true
        << "Sam" << "" << "Gumboots" << "Freddy" << "Female" << "" << "" << ""
        << "Sam" << "" << "Gumboots" << "Freddy" << "unspecified" << "" << "" << "";
    QTest::newRow("gender match male") << true
        << "Sam" << "" << "Gumboots" << "Freddy" << "Male" << "" << "" << ""
        << "Sam" << "" << "Gumboots" << "Freddy" << "Male" << "" << "" << "";
    QTest::newRow("gender match female") << true
        << "Sam" << "" << "Gumboots" << "Freddy" << "Female" << "" << "" << ""
        << "Sam" << "" << "Gumboots" << "Freddy" << "Female" << "" << "" << "";
    QTest::newRow("gender mismatch") << false
        << "Sam" << "" << "Gumboots" << "Freddy" << "Male" << "" << "" << ""
        << "Sam" << "" << "Gumboots" << "Freddy" << "Female" << "" << "" << "";

    // Nicknames should cause aggregation in the absence of real names
    QTest::newRow("nickname match") << true
        << "" << "" << "" << "Freddy" << "unspecified" << "" << "" << ""
        << "" << "" << "" << "Freddy" << "unspecified" << "" << "" << "";
    QTest::newRow("nickname mismatch") << false
        << "" << "" << "" << "Freddy" << "unspecified" << "" << "" << ""
        << "" << "" << "" << "Buster" << "unspecified" << "" << "" << "";
    QTest::newRow("nickname match with firstname") << false
        << "Frederick" << "" << "" << "Freddy" << "unspecified" << "" << "" << ""
        << "" << "" << "" << "Freddy" << "unspecified" << "" << "" << "";
    QTest::newRow("nickname match with lastname") << false
        << "" << "" << "Gumboots" << "Freddy" << "unspecified" << "" << "" << ""
        << "" << "" << "" << "Freddy" << "unspecified" << "" << "" << "";

    QTest::newRow("lname without detail match") << false
        << "" << "" << "Gumboots" << "" << "unspecified" << "" << "" << ""
        << "" << "" << "Gumboots" << "" << "unspecified" << "" << "" << "";
    QTest::newRow("lname using phonenumber") << true
        << "" << "" << "Gumboots" << "" << "unspecified" << "111992888337" << "" << ""
        << "" << "" << "Gumboots" << "" << "unspecified" << "111992888337" << "" << "";
    QTest::newRow("lname using multiple phonenumbers") << true
        << "" << "" << "Gumboots" << "" << "unspecified" << "111992888337" << "" << ""
        << "" << "" << "Gumboots" << "" << "unspecified" << "111992888338|111992888337" << "" << "";
    QTest::newRow("lname using email address") << true
        << "" << "" << "Gumboots" << "" << "unspecified" << "" << "gumboots@test.com" << ""
        << "" << "" << "Gumboots" << "" << "unspecified" << "" << "gumboots@test.com" << "";
    QTest::newRow("lname using multiple email addresses") << true
        << "" << "" << "Gumboots" << "" << "unspecified" << "" << "gumboots@test.com" << ""
        << "" << "" << "Gumboots" << "" << "unspecified" << "" << "wellingtons@test.com|gumboots@test.com" << "";
    QTest::newRow("lname using account uri") << true
        << "" << "" << "Gumboots" << "" << "unspecified" << "" << "" << "freddy00001@socialaccount"
        << "" << "" << "Gumboots" << "" << "unspecified" << "" << "" << "freddy00001@socialaccount";
    QTest::newRow("lname using multiple account uris") << true
        << "" << "" << "Gumboots" << "" << "unspecified" << "" << "" << "freddy00001@socialaccount"
        << "" << "" << "Gumboots" << "" << "unspecified" << "" << "" << "freddy11111@socialaccount|freddy00001@socialaccount";

    // partial name matches are no longer aggregated
    QTest::newRow("partial match name, different p/e/a") << false
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "unspecified" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "Fred" << "William Preston" << "Gumboots" << "Freddy" << "unspecified" << "999118222773" << "freddy@test.net" << "fgumboots@coolsocial";
    QTest::newRow("partial match name insentive, different p/e/a") << false
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "unspecified" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "fred" << "william preston" << "Gumboots" << "Freddy" << "unspecified" << "999118222773" << "freddy@test.net" << "fgumboots@coolsocial";
    QTest::newRow("partial match hyphenated name, different p/e/a") << false
        << "Frederick-Albert" << "William Preston" << "Gumboots" << "Freddy" << "unspecified" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "unspecified" << "999118222773" << "freddy@test.net" << "fgumboots@coolsocial";
    QTest::newRow("partial match hyphenated name insensitive, different p/e/a") << false
        << "Frederick-Albert" << "William Preston" << "Gumboots" << "Freddy" << "unspecified" << "111992888337" << "gumboots@test.com" << "freddy00001@socialaccount"
        << "frederick" << "william preston" << "Gumboots" << "Freddy" << "unspecified" << "999118222773" << "freddy@test.net" << "fgumboots@coolsocial";
}

void tst_Aggregation::aggregationHeuristic()
{
    // this test exists to validate the findMatchingAggregate query.
    QFETCH(bool, shouldAggregate);
    QFETCH(QString, aFirstName);
    QFETCH(QString, aMiddleName);
    QFETCH(QString, aLastName);
    QFETCH(QString, aNickname);
    QFETCH(QString, aGender);
    QFETCH(QString, aPhoneNumber);
    QFETCH(QString, aEmailAddress);
    QFETCH(QString, aOnlineAccount);
    QFETCH(QString, bFirstName);
    QFETCH(QString, bMiddleName);
    QFETCH(QString, bLastName);
    QFETCH(QString, bNickname);
    QFETCH(QString, bGender);
    QFETCH(QString, bPhoneNumber);
    QFETCH(QString, bEmailAddress);
    QFETCH(QString, bOnlineAccount);

    // add two test collections
    QContactCollection testAddressbook;
    testAddressbook.setMetaData(QContactCollection::KeyName, QStringLiteral("test"));
    testAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, "tst_aggregation");
    testAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID, 5);
    testAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH, "/addressbooks/test");
    QVERIFY(m_cm->saveCollection(&testAddressbook));

    QContactCollection trialAddressbook;
    trialAddressbook.setMetaData(QContactCollection::KeyName, QStringLiteral("trial"));
    trialAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, "tst_aggregation");
    trialAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID, 6);
    trialAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH, "/addressbooks/trial");
    QVERIFY(m_cm->saveCollection(&trialAddressbook));

    for (int i = 0; i < 2; ++i) {
        QContact a, b;
        QContactName aname, bname;
        QContactNickname anick, bnick;
        QContactGender agen, bgen;
        QContactPhoneNumber aphn, bphn;
        QContactEmailAddress aem, bem;
        QContactOnlineAccount aoa, boa;

        // construct a
        a.setCollectionId(testAddressbook.id());

        if (!aFirstName.isEmpty() || !aMiddleName.isEmpty() || !aLastName.isEmpty()) {
            aname.setFirstName(aFirstName);
            aname.setMiddleName(aMiddleName);
            aname.setLastName(aLastName);
            a.saveDetail(&aname);
        }

        if (!aNickname.isEmpty()) {
            anick.setNickname(aNickname);
            a.saveDetail(&anick);
        }

        if (aGender != QString::fromLatin1("unspecified")) {
            agen.setGender(aGender == QString::fromLatin1("Male") ? QContactGender::GenderMale : QContactGender::GenderFemale);
            a.saveDetail(&agen);
        }

        if (!aPhoneNumber.isEmpty()) {
            aphn.setNumber(aPhoneNumber);
            a.saveDetail(&aphn);
        }

        if (!aEmailAddress.isEmpty()) {
            aem.setEmailAddress(aEmailAddress);
            a.saveDetail(&aem);
        }

        if (!aOnlineAccount.isEmpty()) {
            aoa.setAccountUri(aOnlineAccount);
            a.saveDetail(&aoa);
        }

        // construct b
        b.setCollectionId(trialAddressbook.id());

        if (!bFirstName.isEmpty() || !bMiddleName.isEmpty() || !bLastName.isEmpty()) {
            bname.setFirstName(bFirstName);
            bname.setMiddleName(bMiddleName);
            bname.setLastName(bLastName);
            b.saveDetail(&bname);
        }

        if (!bNickname.isEmpty()) {
            bnick.setNickname(bNickname);
            b.saveDetail(&bnick);
        }

        if (bGender != QString::fromLatin1("unspecified")) {
            bgen.setGender(bGender == QString::fromLatin1("Male") ? QContactGender::GenderMale : QContactGender::GenderFemale);
            b.saveDetail(&bgen);
        }

        if (!bPhoneNumber.isEmpty()) {
            foreach (QString number, bPhoneNumber.split(QString::fromLatin1("|"))){
                bphn = QContactPhoneNumber();
                bphn.setNumber(number);
                b.saveDetail(&bphn);
            }
        }

        if (!bEmailAddress.isEmpty()) {
            foreach (QString address, bEmailAddress.split(QString::fromLatin1("|"))){
                bem = QContactEmailAddress();
                bem.setEmailAddress(address);
                b.saveDetail(&bem);
            }
        }

        if (!bOnlineAccount.isEmpty()) {
            foreach (QString address, bOnlineAccount.split(QString::fromLatin1("|"))){
                bphn = QContactOnlineAccount();
                boa.setAccountUri(address);
                b.saveDetail(&boa);
            }
        }

        // Now perform the saves and see if we get some aggregation as required.
        int count = m_cm->contactIds().count();
        QVERIFY(m_cm->saveContact(i == 0 ? &a : &b));
        QCOMPARE(m_cm->contactIds().count(), (count+1));
        QVERIFY(m_cm->saveContact(i == 0 ? &b : &a));
        QCOMPARE(m_cm->contactIds().count(), shouldAggregate ? (count+1) : (count+2));

        QVERIFY(m_cm->removeContact(a.id()));
        QVERIFY(m_cm->removeContact(b.id()));
    }
}

void tst_Aggregation::regenerateAggregate()
{
    // here we create a local contact, and then save it
    // and then we create a "synced" contact, which should "match" it.
    // It should be related to the aggregate created for the sync.
    // We then remove the synced contact, which should cause the aggregate
    // to be "regenerated" from the remaining aggregated contacts
    // (which in this case, is just the local contact).

    QContactCollectionFilter allCollections;

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allCollections).size();

    // set up some signal spies
    QSignalSpy addSpy(m_cm, contactsAddedSignal);
    QSignalSpy chgSpy(m_cm, contactsChangedSignal);
    int addSpyCount = 0;
    int chgSpyCount = 0;

    // now add a new local contact (no collectionId specified == automatically local)
    QContact alice;

    QContactName an;
    an.setFirstName("Alice8");
    an.setMiddleName("In");
    an.setLastName("Wonderland");
    alice.saveDetail(&an);

    QContactPhoneNumber aph;
    aph.setNumber("88888");
    alice.saveDetail(&aph);

    QContactEmailAddress aem;
    aem.setEmailAddress("alice8@test.com");
    alice.saveDetail(&aem);

    m_addAccumulatedIds.clear();
    QVERIFY(m_cm->saveContact(&alice));
    QTRY_VERIFY(addSpy.count() > addSpyCount); // should have added local + aggregate
    QTRY_COMPARE(m_addAccumulatedIds.size(), 2);
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(alice)));
    addSpyCount = addSpy.count();

    QCOMPARE(m_cm->contactIds().size(), aggCount + 1); // 1 extra aggregate contact
    aggCount = m_cm->contactIds().size();
    QCOMPARE(m_cm->contactIds(allCollections).size(), allCount + 2); // should have added local + aggregate
    allCount = m_cm->contactIds(allCollections).size();

    QList<QContact> allContacts = m_cm->contacts(allCollections);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    QContact localAlice;
    QContact aggregateAlice;
    bool foundLocalAlice = false;
    bool foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactEmailAddress currEm = curr.detail<QContactEmailAddress>();
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice8")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("88888")
                && currEm.emailAddress() == QLatin1String("alice8@test.com")) {
            if (curr.collectionId().localId() == localAddressbookId()) {
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(localAlice.id()));

    // now add the doppleganger from another sync source
    QContactCollection testAddressbook;
    testAddressbook.setMetaData(QContactCollection::KeyName, QStringLiteral("test"));
    testAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, "tst_aggregation");
    testAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID, 5);
    testAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH, "/addressbooks/test");
    QVERIFY(m_cm->saveCollection(&testAddressbook));

    QContact syncAlice;
    syncAlice.setCollectionId(testAddressbook.id());

    QContactName san;
    san.setFirstName(an.firstName());
    san.setMiddleName(an.middleName());
    san.setLastName(an.lastName());
    syncAlice.saveDetail(&san);

    QContactPhoneNumber saph;
    saph.setNumber(aph.number());
    syncAlice.saveDetail(&saph);

    QContactEmailAddress saem;
    saem.setEmailAddress(aem.emailAddress());
    syncAlice.saveDetail(&saem);

    QContactHobby sah; // this is a "new" detail which doesn't appear in the local contact.
    sah.setHobby(QLatin1String("tennis"));
    syncAlice.saveDetail(&sah);

    // DON'T clear the m_addAccumulatedIds list here.
    // DO clear the m_chgAccumulatedIds list here, though.
    chgSpyCount = chgSpy.count();
    m_chgAccumulatedIds.clear();
    QVERIFY(m_cm->saveContact(&syncAlice));
    QTRY_VERIFY(addSpy.count() > addSpyCount); // should have added test but not an aggregate - aggregate already exists
    QTRY_VERIFY(chgSpy.count() > chgSpyCount); // should have updated the aggregate
    QTRY_COMPARE(m_addAccumulatedIds.size(), 3);
    QTRY_COMPARE(m_chgAccumulatedIds.size(), 1); // the aggregate should have been updated (with the hobby)
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(localAlice)));
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(aggregateAlice)));
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(syncAlice)));
    QVERIFY(m_chgAccumulatedIds.contains(ContactId::apiId(aggregateAlice)));
    addSpyCount = addSpy.count();

    QCOMPARE(m_cm->contactIds().size(), aggCount); // no extra aggregate contact
    aggCount = m_cm->contactIds().size();
    QCOMPARE(m_cm->contactIds(allCollections).size(), allCount + 1); // should have added test but not an aggregate
    allCount = m_cm->contactIds(allCollections).size();

    allContacts = m_cm->contacts(allCollections);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    QContact testAlice;
    bool foundTestAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactEmailAddress currEm = curr.detail<QContactEmailAddress>();
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice8")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("88888")
                && currEm.emailAddress() == QLatin1String("alice8@test.com")) {
            if (curr.collectionId().localId() == localAddressbookId()) {
                QCOMPARE(curr.detail<QContactHobby>().value<QString>(QContactHobby::FieldHobby), QString()); // local shouldn't get it
                localAlice = curr;
                foundLocalAlice = true;
            } else if (curr.collectionId() == testAddressbook.id()) {
                QCOMPARE(curr.detail<QContactHobby>().value<QString>(QContactHobby::FieldHobby), QLatin1String("tennis")); // came from here
                testAlice = curr;
                foundTestAlice = true;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                QCOMPARE(curr.detail<QContactHobby>().value<QString>(QContactHobby::FieldHobby), QLatin1String("tennis")); // aggregated to here
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundTestAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(testAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(localAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(testAlice.id()));

    // now remove the "test" sync contact
    QVERIFY(m_cm->removeContact(removalId(testAlice)));
    QVERIFY(!m_cm->contactIds(allCollections).contains(ContactId::apiId(testAlice))); // should have been removed

    // but the other contacts should NOT have been removed
    QVERIFY(m_cm->contactIds(allCollections).contains(ContactId::apiId(localAlice)));
    QVERIFY(m_cm->contactIds(allCollections).contains(ContactId::apiId(aggregateAlice)));

    // reload them, and ensure that the "hobby" detail has been removed from the aggregate
    allContacts = m_cm->contacts(allCollections);
    foreach (const QContact &curr, allContacts) {
        QContactEmailAddress currEm = curr.detail<QContactEmailAddress>();
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice8")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("88888")
                && currEm.emailAddress() == QLatin1String("alice8@test.com")) {
            if (curr.collectionId().localId() == localAddressbookId()) {
                QCOMPARE(curr.detail<QContactHobby>().value<QString>(QContactHobby::FieldHobby), QString());
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                QCOMPARE(curr.detail<QContactHobby>().value<QString>(QContactHobby::FieldHobby), QString());
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }
}

void tst_Aggregation::detailUris()
{
    QContactCollectionFilter allCollections;

    // save alice.  Some details will have a detailUri or linkedDetailUris
    QContact alice;
    QContactName an;
    an.setFirstName("Alice9");
    an.setMiddleName("In");
    an.setLastName("Wonderland");
    alice.saveDetail(&an);
    QContactPhoneNumber aph;
    aph.setNumber("99999");
    aph.setDetailUri("alice9PhoneNumberDetailUri");
    alice.saveDetail(&aph);
    QContactEmailAddress aem;
    aem.setEmailAddress("alice9@test.com");
    aem.setLinkedDetailUris("alice9PhoneNumberDetailUri");
    alice.saveDetail(&aem);
    QVERIFY(m_cm->saveContact(&alice));

    QList<QContact> allContacts = m_cm->contacts(allCollections);
    QContact localAlice;
    QContact aggregateAlice;
    foreach (const QContact &curr, allContacts) {
        QContactEmailAddress currEm = curr.detail<QContactEmailAddress>();
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice9")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("99999")
                && currEm.emailAddress() == QLatin1String("alice9@test.com")) {
            if (curr.collectionId().localId() == localAddressbookId()) {
                localAlice = curr;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateAlice = curr;
            }
        }
    }

    QVERIFY(!localAlice.id().isNull());
    QVERIFY(!aggregateAlice.id().isNull());

    // now check to ensure that the detail uris and links were updated correctly
    // in the aggregate.  Those uris need to be unique in the database.
    QCOMPARE(localAlice.detail<QContactPhoneNumber>().detailUri(), QLatin1String("alice9PhoneNumberDetailUri"));
    QVERIFY(aggregateAlice.detail<QContactPhoneNumber>().detailUri().startsWith(QLatin1String("aggregate:")));
    QVERIFY(aggregateAlice.detail<QContactPhoneNumber>().detailUri().endsWith(QLatin1String(":alice9PhoneNumberDetailUri")));
    QCOMPARE(localAlice.detail<QContactEmailAddress>().linkedDetailUris(), QStringList() << QLatin1String("alice9PhoneNumberDetailUri"));
    QCOMPARE(aggregateAlice.detail<QContactEmailAddress>().linkedDetailUris().count(), 1);
    QVERIFY(aggregateAlice.detail<QContactEmailAddress>().linkedDetailUris().at(0).startsWith(QLatin1String("aggregate:")));
    QVERIFY(aggregateAlice.detail<QContactEmailAddress>().linkedDetailUris().at(0).endsWith(QLatin1String(":alice9PhoneNumberDetailUri")));

    // try to add another detail with a conflicting detail URI
    QContact failAlice(alice);

    QContactTag at;
    at.setTag("fail");
    at.setDetailUri("alice9PhoneNumberDetailUri");
    failAlice.saveDetail(&at);
    QCOMPARE(m_cm->saveContact(&failAlice), false);

    // now perform an update of the local contact.  This should also trigger regeneration of the aggregate.
    QContactHobby ah;
    ah.setHobby("tennis");
    ah.setDetailUri("alice9HobbyDetailUri");
    localAlice.saveDetail(&ah);
    QVERIFY(m_cm->saveContact(&localAlice));

    // reload them both
    allContacts = m_cm->contacts(allCollections);
    localAlice = QContact();
    aggregateAlice = QContact();
    foreach (const QContact &curr, allContacts) {
        QContactEmailAddress currEm = curr.detail<QContactEmailAddress>();
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice9")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("99999")
                && currEm.emailAddress() == QLatin1String("alice9@test.com")) {
            if (curr.collectionId().localId() == localAddressbookId()) {
                localAlice = curr;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateAlice = curr;
            }
        }
    }

    QVERIFY(!localAlice.id().isNull());
    QVERIFY(!aggregateAlice.id().isNull());

    // now check to ensure that the detail uris and links were updated correctly
    // in the aggregate.  Those uris need to be unique in the database.
    QCOMPARE(localAlice.detail<QContactPhoneNumber>().detailUri(), QLatin1String("alice9PhoneNumberDetailUri"));
    QVERIFY(aggregateAlice.detail<QContactPhoneNumber>().detailUri().startsWith(QLatin1String("aggregate:")));
    QVERIFY(aggregateAlice.detail<QContactPhoneNumber>().detailUri().endsWith(QLatin1String(":alice9PhoneNumberDetailUri")));
    QCOMPARE(localAlice.detail<QContactEmailAddress>().linkedDetailUris(), QStringList() << QLatin1String("alice9PhoneNumberDetailUri"));
    QCOMPARE(aggregateAlice.detail<QContactEmailAddress>().linkedDetailUris().count(), 1);
    QVERIFY(aggregateAlice.detail<QContactEmailAddress>().linkedDetailUris().at(0).startsWith(QLatin1String("aggregate:")));
    QVERIFY(aggregateAlice.detail<QContactEmailAddress>().linkedDetailUris().at(0).endsWith(QLatin1String(":alice9PhoneNumberDetailUri")));
    QCOMPARE(localAlice.detail<QContactHobby>().detailUri(), QLatin1String("alice9HobbyDetailUri"));
    QVERIFY(aggregateAlice.detail<QContactHobby>().detailUri().startsWith(QLatin1String("aggregate:")));
    QVERIFY(aggregateAlice.detail<QContactHobby>().detailUri().endsWith(QLatin1String(":alice9HobbyDetailUri")));
}

void tst_Aggregation::correctDetails()
{
    QContact a, b, c, d;
    QContactName an, bn, cn, dn;
    QContactPhoneNumber ap, bp, cp, dp;
    QContactEmailAddress ae, be, ce, de;
    QContactHobby ah, bh, ch, dh;

    an.setFirstName("a"); an.setLastName("A");
    bn.setFirstName("b"); bn.setLastName("B");
    cn.setFirstName("c"); cn.setLastName("C");
    dn.setFirstName("d"); dn.setLastName("D");

    ap.setNumber("123");
    bp.setNumber("234");
    cp.setNumber("345");
    dp.setNumber("456");

    ae.setEmailAddress("a@test.com");
    be.setEmailAddress("b@test.com");
    ce.setEmailAddress("c@test.com");
    de.setEmailAddress("d@test.com");

    ah.setHobby("soccer");
    bh.setHobby("tennis");
    ch.setHobby("squash");

    a.saveDetail(&an); a.saveDetail(&ap); a.saveDetail(&ae); a.saveDetail(&ah);
    b.saveDetail(&bn); b.saveDetail(&bp); b.saveDetail(&be); b.saveDetail(&bh);
    c.saveDetail(&cn); c.saveDetail(&cp); c.saveDetail(&ce); c.saveDetail(&ch);
    d.saveDetail(&dn); d.saveDetail(&dp); d.saveDetail(&de);

    QList<QContact> saveList;
    saveList << a << b << c << d;
    m_cm->saveContacts(&saveList);

    QContactCollectionFilter allCollections;
    QList<QContact> allContacts = m_cm->contacts(allCollections);

    QVERIFY(allContacts.size() >= saveList.size()); // at least that amount, maybe more (aggregates)
    for (int i = 0; i < allContacts.size(); ++i) {
        QContact curr = allContacts.at(i);
        bool needsComparison = true;
        QContact xpct;
        if (curr.detail<QContactName>().value(QContactName::FieldFirstName) ==
               a.detail<QContactName>().value(QContactName::FieldFirstName)) {
            xpct = a;
        } else if (curr.detail<QContactName>().value(QContactName::FieldFirstName) ==
                      b.detail<QContactName>().value(QContactName::FieldFirstName)) {
            xpct = b;
        } else if (curr.detail<QContactName>().value(QContactName::FieldFirstName) ==
                      c.detail<QContactName>().value(QContactName::FieldFirstName)) {
            xpct = c;
        } else if (curr.detail<QContactName>().value(QContactName::FieldFirstName) ==
                      d.detail<QContactName>().value(QContactName::FieldFirstName)) {
            xpct = d;
        } else {
            needsComparison = false;
        }

        if (needsComparison) {
            //qWarning() << "actual:" << i
            //           << curr.detail<QContactSyncTarget>().value(QContactSyncTarget::FieldSyncTarget)
            //           << curr.detail<QContactName>().value(QContactName::FieldFirstName)
            //           << curr.detail<QContactName>().value(QContactName::FieldLastName)
            //           << curr.detail<QContactPhoneNumber>().value(QContactPhoneNumber::FieldNumber)
            //           << curr.detail<QContactEmailAddress>().value(QContactEmailAddress::FieldEmailAddress)
            //           << curr.detail<QContactHobby>().value(QContactHobby::FieldHobby);
            //qWarning() << "expected:" << i
            //           << xpct.detail<QContactSyncTarget>().value(QContactSyncTarget::FieldSyncTarget)
            //           << xpct.detail<QContactName>().value(QContactName::FieldFirstName)
            //           << xpct.detail<QContactName>().value(QContactName::FieldLastName)
            //           << xpct.detail<QContactPhoneNumber>().value(QContactPhoneNumber::FieldNumber)
            //           << xpct.detail<QContactEmailAddress>().value(QContactEmailAddress::FieldEmailAddress)
            //           << xpct.detail<QContactHobby>().value(QContactHobby::FieldHobby);
            QCOMPARE(curr.detail<QContactPhoneNumber>().value(QContactPhoneNumber::FieldNumber),
                     xpct.detail<QContactPhoneNumber>().value(QContactPhoneNumber::FieldNumber));
            QCOMPARE(curr.detail<QContactEmailAddress>().value(QContactEmailAddress::FieldEmailAddress),
                     xpct.detail<QContactEmailAddress>().value(QContactEmailAddress::FieldEmailAddress));
            QCOMPARE(curr.detail<QContactHobby>().value(QContactHobby::FieldHobby),
                     xpct.detail<QContactHobby>().value(QContactHobby::FieldHobby));
        }
    }
}

void tst_Aggregation::batchSemantics()
{
    // TODO: the following comment is no longer true; we still apply batch semantics rules
    // for simplification of possible cases, however

    // for performance reasons, the engine assumes:
    // 1) collectionId of all contacts in a batch save must be the same
    // 2) no two contacts from the same collection should be aggregated together

    QContactCollectionFilter allCollections;
    QList<QContact> allContacts = m_cm->contacts(allCollections);
    int allContactsCount = allContacts.size();

    QContactCollection testAddressbook;
    testAddressbook.setMetaData(QContactCollection::KeyName, QStringLiteral("test"));
    testAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, "tst_aggregation");
    testAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID, 5);
    testAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH, "/addressbooks/test");
    QVERIFY(m_cm->saveCollection(&testAddressbook));

    QContactCollection trialAddressbook;
    trialAddressbook.setMetaData(QContactCollection::KeyName, QStringLiteral("trial"));
    trialAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, "tst_aggregation");
    trialAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID, 6);
    trialAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH, "/addressbooks/trial");
    QVERIFY(m_cm->saveCollection(&trialAddressbook));

    QContact a, b, c;
    b.setCollectionId(testAddressbook.id());
    c.setCollectionId(trialAddressbook.id());

    QContactName aname, bname, cname;
    aname.setFirstName("a");
    aname.setLastName("batch");
    bname.setFirstName("b");
    bname.setLastName("batch");
    cname.setFirstName("c");
    cname.setLastName("batch");

    a.saveDetail(&aname);
    b.saveDetail(&bname);
    c.saveDetail(&cname);

    // a) batch save should fail due to different collection ids.
    QList<QContact> saveList;
    saveList << a << b << c;
    QVERIFY(!m_cm->saveContacts(&saveList));

    // b) same as (a)
    c.setCollectionId(testAddressbook.id()); // move addressbooks.
    saveList.clear();
    saveList << a << b << c;
    QVERIFY(!m_cm->saveContacts(&saveList));

    // c) same as (a) although in this case, local / empty are considered identical
    b.setCollectionId(QContactCollectionId());
    saveList.clear();
    saveList << a << b << c;
    QVERIFY(!m_cm->saveContacts(&saveList));

    // d) now it should succeed.
    c.setCollectionId(QContactCollectionId());
    saveList.clear();
    saveList << a << b << c;
    QVERIFY(m_cm->saveContacts(&saveList));

    allContacts = m_cm->contacts(allCollections);
    int newContactsCount = allContacts.size() - allContactsCount;
    QCOMPARE(newContactsCount, 6); // 3 local, 3 aggregate

    // Now we test the semantic of "two contacts from the same collection should get aggregated if they match"
    QContact d, e;
    d.setCollectionId(trialAddressbook.id());
    e.setCollectionId(trialAddressbook.id());

    QContactName dname, ename;
    dname.setFirstName("d");
    dname.setLastName("batch");
    ename.setFirstName("d");
    ename.setLastName("batch");

    d.saveDetail(&dname);
    e.saveDetail(&ename);

    saveList.clear();
    saveList << d << e;
    QVERIFY(m_cm->saveContacts(&saveList));

    allContacts = m_cm->contacts(allCollections);
    newContactsCount = allContacts.size() - allContactsCount;
    QCOMPARE(newContactsCount, 9); // 5 local, 4 aggregate - d and e should have been aggregated into one.
}

void tst_Aggregation::customSemantics()
{
    // the qtcontacts-sqlite engine defines some custom semantics
    // 1) avatars have a custom "AvatarMetadata" field
    // 2) self contact cannot be changed, and its id will always be "1" (aggregate=2)

    // ensure that the AvatarMetadata field is supported.
    QContact alice;
    QContactName an;
    an.setFirstName("Alice");
    alice.saveDetail(&an);
    QContactAvatar aa;
    aa.setImageUrl(QUrl(QString::fromLatin1("test.png")));
    aa.setValue(QContactAvatar::FieldMetaData, "cover");
    alice.saveDetail(&aa);
    QVERIFY(m_cm->saveContact(&alice));
    QContact aliceReloaded = m_cm->contact(retrievalId(alice));
    QCOMPARE(aliceReloaded.detail<QContactName>().value<QString>(QContactName::FieldFirstName), QLatin1String("Alice"));
    QCOMPARE(QUrl(aliceReloaded.detail<QContactAvatar>().value<QString>(QContactAvatar::FieldImageUrl)).toString(), QUrl(QString::fromLatin1("test.png")).toString());
    QCOMPARE(aliceReloaded.detail<QContactAvatar>().value<QString>(QContactAvatar::FieldMetaData), QLatin1String("cover"));

    // test the self contact semantics
    QCOMPARE(m_cm->selfContactId(), ContactId::apiId(2, m_cm->managerUri()));
    QVERIFY(!m_cm->setSelfContactId(ContactId::apiId(alice)));

    // ensure we cannot delete the self contact.
    QVERIFY(!m_cm->removeContact(ContactId::apiId(1, m_cm->managerUri())));
    QVERIFY(!m_cm->removeContact(ContactId::apiId(2, m_cm->managerUri())));
    QVERIFY(m_cm->removeContact(removalId(alice)));
}

void tst_Aggregation::changeLogFiltering()
{
    // The qtcontacts-sqlite engine automatically adds creation timestamp
    // if not already set.  It always clobbers (updates) modification timestamp.

    QTest::qWait(1); // wait for millisecond change, to ensure unique timestamps for saved contacts.
    QDateTime startTime = QDateTime::currentDateTimeUtc();
    QDateTime minus5 = startTime.addDays(-5);
    QDateTime minus3 = startTime.addDays(-3);
    QDateTime minus2 = startTime.addDays(-2);

    // 1) if provided, creation timestamp should not be overwritten.
    //    if not provided, modification timestamp should be set by the backend.
    QContact a;
    QContactName an;
    an.setFirstName("Alice");
    a.saveDetail(&an);
    QContactTimestamp at;
    at.setCreated(minus5);
    a.saveDetail(&at);

    QTest::qWait(1);
    QDateTime justPrior = QDateTime::currentDateTimeUtc();
    QVERIFY(m_cm->saveContact(&a));
    a = m_cm->contact(retrievalId(a));
    at = a.detail<QContactTimestamp>();
    QCOMPARE(at.created(), minus5);
    QVERIFY(at.lastModified() >= justPrior);
    QVERIFY(at.lastModified() <= QDateTime::currentDateTimeUtc());

    // 2) even if modified timestamp is provided, it should be updated by the  backend.
    at.setLastModified(minus2);
    a.saveDetail(&at);
    QTest::qWait(1);
    justPrior = QDateTime::currentDateTimeUtc();
    QVERIFY(m_cm->saveContact(&a));
    a = m_cm->contact(retrievalId(a));
    at = a.detail<QContactTimestamp>();
    QCOMPARE(at.created(), minus5);
    QVERIFY(at.lastModified() >= justPrior);
    QVERIFY(at.lastModified() <= QDateTime::currentDateTimeUtc());

    // 3) created timestamp should only be generated on creation, not normal save.
    at.setCreated(QDateTime());
    a.saveDetail(&at);
    QTest::qWait(1);
    justPrior = QDateTime::currentDateTimeUtc();
    QVERIFY(m_cm->saveContact(&a));
    a = m_cm->contact(retrievalId(a));
    at = a.detail<QContactTimestamp>();
    QCOMPARE(at.created(), QDateTime());
    QVERIFY(at.lastModified() >= justPrior);
    QVERIFY(at.lastModified() <= QDateTime::currentDateTimeUtc());

    // Generate a timestamp which is before b's created timestamp.
    QTest::qWait(1);
    QDateTime beforeBCreated = QDateTime::currentDateTimeUtc();

    QContact b;
    QContactName bn;
    bn.setFirstName("Bob");
    b.saveDetail(&bn);
    QTest::qWait(1);
    justPrior = QDateTime::currentDateTimeUtc();
    QVERIFY(m_cm->saveContact(&b));
    b = m_cm->contact(retrievalId(b));
    QContactTimestamp bt = b.detail<QContactTimestamp>();
    QVERIFY(bt.created() >= justPrior);
    QVERIFY(bt.created() <= QDateTime::currentDateTimeUtc());
    QVERIFY(bt.lastModified() >= justPrior);
    QVERIFY(bt.lastModified() <= QDateTime::currentDateTimeUtc());

    // Generate a timestamp which is after b's lastModified timestamp but which
    // will be before a's lastModified timestamp due to the upcoming save.
    QTest::qWait(1);
    QDateTime betweenTime = QDateTime::currentDateTimeUtc();

    // 4) ensure filtering works as expected.
    // First, ensure timestamps are filterable;
    // invalid date times are always included in filtered results.
    at.setCreated(minus5);
    a.saveDetail(&at);
    QTest::qWait(1);
    justPrior = QDateTime::currentDateTimeUtc();
    QVERIFY(m_cm->saveContact(&a));
    a = m_cm->contact(retrievalId(a));
    at = a.detail<QContactTimestamp>();
    QCOMPARE(at.created(), minus5);
    QVERIFY(at.lastModified() >= justPrior);
    QVERIFY(at.lastModified() <= QDateTime::currentDateTimeUtc());

    QContactCollectionFilter localFilter;
    localFilter.setCollectionId(QContactCollectionId(m_cm->managerUri(), localAddressbookId()));
    QContactCollectionFilter aggFilter;
    aggFilter.setCollectionId(QContactCollectionId(m_cm->managerUri(), aggregateAddressbookId()));
    QContactIntersectionFilter cif;
    QContactChangeLogFilter clf;

    clf.setEventType(QContactChangeLogFilter::EventAdded);
    clf.setSince(beforeBCreated); // should contain b, but not a as a's creation time was days-5
    cif.clear(); cif << localFilter << clf;
    QList<QContactId> filtered = m_cm->contactIds(cif);
    QVERIFY(!filtered.contains(retrievalId(a)));
    QVERIFY(filtered.contains(retrievalId(b)));

    clf.setEventType(QContactChangeLogFilter::EventAdded);
    clf.setSince(betweenTime);   // should not contain either a or b
    cif.clear(); cif << localFilter << clf;
    filtered = m_cm->contactIds(cif);
    QVERIFY(!filtered.contains(retrievalId(a)));
    QVERIFY(!filtered.contains(retrievalId(b)));

    clf.setEventType(QContactChangeLogFilter::EventChanged);
    clf.setSince(betweenTime);   // should contain a (modified after betweenTime) but not b (modified before)
    cif.clear(); cif << localFilter << clf;
    filtered = m_cm->contactIds(cif);
    QVERIFY(filtered.contains(retrievalId(a)));
    QVERIFY(!filtered.contains(retrievalId(b)));

    clf.setEventType(QContactChangeLogFilter::EventChanged);
    clf.setSince(startTime);     // should contain both a and b
    cif.clear(); cif << localFilter << clf;
    filtered = m_cm->contactIds(cif);
    QVERIFY(filtered.contains(retrievalId(a)));
    QVERIFY(filtered.contains(retrievalId(b)));

    // Filtering for removed contactIds is supported
    clf.setEventType(QContactChangeLogFilter::EventRemoved);
    clf.setSince(startTime);     // should contain neither a nor b
    filtered = m_cm->contactIds(clf);
    QVERIFY(!filtered.contains(retrievalId(a)));
    QVERIFY(!filtered.contains(retrievalId(b)));

    // Filtering in combination with syncTarget filtering is also supported
    cif.clear(); cif << localFilter << clf;
    filtered = m_cm->contactIds(cif);
    QVERIFY(!filtered.contains(retrievalId(a)));
    QVERIFY(!filtered.contains(retrievalId(b)));

    // Either order of intersected filters is the same
    cif.clear(); cif << clf << localFilter;
    filtered = m_cm->contactIds(cif);
    QVERIFY(!filtered.contains(retrievalId(a)));
    QVERIFY(!filtered.contains(retrievalId(b)));

    QContactId idA(removalId(a));
    QVERIFY(m_cm->removeContact(idA));

    QTest::qWait(1);
    QDateTime postDeleteTime = QDateTime::currentDateTimeUtc();

    QContactId idB(removalId(b));
    QVERIFY(m_cm->removeContact(idB));

    clf = QContactChangeLogFilter();
    clf.setEventType(QContactChangeLogFilter::EventRemoved);
    clf.setSince(startTime);     // should contain both a and b
    filtered = m_cm->contactIds(clf);
    QVERIFY(filtered.count() >= 2);
    QVERIFY(filtered.contains(idA));
    QVERIFY(filtered.contains(idB));

    // Check that syncTarget filtering is also applied
    cif.clear(); cif << localFilter << clf;
    filtered = m_cm->contactIds(cif);
    QVERIFY(filtered.count() >= 2);
    QVERIFY(filtered.contains(idA));
    QVERIFY(filtered.contains(idB));

    // And that aggregate contacts are not reported for EventRemoved filters
    // as aggregate contacts are deleted directly rather than marked with changeFlags.
    cif.clear(); cif << aggFilter << clf;
    filtered = m_cm->contactIds(cif);
    QCOMPARE(filtered.count(), 0);

    // Check that since values are applied
    clf = QContactChangeLogFilter();
    clf.setEventType(QContactChangeLogFilter::EventRemoved);
    clf.setSince(postDeleteTime);     // should contain only b
    filtered = m_cm->contactIds(clf);
    QVERIFY(filtered.count() >= 1);
    QVERIFY(filtered.contains(idB));

    cif.clear(); cif << localFilter << clf;
    filtered = m_cm->contactIds(cif);
    QVERIFY(filtered.count() >= 1);
    QVERIFY(filtered.contains(idB));

    // Check that since is not required
    clf = QContactChangeLogFilter();
    clf.setEventType(QContactChangeLogFilter::EventRemoved);
    filtered = m_cm->contactIds(clf);
    QVERIFY(filtered.count() >= 2);
    QVERIFY(filtered.contains(idA));
    QVERIFY(filtered.contains(idB));
}

void tst_Aggregation::deactivationSingle()
{
    QContactCollectionFilter allCollections;

    QContactCollection testAddressbook;
    testAddressbook.setMetaData(QContactCollection::KeyName, QStringLiteral("test"));
    testAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, "tst_aggregation");
    testAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID, 5);
    testAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH, "/addressbooks/test");
    QVERIFY(m_cm->saveCollection(&testAddressbook));

    // add a new contact (collectionId must be specified to deactivate)
    QContact syncAlice;
    syncAlice.setCollectionId(testAddressbook.id());

    QContactName an;
    an.setFirstName("Alice");
    an.setMiddleName("Through The");
    an.setLastName("Looking-Glass");
    syncAlice.saveDetail(&an);

    QVERIFY(m_cm->saveContact(&syncAlice));

    QContact aggregateAlice;

    QList<QContact> contacts = m_cm->contacts(allCollections);
    foreach (const QContact &curr, contacts) {
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice") &&
            currName.middleName() == QLatin1String("Through The") &&
            currName.lastName() == QLatin1String("Looking-Glass")) {
            if (curr.collectionId() == testAddressbook.id()) {
                syncAlice = curr;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateAlice = curr;
            }
        }
    }

    // Check that aggregation occurred
    QVERIFY(syncAlice.id() != QContactId());
    QVERIFY(aggregateAlice.id() != QContactId());
    QVERIFY(syncAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).count() == 1);
    QVERIFY(syncAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).count() == 1);
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(syncAlice.id()));

    // Verify the presence of the contact IDs
    QList<QContactId> contactIds = m_cm->contactIds(allCollections);
    QVERIFY(contactIds.contains(ContactId::apiId(syncAlice)));
    QVERIFY(contactIds.contains(ContactId::apiId(aggregateAlice)));

    contactIds = m_cm->contactIds();
    QVERIFY(contactIds.contains(ContactId::apiId(syncAlice)) == false);
    QVERIFY(contactIds.contains(ContactId::apiId(aggregateAlice)));

    QContactId syncAliceId = syncAlice.id();

    // Now deactivate the test contact
    QContactDeactivated deactivated;
    syncAlice.saveDetail(&deactivated);
    QVERIFY(m_cm->saveContact(&syncAlice));

    syncAlice = aggregateAlice = QContact();

    contacts = m_cm->contacts(allCollections);
    foreach (const QContact &curr, contacts) {
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice") &&
            currName.middleName() == QLatin1String("Through The") &&
            currName.lastName() == QLatin1String("Looking-Glass")) {
            if (curr.collectionId() == testAddressbook.id()) {
                syncAlice = curr;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateAlice = curr;
            }
        }
    }

    // The deactivated contact is not found (although relationships remain)
    // The deactivated contact is not found and the aggregate is removed
    QVERIFY(syncAlice.id() == QContactId());
    QVERIFY(aggregateAlice.id() == QContactId());

    // Verify that test alice still exists
    syncAlice = m_cm->contact(syncAliceId);
    QVERIFY(syncAlice.id() == syncAliceId);
    QVERIFY(syncAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).count() == 0);

    // Verify the presence/absence of the contact IDs
    contactIds = m_cm->contactIds(allCollections);
    QVERIFY(contactIds.contains(ContactId::apiId(syncAlice)) == false);

    contactIds = m_cm->contactIds(allCollections & QContactStatusFlags::matchFlag(QContactStatusFlags::IsDeactivated, QContactFilter::MatchContains));
    QVERIFY(contactIds.contains(syncAliceId));

    // Reactivate
    deactivated = syncAlice.detail<QContactDeactivated>();
    syncAlice.removeDetail(&deactivated, QContact::IgnoreAccessConstraints);
    QVERIFY(m_cm->saveContact(&syncAlice));

    syncAlice = aggregateAlice = QContact();

    contacts = m_cm->contacts(allCollections);
    foreach (const QContact &curr, contacts) {
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice") &&
            currName.middleName() == QLatin1String("Through The") &&
            currName.lastName() == QLatin1String("Looking-Glass")) {
            if (curr.collectionId() == testAddressbook.id()) {
                syncAlice = curr;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateAlice = curr;
            }
        }
    }

    // Check that aggregation is restored
    QVERIFY(syncAlice.id() != QContactId());
    QVERIFY(aggregateAlice.id() != QContactId());
    QVERIFY(syncAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).count() == 1);
    QVERIFY(syncAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).count() == 1);
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(syncAlice.id()));

    // Check that the reactivated contact retains the same ID
    QVERIFY(syncAlice.id() == syncAliceId);

    // Verify the presence of all contact IDs when queried
    contactIds = m_cm->contactIds(allCollections);
    QVERIFY(contactIds.contains(ContactId::apiId(syncAlice)));
    QVERIFY(contactIds.contains(ContactId::apiId(aggregateAlice)));
}

void tst_Aggregation::deactivationMultiple()
{
    QContactCollectionFilter allCollections;

    QContactCollection testAddressbook;
    testAddressbook.setMetaData(QContactCollection::KeyName, QStringLiteral("test"));
    testAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, "tst_aggregation");
    testAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID, 5);
    testAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH, "/addressbooks/test");
    QVERIFY(m_cm->saveCollection(&testAddressbook));

    QContactCollection trialAddressbook;
    trialAddressbook.setMetaData(QContactCollection::KeyName, QStringLiteral("trial"));
    trialAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, "tst_aggregation");
    trialAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID, 6);
    trialAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH, "/addressbooks/trial");
    QVERIFY(m_cm->saveCollection(&trialAddressbook));

    // add a new contact (collection must be specified to deactivate)
    QContact syncAlice;
    syncAlice.setCollectionId(testAddressbook.id());

    QContactName an;
    an.setFirstName("Alice");
    an.setMiddleName("Through The");
    an.setLastName("Looking-Glass");
    syncAlice.saveDetail(&an);

    QContactPhoneNumber aph;
    aph.setNumber("34567");
    syncAlice.saveDetail(&aph);

    QVERIFY(m_cm->saveContact(&syncAlice));

    // now add the doppelganger from another sync source
    QContact otherAlice;
    otherAlice.setCollectionId(trialAddressbook.id());

    QContactName san;
    san.setFirstName(an.firstName());
    san.setMiddleName(an.middleName());
    san.setLastName(an.lastName());
    otherAlice.saveDetail(&san);

    QContactPhoneNumber saph;
    saph.setNumber("76543");
    otherAlice.saveDetail(&saph);

    QVERIFY(m_cm->saveContact(&otherAlice));

    QContact aggregateAlice;

    QList<QContact> contacts = m_cm->contacts(allCollections);
    foreach (const QContact &curr, contacts) {
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice") &&
            currName.middleName() == QLatin1String("Through The") &&
            currName.lastName() == QLatin1String("Looking-Glass")) {
            if (curr.collectionId() == testAddressbook.id()) {
                syncAlice = curr;
            } else if (curr.collectionId() == trialAddressbook.id()) {
                otherAlice = curr;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateAlice = curr;
            }
        }
    }

    // Check that aggregation occurred
    QVERIFY(syncAlice.id() != QContactId());
    QVERIFY(otherAlice.id() != QContactId());
    QVERIFY(aggregateAlice.id() != QContactId());
    QVERIFY(syncAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).count() == 1);
    QVERIFY(syncAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(otherAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).count() == 1);
    QVERIFY(otherAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).count() == 2);
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(syncAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(otherAlice.id()));

    QCOMPARE(syncAlice.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(otherAlice.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().count(), 2);

    // Verify the presence of the contact IDs
    QList<QContactId> contactIds = m_cm->contactIds(allCollections);
    QVERIFY(contactIds.contains(ContactId::apiId(syncAlice)));
    QVERIFY(contactIds.contains(ContactId::apiId(otherAlice)));
    QVERIFY(contactIds.contains(ContactId::apiId(aggregateAlice)));

    contactIds = m_cm->contactIds();
    QVERIFY(contactIds.contains(ContactId::apiId(syncAlice)) == false);
    QVERIFY(contactIds.contains(ContactId::apiId(otherAlice)) == false);
    QVERIFY(contactIds.contains(ContactId::apiId(aggregateAlice)));

    QContactId syncAliceId = syncAlice.id();

    // Now deactivate the test contact
    QContactDeactivated deactivated;
    syncAlice.saveDetail(&deactivated);
    QVERIFY(m_cm->saveContact(&syncAlice));

    syncAlice = otherAlice = aggregateAlice = QContact();

    contacts = m_cm->contacts(allCollections);
    foreach (const QContact &curr, contacts) {
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice") &&
            currName.middleName() == QLatin1String("Through The") &&
            currName.lastName() == QLatin1String("Looking-Glass")) {
            if (curr.collectionId() == testAddressbook.id()) {
                syncAlice = curr;
            } else if (curr.collectionId() == trialAddressbook.id()) {
                otherAlice = curr;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateAlice = curr;
            }
        }
    }

    // The deactivated contact is not found (although relationships remain)
    QVERIFY(syncAlice.id() == QContactId());
    QVERIFY(otherAlice.id() != QContactId());
    QVERIFY(aggregateAlice.id() != QContactId());
    QVERIFY(otherAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).count() == 1);
    QVERIFY(otherAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).count() == 2);
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(syncAliceId));
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(otherAlice.id()));

    // Check that the aggregate does not contain the deactivated detail
    QCOMPARE(otherAlice.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().count(), 1);

    // Verify that test alice still exists
    syncAlice = m_cm->contact(syncAliceId);
    QVERIFY(syncAlice.id() == syncAliceId);
    QVERIFY(syncAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).count() == 1);
    QVERIFY(syncAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateAlice.id()));

    // Verify the presence/absence of the contact IDs
    contactIds = m_cm->contactIds(allCollections);
    QVERIFY(contactIds.contains(ContactId::apiId(syncAlice)) == false);
    QVERIFY(contactIds.contains(ContactId::apiId(otherAlice)));
    QVERIFY(contactIds.contains(ContactId::apiId(aggregateAlice)));

    contactIds = m_cm->contactIds(allCollections & QContactStatusFlags::matchFlag(QContactStatusFlags::IsDeactivated, QContactFilter::MatchContains));
    QVERIFY(contactIds.contains(syncAliceId));
    QVERIFY(contactIds.contains(ContactId::apiId(otherAlice)) == false);
    QVERIFY(contactIds.contains(ContactId::apiId(aggregateAlice)) == false);

    // Reactivate
    deactivated = syncAlice.detail<QContactDeactivated>();
    syncAlice.removeDetail(&deactivated);
    QVERIFY(m_cm->saveContact(&syncAlice));

    syncAlice = otherAlice = aggregateAlice = QContact();

    contacts = m_cm->contacts(allCollections);
    foreach (const QContact &curr, contacts) {
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice") &&
            currName.middleName() == QLatin1String("Through The") &&
            currName.lastName() == QLatin1String("Looking-Glass")) {
            if (curr.collectionId() == testAddressbook.id()) {
                syncAlice = curr;
            } else if (curr.collectionId() == trialAddressbook.id()) {
                otherAlice = curr;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateAlice = curr;
            }
        }
    }

    // Check that aggregation remains intact
    QVERIFY(syncAlice.id() != QContactId());
    QVERIFY(otherAlice.id() != QContactId());
    QVERIFY(aggregateAlice.id() != QContactId());
    QVERIFY(syncAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).count() == 1);
    QVERIFY(syncAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(otherAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).count() == 1);
    QVERIFY(otherAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).count() == 2);
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(syncAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(otherAlice.id()));

    // Re-activated details are now aggregated
    QCOMPARE(syncAlice.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(otherAlice.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().count(), 2);

    // Check that the reactivated contact retains the same ID
    QVERIFY(syncAlice.id() == syncAliceId);

    // Verify the presence of all contact IDs when queried
    contactIds = m_cm->contactIds(allCollections);
    QVERIFY(contactIds.contains(ContactId::apiId(syncAlice)));
    QVERIFY(contactIds.contains(ContactId::apiId(otherAlice)));
    QVERIFY(contactIds.contains(ContactId::apiId(aggregateAlice)));
}

void tst_Aggregation::deletionSingle()
{
    QtContactsSqliteExtensions::ContactManagerEngine *cme = QtContactsSqliteExtensions::contactManagerEngine(*m_cm);
    QContactManager::Error err = QContactManager::NoError;

    QContactCollectionFilter allCollections;

    QContactCollection testAddressbook;
    testAddressbook.setMetaData(QContactCollection::KeyName, QStringLiteral("test"));
    testAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, "tst_aggregation");
    testAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID, 5);
    testAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH, "/addressbooks/test");
    QVERIFY(m_cm->saveCollection(&testAddressbook));

    // add a new contact
    QContact alice;

    QContactName an;
    an.setFirstName("Alice");
    an.setMiddleName("Through The");
    an.setLastName("Looking-Glass");
    alice.saveDetail(&an);

    QVERIFY(m_cm->saveContact(&alice));

    QContact aggregateAlice;

    QList<QContact> contacts = m_cm->contacts(allCollections);
    foreach (const QContact &curr, contacts) {
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice") &&
            currName.middleName() == QLatin1String("Through The") &&
            currName.lastName() == QLatin1String("Looking-Glass")) {
            if (curr.collectionId().localId() == localAddressbookId()) {
                alice = curr;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateAlice = curr;
            }
        }
    }

    // Check that aggregation occurred
    QVERIFY(alice.id() != QContactId());
    QVERIFY(aggregateAlice.id() != QContactId());
    QVERIFY(alice.relatedContacts(aggregatesRelationship, QContactRelationship::First).count() == 1);
    QVERIFY(alice.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).count() == 1);
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(alice.id()));

    // Verify the presence of the contact IDs
    QList<QContactId> contactIds = m_cm->contactIds(allCollections);
    QVERIFY(contactIds.contains(ContactId::apiId(alice)));
    QVERIFY(contactIds.contains(ContactId::apiId(aggregateAlice)));

    contactIds = m_cm->contactIds();
    QVERIFY(contactIds.contains(ContactId::apiId(alice)) == false);
    QVERIFY(contactIds.contains(ContactId::apiId(aggregateAlice)));

    QContactId aliceId = alice.id();

    // Now delete the contact
    QVERIFY(m_cm->removeContact(alice.id()));

    alice = aggregateAlice = QContact();
    contacts = m_cm->contacts(allCollections);
    foreach (const QContact &curr, contacts) {
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice") &&
            currName.middleName() == QLatin1String("Through The") &&
            currName.lastName() == QLatin1String("Looking-Glass")) {
            if (curr.collectionId().localId() == localAddressbookId()) {
                alice = curr;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateAlice = curr;
            }
        }
    }

    // The deleted contact is not found and the aggregate is removed
    QVERIFY(alice.id() == QContactId());
    QVERIFY(aggregateAlice.id() == QContactId());
    alice = m_cm->contact(aliceId);
    QCOMPARE(m_cm->error(), QContactManager::DoesNotExistError);
    QVERIFY(alice.id() == QContactId()); // not found.

    // Verify the presence/absence of the contact IDs with appropriate filter applied.
    contactIds = m_cm->contactIds(allCollections);
    QVERIFY(contactIds.contains(ContactId::apiId(alice)) == false);
    contactIds = m_cm->contactIds(allCollections & QContactStatusFlags::matchFlag(QContactStatusFlags::IsDeleted, QContactFilter::MatchContains));
    QVERIFY(contactIds.contains(aliceId));

    // attempt to modify alice.  should fail.
    QContactHobby ahobby;
    ahobby.setHobby("Snowboarding");
    alice.saveDetail(&ahobby);
    alice.setId(aliceId);
    QVERIFY(!m_cm->saveContact(&alice));
    QCOMPARE(m_cm->error(), QContactManager::DoesNotExistError);

    // Undelete alice.
    alice.clearDetails();
    QContactUndelete undelete;
    alice.saveDetail(&undelete, QContact::IgnoreAccessConstraints);
    alice.setId(aliceId);
    QVERIFY(m_cm->saveContact(&alice));

    alice = aggregateAlice = QContact();
    contacts = m_cm->contacts(allCollections);
    foreach (const QContact &curr, contacts) {
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice") &&
            currName.middleName() == QLatin1String("Through The") &&
            currName.lastName() == QLatin1String("Looking-Glass")) {
            if (curr.collectionId().localId() == localAddressbookId()) {
                alice = curr;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateAlice = curr;
            }
        }
    }

    // Check that aggregation is restored
    QVERIFY(alice.id() != QContactId());
    QVERIFY(aggregateAlice.id() != QContactId());
    QVERIFY(alice.relatedContacts(aggregatesRelationship, QContactRelationship::First).count() == 1);
    QVERIFY(alice.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).count() == 1);
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(alice.id()));

    // Check that the undeleted contact retains the same ID
    QVERIFY(alice.id() == aliceId);

    // Verify the presence of all contact IDs when queried
    contactIds = m_cm->contactIds(allCollections);
    QVERIFY(contactIds.contains(ContactId::apiId(alice)));
    QVERIFY(contactIds.contains(ContactId::apiId(aggregateAlice)));

    // Now both delete and purge the contact.
    QVERIFY(m_cm->removeContact(aliceId));
    QVERIFY(cme->clearChangeFlags(QList<QContactId>() << aliceId, &err));

    // Verify the absence of the contact IDs even with appropriate filter applied.
    contactIds = m_cm->contactIds(allCollections);
    QVERIFY(!contactIds.contains(aliceId));
    contactIds = m_cm->contactIds(allCollections & QContactStatusFlags::matchFlag(QContactStatusFlags::IsDeleted, QContactFilter::MatchContains));
    QVERIFY(!contactIds.contains(aliceId));

    // Verify that we cannot undelete the purged contact.
    alice.clearDetails();
    QContactUndelete undelete2;
    alice.saveDetail(&undelete2, QContact::IgnoreAccessConstraints);
    alice.setId(aliceId);
    QVERIFY(!m_cm->saveContact(&alice));
}

void tst_Aggregation::deletionMultiple()
{
    QContactCollectionFilter allCollections;

    QContactCollection testAddressbook;
    testAddressbook.setMetaData(QContactCollection::KeyName, QStringLiteral("test"));
    testAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, "tst_aggregation");
    testAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID, 5);
    testAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH, "/addressbooks/test");
    QVERIFY(m_cm->saveCollection(&testAddressbook));

    QContactCollection trialAddressbook;
    trialAddressbook.setMetaData(QContactCollection::KeyName, QStringLiteral("trial"));
    trialAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, "tst_aggregation");
    trialAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID, 6);
    trialAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH, "/addressbooks/trial");
    QVERIFY(m_cm->saveCollection(&trialAddressbook));

    // add a new contact
    QContact testAlice;
    testAlice.setCollectionId(testAddressbook.id());

    QContactName an;
    an.setFirstName("Alice");
    an.setMiddleName("Through The");
    an.setLastName("Looking-Glass");
    testAlice.saveDetail(&an);

    QContactPhoneNumber aph;
    aph.setNumber("34567");
    testAlice.saveDetail(&aph);

    QVERIFY(m_cm->saveContact(&testAlice));

    // now add the doppelganger from another sync source
    QContact trialAlice;
    trialAlice.setCollectionId(trialAddressbook.id());

    QContactName san;
    san.setFirstName(an.firstName());
    san.setMiddleName(an.middleName());
    san.setLastName(an.lastName());
    trialAlice.saveDetail(&san);

    QContactPhoneNumber saph;
    saph.setNumber("76543");
    trialAlice.saveDetail(&saph);

    QVERIFY(m_cm->saveContact(&trialAlice));

    QContact aggregateAlice;

    QList<QContact> contacts = m_cm->contacts(allCollections);
    foreach (const QContact &curr, contacts) {
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice") &&
            currName.middleName() == QLatin1String("Through The") &&
            currName.lastName() == QLatin1String("Looking-Glass")) {
            if (curr.collectionId() == testAddressbook.id()) {
                testAlice = curr;
            } else if (curr.collectionId() == trialAddressbook.id()) {
                trialAlice = curr;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateAlice = curr;
            }
        }
    }

    // Check that aggregation occurred
    QVERIFY(testAlice.id() != QContactId());
    QVERIFY(trialAlice.id() != QContactId());
    QVERIFY(aggregateAlice.id() != QContactId());
    QVERIFY(testAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).count() == 1);
    QVERIFY(testAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(trialAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).count() == 1);
    QVERIFY(trialAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).count() == 2);
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(testAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(trialAlice.id()));

    QCOMPARE(testAlice.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(trialAlice.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().count(), 2);

    // Verify the presence of the contact IDs
    QList<QContactId> contactIds = m_cm->contactIds(allCollections);
    QVERIFY(contactIds.contains(ContactId::apiId(testAlice)));
    QVERIFY(contactIds.contains(ContactId::apiId(trialAlice)));
    QVERIFY(contactIds.contains(ContactId::apiId(aggregateAlice)));

    contactIds = m_cm->contactIds();
    QVERIFY(contactIds.contains(ContactId::apiId(testAlice)) == false);
    QVERIFY(contactIds.contains(ContactId::apiId(trialAlice)) == false);
    QVERIFY(contactIds.contains(ContactId::apiId(aggregateAlice)));

    QContactId testAliceId = testAlice.id();

    // Now delete the test contact
    QVERIFY(m_cm->removeContact(testAlice.id()));

    testAlice = trialAlice = aggregateAlice = QContact();
    contacts = m_cm->contacts(allCollections);
    foreach (const QContact &curr, contacts) {
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice") &&
            currName.middleName() == QLatin1String("Through The") &&
            currName.lastName() == QLatin1String("Looking-Glass")) {
            if (curr.collectionId() == testAddressbook.id()) {
                testAlice = curr;
            } else if (curr.collectionId() == trialAddressbook.id()) {
                trialAlice = curr;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateAlice = curr;
            }
        }
    }

    // The deleted contact is not found
    QVERIFY(testAlice.id() == QContactId());
    QVERIFY(trialAlice.id() != QContactId());
    QVERIFY(aggregateAlice.id() != QContactId());
    QVERIFY(trialAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).count() == 1);
    QVERIFY(trialAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).count() == 1);
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(trialAlice.id()));
    QVERIFY(!aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(testAliceId));

    // Relationships to the deleted contact are not found
    QList<QContactRelationship> aggRels = m_cm->relationships(aggregateAlice.id());
    QList<QContactRelationship> testRels = m_cm->relationships(testAliceId);
    QCOMPARE(aggRels.size(), 1);
    QCOMPARE(testRels.size(), 0);

    // Check that the aggregate does not contain the deleted details
    QCOMPARE(trialAlice.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(aggregateAlice.detail<QContactPhoneNumber>().number(), trialAlice.detail<QContactPhoneNumber>().number());

    // Verify that test alice does not exist
    testAlice = m_cm->contact(testAliceId);
    QVERIFY(testAlice.id() == QContactId());

    // Verify the presence/absence of the contact IDs
    contactIds = m_cm->contactIds(allCollections);
    QVERIFY(contactIds.contains(testAliceId) == false);
    QVERIFY(contactIds.contains(ContactId::apiId(trialAlice)));
    QVERIFY(contactIds.contains(ContactId::apiId(aggregateAlice)));

    contactIds = m_cm->contactIds(allCollections & QContactStatusFlags::matchFlag(QContactStatusFlags::IsDeleted, QContactFilter::MatchContains));
    QVERIFY(contactIds.contains(testAliceId));
    QVERIFY(contactIds.contains(ContactId::apiId(trialAlice)) == false);
    QVERIFY(contactIds.contains(ContactId::apiId(aggregateAlice)) == false);

    // Undelete
    testAlice.clearDetails();
    QContactUndelete undelete;
    testAlice.saveDetail(&undelete);
    testAlice.setId(testAliceId);
    testAlice.setCollectionId(testAddressbook.id());
    QVERIFY(m_cm->saveContact(&testAlice));

    testAlice = trialAlice = aggregateAlice = QContact();
    contacts = m_cm->contacts(allCollections);
    foreach (const QContact &curr, contacts) {
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice") &&
            currName.middleName() == QLatin1String("Through The") &&
            currName.lastName() == QLatin1String("Looking-Glass")) {
            if (curr.collectionId() == testAddressbook.id()) {
                testAlice = curr;
            } else if (curr.collectionId() == trialAddressbook.id()) {
                trialAlice = curr;
            } else {
                QCOMPARE(curr.collectionId().localId(), aggregateAddressbookId());
                aggregateAlice = curr;
            }
        }
    }

    // Check that aggregation remains intact
    QVERIFY(testAlice.id() != QContactId());
    QVERIFY(trialAlice.id() != QContactId());
    QVERIFY(aggregateAlice.id() != QContactId());
    QVERIFY(testAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).count() == 1);
    QVERIFY(testAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(trialAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).count() == 1);
    QVERIFY(trialAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First).contains(aggregateAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).count() == 2);
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(testAlice.id()));
    QVERIFY(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second).contains(trialAlice.id()));

    // Re-activated details are now aggregated
    QCOMPARE(testAlice.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(trialAlice.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().count(), 2);

    // Check that the reactivated contact retains the same ID
    QVERIFY(testAlice.id() == testAliceId);

    // Verify the presence of all contact IDs when queried
    contactIds = m_cm->contactIds(allCollections);
    QVERIFY(contactIds.contains(ContactId::apiId(testAlice)));
    QVERIFY(contactIds.contains(ContactId::apiId(trialAlice)));
    QVERIFY(contactIds.contains(ContactId::apiId(aggregateAlice)));
}

void tst_Aggregation::deletionCollections()
{
    QContactCollectionFilter allCollections;

    const int count = m_cm->contactIds(allCollections).size();
    const int deletedCount = m_cm->contactIds(allCollections & QContactStatusFlags::matchFlag(QContactStatusFlags::IsDeleted, QContactFilter::MatchContains)).size();

    // create two test collections.  one is aggregable, one is not.
    QContactCollection testAddressbook;
    testAddressbook.setMetaData(QContactCollection::KeyName, QStringLiteral("test"));
    testAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, "tst_aggregation");
    testAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID, 5);
    testAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH, "/addressbooks/test");
    testAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_AGGREGABLE, false);
    testAddressbook.setExtendedMetaData("customKey1", "customValue1");
    QVERIFY(m_cm->saveCollection(&testAddressbook));
    QContactCollectionId testAddressbookId = testAddressbook.id();

    QContactCollection trialAddressbook;
    trialAddressbook.setMetaData(QContactCollection::KeyName, QStringLiteral("trial"));
    trialAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, "tst_aggregation");
    trialAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID, 6);
    trialAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH, "/addressbooks/trial");
    trialAddressbook.setExtendedMetaData("customKey2", "customValue2");
    trialAddressbook.setExtendedMetaData("customKey1", "customValue3");
    QVERIFY(m_cm->saveCollection(&trialAddressbook));
    QContactCollectionId trialAddressbookId = trialAddressbook.id();

    // add three contacts to each addressbook.
    QContact a, b, c, x, y, z;
    a.setCollectionId(testAddressbook.id());
    b.setCollectionId(testAddressbook.id());
    c.setCollectionId(testAddressbook.id());
    x.setCollectionId(trialAddressbook.id());
    y.setCollectionId(trialAddressbook.id());
    z.setCollectionId(trialAddressbook.id());
    QContactName an, bn, cn, xn, yn, zn;
    an.setFirstName("A"); an.setLastName("A");
    bn.setFirstName("B"); bn.setLastName("B");
    cn.setFirstName("C"); cn.setLastName("C");
    xn.setFirstName("X"); xn.setLastName("X");
    yn.setFirstName("Y"); yn.setLastName("Y");
    zn.setFirstName("Z"); zn.setLastName("Z");

    QVERIFY(m_cm->saveContact(&a));
    QVERIFY(m_cm->saveContact(&b));
    QVERIFY(m_cm->saveContact(&c));
    QVERIFY(m_cm->saveContact(&x));
    QVERIFY(m_cm->saveContact(&y));
    QVERIFY(m_cm->saveContact(&z));

    // ensure that we have the number of contacts we expect, including xyz aggregates.
    QList<QContactId> ids = m_cm->contactIds(allCollections);
    QList<QContactId> deletedIds = m_cm->contactIds(allCollections & QContactStatusFlags::matchFlag(QContactStatusFlags::IsDeleted, QContactFilter::MatchContains));
    QCOMPARE(ids.size(), count + 9); // a,b,c,x,xa,y,ya,z,za
    QVERIFY(ids.contains(a.id()));
    QVERIFY(ids.contains(b.id()));
    QVERIFY(ids.contains(c.id()));
    QVERIFY(ids.contains(x.id()));
    QVERIFY(ids.contains(y.id()));
    QVERIFY(ids.contains(z.id()));
    QCOMPARE(deletedIds.size(), deletedCount + 0);

    // now mark b, x and z for deletion.
    QVERIFY(m_cm->removeContact(b.id()));
    QVERIFY(m_cm->removeContact(x.id()));
    QVERIFY(m_cm->removeContact(z.id()));

    // now modify a and y to set some modification change flags.
    QContactPhoneNumber ap, yp;
    ap.setNumber("12345");
    yp.setNumber("54321");
    QVERIFY(a.saveDetail(&ap));
    QVERIFY(y.saveDetail(&yp));
    QVERIFY(m_cm->saveContact(&a));
    QVERIFY(m_cm->saveContact(&y));

    // ensure that we have the number of contacts we expect, including y aggregate.
    // note that aggregates for deleted contacts don't exist, so xa,za won't be returned in deletedIds.
    ids = m_cm->contactIds(allCollections);
    deletedIds = m_cm->contactIds(allCollections & QContactStatusFlags::matchFlag(QContactStatusFlags::IsDeleted, QContactFilter::MatchContains));
    QCOMPARE(ids.size(), count + 4); // a,c,y,ya
    QVERIFY(ids.contains(a.id()));
    QVERIFY(!ids.contains(b.id()));
    QVERIFY(ids.contains(c.id()));
    QVERIFY(!ids.contains(x.id()));
    QVERIFY(ids.contains(y.id()));
    QVERIFY(!ids.contains(z.id()));
    QCOMPARE(deletedIds.size(), deletedCount + 3); // b, x, z
    QVERIFY(!deletedIds.contains(a.id()));
    QVERIFY(deletedIds.contains(b.id()));
    QVERIFY(!deletedIds.contains(c.id()));
    QVERIFY(deletedIds.contains(x.id()));
    QVERIFY(!deletedIds.contains(y.id()));
    QVERIFY(deletedIds.contains(z.id()));

    // now clear the change flags for trialAddressbook.  This should result in x+z being purged.
    QtContactsSqliteExtensions::ContactManagerEngine *cme = QtContactsSqliteExtensions::contactManagerEngine(*m_cm);
    QContactManager::Error err = QContactManager::NoError;
    QVERIFY(cme->clearChangeFlags(trialAddressbook.id(), &err));

    // should still be able to access the trialAddressbook itself.
    QContactCollection reloadedTrialAddressbook = m_cm->collection(trialAddressbookId);
    QCOMPARE(m_cm->error(), QContactManager::NoError);
    QCOMPARE(reloadedTrialAddressbook.metaData(QContactCollection::KeyName).toString(), QStringLiteral("trial"));
    QCOMPARE(reloadedTrialAddressbook.extendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME).toString(), QStringLiteral("tst_aggregation"));
    QCOMPARE(reloadedTrialAddressbook.extendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID).toInt(), 6);
    QCOMPARE(reloadedTrialAddressbook.extendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH).toString(), QStringLiteral("/addressbooks/trial"));
    QCOMPARE(reloadedTrialAddressbook.extendedMetaData("customKey2").toString(), QStringLiteral("customValue2"));
    QCOMPARE(reloadedTrialAddressbook.extendedMetaData("customKey1").toString(), QStringLiteral("customValue3"));

    // ensure x and z have been purged, leaving just a,b,c,y,ya accessible
    ids = m_cm->contactIds(allCollections);
    deletedIds = m_cm->contactIds(allCollections & QContactStatusFlags::matchFlag(QContactStatusFlags::IsDeleted, QContactFilter::MatchContains));
    QCOMPARE(ids.size(), count + 4); // a,c,y,ya
    QVERIFY(ids.contains(a.id()));
    QVERIFY(!ids.contains(b.id()));
    QVERIFY(ids.contains(c.id()));
    QVERIFY(!ids.contains(x.id()));
    QVERIFY(ids.contains(y.id()));
    QVERIFY(!ids.contains(z.id()));
    QCOMPARE(deletedIds.size(), deletedCount + 1); // b
    QVERIFY(!deletedIds.contains(a.id()));
    QVERIFY(deletedIds.contains(b.id()));
    QVERIFY(!deletedIds.contains(c.id()));
    QVERIFY(!deletedIds.contains(x.id()));
    QVERIFY(!deletedIds.contains(y.id()));
    QVERIFY(!deletedIds.contains(z.id()));

    // now delete testAddressbook.  this should also mark a and c as deleted.
    QVERIFY(m_cm->removeCollection(testAddressbookId));
    ids = m_cm->contactIds(allCollections);
    deletedIds = m_cm->contactIds(allCollections & QContactStatusFlags::matchFlag(QContactStatusFlags::IsDeleted, QContactFilter::MatchContains));
    QCOMPARE(ids.size(), count + 2); // y,ya
    QVERIFY(!ids.contains(a.id()));
    QVERIFY(!ids.contains(b.id()));
    QVERIFY(!ids.contains(c.id()));
    QVERIFY(!ids.contains(x.id()));
    QVERIFY(ids.contains(y.id()));
    QVERIFY(!ids.contains(z.id()));
    QCOMPARE(deletedIds.size(), deletedCount + 3); // a,b,c
    QVERIFY(deletedIds.contains(a.id()));
    QVERIFY(deletedIds.contains(b.id()));
    QVERIFY(deletedIds.contains(c.id()));
    QVERIFY(!deletedIds.contains(x.id()));
    QVERIFY(!deletedIds.contains(y.id()));
    QVERIFY(!deletedIds.contains(z.id()));

    // we should not be able to access testAddressbook any more via the normal accessor.
    QContactCollection deletedTestAddressbook = m_cm->collection(testAddressbookId);
    QVERIFY(deletedTestAddressbook.id().isNull());
    QCOMPARE(m_cm->error(), QContactManager::DoesNotExistError);

    // now clearChangeFlags for testAddressbook.  this should purge that addressbook and a,b,c.
    QVERIFY(cme->clearChangeFlags(testAddressbookId, &err));
    ids = m_cm->contactIds(allCollections);
    deletedIds = m_cm->contactIds(allCollections & QContactStatusFlags::matchFlag(QContactStatusFlags::IsDeleted, QContactFilter::MatchContains));
    QCOMPARE(ids.size(), count + 2); // y,ya
    QVERIFY(!ids.contains(a.id()));
    QVERIFY(!ids.contains(b.id()));
    QVERIFY(!ids.contains(c.id()));
    QVERIFY(!ids.contains(x.id()));
    QVERIFY(ids.contains(y.id()));
    QVERIFY(!ids.contains(z.id()));
    QCOMPARE(deletedIds.size(), deletedCount + 0); // nothing
    QVERIFY(!deletedIds.contains(a.id()));
    QVERIFY(!deletedIds.contains(b.id()));
    QVERIFY(!deletedIds.contains(c.id()));
    QVERIFY(!deletedIds.contains(x.id()));
    QVERIFY(!deletedIds.contains(y.id()));
    QVERIFY(!deletedIds.contains(z.id()));

    // now delete trialAddressbook and purge it.
    QVERIFY(m_cm->removeCollection(trialAddressbookId));
    QVERIFY(cme->clearChangeFlags(trialAddressbookId, &err));
    ids = m_cm->contactIds(allCollections);
    deletedIds = m_cm->contactIds(allCollections & QContactStatusFlags::matchFlag(QContactStatusFlags::IsDeleted, QContactFilter::MatchContains));
    QCOMPARE(ids.size(), count + 0); // nothing
    QVERIFY(!ids.contains(a.id()));
    QVERIFY(!ids.contains(b.id()));
    QVERIFY(!ids.contains(c.id()));
    QVERIFY(!ids.contains(x.id()));
    QVERIFY(!ids.contains(y.id()));
    QVERIFY(!ids.contains(z.id()));
    QCOMPARE(deletedIds.size(), deletedCount + 0); // nothing
    QVERIFY(!deletedIds.contains(a.id()));
    QVERIFY(!deletedIds.contains(b.id()));
    QVERIFY(!deletedIds.contains(c.id()));
    QVERIFY(!deletedIds.contains(x.id()));
    QVERIFY(!deletedIds.contains(y.id()));
    QVERIFY(!deletedIds.contains(z.id()));
}

void tst_Aggregation::testOOB()
{
    QtContactsSqliteExtensions::ContactManagerEngine *cme = QtContactsSqliteExtensions::contactManagerEngine(*m_cm);

    const QString &scope(QString::fromLatin1("tst_Aggregation"));

    // Test simple OOB fetches and stores
    QVariant data;
    QVERIFY(cme->fetchOOB(scope, "nonexistentData", &data));
    QCOMPARE(data, QVariant());

    QVERIFY(cme->fetchOOB(scope, "data", &data));
    if (!data.isNull()) {
        QVERIFY(cme->removeOOB(scope, "data"));
    }

    QStringList keys;
    QVERIFY(cme->fetchOOBKeys(scope, &keys));
    QCOMPARE(keys, QStringList());

    QVERIFY(cme->storeOOB(scope, "data", QVariant::fromValue<double>(0.123456789)));

    data = QVariant();
    QVERIFY(cme->fetchOOB(scope, "data", &data));
    QCOMPARE(data.toDouble(), 0.123456789);

    keys.clear();
    QVERIFY(cme->fetchOOBKeys(scope, &keys));
    QCOMPARE(keys, QStringList() << "data");

    // Test overwrite
    QVERIFY(cme->storeOOB(scope, "data", QVariant::fromValue<QString>(QString::fromLatin1("Testing"))));

    data = QVariant();
    QVERIFY(cme->fetchOOB(scope, "data", &data));
    QCOMPARE(data.toString(), QString::fromLatin1("Testing"));

    // Test insertion of a long string
    QString lorem(QString::fromLatin1("Lorem ipsum dolor sit amet, consectetur adipiscing elit. Praesent consectetur elit ut semper porta. Aenean gravida risus ligula, sollicitudin pharetra magna varius quis. Donec mattis vehicula lobortis. In a pulvinar est. Donec consectetur sem eu metus blandit rhoncus. In volutpat lobortis porta. Aliquam ultrices nulla sit amet erat pharetra, in mollis elit condimentum. Sed auctor cursus viverra. Vestibulum at placerat ipsum."
    "Integer venenatis venenatis justo, vel tincidunt felis mattis sit amet. Aliquam tempus augue quis magna ultricies, id volutpat lorem ornare. Ut volutpat hendrerit tincidunt. Pellentesque habitant morbi tristique senectus et netus et malesuada fames ac turpis egestas. Integer sagittis risus non ipsum adipiscing, in semper urna imperdiet. Vivamus lobortis euismod justo, id vestibulum purus posuere cursus. Sed fermentum non sem ac tempor. Vivamus enim velit, euismod nec rutrum et, pellentesque vitae enim. Praesent dignissim consectetur tellus, vel sagittis justo pulvinar eu. Interdum et malesuada fames ac ante ipsum primis in faucibus. Suspendisse potenti. Curabitur condimentum dolor ac dictum condimentum. Nulla id libero hendrerit, facilisis velit at, porttitor erat."
    "Proin blandit a nisl quis laoreet. Pellentesque venenatis, sem non pulvinar blandit, leo est sodales tellus, sit amet semper orci neque non enim. Mauris tincidunt, quam sollicitudin fermentum dignissim, neque nunc consequat mauris, quis facilisis est massa ut purus. Sed et lacus lectus. Aenean laoreet lectus in suscipit pretium. Suspendisse at justo adipiscing, aliquam est ut, tristique tortor. Mauris tincidunt sem pharetra, volutpat erat non, cursus eros. In hac habitasse platea dictumst. Interdum et malesuada fames ac ante ipsum primis in faucibus. Fusce porttitor ultrices tortor, vel tincidunt libero feugiat a. Etiam elementum, magna sed imperdiet ullamcorper, nisl dolor vehicula magna, vel facilisis quam mi eget tortor. Donec pellentesque odio a eros iaculis varius. Sed purus nisi, accumsan quis urna eget, tincidunt venenatis sapien. Suspendisse quis diam dui. Donec eu sollicitudin nibh."
    "Sed pretium urna at odio dictum convallis. Donec vel pulvinar purus. Duis et augue ac turpis porttitor hendrerit quis quis urna. Sed ac lectus odio. Sed volutpat placerat hendrerit. Mauris ac mollis nisl. Praesent ornare egestas elit, vitae ultricies quam imperdiet a. Nam accumsan nulla ut blandit scelerisque. Maecenas condimentum erat sit amet turpis feugiat, ac dictum sapien mattis. In sagittis nulla mi, ut facilisis urna lacinia et. Integer sed erat id massa vestibulum fringilla. Ut nec placerat lorem, quis semper ipsum. Aenean facilisis, odio vitae condimentum interdum, tortor tellus scelerisque purus, at pellentesque leo erat eu orci. Duis in feugiat quam. Mauris lorem dolor, pharetra quis blandit non, cursus et odio."
    "Nunc eu tristique dui. Donec sit amet velit id ipsum rhoncus facilisis. Integer quis ultrices metus. Vestibulum ante ipsum primis in faucibus orci luctus et ultrices posuere cubilia Curae; Donec ac velit lacus. Fusce pharetra lacus metus, nec adipiscing erat consequat consectetur. Proin ipsum massa, placerat eget dignissim in, interdum ut lorem. Aliquam erat volutpat. Duis sagittis nec est in suscipit. Mauris non auctor nibh. Suspendisse ultrices laoreet neque, a lacinia ante lacinia a. Praesent tempus luctus mauris eu ullamcorper. Praesent ultricies ac metus eget imperdiet. Sed massa lectus, tincidunt in dui non, faucibus mattis ante. Curabitur neque quam, congue non dapibus quis, fringilla ut orci."));
    QVERIFY(cme->storeOOB(scope, "data", QVariant::fromValue<QString>(lorem)));
    QVERIFY(cme->fetchOOB(scope, "data", &data));
    QCOMPARE(data.toString(), lorem);

    // Test insertion of a large byte arrays
    QList<int> uniqueSequence;
    QList<int> repeatingSequence;
    QList<int> randomSequence;

    qsrand(0);
    for (int i = 0; i < 100; ++i) {
        for (int j = 0; j < 10; ++j) {
            uniqueSequence.append(i * 100 + j);
            repeatingSequence.append(j);
            randomSequence.append(qrand());
        }
    }

    QByteArray buffer;
    QList<int> extracted;

    {
        QDataStream os(&buffer, QIODevice::WriteOnly);
        os << uniqueSequence;
    }
    QVERIFY(cme->storeOOB(scope, "data", QVariant::fromValue<QByteArray>(buffer)));
    QVERIFY(cme->fetchOOB(scope, "data", &data));
    {
        buffer = data.value<QByteArray>();
        QDataStream is(buffer);
        is >> extracted;
    }
    QCOMPARE(extracted, uniqueSequence);

    {
        QDataStream os(&buffer, QIODevice::WriteOnly);
        os << repeatingSequence;
    }
    QVERIFY(cme->storeOOB(scope, "data", QVariant::fromValue<QByteArray>(buffer)));
    QVERIFY(cme->fetchOOB(scope, "data", &data));
    {
        buffer = data.value<QByteArray>();
        QDataStream is(buffer);
        is >> extracted;
    }
    QCOMPARE(extracted, repeatingSequence);

    {
        QDataStream os(&buffer, QIODevice::WriteOnly);
        os << randomSequence;
    }
    QVERIFY(cme->storeOOB(scope, "data", QVariant::fromValue<QByteArray>(buffer)));
    QVERIFY(cme->fetchOOB(scope, "data", &data));
    {
        buffer = data.value<QByteArray>();
        QDataStream is(buffer);
        is >> extracted;
    }
    QCOMPARE(extracted, randomSequence);

    keys.clear();
    QVERIFY(cme->fetchOOBKeys(scope, &keys));
    QCOMPARE(keys, QStringList() << "data");

    // Test remove
    QVERIFY(cme->removeOOB(scope, "data"));
    QVERIFY(cme->fetchOOB(scope, "data", &data));
    QCOMPARE(data, QVariant());

    keys.clear();
    QVERIFY(cme->fetchOOBKeys(scope, &keys));
    QCOMPARE(keys, QStringList());

    // Test multiple items
    QMap<QString, QVariant> values;
    values.insert("data", 100);
    values.insert("other", 200);
    QVERIFY(cme->storeOOB(scope, values));

    values.clear();
    QVERIFY(cme->fetchOOB(scope, (QStringList() << "data" << "other" << "nonexistent"), &values));
    QCOMPARE(values.count(), 2);
    QCOMPARE(values["data"].toInt(), 100);
    QCOMPARE(values["other"].toInt(), 200);

    keys.clear();
    QVERIFY(cme->fetchOOBKeys(scope, &keys));
    QCOMPARE(keys, QStringList() << "data" << "other");

    // Test empty lists
    values.clear();
    QVERIFY(cme->fetchOOB(scope, &values));
    QCOMPARE(values.count(), 2);
    QCOMPARE(values["data"].toInt(), 100);
    QCOMPARE(values["other"].toInt(), 200);

    keys.clear();
    QVERIFY(cme->fetchOOBKeys(scope, &keys));
    QCOMPARE(keys, QStringList() << "data" << "other");

    QVERIFY(cme->removeOOB(scope));

    values.clear();
    QVERIFY(cme->fetchOOB(scope, &values));
    QCOMPARE(values.count(), 0);

    keys.clear();
    QVERIFY(cme->fetchOOBKeys(scope, &keys));
    QCOMPARE(keys, QStringList());
}

QTEST_GUILESS_MAIN(tst_Aggregation)
#include "tst_aggregation.moc"
