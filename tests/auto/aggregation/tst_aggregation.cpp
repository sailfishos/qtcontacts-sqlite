/*
 * Copyright (C) 2013 Jolla Ltd. <chris.adams@jollamobile.com>
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
#include "testsyncadapter.h"

#include <QLocale>

static const QString aggregatesRelationship(relationshipString(QContactRelationship::Aggregates));

namespace {

static const char *addedAccumulationSlot = SLOT(addAccumulationSlot(QList<QContactId>));
static const char *changedAccumulationSlot = SLOT(chgAccumulationSlot(QList<QContactId>));
static const char *removedAccumulationSlot = SLOT(remAccumulationSlot(QList<QContactId>));

QString detailProvenance(const QContactDetail &detail)
{
    return detail.value<QString>(QContactDetail__FieldProvenance);
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
    void addAccumulationSlot(const QList<QContactId> &ids);
    void chgAccumulationSlot(const QList<QContactId> &ids);
    void remAccumulationSlot(const QList<QContactId> &ids);

private slots:
    void createSingleLocal();
    void createMultipleLocal();
    void createSingleLocalAndSingleSync();

    void updateSingleLocal();
    void updateSingleAggregate();
    void updateAggregateOfLocalAndSync();
    void updateAggregateOfLocalAndModifiableSync();

    void compositionPrefersLocal();

    void promotionToSingleLocal();
    void uniquenessConstraints();

    void removeSingleLocal();
    void removeSingleAggregate();

    void alterRelationships();

    void wasLocalUpdate();
    void wasLocalCreation();

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

    void fetchSyncContacts();
    void storeSyncContacts();

    void testOOB();
    void testSyncAdapter();

private:
    void waitForSignalPropagation();

    QContactManager *m_cm;
    QSet<QContactId> m_addAccumulatedIds;
    QSet<QContactId> m_chgAccumulatedIds;
    QSet<QContactId> m_remAccumulatedIds;
    QSet<QContactId> m_createdIds;
};

tst_Aggregation::tst_Aggregation()
    : m_cm(0)
{
    QMap<QString, QString> parameters;
    parameters.insert(QString::fromLatin1("autoTest"), QString::fromLatin1("true"));
    parameters.insert(QString::fromLatin1("mergePresenceChanges"), QString::fromLatin1("true"));
    m_cm = new QContactManager(QString::fromLatin1("org.nemomobile.contacts.sqlite"), parameters);

    QTest::qWait(250); // creating self contact etc will cause some signals to be emitted.  ignore them.
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
    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);
    m_cm->removeContacts(m_cm->contactIds(allSyncTargets));
    waitForSignalPropagation();
}

void tst_Aggregation::init()
{
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
    waitForSignalPropagation();
    if (!m_createdIds.isEmpty()) {
        m_cm->removeContacts(m_createdIds.toList());
        m_createdIds.clear();
    }
    waitForSignalPropagation();
}

void tst_Aggregation::waitForSignalPropagation()
{
    // Signals are routed via DBUS, so we need to wait for them to arrive
    QTest::qWait(50);
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
    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allSyncTargets).size();

    // set up some signal spies
    QSignalSpy addSpy(m_cm, contactsAddedSignal);
    int addSpyCount = 0;

    // now add a new local contact (no synctarget specified == automatically local)
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
    QCOMPARE(m_cm->contactIds(allSyncTargets).size(), allCount + 2); // should have added local + aggregate
    allCount = m_cm->contactIds(allSyncTargets).size();

    QList<QContact> allContacts = m_cm->contacts(allSyncTargets);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    QContact localAlice;
    QContact aggregateAlice;
    bool foundLocalAlice = false;
    bool foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("1234567")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));

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
    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allSyncTargets).size();

    // set up some signal spies
    QSignalSpy addSpy(m_cm, contactsAddedSignal);
    int addSpyCount = 0;

    // now add two new local contacts (no synctarget specified == automatically local)
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
    QCOMPARE(m_cm->contactIds(allSyncTargets).size(), allCount + 4); // should have added local + aggregate for each
    allCount = m_cm->contactIds(allSyncTargets).size();

    QList<QContact> allContacts = m_cm->contacts(allSyncTargets);
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
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice2")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("234567")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        } else if (currName.firstName() == QLatin1String("Bob2")
                && currName.middleName() == QLatin1String("The")
                && currName.lastName() == QLatin1String("Destroyer")
                && currPhn.number() == QLatin1String("765432")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                localBob = curr;
                foundLocalBob = true;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateBob = curr;
                foundAggregateBob = true;
            }
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(foundLocalBob);
    QVERIFY(foundAggregateBob);
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));
    QVERIFY(!relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateBob.id()));
    QVERIFY(!relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localBob.id()));
    QVERIFY(relatedContactIds(localBob.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateBob.id()));
    QVERIFY(relatedContactIds(aggregateBob.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localBob.id()));
    QVERIFY(!relatedContactIds(localBob.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(!relatedContactIds(aggregateBob.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));

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

    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allSyncTargets).size();

    // set up some signal spies
    QSignalSpy addSpy(m_cm, contactsAddedSignal);
    QSignalSpy chgSpy(m_cm, contactsChangedSignal);
    int addSpyCount = 0;
    int chgSpyCount = 0;

    // now add a new local contact (no synctarget specified == automatically local)
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
    QCOMPARE(m_cm->contactIds(allSyncTargets).size(), allCount + 2); // should have added local + aggregate
    allCount = m_cm->contactIds(allSyncTargets).size();

    QList<QContact> allContacts = m_cm->contacts(allSyncTargets);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    QContact localAlice;
    QContact aggregateAlice;
    bool foundLocalAlice = false;
    bool foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactEmailAddress currEm = curr.detail<QContactEmailAddress>();
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice3")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("34567")
                && currEm.emailAddress() == QLatin1String("alice@test.com")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));

    // now add the doppleganger from another sync source
    QContact syncAlice;
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
    QCOMPARE(m_cm->contactIds(allSyncTargets).size(), allCount + 1); // should have added test but not an aggregate
    allCount = m_cm->contactIds(allSyncTargets).size();

    allContacts = m_cm->contacts(allSyncTargets);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    QContact testAlice;
    bool foundTestAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactEmailAddress currEm = curr.detail<QContactEmailAddress>();
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice3")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("34567")
                && currEm.emailAddress() == QLatin1String("alice@test.com")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                localAlice = curr;
                foundLocalAlice = true;
            } else if (currSt.syncTarget() == QLatin1String("test")) {
                testAlice = curr;
                foundTestAlice = true;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundTestAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(testAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(testAlice.id()));

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

void tst_Aggregation::updateSingleLocal()
{
    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allSyncTargets).size();

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
    QCOMPARE(m_cm->contactIds(allSyncTargets).size(), allCount + 2); // should have added local + aggregate
    allCount = m_cm->contactIds(allSyncTargets).size();

    QList<QContact> allContacts = m_cm->contacts(allSyncTargets);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    QContact localAlice;
    QContact aggregateAlice;
    bool foundLocalAlice = false;
    bool foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        QContactHobby currHobby = curr.detail<QContactHobby>();
        if (currName.firstName() == QLatin1String("Alice")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("4567")
                && currHobby.hobby() == QLatin1String("tennis")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));

    // now update alice.  The aggregate should get updated also.
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
    QCOMPARE(aggregateAlice.detail<QContactEmailAddress>().value<QString>(QContactEmailAddress::FieldEmailAddress), QString::fromLatin1("alice4@test4.com"));
    QCOMPARE(localAlice.detail<QContactPhoneNumber>().value<QString>(QContactPhoneNumber::FieldNumber), QString::fromLatin1("4444"));
    QCOMPARE(aggregateAlice.detail<QContactPhoneNumber>().value<QString>(QContactPhoneNumber::FieldNumber), QString::fromLatin1("4444"));
}

void tst_Aggregation::updateSingleAggregate()
{
    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allSyncTargets).size();

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
    QCOMPARE(m_cm->contactIds(allSyncTargets).size(), allCount + 2); // should have added local + aggregate
    allCount = m_cm->contactIds(allSyncTargets).size();

    QList<QContact> allContacts = m_cm->contacts(allSyncTargets);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    QContact localAlice;
    QContact aggregateAlice;
    bool foundLocalAlice = false;
    bool foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        QContactHobby currHobby = curr.detail<QContactHobby>();
        if (currName.firstName() == QLatin1String("Alice")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("567")
                && currHobby.hobby() == QLatin1String("tennis")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));

    // now update aggregate alice.  We expect the changes to "down promoted" to the local contact!
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
    QVERIFY(m_cm->saveContact(&aggregateAlice));
    QTRY_VERIFY(chgSpy.count() > chgSpyCount);
    QTRY_VERIFY(m_chgAccumulatedIds.contains(ContactId::apiId(localAlice)));
    QTRY_VERIFY(m_chgAccumulatedIds.contains(ContactId::apiId(aggregateAlice)));

    // reload them, and compare.
    localAlice = m_cm->contact(retrievalId(localAlice));
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));
    QCOMPARE(localAlice.details<QContactEmailAddress>().size(), 1);
    QCOMPARE(localAlice.details<QContactPhoneNumber>().size(), 1);
    QCOMPARE(localAlice.details<QContactHobby>().size(), 0);
    QCOMPARE(localAlice.details<QContactNickname>().size(), 1);
    QCOMPARE(aggregateAlice.details<QContactEmailAddress>().size(), 1);
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().size(), 1);
    QCOMPARE(aggregateAlice.details<QContactHobby>().size(), 0);
    QCOMPARE(aggregateAlice.details<QContactNickname>().size(), 1);
    QCOMPARE(localAlice.detail<QContactEmailAddress>().value<QString>(QContactEmailAddress::FieldEmailAddress), QLatin1String("alice5@test.com"));
    QVERIFY(!detailProvenance(localAlice.detail<QContactEmailAddress>()).isEmpty());
    QCOMPARE(aggregateAlice.detail<QContactEmailAddress>().value<QString>(QContactEmailAddress::FieldEmailAddress), QLatin1String("alice5@test.com"));
    QCOMPARE(detailProvenance(aggregateAlice.detail<QContactEmailAddress>()), detailProvenance(localAlice.detail<QContactEmailAddress>()));
    QCOMPARE(localAlice.detail<QContactPhoneNumber>().value<QString>(QContactPhoneNumber::FieldNumber), QLatin1String("555"));
    QVERIFY(!detailProvenance(localAlice.detail<QContactPhoneNumber>()).isEmpty());
    QCOMPARE(aggregateAlice.detail<QContactPhoneNumber>().value<QString>(QContactPhoneNumber::FieldNumber), QLatin1String("555"));
    QCOMPARE(detailProvenance(aggregateAlice.detail<QContactPhoneNumber>()), detailProvenance(localAlice.detail<QContactPhoneNumber>()));
    QVERIFY(localAlice.detail<QContactHobby>().value<QString>(QContactHobby::FieldHobby).isEmpty());
    QVERIFY(aggregateAlice.detail<QContactHobby>().value<QString>(QContactHobby::FieldHobby).isEmpty());
    QCOMPARE(localAlice.detail<QContactNickname>().value<QString>(QContactNickname::FieldNickname), QLatin1String("Ally"));
    QVERIFY(!detailProvenance(localAlice.detail<QContactNickname>()).isEmpty());
    QCOMPARE(aggregateAlice.detail<QContactNickname>().value<QString>(QContactNickname::FieldNickname), QLatin1String("Ally"));
    QCOMPARE(detailProvenance(aggregateAlice.detail<QContactNickname>()), detailProvenance(localAlice.detail<QContactNickname>()));
    QCOMPARE(detailProvenanceContact(localAlice.detail<QContactNickname>()), detailProvenanceContact(localAlice.detail<QContactEmailAddress>()));
}

void tst_Aggregation::updateAggregateOfLocalAndSync()
{
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
    setFilterContact(aggf, alice);
    aggf.setRelatedContactRole(QContactRelationship::Second);
    setFilterType(aggf, QContactRelationship::Aggregates);
    QList<QContact> allAggregatesOfAlice = m_cm->contacts(aggf);
    QCOMPARE(allAggregatesOfAlice.size(), 1);
    QContact aggregateAlice = allAggregatesOfAlice.at(0);

    // now ensure that updates / modifies / removals work as expected
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().size(), 1); // comes from the local contact
    QContactPhoneNumber maph = aggregateAlice.detail<QContactPhoneNumber>();
    maph.setNumber("11115");
    QVERIFY(aggregateAlice.saveDetail(&maph)); // this should work, and modify the local contact's version.
    QCOMPARE(aggregateAlice.details<QContactEmailAddress>().size(), 1); // there are two, but since the values were identical, should only have one!
    QContactEmailAddress mem = aggregateAlice.detail<QContactEmailAddress>();
    mem.setEmailAddress("aliceP2@test.com");
    QVERIFY(aggregateAlice.saveDetail(&mem)); // this has strange semantics.  It should modify the local contact's version
                                     // but the regenerated aggregate will have BOTH email addresses - since we cannot remove
                                     // or modify the email address from the sync-source contact.
    QCOMPARE(aggregateAlice.details<QContactHobby>().size(), 1); // comes from the sync contact

    QContactHobby rah = aggregateAlice.detail<QContactHobby>();
    QVERIFY(rah.accessConstraints() & QContactDetail::Irremovable);
    QVERIFY(rah.accessConstraints() & QContactDetail::ReadOnly);
    QVERIFY(!aggregateAlice.removeDetail(&rah)); // this should be irremovable, due to constraint on synced details

    /*BUG IN MOBILITY - contact.saveDetail() doesn't check read only constraints :-/
    QContactNote man = aggregateAlice.detail<QContactNote>();
    QVERIFY(man.accessConstraints() & QContactDetail::Irremovable);
    QVERIFY(man.accessConstraints() & QContactDetail::ReadOnly);
    man.setNote("modified note");
    QVERIFY(!aggregateAlice.saveDetail(&man)); // this should be read only, due to constraint on synced details
    */

    QVERIFY(m_cm->saveContact(&aggregateAlice));

    // regenerate and ensure we get what we expect.
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));
    QCOMPARE(aggregateAlice.details<QContactNickname>().size(), 1);     // original, comes from the local contact
    QVERIFY(!detailProvenance(aggregateAlice.detail<QContactNickname>()).isEmpty());
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().size(), 1);  // modified, comes from the local contact
    QCOMPARE(detailProvenanceContact(aggregateAlice.detail<QContactPhoneNumber>()), detailProvenanceContact(aggregateAlice.detail<QContactNickname>()));
    QCOMPARE(aggregateAlice.detail<QContactPhoneNumber>().value<QString>(QContactPhoneNumber::FieldNumber), QString::fromLatin1("11115"));
    QCOMPARE(aggregateAlice.details<QContactHobby>().size(), 1);        // couldn't remove, comes from the sync contact
    QVERIFY(!detailProvenance(aggregateAlice.detail<QContactHobby>()).isEmpty());
    QCOMPARE(aggregateAlice.detail<QContactHobby>().value<QString>(QContactHobby::FieldHobby), QString::fromLatin1("tennis"));
    QCOMPARE(aggregateAlice.details<QContactNote>().size(), 1);         // couldn't modify, comes from the sync contact
    QCOMPARE(detailProvenanceContact(aggregateAlice.detail<QContactNote>()), detailProvenanceContact(aggregateAlice.detail<QContactHobby>()));
    QVERIFY(detailProvenanceContact(aggregateAlice.detail<QContactNote>()) != detailProvenanceContact(aggregateAlice.detail<QContactNickname>()));
    QCOMPARE(aggregateAlice.detail<QContactNote>().value<QString>(QContactNote::FieldNote), QString::fromLatin1("noteworthy note"));

    QList<QContactEmailAddress> aaems = aggregateAlice.details<QContactEmailAddress>();
    QCOMPARE(aaems.size(), 2); // modified local, now have two different values
    if (aaems.at(0).emailAddress() == QLatin1String("aliceP@test.com")) { // order is undefined.
        QCOMPARE(detailProvenanceContact(aaems.at(0)), detailProvenanceContact(aggregateAlice.detail<QContactHobby>()));
        QCOMPARE(aaems.at(1).emailAddress(), QLatin1String("aliceP2@test.com"));
        QCOMPARE(detailProvenanceContact(aaems.at(1)), detailProvenanceContact(aggregateAlice.detail<QContactNickname>()));
    } else {
        QCOMPARE(aaems.at(0).emailAddress(), QLatin1String("aliceP2@test.com"));
        QCOMPARE(detailProvenanceContact(aaems.at(0)), detailProvenanceContact(aggregateAlice.detail<QContactNickname>()));
        QCOMPARE(aaems.at(1).emailAddress(), QLatin1String("aliceP@test.com"));
        QCOMPARE(detailProvenanceContact(aaems.at(1)), detailProvenanceContact(aggregateAlice.detail<QContactHobby>()));
    }
}

void tst_Aggregation::updateAggregateOfLocalAndModifiableSync()
{
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
        setFilterContact(filter, alice);
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
    const QString testContact(detailProvenanceContact(aggregateAlice.detail<QContactRingtone>()));

    QCOMPARE(aggregateAlice.details<QContactEmailAddress>().size(), 2);
    QVERIFY(!detailProvenance(aggregateAlice.details<QContactEmailAddress>().at(0)).isEmpty());
    QVERIFY(detailProvenanceContact(aggregateAlice.details<QContactEmailAddress>().at(0)) != localContact);
    QVERIFY(!detailProvenance(aggregateAlice.details<QContactEmailAddress>().at(1)).isEmpty());
    QVERIFY(detailProvenanceContact(aggregateAlice.details<QContactEmailAddress>().at(1)) != localContact);
    QVERIFY(detailProvenanceContact(aggregateAlice.details<QContactEmailAddress>().at(0)) != detailProvenanceContact(aggregateAlice.details<QContactEmailAddress>().at(1)));

    QCOMPARE(aggregateAlice.details<QContactNote>().size(), 1);
    QCOMPARE(detailProvenanceContact(aggregateAlice.detail<QContactNote>()), testContact);

    QCOMPARE(aggregateAlice.details<QContactHobby>().size(), 1);
    QCOMPARE(detailProvenanceContact(aggregateAlice.detail<QContactHobby>()), testContact);

    QCOMPARE(aggregateAlice.details<QContactTag>().size(), 1);
    QVERIFY(!detailProvenance(aggregateAlice.detail<QContactTag>()).isEmpty());
    QVERIFY(detailProvenanceContact(aggregateAlice.detail<QContactTag>()) != localContact);
    QVERIFY(detailProvenanceContact(aggregateAlice.detail<QContactTag>()) != testContact);

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

    // Aggregate details which are promoted from modifiable details are not readonly
    QVERIFY((aggregateAlice.detail<QContactName>().accessConstraints() & QContactDetail::ReadOnly) == 0);
    QVERIFY((aggregateAlice.detail<QContactNickname>().accessConstraints() & QContactDetail::ReadOnly) == 0);
    QVERIFY((aggregateAlice.detail<QContactPhoneNumber>().accessConstraints() & QContactDetail::ReadOnly) == 0);
    QVERIFY((aggregateAlice.details<QContactEmailAddress>().at(0).accessConstraints() & QContactDetail::ReadOnly) == 0);
    QVERIFY((aggregateAlice.details<QContactEmailAddress>().at(1).accessConstraints() & QContactDetail::ReadOnly) == 0);
    QVERIFY((aggregateAlice.detail<QContactHobby>().accessConstraints() & QContactDetail::ReadOnly) > 0);
    QVERIFY((aggregateAlice.detail<QContactNote>().accessConstraints() & QContactDetail::ReadOnly) == 0);
    QVERIFY((aggregateAlice.detail<QContactOrganization>().accessConstraints() & QContactDetail::ReadOnly) == 0);
    QVERIFY((aggregateAlice.detail<QContactRingtone>().accessConstraints() & QContactDetail::ReadOnly) > 0);
    QVERIFY((aggregateAlice.detail<QContactTag>().accessConstraints() & QContactDetail::ReadOnly) > 0);

    // now ensure that updates / modifies / removals work as expected
    {
        // locally-originated detail - should be modified
        QContactPhoneNumber phoneNumber = aggregateAlice.detail<QContactPhoneNumber>();
        phoneNumber.setNumber("22222");
        QVERIFY(aggregateAlice.saveDetail(&phoneNumber));

        // modifiable sync details - should be modified in each contact
        foreach (QContactEmailAddress emailAddress, aggregateAlice.details<QContactEmailAddress>()) {
            if (emailAddress.emailAddress() == QString::fromLatin1("aliceP@test.com")) {
                emailAddress.setEmailAddress("aliceP2@test.com");
                QVERIFY(aggregateAlice.saveDetail(&emailAddress));
            } else {
                emailAddress.setEmailAddress("alice2@example.org");
                QVERIFY(aggregateAlice.saveDetail(&emailAddress));
            }
        }

        // modifiable sync detail - should be removed
        QContactNote note = aggregateAlice.detail<QContactNote>();
        QVERIFY(aggregateAlice.removeDetail(&note));

        // modifiable sync detail - should be removed
        QContactOrganization organization = aggregateAlice.detail<QContactOrganization>();
        QVERIFY(aggregateAlice.removeDetail(&organization));

        // unmodifiable sync detail - modification should be pushed to local
        QContactHobby hobby = aggregateAlice.detail<QContactHobby>();
        hobby.setHobby("crochet");
        QVERIFY(aggregateAlice.saveDetail(&hobby));
    }

    QVERIFY(m_cm->saveContact(&aggregateAlice));
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));

    QCOMPARE(aggregateAlice.details<QContactNickname>().size(), 1);
    QVERIFY(!detailProvenance(aggregateAlice.detail<QContactNickname>()).isEmpty());

    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().size(), 1);
    QCOMPARE(detailProvenanceContact(aggregateAlice.detail<QContactPhoneNumber>()), localContact);
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().at(0).number(), QString::fromLatin1("22222"));

    QList<QContactEmailAddress> aaeas = aggregateAlice.details<QContactEmailAddress>();
    QCOMPARE(aaeas.size(), 2);
    if (aaeas.at(0).emailAddress() == QString::fromLatin1("aliceP2@test.com")) {
        QCOMPARE(detailProvenanceContact(aaeas.at(0)), testContact);
        QCOMPARE(detailProvenanceContact(aaeas.at(1)), trialContact);
        QCOMPARE(aaeas.at(1).emailAddress(), QString::fromLatin1("alice2@example.org"));
    } else {
        QCOMPARE(detailProvenanceContact(aaeas.at(0)), trialContact);
        QCOMPARE(aaeas.at(0).emailAddress(), QString::fromLatin1("alice2@example.org"));
        QCOMPARE(detailProvenanceContact(aaeas.at(1)), testContact);
        QCOMPARE(aaeas.at(1).emailAddress(), QString::fromLatin1("aliceP2@test.com"));
    }

    QCOMPARE(aggregateAlice.details<QContactNote>().size(), 0);

    QList<QContactHobby> aahs = aggregateAlice.details<QContactHobby>();
    QCOMPARE(aahs.size(), 2);
    if (aahs.at(0).hobby() == QString::fromLatin1("tennis")) {
        QCOMPARE(detailProvenanceContact(aahs.at(0)), testContact);
        QCOMPARE(aahs.at(1).hobby(), QString::fromLatin1("crochet"));
        QCOMPARE(detailProvenanceContact(aahs.at(1)), localContact);
    } else {
        QCOMPARE(aahs.at(0).hobby(), QString::fromLatin1("crochet"));
        QCOMPARE(detailProvenanceContact(aahs.at(0)), localContact);
        QCOMPARE(aahs.at(1).hobby(), QString::fromLatin1("tennis"));
        QCOMPARE(detailProvenanceContact(aahs.at(1)), testContact);
    }

    QCOMPARE(aggregateAlice.details<QContactOrganization>().size(), 0);

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

    // Aggregate details which are promoted from modifiable details are not readonly
    QVERIFY((aggregateAlice.detail<QContactName>().accessConstraints() & QContactDetail::ReadOnly) == 0);
    QVERIFY((aggregateAlice.detail<QContactPhoneNumber>().accessConstraints() & QContactDetail::ReadOnly) == 0);
    QVERIFY((aggregateAlice.details<QContactEmailAddress>().at(0).accessConstraints() & QContactDetail::ReadOnly) == 0);
    QVERIFY((aggregateAlice.details<QContactEmailAddress>().at(1).accessConstraints() & QContactDetail::ReadOnly) == 0);
    if ((aggregateAlice.details<QContactHobby>().at(0).accessConstraints() & QContactDetail::ReadOnly) > 0) {
        QVERIFY((aggregateAlice.details<QContactHobby>().at(1).accessConstraints() & QContactDetail::ReadOnly) == 0);
    } else {
        QVERIFY((aggregateAlice.details<QContactHobby>().at(1).accessConstraints() & QContactDetail::ReadOnly) > 0);
    }
    QVERIFY((aggregateAlice.detail<QContactRingtone>().accessConstraints() & QContactDetail::ReadOnly) > 0);
    QVERIFY((aggregateAlice.detail<QContactTag>().accessConstraints() & QContactDetail::ReadOnly) > 0);

    // Modified details should remain modifiable after modification
    {
        foreach (QContactEmailAddress emailAddress, aggregateAlice.details<QContactEmailAddress>()) {
            if (emailAddress.emailAddress() == QString::fromLatin1("aliceP2@test.com")) {
                emailAddress.setEmailAddress("aliceP3@test.com");
                QVERIFY(aggregateAlice.saveDetail(&emailAddress));
            } else {
                emailAddress.setEmailAddress("alice3@example.org");
                QVERIFY(aggregateAlice.saveDetail(&emailAddress));
            }
        }
    }

    QVERIFY(m_cm->saveContact(&aggregateAlice));
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));

    aaeas = aggregateAlice.details<QContactEmailAddress>();
    QCOMPARE(aaeas.size(), 2);
    if (aaeas.at(0).emailAddress() == QString::fromLatin1("aliceP3@test.com")) {
        QCOMPARE(detailProvenanceContact(aaeas.at(0)), testContact);
        QCOMPARE(detailProvenanceContact(aaeas.at(1)), trialContact);
        QCOMPARE(aaeas.at(1).emailAddress(), QString::fromLatin1("alice3@example.org"));
    } else {
        QCOMPARE(detailProvenanceContact(aaeas.at(0)), trialContact);
        QCOMPARE(aaeas.at(0).emailAddress(), QString::fromLatin1("alice3@example.org"));
        QCOMPARE(detailProvenanceContact(aaeas.at(1)), testContact);
        QCOMPARE(aaeas.at(1).emailAddress(), QString::fromLatin1("aliceP3@test.com"));
    }

    testAlice = m_cm->contact(retrievalId(testAlice));
    QCOMPARE(testAlice.details<QContactEmailAddress>().count(), 1);
    QCOMPARE(testAlice.details<QContactEmailAddress>().at(0).emailAddress(), QString::fromLatin1("aliceP3@test.com"));
    QCOMPARE(testAlice.detail<QContactEmailAddress>().value(QContactDetail__FieldModifiable).toBool(), true);

    trialAlice = m_cm->contact(retrievalId(trialAlice));
    QCOMPARE(trialAlice.details<QContactEmailAddress>().count(), 1);
    QCOMPARE(trialAlice.details<QContactEmailAddress>().at(0).emailAddress(), QString::fromLatin1("alice3@example.org"));
    QCOMPARE(trialAlice.detail<QContactEmailAddress>().value(QContactDetail__FieldModifiable).toBool(), true);
}

void tst_Aggregation::compositionPrefersLocal()
{
    // Composed details should prefer the values of the local, where present
    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);

    // These contacts should all be aggregated together
    QContact stContact1, stContact2, stContact3, localContact;

    QContactName n1;
    n1.setPrefix(QLatin1String("Supt."));
    n1.setFirstName(QLatin1String("Link"));
    n1.setMiddleName(QLatin1String("Alice"));
    n1.setLastName(QLatin1String("CompositionTester"));
    stContact1.saveDetail(&n1);

    QContactSyncTarget sast;
    sast.setSyncTarget(QLatin1String("test1"));
    stContact1.saveDetail(&sast);

    QVERIFY(m_cm->saveContact(&stContact1));

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
    stContact2.saveDetail(&n3);

    sast.setSyncTarget(QLatin1String("test2"));
    stContact2.saveDetail(&sast);

    QVERIFY(m_cm->saveContact(&stContact2));

    // Add a contact via synchronization
    QContactName n4;
    n4.setFirstName(QLatin1String("Link"));
    n4.setMiddleName(QLatin1String("Donatella"));
    n4.setLastName(QLatin1String("CompositionTester"));
    stContact3.saveDetail(&n4);

    QList<QPair<QContact, QContact> > modifications;
    modifications.append(qMakePair(QContact(), stContact3));

    QtContactsSqliteExtensions::ContactManagerEngine::ConflictResolutionPolicy policy(QtContactsSqliteExtensions::ContactManagerEngine::PreserveLocalChanges);
    QContactManager::Error err;

    QtContactsSqliteExtensions::ContactManagerEngine *cme = QtContactsSqliteExtensions::contactManagerEngine(*m_cm);
    QVERIFY(cme->storeSyncContacts("test3", policy, &modifications, &err));

    QList<QContact> allContacts = m_cm->contacts(allSyncTargets);
    QContact st1, st2, st3, l, a;
    foreach (const QContact &curr, allContacts) {
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Link") && currName.lastName() == QLatin1String("CompositionTester")) {
            QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
            if (currSt.syncTarget() == QLatin1String("test1")) {
                st1 = curr;
            } else if (currSt.syncTarget() == QLatin1String("test2")) {
                st2 = curr;
            } else if (currSt.syncTarget() == QLatin1String("test3")) {
                st3 = curr;
            } else if (currSt.syncTarget() == QLatin1String("local")) {
                l = curr;
            } else if (currSt.syncTarget() == QLatin1String("aggregate")) {
                a = curr;
            }
        }
    }

    QVERIFY(st1.id() != QContactId());
    QVERIFY(st2.id() != QContactId());
    QVERIFY(st3.id() != QContactId());
    QVERIFY(l.id() != QContactId());
    QVERIFY(a.id() != QContactId());
    QVERIFY(relatedContactIds(st1.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(a.id()));
    QVERIFY(relatedContactIds(a.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(st1.id()));
    QVERIFY(relatedContactIds(st2.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(a.id()));
    QVERIFY(relatedContactIds(a.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(st2.id()));
    QVERIFY(relatedContactIds(st3.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(a.id()));
    QVERIFY(relatedContactIds(a.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(st3.id()));
    QVERIFY(relatedContactIds(l.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(a.id()));
    QVERIFY(relatedContactIds(a.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(l.id()));

    // The name of the local contact should be prioritized in aggregation
    QContactName name(a.detail<QContactName>());
    QCOMPARE(name.middleName(), n2.middleName());

    // The name elements unspecified by the local should be filled by other constituents in indeterminate order
    QCOMPARE(name.prefix(), n1.prefix());
    QCOMPARE(name.suffix(), n3.suffix());

    // Change the names in non-local constituents
    n1 = st1.detail<QContactName>();
    n1.setPrefix(QLatin1String("Dr."));
    n1.setMiddleName(QLatin1String("Enzo"));
    st1.saveDetail(&n1);
    QVERIFY(m_cm->saveContact(&st1));

    // Update with a definition mask
    n3 = st2.detail<QContactName>();
    n3.setMiddleName(QLatin1String("Francois"));
    n3.setSuffix(QLatin1String("MBA"));
    st2.saveDetail(&n3);
    QList<QContact> saveList;
    saveList.append(st2);
    QVERIFY(m_cm->saveContacts(&saveList, QList<QContactDetail::DetailType>() << QContactName::Type));

    a = m_cm->contact(retrievalId(a));

    name = a.detail<QContactName>();
    QCOMPARE(name.middleName(), n2.middleName());
    QCOMPARE(name.prefix(), n1.prefix());
    QCOMPARE(name.suffix(), n3.suffix());

    // Update with a definition mask not including name (should not update, but local still has priority)
    QContactName n5 = st2.detail<QContactName>();
    n5.setMiddleName(QLatin1String("Guillermo"));
    n5.setSuffix(QLatin1String("Ph.D"));
    st2.saveDetail(&n5);
    QVERIFY(m_cm->saveContacts(&saveList, QList<QContactDetail::DetailType>() << QContactAvatar::Type));

    a = m_cm->contact(retrievalId(a));

    name = a.detail<QContactName>();
    QCOMPARE(name.middleName(), n2.middleName());
    QCOMPARE(name.prefix(), n1.prefix());
    QCOMPARE(name.suffix(), n3.suffix());

    // Update via synchronization
    QList<QContactId> exportedIds;
    QList<QContact> syncContacts;
    QDateTime updatedSyncTime;
    QVERIFY(cme->fetchSyncContacts("test3", QDateTime(), exportedIds, &syncContacts, 0, 0, &updatedSyncTime, &err));
    QCOMPARE(syncContacts.count(), 1);

    QContact modified(syncContacts.at(0));

    n4 = modified.detail<QContactName>();
    n4.setMiddleName(QLatin1String("Hector"));
    modified.saveDetail(&n4);

    modifications.clear();
    modifications.append(qMakePair(syncContacts.at(0), modified));
    QVERIFY(cme->storeSyncContacts("test3", policy, &modifications, &err));

    a = m_cm->contact(retrievalId(a));
    l = m_cm->contact(retrievalId(l));

    // The sync update will also update the local
    name = a.detail<QContactName>();
    QCOMPARE(name.middleName(), n4.middleName());
    QCOMPARE(name.prefix(), n1.prefix());
    QCOMPARE(name.suffix(), n3.suffix());

    name = l.detail<QContactName>();
    QCOMPARE(name.middleName(), n4.middleName());

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

void tst_Aggregation::promotionToSingleLocal()
{
    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);

    // first, save a sync target alice.  This should generate an aggregate, but not a local.
    // (with no last name, we require QContactIncidental to find the correct aggregate)
    QContact syncAlice;
    QContactName san;
    san.setFirstName(QLatin1String("Single"));
    san.setMiddleName(QLatin1String("Promotion"));
    syncAlice.saveDetail(&san);

    QContactEmailAddress saem;
    saem.setEmailAddress(QLatin1String("spta@test.com"));
    syncAlice.saveDetail(&saem);

    QContactSyncTarget sast;
    sast.setSyncTarget(QLatin1String("test"));
    syncAlice.saveDetail(&sast);

    QVERIFY(m_cm->saveContact(&syncAlice));

    QList<QContact> allContacts = m_cm->contacts(allSyncTargets);
    QContact aggregateAlice;
    QContact localAlice;
    bool foundLocalAlice = false;
    bool foundSyncAlice = false;
    bool foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactEmailAddress currEm = curr.detail<QContactEmailAddress>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Single")
                && currName.middleName() == QLatin1String("Promotion")
                && currName.lastName().isEmpty()
                && currEm.emailAddress() == QLatin1String("spta@test.com")) {
            if (currSt.syncTarget() == QLatin1String("test")) {
                syncAlice = curr;
                foundSyncAlice = true;
            } else if (currSt.syncTarget() == QLatin1String("local")) {
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    QVERIFY(!foundLocalAlice); // shouldn't have created a local
    QVERIFY(foundSyncAlice);
    QVERIFY(foundAggregateAlice); // should have created an aggregate
    QVERIFY(relatedContactIds(syncAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(syncAlice.id()));

    // now we favoritify the aggregate contact.
    // this should cause the creation of a local contact.
    // The favoriteness should be promoted down to the local, but not to the sync.
    // Also, the email address should not be promoted down to the local, as it
    // came from the sync.
    QContactFavorite afav = aggregateAlice.detail<QContactFavorite>();
    afav.setFavorite(true);
    QVERIFY(aggregateAlice.saveDetail(&afav)); // this overwrites the existing one.
    QVERIFY(m_cm->saveContact(&aggregateAlice)); // should succeed, and update.

    allContacts = m_cm->contacts(allSyncTargets);
    foundLocalAlice = false;
    foundSyncAlice = false;
    foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactEmailAddress currEm = curr.detail<QContactEmailAddress>();
        QContactName currName = curr.detail<QContactName>();
        QContactFavorite currFav = curr.detail<QContactFavorite>();
        if (currName.firstName() == QLatin1String("Single")
                && currName.middleName() == QLatin1String("Promotion")
                && currName.lastName().isEmpty()) {
            if (currSt.syncTarget() == QLatin1String("test")) {
                QVERIFY(!foundSyncAlice); // found more than one = error...
                QCOMPARE(currEm.emailAddress(), QLatin1String("spta@test.com"));
                QVERIFY(!currFav.isFavorite());
                syncAlice = curr;
                foundSyncAlice = true;
            } else if (currSt.syncTarget() == QLatin1String("local")) {
                QVERIFY(!foundLocalAlice); // found more than one = error...
                QCOMPARE(currEm.emailAddress(), QString());
                QVERIFY(currFav.isFavorite());
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QVERIFY(!foundAggregateAlice); // found more than one = error...
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                QCOMPARE(currEm.emailAddress(), QLatin1String("spta@test.com"));
                QVERIFY(currFav.isFavorite());
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    // ensure that we found them all
    QVERIFY(foundLocalAlice);
    QVERIFY(foundSyncAlice);
    QVERIFY(foundAggregateAlice);

    // ensure they're related as required.
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));
    QVERIFY(relatedContactIds(syncAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(syncAlice.id()));

    // the created local contact is incidental; the others are not
    QVERIFY(!localAlice.details<QContactIncidental>().isEmpty());
    QVERIFY(localAlice.detail<QContactStatusFlags>().testFlag(QContactStatusFlags::IsIncidental));
    QVERIFY(syncAlice.details<QContactIncidental>().isEmpty());
    QVERIFY(!syncAlice.detail<QContactStatusFlags>().testFlag(QContactStatusFlags::IsIncidental));
    QVERIFY(aggregateAlice.details<QContactIncidental>().isEmpty());
    QVERIFY(!aggregateAlice.detail<QContactStatusFlags>().testFlag(QContactStatusFlags::IsIncidental));

    // finally, save a phone number in the aggregate.
    // this should get downpromoted to the local.
    // no more local contacts should be generated.
    QContactPhoneNumber aphn;
    aphn.setNumber("11111");
    QVERIFY(aggregateAlice.saveDetail(&aphn));
    QVERIFY(m_cm->saveContact(&aggregateAlice));

    allContacts = m_cm->contacts(allSyncTargets);
    foundLocalAlice = false;
    foundSyncAlice = false;
    foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Single")
                && currName.middleName() == QLatin1String("Promotion")
                && currName.lastName().isEmpty()) {
            if (currSt.syncTarget() == QLatin1String("test")) {
                QVERIFY(!foundSyncAlice); // found more than one = error...
                syncAlice = curr;
                foundSyncAlice = true;
            } else if (currSt.syncTarget() == QLatin1String("local")) {
                QVERIFY(!foundLocalAlice); // found more than one = error...
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QVERIFY(!foundAggregateAlice); // found more than one = error...
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    // ensure that we found them all
    QVERIFY(foundLocalAlice);
    QVERIFY(foundSyncAlice);
    QVERIFY(foundAggregateAlice);

    // ensure they're related as required.
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));
    QVERIFY(relatedContactIds(syncAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(syncAlice.id()));

    QCOMPARE(localAlice.detail<QContactEmailAddress>().emailAddress(), QString());
    QCOMPARE(localAlice.detail<QContactPhoneNumber>().number(), QLatin1String("11111"));
    QVERIFY(!detailProvenance(localAlice.detail<QContactPhoneNumber>()).isEmpty());
    QVERIFY(localAlice.detail<QContactFavorite>().isFavorite());

    QCOMPARE(syncAlice.detail<QContactEmailAddress>().emailAddress(), QLatin1String("spta@test.com"));
    QVERIFY(!detailProvenance(syncAlice.detail<QContactEmailAddress>()).isEmpty());
    QVERIFY(detailProvenanceContact(syncAlice.detail<QContactEmailAddress>()) != detailProvenanceContact(localAlice.detail<QContactPhoneNumber>()));
    QCOMPARE(syncAlice.detail<QContactPhoneNumber>().number(), QString());
    QVERIFY(!syncAlice.detail<QContactFavorite>().isFavorite());

    QCOMPARE(aggregateAlice.detail<QContactEmailAddress>().emailAddress(), QLatin1String("spta@test.com"));
    QCOMPARE(detailProvenance(aggregateAlice.detail<QContactEmailAddress>()), detailProvenance(syncAlice.detail<QContactEmailAddress>()));
    QCOMPARE(aggregateAlice.detail<QContactPhoneNumber>().number(), QLatin1String("11111"));
    QCOMPARE(detailProvenance(aggregateAlice.detail<QContactPhoneNumber>()), detailProvenance(localAlice.detail<QContactPhoneNumber>()));
    QVERIFY(detailProvenanceContact(aggregateAlice.detail<QContactPhoneNumber>()) != detailProvenanceContact(aggregateAlice.detail<QContactEmailAddress>()));
    QVERIFY(aggregateAlice.detail<QContactFavorite>().isFavorite());

    // now unfavorite aggregate alice.  ensure that it propagates properly.
    afav = aggregateAlice.detail<QContactFavorite>();
    afav.setFavorite(false);
    QVERIFY(aggregateAlice.saveDetail(&afav));
    QVERIFY(m_cm->saveContact(&aggregateAlice));

    allContacts = m_cm->contacts(allSyncTargets);
    foundLocalAlice = false;
    foundSyncAlice = false;
    foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Single")
                && currName.middleName() == QLatin1String("Promotion")
                && currName.lastName().isEmpty()) {
            if (currSt.syncTarget() == QLatin1String("test")) {
                QVERIFY(!foundSyncAlice); // found more than one = error...
                syncAlice = curr;
                foundSyncAlice = true;
            } else if (currSt.syncTarget() == QLatin1String("local")) {
                QVERIFY(!foundLocalAlice); // found more than one = error...
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QVERIFY(!foundAggregateAlice); // found more than one = error...
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    // ensure that we found them all
    QVERIFY(foundLocalAlice);
    QVERIFY(foundSyncAlice);
    QVERIFY(foundAggregateAlice);

    QCOMPARE(localAlice.detail<QContactEmailAddress>().emailAddress(), QString());
    QCOMPARE(localAlice.detail<QContactPhoneNumber>().number(), QLatin1String("11111"));
    QVERIFY(!localAlice.detail<QContactFavorite>().isFavorite());

    QCOMPARE(syncAlice.detail<QContactEmailAddress>().emailAddress(), QLatin1String("spta@test.com"));
    QCOMPARE(syncAlice.detail<QContactPhoneNumber>().number(), QString());
    QVERIFY(!syncAlice.detail<QContactFavorite>().isFavorite());

    QCOMPARE(aggregateAlice.detail<QContactEmailAddress>().emailAddress(), QLatin1String("spta@test.com"));
    QCOMPARE(aggregateAlice.detail<QContactPhoneNumber>().number(), QLatin1String("11111"));
    QVERIFY(!aggregateAlice.detail<QContactFavorite>().isFavorite());
}

void tst_Aggregation::uniquenessConstraints()
{
    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);

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
    QVERIFY(m_cm->saveContact(&localAlice));

    QList<QContact> allContacts = m_cm->contacts(allSyncTargets);
    QContact aggregateAlice;
    bool foundLocalAlice = false;
    bool foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactEmailAddress currEm = curr.detail<QContactEmailAddress>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Uniqueness")
                && currName.lastName() == QLatin1String("Constraints")
                && currEm.emailAddress() == QLatin1String("uniqueness@test.com")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundAggregateAlice);

    // test uniqueness constraint of favorite detail.
    QCOMPARE(aggregateAlice.details<QContactFavorite>().size(), 1);
    QContactFavorite afav;
    afav.setFavorite(true);
    QVERIFY(aggregateAlice.saveDetail(&afav)); // this actually creates a second (in memory) favorite detail
    QCOMPARE(aggregateAlice.details<QContactFavorite>().size(), 2);
    QVERIFY(!m_cm->saveContact(&aggregateAlice)); // should fail, as Favorite is unique
    QVERIFY(aggregateAlice.removeDetail(&afav));
    QCOMPARE(aggregateAlice.details<QContactFavorite>().size(), 1);
    afav = aggregateAlice.detail<QContactFavorite>();
    afav.setFavorite(true);
    QVERIFY(aggregateAlice.saveDetail(&afav));   // should update the existing.
    QVERIFY(m_cm->saveContact(&aggregateAlice)); // should succeed.
    QVERIFY(m_cm->contact(retrievalId(aggregateAlice)).detail<QContactFavorite>().isFavorite());
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));

    // test uniqueness constraint of birthday detail.
    QDateTime aliceBirthday = QLocale::c().toDateTime("25/12/1950 01:23:45", "dd/MM/yyyy hh:mm:ss");
    QCOMPARE(aggregateAlice.details<QContactBirthday>().size(), 0);
    QContactBirthday abd;
    abd.setDateTime(aliceBirthday);
    QVERIFY(aggregateAlice.saveDetail(&abd));
    QCOMPARE(aggregateAlice.details<QContactBirthday>().size(), 1);
    QVERIFY(m_cm->saveContact(&aggregateAlice));
    // now save another, should fail.
    QContactBirthday anotherBd;
    anotherBd.setDateTime(QDateTime::currentDateTime());
    QVERIFY(aggregateAlice.saveDetail(&anotherBd));
    QCOMPARE(aggregateAlice.details<QContactBirthday>().size(), 2);
    QVERIFY(!m_cm->saveContact(&aggregateAlice)); // should fail, uniqueness.
    QVERIFY(aggregateAlice.removeDetail(&anotherBd));
    QVERIFY(m_cm->saveContact(&aggregateAlice)); // back to just one, should succeed.
    QVERIFY(m_cm->contact(retrievalId(aggregateAlice)).detail<QContactBirthday>().date() == aliceBirthday.date());
    // now save a different birthday in another contact aggregated into alice.
    QContact testsyncAlice;
    QContactSyncTarget tsst;
    tsst.setSyncTarget("test");
    testsyncAlice.saveDetail(&tsst);
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
    QVERIFY(m_cm->saveContact(&testsyncAlice)); // should get aggregated into aggregateAlice.
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice)); // reload
    QCOMPARE(aggregateAlice.details<QContactBirthday>().size(), 1); // should still only have one birthday - local should take precedence.
    QVERIFY(aggregateAlice.detail<QContactBirthday>().date() == aliceBirthday.date());
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));

    // test uniqueness constraint of name detail.
    QVERIFY(aggregateAlice.details<QContactName>().size() == 1);
    QContactName anotherName;
    anotherName.setFirstName("Testing");
    QVERIFY(aggregateAlice.saveDetail(&anotherName));
    QCOMPARE(aggregateAlice.details<QContactName>().size(), 2);
    QVERIFY(!m_cm->saveContact(&aggregateAlice));
    QVERIFY(aggregateAlice.removeDetail(&anotherName));
    QCOMPARE(aggregateAlice.details<QContactName>().size(), 1);
    anotherName = aggregateAlice.detail<QContactName>();
    anotherName.setMiddleName("Middle");
    QVERIFY(aggregateAlice.saveDetail(&anotherName));
    QVERIFY(m_cm->saveContact(&aggregateAlice));
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));

    // test uniqueness (and read-only) constraint of sync target.
    QVERIFY(aggregateAlice.details<QContactSyncTarget>().size() == 1);
    QCOMPARE(aggregateAlice.detail<QContactSyncTarget>().value<QString>(QContactSyncTarget::FieldSyncTarget), QString::fromLatin1("aggregate"));
    QContactSyncTarget ast;
    ast.setSyncTarget("uniqueness");
    QVERIFY(aggregateAlice.saveDetail(&ast));
    QCOMPARE(aggregateAlice.details<QContactSyncTarget>().size(), 2);
    QVERIFY(!m_cm->saveContact(&aggregateAlice));
    QVERIFY(aggregateAlice.removeDetail(&ast));
    QCOMPARE(aggregateAlice.details<QContactSyncTarget>().size(), 1);
    ast = aggregateAlice.detail<QContactSyncTarget>();
    ast.setSyncTarget("uniqueness");
    QVERIFY(aggregateAlice.saveDetail(&ast));
    QVERIFY(!m_cm->saveContact(&aggregateAlice)); // should also fail, as sync target is read only.
    ast = aggregateAlice.detail<QContactSyncTarget>();
    ast.setSyncTarget("aggregate"); // reset the state.
    QVERIFY(aggregateAlice.saveDetail(&ast));
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));

    // test uniqueness constraint of timestamp detail.
    // Timestamp is a bit special, since if no values exist, we don't synthesise it,
    // even though it exists in the main table.
    QDateTime testDt = QDateTime::currentDateTime();
    bool hasCreatedTs = false;
    if (aggregateAlice.details<QContactTimestamp>().size() == 0) {
        QContactTimestamp firstTs;
        firstTs.setCreated(testDt);
        QVERIFY(aggregateAlice.saveDetail(&firstTs));
        QVERIFY(m_cm->saveContact(&aggregateAlice));
        hasCreatedTs = true;
    }
    QVERIFY(aggregateAlice.details<QContactTimestamp>().size() == 1);
    QContactTimestamp ats;
    ats.setLastModified(testDt);
    QVERIFY(aggregateAlice.saveDetail(&ats));
    QCOMPARE(aggregateAlice.details<QContactTimestamp>().size(), 2);
    QVERIFY(!m_cm->saveContact(&aggregateAlice));
    QVERIFY(aggregateAlice.removeDetail(&ats));
    QCOMPARE(aggregateAlice.details<QContactTimestamp>().size(), 1);
    ats = aggregateAlice.detail<QContactTimestamp>();
    ats.setLastModified(testDt);
    QVERIFY(aggregateAlice.saveDetail(&ats));

    QDateTime beforeWrite(QDateTime::currentDateTimeUtc());
    QVERIFY(m_cm->saveContact(&aggregateAlice));
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
    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allSyncTargets).size();
    int oldAggCount = aggCount;
    int oldAllCount = allCount;

    // set up some signal spies
    QSignalSpy addSpy(m_cm, contactsAddedSignal);
    QSignalSpy remSpy(m_cm, contactsRemovedSignal);
    int addSpyCount = 0;
    int remSpyCount = 0;

    // now add a new local contact (no synctarget specified == automatically local)
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
    QCOMPARE(m_cm->contactIds(allSyncTargets).size(), allCount + 2); // should have added local + aggregate
    allCount = m_cm->contactIds(allSyncTargets).size();

    QList<QContact> allContacts = m_cm->contacts(allSyncTargets);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    QContact localAlice;
    QContact aggregateAlice;
    bool foundLocalAlice = false;
    bool foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("67")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));

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
    QVERIFY(m_cm->contactIds(allSyncTargets).contains(ContactId::apiId(bob)));
    QList<QContact> stillExisting = m_cm->contacts(allSyncTargets);
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
    QVERIFY(!m_cm->contactIds(allSyncTargets).contains(ContactId::apiId(bob)));

    // should be back to our original counts
    int newAggCount = m_cm->contactIds().size();
    int newAllCount = m_cm->contactIds(allSyncTargets).size();
    QCOMPARE(newAggCount, oldAggCount);
    QCOMPARE(newAllCount, oldAllCount);
}

void tst_Aggregation::removeSingleAggregate()
{
    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allSyncTargets).size();
    int oldAggCount = aggCount;
    int oldAllCount = allCount;

    // set up some signal spies
    QSignalSpy addSpy(m_cm, contactsAddedSignal);
    QSignalSpy remSpy(m_cm, contactsRemovedSignal);
    int addSpyCount = 0;
    int remSpyCount = 0;

    // now add a new local contact (no synctarget specified == automatically local)
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
    QCOMPARE(m_cm->contactIds(allSyncTargets).size(), allCount + 2); // should have added local + aggregate
    allCount = m_cm->contactIds(allSyncTargets).size();

    QList<QContact> allContacts = m_cm->contacts(allSyncTargets);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    QContact localAlice;
    QContact aggregateAlice;
    bool foundLocalAlice = false;
    bool foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("7")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));

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

    // now remove aggregate alice.  We expect that the "orphan" local alice will also be removed.
    remSpyCount = remSpy.count();
    m_remAccumulatedIds.clear();
    QVERIFY(m_cm->removeContact(removalId(aggregateAlice)));
    QTRY_VERIFY(remSpy.count() > remSpyCount);
    QTRY_VERIFY(m_remAccumulatedIds.contains(ContactId::apiId(localAlice)));
    QTRY_VERIFY(m_remAccumulatedIds.contains(ContactId::apiId(aggregateAlice)));

    // alice's aggregate contact should have been removed, bob's should not have.
    QCOMPARE(m_cm->contactIds().size(), (aggCount-1));

    // but bob should not have been removed.
    QVERIFY(m_cm->contactIds(allSyncTargets).contains(ContactId::apiId(bob)));
    QList<QContact> stillExisting = m_cm->contacts(allSyncTargets);
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
    QVERIFY(!m_cm->contactIds(allSyncTargets).contains(ContactId::apiId(bob)));

    // should be back to our original counts
    int newAggCount = m_cm->contactIds().size();
    int newAllCount = m_cm->contactIds(allSyncTargets).size();
    QCOMPARE(newAggCount, oldAggCount);
    QCOMPARE(newAllCount, oldAllCount);
}

void tst_Aggregation::alterRelationships()
{
    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allSyncTargets).size();

    // set up some signal spies
    QSignalSpy addSpy(m_cm, contactsAddedSignal);
    QSignalSpy remSpy(m_cm, contactsRemovedSignal);
    int addSpyCount = 0;
    int remSpyCount = 0;

    // now add two new contacts (with different sync targets)
    QContact alice;

    QContactName an;
    an.setMiddleName("Alice");
    an.setFirstName("test");
    an.setLastName("alterRelationships");
    alice.saveDetail(&an);

    QContactSyncTarget aliceST;
    aliceST.setSyncTarget("test-one");
    alice.saveDetail(&aliceST);

    // Add a detail with non-empty detail URI - during the alteration, a duplicate
    // of the linked detail URI will exist in each aggregate, until the obsolete
    // aggregate is removed
    QContactPhoneNumber ap;
    ap.setNumber("1234567");
    ap.setSubTypes(QList<int>() << QContactPhoneNumber::SubTypeMobile);
    ap.setDetailUri("alice-alterRelationships-phone");
    alice.saveDetail(&ap);

    QContact bob;

    QContactName bn;
    bn.setMiddleName("Bob");
    bn.setLastName("alterRelationships");
    bob.saveDetail(&bn);

    QContactSyncTarget bobST;
    bobST.setSyncTarget("test-two");
    bob.saveDetail(&bobST);

    QContactPhoneNumber bp;
    bp.setNumber("2345678");
    bp.setSubTypes(QList<int>() << QContactPhoneNumber::SubTypeMobile);
    bp.setDetailUri("bob-alterRelationships-phone");
    bob.saveDetail(&bp);

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
    QCOMPARE(m_cm->contactIds(allSyncTargets).size(), allCount + 4); // should have added 2 normal + 2 aggregates
    allCount = m_cm->contactIds(allSyncTargets).size();

    QContact localAlice;
    QContact localBob;
    QContact aggregateAlice;
    QContact aggregateBob;

    QList<QContact> allContacts = m_cm->contacts(allSyncTargets);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.middleName() == QLatin1String("Alice") && currName.lastName() == QLatin1String("alterRelationships")) {
            if (currSt.syncTarget() == QLatin1String("test-one")) {
                localAlice = curr;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
            }
        } else if (currName.middleName() == QLatin1String("Bob") && currName.lastName() == QLatin1String("alterRelationships")) {
            if (currSt.syncTarget() == QLatin1String("test-two")) {
                localBob = curr;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateBob = curr;
            }
        }
    }

    QVERIFY(localAlice.id() != QContactId());
    QVERIFY(localBob.id() != QContactId());
    QVERIFY(aggregateAlice.id() != QContactId());
    QVERIFY(aggregateBob.id() != QContactId());
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(localBob.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateBob.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));
    QVERIFY(relatedContactIds(aggregateBob.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localBob.id()));

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

    allContacts = m_cm->contacts(allSyncTargets);
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.middleName() == QLatin1String("Alice") && currName.lastName() == QLatin1String("alterRelationships")) {
            if (currSt.syncTarget() == QLatin1String("test-one")) {
                localAlice = curr;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
            }
        } else if (currName.middleName() == QLatin1String("Bob") && currName.lastName() == QLatin1String("alterRelationships")) {
            if (currSt.syncTarget() == QLatin1String("test-two")) {
                localBob = curr;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateBob = curr;
            }
        }
    }

    QVERIFY(localAlice.id() != QContactId());
    QVERIFY(localBob.id() != QContactId());
    QVERIFY(aggregateAlice.id() != QContactId());
    QVERIFY(aggregateBob.id() != QContactId());
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(localBob.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateBob.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));
    QVERIFY(relatedContactIds(aggregateBob.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localBob.id()));
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

    allContacts = m_cm->contacts(allSyncTargets);
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.middleName() == QLatin1String("Alice") && currName.lastName() == QLatin1String("alterRelationships")) {
            if (currSt.syncTarget() == QLatin1String("test-one")) {
                localAlice = curr;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
            }
        } else if (currName.middleName() == QLatin1String("Bob") && currName.lastName() == QLatin1String("alterRelationships")) {
            if (currSt.syncTarget() == QLatin1String("test-two")) {
                localBob = curr;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateBob = curr;
            }
        }
    }

    QVERIFY(localAlice.id() != QContactId());
    QVERIFY(localBob.id() != QContactId());
    QVERIFY(aggregateAlice.id() != QContactId());
    QCOMPARE(aggregateBob.id(), QContactId());
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(localBob.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localBob.id()));

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
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));

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
    QVERIFY(!relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(!relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));
}

void tst_Aggregation::wasLocalUpdate()
{
    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allSyncTargets).size();

    // set up some signal spies
    QSignalSpy addSpy(m_cm, contactsAddedSignal);
    QSignalSpy remSpy(m_cm, contactsRemovedSignal);
    int addSpyCount = 0;
    int remSpyCount = 0;

    // now add two new local contacts (no synctarget specified == automatically local)
    QContact alice;

    QContactName an;
    an.setFirstName("Alice");
    an.setLastName("wasLocal");
    alice.saveDetail(&an);

    QContactPhoneNumber ap;
    ap.setNumber("1234567");
    ap.setSubTypes(QList<int>() << QContactPhoneNumber::SubTypeMobile);
    alice.saveDetail(&ap);

    QContact bob;

    QContactNickname bn;
    bn.setNickname("Cooper");
    bob.saveDetail(&bn);

    QContactPhoneNumber bp;
    bp.setNumber("2345678");
    bp.setSubTypes(QList<int>() << QContactPhoneNumber::SubTypeMobile);
    bob.saveDetail(&bp);

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
    QCOMPARE(m_cm->contactIds(allSyncTargets).size(), allCount + 4); // should have added locals + aggregates
    allCount = m_cm->contactIds(allSyncTargets).size();

    QContact localAlice;
    QContact localBob;
    QContact aggregateAlice;
    QContact aggregateBob;

    QList<QContact> allContacts = m_cm->contacts(allSyncTargets);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactName currName = curr.detail<QContactName>();
        QContactNickname currNick = curr.detail<QContactNickname>();
        if (currName.firstName() == QLatin1String("Alice") && currName.lastName() == QLatin1String("wasLocal")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                localAlice = curr;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
            }
        } else if (currNick.nickname() == QLatin1String("Cooper")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                localBob = curr;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateBob = curr;
            }
        }
    }

    // Verify the presence of contacts
    QVERIFY(localAlice.id() != QContactId());
    QVERIFY(localBob.id() != QContactId());
    QVERIFY(aggregateAlice.id() != QContactId());
    QVERIFY(aggregateBob.id() != QContactId());

    // Verify the relationships
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(localBob.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateBob.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));
    QVERIFY(relatedContactIds(aggregateBob.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localBob.id()));

    // Verify the details
    QCOMPARE(localAlice.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(localAlice.details<QContactPhoneNumber>().at(0).number(), QLatin1String("1234567"));
    QCOMPARE(localBob.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(localBob.details<QContactPhoneNumber>().at(0).number(), QLatin1String("2345678"));
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().at(0).number(), QLatin1String("1234567"));
    QCOMPARE(aggregateBob.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(aggregateBob.details<QContactPhoneNumber>().at(0).number(), QLatin1String("2345678"));

    // Convert localBob to was_local
    QContactSyncTarget bst = localBob.detail<QContactSyncTarget>();
    bst.setSyncTarget("was_local");
    QVERIFY(localBob.saveDetail(&bst));

    QVERIFY(m_cm->saveContact(&localBob));

    // Convert localBob to be a constituent of aggregateAlice
    QContactRelationship relationship;
    relationship = makeRelationship(QContactRelationship::Aggregates, aggregateAlice.id(), localBob.id());
    QVERIFY(m_cm->saveRelationship(&relationship));

    // Remove the aggregation relationship for Bob
    relationship = makeRelationship(QContactRelationship::Aggregates, aggregateBob.id(), localBob.id());
    QVERIFY(m_cm->removeRelationship(relationship));

    // The childless aggregate should have been removed
    QTRY_VERIFY(remSpy.count() > remSpyCount);
    QTRY_COMPARE(m_remAccumulatedIds.size(), 1);
    QVERIFY(m_remAccumulatedIds.contains(ContactId::apiId(aggregateBob)));
    remSpyCount = remSpy.count();

    // No new aggregate should have been created
    QCOMPARE(addSpy.count(), addSpyCount);

    // Verify the relationships
    localAlice = QContact();
    localBob = QContact();
    aggregateAlice = QContact();
    aggregateBob = QContact();

    allContacts = m_cm->contacts(allSyncTargets);
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactName currName = curr.detail<QContactName>();
        QContactNickname currNick = curr.detail<QContactNickname>();
        if (currName.firstName() == QLatin1String("Alice") && currName.lastName() == QLatin1String("wasLocal")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                localAlice = curr;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
            }
        } else if (currNick.nickname() == QLatin1String("Cooper")) {
            if (currSt.syncTarget() == QLatin1String("was_local")) {
                localBob = curr;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateBob = curr;
            }
        }
    }

    QVERIFY(localAlice.id() != QContactId());
    QVERIFY(localBob.id() != QContactId());
    QVERIFY(aggregateAlice.id() != QContactId());
    QVERIFY(aggregateBob.id() == QContactId());
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(localBob.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localBob.id()));
    QCOMPARE(localAlice.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(localAlice.details<QContactPhoneNumber>().at(0).number(), QLatin1String("1234567"));
    QCOMPARE(localBob.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(localBob.details<QContactPhoneNumber>().at(0).number(), QLatin1String("2345678"));
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().count(), 2);
    foreach (const QContactPhoneNumber &phoneNumber, aggregateAlice.details<QContactPhoneNumber>()) {
        QVERIFY(phoneNumber.number() == QLatin1String("1234567") ||
                phoneNumber.number() == QLatin1String("2345678"));
    }

    // Was-local details are aggregated
    QCOMPARE(aggregateAlice.detail<QContactNickname>().nickname(), QLatin1String("Cooper"));
    QCOMPARE(localAlice.detail<QContactNickname>().nickname(), QString());

    // Changes are promoted down to the was_local constituent
    QContactPhoneNumber pn = aggregateAlice.details<QContactPhoneNumber>().at(0);
    if (pn.number() != QLatin1String("2345678")) {
        pn = aggregateAlice.details<QContactPhoneNumber>().at(1);
    }
    pn.setNumber("7654321");
    pn.setSubTypes(QList<int>() << QContactPhoneNumber::SubTypeMobile);
    aggregateAlice.saveDetail(&pn);

    QVERIFY(m_cm->saveContact(&aggregateAlice));

    localAlice = m_cm->contact(retrievalId(localAlice));
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));
    localBob = m_cm->contact(retrievalId(localBob));

    QCOMPARE(localAlice.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(localAlice.details<QContactPhoneNumber>().at(0).number(), QLatin1String("1234567"));
    QCOMPARE(localBob.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(localBob.details<QContactPhoneNumber>().at(0).number(), QLatin1String("7654321"));
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().count(), 2);
    foreach (const QContactPhoneNumber &phoneNumber, aggregateAlice.details<QContactPhoneNumber>()) {
        QVERIFY(phoneNumber.number() == QLatin1String("1234567") ||
                phoneNumber.number() == QLatin1String("7654321"));
    }

    // New details are not promoted down to the was_local constituent
    pn = QContactPhoneNumber();
    pn.setNumber("1234321");
    aggregateAlice.saveDetail(&pn);

    QVERIFY(m_cm->saveContact(&aggregateAlice));

    localAlice = m_cm->contact(retrievalId(localAlice));
    aggregateAlice = m_cm->contact(retrievalId(aggregateAlice));
    localBob = m_cm->contact(retrievalId(localBob));

    QCOMPARE(localAlice.details<QContactPhoneNumber>().count(), 2);
    foreach (const QContactPhoneNumber &phoneNumber, localAlice.details<QContactPhoneNumber>()) {
        QVERIFY(phoneNumber.number() == QLatin1String("1234567") ||
                phoneNumber.number() == QLatin1String("1234321"));
    }
    QCOMPARE(localBob.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(localBob.details<QContactPhoneNumber>().at(0).number(), QLatin1String("7654321"));
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().count(), 3);
    foreach (const QContactPhoneNumber &phoneNumber, aggregateAlice.details<QContactPhoneNumber>()) {
        QVERIFY(phoneNumber.number() == QLatin1String("1234567") ||
                phoneNumber.number() == QLatin1String("7654321") ||
                phoneNumber.number() == QLatin1String("1234321"));
    }
}

void tst_Aggregation::wasLocalCreation()
{
    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allSyncTargets).size();

    // set up some signal spies
    QSignalSpy addSpy(m_cm, contactsAddedSignal);
    QSignalSpy remSpy(m_cm, contactsRemovedSignal);
    int addSpyCount = 0;

    QContactName an;
    an.setFirstName("Alice");
    an.setLastName("wasLocalCreation");

    // add two new local contacts which get aggregated together (no synctarget specified == automatically local)
    QContact alice;
    alice.saveDetail(&an);

    QContact bob;
    bob.saveDetail(&an);

    m_addAccumulatedIds.clear();
    QVERIFY(m_cm->saveContact(&alice));
    QVERIFY(m_cm->saveContact(&bob));
    QTRY_VERIFY(addSpy.count() >= addSpyCount + 2);
    QTRY_COMPARE(m_addAccumulatedIds.size(), 3); // should have added 2 locals + 1 aggregate
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(alice)));
    QVERIFY(m_addAccumulatedIds.contains(ContactId::apiId(bob)));
    addSpyCount = addSpy.count();

    QCOMPARE(m_cm->contactIds().size(), aggCount + 1); // 1 extra aggregate contact
    aggCount = m_cm->contactIds().size();
    QCOMPARE(m_cm->contactIds(allSyncTargets).size(), allCount + 3); // should have added 2 locals + 1 aggregate
    allCount = m_cm->contactIds(allSyncTargets).size();

    QContact localAlice;
    QContact localBob;
    QContact aggregateAlice;

    QList<QContact> allContacts = m_cm->contacts(allSyncTargets);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        if (currSt.syncTarget() == QLatin1String("aggregate")) {
            aggregateAlice = curr;
        } else {
            if (curr.id() == alice.id()) {
                localAlice = curr;

                // Alice should be a local constituent
                QCOMPARE(currSt.syncTarget(), QLatin1String("local"));
            } else {
                localBob = curr;

                // Bob should have become a was_local constituent
                QCOMPARE(currSt.syncTarget(), QLatin1String("was_local"));
            }
        }
    }

    // Verify the relationships
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(localBob.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localBob.id()));
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
    QTest::newRow("shared email") << false /* husband and wife, sharing email, should not get aggregated */
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "unspecified" << "" << "gumboots@test.com" << ""
        << "Jillian" << "Anastacia Faith" << "Gumboots" << "Jilly" << "unspecified" << "" << "gumboots@test.com" << "";
    QTest::newRow("shared phone") << false /* husband and wife, sharing phone, should not get aggregated */
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "unspecified" << "111992888337" << "" << ""
        << "Jillian" << "Anastacia Faith" << "Gumboots" << "Jilly" << "unspecified" << "111992888337" << "" << "";
    QTest::newRow("shared phone+email") << false /* husband and wife, sharing phone+email, should not get aggregated */
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "unspecified" << "111992888337" << "gumboots@test.com" << ""
        << "Jillian" << "Anastacia Faith" << "Gumboots" << "Jilly" << "unspecified" << "111992888337" << "gumboots@test.com" << "";
    QTest::newRow("shared phone+email+account") << false /* husband and wife, sharing phone+email+account, should not get aggregated */
        << "Frederick" << "William Preston" << "Gumboots" << "Freddy" << "unspecified" << "111992888337" << "gumboots@test.com" << "gumboots@familysocial"
        << "Jillian" << "Anastacia Faith" << "Gumboots" << "Jilly" << "unspecified" << "111992888337" << "gumboots@test.com" << "gumboots@familysocial";

    // different contactable details / same name
    QTest::newRow("match name, different p/e/a") << true /* identical name match is enough to match the contact */
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
    QTest::newRow("similar name, different p/e/a") << false /* Only the last names match; not enough */
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

    for (int i = 0; i < 2; ++i) {
        QContact a, b;
        QContactSyncTarget async, bsync;
        QContactName aname, bname;
        QContactNickname anick, bnick;
        QContactGender agen, bgen;
        QContactPhoneNumber aphn, bphn;
        QContactEmailAddress aem, bem;
        QContactOnlineAccount aoa, boa;

        // construct a
        async.setSyncTarget("aggregation-heuristic-a");
        a.saveDetail(&async);

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
        bsync.setSyncTarget("aggregation-heuristic-b");
        b.saveDetail(&bsync);

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

        m_cm->removeContact(a.id());
        m_cm->removeContact(b.id());
    }
}

void tst_Aggregation::regenerateAggregate()
{
    // here we create a local contact, and then save it
    // and then we create a "sync" contact, which should "match" it.
    // It should be related to the aggregate created for the sync.
    // We then remove the sync target, which should cause the aggregate
    // to be "regenerated" from the remaining aggregated contacts
    // (which in this case, is just the local contact).

    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);

    int aggCount = m_cm->contactIds().size();
    int allCount = m_cm->contactIds(allSyncTargets).size();

    // set up some signal spies
    QSignalSpy addSpy(m_cm, contactsAddedSignal);
    QSignalSpy chgSpy(m_cm, contactsChangedSignal);
    int addSpyCount = 0;
    int chgSpyCount = 0;

    // now add a new local contact (no synctarget specified == automatically local)
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
    QCOMPARE(m_cm->contactIds(allSyncTargets).size(), allCount + 2); // should have added local + aggregate
    allCount = m_cm->contactIds(allSyncTargets).size();

    QList<QContact> allContacts = m_cm->contacts(allSyncTargets);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    QContact localAlice;
    QContact aggregateAlice;
    bool foundLocalAlice = false;
    bool foundAggregateAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactEmailAddress currEm = curr.detail<QContactEmailAddress>();
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice8")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("88888")
                && currEm.emailAddress() == QLatin1String("alice8@test.com")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));

    // now add the doppleganger from another sync source
    QContact syncAlice;
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
    QCOMPARE(m_cm->contactIds(allSyncTargets).size(), allCount + 1); // should have added test but not an aggregate
    allCount = m_cm->contactIds(allSyncTargets).size();

    allContacts = m_cm->contacts(allSyncTargets);
    QCOMPARE(allContacts.size(), allCount); // should return as many contacts as contactIds.
    QContact testAlice;
    bool foundTestAlice = false;
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactEmailAddress currEm = curr.detail<QContactEmailAddress>();
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice8")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("88888")
                && currEm.emailAddress() == QLatin1String("alice8@test.com")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                QCOMPARE(curr.detail<QContactHobby>().value<QString>(QContactHobby::FieldHobby), QString()); // local shouldn't get it
                localAlice = curr;
                foundLocalAlice = true;
            } else if (currSt.syncTarget() == QLatin1String("test")) {
                QCOMPARE(curr.detail<QContactHobby>().value<QString>(QContactHobby::FieldHobby), QLatin1String("tennis")); // came from here
                testAlice = curr;
                foundTestAlice = true;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                QCOMPARE(curr.detail<QContactHobby>().value<QString>(QContactHobby::FieldHobby), QLatin1String("tennis")); // aggregated to here
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }

    QVERIFY(foundLocalAlice);
    QVERIFY(foundTestAlice);
    QVERIFY(foundAggregateAlice);
    QVERIFY(relatedContactIds(localAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(testAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(localAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(testAlice.id()));

    // now remove the "test" sync contact
    QVERIFY(m_cm->removeContact(removalId(testAlice)));
    QVERIFY(!m_cm->contactIds(allSyncTargets).contains(ContactId::apiId(testAlice))); // should have been removed

    // but the other contacts should NOT have been removed
    QVERIFY(m_cm->contactIds(allSyncTargets).contains(ContactId::apiId(localAlice)));
    QVERIFY(m_cm->contactIds(allSyncTargets).contains(ContactId::apiId(aggregateAlice)));

    // reload them, and ensure that the "hobby" detail has been removed from the aggregate
    allContacts = m_cm->contacts(allSyncTargets);
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactEmailAddress currEm = curr.detail<QContactEmailAddress>();
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice8")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("88888")
                && currEm.emailAddress() == QLatin1String("alice8@test.com")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                QCOMPARE(curr.detail<QContactHobby>().value<QString>(QContactHobby::FieldHobby), QString());
                localAlice = curr;
                foundLocalAlice = true;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                QCOMPARE(curr.detail<QContactHobby>().value<QString>(QContactHobby::FieldHobby), QString());
                aggregateAlice = curr;
                foundAggregateAlice = true;
            }
        }
    }
}

void tst_Aggregation::detailUris()
{
    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);

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

    QList<QContact> allContacts = m_cm->contacts(allSyncTargets);
    QContact localAlice;
    QContact aggregateAlice;
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactEmailAddress currEm = curr.detail<QContactEmailAddress>();
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice9")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("99999")
                && currEm.emailAddress() == QLatin1String("alice9@test.com")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                localAlice = curr;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
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
    allContacts = m_cm->contacts(allSyncTargets);
    localAlice = QContact();
    aggregateAlice = QContact();
    foreach (const QContact &curr, allContacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactEmailAddress currEm = curr.detail<QContactEmailAddress>();
        QContactPhoneNumber currPhn = curr.detail<QContactPhoneNumber>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice9")
                && currName.middleName() == QLatin1String("In")
                && currName.lastName() == QLatin1String("Wonderland")
                && currPhn.number() == QLatin1String("99999")
                && currEm.emailAddress() == QLatin1String("alice9@test.com")) {
            if (currSt.syncTarget() == QLatin1String("local")) {
                localAlice = curr;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
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

    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);
    QList<QContact> allContacts = m_cm->contacts(allSyncTargets);

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
    // 1) sync targets of all contacts in a batch save must be the same
    // 2) no two contacts from the same sync target should be aggregated together

    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);
    QList<QContact> allContacts = m_cm->contacts(allSyncTargets);
    int allContactsCount = allContacts.size();

    QContact a, b, c;
    QContactName aname, bname, cname;
    QContactSyncTarget ast, bst, cst;
    aname.setFirstName("a");
    aname.setLastName("batch");
    bname.setFirstName("b");
    bname.setLastName("batch");
    cname.setFirstName("c");
    cname.setLastName("batch");
    ast.setSyncTarget("async");
    bst.setSyncTarget("bsync");
    cst.setSyncTarget("csync");

    a.saveDetail(&aname);
    a.saveDetail(&ast);
    b.saveDetail(&bname);
    b.saveDetail(&bst);
    c.saveDetail(&cname);
    c.saveDetail(&cst);

    // a) batch save should fail due to different sync targets.
    QList<QContact> saveList;
    saveList << a << b << c;
    QVERIFY(!m_cm->saveContacts(&saveList));

    // b) same as (a)
    cst.setSyncTarget("bsync");
    c.saveDetail(&cst);
    saveList.clear();
    saveList << a << b << c;
    QVERIFY(!m_cm->saveContacts(&saveList));

    // c) same as (a) although in this case, local / empty are considered identical
    ast.setSyncTarget("local");
    bst.setSyncTarget(QString());
    cst.setSyncTarget("csync");
    a.saveDetail(&ast);
    b.saveDetail(&bst);
    c.saveDetail(&cst);
    saveList.clear();
    saveList << a << b << c;
    QVERIFY(!m_cm->saveContacts(&saveList));

    // d) now it should succeed.
    cst.setSyncTarget("local");
    c.saveDetail(&cst);
    saveList.clear();
    saveList << a << b << c;
    QVERIFY(m_cm->saveContacts(&saveList));

    allContacts = m_cm->contacts(allSyncTargets);
    int newContactsCount = allContacts.size() - allContactsCount;
    QCOMPARE(newContactsCount, 6); // 3 local, 3 aggregate

    // Now we test the semantic of "two contacts from the same sync target should get aggregated if they match"
    QContact d, e;
    QContactName dname, ename;
    QContactSyncTarget dst, est;
    dname.setFirstName("d");
    dname.setLastName("batch");
    ename.setFirstName("d");
    ename.setLastName("batch");
    ast.setSyncTarget("batch-sync");
    bst.setSyncTarget("batch-sync");

    d.saveDetail(&dname);
    d.saveDetail(&dst);
    e.saveDetail(&ename);
    e.saveDetail(&est);

    saveList.clear();
    saveList << d << e;
    QVERIFY(m_cm->saveContacts(&saveList));

    allContacts = m_cm->contacts(allSyncTargets);
    newContactsCount = allContacts.size() - allContactsCount;
    QCOMPARE(newContactsCount, 9); // 5 local, 4 aggregate - d and e should have been aggregated into one.
}

void tst_Aggregation::customSemantics()
{
    // the qtcontacts-sqlite engine defines some custom semantics
    // 1) avatars have a custom "AvatarMetadata" field
    // 2) self contact cannot be changed, and its id will always be "2"

    // 1 - ensure that the AvatarMetadata field is supported.
    QContact alice;
    QContactName an;
    an.setFirstName("Alice");
    alice.saveDetail(&an);
    QContactAvatar aa;
    aa.setImageUrl(QUrl(QString::fromLatin1("test.png")));
    aa.setValue(QContactAvatar__FieldAvatarMetadata, "cover");
    alice.saveDetail(&aa);
    QVERIFY(m_cm->saveContact(&alice));
    QContact aliceReloaded = m_cm->contact(retrievalId(alice));
    QCOMPARE(aliceReloaded.detail<QContactName>().value<QString>(QContactName::FieldFirstName), QLatin1String("Alice"));
    QCOMPARE(QUrl(aliceReloaded.detail<QContactAvatar>().value<QString>(QContactAvatar::FieldImageUrl)).toString(), QUrl(QString::fromLatin1("test.png")).toString());
    QCOMPARE(aliceReloaded.detail<QContactAvatar>().value<QString>(QContactAvatar__FieldAvatarMetadata), QLatin1String("cover"));

    // 2 - test the self contact semantics
    QCOMPARE(m_cm->selfContactId(), ContactId::apiId(2));
    QVERIFY(!m_cm->setSelfContactId(ContactId::apiId(alice)));

    // cleanup.
    m_cm->removeContact(removalId(alice));
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

    QContactIntersectionFilter cif;
    QContactDetailFilter stf;
    setFilterDetail<QContactSyncTarget>(stf, QContactSyncTarget::FieldSyncTarget);
    stf.setValue("local"); // explicitly ignore aggregates.
    QContactDetailFilter astf;
    setFilterDetail<QContactSyncTarget>(astf, QContactSyncTarget::FieldSyncTarget);
    astf.setValue("aggregate"); // only include aggregates
    QContactChangeLogFilter clf;

    clf.setEventType(QContactChangeLogFilter::EventAdded);
    clf.setSince(beforeBCreated); // should contain b, but not a as a's creation time was days-5
    cif.clear(); cif << stf << clf;
    QList<QContactId> filtered = m_cm->contactIds(cif);
    QVERIFY(!filtered.contains(retrievalId(a)));
    QVERIFY(filtered.contains(retrievalId(b)));

    clf.setEventType(QContactChangeLogFilter::EventAdded);
    clf.setSince(betweenTime);   // should not contain either a or b
    cif.clear(); cif << stf << clf;
    filtered = m_cm->contactIds(cif);
    QVERIFY(!filtered.contains(retrievalId(a)));
    QVERIFY(!filtered.contains(retrievalId(b)));

    clf.setEventType(QContactChangeLogFilter::EventChanged);
    clf.setSince(betweenTime);   // should contain a (modified after betweenTime) but not b (modified before)
    cif.clear(); cif << stf << clf;
    filtered = m_cm->contactIds(cif);
    QVERIFY(filtered.contains(retrievalId(a)));
    QVERIFY(!filtered.contains(retrievalId(b)));

    clf.setEventType(QContactChangeLogFilter::EventChanged);
    clf.setSince(startTime);     // should contain both a and b
    cif.clear(); cif << stf << clf;
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
    cif.clear(); cif << stf << clf;
    filtered = m_cm->contactIds(cif);
    QVERIFY(!filtered.contains(retrievalId(a)));
    QVERIFY(!filtered.contains(retrievalId(b)));

    // Either order of intersected filters is the same
    cif.clear(); cif << clf << stf;
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
    QVERIFY(filtered.count() >= 4);
    QVERIFY(filtered.contains(idA));
    QVERIFY(filtered.contains(idB));

    // Check that syncTarget filtering is also applied
    cif.clear(); cif << stf << clf;
    filtered = m_cm->contactIds(cif);
    QVERIFY(filtered.count() >= 2);
    QVERIFY(filtered.contains(idA));
    QVERIFY(filtered.contains(idB));

    cif.clear(); cif << astf << clf;
    filtered = m_cm->contactIds(cif);
    QVERIFY(filtered.count() >= 2);
    QVERIFY(!filtered.contains(idA));
    QVERIFY(!filtered.contains(idB));

    // Check that since values are applied
    clf = QContactChangeLogFilter();
    clf.setEventType(QContactChangeLogFilter::EventRemoved);
    clf.setSince(postDeleteTime);     // should contain both only b
    filtered = m_cm->contactIds(clf);
    QVERIFY(filtered.count() >= 2);
    QVERIFY(filtered.contains(idB));

    cif.clear(); cif << stf << clf;
    filtered = m_cm->contactIds(cif);
    QVERIFY(filtered.count() >= 1);
    QVERIFY(filtered.contains(idB));

    cif.clear(); cif << astf << clf;
    filtered = m_cm->contactIds(cif);
    QVERIFY(filtered.count() >= 1);
    QVERIFY(!filtered.contains(idB));

    // Check that since is not required
    clf = QContactChangeLogFilter();
    clf.setEventType(QContactChangeLogFilter::EventRemoved);
    filtered = m_cm->contactIds(clf);
    QVERIFY(filtered.count() >= 4);
    QVERIFY(filtered.contains(idA));
    QVERIFY(filtered.contains(idB));
}

void tst_Aggregation::deactivationSingle()
{
    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);

    // add a new contact (synctarget must be specified to deactivate)
    QContact syncAlice;

    QContactSyncTarget sast;
    sast.setSyncTarget(QLatin1String("test"));
    syncAlice.saveDetail(&sast);

    QContactName an;
    an.setFirstName("Alice");
    an.setMiddleName("Through The");
    an.setLastName("Looking-Glass");
    syncAlice.saveDetail(&an);

    QVERIFY(m_cm->saveContact(&syncAlice));

    QContact aggregateAlice;

    QList<QContact> contacts = m_cm->contacts(allSyncTargets);
    foreach (const QContact &curr, contacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice") &&
            currName.middleName() == QLatin1String("Through The") &&
            currName.lastName() == QLatin1String("Looking-Glass")) {
            if (currSt.syncTarget() == QLatin1String("test")) {
                syncAlice = curr;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
            }
        }
    }

    // Check that aggregation occurred
    QVERIFY(syncAlice.id() != QContactId());
    QVERIFY(aggregateAlice.id() != QContactId());
    QVERIFY(relatedContactIds(syncAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).count() == 1);
    QVERIFY(relatedContactIds(syncAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).count() == 1);
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(syncAlice.id()));

    // Verify the presence of the contact IDs
    QList<QContactId> contactIds = m_cm->contactIds(allSyncTargets);
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

    contacts = m_cm->contacts(allSyncTargets);
    foreach (const QContact &curr, contacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice") &&
            currName.middleName() == QLatin1String("Through The") &&
            currName.lastName() == QLatin1String("Looking-Glass")) {
            if (currSt.syncTarget() == QLatin1String("test")) {
                syncAlice = curr;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
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
    QVERIFY(relatedContactIds(syncAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).count() == 0);

    // Verify the presence/absence of the contact IDs
    contactIds = m_cm->contactIds(allSyncTargets);
    QVERIFY(contactIds.contains(ContactId::apiId(syncAlice)) == false);

    contactIds = m_cm->contactIds(allSyncTargets & QContactStatusFlags::matchFlag(QContactStatusFlags::IsDeactivated, QContactFilter::MatchContains));
    QVERIFY(contactIds.contains(syncAliceId));

    // Reactivate
    deactivated = syncAlice.detail<QContactDeactivated>();
    syncAlice.removeDetail(&deactivated);
    QVERIFY(m_cm->saveContact(&syncAlice));

    syncAlice = aggregateAlice = QContact();

    contacts = m_cm->contacts(allSyncTargets);
    foreach (const QContact &curr, contacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice") &&
            currName.middleName() == QLatin1String("Through The") &&
            currName.lastName() == QLatin1String("Looking-Glass")) {
            if (currSt.syncTarget() == QLatin1String("test")) {
                syncAlice = curr;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
            }
        }
    }

    // Check that aggregation is restored
    QVERIFY(syncAlice.id() != QContactId());
    QVERIFY(aggregateAlice.id() != QContactId());
    QVERIFY(relatedContactIds(syncAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).count() == 1);
    QVERIFY(relatedContactIds(syncAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).count() == 1);
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(syncAlice.id()));

    // Check that the reactivated contact retains the same ID
    QVERIFY(syncAlice.id() == syncAliceId);

    // Verify the presence of all contact IDs when queried
    contactIds = m_cm->contactIds(allSyncTargets);
    QVERIFY(contactIds.contains(ContactId::apiId(syncAlice)));
    QVERIFY(contactIds.contains(ContactId::apiId(aggregateAlice)));
}

void tst_Aggregation::deactivationMultiple()
{
    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);

    // add a new contact (synctarget must be specified to deactivate)
    QContact syncAlice;

    QContactName an;
    an.setFirstName("Alice");
    an.setMiddleName("Through The");
    an.setLastName("Looking-Glass");
    syncAlice.saveDetail(&an);

    QContactSyncTarget sast;
    sast.setSyncTarget(QLatin1String("test"));
    syncAlice.saveDetail(&sast);

    QContactPhoneNumber aph;
    aph.setNumber("34567");
    syncAlice.saveDetail(&aph);

    QVERIFY(m_cm->saveContact(&syncAlice));

    // now add the doppelganger from another sync source
    QContact otherAlice;
    QContactName san;
    san.setFirstName(an.firstName());
    san.setMiddleName(an.middleName());
    san.setLastName(an.lastName());
    otherAlice.saveDetail(&san);

    QContactPhoneNumber saph;
    saph.setNumber("76543");
    otherAlice.saveDetail(&saph);

    QContactSyncTarget oast;
    oast.setSyncTarget(QLatin1String("other"));
    otherAlice.saveDetail(&oast);

    QVERIFY(m_cm->saveContact(&otherAlice));

    QContact aggregateAlice;

    QList<QContact> contacts = m_cm->contacts(allSyncTargets);
    foreach (const QContact &curr, contacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice") &&
            currName.middleName() == QLatin1String("Through The") &&
            currName.lastName() == QLatin1String("Looking-Glass")) {
            if (currSt.syncTarget() == QLatin1String("test")) {
                syncAlice = curr;
            } else if (currSt.syncTarget() == QLatin1String("other")) {
                otherAlice = curr;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
            }
        }
    }

    // Check that aggregation occurred
    QVERIFY(syncAlice.id() != QContactId());
    QVERIFY(otherAlice.id() != QContactId());
    QVERIFY(aggregateAlice.id() != QContactId());
    QVERIFY(relatedContactIds(syncAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).count() == 1);
    QVERIFY(relatedContactIds(syncAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(otherAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).count() == 1);
    QVERIFY(relatedContactIds(otherAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).count() == 2);
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(syncAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(otherAlice.id()));

    QCOMPARE(syncAlice.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(otherAlice.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().count(), 2);

    // Verify the presence of the contact IDs
    QList<QContactId> contactIds = m_cm->contactIds(allSyncTargets);
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

    contacts = m_cm->contacts(allSyncTargets);
    foreach (const QContact &curr, contacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice") &&
            currName.middleName() == QLatin1String("Through The") &&
            currName.lastName() == QLatin1String("Looking-Glass")) {
            if (currSt.syncTarget() == QLatin1String("test")) {
                syncAlice = curr;
            } else if (currSt.syncTarget() == QLatin1String("other")) {
                otherAlice = curr;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
            }
        }
    }

    // The deactivated contact is not found (although relationships remain)
    QVERIFY(syncAlice.id() == QContactId());
    QVERIFY(otherAlice.id() != QContactId());
    QVERIFY(aggregateAlice.id() != QContactId());
    QVERIFY(relatedContactIds(otherAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).count() == 1);
    QVERIFY(relatedContactIds(otherAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).count() == 2);
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(syncAliceId));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(otherAlice.id()));

    // Check that the aggregate does not contain the deactivated detail
    QCOMPARE(otherAlice.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().count(), 1);

    // Verify that test alice still exists
    syncAlice = m_cm->contact(syncAliceId);
    QVERIFY(syncAlice.id() == syncAliceId);
    QVERIFY(relatedContactIds(syncAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).count() == 1);
    QVERIFY(relatedContactIds(syncAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));

    // Verify the presence/absence of the contact IDs
    contactIds = m_cm->contactIds(allSyncTargets);
    QVERIFY(contactIds.contains(ContactId::apiId(syncAlice)) == false);
    QVERIFY(contactIds.contains(ContactId::apiId(otherAlice)));
    QVERIFY(contactIds.contains(ContactId::apiId(aggregateAlice)));

    contactIds = m_cm->contactIds(allSyncTargets & QContactStatusFlags::matchFlag(QContactStatusFlags::IsDeactivated, QContactFilter::MatchContains));
    QVERIFY(contactIds.contains(syncAliceId));
    QVERIFY(contactIds.contains(ContactId::apiId(otherAlice)) == false);
    QVERIFY(contactIds.contains(ContactId::apiId(aggregateAlice)) == false);

    // Reactivate
    deactivated = syncAlice.detail<QContactDeactivated>();
    syncAlice.removeDetail(&deactivated);
    QVERIFY(m_cm->saveContact(&syncAlice));

    syncAlice = otherAlice = aggregateAlice = QContact();

    contacts = m_cm->contacts(allSyncTargets);
    foreach (const QContact &curr, contacts) {
        QContactSyncTarget currSt = curr.detail<QContactSyncTarget>();
        QContactName currName = curr.detail<QContactName>();
        if (currName.firstName() == QLatin1String("Alice") &&
            currName.middleName() == QLatin1String("Through The") &&
            currName.lastName() == QLatin1String("Looking-Glass")) {
            if (currSt.syncTarget() == QLatin1String("test")) {
                syncAlice = curr;
            } else if (currSt.syncTarget() == QLatin1String("other")) {
                otherAlice = curr;
            } else {
                QCOMPARE(currSt.syncTarget(), QLatin1String("aggregate"));
                aggregateAlice = curr;
            }
        }
    }

    // Check that aggregation remains intact
    QVERIFY(syncAlice.id() != QContactId());
    QVERIFY(otherAlice.id() != QContactId());
    QVERIFY(aggregateAlice.id() != QContactId());
    QVERIFY(relatedContactIds(syncAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).count() == 1);
    QVERIFY(relatedContactIds(syncAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(otherAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).count() == 1);
    QVERIFY(relatedContactIds(otherAlice.relatedContacts(aggregatesRelationship, QContactRelationship::First)).contains(aggregateAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).count() == 2);
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(syncAlice.id()));
    QVERIFY(relatedContactIds(aggregateAlice.relatedContacts(aggregatesRelationship, QContactRelationship::Second)).contains(otherAlice.id()));

    // Re-activated details are now aggregated
    QCOMPARE(syncAlice.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(otherAlice.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(aggregateAlice.details<QContactPhoneNumber>().count(), 2);

    // Check that the reactivated contact retains the same ID
    QVERIFY(syncAlice.id() == syncAliceId);

    // Verify the presence of all contact IDs when queried
    contactIds = m_cm->contactIds(allSyncTargets);
    QVERIFY(contactIds.contains(ContactId::apiId(syncAlice)));
    QVERIFY(contactIds.contains(ContactId::apiId(otherAlice)));
    QVERIFY(contactIds.contains(ContactId::apiId(aggregateAlice)));
}

void tst_Aggregation::fetchSyncContacts()
{
    QtContactsSqliteExtensions::ContactManagerEngine *cme = QtContactsSqliteExtensions::contactManagerEngine(*m_cm);

    QSignalSpy syncSpy(cme, SIGNAL(syncContactsChanged(QStringList)));

    QList<QContact> syncContacts;
    QList<QContact> addedContacts;
    QList<QContactId> exportedIds;
    QList<QContactId> syncExportedIds;
    QList<QContactId> deletedIds;

    QDateTime initialTime = QDateTime::currentDateTimeUtc();
    QDateTime updatedSyncTime;
    QTest::qWait(1);

    // Initial test - ensure that nothing is reported for sync
    QContactManager::Error err;
    QVERIFY(cme->fetchSyncContacts("sync-test", initialTime, exportedIds, &syncContacts, &addedContacts, &deletedIds, &updatedSyncTime, &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(syncContacts.count(), 0);
    QCOMPARE(addedContacts.count(), 0);
    QCOMPARE(deletedIds.count(), 0);
    QCOMPARE(syncSpy.count(), 0);

    // Also test export behavior
    syncContacts.clear();
    addedContacts.clear();
    deletedIds.clear();
    QVERIFY(cme->fetchSyncContacts("export", initialTime, syncExportedIds, &syncContacts, &addedContacts, &deletedIds, &updatedSyncTime, &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(syncContacts.count(), 0);
    QCOMPARE(addedContacts.count(), 0);
    QCOMPARE(deletedIds.count(), 0);

    // Store a sync target contact originating at this service
    QContactName n;
    n.setFirstName("Mad");
    n.setLastName("Hatter");

    QContactSyncTarget stTarget;
    stTarget.setSyncTarget("sync-test");

    QContact stc;
    stc.saveDetail(&n);
    stc.saveDetail(&stTarget);

    QContactEmailAddress e;
    e.setEmailAddress("mad.hatter@example.org");
    stc.saveDetail(&e);

    // Add a detail marked as non-exportable
    QContactPhoneNumber pn;
    pn.setNumber("555-555-555");
    pn.setValue(QContactDetail__FieldNonexportable, true);
    stc.saveDetail(&pn);

    QVERIFY(m_cm->saveContact(&stc));

    QTRY_COMPARE(syncSpy.count(), 1);
    QVariantList signalArgs(syncSpy.takeFirst());
    QCOMPARE(syncSpy.count(), 0);
    QCOMPARE(signalArgs.count(), 1);
    QStringList changedSyncTargets(signalArgs.first().value<QStringList>());
    QCOMPARE(changedSyncTargets.count(), 1);
    QCOMPARE(changedSyncTargets.at(0), QString::fromLatin1("sync-test"));

    stc = m_cm->contact(retrievalId(stc));

    QCOMPARE(stc.relatedContacts(aggregatesRelationship, QContactRelationship::First).count(), 1);
    QContactId a1 = stc.relatedContacts(aggregatesRelationship, QContactRelationship::First).at(0).id();

    syncContacts.clear();
    addedContacts.clear();
    deletedIds.clear();
    updatedSyncTime = QDateTime();
    QVERIFY(cme->fetchSyncContacts("sync-test", initialTime, exportedIds, &syncContacts, &addedContacts, &deletedIds, &updatedSyncTime, &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(syncContacts.count(), 1);
    QCOMPARE(addedContacts.count(), 0);
    QCOMPARE(deletedIds.count(), 0);

    // The partial aggregate should have the same ID as the constituent it was derived from
    QContact pa = syncContacts.at(0);
    QCOMPARE(pa.id(), stc.id());

    QCOMPARE(pa.details<QContactEmailAddress>().count(), 1);
    QCOMPARE(pa.details<QContactEmailAddress>().at(0).emailAddress(), e.emailAddress());

    QCOMPARE(pa.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(pa.details<QContactPhoneNumber>().at(0).number(), pn.number());

    // Invalid since time is equivalent to not having a time limitation
    syncContacts.clear();
    updatedSyncTime = QDateTime();
    QVERIFY(cme->fetchSyncContacts("sync-test", QDateTime(), exportedIds, &syncContacts, 0, 0, &updatedSyncTime, &err));
    QCOMPARE(err, QContactManager::NoError);
    QVERIFY(syncContacts.count() >= 1);

    // This contact should also be reported for export
    syncContacts.clear();
    addedContacts.clear();
    deletedIds.clear();
    QVERIFY(cme->fetchSyncContacts("export", initialTime, syncExportedIds, &syncContacts, &addedContacts, &deletedIds, &updatedSyncTime, &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(syncContacts.count(), 0);
    QCOMPARE(addedContacts.count(), 1);
    QCOMPARE(deletedIds.count(), 0);

    // The export contact should have the aggregate ID
    pa = addedContacts.at(0);
    QCOMPARE(pa.id(), a1);

    QCOMPARE(pa.details<QContactEmailAddress>().count(), 1);
    QCOMPARE(pa.details<QContactEmailAddress>().at(0).emailAddress(), e.emailAddress());

    // The non-exportable data is excluded from the export details
    QCOMPARE(pa.details<QContactPhoneNumber>().count(), 0);

    // Add this contact to the sync-export set
    syncExportedIds.append(pa.id());

    // Create a local contact which is merged with the test contact
    QContact lc;
    lc.saveDetail(&n);

    e.setEmailAddress("cheshire.cat@example.org");
    lc.saveDetail(&e);

    QVERIFY(m_cm->saveContact(&lc));

    QTRY_COMPARE(syncSpy.count(), 1);
    signalArgs = syncSpy.takeFirst();
    QCOMPARE(syncSpy.count(), 0);
    QCOMPARE(signalArgs.count(), 1);
    changedSyncTargets = signalArgs.first().value<QStringList>();
    QCOMPARE(changedSyncTargets.count(), 1);
    QCOMPARE(changedSyncTargets.at(0), QString::fromLatin1("sync-test"));

    lc = m_cm->contact(retrievalId(lc));

    QCOMPARE(lc.relatedContacts(aggregatesRelationship, QContactRelationship::First).count(), 1);
    QCOMPARE(lc.relatedContacts(aggregatesRelationship, QContactRelationship::First).at(0).id(), a1);

    syncContacts.clear();
    addedContacts.clear();
    deletedIds.clear();
    updatedSyncTime = QDateTime();
    QVERIFY(cme->fetchSyncContacts("sync-test", initialTime, exportedIds, &syncContacts, &addedContacts, &deletedIds, &updatedSyncTime, &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(syncContacts.count(), 1);
    QCOMPARE(addedContacts.count(), 0);
    QCOMPARE(deletedIds.count(), 0);

    pa = syncContacts.at(0);
    QCOMPARE(pa.id(), stc.id());
    QCOMPARE(pa.details<QContactEmailAddress>().count(), 2);
    QSet<QString> addresses;
    foreach (const QContactEmailAddress &addr, pa.details<QContactEmailAddress>()) {
        addresses.insert(addr.emailAddress());
    }
    QVERIFY(addresses.contains(stc.detail<QContactEmailAddress>().emailAddress()));
    QVERIFY(addresses.contains(lc.detail<QContactEmailAddress>().emailAddress()));

    // The modified aggregate should be reported modified for export
    syncContacts.clear();
    addedContacts.clear();
    deletedIds.clear();
    QVERIFY(cme->fetchSyncContacts("export", initialTime, syncExportedIds, &syncContacts, &addedContacts, &deletedIds, &updatedSyncTime, &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(syncContacts.count(), 1);
    QCOMPARE(addedContacts.count(), 0);
    QCOMPARE(deletedIds.count(), 0);

    pa = syncContacts.at(0);
    QCOMPARE(pa.id(), a1);
    QCOMPARE(pa.details<QContactEmailAddress>().count(), 2);
    addresses.clear();
    foreach (const QContactEmailAddress &addr, pa.details<QContactEmailAddress>()) {
        addresses.insert(addr.emailAddress());
    }
    QVERIFY(addresses.contains(stc.detail<QContactEmailAddress>().emailAddress()));
    QVERIFY(addresses.contains(lc.detail<QContactEmailAddress>().emailAddress()));

    // Create another local contact which is merged with the test contact (the first local becomes was_local)
    QContact alc;
    alc.saveDetail(&n);

    e.setEmailAddress("white.rabbit@example.org");
    alc.saveDetail(&e);

    QVERIFY(m_cm->saveContact(&alc));

    QTRY_COMPARE(syncSpy.count(), 1);
    signalArgs = syncSpy.takeFirst();
    QCOMPARE(syncSpy.count(), 0);
    QCOMPARE(signalArgs.count(), 1);
    changedSyncTargets = signalArgs.first().value<QStringList>();
    QCOMPARE(changedSyncTargets.count(), 1);
    QCOMPARE(changedSyncTargets.at(0), QString::fromLatin1("sync-test"));

    alc = m_cm->contact(retrievalId(alc));

    QCOMPARE(alc.relatedContacts(aggregatesRelationship, QContactRelationship::First).count(), 1);
    QCOMPARE(alc.relatedContacts(aggregatesRelationship, QContactRelationship::First).at(0).id(), a1);

    syncContacts.clear();
    addedContacts.clear();
    deletedIds.clear();
    updatedSyncTime = QDateTime();
    QVERIFY(cme->fetchSyncContacts("sync-test", initialTime, exportedIds, &syncContacts, &addedContacts, &deletedIds, &updatedSyncTime, &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(syncContacts.count(), 1);
    QCOMPARE(addedContacts.count(), 0);
    QCOMPARE(deletedIds.count(), 0);

    pa = syncContacts.at(0);
    QCOMPARE(pa.id(), stc.id());
    QCOMPARE(pa.details<QContactEmailAddress>().count(), 3);
    addresses.clear();
    foreach (const QContactEmailAddress &addr, pa.details<QContactEmailAddress>()) {
        addresses.insert(addr.emailAddress());
    }
    QVERIFY(addresses.contains(stc.detail<QContactEmailAddress>().emailAddress()));
    QVERIFY(addresses.contains(lc.detail<QContactEmailAddress>().emailAddress()));
    QVERIFY(addresses.contains(alc.detail<QContactEmailAddress>().emailAddress()));

    // The export contact is modified, and the ID is unchanged
    syncContacts.clear();
    addedContacts.clear();
    deletedIds.clear();
    QVERIFY(cme->fetchSyncContacts("export", initialTime, syncExportedIds, &syncContacts, &addedContacts, &deletedIds, &updatedSyncTime, &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(syncContacts.count(), 1);
    QCOMPARE(addedContacts.count(), 0);
    QCOMPARE(deletedIds.count(), 0);

    pa = syncContacts.at(0);
    QCOMPARE(pa.id(), a1);
    QCOMPARE(pa.details<QContactEmailAddress>().count(), 3);
    addresses.clear();
    foreach (const QContactEmailAddress &addr, pa.details<QContactEmailAddress>()) {
        addresses.insert(addr.emailAddress());
    }
    QVERIFY(addresses.contains(stc.detail<QContactEmailAddress>().emailAddress()));
    QVERIFY(addresses.contains(lc.detail<QContactEmailAddress>().emailAddress()));
    QVERIFY(addresses.contains(alc.detail<QContactEmailAddress>().emailAddress()));

    // Create a different sync target contact which is merged with the test contact
    QContact dstc;
    dstc.saveDetail(&n);

    e.setEmailAddress("lewis.carroll@example.org");
    dstc.saveDetail(&e);

    QContactSyncTarget dstTarget;
    dstTarget.setSyncTarget("different-sync-target");
    dstc.saveDetail(&dstTarget);

    QVERIFY(m_cm->saveContact(&dstc));

    QTRY_COMPARE(syncSpy.count(), 1);
    signalArgs = syncSpy.takeFirst();
    QCOMPARE(syncSpy.count(), 0);
    QCOMPARE(signalArgs.count(), 1);
    changedSyncTargets = signalArgs.first().value<QStringList>();
    QCOMPARE(changedSyncTargets.count(), 1);
    QCOMPARE(changedSyncTargets.at(0), QString::fromLatin1("different-sync-target"));

    dstc = m_cm->contact(retrievalId(dstc));

    QCOMPARE(dstc.relatedContacts(aggregatesRelationship, QContactRelationship::First).count(), 1);
    QCOMPARE(dstc.relatedContacts(aggregatesRelationship, QContactRelationship::First).at(0).id(), a1);

    syncContacts.clear();
    addedContacts.clear();
    deletedIds.clear();
    updatedSyncTime = QDateTime();
    QVERIFY(cme->fetchSyncContacts("sync-test", initialTime, exportedIds, &syncContacts, &addedContacts, &deletedIds, &updatedSyncTime, &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(syncContacts.count(), 1);
    QCOMPARE(addedContacts.count(), 0);
    QCOMPARE(deletedIds.count(), 0);

    // Data from the other sync target should not be be returned here
    pa = syncContacts.at(0);
    QCOMPARE(pa.id(), stc.id());
    QCOMPARE(pa.details<QContactEmailAddress>().count(), 3);
    addresses.clear();
    foreach (const QContactEmailAddress &addr, pa.details<QContactEmailAddress>()) {
        addresses.insert(addr.emailAddress());
    }
    QVERIFY(addresses.contains(stc.detail<QContactEmailAddress>().emailAddress()));
    QVERIFY(addresses.contains(lc.detail<QContactEmailAddress>().emailAddress()));
    QVERIFY(addresses.contains(alc.detail<QContactEmailAddress>().emailAddress()));
    QVERIFY(!addresses.contains(dstc.detail<QContactEmailAddress>().emailAddress()));

    // The additional sync target data is included for export
    syncContacts.clear();
    addedContacts.clear();
    deletedIds.clear();
    QVERIFY(cme->fetchSyncContacts("export", initialTime, syncExportedIds, &syncContacts, &addedContacts, &deletedIds, &updatedSyncTime, &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(syncContacts.count(), 1);
    QCOMPARE(addedContacts.count(), 0);
    QCOMPARE(deletedIds.count(), 0);

    pa = syncContacts.at(0);
    QCOMPARE(pa.id(), a1);
    QCOMPARE(pa.details<QContactEmailAddress>().count(), 4);
    addresses.clear();
    foreach (const QContactEmailAddress &addr, pa.details<QContactEmailAddress>()) {
        addresses.insert(addr.emailAddress());
    }
    QVERIFY(addresses.contains(stc.detail<QContactEmailAddress>().emailAddress()));
    QVERIFY(addresses.contains(lc.detail<QContactEmailAddress>().emailAddress()));
    QVERIFY(addresses.contains(alc.detail<QContactEmailAddress>().emailAddress()));
    QVERIFY(addresses.contains(dstc.detail<QContactEmailAddress>().emailAddress()));

    // Store an additional sync target contact originating at this service, merged into the same aggregate
    QContact astc;
    astc.saveDetail(&n);
    astc.saveDetail(&stTarget);

    e.setEmailAddress("march.hare@example.org");
    astc.saveDetail(&e);

    QVERIFY(m_cm->saveContact(&astc));

    QTRY_COMPARE(syncSpy.count(), 1);
    signalArgs = syncSpy.takeFirst();
    QCOMPARE(syncSpy.count(), 0);
    QCOMPARE(signalArgs.count(), 1);
    changedSyncTargets = signalArgs.first().value<QStringList>();
    QCOMPARE(changedSyncTargets.count(), 1);
    QCOMPARE(changedSyncTargets.at(0), QString::fromLatin1("sync-test"));

    astc = m_cm->contact(retrievalId(astc));

    QCOMPARE(astc.relatedContacts(aggregatesRelationship, QContactRelationship::First).count(), 1);
    QCOMPARE(astc.relatedContacts(aggregatesRelationship, QContactRelationship::First).at(0).id(), a1);

    // We should have two partial aggregates now
    syncContacts.clear();
    addedContacts.clear();
    deletedIds.clear();
    updatedSyncTime = QDateTime();
    QVERIFY(cme->fetchSyncContacts("sync-test", initialTime, exportedIds, &syncContacts, &addedContacts, &deletedIds, &updatedSyncTime, &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(syncContacts.count(), 2);
    QCOMPARE(addedContacts.count(), 0);
    QCOMPARE(deletedIds.count(), 0);
    QDateTime updatedInitialSyncTime = updatedSyncTime; // store for use later.

    QVERIFY(syncContacts.at(0).id() != syncContacts.at(1).id());
    for (int i = 0; i < 2; ++i) {
        // Each partial aggregate should contain their own data, plus any shared local/was_local data
        pa = syncContacts.at(i);
        if (pa.id() == stc.id()) {
            QCOMPARE(pa.details<QContactEmailAddress>().count(), 3);
            addresses.clear();
            foreach (const QContactEmailAddress &addr, pa.details<QContactEmailAddress>()) {
                addresses.insert(addr.emailAddress());
            }
            QVERIFY(addresses.contains(stc.detail<QContactEmailAddress>().emailAddress()));
            QVERIFY(addresses.contains(lc.detail<QContactEmailAddress>().emailAddress()));
            QVERIFY(addresses.contains(alc.detail<QContactEmailAddress>().emailAddress()));
        } else {
            QCOMPARE(pa.id(), astc.id());
            QCOMPARE(pa.details<QContactEmailAddress>().count(), 3);
            addresses.clear();
            foreach (const QContactEmailAddress &addr, pa.details<QContactEmailAddress>()) {
                addresses.insert(addr.emailAddress());
            }
            QVERIFY(addresses.contains(astc.detail<QContactEmailAddress>().emailAddress()));
            QVERIFY(addresses.contains(lc.detail<QContactEmailAddress>().emailAddress()));
            QVERIFY(addresses.contains(alc.detail<QContactEmailAddress>().emailAddress()));
        }
    }

    // The export set still contains a single contact
    syncContacts.clear();
    addedContacts.clear();
    deletedIds.clear();
    QVERIFY(cme->fetchSyncContacts("export", initialTime, syncExportedIds, &syncContacts, &addedContacts, &deletedIds, &updatedSyncTime, &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(syncContacts.count(), 1);
    QCOMPARE(addedContacts.count(), 0);
    QCOMPARE(deletedIds.count(), 0);

    pa = syncContacts.at(0);
    QCOMPARE(pa.id(), a1);
    QCOMPARE(pa.details<QContactEmailAddress>().count(), 5);
    addresses.clear();
    foreach (const QContactEmailAddress &addr, pa.details<QContactEmailAddress>()) {
        addresses.insert(addr.emailAddress());
    }
    QVERIFY(addresses.contains(stc.detail<QContactEmailAddress>().emailAddress()));
    QVERIFY(addresses.contains(lc.detail<QContactEmailAddress>().emailAddress()));
    QVERIFY(addresses.contains(alc.detail<QContactEmailAddress>().emailAddress()));
    QVERIFY(addresses.contains(dstc.detail<QContactEmailAddress>().emailAddress()));
    QVERIFY(addresses.contains(astc.detail<QContactEmailAddress>().emailAddress()));

    // Create a time boundary here
    QDateTime nextTime = QDateTime::currentDateTimeUtc();
    QTest::qWait(1);

    // Add an new local contact, which is unrelated
    QContact nlc;

    QContactName n2;
    n2.setFirstName("The Queen");
    n2.setLastName("of Hearts");
    nlc.saveDetail(&n2);

    e.setEmailAddress("her.majesty@example.org");
    nlc.saveDetail(&e);

    QVERIFY(m_cm->saveContact(&nlc));

    // No sync target is affected by this store
    QVERIFY(!syncSpy.wait(1000));

    nlc = m_cm->contact(retrievalId(nlc));

    QCOMPARE(nlc.relatedContacts(aggregatesRelationship, QContactRelationship::First).count(), 1);
    QVERIFY(nlc.relatedContacts(aggregatesRelationship, QContactRelationship::First).at(0).id() != a1);
    QContactId a2 = nlc.relatedContacts(aggregatesRelationship, QContactRelationship::First).at(0).id();

    // The new contact will be reported as newly added
    syncContacts.clear();
    addedContacts.clear();
    deletedIds.clear();
    updatedSyncTime = QDateTime();
    QVERIFY(cme->fetchSyncContacts("sync-test", initialTime, exportedIds, &syncContacts, &addedContacts, &deletedIds, &updatedSyncTime, &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(syncContacts.count(), 2);
    QCOMPARE(addedContacts.count(), 1);
    QCOMPARE(deletedIds.count(), 0);

    pa = syncContacts.at(0);
    QVERIFY(pa.id() == stc.id() || pa.id() == astc.id());
    pa = syncContacts.at(1);
    QVERIFY(pa.id() == stc.id() || pa.id() == astc.id());

    // Added contacts return the IDs of their local constituents
    pa = addedContacts.at(0);
    QCOMPARE(pa.id(), nlc.id());
    QCOMPARE(pa.details<QContactEmailAddress>().count(), 1);
    addresses.clear();
    foreach (const QContactEmailAddress &addr, pa.details<QContactEmailAddress>()) {
        addresses.insert(addr.emailAddress());
    }
    QVERIFY(addresses.contains(nlc.detail<QContactEmailAddress>().emailAddress()));

    // The new contact is also reported for export
    syncContacts.clear();
    addedContacts.clear();
    deletedIds.clear();
    QVERIFY(cme->fetchSyncContacts("export", initialTime, syncExportedIds, &syncContacts, &addedContacts, &deletedIds, &updatedSyncTime, &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(syncContacts.count(), 1);
    QCOMPARE(addedContacts.count(), 1);
    QCOMPARE(deletedIds.count(), 0);

    QCOMPARE(syncContacts.at(0).id(), a1);

    pa = addedContacts.at(0);
    QCOMPARE(pa.id(), a2);
    QCOMPARE(pa.details<QContactEmailAddress>().count(), 1);
    addresses.clear();
    foreach (const QContactEmailAddress &addr, pa.details<QContactEmailAddress>()) {
        addresses.insert(addr.emailAddress());
    }
    QVERIFY(addresses.contains(nlc.detail<QContactEmailAddress>().emailAddress()));

    // Add this contact to our export set
    syncExportedIds.append(pa.id());

    // Create a time boundary here
    QDateTime afterAdditionTime = QDateTime::currentDateTimeUtc();
    QTest::qWait(1);

    // Test the timestamp filtering - fetch using nextTime
    syncContacts.clear();
    addedContacts.clear();
    deletedIds.clear();
    updatedSyncTime = QDateTime();
    QVERIFY(cme->fetchSyncContacts("sync-test", nextTime, exportedIds, &syncContacts, &addedContacts, &deletedIds, &updatedSyncTime, &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(syncContacts.count(), 0);
    QCOMPARE(addedContacts.count(), 1);
    QCOMPARE(deletedIds.count(), 0);
    QDateTime updatedNextSyncTime = updatedSyncTime;

    pa = addedContacts.at(0);
    QCOMPARE(pa.id(), nlc.id());

    // Test the timestamp filtering - fetch using updatedSyncTime -- results should be equivalent to using nextTime
    syncContacts.clear();
    addedContacts.clear();
    deletedIds.clear();
    updatedSyncTime = updatedInitialSyncTime; // the updatedInitialSyncTime should be "equivalent" time-separation-wise to nextTime.
    QVERIFY(cme->fetchSyncContacts("sync-test", updatedSyncTime, exportedIds, &syncContacts, &addedContacts, &deletedIds, &updatedSyncTime, &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(syncContacts.count(), 0);
    QCOMPARE(addedContacts.count(), 1);
    QCOMPARE(deletedIds.count(), 0);

    pa = addedContacts.at(0);
    QCOMPARE(pa.id(), nlc.id());

    // ensure that the timestamp update calculations work as expected
    QCOMPARE(updatedSyncTime, updatedNextSyncTime); // should have been updated to the same value.
    QCOMPARE(updatedSyncTime, pa.detail<QContactTimestamp>().created());

    // Fetch with afterAddition
    syncContacts.clear();
    addedContacts.clear();
    deletedIds.clear();
    updatedSyncTime = QDateTime();
    QVERIFY(cme->fetchSyncContacts("sync-test", afterAdditionTime, exportedIds, &syncContacts, &addedContacts, &deletedIds, &updatedSyncTime, &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(syncContacts.count(), 0);
    QCOMPARE(addedContacts.count(), 0);
    QCOMPARE(deletedIds.count(), 0);

    // Report the added contact in the previously-exported list, as being of relevance
    exportedIds.append(nlc.id());

    syncContacts.clear();
    addedContacts.clear();
    deletedIds.clear();
    updatedSyncTime = QDateTime();
    QVERIFY(cme->fetchSyncContacts("sync-test", afterAdditionTime, exportedIds, &syncContacts, &addedContacts, &deletedIds, &updatedSyncTime, &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(syncContacts.count(), 0);
    QCOMPARE(addedContacts.count(), 0);
    QCOMPARE(deletedIds.count(), 0);

    syncContacts.clear();
    addedContacts.clear();
    deletedIds.clear();
    updatedSyncTime = QDateTime();
    QVERIFY(cme->fetchSyncContacts("sync-test", initialTime, exportedIds, &syncContacts, &addedContacts, &deletedIds, &updatedSyncTime, &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(syncContacts.count(), 3);
    QCOMPARE(addedContacts.count(), 0);
    QCOMPARE(deletedIds.count(), 0);

    // The added contact is now in our sync set
    QSet<QContactId> paIds;
    foreach (const QContact &pac, syncContacts) {
        paIds.insert(pac.id());
    }
    QCOMPARE(paIds, (QList<QContactId>() << stc.id() << astc.id() << nlc.id()).toSet());

    // Merge another contact into the extraneous contact, from a different sync target
    QContact nastc;
    nastc.saveDetail(&n2);

    nastc.saveDetail(&dstTarget);

    e.setEmailAddress("caterpillar@example.org");
    nastc.saveDetail(&e);

    QVERIFY(m_cm->saveContact(&nastc));

    QTRY_COMPARE(syncSpy.count(), 1);
    signalArgs = syncSpy.takeFirst();
    QCOMPARE(syncSpy.count(), 0);
    QCOMPARE(signalArgs.count(), 1);
    changedSyncTargets = signalArgs.first().value<QStringList>();
    QCOMPARE(changedSyncTargets.count(), 1);
    QCOMPARE(changedSyncTargets.at(0), QString::fromLatin1("different-sync-target"));

    nastc = m_cm->contact(retrievalId(nastc));

    QCOMPARE(nastc.relatedContacts(aggregatesRelationship, QContactRelationship::First).count(), 1);
    QCOMPARE(nastc.relatedContacts(aggregatesRelationship, QContactRelationship::First).at(0).id(), a2);

    QContact na = m_cm->contact(a2);
    QCOMPARE(na.details<QContactEmailAddress>().count(), 2);
    addresses.clear();
    foreach (const QContactEmailAddress &addr, na.details<QContactEmailAddress>()) {
        addresses.insert(addr.emailAddress());
    }
    QVERIFY(addresses.contains(nlc.detail<QContactEmailAddress>().emailAddress()));
    QVERIFY(addresses.contains(nastc.detail<QContactEmailAddress>().emailAddress()));

    // Filter so only this contact is included
    syncContacts.clear();
    addedContacts.clear();
    deletedIds.clear();
    updatedSyncTime = QDateTime();
    QVERIFY(cme->fetchSyncContacts("sync-test", afterAdditionTime, exportedIds, &syncContacts, &addedContacts, &deletedIds, &updatedSyncTime, &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(syncContacts.count(), 1);
    QCOMPARE(addedContacts.count(), 0);
    QCOMPARE(deletedIds.count(), 0);

    // The data from the other sync target is excluded
    pa = syncContacts.at(0);
    QCOMPARE(pa.id(), nlc.id());
    QCOMPARE(pa.details<QContactEmailAddress>().count(), 1);
    addresses.clear();
    foreach (const QContactEmailAddress &addr, pa.details<QContactEmailAddress>()) {
        addresses.insert(addr.emailAddress());
    }
    QVERIFY(addresses.contains(nlc.detail<QContactEmailAddress>().emailAddress()));
    QVERIFY(!addresses.contains(nastc.detail<QContactEmailAddress>().emailAddress()));

    QDateTime finalAdditionTime = QDateTime::currentDateTimeUtc();
    QTest::qWait(1);

    // The contact is reported as modified for export
    syncContacts.clear();
    addedContacts.clear();
    deletedIds.clear();
    QVERIFY(cme->fetchSyncContacts("export", afterAdditionTime, syncExportedIds, &syncContacts, &addedContacts, &deletedIds, &updatedSyncTime, &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(syncContacts.count(), 1);
    QCOMPARE(addedContacts.count(), 0);
    QCOMPARE(deletedIds.count(), 0);

    pa = syncContacts.at(0);
    QCOMPARE(pa.id(), a2);
    QCOMPARE(pa.details<QContactEmailAddress>().count(), 2);
    addresses.clear();
    foreach (const QContactEmailAddress &addr, pa.details<QContactEmailAddress>()) {
        addresses.insert(addr.emailAddress());
    }
    QVERIFY(addresses.contains(nlc.detail<QContactEmailAddress>().emailAddress()));
    QVERIFY(addresses.contains(nastc.detail<QContactEmailAddress>().emailAddress()));

    // Create a final new contact, with a different sync target
    QContact fstc;

    QContactName n3;
    n3.setFirstName("Mock");
    n3.setLastName("Turtle");
    fstc.saveDetail(&n3);

    fstc.saveDetail(&dstTarget);

    QVERIFY(m_cm->saveContact(&fstc));

    QTRY_COMPARE(syncSpy.count(), 1);
    signalArgs = syncSpy.takeFirst();
    QCOMPARE(syncSpy.count(), 0);
    QCOMPARE(signalArgs.count(), 1);
    changedSyncTargets = signalArgs.first().value<QStringList>();
    QCOMPARE(changedSyncTargets.count(), 1);
    QCOMPARE(changedSyncTargets.at(0), QString::fromLatin1("different-sync-target"));

    fstc = m_cm->contact(retrievalId(fstc));

    QCOMPARE(fstc.relatedContacts(aggregatesRelationship, QContactRelationship::First).count(), 1);
    QVERIFY(fstc.relatedContacts(aggregatesRelationship, QContactRelationship::First).at(0).id() != a1);
    QVERIFY(fstc.relatedContacts(aggregatesRelationship, QContactRelationship::First).at(0).id() != a2);
    QContactId a3 = fstc.relatedContacts(aggregatesRelationship, QContactRelationship::First).at(0).id();

    // This contact should not be reported to us, because of the sync target
    syncContacts.clear();
    addedContacts.clear();
    deletedIds.clear();
    updatedSyncTime = QDateTime();
    QVERIFY(cme->fetchSyncContacts("sync-test", finalAdditionTime, exportedIds, &syncContacts, &addedContacts, &deletedIds, &updatedSyncTime, &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(syncContacts.count(), 0);
    QCOMPARE(addedContacts.count(), 0);
    QCOMPARE(deletedIds.count(), 0);

    // It is reported for export, however
    syncContacts.clear();
    addedContacts.clear();
    deletedIds.clear();
    QVERIFY(cme->fetchSyncContacts("export", finalAdditionTime, syncExportedIds, &syncContacts, &addedContacts, &deletedIds, &updatedSyncTime, &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(syncContacts.count(), 0);
    QCOMPARE(addedContacts.count(), 1);
    QCOMPARE(deletedIds.count(), 0);

    pa = addedContacts.at(0);
    QCOMPARE(pa.id(), a3);

    // Add to the exported set
    syncExportedIds.append(pa.id());

    // Now make the aggregate a favorite, which will cause the incidental creation of a local
    QContact fa = m_cm->contact(a3);

    QContactFavorite f = fa.detail<QContactFavorite>();
    f.setFavorite(true);
    QVERIFY(fa.saveDetail(&f));
    QVERIFY(m_cm->saveContact(&fa));

    QTRY_COMPARE(syncSpy.count(), 1);
    signalArgs = syncSpy.takeFirst();
    QCOMPARE(syncSpy.count(), 0);
    QCOMPARE(signalArgs.count(), 1);
    changedSyncTargets = signalArgs.first().value<QStringList>();
    QCOMPARE(changedSyncTargets.count(), 1);
    QCOMPARE(changedSyncTargets.at(0), QString::fromLatin1("different-sync-target"));

    fa = m_cm->contact(a3);

    QVERIFY(fa.detail<QContactFavorite>().isFavorite());
    QCOMPARE(fa.relatedContacts(aggregatesRelationship, QContactRelationship::Second).count(), 2);

    QContact flc;
    if (fa.relatedContacts(aggregatesRelationship, QContactRelationship::Second).at(0).id() == fstc.id()) {
        flc = m_cm->contact(fa.relatedContacts(aggregatesRelationship, QContactRelationship::Second).at(1).id());
    } else {
        QVERIFY(fa.relatedContacts(aggregatesRelationship, QContactRelationship::Second).at(1).id() == fstc.id());
        flc = m_cm->contact(fa.relatedContacts(aggregatesRelationship, QContactRelationship::Second).at(0).id());
    }

    // The created local constituent is incidental
    QVERIFY(!flc.details<QContactIncidental>().isEmpty());
    QVERIFY(fstc.details<QContactIncidental>().isEmpty());

    // Although we created a local, it isn't reported as added since it is incidental
    syncContacts.clear();
    addedContacts.clear();
    deletedIds.clear();
    updatedSyncTime = QDateTime();
    QVERIFY(cme->fetchSyncContacts("sync-test", finalAdditionTime, exportedIds, &syncContacts, &addedContacts, &deletedIds, &updatedSyncTime, &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(syncContacts.count(), 0);
    QCOMPARE(addedContacts.count(), 0);
    QCOMPARE(deletedIds.count(), 0);

    // This results in a modification for export purposes
    syncContacts.clear();
    addedContacts.clear();
    deletedIds.clear();
    QVERIFY(cme->fetchSyncContacts("export", finalAdditionTime, syncExportedIds, &syncContacts, &addedContacts, &deletedIds, &updatedSyncTime, &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(syncContacts.count(), 1);
    QCOMPARE(addedContacts.count(), 0);
    QCOMPARE(deletedIds.count(), 0);

    pa = syncContacts.at(0);
    QCOMPARE(pa.id(), a3);
    QVERIFY(pa.detail<QContactFavorite>().isFavorite());

    // Modify a contact locally, and affected sync targets should be reported
    QContact ac = m_cm->contact(a1);

    QContactHobby h;
    h.setHobby("Croquet");
    ac.saveDetail(&h);

    QVERIFY(m_cm->saveContact(&ac));

    QTRY_COMPARE(syncSpy.count(), 1);
    signalArgs = syncSpy.takeFirst();
    QCOMPARE(syncSpy.count(), 0);
    QCOMPARE(signalArgs.count(), 1);
    changedSyncTargets = signalArgs.first().value<QStringList>();
    QCOMPARE(changedSyncTargets.count(), 2);
    QCOMPARE(changedSyncTargets.toSet(), (QSet<QString>() << "sync-test" << "different-sync-target"));

    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);

    QList<QContactId> contactIds = m_cm->contactIds(allSyncTargets);
    QVERIFY(contactIds.contains(stc.id()));
    QVERIFY(contactIds.contains(lc.id()));
    QVERIFY(contactIds.contains(alc.id()));
    QVERIFY(contactIds.contains(dstc.id()));
    QVERIFY(contactIds.contains(astc.id()));
    QVERIFY(contactIds.contains(a1));
    QVERIFY(contactIds.contains(nlc.id()));
    QVERIFY(contactIds.contains(nastc.id()));
    QVERIFY(contactIds.contains(a2));
    QVERIFY(contactIds.contains(fstc.id()));
    QVERIFY(contactIds.contains(flc.id()));
    QVERIFY(contactIds.contains(a3));

    // Remove a local contact that affects an aggregate containing our sync target
    QVERIFY(m_cm->removeContact(lc.id()));

    // The deletion should report changes to sync targets
    QTRY_COMPARE(syncSpy.count(), 1);
    signalArgs = syncSpy.takeFirst();
    QCOMPARE(syncSpy.count(), 0);
    QCOMPARE(signalArgs.count(), 1);
    changedSyncTargets = signalArgs.first().value<QStringList>();
    QCOMPARE(changedSyncTargets.count(), 2);
    QCOMPARE(changedSyncTargets.toSet(), (QSet<QString>() << "sync-test" << "different-sync-target"));

    // Remove a local contact that affects a different sync target
    QVERIFY(m_cm->removeContact(nlc.id()));

    QTRY_COMPARE(syncSpy.count(), 1);
    signalArgs = syncSpy.takeFirst();
    QCOMPARE(syncSpy.count(), 0);
    QCOMPARE(signalArgs.count(), 1);
    changedSyncTargets = signalArgs.first().value<QStringList>();
    QCOMPARE(changedSyncTargets.count(), 1);
    QCOMPARE(changedSyncTargets.first(), QString::fromLatin1("different-sync-target"));

    contactIds = m_cm->contactIds(allSyncTargets);
    QVERIFY(contactIds.contains(stc.id()));
    QVERIFY(!contactIds.contains(lc.id()));
    QVERIFY(contactIds.contains(alc.id()));
    QVERIFY(contactIds.contains(dstc.id()));
    QVERIFY(contactIds.contains(astc.id()));
    QVERIFY(contactIds.contains(a1));
    QVERIFY(!contactIds.contains(nlc.id()));
    QVERIFY(contactIds.contains(nastc.id()));
    QVERIFY(contactIds.contains(a2));
    QVERIFY(contactIds.contains(fstc.id()));
    QVERIFY(contactIds.contains(flc.id()));
    QVERIFY(contactIds.contains(a3));

    // Now remove all contacts
    QVERIFY(m_cm->removeContacts(QList<QContactId>() << a1 << a2 << a3));

    contactIds = m_cm->contactIds(allSyncTargets);
    QVERIFY(!contactIds.contains(stc.id()));
    QVERIFY(!contactIds.contains(lc.id()));
    QVERIFY(!contactIds.contains(alc.id()));
    QVERIFY(!contactIds.contains(dstc.id()));
    QVERIFY(!contactIds.contains(astc.id()));
    QVERIFY(!contactIds.contains(a1));
    QVERIFY(!contactIds.contains(nlc.id()));
    QVERIFY(!contactIds.contains(nastc.id()));
    QVERIFY(!contactIds.contains(a2));
    QVERIFY(!contactIds.contains(fstc.id()));
    QVERIFY(!contactIds.contains(flc.id()));
    QVERIFY(!contactIds.contains(a3));

    // The IDs previously reported to us as sync or added contacts should be reported as deleted
    syncContacts.clear();
    addedContacts.clear();
    deletedIds.clear();
    updatedSyncTime = QDateTime();
    QVERIFY(cme->fetchSyncContacts("sync-test", afterAdditionTime, exportedIds, &syncContacts, &addedContacts, &deletedIds, &updatedSyncTime, &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(syncContacts.count(), 0);
    QCOMPARE(addedContacts.count(), 0);
    QCOMPARE(deletedIds.count(), 3);
    QVERIFY(deletedIds.contains(stc.id()));
    QVERIFY(deletedIds.contains(astc.id()));
    QVERIFY(deletedIds.contains(nlc.id()));

    // Previously exported contacts are also reported as deleted
    syncContacts.clear();
    addedContacts.clear();
    deletedIds.clear();
    QVERIFY(cme->fetchSyncContacts("export", finalAdditionTime, syncExportedIds, &syncContacts, &addedContacts, &deletedIds, &updatedSyncTime, &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(syncContacts.count(), 0);
    QCOMPARE(addedContacts.count(), 0);
    QCOMPARE(deletedIds.count(), 3);
    QVERIFY(deletedIds.contains(a1));
    QVERIFY(deletedIds.contains(a2));
    QVERIFY(deletedIds.contains(a3));

    QTRY_COMPARE(syncSpy.count(), 1);
    signalArgs = syncSpy.takeFirst();
    QCOMPARE(syncSpy.count(), 0);
    QCOMPARE(signalArgs.count(), 1);
    changedSyncTargets = signalArgs.first().value<QStringList>();
    QCOMPARE(changedSyncTargets.count(), 2);
    QCOMPARE(changedSyncTargets.toSet(), (QSet<QString>() << "sync-test" << "different-sync-target"));
}

void tst_Aggregation::storeSyncContacts()
{
    QtContactsSqliteExtensions::ContactManagerEngine *cme = QtContactsSqliteExtensions::contactManagerEngine(*m_cm);

    QSignalSpy syncSpy(cme, SIGNAL(syncContactsChanged(QStringList)));

    QDateTime initialTime = QDateTime::currentDateTimeUtc();
    QTest::qWait(1);

    // Check for no errors with no input
    QList<QPair<QContact, QContact> > modifications;
    QContactManager::Error err;
    QtContactsSqliteExtensions::ContactManagerEngine::ConflictResolutionPolicy policy(QtContactsSqliteExtensions::ContactManagerEngine::PreserveLocalChanges);
    QVERIFY(cme->storeSyncContacts("sync-test", policy, &modifications, &err));
    QCOMPARE(err, QContactManager::NoError);

    // Store a sync target contact originating at this service
    QContactName n;
    n.setFirstName("Albert");
    n.setLastName("Einstein");

    QContact stc;
    stc.saveDetail(&n);

    QContactEmailAddress e;
    e.setEmailAddress("albert.einstein@example.org");
    stc.saveDetail(&e);

    QContactEmailAddress e2;
    e2.setEmailAddress("theoretical.physicist@example.org");
    stc.saveDetail(&e2);

    QContactHobby h;
    h.setHobby("Kickboxing");
    stc.saveDetail(&h);

    modifications.append(qMakePair(QContact(), stc));
    QVERIFY(cme->storeSyncContacts("sync-test", policy, &modifications, &err));

    // The added ID should be reported
    QContactId additionId(modifications.first().second.id());
    QVERIFY(additionId != QContactId());

    // The syncTarget should not be reported as updated by storeSyncContacts
    QVERIFY(!syncSpy.wait(1000));

    QList<QContactId> exportedIds;
    QList<QContact> syncContacts;
    QDateTime updatedSyncTime;
    QVERIFY(cme->fetchSyncContacts("sync-test", initialTime, exportedIds, &syncContacts, 0, 0, &updatedSyncTime, &err));
    QCOMPARE(syncContacts.count(), 1);
    QCOMPARE(syncContacts.at(0).id(), additionId);

    stc = m_cm->contact(additionId);

    // Verify that the contact properties are as we expect
    QContactName n2 = stc.detail<QContactName>();
    QCOMPARE(n2.prefix(), n.prefix());
    QCOMPARE(n2.firstName(), n.firstName());
    QCOMPARE(n2.middleName(), n.middleName());
    QCOMPARE(n2.lastName(), n.lastName());
    QCOMPARE(n2.suffix(), n.suffix());

    QCOMPARE(stc.details<QContactEmailAddress>().count(), 2);
    QSet<QString> emailAddresses;
    foreach (const QContactEmailAddress &e, stc.details<QContactEmailAddress>()) {
        QCOMPARE(e.value(QContactDetail__FieldModifiable).toBool(), true);
        emailAddresses.insert(e.emailAddress());
    }
    QCOMPARE(emailAddresses, (QStringList() << e.emailAddress() << e2.emailAddress()).toSet());

    QCOMPARE(stc.details<QContactHobby>().count(), 1);
    QCOMPARE(stc.details<QContactHobby>().at(0).hobby(), h.hobby());
    QCOMPARE(stc.details<QContactHobby>().at(0).value(QContactDetail__FieldModifiable).toBool(), true);

    QCOMPARE(stc.details<QContactGuid>().count(), 0);

    QCOMPARE(stc.relatedContacts(aggregatesRelationship, QContactRelationship::First).count(), 1);

    QContactId a1Id(stc.relatedContacts(aggregatesRelationship, QContactRelationship::First).at(0).id());
    QContact a = m_cm->contact(a1Id);

    n2 = a.detail<QContactName>();
    QCOMPARE(n2.prefix(), n.prefix());
    QCOMPARE(n2.firstName(), n.firstName());
    QCOMPARE(n2.middleName(), n.middleName());
    QCOMPARE(n2.lastName(), n.lastName());
    QCOMPARE(n2.suffix(), n.suffix());

    QCOMPARE(a.details<QContactEmailAddress>().count(), 2);
    emailAddresses.clear();
    foreach (const QContactEmailAddress &e, a.details<QContactEmailAddress>()) {
        emailAddresses.insert(e.emailAddress());
    }
    QCOMPARE(emailAddresses, (QStringList() << e.emailAddress() << e2.emailAddress()).toSet());

    QCOMPARE(a.details<QContactHobby>().count(), 1);
    QCOMPARE(a.details<QContactHobby>().at(0).hobby(), h.hobby());

    QCOMPARE(a.details<QContactGuid>().count(), 0);

    // Fetch the partial aggregate for this contact
    syncContacts.clear();
    updatedSyncTime = QDateTime();
    QVERIFY(cme->fetchSyncContacts("sync-test", initialTime, exportedIds, &syncContacts, 0, 0, &updatedSyncTime, &err));
    QCOMPARE(syncContacts.count(), 1);

    QContact pa = syncContacts.at(0);
    QCOMPARE(pa.id(), stc.id());
    QCOMPARE(pa.details<QContactEmailAddress>().count(), 2);

    // Effect some changes to the partial aggregate
    QContact mpa(pa);

    n.setPrefix("Doctor");
    n.setFirstName("Alberto");

    n2 = mpa.detail<QContactName>();
    n2.setPrefix(n.prefix());
    n2.setFirstName(n.firstName());
    mpa.saveDetail(&n2);

    QContactNickname nn;
    nn.setNickname("Smartypants");
    nn.setValue(QContactDetail__FieldModifiable, true);
    mpa.saveDetail(&nn);

    QContactEmailAddress e3;
    e3.setEmailAddress("smartypants@example.org");
    e3.setValue(QContactDetail__FieldModifiable, true);
    mpa.saveDetail(&e3);

    QContactEmailAddress e4 = mpa.details<QContactEmailAddress>().at(0);
    QContactEmailAddress e5 = mpa.details<QContactEmailAddress>().at(1);
    if (e4.emailAddress() != e.emailAddress()) {
        e4 = mpa.details<QContactEmailAddress>().at(1);
        e5 = mpa.details<QContactEmailAddress>().at(0);
    }

    e4.setEmailAddress("alberto.einstein@example.org");
    e4.setValue(QContactDetail__FieldModifiable, true);
    mpa.saveDetail(&e4);

    mpa.removeDetail(&e5);

    modifications.clear();
    modifications.append(qMakePair(pa, mpa));
    QVERIFY(cme->storeSyncContacts("sync-test", policy, &modifications, &err));

    // The syncTarget should not be reported as updated by storeSyncContacts
    QVERIFY(!syncSpy.wait(1000));

    // Verify that the expected changes occurred
    stc = m_cm->contact(retrievalId(stc));
    QCOMPARE(stc.relatedContacts(aggregatesRelationship, QContactRelationship::First).count(), 1);
    QCOMPARE(stc.relatedContacts(aggregatesRelationship, QContactRelationship::First).at(0).id(), a.id());

    n2 = stc.detail<QContactName>();
    QCOMPARE(n2.prefix(), n.prefix());
    QCOMPARE(n2.firstName(), n.firstName());
    QCOMPARE(n2.middleName(), n.middleName());
    QCOMPARE(n2.lastName(), n.lastName());
    QCOMPARE(n2.suffix(), n.suffix());

    QCOMPARE(stc.details<QContactEmailAddress>().count(), 2);
    emailAddresses.clear();
    foreach (const QContactEmailAddress &e, stc.details<QContactEmailAddress>()) {
        emailAddresses.insert(e.emailAddress());
    }
    QCOMPARE(emailAddresses, (QStringList() << e3.emailAddress() << e4.emailAddress()).toSet());

    QCOMPARE(stc.details<QContactHobby>().count(), 1);
    QCOMPARE(stc.details<QContactHobby>().at(0).hobby(), h.hobby());

    QCOMPARE(stc.details<QContactNickname>().count(), 1);
    QCOMPARE(stc.details<QContactNickname>().at(0).nickname(), nn.nickname());

    a = m_cm->contact(a1Id);

    n2 = a.detail<QContactName>();
    QCOMPARE(n2.prefix(), n.prefix());
    QCOMPARE(n2.firstName(), n.firstName());
    QCOMPARE(n2.middleName(), n.middleName());
    QCOMPARE(n2.lastName(), n.lastName());
    QCOMPARE(n2.suffix(), n.suffix());

    QCOMPARE(a.details<QContactEmailAddress>().count(), 2);
    emailAddresses.clear();
    foreach (const QContactEmailAddress &e, a.details<QContactEmailAddress>()) {
        emailAddresses.insert(e.emailAddress());
    }
    QCOMPARE(emailAddresses, (QStringList() << e3.emailAddress() << e4.emailAddress()).toSet());

    QCOMPARE(a.details<QContactHobby>().count(), 1);
    QCOMPARE(a.details<QContactHobby>().at(0).hobby(), h.hobby());

    QCOMPARE(a.details<QContactNickname>().count(), 1);
    QCOMPARE(a.details<QContactNickname>().at(0).nickname(), nn.nickname());

    // Link a local constituent to the sync-test contact
    QContact lc;

    lc.saveDetail(&n);

    QContactEmailAddress e6;
    e6.setEmailAddress("aeinstein1879@example.org");
    lc.saveDetail(&e6);

    QContactTag t;
    t.setContexts(QContactDetail::ContextWork);
    t.setTag("Physicist");
    lc.saveDetail(&t);

    QContactPhoneNumber pn;
    pn.setNumber("555-PHYSICS");
    pn.setSubTypes(QList<int>() << QContactPhoneNumber::SubTypeMobile);
    lc.saveDetail(&pn);

    QVERIFY(m_cm->saveContact(&lc));

    QTRY_COMPARE(syncSpy.count(), 1);
    QVariantList signalArgs(syncSpy.takeFirst());
    syncSpy.clear();
    QCOMPARE(syncSpy.count(), 0);
    QCOMPARE(signalArgs.count(), 1);
    QStringList changedSyncTargets(signalArgs.first().value<QStringList>());
    QCOMPARE(changedSyncTargets.count(), 1);
    QCOMPARE(changedSyncTargets.at(0), QString::fromLatin1("sync-test"));

    lc = m_cm->contact(retrievalId(lc));

    n2 = lc.detail<QContactName>();
    QCOMPARE(n2.prefix(), n.prefix());
    QCOMPARE(n2.firstName(), n.firstName());
    QCOMPARE(n2.middleName(), n.middleName());
    QCOMPARE(n2.lastName(), n.lastName());
    QCOMPARE(n2.suffix(), n.suffix());

    QCOMPARE(lc.details<QContactEmailAddress>().count(), 1);
    QCOMPARE(lc.details<QContactEmailAddress>().at(0).emailAddress(), e6.emailAddress());

    QCOMPARE(lc.details<QContactTag>().count(), 1);
    QCOMPARE(lc.details<QContactTag>().at(0).tag(), t.tag());
    QCOMPARE(lc.details<QContactTag>().at(0).contexts(), t.contexts());

    QCOMPARE(lc.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(lc.details<QContactPhoneNumber>().at(0).number(), pn.number());
    QCOMPARE(lc.details<QContactPhoneNumber>().at(0).subTypes(), pn.subTypes());

    QCOMPARE(lc.details<QContactGuid>().count(), 1);

    QCOMPARE(lc.relatedContacts(aggregatesRelationship, QContactRelationship::First).count(), 1);
    QCOMPARE(lc.relatedContacts(aggregatesRelationship, QContactRelationship::First).at(0).id(), a.id());

    a = m_cm->contact(a1Id);

    n2 = a.detail<QContactName>();
    QCOMPARE(n2.prefix(), n.prefix());
    QCOMPARE(n2.firstName(), n.firstName());
    QCOMPARE(n2.middleName(), n.middleName());
    QCOMPARE(n2.lastName(), n.lastName());
    QCOMPARE(n2.suffix(), n.suffix());

    QCOMPARE(a.details<QContactEmailAddress>().count(), 3);
    emailAddresses.clear();
    foreach (const QContactEmailAddress &e, a.details<QContactEmailAddress>()) {
        emailAddresses.insert(e.emailAddress());
    }
    QCOMPARE(emailAddresses, (QStringList() << e3.emailAddress() << e4.emailAddress() << e6.emailAddress()).toSet());

    QCOMPARE(a.details<QContactHobby>().count(), 1);
    QCOMPARE(a.details<QContactHobby>().at(0).hobby(), h.hobby());

    QCOMPARE(a.details<QContactNickname>().count(), 1);
    QCOMPARE(a.details<QContactNickname>().at(0).nickname(), nn.nickname());

    QCOMPARE(a.details<QContactTag>().count(), 1);
    QCOMPARE(a.details<QContactTag>().at(0).tag(), t.tag());
    QCOMPARE(a.details<QContactTag>().at(0).contexts(), t.contexts());

    QCOMPARE(a.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(a.details<QContactPhoneNumber>().at(0).number(), pn.number());
    QCOMPARE(a.details<QContactPhoneNumber>().at(0).subTypes(), pn.subTypes());

    QCOMPARE(a.details<QContactGuid>().count(), 0);

    QCOMPARE(a.relatedContacts(aggregatesRelationship, QContactRelationship::Second).count(), 2);

    syncContacts.clear();
    updatedSyncTime = QDateTime();
    QVERIFY(cme->fetchSyncContacts("sync-test", initialTime, exportedIds, &syncContacts, 0, 0, &updatedSyncTime, &err));
    QCOMPARE(syncContacts.count(), 1);

    pa = syncContacts.at(0);
    QCOMPARE(pa.id(), stc.id());
    QCOMPARE(pa.details<QContactEmailAddress>().count(), 3);

    // The local's GUID is not present
    QCOMPARE(pa.details<QContactGuid>().count(), 0);

    // Make changes that will affect both constituents
    mpa = QContact(pa);

    n.setPrefix("Herr");
    n.setFirstName("Albert");
    n.setMiddleName("J.");

    n2 = mpa.detail<QContactName>();
    n2.setPrefix(n.prefix());
    n2.setFirstName(n.firstName());
    n2.setMiddleName(n.middleName());
    mpa.saveDetail(&n2);

    nn = mpa.detail<QContactNickname>();
    nn.setNickname("Cleverclogs");
    nn.setValue(QContactDetail__FieldModifiable, true);
    mpa.saveDetail(&nn);

    QContactEmailAddress e7, e8;
    foreach (const QContactEmailAddress &e, mpa.details<QContactEmailAddress>()) {
        if (e.emailAddress() == e4.emailAddress()) {
            e7 = e;
        } else if (e.emailAddress() == e6.emailAddress()) {
            e8 = e;
        }
    }

    e7.setEmailAddress("albert.j.einstein@example.org");
    e7.setValue(QContactDetail__FieldModifiable, true);
    mpa.saveDetail(&e7);

    e8.setEmailAddress("ajeinstein1879@example.org");
    e8.setValue(QContactDetail__FieldModifiable, true);
    mpa.saveDetail(&e8);

    QContactHobby h2 = mpa.detail<QContactHobby>();
    mpa.removeDetail(&h2);

    QContactGuid guid;
    guid.setGuid("I am a unique snowflake");
    mpa.saveDetail(&guid);

    // Include changes to context and subtype fields
    t = mpa.detail<QContactTag>();
    t.setContexts(QContactDetail::ContextOther);
    t.setValue(QContactDetail__FieldModifiable, true);
    mpa.saveDetail(&t);

    pn = mpa.detail<QContactPhoneNumber>();
    pn.setSubTypes(QList<int>() << QContactPhoneNumber::SubTypePager);
    pn.setValue(QContactDetail__FieldModifiable, true);
    mpa.saveDetail(&pn);

    modifications.clear();
    modifications.append(qMakePair(pa, mpa));
    QVERIFY(cme->storeSyncContacts("sync-test", policy, &modifications, &err));

    // Verify that the expected changes occurred
    stc = m_cm->contact(retrievalId(stc));
    QCOMPARE(stc.relatedContacts(aggregatesRelationship, QContactRelationship::First).count(), 1);
    QCOMPARE(stc.relatedContacts(aggregatesRelationship, QContactRelationship::First).at(0).id(), a.id());

    n2 = stc.detail<QContactName>();
    QCOMPARE(n2.prefix(), n.prefix());
    QCOMPARE(n2.firstName(), n.firstName());
    QCOMPARE(n2.middleName(), n.middleName());
    QCOMPARE(n2.lastName(), n.lastName());
    QCOMPARE(n2.suffix(), n.suffix());

    QCOMPARE(stc.details<QContactEmailAddress>().count(), 2);
    emailAddresses.clear();
    foreach (const QContactEmailAddress &e, stc.details<QContactEmailAddress>()) {
        emailAddresses.insert(e.emailAddress());
    }
    QCOMPARE(emailAddresses, (QStringList() << e3.emailAddress() << e7.emailAddress()).toSet());

    QCOMPARE(stc.details<QContactHobby>().count(), 0);

    QCOMPARE(stc.details<QContactNickname>().count(), 1);
    QCOMPARE(stc.details<QContactNickname>().at(0).nickname(), nn.nickname());

    QCOMPARE(stc.details<QContactGuid>().count(), 1);
    QCOMPARE(stc.details<QContactGuid>().at(0).guid(), guid.guid());

    lc = m_cm->contact(retrievalId(lc));
    QCOMPARE(lc.relatedContacts(aggregatesRelationship, QContactRelationship::First).count(), 1);
    QCOMPARE(lc.relatedContacts(aggregatesRelationship, QContactRelationship::First).at(0).id(), a.id());

    n2 = lc.detail<QContactName>();
    QCOMPARE(n2.prefix(), n.prefix());
    QCOMPARE(n2.firstName(), n.firstName());
    QCOMPARE(n2.middleName(), n.middleName());
    QCOMPARE(n2.lastName(), n.lastName());
    QCOMPARE(n2.suffix(), n.suffix());

    QCOMPARE(lc.details<QContactEmailAddress>().count(), 1);
    QCOMPARE(lc.details<QContactEmailAddress>().at(0).emailAddress(), e8.emailAddress());

    QCOMPARE(lc.details<QContactTag>().count(), 1);
    QCOMPARE(lc.details<QContactTag>().at(0).tag(), t.tag());
    QCOMPARE(lc.details<QContactTag>().at(0).contexts(), t.contexts());

    QCOMPARE(lc.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(lc.details<QContactPhoneNumber>().at(0).number(), pn.number());
    QCOMPARE(lc.details<QContactPhoneNumber>().at(0).subTypes(), pn.subTypes());

    QCOMPARE(lc.details<QContactGuid>().count(), 1);
    QVERIFY(lc.details<QContactGuid>().at(0).guid() != guid.guid());

    a = m_cm->contact(a1Id);

    n2 = a.detail<QContactName>();
    QCOMPARE(n2.prefix(), n.prefix());
    QCOMPARE(n2.firstName(), n.firstName());
    QCOMPARE(n2.middleName(), n.middleName());
    QCOMPARE(n2.lastName(), n.lastName());
    QCOMPARE(n2.suffix(), n.suffix());

    QCOMPARE(a.details<QContactEmailAddress>().count(), 3);
    emailAddresses.clear();
    foreach (const QContactEmailAddress &e, a.details<QContactEmailAddress>()) {
        emailAddresses.insert(e.emailAddress());
    }
    QCOMPARE(emailAddresses, (QStringList() << e3.emailAddress() << e7.emailAddress() << e8.emailAddress()).toSet());

    QCOMPARE(a.details<QContactHobby>().count(), 0);

    QCOMPARE(a.details<QContactNickname>().count(), 1);
    QCOMPARE(a.details<QContactNickname>().at(0).nickname(), nn.nickname());

    QCOMPARE(a.details<QContactTag>().count(), 1);
    QCOMPARE(a.details<QContactTag>().at(0).tag(), t.tag());
    QCOMPARE(a.details<QContactTag>().at(0).contexts(), t.contexts());

    QCOMPARE(a.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(a.details<QContactPhoneNumber>().at(0).number(), pn.number());
    QCOMPARE(a.details<QContactPhoneNumber>().at(0).subTypes(), pn.subTypes());

    QCOMPARE(a.details<QContactGuid>().count(), 0);

    // The sync target partial agregate should contain the sync target GUID
    syncContacts.clear();
    updatedSyncTime = QDateTime();
    QVERIFY(cme->fetchSyncContacts("sync-test", initialTime, exportedIds, &syncContacts, 0, 0, &updatedSyncTime, &err));
    QCOMPARE(syncContacts.count(), 1);

    pa = syncContacts.at(0);
    QCOMPARE(pa.id(), stc.id());
    QCOMPARE(pa.details<QContactGuid>().count(), 1);
    QCOMPARE(pa.detail<QContactGuid>().guid(), stc.detail<QContactGuid>().guid());

    // Link a constituent from a different sync target
    QContact dstc;

    QContactSyncTarget dstTarget;
    dstTarget.setSyncTarget("different-sync-target");
    dstc.saveDetail(&dstTarget);

    dstc.saveDetail(&n);

    QVERIFY(m_cm->saveContact(&dstc));

    QTRY_COMPARE(syncSpy.count(), 1);
    signalArgs = syncSpy.takeFirst();
    syncSpy.clear();
    QCOMPARE(syncSpy.count(), 0);
    QCOMPARE(signalArgs.count(), 1);
    changedSyncTargets = signalArgs.first().value<QStringList>();
    QCOMPARE(changedSyncTargets.count(), 1);
    QCOMPARE(changedSyncTargets.at(0), QString::fromLatin1("different-sync-target"));

    dstc = m_cm->contact(retrievalId(dstc));

    n2 = dstc.detail<QContactName>();
    QCOMPARE(n2.prefix(), n.prefix());
    QCOMPARE(n2.firstName(), n.firstName());
    QCOMPARE(n2.middleName(), n.middleName());
    QCOMPARE(n2.lastName(), n.lastName());
    QCOMPARE(n2.suffix(), n.suffix());

    QCOMPARE(dstc.relatedContacts(aggregatesRelationship, QContactRelationship::First).count(), 1);
    QCOMPARE(dstc.relatedContacts(aggregatesRelationship, QContactRelationship::First).at(0).id(), a.id());

    // Modify the name again
    syncContacts.clear();
    updatedSyncTime = QDateTime();
    QVERIFY(cme->fetchSyncContacts("sync-test", initialTime, exportedIds, &syncContacts, 0, 0, &updatedSyncTime, &err));
    QCOMPARE(syncContacts.count(), 1);

    pa = syncContacts.at(0);
    mpa = QContact(pa);

    n.setMiddleName("Q.");
    mpa.saveDetail(&n);

    modifications.clear();
    modifications.append(qMakePair(pa, mpa));
    QVERIFY(cme->storeSyncContacts("sync-test", policy, &modifications, &err));

    // A sync-target that was not the subject of this update should be reported as having changed
    QTRY_COMPARE(syncSpy.count(), 1);
    signalArgs = syncSpy.takeFirst();
    QCOMPARE(syncSpy.count(), 0);
    QCOMPARE(signalArgs.count(), 1);
    changedSyncTargets = signalArgs.first().value<QStringList>();
    QCOMPARE(changedSyncTargets.count(), 1);
    QCOMPARE(changedSyncTargets.at(0), QString::fromLatin1("different-sync-target"));

    // Verify that the name changes occurred for the affected constituents, but not the unrelated one
    stc = m_cm->contact(retrievalId(stc));
    QCOMPARE(stc.detail<QContactName>().middleName(), n.middleName());

    lc = m_cm->contact(retrievalId(lc));
    QCOMPARE(lc.detail<QContactName>().middleName(), n.middleName());

    a = m_cm->contact(a1Id);
    QCOMPARE(a.detail<QContactName>().middleName(), n.middleName());

    dstc = m_cm->contact(retrievalId(dstc));
    QCOMPARE(dstc.detail<QContactName>().middleName(), QString::fromLatin1("J."));

    // Test conflict resolution - we currently support only PreserveLocalChanges
    syncContacts.clear();
    updatedSyncTime = QDateTime();
    QVERIFY(cme->fetchSyncContacts("sync-test", initialTime, exportedIds, &syncContacts, 0, 0, &updatedSyncTime, &err));
    QCOMPARE(syncContacts.count(), 1);
    pa = syncContacts.at(0);
    mpa = QContact(pa);

    // Composited details
    // Change one field in only the PA, and change another in both
    n = a.detail<QContactName>();
    n.setSuffix("Sr.");
    a.saveDetail(&n);

    // Add conflicting composited details
    QContactGender g = a.detail<QContactGender>();
    g.setGender(QContactGender::GenderMale);
    a.saveDetail(&g);

    // Identified details
    // Modify in both
    t = a.detail<QContactTag>();
    t.setTag("Deceased");
    a.saveDetail(&t);

    // Remove from local device, modify in sync
    foreach (const QContactEmailAddress &e, a.details<QContactEmailAddress>()) {
        if (e.emailAddress() == e8.emailAddress()) {
            e8 = e;
            a.removeDetail(&e8);
            break;
        }
    }

    // Remove from sync, modify in local device
    pn = a.detail<QContactPhoneNumber>();
    pn.setNumber("555-PSYCHIC");
    a.saveDetail(&pn);

    // Store the changes to the local device, stored via the aggregate
    QVERIFY(m_cm->saveContact(&a));

    lc = m_cm->contact(retrievalId(lc));

    // Verify the changes
    n2 = lc.detail<QContactName>();
    QCOMPARE(n2.prefix(), n.prefix());
    QCOMPARE(n2.firstName(), n.firstName());
    QCOMPARE(n2.middleName(), n.middleName());
    QCOMPARE(n2.lastName(), n.lastName());
    QCOMPARE(n2.suffix(), n.suffix());

    QCOMPARE(lc.details<QContactGender>().count(), 1);
    QCOMPARE(lc.detail<QContactGender>().gender(), QContactGender::GenderMale);

    QCOMPARE(lc.details<QContactEmailAddress>().count(), 0);

    QCOMPARE(lc.details<QContactTag>().count(), 1);
    QCOMPARE(lc.detail<QContactTag>().tag(), t.tag());

    QCOMPARE(lc.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(lc.detail<QContactPhoneNumber>().number(), pn.number());

    a = m_cm->contact(retrievalId(a));

    n2 = a.detail<QContactName>();
    QCOMPARE(n2.prefix(), n.prefix());
    QCOMPARE(n2.firstName(), n.firstName());
    QCOMPARE(n2.middleName(), n.middleName());
    QCOMPARE(n2.lastName(), n.lastName());
    QCOMPARE(n2.suffix(), n.suffix());

    QCOMPARE(a.details<QContactGender>().count(), 1);
    QCOMPARE(a.detail<QContactGender>().gender(), QContactGender::GenderMale);

    QCOMPARE(a.details<QContactEmailAddress>().count(), 2);
    emailAddresses.clear();
    foreach (const QContactEmailAddress &e, a.details<QContactEmailAddress>()) {
        emailAddresses.insert(e.emailAddress());
    }
    QCOMPARE(emailAddresses, (QStringList() << e3.emailAddress() << e7.emailAddress()).toSet());

    QCOMPARE(a.details<QContactTag>().count(), 1);
    QCOMPARE(a.detail<QContactTag>().tag(), t.tag());

    QCOMPARE(a.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(a.detail<QContactPhoneNumber>().number(), pn.number());

    // Store the conflicting sync changes
    n = mpa.detail<QContactName>();
    n.setSuffix("Jr.");
    n.setMiddleName("\"Crusher\"");
    mpa.saveDetail(&n);

    g = mpa.detail<QContactGender>();
    g.setGender(QContactGender::GenderFemale);
    mpa.saveDetail(&g);

    QContactTag t2 = mpa.detail<QContactTag>();
    t2.setTag("Non-operational");
    t2.setValue(QContactDetail__FieldModifiable, true);
    mpa.saveDetail(&t2);

    QContactEmailAddress e9;
    foreach (const QContactEmailAddress &e, mpa.details<QContactEmailAddress>()) {
        if (e.emailAddress() == e8.emailAddress()) {
            e9 = e;
            break;
        }
    }

    e9.setEmailAddress("modified@example.org");
    e9.setValue(QContactDetail__FieldModifiable, true);
    mpa.saveDetail(&e9);

    QContactPhoneNumber pn2 = mpa.detail<QContactPhoneNumber>();
    mpa.removeDetail(&pn2);

    modifications.clear();
    modifications.append(qMakePair(pa, mpa));
    QVERIFY(cme->storeSyncContacts("sync-test", policy, &modifications, &err));

    // Verify that the expected changes occurred
    stc = m_cm->contact(retrievalId(stc));

    // The composited changes will have been applied to the sync-target contact
    n2 = stc.detail<QContactName>();
    QCOMPARE(n2.prefix(), n.prefix());
    QCOMPARE(n2.firstName(), n.firstName());
    QCOMPARE(n2.middleName(), n.middleName());
    QCOMPARE(n2.lastName(), n.lastName());
    QCOMPARE(n2.suffix(), n.suffix());

    QCOMPARE(stc.details<QContactGender>().count(), 1);
    QCOMPARE(stc.detail<QContactGender>().gender(), QContactGender::GenderFemale);

    // The aggregate will have combined changes
    a = m_cm->contact(retrievalId(a));

    n2 = a.detail<QContactName>();

    // The conflict will resolve in favor of the local change
    QCOMPARE(n2.suffix(), QString::fromLatin1("Sr."));

    // The unconflicting change will be applied
    QCOMPARE(n2.middleName(), QString::fromLatin1("\"Crusher\""));

    // Gender will resolve to the local change
    QCOMPARE(a.details<QContactGender>().count(), 1);
    QCOMPARE(a.detail<QContactGender>().gender(), QContactGender::GenderMale);

    // The conflicting edits will resolve in favor of the local change
    QCOMPARE(a.details<QContactTag>().count(), 1);
    QCOMPARE(a.detail<QContactTag>().tag(), t.tag());

    // The locally-removed, remotely-modified detail will be absent
    QCOMPARE(a.details<QContactEmailAddress>().count(), 2);
    emailAddresses.clear();
    foreach (const QContactEmailAddress &e, a.details<QContactEmailAddress>()) {
        emailAddresses.insert(e.emailAddress());
    }
    QCOMPARE(emailAddresses, (QStringList() << e3.emailAddress() << e7.emailAddress()).toSet());

    // The remotely-removed, locally-edited detail is still present
    QCOMPARE(a.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(a.detail<QContactPhoneNumber>().number(), pn.number());

    // Check that the partial aggregate matches the aggregate
    syncContacts.clear();
    updatedSyncTime = QDateTime();
    QVERIFY(cme->fetchSyncContacts("sync-test", initialTime, exportedIds, &syncContacts, 0, 0, &updatedSyncTime, &err));
    QCOMPARE(syncContacts.count(), 1);

    pa = syncContacts.at(0);

    n2 = pa.detail<QContactName>();
    QCOMPARE(n2.suffix(), QString::fromLatin1("Sr."));
    QCOMPARE(n2.middleName(), QString::fromLatin1("\"Crusher\""));

    QCOMPARE(pa.details<QContactGender>().count(), 1);
    QCOMPARE(pa.detail<QContactGender>().gender(), QContactGender::GenderMale);

    QCOMPARE(pa.details<QContactTag>().count(), 1);
    QCOMPARE(pa.detail<QContactTag>().tag(), t.tag());

    QCOMPARE(pa.details<QContactEmailAddress>().count(), 2);
    emailAddresses.clear();
    foreach (const QContactEmailAddress &e, pa.details<QContactEmailAddress>()) {
        emailAddresses.insert(e.emailAddress());
    }
    QCOMPARE(emailAddresses, (QStringList() << e3.emailAddress() << e7.emailAddress()).toSet());

    QCOMPARE(pa.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(pa.detail<QContactPhoneNumber>().number(), pn.number());

    QDateTime nextTime = QDateTime::currentDateTimeUtc();
    QTest::qWait(1);

    // Create another local contact that we can export
    QContact alc;

    QContactName n3;
    n3.setFirstName("Niels");
    n3.setLastName("Bohr");
    alc.saveDetail(&n3);

    QContactNote note;
    note.setNote("Quite tall");
    alc.saveDetail(&note);

    alc.saveDetail(&t);

    QVERIFY(m_cm->saveContact(&alc));
    alc = m_cm->contact(alc.id());

    QCOMPARE(alc.details<QContactGuid>().count(), 1);

    QList<QContact> addedContacts;

    syncContacts.clear();
    updatedSyncTime = QDateTime();
    QVERIFY(cme->fetchSyncContacts("sync-test", nextTime, exportedIds, &syncContacts, &addedContacts, 0, &updatedSyncTime, &err));
    QCOMPARE(addedContacts.count(), 1);

    pa = addedContacts.at(0);
    QCOMPARE(pa.id(), alc.id());

    // The GUID is not promoted into the the partial aggregate
    QCOMPARE(pa.details<QContactGuid>().count(), 0);

    // Make changes to this contact
    mpa = pa;

    QContactTag t3 = mpa.detail<QContactTag>();
    t3.setTag("Danish Physicist");
    mpa.saveDetail(&t3);

    QContactHobby h3;
    h3.setHobby("Football");
    mpa.saveDetail(&h3);

    modifications.clear();
    modifications.append(qMakePair(pa, mpa));
    QVERIFY(cme->storeSyncContacts("sync-test", policy, &modifications, &err));

    // The tag should have been modified in the original local contact
    alc = m_cm->contact(alc.id());

    QCOMPARE(alc.details<QContactTag>().count(), 1);
    QCOMPARE(alc.details<QContactTag>().at(0).tag(), t3.tag());

    QCOMPARE(alc.details<QContactNote>().count(), 1);
    QCOMPARE(alc.details<QContactNote>().at(0).note(), note.note());

    QCOMPARE(alc.details<QContactHobby>().count(), 0);

    // A new incidental contact should have been created to contain the hobby
    QCOMPARE(alc.relatedContacts(aggregatesRelationship, QContactRelationship::First).count(), 1);

    QContactId a2Id(alc.relatedContacts(aggregatesRelationship, QContactRelationship::First).at(0).id());
    QContact a2 = m_cm->contact(a2Id);
    QCOMPARE(a2.relatedContacts(aggregatesRelationship, QContactRelationship::Second).count(), 2);

    QContactId stId;
    if (a2.relatedContacts(aggregatesRelationship, QContactRelationship::Second).at(0).id() == alc.id()) {
        stId = a2.relatedContacts(aggregatesRelationship, QContactRelationship::Second).at(1).id();
    } else {
        stId = a2.relatedContacts(aggregatesRelationship, QContactRelationship::Second).at(0).id();
    }

    QContact stc2 = m_cm->contact(stId);

    n2 = stc2.detail<QContactName>();
    QCOMPARE(n2.prefix(), n3.prefix());
    QCOMPARE(n2.firstName(), n3.firstName());
    QCOMPARE(n2.middleName(), n3.middleName());
    QCOMPARE(n2.lastName(), n3.lastName());
    QCOMPARE(n2.suffix(), n3.suffix());

    QCOMPARE(stc2.details<QContactTag>().count(), 0);

    QCOMPARE(stc2.details<QContactNote>().count(), 0);

    QCOMPARE(stc2.details<QContactHobby>().count(), 1);
    QCOMPARE(stc2.details<QContactHobby>().at(0).hobby(), h3.hobby());

    // Both contacts should now be reported by their sync-target IDs
    syncContacts.clear();
    updatedSyncTime = QDateTime();
    QVERIFY(cme->fetchSyncContacts("sync-test", initialTime, exportedIds, &syncContacts, 0, 0, &updatedSyncTime, &err));
    QCOMPARE(syncContacts.count(), 2);
    QCOMPARE((QList<QContactId>() << syncContacts.at(0).id() << syncContacts.at(1).id()).toSet(), (QList<QContactId>() << stc.id() << alc.id()).toSet());

    QCOMPARE(alc.details<QContactGuid>().count(), 1);

    // Make a modification to the incidental contact - it should remain incidental
    if (syncContacts.at(0).id() == alc.id()) {
        pa = syncContacts.at(0);
    } else {
        pa = syncContacts.at(1);
    }
    mpa = pa;

    note = mpa.detail<QContactNote>();
    note.setNote("Quite tall indeed");
    mpa.saveDetail(&note);

    h3 = mpa.detail<QContactHobby>();
    h3.setHobby("Ventriloquism");
    mpa.saveDetail(&h3);

    QContactHobby h4;
    h4.setHobby("Philately");
    mpa.saveDetail(&h4);

    QContactGuid stGuid;
    stGuid.setGuid("I am also a unique snowflake");
    mpa.saveDetail(&stGuid);

    modifications.clear();
    modifications.append(qMakePair(pa, mpa));
    QVERIFY(cme->storeSyncContacts("sync-test", policy, &modifications, &err));

    // The changes should be made in their respective contacts
    alc = m_cm->contact(alc.id());

    QCOMPARE(alc.details<QContactTag>().count(), 1);
    QCOMPARE(alc.details<QContactTag>().at(0).tag(), t3.tag());

    QCOMPARE(alc.details<QContactNote>().count(), 1);
    QCOMPARE(alc.details<QContactNote>().at(0).note(), note.note());

    QCOMPARE(alc.details<QContactHobby>().count(), 0);

    stc2 = m_cm->contact(stc2.id());

    QCOMPARE(stc2.details<QContactTag>().count(), 0);

    QCOMPARE(stc2.details<QContactNote>().count(), 0);

    QCOMPARE(stc2.details<QContactHobby>().count(), 2);
    QCOMPARE((QSet<QString>() << stc2.details<QContactHobby>().at(0).hobby() << stc2.details<QContactHobby>().at(1).hobby()), (QSet<QString>() << h3.hobby() << h4.hobby()));

    QCOMPARE(stc2.details<QContactGuid>().count(), 1);
    QCOMPARE(stc2.details<QContactGuid>().at(0).guid(), stGuid.guid());

    syncContacts.clear();
    updatedSyncTime = QDateTime();
    QVERIFY(cme->fetchSyncContacts("sync-test", initialTime, exportedIds, &syncContacts, 0, 0, &updatedSyncTime, &err));
    QCOMPARE(syncContacts.count(), 2);
    QCOMPARE((QList<QContactId>() << syncContacts.at(0).id() << syncContacts.at(1).id()).toSet(), (QList<QContactId>() << stc.id() << alc.id()).toSet());

    if (syncContacts.at(0).id() == alc.id()) {
        pa = syncContacts.at(0);
    } else {
        pa = syncContacts.at(1);
    }

    // Verify that the GUID from the sync-target contact is returned
    QCOMPARE(pa.details<QContactGuid>().count(), 1);
    QCOMPARE(pa.details<QContactGuid>().at(0).guid(), stGuid.guid());

    // Test changes to export contacts
    QList<QContactId> syncExportedIds;

    syncContacts.clear();
    addedContacts.clear();
    QVERIFY(cme->fetchSyncContacts("export", initialTime, syncExportedIds, &syncContacts, &addedContacts, 0, &updatedSyncTime, &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(syncContacts.count(), 0);
    QCOMPARE(addedContacts.count(), 2);

    // The export set contains aggregate IDs
    QCOMPARE((QList<QContactId>() << addedContacts.at(0).id() << addedContacts.at(1).id()).toSet(), (QList<QContactId>() << a.id() << a2.id()).toSet());

    syncExportedIds.append(a.id());
    syncExportedIds.append(a2.id());

    pa = addedContacts.at(0);
    if (pa.id() != a2.id()) {
        pa = addedContacts.at(1);
    }
    QCOMPARE(pa.id(), a2.id());

    QCOMPARE(pa.details<QContactTag>().count(), 1);
    QCOMPARE(pa.details<QContactTag>().at(0).tag(), t3.tag());

    QCOMPARE(pa.details<QContactHobby>().count(), 2);
    QCOMPARE((QSet<QString>() << pa.details<QContactHobby>().at(0).hobby() << pa.details<QContactHobby>().at(1).hobby()), (QSet<QString>() << h3.hobby() << h4.hobby()));

    QDateTime modificationTime = QDateTime::currentDateTimeUtc();
    QTest::qWait(1);

    // Make modifications to the original details from the export contacts
    mpa = pa;

    t3 = mpa.detail<QContactTag>();
    t3.setTag("Danelandic Physicist");
    mpa.saveDetail(&t3);

    h3 = mpa.details<QContactHobby>().at(0);
    if (h3.hobby() == h4.hobby()) {
        h3 = mpa.details<QContactHobby>().at(1);
    }
    h3.setHobby("Gurning");
    mpa.saveDetail(&h3);

    QContactNote note2;
    note2.setNote("Quietly toils");
    mpa.saveDetail(&note2);

    modifications.clear();
    modifications.append(qMakePair(pa, mpa));
    QVERIFY(cme->storeSyncContacts("export", policy, &modifications, &err));

    // The changes should be applied to the origin details, and additions go to the local constituent
    stc2 = m_cm->contact(stId);

    QCOMPARE(stc2.details<QContactTag>().count(), 0);

    QCOMPARE(stc2.details<QContactHobby>().count(), 2);
    QCOMPARE((QSet<QString>() << stc2.details<QContactHobby>().at(0).hobby() << stc2.details<QContactHobby>().at(1).hobby()), (QSet<QString>() << h3.hobby() << h4.hobby()));

    QCOMPARE(stc2.details<QContactNote>().count(), 0);

    alc = m_cm->contact(alc.id());

    QCOMPARE(alc.details<QContactTag>().count(), 1);
    QCOMPARE(alc.details<QContactTag>().at(0).tag(), t3.tag());

    QCOMPARE(alc.details<QContactHobby>().count(), 0);

    QCOMPARE(alc.details<QContactNote>().count(), 2);
    QCOMPARE((QSet<QString>() << alc.details<QContactNote>().at(0).note() << alc.details<QContactNote>().at(1).note()), (QSet<QString>() << note.note() << note2.note()));

    syncContacts.clear();
    addedContacts.clear();
    QVERIFY(cme->fetchSyncContacts("export", modificationTime, syncExportedIds, &syncContacts, &addedContacts, 0, &updatedSyncTime, &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(syncContacts.count(), 1);
    QCOMPARE(addedContacts.count(), 0);

    pa = syncContacts.at(0);
    QCOMPARE(pa.id(), a2.id());

    QCOMPARE(pa.details<QContactTag>().count(), 1);
    QCOMPARE(pa.details<QContactTag>().at(0).tag(), t3.tag());

    QCOMPARE(pa.details<QContactHobby>().count(), 2);
    QCOMPARE((QSet<QString>() << pa.details<QContactHobby>().at(0).hobby() << pa.details<QContactHobby>().at(1).hobby()), (QSet<QString>() << h3.hobby() << h4.hobby()));

    QCOMPARE(pa.details<QContactNote>().count(), 2);
    QCOMPARE((QSet<QString>() << pa.details<QContactNote>().at(0).note() << pa.details<QContactNote>().at(1).note()), (QSet<QString>() << note.note() << note2.note()));

    // Create a new sync-target contact to be exported
    QContact adstc;
    adstc.saveDetail(&dstTarget);

    QContactName n4;
    n4.setFirstName("Enrico");
    n4.setLastName("Fermi");
    adstc.saveDetail(&n4);

    QContactTag t4;
    t4.setTag("Italian Physicist");
    adstc.saveDetail(&t4);

    QVERIFY(m_cm->saveContact(&adstc));
    adstc = m_cm->contact(adstc.id());
    QCOMPARE(adstc.relatedContacts(aggregatesRelationship, QContactRelationship::First).count(), 1);

    QContact a3 = m_cm->contact(adstc.relatedContacts(aggregatesRelationship, QContactRelationship::First).at(0).id());
    QCOMPARE(a3.relatedContacts(aggregatesRelationship, QContactRelationship::Second).count(), 1);

    syncContacts.clear();
    addedContacts.clear();
    QVERIFY(cme->fetchSyncContacts("export", modificationTime, syncExportedIds, &syncContacts, &addedContacts, 0, &updatedSyncTime, &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(syncContacts.count(), 1);
    QCOMPARE(addedContacts.count(), 1);

    pa = addedContacts.at(0);
    QCOMPARE(pa.id(), a3.id());

    QCOMPARE(pa.details<QContactTag>().count(), 1);
    QCOMPARE(pa.details<QContactTag>().at(0).tag(), t4.tag());

    QCOMPARE(pa.details<QContactHobby>().count(), 0);

    // Make modifications to this contact via the export DB
    mpa = pa;

    n4 = mpa.detail<QContactName>();
    n4.setPrefix("Dr");
    mpa.saveDetail(&n4);

    t4 = mpa.detail<QContactTag>();
    t4.setTag("Italian-american Physicist");
    mpa.saveDetail(&t4);

    QContactHobby h5;
    h5.setHobby("Tennis");
    mpa.saveDetail(&h5);

    modifications.clear();
    modifications.append(qMakePair(pa, mpa));
    QVERIFY(cme->storeSyncContacts("export", policy, &modifications, &err));

    // The aggregate should be updated
    a3 = m_cm->contact(a3.id());

    n2 = a3.detail<QContactName>();
    QCOMPARE(n2.prefix(), n4.prefix());
    QCOMPARE(n2.firstName(), n4.firstName());
    QCOMPARE(n2.middleName(), n4.middleName());
    QCOMPARE(n2.lastName(), n4.lastName());
    QCOMPARE(n2.suffix(), n4.suffix());

    QCOMPARE(a3.details<QContactTag>().count(), 1);
    QCOMPARE(a3.details<QContactTag>().at(0).tag(), t4.tag());

    QCOMPARE(a3.details<QContactHobby>().count(), 1);
    QCOMPARE(a3.details<QContactHobby>().at(0).hobby(), h5.hobby());

    // A local constituent has been created to contain the new detail
    QCOMPARE(a3.relatedContacts(aggregatesRelationship, QContactRelationship::Second).count(), 2);

    QContact nlc;
    if (a3.relatedContacts(aggregatesRelationship, QContactRelationship::Second).at(0).id() == adstc.id()) {
        nlc = m_cm->contact(a3.relatedContacts(aggregatesRelationship, QContactRelationship::Second).at(1).id());
    } else {
        nlc = m_cm->contact(a3.relatedContacts(aggregatesRelationship, QContactRelationship::Second).at(0).id());
    }

    QCOMPARE(nlc.relatedContacts(aggregatesRelationship, QContactRelationship::First).count(), 1);
    QCOMPARE(nlc.relatedContacts(aggregatesRelationship, QContactRelationship::First).at(0).id(), a3.id());

    n2 = nlc.detail<QContactName>();
    QCOMPARE(n2.prefix(), n4.prefix());
    QCOMPARE(n2.firstName(), n4.firstName());
    QCOMPARE(n2.middleName(), n4.middleName());
    QCOMPARE(n2.lastName(), n4.lastName());
    QCOMPARE(n2.suffix(), n4.suffix());

    QCOMPARE(nlc.details<QContactTag>().count(), 0);

    QCOMPARE(nlc.details<QContactHobby>().count(), 1);
    QCOMPARE(nlc.details<QContactHobby>().at(0).hobby(), h5.hobby());

    // The sync target constituent should be updated, although the name remains unchanged
    adstc = m_cm->contact(adstc.id());

    n2 = adstc.detail<QContactName>();
    QCOMPARE(n2.prefix(), QString());
    QCOMPARE(n2.firstName(), n4.firstName());
    QCOMPARE(n2.lastName(), n4.lastName());

    QCOMPARE(adstc.details<QContactTag>().count(), 1);
    QCOMPARE(adstc.details<QContactTag>().at(0).tag(), t4.tag());

    QCOMPARE(adstc.details<QContactHobby>().count(), 0);

    // Add a contact in the export DB and sync back to the primary
    QContact xc;

    QContactName n6;
    n6.setFirstName("Werner");
    n6.setLastName("Heisenberg");
    xc.saveDetail(&n6);

    QContactTag t5;
    t5.setTag("German Physicist");
    xc.saveDetail(&t5);

    modifications.clear();
    modifications.append(qMakePair(QContact(), xc));
    QVERIFY(cme->storeSyncContacts("export", policy, &modifications, &err));

    syncContacts.clear();
    addedContacts.clear();
    QVERIFY(cme->fetchSyncContacts("export", modificationTime, syncExportedIds, &syncContacts, &addedContacts, 0, &updatedSyncTime, &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(syncContacts.count(), 1);
    QCOMPARE(addedContacts.count(), 2);

    pa = addedContacts.at(0);
    if (pa.id() == a3.id()) {
        pa = addedContacts.at(1);
    }

    n2 = pa.detail<QContactName>();
    QCOMPARE(n2.prefix(), n6.prefix());
    QCOMPARE(n2.firstName(), n6.firstName());
    QCOMPARE(n2.middleName(), n6.middleName());
    QCOMPARE(n2.lastName(), n6.lastName());
    QCOMPARE(n2.suffix(), n6.suffix());

    QCOMPARE(pa.details<QContactTag>().count(), 1);
    QCOMPARE(pa.details<QContactTag>().at(0).tag(), t5.tag());

    // The exported ID will be the aggregate ID
    QContact a4 = m_cm->contact(pa.id());
    QCOMPARE(a4.detail<QContactSyncTarget>().syncTarget(), QString::fromLatin1("aggregate"));

    n2 = a4.detail<QContactName>();
    QCOMPARE(n2.prefix(), n6.prefix());
    QCOMPARE(n2.firstName(), n6.firstName());
    QCOMPARE(n2.middleName(), n6.middleName());
    QCOMPARE(n2.lastName(), n6.lastName());
    QCOMPARE(n2.suffix(), n6.suffix());

    QCOMPARE(a4.details<QContactTag>().count(), 1);
    QCOMPARE(a4.details<QContactTag>().at(0).tag(), t5.tag());

    // A local constituent will have been created
    QCOMPARE(a4.relatedContacts(aggregatesRelationship, QContactRelationship::Second).count(), 1);

    QContact lxc = m_cm->contact(a4.relatedContacts(aggregatesRelationship, QContactRelationship::Second).at(0).id());
    QCOMPARE(lxc.detail<QContactSyncTarget>().syncTarget(), QString::fromLatin1("local"));

    n2 = lxc.detail<QContactName>();
    QCOMPARE(n2.prefix(), n6.prefix());
    QCOMPARE(n2.firstName(), n6.firstName());
    QCOMPARE(n2.middleName(), n6.middleName());
    QCOMPARE(n2.lastName(), n6.lastName());
    QCOMPARE(n2.suffix(), n6.suffix());

    QCOMPARE(lxc.details<QContactTag>().count(), 1);
    QCOMPARE(lxc.details<QContactTag>().at(0).tag(), t5.tag());

    // Modify the created local
    mpa = pa;

    QContactName n7 = mpa.detail<QContactName>();
    n7.setMiddleName("Karl");
    mpa.saveDetail(&n7);

    t5 = mpa.detail<QContactTag>();
    t5.setTag("Quantum Mechanic");
    mpa.saveDetail(&t5);

    modifications.clear();
    modifications.append(qMakePair(pa, mpa));
    QVERIFY(cme->storeSyncContacts("export", policy, &modifications, &err));

    // The local's details should have been updated
    lxc = m_cm->contact(lxc.id());

    n2 = lxc.detail<QContactName>();
    QCOMPARE(n2.prefix(), n7.prefix());
    QCOMPARE(n2.firstName(), n7.firstName());
    QCOMPARE(n2.middleName(), n7.middleName());
    QCOMPARE(n2.lastName(), n7.lastName());
    QCOMPARE(n2.suffix(), n7.suffix());

    QCOMPARE(lxc.details<QContactTag>().count(), 1);
    QCOMPARE(lxc.details<QContactTag>().at(0).tag(), t5.tag());

    // The aggregate's details should also be updated
    a4 = m_cm->contact(a4.id());

    n2 = a4.detail<QContactName>();
    QCOMPARE(n2.prefix(), n7.prefix());
    QCOMPARE(n2.firstName(), n7.firstName());
    QCOMPARE(n2.middleName(), n7.middleName());
    QCOMPARE(n2.lastName(), n7.lastName());
    QCOMPARE(n2.suffix(), n7.suffix());

    QCOMPARE(a4.details<QContactTag>().count(), 1);
    QCOMPARE(a4.details<QContactTag>().at(0).tag(), t5.tag());

    // Report the export DB contact as deleted from the export DB
    modifications.clear();
    modifications.append(qMakePair(a4, QContact()));
    QVERIFY(cme->storeSyncContacts("export", policy, &modifications, &err));

    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);

    QList<QContactId> contactIds = m_cm->contactIds(allSyncTargets);

    // The local and aggregate should be removed
    QVERIFY(!contactIds.contains(lxc.id()));
    QVERIFY(!contactIds.contains(a4.id()));

    // Report an exported contact as removed from the export DB
    modifications.clear();
    modifications.append(qMakePair(a3, QContact()));
    QVERIFY(cme->storeSyncContacts("export", policy, &modifications, &err));

    // All constituents will be removed except that belonging to another sync target
    contactIds = m_cm->contactIds(allSyncTargets);
    QVERIFY(!contactIds.contains(nlc.id()));
    QVERIFY(!contactIds.contains(a3.id()));
    QVERIFY(contactIds.contains(adstc.id()));

    // Report both sync-test contacts as remotely-deleted
    modifications.clear();
    modifications.append(qMakePair(stc, QContact()));
    modifications.append(qMakePair(stc2, QContact()));
    QVERIFY(cme->storeSyncContacts("sync-test", policy, &modifications, &err));

    // The sync target constituents should be removed
    contactIds = m_cm->contactIds(allSyncTargets);
    QVERIFY(!contactIds.contains(stc.id()));
    QVERIFY(!contactIds.contains(stc2.id()));

    // The local constituents and those from other sync targets should remain
    QVERIFY(contactIds.contains(lc.id()));
    QVERIFY(contactIds.contains(alc.id()));
    QVERIFY(contactIds.contains(dstc.id()));

    // The aggregates should no longer contain details from sync target contacts
    // but should still contain details from the local constituents
    a = m_cm->contact(a1Id);
    QCOMPARE(a.details<QContactEmailAddress>().count(), 0);
    QCOMPARE(a.details<QContactHobby>().count(), 0);
    QCOMPARE(a.details<QContactNickname>().count(), 0);
    QCOMPARE(a.details<QContactPhoneNumber>().count(), 1);
    QCOMPARE(a.detail<QContactPhoneNumber>().number(), pn.number());
    QCOMPARE(a.details<QContactTag>().count(), 1);
    QCOMPARE(a.detail<QContactTag>().tag(), t.tag());

    a2 = m_cm->contact(a2Id);
    QCOMPARE(a2.details<QContactEmailAddress>().count(), 0);
    QCOMPARE(a2.details<QContactTag>().count(), 1);
    QCOMPARE(a2.details<QContactTag>().at(0).tag(), t3.tag());
    QCOMPARE(a2.details<QContactNote>().count(), 2);
    QCOMPARE((QSet<QString>() << a2.details<QContactNote>().at(0).note() << a2.details<QContactNote>().at(1).note()), (QSet<QString>() << note.note() << note2.note()));
    QCOMPARE(a2.details<QContactHobby>().count(), 0);

    // The next fetch should not return the deleted contacts
    syncContacts.clear();
    updatedSyncTime = QDateTime();
    QVERIFY(cme->fetchSyncContacts("sync-test", initialTime, exportedIds, &syncContacts, 0, 0, &updatedSyncTime, &err));
    QCOMPARE(syncContacts.count(), 0);

    // Now ensure that if the sync target constituent is the only constituent,
    // removing it via storeSyncContacts() will result in the aggregate being removed.
    QDateTime finalTime = QDateTime::currentDateTimeUtc();
    QTest::qWait(1);

    // Create a final sync target contact, with no linked constituents
    QContactName n5;
    n5.setFirstName("Julius");
    n5.setLastName("Oppenheimer");

    QContact fstc;
    fstc.saveDetail(&n5);

    modifications.clear();
    modifications.append(qMakePair(QContact(), fstc));
    QVERIFY(cme->storeSyncContacts("sync-test", policy, &modifications, &err));

    syncContacts.clear();
    updatedSyncTime = QDateTime();
    QVERIFY(cme->fetchSyncContacts("sync-test", finalTime, exportedIds, &syncContacts, 0, 0, &updatedSyncTime, &err));
    QCOMPARE(syncContacts.count(), 1);

    fstc = m_cm->contact(retrievalId(syncContacts.at(0)));

    // The contact should have an aggregate
    QCOMPARE(fstc.relatedContacts(aggregatesRelationship, QContactRelationship::First).count(), 1);
    QContactId faId = fstc.relatedContacts(aggregatesRelationship, QContactRelationship::First).at(0).id();

    contactIds = m_cm->contactIds(allSyncTargets);
    QVERIFY(contactIds.contains(fstc.id()));
    QVERIFY(contactIds.contains(faId));

    // Wait for the delivery of any pending delete signals
    QTest::qWait(500);

    // Now remove the sync target contact
    QSignalSpy remSpy(m_cm, contactsRemovedSignal);
    int remSpyCount = remSpy.count();
    modifications.clear();
    modifications.append(qMakePair(fstc, QContact()));
    QVERIFY(cme->storeSyncContacts("sync-test", policy, &modifications, &err));

    // Both the constituent and the aggregate should be removed
    contactIds = m_cm->contactIds(allSyncTargets);
    QVERIFY(!contactIds.contains(fstc.id()));
    QVERIFY(!contactIds.contains(faId));
    QTRY_COMPARE(remSpy.count(), remSpyCount+1);
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

bool haveExpectedContent(const QContact &c, const QString &phone, TestSyncAdapter::PhoneModifiability modifiability, const QString &email)
{
    const QContactPhoneNumber &phn(c.detail<QContactPhoneNumber>());

    TestSyncAdapter::PhoneModifiability modif = TestSyncAdapter::ImplicitlyModifiable;
    if (phn.values().contains(QContactDetail__FieldModifiable)) {
        modif = phn.value<bool>(QContactDetail__FieldModifiable)
              ? TestSyncAdapter::ExplicitlyModifiable
              : TestSyncAdapter::ExplicitlyNonModifiable;
    }

    return phn.number() == phone && modif == modifiability
        && c.detail<QContactEmailAddress>().emailAddress() == email;
}

void tst_Aggregation::testSyncAdapter()
{
    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);
    QList<QContactId> originalIds = m_cm->contactIds(allSyncTargets);

    // add some contacts remotely, and downsync them.  It should not result in an upsync.
    QString accountId(QStringLiteral("1"));
    TestSyncAdapter tsa(accountId);
    tsa.addRemoteContact(accountId, "John", "TsaOne", "1111111", TestSyncAdapter::ImplicitlyModifiable);
    tsa.addRemoteContact(accountId, "Bob", "TsaTwo", "2222222", TestSyncAdapter::ExplicitlyModifiable);
    tsa.addRemoteContact(accountId, "Mark", "TsaThree", "3333333", TestSyncAdapter::ExplicitlyNonModifiable);

    QSignalSpy finishedSpy(&tsa, SIGNAL(finished()));
    tsa.performTwoWaySync(accountId);
    QTRY_COMPARE(finishedSpy.count(), 1);

    // should have 6 more contacts than we had before (aggregate+synctarget x 3)
    QList<QContact> allContacts = m_cm->contacts(allSyncTargets);
    QContactId tsaOneStcId, tsaOneAggId, tsaTwoStcId, tsaTwoAggId, tsaThreeStcId, tsaThreeAggId;
    for (int i = allContacts.size() - 1; i >= 0; --i) {
        const QContact &c(allContacts[i]);
        if (originalIds.contains(c.id())) {
            allContacts.removeAt(i);
        } else {
            bool isAggregate = c.relatedContacts(QStringLiteral("Aggregates"), QContactRelationship::Second).size() > 0;
            if (c.detail<QContactName>().firstName() == QStringLiteral("John")) {
                if (isAggregate) {
                    tsaOneAggId = c.id();
                } else {
                    tsaOneStcId = c.id();
                }
            } else if (c.detail<QContactName>().firstName() == QStringLiteral("Bob")) {
                if (isAggregate) {
                    tsaTwoAggId = c.id();
                } else {
                    tsaTwoStcId = c.id();
                }
            } else if (c.detail<QContactName>().firstName() == QStringLiteral("Mark")) {
                if (isAggregate) {
                    tsaThreeAggId = c.id();
                } else {
                    tsaThreeStcId = c.id();
                }
            }
        }
    }
    QCOMPARE(allContacts.size(), 6);
    QVERIFY(tsaOneStcId != QContactId());
    QVERIFY(tsaOneAggId != QContactId());
    QVERIFY(tsaTwoStcId != QContactId());
    QVERIFY(tsaTwoAggId != QContactId());
    QVERIFY(tsaThreeStcId != QContactId());
    QVERIFY(tsaThreeAggId != QContactId());

    // verify that the added IDs were reported
    QSet<QContactId> reportedIds(tsa.modifiedIds(accountId));
    QCOMPARE(reportedIds.size(), 3);
    QVERIFY(reportedIds.contains(tsaOneStcId));
    QVERIFY(reportedIds.contains(tsaTwoStcId));
    QVERIFY(reportedIds.contains(tsaThreeStcId));

    // and no upsync of local changes should be required (there shouldn't have been any local changes).
    QVERIFY(!tsa.upsyncWasRequired(accountId));
    QVERIFY(tsa.downsyncWasRequired(accountId));

    // ensure that the downsynced contacts have the data we expect
    // note that aggregate contacts don't have a modifiability flag, since by definition
    // the modification would "actually" occur to some constituent contact detail,
    // and thus are considered by the haveExpectedContent function to be ImplicitlyModifiable.
    QVERIFY(haveExpectedContent(m_cm->contact(tsaOneStcId), QStringLiteral("1111111"), TestSyncAdapter::ExplicitlyModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaOneAggId), QStringLiteral("1111111"), TestSyncAdapter::ImplicitlyModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaTwoStcId), QStringLiteral("2222222"), TestSyncAdapter::ExplicitlyModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaTwoAggId), QStringLiteral("2222222"), TestSyncAdapter::ImplicitlyModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaThreeStcId), QStringLiteral("3333333"), TestSyncAdapter::ExplicitlyNonModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaThreeAggId), QStringLiteral("3333333"), TestSyncAdapter::ImplicitlyModifiable, QString()));

    // now modify tsaTwo's aggregate - should cause the creation of an incidental local.
    // triggering update should then not require downsync but would require upsync.
    QContact tsaTwoAggregate = m_cm->contact(tsaTwoAggId);
    QContactEmailAddress tsaTwoEmail;
    tsaTwoEmail.setEmailAddress("bob@tsatwo.com");
    tsaTwoAggregate.saveDetail(&tsaTwoEmail); // new detail, will cause creation of incidental local.
    m_cm->saveContact(&tsaTwoAggregate);
    allContacts = m_cm->contacts(allSyncTargets);
    QContactId tsaTwoLocalId;
    for (int i = allContacts.size() - 1; i >= 0; --i) {
        if (originalIds.contains(allContacts[i].id())) {
            allContacts.removeAt(i);
        } else if (allContacts[i].detail<QContactName>().firstName() == QStringLiteral("Bob")
                && allContacts[i].id() != tsaTwoAggId
                && allContacts[i].id() != tsaTwoStcId) {
            tsaTwoLocalId = allContacts[i].id();
        }
    }
    QCOMPARE(allContacts.size(), 7); // new incidental local.
    QVERIFY(tsaTwoLocalId != QContactId());

    // perform the sync.
    tsa.performTwoWaySync(accountId);
    QTRY_COMPARE(finishedSpy.count(), 2);

    // downsync should not have been required, but upsync should have been.
    QVERIFY(!tsa.downsyncWasRequired(accountId));
    QVERIFY(tsa.upsyncWasRequired(accountId));

    // ensure that the contacts have the data we expect
    QVERIFY(haveExpectedContent(m_cm->contact(tsaOneStcId), QStringLiteral("1111111"), TestSyncAdapter::ExplicitlyModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaOneAggId), QStringLiteral("1111111"), TestSyncAdapter::ImplicitlyModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaTwoStcId), QStringLiteral("2222222"), TestSyncAdapter::ExplicitlyModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaTwoLocalId), QString(), TestSyncAdapter::ImplicitlyModifiable, QStringLiteral("bob@tsatwo.com")));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaTwoAggId), QStringLiteral("2222222"), TestSyncAdapter::ImplicitlyModifiable, QStringLiteral("bob@tsatwo.com")));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaThreeStcId), QStringLiteral("3333333"), TestSyncAdapter::ExplicitlyNonModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaThreeAggId), QStringLiteral("3333333"), TestSyncAdapter::ImplicitlyModifiable, QString()));
    QVERIFY(haveExpectedContent(tsa.remoteContact(accountId, QStringLiteral("Bob"), QStringLiteral("TsaTwo")),
                                QStringLiteral("2222222"), TestSyncAdapter::ExplicitlyModifiable, QStringLiteral("bob@tsatwo.com")));

    // modify both locally and remotely.
    // we modify the phone number locally (ie, the synctarget constituent)
    // and the email address remotely (ie, the local constituent)
    // and test to ensure that everything is resolved/updated correctly.
    QContact tsaTwoStc = m_cm->contact(tsaTwoStcId);
    QContactPhoneNumber tsaTwoStcPhn = tsaTwoStc.detail<QContactPhoneNumber>();
    tsaTwoStcPhn.setNumber("2222229");
    tsaTwoStc.saveDetail(&tsaTwoStcPhn);
    QVERIFY(m_cm->saveContact(&tsaTwoStc));

    tsa.changeRemoteContactEmail(accountId, "Bob", "TsaTwo", "bob2@tsatwo.com");

    // perform the sync.
    tsa.performTwoWaySync(accountId);

    // ensure that the per-account separation is maintained properly for out of band data etc.
    tsa.addRemoteContact(QStringLiteral("2"), QStringLiteral("Jerry"), QStringLiteral("TsaTwoTwo"), QStringLiteral("555"));
    tsa.performTwoWaySync(QStringLiteral("2"));
    QTRY_COMPARE(finishedSpy.count(), 4); // wait for both to finish
    tsa.removeRemoteContact(QStringLiteral("2"), QStringLiteral("Jerry"), QStringLiteral("TsaTwoTwo"));
    tsa.performTwoWaySync(QStringLiteral("2"));
    QTRY_COMPARE(finishedSpy.count(), 5);

    // downsync and upsync should have been required in the original sync for the "main" account.
    QVERIFY(tsa.downsyncWasRequired(accountId));
    QVERIFY(tsa.upsyncWasRequired(accountId));

    // ensure that the contacts have the data we expect
    QVERIFY(haveExpectedContent(m_cm->contact(tsaOneStcId), QStringLiteral("1111111"), TestSyncAdapter::ExplicitlyModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaOneAggId), QStringLiteral("1111111"), TestSyncAdapter::ImplicitlyModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaTwoStcId), QStringLiteral("2222229"), TestSyncAdapter::ExplicitlyModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaTwoLocalId), QString(), TestSyncAdapter::ImplicitlyModifiable, QStringLiteral("bob2@tsatwo.com")));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaTwoAggId), QStringLiteral("2222229"), TestSyncAdapter::ImplicitlyModifiable, QStringLiteral("bob2@tsatwo.com")));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaThreeStcId), QStringLiteral("3333333"), TestSyncAdapter::ExplicitlyNonModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaThreeAggId), QStringLiteral("3333333"), TestSyncAdapter::ImplicitlyModifiable, QString()));
    QVERIFY(haveExpectedContent(tsa.remoteContact(accountId, QStringLiteral("Bob"), QStringLiteral("TsaTwo")),
                                QStringLiteral("2222229"), TestSyncAdapter::ExplicitlyModifiable, QStringLiteral("bob2@tsatwo.com")));

    // remove a contact locally, ensure that the removal is upsynced.
    QVERIFY(tsa.remoteContact(accountId, QStringLiteral("Mark"), QStringLiteral("TsaThree")) != QContact());
    QVERIFY(m_cm->removeContact(tsaThreeAggId));
    tsa.performTwoWaySync(accountId);
    QTRY_COMPARE(finishedSpy.count(), 6);
    QVERIFY(tsa.downsyncWasRequired(accountId)); // the previously upsynced changes which were applied will be returned, hence will be downsynced; but discarded as nonsubstantial / already applied.
    QVERIFY(tsa.upsyncWasRequired(accountId));

    // ensure that the contacts have the data we expect
    QVERIFY(haveExpectedContent(m_cm->contact(tsaOneStcId), QStringLiteral("1111111"), TestSyncAdapter::ExplicitlyModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaOneAggId), QStringLiteral("1111111"), TestSyncAdapter::ImplicitlyModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaTwoStcId), QStringLiteral("2222229"), TestSyncAdapter::ExplicitlyModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaTwoLocalId), QString(), TestSyncAdapter::ImplicitlyModifiable, QStringLiteral("bob2@tsatwo.com")));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaTwoAggId), QStringLiteral("2222229"), TestSyncAdapter::ImplicitlyModifiable, QStringLiteral("bob2@tsatwo.com")));
    QVERIFY(haveExpectedContent(tsa.remoteContact(accountId, QStringLiteral("Bob"), QStringLiteral("TsaTwo")),
                                QStringLiteral("2222229"), TestSyncAdapter::ExplicitlyModifiable, QStringLiteral("bob2@tsatwo.com")));
    QVERIFY(tsa.remoteContact(accountId, QStringLiteral("Mark"), QStringLiteral("TsaThree")) == QContact()); // deleted remotely.

    // add a contact locally, ensure that the addition is upsynced.
    QContact tsaFourLocal;
    QContactName tsaFourName;
    tsaFourName.setFirstName("Jennifer");
    tsaFourName.setLastName("TsaFour");
    QContactEmailAddress tsaFourEmail;
    tsaFourEmail.setEmailAddress("jennifer@tsafour.com");
    tsaFourLocal.saveDetail(&tsaFourName);
    tsaFourLocal.saveDetail(&tsaFourEmail);
    QVERIFY(m_cm->saveContact(&tsaFourLocal));
    QContactId tsaFourLocalId = tsaFourLocal.id();
    QContactId tsaFourAggId;
    allContacts = m_cm->contacts(allSyncTargets);
    for (int i = allContacts.size() - 1; i >= 0; --i) {
        if (originalIds.contains(allContacts[i].id())) {
            allContacts.removeAt(i);
        } else if (allContacts[i].detail<QContactName>().firstName() == QStringLiteral("Jennifer")
                && allContacts[i].id() != tsaFourLocalId) {
            tsaFourAggId = allContacts[i].id();
        }
    }
    QVERIFY(tsaFourAggId != QContactId());
    tsa.performTwoWaySync(accountId);
    QTRY_COMPARE(finishedSpy.count(), 7);
    QVERIFY(!tsa.downsyncWasRequired(accountId)); // there were no remote changes
    QVERIFY(tsa.upsyncWasRequired(accountId));
    QVERIFY(haveExpectedContent(tsa.remoteContact(accountId, QStringLiteral("Jennifer"), QStringLiteral("TsaFour")),
                                QString(), TestSyncAdapter::ImplicitlyModifiable, QStringLiteral("jennifer@tsafour.com")));

    // remove some contacts remotely, ensure the removals are downsynced.
    QTest::qWait(1);
    tsa.removeRemoteContact(accountId, QStringLiteral("Bob"), QStringLiteral("TsaTwo"));
    QVERIFY(tsa.remoteContact(accountId, QStringLiteral("Bob"), QStringLiteral("TsaTwo")) == QContact());
    tsa.removeRemoteContact(accountId, QStringLiteral("Jennifer"), QStringLiteral("TsaFour"));
    QVERIFY(tsa.remoteContact(accountId, QStringLiteral("Jennifer"), QStringLiteral("TsaFour")) == QContact());
    tsa.performTwoWaySync(accountId);
    QTRY_COMPARE(finishedSpy.count(), 8);
    QVERIFY(tsa.downsyncWasRequired(accountId));
    QVERIFY(!tsa.upsyncWasRequired(accountId));
    QList<QContactId> allIds = m_cm->contactIds(allSyncTargets);
    // the sync target constituents of two and four should be removed.
    // the local constituents (and the aggregates) should remain.
    QVERIFY(!allIds.contains(tsaTwoStcId));
    // the local constituent of tsaTwo should remain even though it's incidental.
    QVERIFY(allIds.contains(tsaTwoLocalId));
    QVERIFY(allIds.contains(tsaTwoAggId));
    // and the tsaFour contact was originally a pure-local addition, so shouldn't be removed.
    // it may, after all, have been synced up to other sync sources.
    // Note: we should NOT sync up either tsaTwo or tsaFour in subsequent syncs.
    QVERIFY(allIds.contains(tsaFourLocalId));
    QVERIFY(allIds.contains(tsaFourAggId));

    // modify two and four locally, and ensure they don't get synced up.
    tsaFourLocal = m_cm->contact(tsaFourLocalId);
    tsaFourEmail = tsaFourLocal.detail<QContactEmailAddress>();
    tsaFourEmail.setEmailAddress("jennifer2@tsafour.com");
    tsaFourLocal.saveDetail(&tsaFourEmail);
    m_cm->saveContact(&tsaFourLocal);
    tsaTwoAggregate = m_cm->contact(tsaTwoAggId);
    tsaTwoEmail = tsaTwoAggregate.detail<QContactEmailAddress>();
    tsaTwoEmail.setEmailAddress("bob3@tsatwo.com");
    tsaTwoAggregate.saveDetail(&tsaTwoEmail);
    m_cm->saveContact(&tsaTwoAggregate);
    tsa.performTwoWaySync(accountId);
    QTRY_COMPARE(finishedSpy.count(), 9);
    QVERIFY(!tsa.downsyncWasRequired(accountId)); // no remote changes since last sync, and last sync didn't upsync any changes.
    QVERIFY(!tsa.upsyncWasRequired(accountId));   // changes shouldn't have been upsynced.

    // modify (the only remaining) remote contact, delete the local contacts.
    // the conflict should be resolved in favour of the local device, and the
    // removal should be upsynced.  TODO: support different conflict resolutions.
    tsa.changeRemoteContactPhone(accountId, QStringLiteral("John"), QStringLiteral("TsaOne"), QStringLiteral("1111112"));
    QVERIFY(m_cm->removeContact(tsaOneAggId));
    QVERIFY(m_cm->removeContact(tsaTwoAggId));
    QVERIFY(m_cm->removeContact(tsaFourAggId));
    tsa.performTwoWaySync(accountId);
    QTRY_COMPARE(finishedSpy.count(), 10);
    QVERIFY(tsa.downsyncWasRequired(accountId));
    QVERIFY(tsa.upsyncWasRequired(accountId));
    allIds = m_cm->contactIds(allSyncTargets);
    QVERIFY(!allIds.contains(tsaOneStcId));
    QVERIFY(!allIds.contains(tsaOneAggId));
    QVERIFY(!allIds.contains(tsaTwoLocalId));
    QVERIFY(!allIds.contains(tsaTwoAggId));
    QVERIFY(!allIds.contains(tsaFourLocalId));
    QVERIFY(!allIds.contains(tsaFourAggId));
    // should be back to original set of ids prior to test.
    QCOMPARE(originalIds.size(), allIds.size());
    foreach (const QContactId &id, allIds) {
        QVERIFY(originalIds.contains(id));
    }

    // now add a new contact remotely, which has a name.
    // remove the name locally, and modify it remotely - this will cause a "composed detail" modification update.
    tsa.addRemoteContact(accountId, "John", "TsaFive", "555555", TestSyncAdapter::ImplicitlyModifiable);
    tsa.performTwoWaySync(accountId);
    QTRY_COMPARE(finishedSpy.count(), 11);
    allIds = m_cm->contactIds(allSyncTargets);
    QCOMPARE(allIds.size(), originalIds.size() + 2); // remote + aggregate
    allContacts = m_cm->contacts(allSyncTargets);
    QContact tsaFiveStc, tsaFiveAgg;
    for (int i = allContacts.size() - 1; i >= 0; --i) {
        const QContact &c(allContacts[i]);
        if (originalIds.contains(c.id())) {
            allContacts.removeAt(i);
        } else {
            bool isAggregate = c.relatedContacts(QStringLiteral("Aggregates"), QContactRelationship::Second).size() > 0;
            if (isAggregate) {
                tsaFiveAgg = c;
            } else {
                tsaFiveStc = c;
            }
        }
    }
    QVERIFY(tsaFiveAgg.id() != QContactId());
    QVERIFY(tsaFiveStc.id() != QContactId());
    // now remove the name on the local copy
    QContactName tsaFiveName = tsaFiveStc.detail<QContactName>();
    tsaFiveStc.removeDetail(&tsaFiveName);
    QVERIFY(m_cm->saveContact(&tsaFiveStc));
    // now modify the name on the remote server, and trigger sync.
    // during this process, the local contact will NOT have a QContactName detail
    // so the modification pair will be <null, newName>.  This used to trigger a bug.
    tsa.changeRemoteContactName(accountId, "John", "TsaFive", "Jonathan", "TsaFive");
    tsa.performTwoWaySync(accountId);
    QTRY_COMPARE(finishedSpy.count(), 12);
    // ensure that the contact contains the data we expect
    // currently, we only support PreserveLocalChanges conflict resolution
    tsaFiveStc = m_cm->contact(tsaFiveStc.id());
    QCOMPARE(tsaFiveStc.detail<QContactName>().firstName(), QString());
    QCOMPARE(tsaFiveStc.detail<QContactName>().lastName(), QString());
    QCOMPARE(tsaFiveStc.detail<QContactPhoneNumber>().number(), QStringLiteral("555555"));
    // now do the same test as above, but this time remove the name remotely and modify it locally.
    tsa.addRemoteContact(accountId, "James", "TsaSix", "666666", TestSyncAdapter::ImplicitlyModifiable);
    tsa.performTwoWaySync(accountId);
    QTRY_COMPARE(finishedSpy.count(), 13);
    allIds = m_cm->contactIds(allSyncTargets);
    QCOMPARE(allIds.size(), originalIds.size() + 4); // remote + aggregate for Five and Six
    allContacts = m_cm->contacts(allSyncTargets);
    QContact tsaSixStc, tsaSixAgg;
    for (int i = allContacts.size() - 1; i >= 0; --i) {
        const QContact &c(allContacts[i]);
        if (originalIds.contains(c.id())) {
            allContacts.removeAt(i);
        } else {
            bool isAggregate = c.relatedContacts(QStringLiteral("Aggregates"), QContactRelationship::Second).size() > 0;
            if (isAggregate && c.id() != tsaFiveAgg.id()) {
                tsaSixAgg = c;
            } else if (!isAggregate && c.id() != tsaFiveStc.id()){
                tsaSixStc = c;
            }
        }
    }
    QVERIFY(tsaSixAgg.id() != QContactId());
    QVERIFY(tsaSixStc.id() != QContactId());
    // now modify the name on the local copy
    QContactName tsaSixName = tsaSixStc.detail<QContactName>();
    tsaSixName.setFirstName("Jimmy");
    tsaSixStc.saveDetail(&tsaSixName);
    QVERIFY(m_cm->saveContact(&tsaSixStc));
    // now remove the name on the remote server, and trigger sync.
    // during this process, the remote contact will NOT have a QContactName detail
    // so the modification pair will be <newName, null>.
    tsa.changeRemoteContactName(accountId, "James", "TsaSix", "", "");
    tsa.performTwoWaySync(accountId);
    QTRY_COMPARE(finishedSpy.count(), 14);
    // ensure that the contact contains the data we expect
    // currently, we only support PreserveLocalChanges conflict resolution
    tsaSixStc = m_cm->contact(tsaSixStc.id());
    QCOMPARE(tsaSixStc.detail<QContactName>().firstName(), QStringLiteral("Jimmy"));
    QCOMPARE(tsaSixStc.detail<QContactName>().lastName(), QStringLiteral("TsaSix"));
    QCOMPARE(tsaSixStc.detail<QContactPhoneNumber>().number(), QStringLiteral("666666"));
    // partial clean up, locally remove all contact aggregates from sync adapter.
    QVERIFY(m_cm->removeContact(tsaFiveAgg.id()));
    QVERIFY(m_cm->removeContact(tsaSixAgg.id()));
    QCOMPARE(m_cm->contactIds(allSyncTargets).size(), originalIds.size());
    tsa.performTwoWaySync(accountId);
    QTRY_COMPARE(finishedSpy.count(), 15);

    // the following test ensures that "remote duplicate removal" works correctly.
    // - have multiple duplicated contacts server-side
    // - sync them down
    // - remove all but one duplicate server-side
    // - sync the changes (removals)
    // - ensure that the removals are applied correctly device-side.
    tsa.addRemoteDuplicates(accountId, "John", "Duplicate", "1234321");
    tsa.performTwoWaySync(accountId);
    QTRY_COMPARE(finishedSpy.count(), 16);
    allContacts = m_cm->contacts(allSyncTargets);
    int syncTargetConstituentsCount = 0, aggCount = 0;
    QContact tsaSevenAgg;
    for (int i = allContacts.size() - 1; i >= 0; --i) {
        const QContact &c(allContacts[i]);
        if (originalIds.contains(c.id())) {
            allContacts.removeAt(i);
        } else {
            int tempAggCount = c.relatedContacts(QStringLiteral("Aggregates"), QContactRelationship::Second).size();
            if (tempAggCount > 0) {
                aggCount = tempAggCount;
                tsaSevenAgg = c;
            } else {
                syncTargetConstituentsCount += 1;
            }
        }
    }
    QVERIFY(tsaSevenAgg.id() != QContactId());
    QCOMPARE(aggCount, 3); // the aggregate should aggregate the 3 duplicates
    QCOMPARE(syncTargetConstituentsCount, 3); // there should be 3 duplicates
    tsa.mergeRemoteDuplicates(accountId);
    tsa.performTwoWaySync(accountId);
    QTRY_COMPARE(finishedSpy.count(), 17);
    allContacts = m_cm->contacts(allSyncTargets);
    syncTargetConstituentsCount = 0; aggCount = 0;
    for (int i = allContacts.size() - 1; i >= 0; --i) {
        const QContact &c(allContacts[i]);
        if (originalIds.contains(c.id())) {
            allContacts.removeAt(i);
        } else {
            int tempAggCount = c.relatedContacts(QStringLiteral("Aggregates"), QContactRelationship::Second).size();
            if (tempAggCount > 0) {
                tsaSevenAgg = c;
                aggCount = tempAggCount;
            } else {
                syncTargetConstituentsCount += 1;
            }
        }
    }
    QCOMPARE(aggCount, 1); // now there should be just one sync target constituent.
    QCOMPARE(syncTargetConstituentsCount, 1);
    // clean up.
    QVERIFY(m_cm->removeContact(tsaSevenAgg.id()));
    QCOMPARE(m_cm->contactIds(allSyncTargets).size(), originalIds.size());
    tsa.removeAllContacts();
}

QTEST_MAIN(tst_Aggregation)
#include "tst_aggregation.moc"
