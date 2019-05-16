/*
 * Copyright (C) 2013 Jolla Ltd. <andrew.den.exter@jollamobile.com>
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
                                  bool withinTransaction);

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

    QList<QContactDetail> transientDetails(quint32 contactId) const;
    bool storeTransientDetails(quint32 contactId, const QList<QContactDetail> &details);
    void removeTransientDetails(quint32 contactId);

    QContactManager::Error fetchSyncContacts(const QString &syncTarget, const QDateTime &lastSync, const QList<QContactId> &exportedIds,
                                             QList<QContact> *syncContacts, QList<QContact> *addedContacts, QList<QContactId> *deletedContactIds,
                                             QDateTime *maxTimestamp);

    QContactManager::Error updateSyncContacts(const QString &syncTarget,
                                              QtContactsSqliteExtensions::ContactManagerEngine::ConflictResolutionPolicy conflictPolicy,
                                              QList<QPair<QContact, QContact> > *remoteChanges);

    bool storeOOB(const QString &scope, const QMap<QString, QVariant> &values);
    bool removeOOB(const QString &scope, const QStringList &keys);

private:
    bool beginTransaction();
    bool commitTransaction();
    void rollbackTransaction();

    QContactManager::Error create(QContact *contact, const DetailList &definitionMask, bool withinTransaction, bool withinAggregateUpdate);
    QContactManager::Error update(QContact *contact, const DetailList &definitionMask, bool *aggregateUpdated, bool withinTransaction, bool withinAggregateUpdate, bool transientUpdate);
    QContactManager::Error write(quint32 contactId, QContact *contact, const DetailList &definitionMask);

    QContactManager::Error saveRelationships(const QList<QContactRelationship> &relationships, QMap<int, QContactManager::Error> *errorMap, bool withinAggregateUpdate);
    QContactManager::Error removeRelationships(const QList<QContactRelationship> &relationships, QMap<int, QContactManager::Error> *errorMap);

    QContactManager::Error removeContacts(const QVariantList &ids);

    QContactManager::Error setAggregate(QContact *contact, quint32 contactId, bool update, const DetailList &definitionMask, bool withinTransaction);
    QContactManager::Error calculateDelta(QContact *contact, const ContactWriter::DetailList &definitionMask,
                                          QList<QContactDetail> *addDelta, QList<QContactDetail> *removeDelta, QList<QContact> *writeList);
    QContactManager::Error updateOrCreateAggregate(QContact *contact, const DetailList &definitionMask, bool withinTransaction, quint32 *aggregateContactId = 0);
    QContactManager::Error updateLocalAndAggregate(QContact *contact, const DetailList &definitionMask, bool withinTransaction);
    QContactManager::Error regenerateAggregates(const QList<quint32> &aggregateIds, const DetailList &definitionMask, bool withinTransaction);
    QContactManager::Error removeChildlessAggregates(QList<QContactId> *realRemoveIds);
    QContactManager::Error aggregateOrphanedContacts(bool withinTransaction);
    QContactManager::Error recordAffectedSyncTargets(const QVariantList &ids);

    QContactManager::Error syncFetch(const QString &syncTarget, const QDateTime &lastSync, const QSet<quint32> &exportedIds,
                                     QList<QContact> *syncContacts, QList<QContact> *addedContacts, QList<QContactId> *deletedContactIds,
                                     QDateTime *maxTimestamp);

    QContactManager::Error syncUpdate(const QString &syncTarget,
                                      QtContactsSqliteExtensions::ContactManagerEngine::ConflictResolutionPolicy conflictPolicy,
                                      QList<QPair<QContact, QContact> > *remoteChanges);

    ContactsDatabase::Query bindContactDetails(const QContact &contact, const DetailList &definitionMask = DetailList(), quint32 contactId = 0);

    template <typename T> bool writeDetails(
            quint32 contactId,
            QContact *contact,
            const DetailList &definitionMask,
            const QString &syncTarget,
            bool syncable,
            bool wasLocal,
            QContactManager::Error *error);

    template <typename T> quint32 writeCommonDetails(
            quint32 contactId,
            const T &detail,
            bool syncable,
            bool wasLocal,
            bool aggregateContact,
            QContactManager::Error *error);

    template <typename T> bool removeCommonDetails(quint32 contactId, QContactManager::Error *error);

    ContactsEngine &m_engine;
    ContactsDatabase &m_database;
    ContactNotifier *m_notifier;
    ContactReader *m_reader;

    bool m_displayLabelGroupsChanged;
    QSet<QContactId> m_addedIds;
    QSet<QContactId> m_removedIds;
    QSet<QContactId> m_changedIds;
    QSet<QContactId> m_presenceChangedIds;
    QSet<QString> m_changedSyncTargets;
    QSet<quint32> m_changedLocalIds;
    QSet<QString> m_suppressedSyncTargets;
};


#endif
