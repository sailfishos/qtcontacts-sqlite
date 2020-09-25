/*
 * Copyright (c) 2013 - 2019 Jolla Ltd.
 * Copyright (c) 2019 - 2020 Open Mobile Platform LLC.
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

#ifndef CONTACTREADER_H
#define CONTACTREADER_H

#include "contactid_p.h"
#include "contactsdatabase.h"

#include <QContact>
#include <QContactManager>

#include <QSqlDatabase>
#include <QSqlQuery>

QTCONTACTS_USE_NAMESPACE

class ContactReader
{
public:
    ContactReader(ContactsDatabase &database, const QString &managerUri);
    virtual ~ContactReader();

    QContactManager::Error readContacts(
            const QString &table,
            QList<QContact> *contacts,
            const QContactFilter &filter,
            const QList<QContactSortOrder> &order,
            const QContactFetchHint &fetchHint,
            bool keepChangeFlags = false); // for sync fetch only

    QContactManager::Error readContacts(
            const QString &table,
            QList<QContact> *contacts,
            const QList<QContactId> &contactIds,
            const QContactFetchHint &fetchHint);

    QContactManager::Error readContacts(
            const QString &table,
            QList<QContact> *contacts,
            const QList<quint32> &databaseIds,
            const QContactFetchHint &fetchHint,
            bool relaxConstraints = false);

    QContactManager::Error readContactIds(
            QList<QContactId> *contactIds,
            const QContactFilter &filter,
            const QList<QContactSortOrder> &order);

    QContactManager::Error getIdentity(
            ContactsDatabase::Identity identity, QContactId *contactId);

    QContactManager::Error readRelationships(
            QList<QContactRelationship> *relationships,
            const QString &type,
            const QContactId &first,
            const QContactId &second);

    QContactManager::Error readDetails(
            QList<QContactDetail> *details,
            QContactDetail::DetailType type,
            QList<int> fields,
            const QContactFilter &filter,
            const QList<QContactSortOrder> &order,
            const QContactFetchHint &hint);

    QContactManager::Error getCollectionIdentity(
            ContactsDatabase::CollectionIdentity identity,
            QContactCollectionId *collectionId);

    QContactManager::Error readCollections(
            const QString &table,
            QList<QContactCollection> *collections);

    QContactManager::Error fetchCollections(
            int accountId,
            const QString &applicationName,
            QList<QContactCollection> *addedCollections,
            QList<QContactCollection> *modifiedCollections,
            QList<QContactCollection> *deletedCollections,
            QList<QContactCollection> *unmodifiedCollections);

    QContactManager::Error fetchContacts(
            const QContactCollectionId &collectionId,
            QList<QContact> *addedContacts,
            QList<QContact> *modifiedContacts,
            QList<QContact> *deletedContacts,
            QList<QContact> *unmodifiedContacts);

    QContactManager::Error recordUnhandledChangeFlags(
            const QContactCollectionId &collectionId,
            bool *record);

    bool fetchOOB(const QString &scope, const QStringList &keys, QMap<QString, QVariant> *values);

    bool fetchOOBKeys(const QString &scope, QStringList *keys);

protected:
    QContactManager::Error readDeletedContactIds(
            QList<QContactId> *contactIds,
            const QContactFilter &filter);

    QContactManager::Error queryContacts(
            const QString &table,
            QList<QContact> *contacts,
            const QContactFetchHint &fetchHint,
            bool relaxConstraints = false,
            bool ignoreDeleted = false,
            bool keepChangeFlags = false);

    QContactManager::Error queryContacts(
            const QString &table,
            QList<QContact> *contacts,
            const QContactFetchHint &fetchHint,
            bool relaxConstraints,
            bool keepChangeFlags,
            QSqlQuery &query,
            QSqlQuery &relationshipQuery);

    virtual void contactsAvailable(const QList<QContact> &contacts);
    virtual void contactIdsAvailable(const QList<QContactId> &contactIds);
    virtual void collectionsAvailable(const QList<QContactCollection> &collections);

private:
    ContactsDatabase &m_database;
    QString m_managerUri;
};

#endif
