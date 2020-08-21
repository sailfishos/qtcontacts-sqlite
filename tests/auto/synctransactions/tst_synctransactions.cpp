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
#include "testsyncadaptor.h"
#include "qtcontacts-extensions.h"

#include "qcontactcollectionchangesfetchrequest.h"
#include "qcontactcollectionchangesfetchrequest_impl.h"
#include "qcontactchangesfetchrequest.h"
#include "qcontactchangesfetchrequest_impl.h"
#include "qcontactchangessaverequest.h"
#include "qcontactchangessaverequest_impl.h"
#include "qcontactclearchangeflagsrequest.h"
#include "qcontactclearchangeflagsrequest_impl.h"

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

    void twcsa_nodelta();
    void twcsa_delta();
    void twcsa_oneway();

private:
    void waitForSignalPropagation();

    QContactManager *m_cm;
    QSet<QContactCollectionId> m_createdColIds;
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
    QContactManager::Error err = QContactManager::NoError;
    QtContactsSqliteExtensions::ContactManagerEngine *cme = QtContactsSqliteExtensions::contactManagerEngine(*m_cm);

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

bool haveExpectedContent(const QContact &c, const QString &phone, TestSyncAdaptor::PhoneModifiability modifiability, const QString &email)
{
    const QContactPhoneNumber &phn(c.detail<QContactPhoneNumber>());

    TestSyncAdaptor::PhoneModifiability modif = TestSyncAdaptor::ImplicitlyModifiable;
    if (phn.values().contains(QContactDetail__FieldModifiable)) {
        modif = phn.value<bool>(QContactDetail__FieldModifiable)
              ? TestSyncAdaptor::ExplicitlyModifiable
              : TestSyncAdaptor::ExplicitlyNonModifiable;
    }

    return phn.number() == phone && modif == modifiability
        && c.detail<QContactEmailAddress>().emailAddress() == email;
}

void tst_synctransactions::twcsa_nodelta()
{
    // construct a sync adaptor, and prefill its read-write collection with 3 contacts.
    const int accountId = 3;
    const QString applicationName = QStringLiteral("tst_synctransactions::twcsa_nodelta");
    TestSyncAdaptor tsa(accountId, applicationName, *m_cm);
    tsa.addRemoteContact("John", "One", "1111111", TestSyncAdaptor::ImplicitlyModifiable);
    tsa.addRemoteContact("Luke", "Two", "2222222", TestSyncAdaptor::ExplicitlyModifiable);
    tsa.addRemoteContact("Mark", "Three", "3333333", TestSyncAdaptor::ExplicitlyNonModifiable);

    // perform the initial sync cycle.
    QSignalSpy finishedSpy(&tsa, SIGNAL(finished()));
    QSignalSpy failedSpy(&tsa, SIGNAL(failed()));
    tsa.performTwoWaySync();
    QTRY_COMPARE(failedSpy.count() + finishedSpy.count(), 1);
    QTRY_COMPARE(finishedSpy.count(), 1);

    // should have 8 more contacts than we had before:
    // two built-in contacts from non-aggregable read-only collection,
    // and then 3 constituent contacts from the read-write collection, plus 3 aggregates.
    QContactCollectionFilter allCollections;
    QList<QContact> allContacts = m_cm->contacts(allCollections);
    QContactId aliceId, bobId;
    QContactId johnId, lukeId, markId;
    QContactId aggJohnId, aggLukeId, aggMarkId;
    for (int i = allContacts.size() - 1; i >= 0; --i) {
        const QContact &c(allContacts[i]);
        const bool isAggregate = c.relatedContacts(QStringLiteral("Aggregates"), QContactRelationship::Second).size() > 0;
        const QString fname = c.detail<QContactName>().firstName();
        if (!isAggregate && fname == QStringLiteral("Alice")) aliceId = c.id();
        if (isAggregate && fname == QStringLiteral("Alice")) QCOMPARE(isAggregate, false); // force failure.
        if (!isAggregate && fname == QStringLiteral("Bob")) bobId = c.id();
        if (isAggregate && fname == QStringLiteral("Bob")) QCOMPARE(isAggregate, false); // force failure.
        if (!isAggregate && fname == QStringLiteral("John")) johnId = c.id();
        if (isAggregate && fname == QStringLiteral("John")) aggJohnId = c.id();
        if (!isAggregate && fname == QStringLiteral("Luke")) lukeId = c.id();
        if (isAggregate && fname == QStringLiteral("Luke")) aggLukeId = c.id();
        if (!isAggregate && fname == QStringLiteral("Mark")) markId = c.id();
        if (isAggregate && fname == QStringLiteral("Mark")) aggMarkId = c.id();
    }
    QCOMPARE(allContacts.size(), 8);
    QVERIFY(aliceId != QContactId());
    QVERIFY(bobId != QContactId());
    QVERIFY(johnId != QContactId());
    QVERIFY(lukeId != QContactId());
    QVERIFY(markId != QContactId());
    QVERIFY(aggJohnId != QContactId());
    QVERIFY(aggLukeId != QContactId());
    QVERIFY(aggMarkId != QContactId());

    // ensure that the collections themselves have been downsynced
    QContactCollection emptyCollection, readonlyCollection, readwriteCollection;
    QString emptyCollectionCtag, readonlyCollectionCtag, readwriteCollectionCtag;
    for (const QContactCollection &c : m_cm->collections()) {
        const int collectionAccountId = c.extendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID).toInt();
        const QString collectionAppName = c.extendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME).toString();
        const QString collectionName = c.metaData(QContactCollection::KeyName).toString();
        if (collectionAccountId == accountId && collectionAppName == applicationName) {
            if (collectionName == QStringLiteral("Empty")) {
                emptyCollection = c;
                emptyCollectionCtag = c.extendedMetaData(QStringLiteral("ctag")).toString();
            } else if (collectionName == QStringLiteral("ReadOnly")) {
                readonlyCollection = c;
                readonlyCollectionCtag = c.extendedMetaData(QStringLiteral("ctag")).toString();
            } else {
                QCOMPARE(collectionName, QStringLiteral("ReadWrite"));
                readwriteCollection = c;
                readwriteCollectionCtag = c.extendedMetaData(QStringLiteral("ctag")).toString();
            }
        }
    }
    QVERIFY(!emptyCollection.id().isNull());
    QVERIFY(!readonlyCollection.id().isNull());
    QVERIFY(!readwriteCollection.id().isNull());
    QVERIFY(!emptyCollectionCtag.isEmpty());
    QVERIFY(!readonlyCollectionCtag.isEmpty());
    QVERIFY(!readwriteCollectionCtag.isEmpty());

    // ensure that the downsynced contacts have the data we expect
    // note that aggregate contact details are explicitly non-modifiable always.
    QVERIFY(haveExpectedContent(m_cm->contact(aliceId), QStringLiteral("123123123"), TestSyncAdaptor::ExplicitlyNonModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(bobId), QString(), TestSyncAdaptor::ImplicitlyModifiable, QStringLiteral("bob@constructor.tld")));
    QVERIFY(haveExpectedContent(m_cm->contact(johnId), QStringLiteral("1111111"), TestSyncAdaptor::ImplicitlyModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(aggJohnId), QStringLiteral("1111111"), TestSyncAdaptor::ExplicitlyNonModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(lukeId), QStringLiteral("2222222"), TestSyncAdaptor::ExplicitlyModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(aggLukeId), QStringLiteral("2222222"), TestSyncAdaptor::ExplicitlyNonModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(markId), QStringLiteral("3333333"), TestSyncAdaptor::ExplicitlyNonModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(aggMarkId), QStringLiteral("3333333"), TestSyncAdaptor::ExplicitlyNonModifiable, QString()));

    // and ensure they belong to the collections we expect
    QCOMPARE(m_cm->contact(aliceId).collectionId(), readonlyCollection.id());
    QCOMPARE(m_cm->contact(bobId).collectionId(), readonlyCollection.id());
    QCOMPARE(m_cm->contact(johnId).collectionId(), readwriteCollection.id());
    QCOMPARE(m_cm->contact(lukeId).collectionId(), readwriteCollection.id());
    QCOMPARE(m_cm->contact(markId).collectionId(), readwriteCollection.id());
    QCOMPARE(m_cm->contact(aggJohnId).collectionId(), QContactCollectionId(m_cm->managerUri(), aggregateAddressbookId()));
    QCOMPARE(m_cm->contact(aggLukeId).collectionId(), QContactCollectionId(m_cm->managerUri(), aggregateAddressbookId()));
    QCOMPARE(m_cm->contact(aggMarkId).collectionId(), QContactCollectionId(m_cm->managerUri(), aggregateAddressbookId()));

    // simulate a local addition and local modification
    QContact matthew;
    QContactName mn;
    mn.setFirstName(QStringLiteral("Matthew"));
    mn.setLastName(QStringLiteral("Four"));
    matthew.saveDetail(&mn);
    QContactPhoneNumber mp;
    mp.setNumber(QStringLiteral("4444444"));
    matthew.saveDetail(&mn);
    matthew.saveDetail(&mp);
    matthew.setCollectionId(readwriteCollection.id());
    QVERIFY(m_cm->saveContact(&matthew));
    QContact mark = m_cm->contact(markId);
    QContactEmailAddress me;
    me.setEmailAddress(QStringLiteral("mark@three.tld"));
    mark.saveDetail(&me);
    QVERIFY(m_cm->saveContact(&mark));

    // simulate a remote modification, and a remote deletion.
    tsa.changeRemoteContactPhone(QStringLiteral("John"), QStringLiteral("One"), QStringLiteral("1111123"));
    tsa.removeRemoteContact(QStringLiteral("Luke"), QStringLiteral("Two"));

    // now perform another sync cycle
    tsa.performTwoWaySync();
    QTRY_COMPARE(failedSpy.count() + finishedSpy.count(), 2);
    QTRY_COMPARE(finishedSpy.count(), 2);

    // ensure that the local database now contains the appropriate information
    allContacts = m_cm->contacts(allCollections);
    QContactId matthewId, aggMatthewId;
    for (int i = allContacts.size() - 1; i >= 0; --i) {
        const QContact &c(allContacts[i]);
        const bool isAggregate = c.relatedContacts(QStringLiteral("Aggregates"), QContactRelationship::Second).size() > 0;
        const QString fname = c.detail<QContactName>().firstName();
        if (!isAggregate && fname == QStringLiteral("Alice")) QCOMPARE(c.id(), aliceId);         // id should not have changed
        else if (isAggregate && fname == QStringLiteral("Alice")) QCOMPARE(isAggregate, false);  // force failure.
        else if (!isAggregate && fname == QStringLiteral("Bob")) QCOMPARE(c.id(), bobId);        // id should not have changed
        else if (isAggregate && fname == QStringLiteral("Bob")) QCOMPARE(isAggregate, false);    // force failure.
        else if (!isAggregate && fname == QStringLiteral("John")) QCOMPARE(c.id(), johnId);      // id should not have changed
        else if (isAggregate && fname == QStringLiteral("John")) QCOMPARE(c.id(), aggJohnId);    // id should not have changed
        else if (!isAggregate && fname == QStringLiteral("Mark")) QCOMPARE(c.id(), markId);      // id should not have changed
        else if (isAggregate && fname == QStringLiteral("Mark")) QCOMPARE(c.id(), aggMarkId);    // id should not have changed
        else if (!isAggregate && fname == QStringLiteral("Matthew")) matthewId = c.id();
        else if (isAggregate && fname == QStringLiteral("Matthew")) aggMatthewId = c.id();
        else QCOMPARE(fname, QStringLiteral("Alice")); // force failure, unknown/deleted contact seen.
    }
    QCOMPARE(allContacts.size(), 8);
    QVERIFY(aggMarkId != QContactId());
    QVERIFY(aggMatthewId != QContactId());
    QCOMPARE(m_cm->contact(johnId).detail<QContactPhoneNumber>().number(), QStringLiteral("1111123"));
    QCOMPARE(m_cm->contact(aggJohnId).detail<QContactPhoneNumber>().number(), QStringLiteral("1111123"));
    QCOMPARE(m_cm->contact(johnId).details<QContactPhoneNumber>().size(), 1);
    QCOMPARE(m_cm->contact(aggJohnId).details<QContactPhoneNumber>().size(), 1);

    // check the collections are available and that the ctag of the readwrite collection has changed.
    for (const QContactCollection &c : m_cm->collections()) {
        const int collectionAccountId = c.extendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID).toInt();
        const QString collectionAppName = c.extendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME).toString();
        const QString collectionName = c.metaData(QContactCollection::KeyName).toString();
        if (collectionAccountId == accountId && collectionAppName == applicationName) {
            if (collectionName == QStringLiteral("Empty")) {
                emptyCollection = c;
                QCOMPARE(c.extendedMetaData(QStringLiteral("ctag")).toString(), emptyCollectionCtag);
            } else if (collectionName == QStringLiteral("ReadOnly")) {
                readonlyCollection = c;
                QCOMPARE(c.extendedMetaData(QStringLiteral("ctag")).toString(), readonlyCollectionCtag);
            } else {
                QCOMPARE(collectionName, QStringLiteral("ReadWrite"));
                readwriteCollection = c;
                QVERIFY(c.extendedMetaData(QStringLiteral("ctag")).toString() != readwriteCollectionCtag);
                readwriteCollectionCtag = c.extendedMetaData(QStringLiteral("ctag")).toString();
            }
        }
    }

    // and ensure that the remote database contains the appropriate information
    QContact remoteMatthew = tsa.remoteContact(QStringLiteral("Matthew"), QStringLiteral("Four"));
    QCOMPARE(remoteMatthew.detail<QContactPhoneNumber>().number(), matthew.detail<QContactPhoneNumber>().number());
    QContact remoteMark = tsa.remoteContact(QStringLiteral("Mark"), QStringLiteral("Three"));
    QCOMPARE(remoteMark.detail<QContactEmailAddress>().emailAddress(), mark.detail<QContactEmailAddress>().emailAddress());
    QContact remoteJohn = tsa.remoteContact(QStringLiteral("John"), QStringLiteral("One"));
    QCOMPARE(remoteJohn.detail<QContactPhoneNumber>().number(), QStringLiteral("1111123"));
    QContact remoteLuke = tsa.remoteContact(QStringLiteral("Luke"), QStringLiteral("Two"));
    QCOMPARE(remoteLuke.details<QContactName>().size(), 0); // contact was deleted from remote, shouldn't exist.

    // delete the read-write collection locally
    QVERIFY(m_cm->removeCollection(readwriteCollection.id()));

    // now perform another sync cycle
    tsa.performTwoWaySync();
    QTRY_COMPARE(failedSpy.count() + finishedSpy.count(), 3);
    QTRY_COMPARE(finishedSpy.count(), 3);

    // ensure that the local database now contains the appropriate information
    allContacts = m_cm->contacts(allCollections);
    for (int i = allContacts.size() - 1; i >= 0; --i) {
        const QContact &c(allContacts[i]);
        const bool isAggregate = c.relatedContacts(QStringLiteral("Aggregates"), QContactRelationship::Second).size() > 0;
        const QString fname = c.detail<QContactName>().firstName();
        if (!isAggregate && fname == QStringLiteral("Alice")) QCOMPARE(c.id(), aliceId);         // id should not have changed
        else if (isAggregate && fname == QStringLiteral("Alice")) QCOMPARE(isAggregate, false);  // force failure.
        else if (!isAggregate && fname == QStringLiteral("Bob")) QCOMPARE(c.id(), bobId);        // id should not have changed
        else if (isAggregate && fname == QStringLiteral("Bob")) QCOMPARE(isAggregate, false);    // force failure.
        else QVERIFY(fname == QStringLiteral("Alice") || fname == QStringLiteral("Bob"));        // force failure.
    }
    QCOMPARE(allContacts.size(), 2);

    // the readwrite collection should no longer exist locally
    for (const QContactCollection &c : m_cm->collections()) {
        const int collectionAccountId = c.extendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID).toInt();
        const QString collectionAppName = c.extendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME).toString();
        const QString collectionName = c.metaData(QContactCollection::KeyName).toString();
        if (collectionAccountId == accountId && collectionAppName == applicationName) {
            if (collectionName == QStringLiteral("Empty")) {
                emptyCollection = c;
                QCOMPARE(c.extendedMetaData(QStringLiteral("ctag")).toString(), emptyCollectionCtag);
            } else if (collectionName == QStringLiteral("ReadOnly")) {
                readonlyCollection = c;
                QCOMPARE(c.extendedMetaData(QStringLiteral("ctag")).toString(), readonlyCollectionCtag);
            } else {
                // force a failure, the other collection should have been deleted.
                QVERIFY(collectionName == QStringLiteral("Empty") || collectionName == QStringLiteral("ReadOnly"));
            }
        }
    }

    // now perform another sync cycle without performing any changes either locally or remotely.
    tsa.performTwoWaySync();
    QTRY_COMPARE(failedSpy.count() + finishedSpy.count(), 4);
    QTRY_COMPARE(finishedSpy.count(), 4);

    // no changes should have occurred.
    allContacts = m_cm->contacts(allCollections);
    for (int i = allContacts.size() - 1; i >= 0; --i) {
        const QContact &c(allContacts[i]);
        const bool isAggregate = c.relatedContacts(QStringLiteral("Aggregates"), QContactRelationship::Second).size() > 0;
        const QString fname = c.detail<QContactName>().firstName();
        if (!isAggregate && fname == QStringLiteral("Alice")) QCOMPARE(c.id(), aliceId);         // id should not have changed
        else if (isAggregate && fname == QStringLiteral("Alice")) QCOMPARE(isAggregate, false);  // force failure.
        else if (!isAggregate && fname == QStringLiteral("Bob")) QCOMPARE(c.id(), bobId);        // id should not have changed
        else if (isAggregate && fname == QStringLiteral("Bob")) QCOMPARE(isAggregate, false);    // force failure.
        else QVERIFY(fname == QStringLiteral("Alice") || fname == QStringLiteral("Bob"));        // force failure.
    }
    QCOMPARE(allContacts.size(), 2);

    for (const QContactCollection &c : m_cm->collections()) {
        const int collectionAccountId = c.extendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID).toInt();
        const QString collectionAppName = c.extendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME).toString();
        const QString collectionName = c.metaData(QContactCollection::KeyName).toString();
        if (collectionAccountId == accountId && collectionAppName == applicationName) {
            if (collectionName == QStringLiteral("Empty")) {
                emptyCollection = c;
                QCOMPARE(c.extendedMetaData(QStringLiteral("ctag")).toString(), emptyCollectionCtag);
            } else if (collectionName == QStringLiteral("ReadOnly")) {
                readonlyCollection = c;
                QCOMPARE(c.extendedMetaData(QStringLiteral("ctag")).toString(), readonlyCollectionCtag);
            } else {
                // force a failure, the other collection should have been deleted.
                QVERIFY(collectionName == QStringLiteral("Empty") || collectionName == QStringLiteral("ReadOnly"));
            }
        }
    }
}

void tst_synctransactions::twcsa_delta()
{
    // TODO: a sync plugin which supports delta sync.
}

void tst_synctransactions::twcsa_oneway()
{
    // TODO: a sync plugin which only supports to-device sync.
}



/*

void tst_Aggregation::TestSyncAdaptor()
{
    QContactDetailFilter allSyncTargets;
    setFilterDetail<QContactSyncTarget>(allSyncTargets, QContactSyncTarget::FieldSyncTarget);
    QList<QContactId> originalIds = m_cm->contactIds(allSyncTargets);

    // add some contacts remotely, and downsync them.  It should not result in an upsync.
    QString accountId(QStringLiteral("1"));
    TestSyncAdaptor tsa(accountId);
    tsa.addRemoteContact(accountId, "John", "TsaOne", "1111111", TestSyncAdaptor::ImplicitlyModifiable);
    tsa.addRemoteContact(accountId, "Bob", "TsaTwo", "2222222", TestSyncAdaptor::ExplicitlyModifiable);
    tsa.addRemoteContact(accountId, "Mark", "TsaThree", "3333333", TestSyncAdaptor::ExplicitlyNonModifiable);

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
    QVERIFY(haveExpectedContent(m_cm->contact(tsaOneStcId), QStringLiteral("1111111"), TestSyncAdaptor::ExplicitlyModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaOneAggId), QStringLiteral("1111111"), TestSyncAdaptor::ImplicitlyModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaTwoStcId), QStringLiteral("2222222"), TestSyncAdaptor::ExplicitlyModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaTwoAggId), QStringLiteral("2222222"), TestSyncAdaptor::ImplicitlyModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaThreeStcId), QStringLiteral("3333333"), TestSyncAdaptor::ExplicitlyNonModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaThreeAggId), QStringLiteral("3333333"), TestSyncAdaptor::ImplicitlyModifiable, QString()));

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
    QVERIFY(haveExpectedContent(m_cm->contact(tsaOneStcId), QStringLiteral("1111111"), TestSyncAdaptor::ExplicitlyModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaOneAggId), QStringLiteral("1111111"), TestSyncAdaptor::ImplicitlyModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaTwoStcId), QStringLiteral("2222222"), TestSyncAdaptor::ExplicitlyModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaTwoLocalId), QString(), TestSyncAdaptor::ImplicitlyModifiable, QStringLiteral("bob@tsatwo.com")));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaTwoAggId), QStringLiteral("2222222"), TestSyncAdaptor::ImplicitlyModifiable, QStringLiteral("bob@tsatwo.com")));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaThreeStcId), QStringLiteral("3333333"), TestSyncAdaptor::ExplicitlyNonModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaThreeAggId), QStringLiteral("3333333"), TestSyncAdaptor::ImplicitlyModifiable, QString()));
    QVERIFY(haveExpectedContent(tsa.remoteContact(accountId, QStringLiteral("Bob"), QStringLiteral("TsaTwo")),
                                QStringLiteral("2222222"), TestSyncAdaptor::ExplicitlyModifiable, QStringLiteral("bob@tsatwo.com")));

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
    QVERIFY(haveExpectedContent(m_cm->contact(tsaOneStcId), QStringLiteral("1111111"), TestSyncAdaptor::ExplicitlyModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaOneAggId), QStringLiteral("1111111"), TestSyncAdaptor::ImplicitlyModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaTwoStcId), QStringLiteral("2222229"), TestSyncAdaptor::ExplicitlyModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaTwoLocalId), QString(), TestSyncAdaptor::ImplicitlyModifiable, QStringLiteral("bob2@tsatwo.com")));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaTwoAggId), QStringLiteral("2222229"), TestSyncAdaptor::ImplicitlyModifiable, QStringLiteral("bob2@tsatwo.com")));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaThreeStcId), QStringLiteral("3333333"), TestSyncAdaptor::ExplicitlyNonModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaThreeAggId), QStringLiteral("3333333"), TestSyncAdaptor::ImplicitlyModifiable, QString()));
    QVERIFY(haveExpectedContent(tsa.remoteContact(accountId, QStringLiteral("Bob"), QStringLiteral("TsaTwo")),
                                QStringLiteral("2222229"), TestSyncAdaptor::ExplicitlyModifiable, QStringLiteral("bob2@tsatwo.com")));

    // remove a contact locally, ensure that the removal is upsynced.
    QVERIFY(tsa.remoteContact(accountId, QStringLiteral("Mark"), QStringLiteral("TsaThree")) != QContact());
    QVERIFY(m_cm->removeContact(tsaThreeAggId));
    tsa.performTwoWaySync(accountId);
    QTRY_COMPARE(finishedSpy.count(), 6);
    QVERIFY(tsa.downsyncWasRequired(accountId)); // the previously upsynced changes which were applied will be returned, hence will be downsynced; but discarded as nonsubstantial / already applied.
    QVERIFY(tsa.upsyncWasRequired(accountId));

    // ensure that the contacts have the data we expect
    QVERIFY(haveExpectedContent(m_cm->contact(tsaOneStcId), QStringLiteral("1111111"), TestSyncAdaptor::ExplicitlyModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaOneAggId), QStringLiteral("1111111"), TestSyncAdaptor::ImplicitlyModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaTwoStcId), QStringLiteral("2222229"), TestSyncAdaptor::ExplicitlyModifiable, QString()));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaTwoLocalId), QString(), TestSyncAdaptor::ImplicitlyModifiable, QStringLiteral("bob2@tsatwo.com")));
    QVERIFY(haveExpectedContent(m_cm->contact(tsaTwoAggId), QStringLiteral("2222229"), TestSyncAdaptor::ImplicitlyModifiable, QStringLiteral("bob2@tsatwo.com")));
    QVERIFY(haveExpectedContent(tsa.remoteContact(accountId, QStringLiteral("Bob"), QStringLiteral("TsaTwo")),
                                QStringLiteral("2222229"), TestSyncAdaptor::ExplicitlyModifiable, QStringLiteral("bob2@tsatwo.com")));
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
                                QString(), TestSyncAdaptor::ImplicitlyModifiable, QStringLiteral("jennifer@tsafour.com")));

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
    tsa.addRemoteContact(accountId, "John", "TsaFive", "555555", TestSyncAdaptor::ImplicitlyModifiable);
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
    tsa.addRemoteContact(accountId, "James", "TsaSix", "666666", TestSyncAdaptor::ImplicitlyModifiable);
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

*/

QTEST_GUILESS_MAIN(tst_synctransactions)
#include "tst_synctransactions.moc"
