/*
 * Copyright (C) 2013 - 2019 Jolla Ltd.
 * Copyright (C) 2019 - 2020 Open Mobile Platform LLC.
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

#ifndef QTCONTACTSSQLITE_CONTACTWRITER
#define QTCONTACTSSQLITE_CONTACTWRITER

#include "contactsdatabase.h"
#include "contactnotifier.h"
#include "contactid_p.h"

#include "../extensions/qtcontacts-extensions.h"
#include "../extensions/qcontactoriginmetadata.h"
#include "../extensions/contactmanagerengine.h"
#include "../extensions/contactdelta.h"

#include <QContactAddress>
#include <QContactAnniversary>
#include <QContactAvatar>
#include <QContactBirthday>
#include <QContactEmailAddress>
#include <QContactExtendedDetail>
#include <QContactGlobalPresence>
#include <QContactGuid>
#include <QContactHobby>
#include <QContactNickname>
#include <QContactNote>
#include <QContactOnlineAccount>
#include <QContactOrganization>
#include <QContactPhoneNumber>
#include <QContactPresence>
#include <QContactRingtone>
#include <QContactTag>
#include <QContactUrl>
#include <QContactManager>

#include <QSet>

QTCONTACTS_USE_NAMESPACE

class ProcessMutex;
class ContactsEngine;
class ContactReader;
class ContactWriter
{
public:
    typedef QList<QContactDetail::DetailType> DetailList;

    ContactWriter(ContactsEngine &engine, ContactsDatabase &database, ContactNotifier *notifier, ContactReader *reader);
    ~ContactWriter();

    QContactManager::Error save(
            QList<QContact> *contacts,
            const DetailList &definitionMask,
            QMap<int, bool> *aggregateUpdated,
            QMap<int, QContactManager::Error> *errorMap,
            bool withinTransaction,
            bool withinAggregateUpdate,
            bool withinSyncUpdate);
    QContactManager::Error remove(const QList<QContactId> &contactIds,
                                  QMap<int, QContactManager::Error> *errorMap,
                                  bool withinTransaction,
                                  bool withinSyncUpdate);

    QContactManager::Error setIdentity(ContactsDatabase::Identity identity, QContactId contactId);

    QContactManager::Error save(
            const QList<QContactRelationship> &relationships,
            QMap<int, QContactManager::Error> *errorMap,
            bool withinTransaction,
            bool withinAggregateUpdate);
    QContactManager::Error remove(
            const QList<QContactRelationship> &relationships,
            QMap<int, QContactManager::Error> *errorMap,
            bool withinTransaction);

    QContactManager::Error save(
            QList<QContactCollection> *collections,
            QMap<int, QContactManager::Error> *errorMap,
            bool withinTransaction,
            bool withinSyncUpdate);
    QContactManager::Error remove(
            const QList<QContactCollectionId> &collectionIds,
            QMap<int, QContactManager::Error> *errorMap,
            bool withinTransaction,
            bool withinSyncUpdate);

    QList<QContactDetail> transientDetails(quint32 contactId) const;
    bool storeTransientDetails(quint32 contactId, const QList<QContactDetail> &details);
    void removeTransientDetails(quint32 contactId);

    QContactManager::Error clearChangeFlags(const QList<QContactId> &contactIds, bool withinTransaction);
    QContactManager::Error clearChangeFlags(const QContactCollectionId &collectionId, bool withinTransaction);
    QContactManager::Error fetchCollectionChanges(
            int accountId,
            const QString &applicationName,
            QList<QContactCollection> *addedCollections,
            QList<QContactCollection> *modifiedCollections,
            QList<QContactCollection> *deletedCollections,
            QList<QContactCollection> *unmodifiedCollections);
    QContactManager::Error fetchContactChanges(
            const QContactCollectionId &collectionId,
            QList<QContact> *addedContacts,
            QList<QContact> *modifiedContacts,
            QList<QContact> *deletedContacts,
            QList<QContact> *unmodifiedContacts);
    QContactManager::Error storeChanges(
            QHash<QContactCollection*, QList<QContact> * /* added contacts */> *addedCollections,
            QHash<QContactCollection*, QList<QContact> * /* added/modified/deleted contacts */> *modifiedCollections,
            const QList<QContactCollectionId> &deletedCollections,
            QtContactsSqliteExtensions::ContactManagerEngine::ConflictResolutionPolicy conflictResolutionPolicy,
            bool clearChangeFlags);

    bool storeOOB(const QString &scope, const QMap<QString, QVariant> &values);
    bool removeOOB(const QString &scope, const QStringList &keys);

private:
    bool beginTransaction();
    bool commitTransaction();
    void rollbackTransaction();

    QContactManager::Error create(QContact *contact, const DetailList &definitionMask, bool withinTransaction, bool withinAggregateUpdate, bool withinSyncUpdate, bool recordUnhandledChangeFlags);
    QContactManager::Error update(QContact *contact, const DetailList &definitionMask, bool *aggregateUpdated, bool withinTransaction, bool withinAggregateUpdate, bool withinSyncUpdate, bool recordUnhandledChangeFlags, bool transientUpdate);
    QContactManager::Error write(quint32 contactId, const QContact &oldContact, QContact *contact, const DetailList &definitionMask, bool recordUnhandledChangeFlags);

    QContactManager::Error saveRelationships(const QList<QContactRelationship> &relationships, QMap<int, QContactManager::Error> *errorMap, bool withinAggregateUpdate);
    QContactManager::Error removeRelationships(const QList<QContactRelationship> &relationships, QMap<int, QContactManager::Error> *errorMap);

    QContactManager::Error removeDetails(const QVariantList &contactIds, bool onlyIfFlagged = false);
    QContactManager::Error removeContacts(const QVariantList &ids, bool onlyIfFlagged = false);
    QContactManager::Error deleteContacts(const QVariantList &ids, bool recordUnhandledChangeFlags);
    QContactManager::Error undeleteContacts(const QVariantList &ids, bool recordUnhandledChangeFlags);

    QContactManager::Error saveCollection(QContactCollection *collection);
    QContactManager::Error removeCollection(const QContactCollectionId &collectionId, bool onlyIfFlagged);
    QContactManager::Error deleteCollection(const QContactCollectionId &collectionId);

    QContactManager::Error collectionIsAggregable(const QContactCollectionId &collectionId, bool *aggregable);
    QContactManager::Error setAggregate(QContact *contact, quint32 contactId, bool update, const DetailList &definitionMask, bool withinTransaction, bool withinSyncUpdate);
    QContactManager::Error updateOrCreateAggregate(QContact *contact, const DetailList &definitionMask, bool withinTransaction, bool withinSyncUpdate, bool createOnly = false, quint32 *aggregateContactId = 0);

    QContactManager::Error regenerateAggregates(const QList<quint32> &aggregateIds, const DetailList &definitionMask, bool withinTransaction);
    QContactManager::Error removeChildlessAggregates(QList<QContactId> *realRemoveIds);
    QContactManager::Error aggregateOrphanedContacts(bool withinTransaction, bool withinSyncUpdate);

    ContactsDatabase::Query bindContactDetails(const QContact &contact, bool keepChangeFlags = false, bool recordUnhandledChangeFlags = false, const DetailList &definitionMask = DetailList(), quint32 contactId = 0);
    ContactsDatabase::Query bindCollectionDetails(const QContactCollection &collection);
    ContactsDatabase::Query bindCollectionMetadataDetails(const QContactCollection &collection, int *count);

    template <typename T> bool writeDetails(
            quint32 contactId,
            const QtContactsSqliteExtensions::ContactDetailDelta &delta,
            QContact *contact,
            const DetailList &definitionMask,
            const QContactCollectionId &collectionId,
            bool syncable,
            bool wasLocal,
            bool uniqueDetail,
            bool recordUnhandledChangeFlags,
            QContactManager::Error *error);

    template <typename T> quint32 writeCommonDetails(
            quint32 contactId,
            quint32 detailId,
            const T &detail,
            bool syncable,
            bool wasLocal,
            bool aggregateContact,
            bool recordUnhandledChangeFlags,
            QContactManager::Error *error);

    template <typename T> bool removeCommonDetails(quint32 contactId, QContactManager::Error *error);

    ContactsEngine &m_engine;
    ContactsDatabase &m_database;
    ContactNotifier *m_notifier;
    ContactReader *m_reader;

    QString m_managerUri;

    bool m_displayLabelGroupsChanged;
    QSet<QContactId> m_addedIds;
    QSet<QContactId> m_removedIds;
    QSet<QContactId> m_changedIds;
    QSet<QContactId> m_presenceChangedIds;
    QSet<QContactCollectionId> m_suppressedCollectionIds;
    QSet<QContactCollectionId> m_collectionContactsChanged;
    QSet<QContactCollectionId> m_addedCollectionIds;
    QSet<QContactCollectionId> m_removedCollectionIds;
    QSet<QContactCollectionId> m_changedCollectionIds;
};


#endif
