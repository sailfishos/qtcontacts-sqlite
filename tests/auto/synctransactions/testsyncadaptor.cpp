/*
 * Copyright (C) 2014 - 2015 Jolla Ltd.
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

#include "testsyncadaptor.h"
#include "../../../src/extensions/twowaycontactsyncadaptor_impl.h"
#include "../../../src/extensions/qtcontacts-extensions.h"

#include <QTimer>
#include <QUuid>

#include <QContact>
#include <QContactGuid>
#include <QContactPhoneNumber>
#include <QContactEmailAddress>
#include <QContactName>
#include <QContactExtendedDetail>

#define TSA_GUID_STRING(accountId, applicationName, fname, lname) QString(accountId + ":" + applicationName + ":" + fname + lname)

namespace {

QMap<QString, QString> managerParameters() {
    QMap<QString, QString> params;
    params.insert(QStringLiteral("autoTest"), QStringLiteral("true"));
    params.insert(QStringLiteral("mergePresenceChanges"), QStringLiteral("true"));
    return params;
}

QContact updateContactEtag(const QContact &c)
{
    const QList<QContactExtendedDetail> extendedDetails = c.details<QContactExtendedDetail>();
    for (const QContactExtendedDetail &ed : extendedDetails) {
        if (ed.name() == QStringLiteral("etag")) {
            QContactExtendedDetail updatedEtag = ed;
            updatedEtag.setData(QUuid::createUuid().toString());
            QContact ret = c;
            ret.saveDetail(&updatedEtag, QContact::IgnoreAccessConstraints);
            return ret;
        }
    }

    QContactExtendedDetail etag;
    etag.setData(QUuid::createUuid().toString());
    QContact ret = c;
    ret.saveDetail(&etag, QContact::IgnoreAccessConstraints);
    return ret;
}

QContactCollection updateCollectionCtag(const QContactCollection &c)
{
    QContactCollection ret = c;
    ret.setExtendedMetaData(QStringLiteral("ctag"), QUuid::createUuid().toString());
    return ret;
}

}

TestSyncAdaptor::TestSyncAdaptor(int accountId, const QString &applicationName, QContactManager &manager, QObject *parent)
    : QObject(parent), TwoWayContactSyncAdaptor(accountId, applicationName, manager)
    , m_accountId(accountId)
    , m_applicationName(applicationName)
{
    cleanUp();

    QContact alice;
    QContactName an = alice.detail<QContactName>();
    an.setFirstName("Alice"); an.setLastName("Wonderland");
    an.setValue(QContactDetail__FieldModifiable, false);
    alice.saveDetail(&an);
    QContactPhoneNumber ap = alice.detail<QContactPhoneNumber>();
    ap.setNumber("123123123");
    ap.setValue(QContactDetail__FieldModifiable, false);
    alice.saveDetail(&ap);
    QContactGuid ag;
    ag.setGuid(TSA_GUID_STRING(m_accountId, m_applicationName, an.firstName(), an.lastName()));
    alice.saveDetail(&ag);
    m_alice = updateContactEtag(alice);

    QContact bob;
    QContactName bn = bob.detail<QContactName>();
    bn.setFirstName("Bob"); bn.setLastName("Constructor");
    bn.setValue(QContactDetail__FieldModifiable, false);
    bob.saveDetail(&bn);
    QContactEmailAddress be = bob.detail<QContactEmailAddress>();
    be.setEmailAddress("bob@constructor.tld");
    be.setValue(QContactDetail__FieldModifiable, false);
    bob.saveDetail(&be);
    QContactGuid bg;
    bg.setGuid(TSA_GUID_STRING(m_accountId, m_applicationName, bn.firstName(), bn.lastName()));
    bob.saveDetail(&bg);
    m_bob = updateContactEtag(bob);

    QContactCollection emptyCollection;
    emptyCollection.setMetaData(QContactCollection::KeyName, QStringLiteral("Empty"));
    emptyCollection.setMetaData(QContactCollection::KeyDescription, QStringLiteral("An empty, read-only collection"));
    emptyCollection.setMetaData(QContactCollection::KeyColor, QStringLiteral("red"));
    emptyCollection.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID, accountId);
    emptyCollection.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, applicationName);
    emptyCollection.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_AGGREGABLE, true);
    emptyCollection.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH, QStringLiteral("/addressbooks/empty"));
    m_emptyCollection = updateCollectionCtag(emptyCollection);

    QContactCollection readonlyCollection;
    readonlyCollection.setMetaData(QContactCollection::KeyName, QStringLiteral("ReadOnly"));
    readonlyCollection.setMetaData(QContactCollection::KeyDescription, QStringLiteral("A non-empty, non-aggregable, read-only collection"));
    readonlyCollection.setMetaData(QContactCollection::KeyColor, QStringLiteral("blue"));
    readonlyCollection.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID, accountId);
    readonlyCollection.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, applicationName);
    readonlyCollection.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_AGGREGABLE, false);
    readonlyCollection.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH, QStringLiteral("/addressbooks/readonly"));
    m_readOnlyCollection = updateCollectionCtag(readonlyCollection);

    QContactCollection readwriteCollection;
    readwriteCollection.setMetaData(QContactCollection::KeyName, QStringLiteral("ReadWrite"));
    readwriteCollection.setMetaData(QContactCollection::KeyDescription, QStringLiteral("A normal, aggregable, read-write collection"));
    readwriteCollection.setMetaData(QContactCollection::KeyColor, QStringLiteral("green"));
    readwriteCollection.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID, accountId);
    readwriteCollection.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME, applicationName);
    readwriteCollection.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_AGGREGABLE, true);
    readwriteCollection.setExtendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH, QStringLiteral("/addressbooks/readwrite"));
    m_readWriteCollection = updateCollectionCtag(readwriteCollection);
}

TestSyncAdaptor::~TestSyncAdaptor()
{
    cleanUp();
}

void TestSyncAdaptor::cleanUp()
{
    removeAllCollections();
}

void TestSyncAdaptor::addRemoteDuplicates(const QString &fname, const QString &lname, const QString &phone)
{
    addRemoteContact(fname, lname, phone);
    addRemoteContact(fname, lname, phone);
    addRemoteContact(fname, lname, phone);
}

void TestSyncAdaptor::mergeRemoteDuplicates()
{
    Q_FOREACH (const QString &dupGuid, m_remoteServerDuplicates.values()) {
        m_remoteAdditions.remove(dupGuid);     // shouldn't be any here anyway.
        m_remoteModifications.remove(dupGuid); // shouldn't be any here anyway.
        m_remoteDeletions.append(m_remoteServerContacts.value(dupGuid));
        m_remoteServerContacts.remove(dupGuid);
    }
    m_remoteServerDuplicates.clear();
}

void TestSyncAdaptor::addRemoteContact(const QString &fname, const QString &lname, const QString &phone, TestSyncAdaptor::PhoneModifiability mod)
{
    QContact newContact;

    QContactName ncn;
    ncn.setFirstName(fname);
    ncn.setLastName(lname);
    newContact.saveDetail(&ncn);

    QContactPhoneNumber ncp;
    ncp.setNumber(phone);
    if (mod == TestSyncAdaptor::ExplicitlyModifiable) {
        ncp.setValue(QContactDetail__FieldModifiable, true);
    } else if (mod == TestSyncAdaptor::ExplicitlyNonModifiable) {
        ncp.setValue(QContactDetail__FieldModifiable, false);
    }
    newContact.saveDetail(&ncp);

    QContactStatusFlags nfl;
    nfl.setFlag(QContactStatusFlags::IsAdded, true);
    newContact.saveDetail(&nfl);

    newContact = updateContactEtag(newContact);

    const QString contactGuidStr(TSA_GUID_STRING(m_accountId, m_applicationName, fname, lname));
    if (m_remoteServerContacts.contains(contactGuidStr)) {
        // this is an intentional duplicate.  we have special handling for duplicates.
        QString duplicateGuidString = contactGuidStr + "#" + QString::number(m_remoteServerDuplicates.values(contactGuidStr).size() + 1);
        QContactGuid guid; guid.setGuid(duplicateGuidString); newContact.saveDetail(&guid);
        m_remoteServerDuplicates.insert(contactGuidStr, duplicateGuidString);
        m_remoteServerContacts.insert(duplicateGuidString, newContact);
        m_remoteAdditions.insert(duplicateGuidString);
    } else {
        QContactGuid guid; guid.setGuid(contactGuidStr); newContact.saveDetail(&guid);
        m_remoteServerContacts.insert(contactGuidStr, newContact);
        m_remoteAdditions.insert(contactGuidStr);
    }
}

void TestSyncAdaptor::removeRemoteContact(const QString &fname, const QString &lname)
{
    const QString contactGuidStr(TSA_GUID_STRING(m_accountId, m_applicationName, fname, lname));
    QContact remContact = m_remoteServerContacts.value(contactGuidStr);
    QContactStatusFlags rfl = remContact.detail<QContactStatusFlags>();
    rfl.setFlag(QContactStatusFlags::IsAdded, false);
    rfl.setFlag(QContactStatusFlags::IsModified, false);
    rfl.setFlag(QContactStatusFlags::IsDeleted, true);
    remContact.saveDetail(&rfl, QContact::IgnoreAccessConstraints);

    // stop tracking the contact if we are currently tracking it.
    m_remoteAdditions.remove(contactGuidStr);
    m_remoteModifications.remove(contactGuidStr);

    // remove it from our remote cache
    m_remoteServerContacts.remove(contactGuidStr);

    // report the contact as deleted
    m_remoteDeletions.append(remContact);
}

QContact TestSyncAdaptor::setRemoteContact(const QString &fname, const QString &lname, const QContact &contact)
{
    const QString contactGuidStr(TSA_GUID_STRING(m_accountId, m_applicationName, fname, lname));
    QContact setContact = contact;

    QContactGuid sguid = setContact.detail<QContactGuid>();
    sguid.setGuid(contactGuidStr);
    setContact.saveDetail(&sguid, QContact::IgnoreAccessConstraints);

    QContactOriginMetadata somd = setContact.detail<QContactOriginMetadata>();
    somd.setGroupId(setContact.id().toString());
    setContact.saveDetail(&somd, QContact::IgnoreAccessConstraints);

    const QContact newContact = updateContactEtag(setContact);
    m_remoteServerContacts[contactGuidStr] = newContact;
    return newContact;
}

void TestSyncAdaptor::changeRemoteContactPhone(const QString &fname, const QString &lname, const QString &modPhone)
{
    const QString contactGuidStr(TSA_GUID_STRING(m_accountId, m_applicationName, fname, lname));
    if (!m_remoteServerContacts.contains(contactGuidStr)) {
        qWarning() << "Contact:" << contactGuidStr << "doesn't exist remotely!";
        return;
    }

    QContact modContact = m_remoteServerContacts.value(contactGuidStr);
    QContactPhoneNumber mcp = modContact.detail<QContactPhoneNumber>();
    mcp.setNumber(modPhone);
    modContact.saveDetail(&mcp);

    QContactStatusFlags flags = modContact.detail<QContactStatusFlags>();
    flags.setFlag(QContactStatusFlags::IsModified, true);
    modContact.saveDetail(&flags, QContact::IgnoreAccessConstraints);

    m_remoteServerContacts[contactGuidStr] = modContact;
    m_remoteModifications.insert(contactGuidStr);
}

void TestSyncAdaptor::changeRemoteContactEmail(const QString &fname, const QString &lname, const QString &modEmail)
{
    const QString contactGuidStr(TSA_GUID_STRING(m_accountId, m_applicationName, fname, lname));
    if (!m_remoteServerContacts.contains(contactGuidStr)) {
        qWarning() << "Contact:" << contactGuidStr << "doesn't exist remotely!";
        return;
    }

    QContact modContact = m_remoteServerContacts.value(contactGuidStr);
    QContactEmailAddress mce = modContact.detail<QContactEmailAddress>();
    mce.setEmailAddress(modEmail);
    modContact.saveDetail(&mce);

    QContactStatusFlags flags = modContact.detail<QContactStatusFlags>();
    flags.setFlag(QContactStatusFlags::IsModified, true);
    modContact.saveDetail(&flags, QContact::IgnoreAccessConstraints);

    m_remoteServerContacts[contactGuidStr] = modContact;
    m_remoteModifications.insert(contactGuidStr);
}

void TestSyncAdaptor::changeRemoteContactName(const QString &fname, const QString &lname, const QString &modfname, const QString &modlname)
{
    const QString contactGuidStr(TSA_GUID_STRING(m_accountId, m_applicationName, fname, lname));
    if (!m_remoteServerContacts.contains(contactGuidStr)) {
        qWarning() << "Contact:" << contactGuidStr << "doesn't exist remotely!";
        return;
    }

    QContact modContact = m_remoteServerContacts.value(contactGuidStr);
    QContactName mcn = modContact.detail<QContactName>();
    if (modfname.isEmpty() && modlname.isEmpty()) {
        modContact.removeDetail(&mcn);
    } else {
        mcn.setFirstName(modfname);
        mcn.setLastName(modlname);
        modContact.saveDetail(&mcn);
    }

    QContactStatusFlags flags = modContact.detail<QContactStatusFlags>();
    flags.setFlag(QContactStatusFlags::IsModified, true);
    modContact.saveDetail(&flags, QContact::IgnoreAccessConstraints);

    const QString modContactGuidStr(TSA_GUID_STRING(m_accountId, m_applicationName, modfname, modlname));
    m_remoteServerContacts.remove(contactGuidStr);
    m_remoteModifications.remove(contactGuidStr);
    m_remoteServerContacts[modContactGuidStr] = modContact;
    m_remoteModifications.insert(modContactGuidStr);
}

bool TestSyncAdaptor::upsyncWasRequired() const
{
    return m_upsyncWasRequired;
}

bool TestSyncAdaptor::downsyncWasRequired() const
{
    return m_downsyncWasRequired;
}

QContact TestSyncAdaptor::remoteContact(const QString &fname, const QString &lname) const
{
    const QString contactGuidStr(TSA_GUID_STRING(m_accountId, m_applicationName, fname, lname));
    return m_remoteServerContacts.value(contactGuidStr);
}

QSet<QContactId> TestSyncAdaptor::modifiedIds() const
{
    return m_modifiedIds;
}

void TestSyncAdaptor::performTwoWaySync()
{
    // reset our state.
    m_downsyncWasRequired = false;
    m_upsyncWasRequired = false;

    startSync();
}

bool TestSyncAdaptor::determineRemoteCollections()
{
    QList<QContactCollection> remoteCollections;
    remoteCollections.append(m_emptyCollection);
    remoteCollections.append(m_readOnlyCollection);
    if (!m_readWriteCollectionDeleted) {
        remoteCollections.append(m_readWriteCollection);
    }

    // simulate sending a network request to remote server.
    QTimer::singleShot(250, this, [this, remoteCollections] {
        this->remoteCollectionsDetermined(remoteCollections);
    });

    return true;
}

bool TestSyncAdaptor::deleteRemoteCollection(const QContactCollection &collection)
{
    // simulate sending a network request to the remote server.
    QTimer::singleShot(250, this, [this, collection] {
        if (collection.metaData(QContactCollection::KeyName).toString() == QStringLiteral("ReadWrite")) {
            this->m_readWriteCollectionDeleted = true;
            this->remoteCollectionDeleted(collection);
        } else {
            qWarning() << "TestSyncAdaptor: unable to delete read-only collection: "
                       << collection.metaData(QContactCollection::KeyName).toString();
            syncOperationError();
        }
    });

    return true;
}

bool TestSyncAdaptor::determineRemoteContacts(const QContactCollection &collection)
{
    // simulate a request to the server.
    QTimer::singleShot(250, this, [this, collection] {
        if (collection.metaData(QContactCollection::KeyName).toString() == QStringLiteral("ReadWrite")) {
            if (this->m_readWriteCollectionDeleted) {
                qWarning() << "TestSyncAdaptor: unable to determine contacts from deleted collection";
                syncOperationError();
            } else {
                remoteContactsDetermined(collection, this->m_remoteServerContacts.values());
            }
        } else if (collection.metaData(QContactCollection::KeyName).toString() == QStringLiteral("ReadOnly")) {
            const QList<QContact> fixed { m_alice, m_bob };
            remoteContactsDetermined(collection, fixed);
        } else if (collection.metaData(QContactCollection::KeyName).toString() == QStringLiteral("Empty")) {
            remoteContactsDetermined(collection, QList<QContact>());
        } else {
            // unknown / nonexistent collection.
            qWarning() << "TestSyncAdaptor: unknown collection, cannot determine contacts";
            syncOperationError();
        }
    });

    return true;
}

bool TestSyncAdaptor::storeLocalChangesRemotely(
        const QContactCollection &collection,
        const QList<QContact> &addedContacts,
        const QList<QContact> &modifiedContacts,
        const QList<QContact> &deletedContacts)
{
    // simulate a request to the server.
    QTimer::singleShot(250, this, [this, collection, addedContacts, modifiedContacts, deletedContacts] {
        if (collection.metaData(QContactCollection::KeyName).toString() == QStringLiteral("ReadWrite")) {
            if (this->m_readWriteCollectionDeleted) {
                qWarning() << "TestSyncAdaptor: unable to store local changes to deleted collection";
                syncOperationError();
            } else {
                // we return the updated (with etags) contacts to twcsa in order to update the database.
                QList<QContact> updatedAdded;
                QList<QContact> updatedModified;
                // apply the local changes to our in memory store.
                m_readWriteCollection = updateCollectionCtag(collection);
                foreach (const QContact &c, addedContacts) {
                    updatedAdded.append(setRemoteContact(
                            c.detail<QContactName>().firstName(),
                            c.detail<QContactName>().lastName(),
                            c));
                }
                foreach (const QContact &c, modifiedContacts) {
                    bool found = false;
                    Q_FOREACH (const QString &storedGuid, m_remoteServerContacts.keys()) {
                        if (c.detail<QContactGuid>().guid() == storedGuid) {
                            // this modified contact exists.  if it was originally
                            // added on the server, it may not yet have a QContactId
                            // in our memory store, so we should set it.
                            if (m_remoteServerContacts[storedGuid].id().isNull()) {
                                m_remoteServerContacts[storedGuid].setId(c.id());
                            }
                        }
                        if (m_remoteServerContacts[storedGuid].id() == c.id()) {
                            found = true;
                            const QContact updated = updateContactEtag(c);
                            m_remoteServerContacts[storedGuid] = updated;
                            updatedModified.append(updated);
                            break;
                        }
                    }
                    if (!found) {
                        qWarning() << "TestSyncAdaptor: unable to apply modification to nonexistent remote contact:"
                                   << c.id() << " : " << c.detail<QContactName>().firstName() << " " << c.detail<QContactName>().lastName();
                        syncOperationError();
                        return;
                    }
                }
                foreach (const QContact &c, deletedContacts) {
                    // we cannot simply call removeRemoteContact since the name might be modified or empty due to a previous test.
                    bool found = false;
                    QMap<QString, QContact> remoteServerContacts = m_remoteServerContacts;
                    Q_FOREACH (const QString &storedGuid, remoteServerContacts.keys()) {
                        if (c.detail<QContactGuid>().guid() == storedGuid) {
                            // this deleted contact exists.  if it was originally
                            // added on the server, it may not yet have a QContactId
                            // in our memory store, so we should set it.
                            if (m_remoteServerContacts[storedGuid].id().isNull()) {
                                m_remoteServerContacts[storedGuid].setId(c.id());
                            }
                        }
                        if (remoteServerContacts.value(storedGuid).id() == c.id()) {
                            found = true;
                            m_remoteServerContacts.remove(storedGuid);
                        }
                    }
                    if (!found) {
                        qWarning() << "TestSyncAdaptor: unable to apply deletion to nonexistent remote contact:"
                                   << c.id() << " : " << c.detail<QContactName>().firstName() << " " << c.detail<QContactName>().lastName();
                        syncOperationError();
                        return;
                    }
                }
                // successfully updated remote data.  return the results (with updated ctag/etags).
                localChangesStoredRemotely(m_readWriteCollection, updatedAdded, updatedModified);
            }
        } else {
            qWarning() << "TestSyncAdaptor: unable to store local changes to read-only (or non-existent) remote collection";
            syncOperationError();
        }
    });

    return true;
}

void TestSyncAdaptor::syncFinishedSuccessfully()
{
    emit finished();
}

void TestSyncAdaptor::syncFinishedWithError()
{
    emit failed();
}

