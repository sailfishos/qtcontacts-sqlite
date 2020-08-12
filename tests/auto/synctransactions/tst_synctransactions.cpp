/*
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

#include <QtGlobal>

#include <QtTest/QtTest>

#include "../../util.h"

#include "qcontactcollectionchangesfetchrequest.h"
#include "qcontactcollectionchangesfetchrequest_impl.h"
#include "qcontactchangesfetchrequest.h"
#include "qcontactchangesfetchrequest_impl.h"
#include "qcontactchangessaverequest.h"
#include "qcontactchangessaverequest_impl.h"
#include "qcontactclearchangeflagsrequest.h"
#include "qcontactclearchangeflagsrequest_impl.h"

namespace {

QByteArray aggregateAddressbookId()
{
    return QByteArrayLiteral("col-") + QByteArray::number(1); // AggregateAddressbookCollectionId
}

QByteArray localAddressbookId()
{
    return QByteArrayLiteral("col-") + QByteArray::number(2); // LocalAddressbookCollectionId
}

}

class tst_synctransactions : public QObject
{
    Q_OBJECT

public:
    tst_synctransactions();
    virtual ~tst_synctransactions();

public slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

public slots:
    void addColAccumulationSlot(const QList<QContactCollectionId> &ids);
    void addAccumulationSlot(const QList<QContactId> &ids);

private slots:
    void singleCollection_noContacts();
    void singleCollection_addedContacts();
    void singleCollection_multipleCycles();
    void singleCollection_unhandledChanges();
    void multipleCollections();

    void syncRequests();

private:
    void waitForSignalPropagation();

    QContactManager *m_cm;
    QSet<QContactCollectionId> m_createdColIds;
    QSet<QContactId> m_createdIds;
};

tst_synctransactions::tst_synctransactions()
    : m_cm(0)
{
    QMap<QString, QString> parameters;
    parameters.insert(QString::fromLatin1("autoTest"), QString::fromLatin1("true"));
    parameters.insert(QString::fromLatin1("mergePresenceChanges"), QString::fromLatin1("true"));
    m_cm = new QContactManager(QString::fromLatin1("org.nemomobile.contacts.sqlite"), parameters);

    QTest::qWait(250); // creating self contact etc will cause some signals to be emitted.  ignore them.
    QObject::connect(m_cm, &QContactManager::collectionsAdded, this, &tst_synctransactions::addColAccumulationSlot);
    QObject::connect(m_cm, &QContactManager::contactsAdded, this, &tst_synctransactions::addAccumulationSlot);
}

tst_synctransactions::~tst_synctransactions()
{
}

void tst_synctransactions::initTestCase()
{
    registerIdType();

    /* Make sure the DB is empty */
    QContactCollectionFilter allCollections;
    m_cm->removeContacts(m_cm->contactIds(allCollections));
    waitForSignalPropagation();
}

void tst_synctransactions::init()
{
    m_createdColIds.clear();
    m_createdIds.clear();
}

void tst_synctransactions::cleanupTestCase()
{
}

void tst_synctransactions::cleanup()
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

void tst_synctransactions::waitForSignalPropagation()
{
    // Signals are routed via DBUS, so we need to wait for them to arrive
    QTest::qWait(50);
}

void tst_synctransactions::addColAccumulationSlot(const QList<QContactCollectionId> &ids)
{
    foreach (const QContactCollectionId &id, ids) {
        m_createdColIds.insert(id);
    }
}

void tst_synctransactions::addAccumulationSlot(const QList<QContactId> &ids)
{
    foreach (const QContactId &id, ids) {
        m_createdIds.insert(id);
    }
}

void tst_synctransactions::singleCollection_noContacts()
{
    QtContactsSqliteExtensions::ContactManagerEngine *cme = QtContactsSqliteExtensions::contactManagerEngine(*m_cm);
    QContactManager::Error err = QContactManager::NoError;
    QContactCollectionId remoteAddressbookId;

    // ensure that initially, no changes are detected.
    {
        QList<QContactCollection> addedCollections;
        QList<QContactCollection> modifiedCollections;
        QList<QContactCollection> deletedCollections;
        QList<QContactCollection> unmodifiedCollections;
        QVERIFY(cme->fetchCollectionChanges(
                0, QStringLiteral("tst_synctransactions"),
                &addedCollections,
                &modifiedCollections,
                &deletedCollections,
                &unmodifiedCollections,
                &err));
        QCOMPARE(addedCollections.count(), 0);
        QCOMPARE(modifiedCollections.count(), 0);
        QCOMPARE(deletedCollections.count(), 0);
        QCOMPARE(unmodifiedCollections.count(), 0);
    }

    // simulate a sync cycle which results in an empty remote addressbook being added.
    {
        QHash<QContactCollection*, QList<QContact> *> additions;
        QHash<QContactCollection*, QList<QContact> *> modifications;
        QContactCollection remoteAddressbook;
        remoteAddressbook.setMetaData(QContactCollection::KeyName, QStringLiteral("test"));
        remoteAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, "tst_synctransactions");
        remoteAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID, 5);
        remoteAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH, "/addressbooks/test");
        QList<QContact> addedCollectionContacts;
        additions.insert(&remoteAddressbook, &addedCollectionContacts);
        const QtContactsSqliteExtensions::ContactManagerEngine::ConflictResolutionPolicy policy(
                QtContactsSqliteExtensions::ContactManagerEngine::PreserveLocalChanges);
        QVERIFY(cme->storeChanges(
                &additions,
                &modifications,
                QList<QContactCollectionId>(),
                policy, true, &err));
        QVERIFY(!remoteAddressbook.id().isNull()); // id should have been set during save operation.
        remoteAddressbookId = remoteAddressbook.id();
    }

    // ensure that no changes are detected, but the collection is reported as unmodified.
    {
        QList<QContactCollection> addedCollections;
        QList<QContactCollection> modifiedCollections;
        QList<QContactCollection> deletedCollections;
        QList<QContactCollection> unmodifiedCollections;
        QVERIFY(cme->fetchCollectionChanges(
                5, QStringLiteral("tst_synctransactions"),
                &addedCollections,
                &modifiedCollections,
                &deletedCollections,
                &unmodifiedCollections,
                &err));
        QCOMPARE(addedCollections.count(), 0);
        QCOMPARE(modifiedCollections.count(), 0);
        QCOMPARE(deletedCollections.count(), 0);
        QCOMPARE(unmodifiedCollections.count(), 1);
        QCOMPARE(unmodifiedCollections.first().id(), remoteAddressbookId);
    }

    // and ensure that no contact changes are reported for that collection
    {
        QList<QContact> addedContacts;
        QList<QContact> modifiedContacts;
        QList<QContact> deletedContacts;
        QList<QContact> unmodifiedContacts;
        QVERIFY(cme->fetchContactChanges(
                    remoteAddressbookId,
                    &addedContacts,
                    &modifiedContacts,
                    &deletedContacts,
                    &unmodifiedContacts,
                    &err));
        QCOMPARE(addedContacts.count(), 0);
        QCOMPARE(modifiedContacts.count(), 0);
        QCOMPARE(deletedContacts.count(), 0);
        QCOMPARE(unmodifiedContacts.count(), 0);
    }

    // clean up.
    QVERIFY(m_cm->removeCollection(remoteAddressbookId));
    QVERIFY(cme->clearChangeFlags(remoteAddressbookId, &err));
}

void tst_synctransactions::singleCollection_addedContacts()
{
    QtContactsSqliteExtensions::ContactManagerEngine *cme = QtContactsSqliteExtensions::contactManagerEngine(*m_cm);
    QContactManager::Error err = QContactManager::NoError;
    QContactCollectionId remoteAddressbookId;
    QContactId remoteContactId;

    // ensure that initially, no changes are detected.
    {
        QList<QContactCollection> addedCollections;
        QList<QContactCollection> modifiedCollections;
        QList<QContactCollection> deletedCollections;
        QList<QContactCollection> unmodifiedCollections;
        QVERIFY(cme->fetchCollectionChanges(
                0, QStringLiteral("tst_synctransactions"),
                &addedCollections,
                &modifiedCollections,
                &deletedCollections,
                &unmodifiedCollections,
                &err));
        QCOMPARE(addedCollections.count(), 0);
        QCOMPARE(modifiedCollections.count(), 0);
        QCOMPARE(deletedCollections.count(), 0);
        QCOMPARE(unmodifiedCollections.count(), 0);
    }

    // simulate a sync cycle which results in a non-empty remote addressbook being added.
    {
        QHash<QContactCollection*, QList<QContact> *> additions;
        QHash<QContactCollection*, QList<QContact> *> modifications;
        QContactCollection remoteAddressbook;
        remoteAddressbook.setMetaData(QContactCollection::KeyName, QStringLiteral("test"));
        remoteAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, "tst_synctransactions");
        remoteAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID, 5);
        remoteAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH, "/addressbooks/test");
        QContact syncAlice;
        QContactName san;
        san.setFirstName("Alice");
        san.setMiddleName("In");
        san.setLastName("Wonderland");
        syncAlice.saveDetail(&san);
        QContactPhoneNumber saph;
        saph.setNumber("123454321");
        syncAlice.saveDetail(&saph);
        QContactEmailAddress saem;
        saem.setEmailAddress("alice@wonderland.tld");
        syncAlice.saveDetail(&saem);
        QContactStatusFlags saf;
        saf.setFlag(QContactStatusFlags::IsAdded, true);
        syncAlice.saveDetail(&saf);
        QList<QContact> addedCollectionContacts;
        addedCollectionContacts.append(syncAlice);
        additions.insert(&remoteAddressbook, &addedCollectionContacts);
        const QtContactsSqliteExtensions::ContactManagerEngine::ConflictResolutionPolicy policy(
                QtContactsSqliteExtensions::ContactManagerEngine::PreserveLocalChanges);
        QVERIFY(cme->storeChanges(
                &additions,
                &modifications,
                QList<QContactCollectionId>(),
                policy, true, &err));
        QVERIFY(!remoteAddressbook.id().isNull()); // id should have been set during save operation.
        QVERIFY(!addedCollectionContacts.first().id().isNull()); // id should have been set during save operation.
        remoteAddressbookId = remoteAddressbook.id();
        remoteContactId = addedCollectionContacts.first().id();
    }

    // ensure that no changes are detected, but the collection is reported as unmodified.
    {
        QList<QContactCollection> addedCollections;
        QList<QContactCollection> modifiedCollections;
        QList<QContactCollection> deletedCollections;
        QList<QContactCollection> unmodifiedCollections;
        QVERIFY(cme->fetchCollectionChanges(
                5, QStringLiteral("tst_synctransactions"),
                &addedCollections,
                &modifiedCollections,
                &deletedCollections,
                &unmodifiedCollections,
                &err));
        QCOMPARE(addedCollections.count(), 0);
        QCOMPARE(modifiedCollections.count(), 0);
        QCOMPARE(deletedCollections.count(), 0);
        QCOMPARE(unmodifiedCollections.count(), 1);
        QCOMPARE(unmodifiedCollections.first().id(), remoteAddressbookId);
    }

    // and ensure that no contact changes are reported for that collection,
    // but the remote contact is reported as unmodified.
    {
        QList<QContact> addedContacts;
        QList<QContact> modifiedContacts;
        QList<QContact> deletedContacts;
        QList<QContact> unmodifiedContacts;
        QVERIFY(cme->fetchContactChanges(
                    remoteAddressbookId,
                    &addedContacts,
                    &modifiedContacts,
                    &deletedContacts,
                    &unmodifiedContacts,
                    &err));
        QCOMPARE(addedContacts.count(), 0);
        QCOMPARE(modifiedContacts.count(), 0);
        QCOMPARE(deletedContacts.count(), 0);
        QCOMPARE(unmodifiedContacts.count(), 1);
        QCOMPARE(unmodifiedContacts.first().id(), remoteContactId);
    }

    // clean up.
    QVERIFY(m_cm->removeCollection(remoteAddressbookId));
    QVERIFY(cme->clearChangeFlags(remoteAddressbookId, &err));
}

void tst_synctransactions::singleCollection_multipleCycles()
{
    QtContactsSqliteExtensions::ContactManagerEngine *cme = QtContactsSqliteExtensions::contactManagerEngine(*m_cm);
    QContactManager::Error err = QContactManager::NoError;

    QHash<QContactCollection*, QList<QContact> *> additions;
    QHash<QContactCollection*, QList<QContact> *> modifications;

    QContactCollection remoteAddressbook;
    remoteAddressbook.setMetaData(QContactCollection::KeyName, QStringLiteral("test"));
    remoteAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, "tst_synctransactions");
    remoteAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID, 5);
    remoteAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH, "/addressbooks/test");

    QContact syncAlice;
    QContactName an;
    an.setFirstName("Alice");
    an.setMiddleName("In");
    an.setLastName("Wonderland");
    syncAlice.saveDetail(&an);
    QContactPhoneNumber aph;
    aph.setNumber("123454321");
    syncAlice.saveDetail(&aph);
    QContactEmailAddress aem;
    aem.setEmailAddress("alice@wonderland.tld");
    syncAlice.saveDetail(&aem);
    QContactStatusFlags af;
    af.setFlag(QContactStatusFlags::IsAdded, true);
    syncAlice.saveDetail(&af);

    QContact syncBob;
    QContactName bn;
    bn.setFirstName("Bob");
    bn.setMiddleName("The");
    bn.setLastName("Constructor");
    syncBob.saveDetail(&bn);
    QContactPhoneNumber bph;
    bph.setNumber("543212345");
    syncBob.saveDetail(&bph);
    QContactEmailAddress bem;
    bem.setEmailAddress("bob@construction.tld");
    syncBob.saveDetail(&bem);
    QContactStatusFlags bf;
    bf.setFlag(QContactStatusFlags::IsAdded, true);
    syncBob.saveDetail(&bf);

    QList<QContact> addedCollectionContacts;
    addedCollectionContacts.append(syncAlice);
    addedCollectionContacts.append(syncBob);
    additions.insert(&remoteAddressbook, &addedCollectionContacts);

    const QtContactsSqliteExtensions::ContactManagerEngine::ConflictResolutionPolicy policy(
            QtContactsSqliteExtensions::ContactManagerEngine::PreserveLocalChanges);

    // initial sync cycle: remote has a non-empty addressbook.
    QVERIFY(cme->storeChanges(
            &additions,
            &modifications,
            QList<QContactCollectionId>(),
            policy, true, &err));
    QCOMPARE(err, QContactManager::NoError);

    QVERIFY(!remoteAddressbook.id().isNull()); // id should have been set during save operation.
    QVERIFY(!addedCollectionContacts.at(0).id().isNull()); // id should have been set during save operation.
    QVERIFY(!addedCollectionContacts.at(1).id().isNull()); // id should have been set during save operation.
    QCOMPARE(addedCollectionContacts.at(0).collectionId(), remoteAddressbook.id());
    QCOMPARE(addedCollectionContacts.at(1).collectionId(), remoteAddressbook.id());
    syncAlice = addedCollectionContacts.at(0);
    syncBob = addedCollectionContacts.at(1);

    // wait a while.  not necessary but for timestamp debugging purposes...
    QTest::qWait(250);

    // now perform some local modifications:
    // add a contact
    QContact syncCharlie;
    syncCharlie.setCollectionId(remoteAddressbook.id());
    QContactName cn;
    cn.setFirstName("Charlie");
    cn.setMiddleName("The");
    cn.setLastName("Horse");
    syncCharlie.saveDetail(&cn);
    QContactPhoneNumber cph;
    cph.setNumber("987656789");
    syncCharlie.saveDetail(&cph);
    QContactEmailAddress cem;
    cem.setEmailAddress("charlie@horse.tld");
    syncCharlie.saveDetail(&cem);
    QVERIFY(m_cm->saveContact(&syncCharlie));

    // delete a contact
    QVERIFY(m_cm->removeContact(syncBob.id()));

    // modify a contact
    syncAlice = m_cm->contact(syncAlice.id());
    aph = syncAlice.detail<QContactPhoneNumber>();
    aph.setNumber("111111111");
    QVERIFY(syncAlice.saveDetail(&aph));
    QVERIFY(m_cm->saveContact(&syncAlice));

    // now perform a second sync cycle.
    // first, retrieve local changes we need to push to remote server.
    QList<QContact> addedContacts;
    QList<QContact> modifiedContacts;
    QList<QContact> deletedContacts;
    QList<QContact> unmodifiedContacts;
    QVERIFY(cme->fetchContactChanges(
                remoteAddressbook.id(),
                &addedContacts,
                &modifiedContacts,
                &deletedContacts,
                &unmodifiedContacts,
                &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(addedContacts.count(), 1);
    QCOMPARE(modifiedContacts.count(), 1);
    QCOMPARE(deletedContacts.count(), 1);
    QCOMPARE(unmodifiedContacts.count(), 0);
    QCOMPARE(addedContacts.first().id(), syncCharlie.id());
    QCOMPARE(deletedContacts.first().id(), syncBob.id());
    QCOMPARE(modifiedContacts.first().id(), syncAlice.id());

    // at this point, Bob should have been marked as deleted,
    // and should not be accessible using the normal access API.
    QContact deletedBob = m_cm->contact(syncBob.id());
    QCOMPARE(m_cm->error(), QContactManager::DoesNotExistError);
    QVERIFY(deletedBob.id().isNull());

    // but we should still be able to access deleted Bob via specific filter.
    QContactCollectionFilter allCollections;
    QList<QContactId> deletedContactIds = m_cm->contactIds(allCollections & QContactStatusFlags::matchFlag(QContactStatusFlags::IsDeleted, QContactFilter::MatchContains));
    QCOMPARE(deletedContactIds.size(), 1);
    QVERIFY(deletedContactIds.contains(syncBob.id()));
    deletedContacts.clear();
    deletedContacts = m_cm->contacts(allCollections & QContactStatusFlags::matchFlag(QContactStatusFlags::IsDeleted, QContactFilter::MatchContains));
    QCOMPARE(deletedContacts.size(), 1);
    QCOMPARE(deletedContacts.first().detail<QContactPhoneNumber>().number(), QStringLiteral("543212345")); // Bob's phone number.

    // now fetch changes from the remote server, and calculate the delta.
    // in this case, we simulate that the user added a hobby on the remote server
    // for contact Alice, and deleted contact Charlie, and these changes need
    // to be stored to the local database.
    syncAlice = modifiedContacts.first();
    QContactHobby ah;
    ah.setHobby("Tennis");
    syncAlice.saveDetail(&ah);
    af = syncAlice.detail<QContactStatusFlags>();
    af.setFlag(QContactStatusFlags::IsModified, true);
    syncAlice.saveDetail(&af, QContact::IgnoreAccessConstraints);

    syncCharlie = addedContacts.first();
    QContactStatusFlags cf = syncCharlie.detail<QContactStatusFlags>();
    cf.setFlag(QContactStatusFlags::IsDeleted, true);
    syncCharlie.saveDetail(&cf, QContact::IgnoreAccessConstraints);

    // write the remote changes to the local database.
    additions.clear();
    modifications.clear();
    QList<QContact> modifiedCollectionContacts;
    modifiedCollectionContacts.append(syncAlice);   // modification
    modifiedCollectionContacts.append(syncCharlie); // deletion
    modifications.insert(&remoteAddressbook, &modifiedCollectionContacts);
    QVERIFY(cme->storeChanges(
            &additions,
            &modifications,
            QList<QContactCollectionId>(),
            policy, true, &err));

    // Alice should have been updated with the new hobby.
    // The other details should not have been changed.
    syncAlice = m_cm->contact(syncAlice.id());
    QCOMPARE(syncAlice.detail<QContactHobby>().hobby(), QStringLiteral("Tennis"));
    QCOMPARE(syncAlice.detail<QContactPhoneNumber>().number(), QStringLiteral("111111111"));

    // we should no longer be able to access the deleted contacts,
    // as the clearChangeFlags parameter was "true" in the above method call.
    deletedContactIds.clear();
    deletedContactIds = m_cm->contactIds(allCollections & QContactStatusFlags::matchFlag(QContactStatusFlags::IsDeleted, QContactFilter::MatchContains));
    QCOMPARE(deletedContactIds.size(), 0);

    // now perform another sync cycle.
    // there should be no local changes reported since the last clearChangeFlags()
    // (in this case, since the last storeChanges() call).
    addedContacts.clear();
    modifiedContacts.clear();
    deletedContacts.clear();
    unmodifiedContacts.clear();
    QVERIFY(cme->fetchContactChanges(
            remoteAddressbook.id(),
            &addedContacts,
            &modifiedContacts,
            &deletedContacts,
            &unmodifiedContacts,
            &err));
    QCOMPARE(addedContacts.size(), 0);
    QCOMPARE(modifiedContacts.size(), 0);
    QCOMPARE(deletedContacts.size(), 0);
    QCOMPARE(unmodifiedContacts.size(), 1);
    QCOMPARE(unmodifiedContacts.first().id(), syncAlice.id());
    syncAlice = unmodifiedContacts.first();

    // report remote deletion of the entire collection and store locally.
    additions.clear();
    modifications.clear();
    QVERIFY(cme->storeChanges(
            &additions,
            &modifications,
            QList<QContactCollectionId>() << remoteAddressbook.id(),
            policy, true, &err));

    // attempting to fetch the collection should fail
    QContactCollection deletedCollection = m_cm->collection(remoteAddressbook.id());
    QCOMPARE(m_cm->error(), QContactManager::DoesNotExistError);
    QVERIFY(deletedCollection.id().isNull());

    // attempting to fetch deleted contacts should return no results.
    // the deletion of the contacts as a result of the deletion of the collection
    // will in this case be applied immediately (and purged) due to the
    // clearChangeFlags=true parameter to the above storeChanges() call.
    deletedContactIds.clear();
    deletedContactIds = m_cm->contactIds(allCollections & QContactStatusFlags::matchFlag(QContactStatusFlags::IsDeleted, QContactFilter::MatchContains));
    QCOMPARE(deletedContactIds.size(), 0);
}

void tst_synctransactions::singleCollection_unhandledChanges()
{
    QtContactsSqliteExtensions::ContactManagerEngine *cme = QtContactsSqliteExtensions::contactManagerEngine(*m_cm);
    QContactManager::Error err = QContactManager::NoError;

    QHash<QContactCollection*, QList<QContact> *> additions;
    QHash<QContactCollection*, QList<QContact> *> modifications;

    QContactCollection remoteAddressbook;
    remoteAddressbook.setMetaData(QContactCollection::KeyName, QStringLiteral("test"));
    remoteAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, "tst_synctransactions");
    remoteAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID, 5);
    remoteAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH, "/addressbooks/test");

    QContact syncAlice;
    QContactName an;
    an.setFirstName("Alice");
    an.setMiddleName("In");
    an.setLastName("Wonderland");
    syncAlice.saveDetail(&an);
    QContactPhoneNumber aph;
    aph.setNumber("123454321");
    syncAlice.saveDetail(&aph);
    QContactEmailAddress aem;
    aem.setEmailAddress("alice@wonderland.tld");
    syncAlice.saveDetail(&aem);
    QContactStatusFlags af;
    af.setFlag(QContactStatusFlags::IsAdded, true);
    syncAlice.saveDetail(&af);

    QContact syncBob;
    QContactName bn;
    bn.setFirstName("Bob");
    bn.setMiddleName("The");
    bn.setLastName("Constructor");
    syncBob.saveDetail(&bn);
    QContactPhoneNumber bph;
    bph.setNumber("543212345");
    syncBob.saveDetail(&bph);
    QContactEmailAddress bem;
    bem.setEmailAddress("bob@construction.tld");
    syncBob.saveDetail(&bem);
    QContactStatusFlags bf;
    bf.setFlag(QContactStatusFlags::IsAdded, true);
    syncBob.saveDetail(&bf);

    QList<QContact> addedCollectionContacts;
    addedCollectionContacts.append(syncAlice);
    addedCollectionContacts.append(syncBob);
    additions.insert(&remoteAddressbook, &addedCollectionContacts);

    const QtContactsSqliteExtensions::ContactManagerEngine::ConflictResolutionPolicy policy(
            QtContactsSqliteExtensions::ContactManagerEngine::PreserveLocalChanges);

    // initial sync cycle: remote has a non-empty addressbook.
    QVERIFY(cme->storeChanges(
            &additions,
            &modifications,
            QList<QContactCollectionId>(),
            policy, true, &err));
    QCOMPARE(err, QContactManager::NoError);

    QVERIFY(!remoteAddressbook.id().isNull()); // id should have been set during save operation.
    QVERIFY(!addedCollectionContacts.at(0).id().isNull()); // id should have been set during save operation.
    QVERIFY(!addedCollectionContacts.at(1).id().isNull()); // id should have been set during save operation.
    QCOMPARE(addedCollectionContacts.at(0).collectionId(), remoteAddressbook.id());
    QCOMPARE(addedCollectionContacts.at(1).collectionId(), remoteAddressbook.id());
    syncAlice = addedCollectionContacts.at(0);
    syncBob = addedCollectionContacts.at(1);

    // wait a while.  not necessary but for timestamp debugging purposes...
    QTest::qWait(250);

    // now perform a local modification:
    // add a contact
    QContact syncCharlie;
    syncCharlie.setCollectionId(remoteAddressbook.id());
    QContactName cn;
    cn.setFirstName("Charlie");
    cn.setMiddleName("The");
    cn.setLastName("Horse");
    syncCharlie.saveDetail(&cn);
    QContactPhoneNumber cph;
    cph.setNumber("987656789");
    syncCharlie.saveDetail(&cph);
    QContactEmailAddress cem;
    cem.setEmailAddress("charlie@horse.tld");
    syncCharlie.saveDetail(&cem);
    QVERIFY(m_cm->saveContact(&syncCharlie));

    // now begin a new sync cycle.  fetch local changes for push to remote server.
    // this should report the local addition of the Charlie contact.
    QList<QContact> addedContacts;
    QList<QContact> modifiedContacts;
    QList<QContact> deletedContacts;
    QList<QContact> unmodifiedContacts;
    QVERIFY(cme->fetchContactChanges(
                remoteAddressbook.id(),
                &addedContacts,
                &modifiedContacts,
                &deletedContacts,
                &unmodifiedContacts,
                &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(addedContacts.count(), 1);
    QCOMPARE(modifiedContacts.count(), 0);
    QCOMPARE(deletedContacts.count(), 0);
    QCOMPARE(unmodifiedContacts.count(), 2);
    QCOMPARE(unmodifiedContacts.at(0).id(), syncAlice.id());
    QCOMPARE(unmodifiedContacts.at(1).id(), syncBob.id());
    QCOMPARE(addedContacts.first().id(), syncCharlie.id());
    syncAlice = unmodifiedContacts.at(0);
    syncBob = unmodifiedContacts.at(1);
    syncCharlie = addedContacts.first();

    // now we simulate the case where:
    // while the sync plugin is upsyncing the local addition to the remote server,
    // the device user modifies another contact locally.  This modification is
    // "unhandled" in the current sync cycle, as the sync plugin doesn't know that
    // this change exists, yet.
    syncAlice = m_cm->contact(syncAlice.id());
    aph = syncAlice.detail<QContactPhoneNumber>();
    aph.setNumber("111111111");
    QVERIFY(syncAlice.saveDetail(&aph));
    QContactHobby ah = syncAlice.detail<QContactHobby>();
    ah.setHobby("Tennis");
    QVERIFY(syncAlice.saveDetail(&ah));
    aem = syncAlice.detail<QContactEmailAddress>();
    QVERIFY(syncAlice.removeDetail(&aem));
    QVERIFY(m_cm->saveContact(&syncAlice));

    // now the sync plugin has successfully upsynced the local addition change.
    // it now downsyncs the remote change: deletion of Bob.
    bf = syncBob.detail<QContactStatusFlags>();
    bf.setFlag(QContactStatusFlags::IsAdded, false);
    bf.setFlag(QContactStatusFlags::IsDeleted, true);
    syncBob.saveDetail(&bf, QContact::IgnoreAccessConstraints);

    // write the remote changes to the local database.
    additions.clear();
    modifications.clear();
    QList<QContact> modifiedCollectionContacts;
    modifiedCollectionContacts.append(syncBob); // deletion
    modifications.insert(&remoteAddressbook, &modifiedCollectionContacts);
    QVERIFY(cme->storeChanges(
            &additions,
            &modifications,
            QList<QContactCollectionId>(),
            policy, true, &err));
    QCOMPARE(err, QContactManager::NoError);

    // the previous sync cycle is completed.
    // now ensure that the previously unhandled change is reported
    // during the next sync cycle.
    addedContacts.clear();
    modifiedContacts.clear();
    deletedContacts.clear();
    unmodifiedContacts.clear();
    QVERIFY(cme->fetchContactChanges(
                remoteAddressbook.id(),
                &addedContacts,
                &modifiedContacts,
                &deletedContacts,
                &unmodifiedContacts,
                &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(addedContacts.count(), 0);
    QCOMPARE(modifiedContacts.count(), 1);
    QCOMPARE(deletedContacts.count(), 0);
    QCOMPARE(unmodifiedContacts.count(), 1);
    QCOMPARE(modifiedContacts.first().id(), syncAlice.id());
    QCOMPARE(unmodifiedContacts.first().id(), syncCharlie.id());

    // ensure the specific changes are reported.
    syncAlice = modifiedContacts.first();
    QCOMPARE(syncAlice.detail<QContactHobby>().hobby(), ah.hobby());
    QVERIFY(syncAlice.detail<QContactHobby>().value(QContactDetail__FieldChangeFlags).toInt() & QContactDetail__ChangeFlag_IsAdded);
    QCOMPARE(syncAlice.detail<QContactPhoneNumber>().number(), aph.number());
    QVERIFY(syncAlice.detail<QContactPhoneNumber>().value(QContactDetail__FieldChangeFlags).toInt() & QContactDetail__ChangeFlag_IsModified);
    QVERIFY(syncAlice.detail<QContactEmailAddress>().value(QContactDetail__FieldChangeFlags).toInt() & QContactDetail__ChangeFlag_IsDeleted);

    // clean up.
    QVERIFY(m_cm->removeCollection(remoteAddressbook.id()));
    QVERIFY(cme->clearChangeFlags(remoteAddressbook.id(), &err));
}

void tst_synctransactions::multipleCollections()
{
    QtContactsSqliteExtensions::ContactManagerEngine *cme = QtContactsSqliteExtensions::contactManagerEngine(*m_cm);
    QContactManager::Error err = QContactManager::NoError;

    QHash<QContactCollection*, QList<QContact> *> additions;
    QHash<QContactCollection*, QList<QContact> *> modifications;

    QContactCollection remoteAddressbook;
    remoteAddressbook.setMetaData(QContactCollection::KeyName, QStringLiteral("test"));
    remoteAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, "tst_synctransactions");
    remoteAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID, 5);
    remoteAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH, "/addressbooks/test");

    QContactCollection anotherAddressbook;
    anotherAddressbook.setMetaData(QContactCollection::KeyName, QStringLiteral("another"));
    anotherAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, "tst_synctransactions");
    anotherAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID, 5);
    anotherAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH, "/addressbooks/another");

    QContact syncAlice;
    QContactName an;
    an.setFirstName("Alice");
    an.setMiddleName("In");
    an.setLastName("Wonderland");
    syncAlice.saveDetail(&an);
    QContactPhoneNumber aph;
    aph.setNumber("123454321");
    syncAlice.saveDetail(&aph);
    QContactEmailAddress aem;
    aem.setEmailAddress("alice@wonderland.tld");
    syncAlice.saveDetail(&aem);
    QContactStatusFlags af;
    af.setFlag(QContactStatusFlags::IsAdded, true);
    syncAlice.saveDetail(&af);

    QContact syncBob;
    QContactName bn;
    bn.setFirstName("Bob");
    bn.setMiddleName("The");
    bn.setLastName("Constructor");
    syncBob.saveDetail(&bn);
    QContactPhoneNumber bph;
    bph.setNumber("543212345");
    syncBob.saveDetail(&bph);
    QContactEmailAddress bem;
    bem.setEmailAddress("bob@construction.tld");
    syncBob.saveDetail(&bem);
    QContactStatusFlags bf;
    bf.setFlag(QContactStatusFlags::IsAdded, true);
    syncBob.saveDetail(&bf);

    QList<QContact> addedCollectionContacts;
    addedCollectionContacts.append(syncAlice);
    addedCollectionContacts.append(syncBob);
    additions.insert(&remoteAddressbook, &addedCollectionContacts);

    QList<QContact> emptyCollectionContacts;
    additions.insert(&anotherAddressbook, &emptyCollectionContacts);

    const QtContactsSqliteExtensions::ContactManagerEngine::ConflictResolutionPolicy policy(
            QtContactsSqliteExtensions::ContactManagerEngine::PreserveLocalChanges);

    // initial sync cycle: remote has a non-empty addressbook.
    QVERIFY(cme->storeChanges(
            &additions,
            &modifications,
            QList<QContactCollectionId>(),
            policy, true, &err));
    QCOMPARE(err, QContactManager::NoError);

    QVERIFY(!remoteAddressbook.id().isNull()); // id should have been set during save operation.
    QVERIFY(!anotherAddressbook.id().isNull()); // id should have been set during save operation.
    QVERIFY(!addedCollectionContacts.at(0).id().isNull()); // id should have been set during save operation.
    QVERIFY(!addedCollectionContacts.at(1).id().isNull()); // id should have been set during save operation.
    QCOMPARE(addedCollectionContacts.at(0).collectionId(), remoteAddressbook.id());
    QCOMPARE(addedCollectionContacts.at(1).collectionId(), remoteAddressbook.id());
    syncAlice = addedCollectionContacts.at(0);
    syncBob = addedCollectionContacts.at(1);

    // wait a while.  not necessary but for timestamp debugging purposes...
    QTest::qWait(250);

    // modify an addressbook locally.
    anotherAddressbook.setMetaData(QContactCollection::KeyDescription, QStringLiteral("another test addressbook"));
    QVERIFY(m_cm->saveCollection(&anotherAddressbook));

    // and add a contact to it locally.
    QContact syncCharlie;
    syncCharlie.setCollectionId(anotherAddressbook.id());
    QContactName cn;
    cn.setFirstName("Charlie");
    cn.setMiddleName("The");
    cn.setLastName("Horse");
    syncCharlie.saveDetail(&cn);
    QContactPhoneNumber cph;
    cph.setNumber("987656789");
    syncCharlie.saveDetail(&cph);
    QContactEmailAddress cem;
    cem.setEmailAddress("charlie@horse.tld");
    syncCharlie.saveDetail(&cem);
    QVERIFY(m_cm->saveContact(&syncCharlie));

    // also simulate a local deletion of a contact in the other addressbook.
    QVERIFY(m_cm->removeContact(syncBob.id()));

    // begin a new sync cycle
    // first, fetch local collection changes using the sync API.
    // note that the remoteAddressbook will be reported as unmodified
    // even though its content changed, as this API only reports
    // changes to collection metadata.
    QList<QContactCollection> addedCollections;
    QList<QContactCollection> modifiedCollections;
    QList<QContactCollection> deletedCollections;
    QList<QContactCollection> unmodifiedCollections;
    QVERIFY(cme->fetchCollectionChanges(
            5, QString(), // should be able to fetch by accountId
            &addedCollections,
            &modifiedCollections,
            &deletedCollections,
            &unmodifiedCollections,
            &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(addedCollections.size(), 0);
    QCOMPARE(modifiedCollections.size(), 1);
    QCOMPARE(deletedCollections.size(), 0);
    QCOMPARE(unmodifiedCollections.size(), 1);
    QCOMPARE(modifiedCollections.at(0).id(), anotherAddressbook.id());
    QCOMPARE(unmodifiedCollections.at(0).id(), remoteAddressbook.id());

    // then fetch local contact changes within each collection.
    QList<QContact> addedContacts;
    QList<QContact> modifiedContacts;
    QList<QContact> deletedContacts;
    QList<QContact> unmodifiedContacts;
    QVERIFY(cme->fetchContactChanges(
                remoteAddressbook.id(),
                &addedContacts,
                &modifiedContacts,
                &deletedContacts,
                &unmodifiedContacts,
                &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(addedContacts.count(), 0);
    QCOMPARE(modifiedContacts.count(), 0);
    QCOMPARE(deletedContacts.count(), 1);
    QCOMPARE(unmodifiedContacts.count(), 1);
    QCOMPARE(deletedContacts.at(0).id(), syncBob.id());
    QCOMPARE(unmodifiedContacts.at(0).id(), syncAlice.id());

    addedContacts.clear();
    modifiedContacts.clear();
    deletedContacts.clear();
    unmodifiedContacts.clear();
    QVERIFY(cme->fetchContactChanges(
                anotherAddressbook.id(),
                &addedContacts,
                &modifiedContacts,
                &deletedContacts,
                &unmodifiedContacts,
                &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(addedContacts.count(), 1);
    QCOMPARE(modifiedContacts.count(), 0);
    QCOMPARE(deletedContacts.count(), 0);
    QCOMPARE(unmodifiedContacts.count(), 0);
    QCOMPARE(addedContacts.at(0).id(), syncCharlie.id());

    // note: performing that operation multiple times should return the same results,
    // as fetching changes should not clear any change flags which are set.
    addedContacts.clear();
    modifiedContacts.clear();
    deletedContacts.clear();
    unmodifiedContacts.clear();
    QVERIFY(cme->fetchContactChanges(
                remoteAddressbook.id(),
                &addedContacts,
                &modifiedContacts,
                &deletedContacts,
                &unmodifiedContacts,
                &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(addedContacts.count(), 0);
    QCOMPARE(modifiedContacts.count(), 0);
    QCOMPARE(deletedContacts.count(), 1);
    QCOMPARE(unmodifiedContacts.count(), 1);
    QCOMPARE(deletedContacts.at(0).id(), syncBob.id());
    QCOMPARE(unmodifiedContacts.at(0).id(), syncAlice.id());

    addedContacts.clear();
    modifiedContacts.clear();
    deletedContacts.clear();
    unmodifiedContacts.clear();
    QVERIFY(cme->fetchContactChanges(
                anotherAddressbook.id(),
                &addedContacts,
                &modifiedContacts,
                &deletedContacts,
                &unmodifiedContacts,
                &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(addedContacts.count(), 1);
    QCOMPARE(modifiedContacts.count(), 0);
    QCOMPARE(deletedContacts.count(), 0);
    QCOMPARE(unmodifiedContacts.count(), 0);
    QCOMPARE(addedContacts.at(0).id(), syncCharlie.id());

    // finally, simulate storing remote changes to the local database.
    // in this simulated sync cycle, no remote changes occurred, so just clear the change flags
    // for the two synced addressbooks.  This should also purge the deleted Bob contact.
    QVERIFY(cme->clearChangeFlags(anotherAddressbook.id(), &err));
    QCOMPARE(err, QContactManager::NoError);
    QVERIFY(cme->clearChangeFlags(remoteAddressbook.id(), &err));
    QCOMPARE(err, QContactManager::NoError);

    // now simulate local deletion of the anotherAddressbook.
    QVERIFY(m_cm->removeCollection(anotherAddressbook.id()));

    // the contact within that collection should be marked as deleted
    // and thus not retrievable using the normal API unless the specific
    // IsDeleted filter is set.
    QContact deletedContact = m_cm->contact(syncCharlie.id());
    QCOMPARE(m_cm->error(), QContactManager::DoesNotExistError);
    QVERIFY(deletedContact.id().isNull());
    QContactIdFilter idFilter;
    idFilter.setIds(QList<QContactId>() << syncCharlie.id());
    deletedContacts = m_cm->contacts(idFilter & QContactStatusFlags::matchFlag(QContactStatusFlags::IsDeleted, QContactFilter::MatchContains));
    QCOMPARE(deletedContacts.size(), 1);
    QCOMPARE(deletedContacts.at(0).id(), syncCharlie.id());
    QCOMPARE(deletedContacts.at(0).detail<QContactPhoneNumber>().number(), syncCharlie.detail<QContactPhoneNumber>().number());
    QContactCollectionFilter allCollections;
    deletedContacts = m_cm->contacts(allCollections & QContactStatusFlags::matchFlag(QContactStatusFlags::IsDeleted, QContactFilter::MatchContains));
    QCOMPARE(deletedContacts.size(), 1); // should not include Bob, who should have been purged due to clearChangeFlags().
    QCOMPARE(deletedContacts.at(0).id(), syncCharlie.id());

    // now simulate another sync cycle.
    // step one: get local collection changes.
    addedCollections.clear();
    modifiedCollections.clear();
    deletedCollections.clear();
    unmodifiedCollections.clear();
    QVERIFY(cme->fetchCollectionChanges(
            0, QStringLiteral("tst_synctransactions"), // should be able to fetch by applicationName
            &addedCollections,
            &modifiedCollections,
            &deletedCollections,
            &unmodifiedCollections,
            &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(addedCollections.size(), 0);
    QCOMPARE(modifiedCollections.size(), 0);
    QCOMPARE(deletedCollections.size(), 1);
    QCOMPARE(unmodifiedCollections.size(), 1);
    QCOMPARE(deletedCollections.at(0).id(), anotherAddressbook.id());
    QCOMPARE(unmodifiedCollections.at(0).id(), remoteAddressbook.id());

    // step two: get local contact changes.
    addedContacts.clear();
    modifiedContacts.clear();
    deletedContacts.clear();
    unmodifiedContacts.clear();
    QVERIFY(cme->fetchContactChanges(
                remoteAddressbook.id(),
                &addedContacts,
                &modifiedContacts,
                &deletedContacts,
                &unmodifiedContacts,
                &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(addedContacts.count(), 0);
    QCOMPARE(modifiedContacts.count(), 0);
    QCOMPARE(deletedContacts.count(), 0);
    QCOMPARE(unmodifiedContacts.count(), 1);
    QCOMPARE(unmodifiedContacts.at(0).id(), syncAlice.id());
    syncAlice = unmodifiedContacts.at(0);

    addedContacts.clear();
    modifiedContacts.clear();
    deletedContacts.clear();
    unmodifiedContacts.clear();
    QVERIFY(cme->fetchContactChanges(
                anotherAddressbook.id(),
                &addedContacts,
                &modifiedContacts,
                &deletedContacts,
                &unmodifiedContacts,
                &err));
    QCOMPARE(err, QContactManager::NoError);
    QCOMPARE(addedContacts.count(), 0);
    QCOMPARE(modifiedContacts.count(), 0);
    QCOMPARE(deletedContacts.count(), 1);
    QCOMPARE(unmodifiedContacts.count(), 0);
    QCOMPARE(deletedContacts.at(0).id(), syncCharlie.id());

    // step three: store remote changes to local database.
    QContactHobby ah;
    ah.setHobby("Tennis");
    QVERIFY(syncAlice.saveDetail(&ah));
    af = syncAlice.detail<QContactStatusFlags>();
    af.setFlag(QContactStatusFlags::IsAdded, false);
    af.setFlag(QContactStatusFlags::IsModified, true);
    QVERIFY(syncAlice.saveDetail(&af, QContact::IgnoreAccessConstraints));

    additions.clear();
    modifications.clear();
    QList<QContact> modifiedCollectionContacts;
    modifiedCollectionContacts.append(syncAlice);
    modifications.insert(&remoteAddressbook, &modifiedCollectionContacts);
    QVERIFY(cme->storeChanges(
            &additions,
            &modifications,
            QList<QContactCollectionId>(),
            policy, true, &err));
    QCOMPARE(err, QContactManager::NoError);
    QVERIFY(cme->clearChangeFlags(anotherAddressbook.id(), &err));
    QCOMPARE(err, QContactManager::NoError);

    // the above operations should have cleared change flags, causing purge of Charlie etc.
    deletedContacts = m_cm->contacts(allCollections & QContactStatusFlags::matchFlag(QContactStatusFlags::IsDeleted, QContactFilter::MatchContains));
    QCOMPARE(deletedContacts.size(), 0);

    // clean up.
    QVERIFY(m_cm->removeCollection(remoteAddressbook.id()));
    QVERIFY(cme->clearChangeFlags(remoteAddressbook.id(), &err));
}

void tst_synctransactions::syncRequests()
{
    // Now test that the sync transaction request classes work properly.
    // This test does the same that the singleCollection_multipleCycles()
    // test does, but with requests rather than raw engine calls.
    QContactCollectionFilter allCollections;
    QContactCollectionId remoteAddressbookId;
    QContactId aliceId, bobId, charlieId;

    {
        QContactCollection remoteAddressbook;
        remoteAddressbook.setMetaData(QContactCollection::KeyName, QStringLiteral("test"));
        remoteAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, "tst_synctransactions");
        remoteAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID, 5);
        remoteAddressbook.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH, "/addressbooks/test");

        QContact syncAlice;
        QContactName an;
        an.setFirstName("Alice");
        an.setMiddleName("In");
        an.setLastName("Wonderland");
        syncAlice.saveDetail(&an);
        QContactPhoneNumber aph;
        aph.setNumber("123454321");
        syncAlice.saveDetail(&aph);
        QContactEmailAddress aem;
        aem.setEmailAddress("alice@wonderland.tld");
        syncAlice.saveDetail(&aem);
        QContactStatusFlags af;
        af.setFlag(QContactStatusFlags::IsAdded, true);
        syncAlice.saveDetail(&af);

        QContact syncBob;
        QContactName bn;
        bn.setFirstName("Bob");
        bn.setMiddleName("The");
        bn.setLastName("Constructor");
        syncBob.saveDetail(&bn);
        QContactPhoneNumber bph;
        bph.setNumber("543212345");
        syncBob.saveDetail(&bph);
        QContactEmailAddress bem;
        bem.setEmailAddress("bob@construction.tld");
        syncBob.saveDetail(&bem);
        QContactStatusFlags bf;
        bf.setFlag(QContactStatusFlags::IsAdded, true);
        syncBob.saveDetail(&bf);

        QList<QContact> addedCollectionContacts;
        addedCollectionContacts.append(syncAlice);
        addedCollectionContacts.append(syncBob);
        QHash<QContactCollection, QList<QContact> > additions;
        additions.insert(remoteAddressbook, addedCollectionContacts);

        QContactChangesSaveRequest *csr = new QContactChangesSaveRequest;
        csr->setManager(m_cm);
        csr->setAddedCollections(additions);
        csr->setClearChangeFlags(true);
        csr->start();
        QVERIFY(csr->waitForFinished(5000));
        QCOMPARE(csr->error(), QContactManager::NoError);

        // ensure that the values have been updated as a result of the operation
        // e.g. to include ids etc.
        remoteAddressbookId = csr->addedCollections().keys().first().id();
        QVERIFY(!remoteAddressbookId.isNull());
        aliceId = csr->addedCollections().value(csr->addedCollections().keys().first()).first().id();
        bobId = csr->addedCollections().value(csr->addedCollections().keys().first()).last().id();
        QVERIFY(!aliceId.isNull());
        QVERIFY(!bobId.isNull());
        QVERIFY(aliceId != bobId);

        QContact reloadAlice = m_cm->contact(aliceId);
        QCOMPARE(m_cm->error(), QContactManager::NoError);
        QCOMPARE(reloadAlice.detail<QContactPhoneNumber>().number(), syncAlice.detail<QContactPhoneNumber>().number());
        QCOMPARE(reloadAlice.detail<QContactEmailAddress>().emailAddress(), syncAlice.detail<QContactEmailAddress>().emailAddress());

        QContact reloadBob = m_cm->contact(bobId);
        QCOMPARE(m_cm->error(), QContactManager::NoError);
        QCOMPARE(reloadBob.detail<QContactPhoneNumber>().number(), syncBob.detail<QContactPhoneNumber>().number());
        QCOMPARE(reloadBob.detail<QContactEmailAddress>().emailAddress(), syncBob.detail<QContactEmailAddress>().emailAddress());
    }

    {
        // now perform some local modifications:
        // add a contact
        QContact syncCharlie;
        syncCharlie.setCollectionId(remoteAddressbookId);
        QContactName cn;
        cn.setFirstName("Charlie");
        cn.setMiddleName("The");
        cn.setLastName("Horse");
        syncCharlie.saveDetail(&cn);
        QContactPhoneNumber cph;
        cph.setNumber("987656789");
        syncCharlie.saveDetail(&cph);
        QContactEmailAddress cem;
        cem.setEmailAddress("charlie@horse.tld");
        syncCharlie.saveDetail(&cem);
        QVERIFY(m_cm->saveContact(&syncCharlie));
        charlieId = syncCharlie.id();

        // delete a contact
        QVERIFY(m_cm->removeContact(bobId));

        // modify a contact
        QContact syncAlice = m_cm->contact(aliceId);
        QContactPhoneNumber aph = syncAlice.detail<QContactPhoneNumber>();
        aph.setNumber("111111111");
        QVERIFY(syncAlice.saveDetail(&aph));
        QVERIFY(m_cm->saveContact(&syncAlice));
    }

    {
        // now perform a second sync cycle.
        // first, retrieve local collection metadata changes we need to push to remote server.
        QContactCollectionChangesFetchRequest *ccfr = new QContactCollectionChangesFetchRequest;
        ccfr->setManager(m_cm);
        ccfr->setApplicationName(QStringLiteral("tst_synctransactions"));
        ccfr->start();
        QVERIFY(ccfr->waitForFinished(5000));
        QCOMPARE(ccfr->error(), QContactManager::NoError);
        QVERIFY(ccfr->addedCollections().isEmpty());
        QVERIFY(ccfr->modifiedCollections().isEmpty());
        QVERIFY(ccfr->removedCollections().isEmpty());
        QCOMPARE(ccfr->unmodifiedCollections().size(), 1);
        QCOMPARE(ccfr->unmodifiedCollections().first().id(), remoteAddressbookId);

        // second, retrieve local contact changes we need to push to the remote server.
        QContactChangesFetchRequest *cfr = new QContactChangesFetchRequest;
        cfr->setManager(m_cm);
        cfr->setCollectionId(remoteAddressbookId);
        cfr->start();
        QVERIFY(cfr->waitForFinished(5000));
        QCOMPARE(cfr->error(), QContactManager::NoError);
        QCOMPARE(cfr->addedContacts().size(), 1);
        QCOMPARE(cfr->addedContacts().first().id(), charlieId);
        QCOMPARE(cfr->modifiedContacts().size(), 1);
        QCOMPARE(cfr->modifiedContacts().first().id(), aliceId);
        QCOMPARE(cfr->removedContacts().size(), 1);
        QCOMPARE(cfr->removedContacts().first().id(), bobId);
        QCOMPARE(cfr->unmodifiedContacts().size(), 0);

        // at this point, Bob should have been marked as deleted,
        // and should not be accessible using the normal access API.
        QContact deletedBob = m_cm->contact(bobId);
        QCOMPARE(m_cm->error(), QContactManager::DoesNotExistError);
        QVERIFY(deletedBob.id().isNull());
        // but we should still be able to access deleted Bob via specific filter.
        QList<QContactId> deletedContactIds = m_cm->contactIds(allCollections & QContactStatusFlags::matchFlag(QContactStatusFlags::IsDeleted, QContactFilter::MatchContains));
        QCOMPARE(deletedContactIds.size(), 1);
        QVERIFY(deletedContactIds.contains(bobId));
        QList<QContact> deletedContacts = m_cm->contacts(allCollections & QContactStatusFlags::matchFlag(QContactStatusFlags::IsDeleted, QContactFilter::MatchContains));
        QCOMPARE(deletedContacts.size(), 1);
        QCOMPARE(deletedContacts.first().detail<QContactPhoneNumber>().number(), QStringLiteral("543212345")); // Bob's phone number.

        // third, fetch changes from the remote server, and calculate the delta.
        // in this case, we simulate that the user added a hobby on the remote server
        // for contact Alice, and deleted contact Charlie, and these changes need
        // to be stored to the local database.
        QContact syncAlice = cfr->modifiedContacts().first();
        QContactHobby ah;
        ah.setHobby("Tennis");
        syncAlice.saveDetail(&ah);
        QContactStatusFlags af = syncAlice.detail<QContactStatusFlags>();
        af.setFlag(QContactStatusFlags::IsModified, true);
        syncAlice.saveDetail(&af, QContact::IgnoreAccessConstraints);

        QContact syncCharlie = cfr->addedContacts().first();
        QContactStatusFlags cf = syncCharlie.detail<QContactStatusFlags>();
        cf.setFlag(QContactStatusFlags::IsDeleted, true);
        syncCharlie.saveDetail(&cf, QContact::IgnoreAccessConstraints);

        QContactCollection remoteAddressbook = m_cm->collection(remoteAddressbookId);
        QHash<QContactCollection, QList<QContact> > modifications;
        modifications.insert(remoteAddressbook, QList<QContact>() << syncAlice << syncCharlie);

        QContactChangesSaveRequest *csr = new QContactChangesSaveRequest;
        csr->setManager(m_cm);
        csr->setClearChangeFlags(true);
        csr->setModifiedCollections(modifications);
        csr->start();
        QVERIFY(csr->waitForFinished(5000));
        QCOMPARE(csr->error(), QContactManager::NoError);

        // Alice should have been updated with the new hobby.
        // The other details should not have been changed.
        syncAlice = m_cm->contact(aliceId);
        QCOMPARE(syncAlice.detail<QContactHobby>().hobby(), QStringLiteral("Tennis"));
        QCOMPARE(syncAlice.detail<QContactPhoneNumber>().number(), QStringLiteral("111111111"));

        // we should no longer be able to access the deleted contacts,
        // as the clearChangeFlags parameter was "true" in the above request.
        deletedContactIds.clear();
        deletedContactIds = m_cm->contactIds(allCollections & QContactStatusFlags::matchFlag(QContactStatusFlags::IsDeleted, QContactFilter::MatchContains));
        QCOMPARE(deletedContactIds.size(), 0);
    }

    {
        // now perform another sync cycle.
        // there should be no local changes reported since the last clearChangeFlags()
        // (in this case, since the last storeChanges() call).
        // first, retrieve local collection metadata changes we need to push to remote server.
        QContactCollectionChangesFetchRequest *ccfr = new QContactCollectionChangesFetchRequest;
        ccfr->setManager(m_cm);
        ccfr->setApplicationName(QStringLiteral("tst_synctransactions"));
        ccfr->start();
        QVERIFY(ccfr->waitForFinished(5000));
        QCOMPARE(ccfr->error(), QContactManager::NoError);
        QVERIFY(ccfr->addedCollections().isEmpty());
        QVERIFY(ccfr->modifiedCollections().isEmpty());
        QVERIFY(ccfr->removedCollections().isEmpty());
        QCOMPARE(ccfr->unmodifiedCollections().size(), 1);
        QCOMPARE(ccfr->unmodifiedCollections().first().id(), remoteAddressbookId);

        // second, retrieve local contact changes we need to push to the remote server.
        QContactChangesFetchRequest *cfr = new QContactChangesFetchRequest;
        cfr->setManager(m_cm);
        cfr->setCollectionId(remoteAddressbookId);
        cfr->start();
        QVERIFY(cfr->waitForFinished(5000));
        QCOMPARE(cfr->error(), QContactManager::NoError);
        QCOMPARE(cfr->addedContacts().size(), 0);
        QCOMPARE(cfr->modifiedContacts().size(), 0);
        QCOMPARE(cfr->removedContacts().size(), 0);
        QCOMPARE(cfr->unmodifiedContacts().size(), 1);
        QCOMPARE(cfr->unmodifiedContacts().first().id(), aliceId);

        // third, report remote changes and store locally
        // in this case, we simulate remote deletion of the entire collection.
        QContactChangesSaveRequest *csr = new QContactChangesSaveRequest;
        csr->setManager(m_cm);
        csr->setClearChangeFlags(true);
        csr->setRemovedCollections(QList<QContactCollectionId>() << remoteAddressbookId);
        csr->start();
        QVERIFY(csr->waitForFinished(5000));
        QCOMPARE(csr->error(), QContactManager::NoError);

        // attempting to fetch the collection should fail
        QContactCollection deletedCollection = m_cm->collection(remoteAddressbookId);
        QCOMPARE(m_cm->error(), QContactManager::DoesNotExistError);
        QVERIFY(deletedCollection.id().isNull());

        // attempting to fetch deleted contacts should return no results.
        // the deletion of the contacts as a result of the deletion of the collection
        // will in this case be applied immediately (and purged) due to the
        // clearChangeFlags=true parameter to the above storeChanges() call.
        QList<QContactId> deletedContactIds = m_cm->contactIds(allCollections & QContactStatusFlags::matchFlag(QContactStatusFlags::IsDeleted, QContactFilter::MatchContains));
        QCOMPARE(deletedContactIds.size(), 0);
    }
}

QTEST_GUILESS_MAIN(tst_synctransactions)
#include "tst_synctransactions.moc"
