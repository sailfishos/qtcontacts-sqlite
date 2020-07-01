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

#include "contactwriter.h"

#include "contactsengine.h"
#include "contactreader.h"
#include "trace_p.h"

#include "../extensions/qcontactdeactivated.h"
#include "../extensions/qcontactstatusflags.h"

#include <QContactDisplayLabel>
#include <QContactFavorite>
#include <QContactGender>
#include <QContactGlobalPresence>
#include <QContactName>
#include <QContactSyncTarget>
#include <QContactTimestamp>
#include <QContactExtendedDetail>
#include <QContactFamily>
#include <QContactGeoLocation>
#include <QContactVersion>

#include <QSqlError>
#include <QUuid>

#include <algorithm>
#include <cmath>

#include <QtDebug>

namespace {
    void dumpContactDetail(const QContactDetail &d)
    {
        QTCONTACTS_SQLITE_DEBUG("++ --------- detail type:" << d.type());
        QMap<int, QVariant> values = d.values();
        Q_FOREACH (int key, values.keys()) {
            QTCONTACTS_SQLITE_DEBUG("    " << key << "=" << values.value(key));
        }
    }

    void dumpContact(const QContact &c)
    {
        Q_FOREACH (const QContactDetail &det, c.details()) {
            dumpContactDetail(det);
        }
    }

    void updateMaxSyncTimestamp(const QList<QContact> *contacts, QDateTime *prevMaxSyncTimestamp)
    {
        Q_ASSERT(prevMaxSyncTimestamp);

        if (!contacts) {
            return;
        }

        for (int i = 0; i < contacts->size(); ++i) {
            const QContactTimestamp &ts(contacts->at(i).detail<QContactTimestamp>());
            const QDateTime contactTimestamp(ts.lastModified().isValid() ? ts.lastModified() : ts.created());
            if (contactTimestamp.isValid() && (contactTimestamp > *prevMaxSyncTimestamp || !prevMaxSyncTimestamp->isValid())) {
                *prevMaxSyncTimestamp = contactTimestamp;
            }
        }
    }

    double log2(double n)
    {
        const double scale = 1.44269504088896340736;
        return std::log(n) * scale;
    }

    double entropy(QByteArray::const_iterator it, QByteArray::const_iterator end, size_t total)
    {
        // Shannon's entropy formula, yields [0..1] (low to high information density)
        double entropy = 0.0;

        int frequency[256] = { 0 };
        for ( ; it != end; ++it) {
            frequency[static_cast<unsigned char>(*it)] += 1;
        }
        for (int i = 0; i < 256; ++i) {
            if (frequency[i] != 0) {
                double p = static_cast<double>(frequency[i]) / total;
                entropy -= p * log2(p);
            }
        }

        return entropy / 8;
    }
}

static const QString aggregateSyncTarget(QString::fromLatin1("aggregate"));
static const QString localSyncTarget(QString::fromLatin1("local"));
static const QString wasLocalSyncTarget(QString::fromLatin1("was_local"));
static const QString exportSyncTarget(QString::fromLatin1("export"));

static const QString aggregationIdsTable(QString::fromLatin1("aggregationIds"));
static const QString modifiableContactsTable(QString::fromLatin1("modifiableContacts"));
static const QString syncConstituentsTable(QString::fromLatin1("syncConstituents"));
static const QString syncAggregatesTable(QString::fromLatin1("syncAggregates"));

static const QString possibleAggregatesTable(QString::fromLatin1("possibleAggregates"));
static const QString matchEmailAddressesTable(QString::fromLatin1("matchEmailAddresses"));
static const QString matchPhoneNumbersTable(QString::fromLatin1("matchPhoneNumbers"));
static const QString matchOnlineAccountsTable(QString::fromLatin1("matchOnlineAccounts"));

ContactWriter::ContactWriter(ContactsEngine &engine, ContactsDatabase &database, ContactNotifier *notifier, ContactReader *reader)
    : m_engine(engine)
    , m_database(database)
    , m_notifier(notifier)
    , m_reader(reader)
    , m_managerUri(engine.managerUri())
    , m_displayLabelGroupsChanged(false)
{
    Q_ASSERT(notifier);
    Q_ASSERT(reader);
}

ContactWriter::~ContactWriter()
{
}

bool ContactWriter::beginTransaction()
{
    return m_database.beginTransaction();
}

bool ContactWriter::commitTransaction()
{
    m_changedLocalIds.clear();

    if (!m_database.commitTransaction()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Commit error: %1").arg(m_database.lastError().text()));
        rollbackTransaction();
        return false;
    }

    if (m_displayLabelGroupsChanged) {
        m_notifier->displayLabelGroupsChanged();
        m_displayLabelGroupsChanged = false;
    }
    if (!m_addedCollectionIds.isEmpty()) {
        m_notifier->collectionsAdded(m_addedCollectionIds.toList());
        m_addedCollectionIds.clear();
    }
    if (!m_changedCollectionIds.isEmpty()) {
        m_notifier->collectionsChanged(m_changedCollectionIds.toList());
        m_changedCollectionIds.clear();
    }
    if (!m_addedIds.isEmpty()) {
        m_notifier->contactsAdded(m_addedIds.toList());
        m_addedIds.clear();
    }
    if (!m_changedIds.isEmpty()) {
        m_notifier->contactsChanged(m_changedIds.toList());
        m_changedIds.clear();
    }
    if (!m_presenceChangedIds.isEmpty()) {
        m_notifier->contactsPresenceChanged(m_presenceChangedIds.toList());
        m_presenceChangedIds.clear();
    }
    if (!m_changedSyncCollectionIds.isEmpty()) {
        // XXXXXXXXXXXXXX TODO: remove this altogether?
        m_notifier->syncContactsChanged(m_changedSyncCollectionIds.toList());
        m_changedSyncCollectionIds.clear();
    }
    if (!m_removedIds.isEmpty()) {
        // Remove any transient data for these obsolete contacts
        QList<quint32> removedDbIds;
        foreach (const QContactId &id, m_removedIds) {
            removedDbIds.append(ContactId::databaseId(id));
        }
        m_database.removeTransientDetails(removedDbIds);

        m_notifier->contactsRemoved(m_removedIds.toList());
        m_removedIds.clear();
    }
    if (!m_removedCollectionIds.isEmpty()) {
        m_notifier->collectionsRemoved(m_removedCollectionIds.toList());
        m_removedCollectionIds.clear();

    }
    return true;
}

void ContactWriter::rollbackTransaction()
{
    m_database.rollbackTransaction();

    m_addedCollectionIds.clear();
    m_changedCollectionIds.clear();
    m_removedCollectionIds.clear();
    m_removedIds.clear();
    m_suppressedCollectionIds.clear();
    m_changedSyncCollectionIds.clear();
    m_changedLocalIds.clear();
    m_presenceChangedIds.clear();
    m_changedIds.clear();
    m_addedIds.clear();
    m_displayLabelGroupsChanged = false;
}

QContactManager::Error ContactWriter::setIdentity(ContactsDatabase::Identity identity, QContactId contactId)
{
    const QString insertIdentity(QStringLiteral("INSERT OR REPLACE INTO Identities (identity, contactId) VALUES (:identity, :contactId)"));
    const QString removeIdentity(QStringLiteral("DELETE FROM Identities WHERE identity = :identity"));

    QMutexLocker locker(m_database.accessMutex());

    quint32 dbId = ContactId::databaseId(contactId);

    ContactsDatabase::Query query(m_database.prepare(dbId == 0 ? removeIdentity : insertIdentity));
    query.bindValue(0, identity);
    if (dbId != 0) {
        query.bindValue(1, dbId);
    }

    if (ContactsDatabase::execute(query)) {
        // Notify..
        return QContactManager::NoError;
    } else {
        query.reportError(QStringLiteral("Unable to update the identity ID: %1").arg(identity));
        return QContactManager::UnspecifiedError;
    }
}

// This function is currently unused - but the way we currently build up the
// relationships query is hideously inefficient, so in the future we should
// rewrite this bindRelationships function and use execBatch().
/*
static QContactManager::Error bindRelationships(
        QSqlQuery *query,
        const QList<QContactRelationship> &relationships,
        QMap<int, QContactManager::Error> *errorMap,
        QSet<QContactLocalId> *contactIds,
        QMultiMap<QContactLocalId, QPair<QString, QContactLocalId> > *bucketedRelationships,
        int *removedDuplicatesCount)
{
    QVariantList firstIds;
    QVariantList secondIds;
    QVariantList types;
    *removedDuplicatesCount = 0;

    for (int i = 0; i < relationships.count(); ++i) {
        const QContactRelationship &relationship = relationships.at(i);
        const QContactLocalId firstId = relationship.first().localId();
        const QContactLocalId secondId = relationship.second().localId();
        const QString &type = relationship.relationshipType();

        if (firstId == 0 || secondId == 0) {
            if (errorMap)
                errorMap->insert(i, QContactManager::UnspecifiedError);
        } else if (type.isEmpty()) {
            if (errorMap)
                errorMap->insert(i, QContactManager::UnspecifiedError);
        } else {
            if (bucketedRelationships->find(firstId, QPair<QString, QContactLocalId>(type, secondId)) != bucketedRelationships->end()) {
                // this relationship is already represented in our database.
                // according to the semantics defined in tst_qcontactmanager,
                // we allow saving duplicates by "overwriting" (with identical values)
                // which means that we simply "drop" this one from the list
                // of relationships to add to the database.
                *removedDuplicatesCount += 1;
            } else {
                // this relationships has not yet been represented in our database.
                firstIds.append(firstId - 1);
                secondIds.append(secondId - 1);
                types.append(type);

                contactIds->insert(firstId);
                contactIds->insert(secondId);

                bucketedRelationships->insert(firstId, QPair<QString, QContactLocalId>(type, secondId));
            }
        }
    }

    if (firstIds.isEmpty() && *removedDuplicatesCount == 0) {
        // if we "successfully overwrote" some duplicates, it's not an error.
        return QContactManager::UnspecifiedError;
    }

    if (firstIds.size() == 1) {
        query->bindValue(0, firstIds.at(0).toUInt());
        query->bindValue(1, secondIds.at(0).toUInt());
        query->bindValue(2, types.at(0).toString());
    } else if (firstIds.size() > 1) {
        query->bindValue(0, firstIds);
        query->bindValue(1, secondIds);
        query->bindValue(2, types);
    }

    return QContactManager::NoError;
}
*/

QContactManager::Error ContactWriter::save(
        const QList<QContactRelationship> &relationships, QMap<int, QContactManager::Error> *errorMap, bool withinTransaction, bool withinAggregateUpdate)
{
    QMutexLocker locker(m_database.accessMutex());

    if (relationships.isEmpty())
        return QContactManager::NoError;

    if (!withinTransaction && !beginTransaction()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to begin database transaction while saving relationships"));
        return QContactManager::UnspecifiedError;
    }

    QContactManager::Error error = saveRelationships(relationships, errorMap, withinAggregateUpdate);
    if (error != QContactManager::NoError) {
        if (!withinTransaction) {
            // only rollback if we created a transaction.
            rollbackTransaction();
            return error;
        }
    }

    if (!withinTransaction && !commitTransaction()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to commit database after relationship save"));
        return QContactManager::UnspecifiedError;
    }

    return QContactManager::NoError;
}

template<typename T>
QString relationshipString(T type)
{
    return type();
}

QContactManager::Error ContactWriter::saveRelationships(
        const QList<QContactRelationship> &relationships, QMap<int, QContactManager::Error> *errorMap, bool withinAggregateUpdate)
{
    // in order to perform duplicate detection we build up the following datastructure.
    QMultiMap<quint32, QPair<QString, quint32> > bucketedRelationships; // first id to <type, second id>.
    {
        const QString existingRelationships(QStringLiteral(
            " SELECT firstId, secondId, type FROM Relationships"
        ));

        ContactsDatabase::Query query(m_database.prepare(existingRelationships));
        if (!ContactsDatabase::execute(query)) {
            query.reportError("Failed to fetch existing relationships for duplicate detection during insert");
            return QContactManager::UnspecifiedError;
        }

        while (query.next()) {
            quint32 fid = query.value<quint32>(0);
            quint32 sid = query.value<quint32>(1);
            QString rt = query.value<QString>(2);
            bucketedRelationships.insert(fid, qMakePair(rt, sid));
        }
    }

    // in order to perform validity detection we build up the following set.
    // XXX TODO: use foreign key constraint or similar in Relationships table?
    QSet<quint32> validContactIds;
    {
        const QString existingContactIds(QStringLiteral(
            " SELECT contactId FROM Contacts"
        ));

        ContactsDatabase::Query query(m_database.prepare(existingContactIds));
        if (!ContactsDatabase::execute(query)) {
            query.reportError("Failed to fetch existing contacts for validity detection during insert");
            return QContactManager::UnspecifiedError;
        }
        while (query.next()) {
            validContactIds.insert(query.value<quint32>(0));
        }
    }

    QList<quint32> firstIdsToBind;
    QList<quint32> secondIdsToBind;
    QList<QString> typesToBind;

    QSet<quint32> aggregatesAffected;

    QSqlQuery multiInsertQuery(m_database);
    QString queryString = QLatin1String("INSERT INTO Relationships");
    int realInsertions = 0;
    int invalidInsertions = 0;
    for (int i = 0; i < relationships.size(); ++i) {
        const QContactRelationship &relationship = relationships.at(i);

        QContactId first(relationship.first());
        QContactId second(relationship.second());

        const quint32 firstId = ContactId::databaseId(first);
        const quint32 secondId = ContactId::databaseId(second);
        const QString &type = relationship.relationshipType();

        if ((firstId == secondId)
                || (!first.managerUri().isEmpty() &&
                    !first.managerUri().startsWith(m_managerUri)
                   )
                || (!second.managerUri().isEmpty() &&
                    !second.managerUri().startsWith(m_managerUri)
                   )
                || (!validContactIds.contains(firstId) || !validContactIds.contains(secondId))) {
            // invalid contact specified in relationship, don't insert.
            invalidInsertions += 1;
            if (errorMap)
                errorMap->insert(i, QContactManager::InvalidRelationshipError);
            continue;
        }

        if (bucketedRelationships.find(firstId, qMakePair(type, secondId)) != bucketedRelationships.end()) {
            // duplicate, don't insert.
            continue;
        } else {
            if (realInsertions == 0) {
                queryString += QString(QLatin1String("\n SELECT :firstId%1 as firstId, :secondId%1 as secondId, :type%1 as type"))
                                      .arg(QString::number(realInsertions));
            } else {
                queryString += QString(QLatin1String("\n UNION SELECT :firstId%1, :secondId%1, :type%1"))
                                      .arg(QString::number(realInsertions));
            }
            firstIdsToBind.append(firstId);
            secondIdsToBind.append(secondId);
            typesToBind.append(type);
            bucketedRelationships.insert(firstId, qMakePair(type, secondId));
            realInsertions += 1;

            if (m_database.aggregating() && (type == relationshipString(QContactRelationship::Aggregates))) {
                // This aggregate needs to be regenerated
                aggregatesAffected.insert(firstId);
            }
        }
    }

    if (realInsertions > 0 && !multiInsertQuery.prepare(queryString)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare multiple insert relationships query:\n%1\nQuery:\n%2")
                .arg(multiInsertQuery.lastError().text())
                .arg(queryString));
        return QContactManager::UnspecifiedError;
    }

    for (int i = 0; i < realInsertions; ++i) {
        multiInsertQuery.bindValue(QString(QLatin1String(":firstId%1")).arg(QString::number(i)), firstIdsToBind.at(i));
        multiInsertQuery.bindValue(QString(QLatin1String(":secondId%1")).arg(QString::number(i)), secondIdsToBind.at(i));
        multiInsertQuery.bindValue(QString(QLatin1String(":type%1")).arg(QString::number(i)), typesToBind.at(i));
    }

    if (realInsertions > 0 && !ContactsDatabase::execute(multiInsertQuery)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to insert relationships:\n%1\nQuery:\n%2")
                .arg(multiInsertQuery.lastError().text())
                .arg(queryString));
        return QContactManager::UnspecifiedError;
    }

    if (invalidInsertions > 0) {
        return QContactManager::InvalidRelationshipError;
    }

    if (m_database.aggregating() && !aggregatesAffected.isEmpty() && !withinAggregateUpdate) {
        QContactManager::Error writeError = regenerateAggregates(aggregatesAffected.toList(), DetailList(), true);
        if (writeError != QContactManager::NoError) {
            return writeError;
        }
    }

    return QContactManager::NoError;
}

QContactManager::Error ContactWriter::remove(
        const QList<QContactRelationship> &relationships, QMap<int, QContactManager::Error> *errorMap, bool withinTransaction)
{
    QMutexLocker locker(m_database.accessMutex());

    if (relationships.isEmpty())
        return QContactManager::NoError;

    if (!withinTransaction && !beginTransaction()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to begin database transaction while removing relationships"));
        return QContactManager::UnspecifiedError;
    }

    QContactManager::Error error = removeRelationships(relationships, errorMap);
    if (error != QContactManager::NoError) {
        if (!withinTransaction) {
            // only rollback if we created a transaction.
            rollbackTransaction();
            return error;
        }
    }

    if (!withinTransaction && !commitTransaction()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to commit database after relationship removal"));
        return QContactManager::UnspecifiedError;
    }

    return QContactManager::NoError;
}

QContactManager::Error ContactWriter::removeRelationships(
        const QList<QContactRelationship> &relationships, QMap<int, QContactManager::Error> *errorMap)
{
    // in order to perform existence detection we build up the following datastructure.
    QMultiMap<quint32, QPair<QString, quint32> > bucketedRelationships; // first id to <type, second id>.
    {
        const QString existingRelationships(QStringLiteral(
            " SELECT firstId, secondId, type FROM Relationships"
        ));

        ContactsDatabase::Query query(m_database.prepare(existingRelationships));
        if (!ContactsDatabase::execute(query)) {
            query.reportError("Failed to fetch existing relationships for duplicate detection during insert");
            return QContactManager::UnspecifiedError;
        }

        while (query.next()) {
            quint32 fid = query.value<quint32>(0);
            quint32 sid = query.value<quint32>(1);
            QString rt = query.value<QString>(2);
            bucketedRelationships.insert(fid, qMakePair(rt, sid));
        }
    }

    QContactManager::Error worstError = QContactManager::NoError;
    QSet<QContactRelationship> alreadyRemoved;
    QSet<quint32> aggregatesAffected;
    bool removeInvalid = false;
    for (int i = 0; i < relationships.size(); ++i) {
        QContactRelationship curr = relationships.at(i);
        if (alreadyRemoved.contains(curr)) {
            continue;
        }

        quint32 currFirst = ContactId::databaseId(curr.first());
        quint32 currSecond = ContactId::databaseId(curr.second());
        QString type(curr.relationshipType());

        if (bucketedRelationships.find(currFirst, qMakePair(curr.relationshipType(), currSecond)) == bucketedRelationships.end()) {
            removeInvalid = true;
            if (errorMap)
                errorMap->insert(i, QContactManager::DoesNotExistError);
            continue;
        }

        if (m_database.aggregating() && (type == relationshipString(QContactRelationship::Aggregates))) {
            // This aggregate needs to be regenerated
            aggregatesAffected.insert(currFirst);
        }

        const QString removeRelationship(QStringLiteral(
            " DELETE FROM Relationships"
            " WHERE firstId = :firstId AND secondId = :secondId AND type = :type"
        ));

        ContactsDatabase::Query query(m_database.prepare(removeRelationship));

        query.bindValue(":firstId", currFirst);
        query.bindValue(":secondId", currSecond);
        query.bindValue(":type", type);

        if (!ContactsDatabase::execute(query)) {
            query.reportError("Failed to remove relationship");
            worstError = QContactManager::UnspecifiedError;
            if (errorMap)
                errorMap->insert(i, worstError);
            continue;
        }

        alreadyRemoved.insert(curr);
    }

    if (removeInvalid) {
        return QContactManager::DoesNotExistError;
    }

    if (m_database.aggregating()) {
        // remove any aggregates that no longer aggregate any contacts.
        QList<QContactId> removedIds;
        QContactManager::Error removeError = removeChildlessAggregates(&removedIds);
        if (removeError != QContactManager::NoError)
            return removeError;

        foreach (const QContactId &id, removedIds) {
            m_removedIds.insert(id);
            aggregatesAffected.remove(ContactId::databaseId(id));
        }

        if (!aggregatesAffected.isEmpty()) {
            QContactManager::Error writeError = regenerateAggregates(aggregatesAffected.toList(), DetailList(), true);
            if (writeError != QContactManager::NoError)
                return writeError;
        }

        // Some contacts may need to have new aggregates created
        QContactManager::Error aggregateError = aggregateOrphanedContacts(true);
        if (aggregateError != QContactManager::NoError)
            return aggregateError;
    }

    return QContactManager::NoError;
}

QContactManager::Error ContactWriter::saveCollection(QContactCollection *collection)
{
    bool collectionExists = ContactCollectionId::isValid(collection->id());

    ContactsDatabase::Query query(bindCollectionDetails(*collection));
    if (!ContactsDatabase::execute(query)) {
        query.reportError("Failed to save collection");
        return QContactManager::UnspecifiedError;
    }

    if (!collectionExists) {
        quint32 collectionId = query.lastInsertId().toUInt();
        collection->setId(ContactCollectionId::apiId(collectionId, m_managerUri));
    }

    return QContactManager::NoError;
}

QContactManager::Error ContactWriter::save(
        QList<QContactCollection> *collections,
        QMap<int, QContactManager::Error> *errorMap,
        bool withinTransaction,
        bool withinSyncUpdate)
{
    Q_UNUSED(withinSyncUpdate) // TODO

    QMutexLocker locker(m_database.accessMutex());

    if (!withinTransaction && !beginTransaction()) {
        // if we are not already within a transaction, create a transaction.
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to begin database transaction while saving collections"));
        return QContactManager::UnspecifiedError;
    }

    QContactManager::Error ret = QContactManager::NoError;
    QSet<QContactCollectionId> addedIds;
    QSet<QContactCollectionId> changedIds;

    for (int i = 0; i < collections->size(); ++i) {
        QContactCollection &collection = (*collections)[i]; // rely on reference stability...
        bool exists = ContactCollectionId::isValid(collection.id());
        QContactManager::Error saveError = QContactManager::NoError;
        if (exists) {
            const QString queryCollectionExistence(QStringLiteral(
                " SELECT COUNT(*) FROM Collections WHERE collectionId = :collectionId"
            ));
            ContactsDatabase::Query query(m_database.prepare(queryCollectionExistence));
            query.bindValue(QLatin1String(":collectionId"), ContactCollectionId::databaseId(collection.id()));
            if (!ContactsDatabase::execute(query)) {
                query.reportError("Failed to query collection existence");
                saveError = QContactManager::UnspecifiedError;
            } else if (query.next()) {
                exists = query.value<quint32>(0) == 1;
            }
        }

        if (saveError == QContactManager::NoError) {
            saveError = saveCollection(&collection);
            if (saveError == QContactManager::NoError) {
                if (exists) {
                    changedIds.insert(collection.id());
                } else {
                    addedIds.insert(collection.id());
                }
            }
        }

        if (errorMap) {
            errorMap->insert(i, saveError);
        }

        if (saveError != QContactManager::NoError) {
            ret = saveError;
        }
    }

    if (ret != QContactManager::NoError) {
        if (!withinTransaction) {
            // only rollback if we created a transaction.
            rollbackTransaction();
        }
    } else {
        foreach (const QContactCollectionId &cid, changedIds) {
            m_changedCollectionIds.insert(cid);
        }
        foreach (const QContactCollectionId &aid, addedIds) {
            m_addedCollectionIds.insert(aid);
        }
        if (!withinTransaction && !commitTransaction()) {
            // only commit if we created a transaction.
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to commit collection save"));
            ret = QContactManager::UnspecifiedError;
        }
    }

    return ret;
}

QContactManager::Error ContactWriter::removeCollection(const QContactCollectionId &collectionId)
{
    const QString removeCollection(QStringLiteral(
        " DELETE FROM Collections WHERE collectionId = :collectionId"
    ));
    ContactsDatabase::Query remove(m_database.prepare(removeCollection));
    remove.bindValue(QLatin1String(":collectionId"), ContactCollectionId::databaseId(collectionId));
    if (!ContactsDatabase::execute(remove)) {
        remove.reportError("Failed to remove collection contacts");
        return QContactManager::UnspecifiedError;
    }
    return QContactManager::NoError;
}

QContactManager::Error ContactWriter::remove(
        const QList<QContactCollectionId> &collectionIds,
        QMap<int, QContactManager::Error> *errorMap,
        bool withinTransaction)
{
    QMutexLocker locker(m_database.accessMutex());

    if (!withinTransaction && !beginTransaction()) {
        // if we are not already within a transaction, create a transaction.
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to begin database transaction while removing collections"));
        return QContactManager::UnspecifiedError;
    }

    QContactManager::Error ret = QContactManager::NoError;
    QSet<QContactId> removedContactIds;
    QSet<QContactCollectionId> removedCollectionIds;

    for (int i = 0; i < collectionIds.size(); ++i) {
        const QContactCollectionId &collectionId(collectionIds[i]);
        if (ContactCollectionId::databaseId(collectionId) <= ContactsDatabase::LocalAddressbookCollectionId) {
            // don't allow removing the built-in collections.
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to remove built-in collections"));
            ret = QContactManager::BadArgumentError;
        } else {
            QContactManager::Error removeError = QContactManager::NoError;
            QList<QContactId> collectionContacts;
            const QString queryContactIds(QStringLiteral(
                " SELECT ContactId FROM Contacts WHERE collectionId = :collectionId"
            ));
            ContactsDatabase::Query query(m_database.prepare(queryContactIds));
            query.bindValue(QLatin1String(":collectionId"), ContactCollectionId::databaseId(collectionId));
            if (!ContactsDatabase::execute(query)) {
                query.reportError("Failed to query collection contacts");
                removeError = QContactManager::UnspecifiedError;
            } else while (query.next()) {
                collectionContacts.append(ContactId::apiId(query.value<quint32>(0), m_managerUri));
            }

            if (removeError == QContactManager::NoError) {
                removeError = remove(collectionContacts, nullptr, true);
                if (removeError != QContactManager::NoError) {
                    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to remove contacts while removing collection"));
                } else {
                    foreach (const QContactId &rid, collectionContacts) {
                        removedContactIds.insert(rid);
                    }
                    removeError = removeCollection(collectionId);
                    if (removeError == QContactManager::NoError) {
                        removedCollectionIds.insert(collectionId);
                    }
                }
            }

            if (errorMap) {
                errorMap->insert(i, removeError);
            }

            if (removeError != QContactManager::NoError) {
                ret = removeError;
            }
        }
    }

    if (ret != QContactManager::NoError) {
        if (!withinTransaction) {
            // only rollback if we created a transaction.
            rollbackTransaction();
        }
    } else {
        foreach (const QContactId &rid, removedContactIds) {
            m_removedIds.insert(rid);
        }
        foreach (const QContactCollectionId &cid, removedCollectionIds) {
            m_removedCollectionIds.insert(cid);
        }
        if (!withinTransaction && !commitTransaction()) {
            // only commit if we created a transaction.
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to commit collection removal"));
            return QContactManager::UnspecifiedError;
        }
    }

    return ret;
}

QContactManager::Error ContactWriter::removeContacts(const QVariantList &ids)
{
    const QString removeContact(QStringLiteral(
        " DELETE FROM Contacts WHERE contactId = :contactId"
    ));

    ContactsDatabase::Query query(m_database.prepare(removeContact));
    query.bindValue(QLatin1String(":contactId"), ids);
    if (!ContactsDatabase::executeBatch(query)) {
        query.reportError("Failed to remove contacts");
        return QContactManager::UnspecifiedError;
    }

    return QContactManager::NoError;
}

QContactManager::Error ContactWriter::remove(const QList<QContactId> &contactIds, QMap<int, QContactManager::Error> *errorMap, bool withinTransaction)
{
    QMutexLocker locker(m_database.accessMutex());

    if (contactIds.isEmpty())
        return QContactManager::NoError;

    // grab the self-contact id so we can avoid removing it.
    quint32 selfContactId = 0;
    {
        QContactId id;
        QContactManager::Error err;
        if ((err = m_reader->getIdentity(ContactsDatabase::SelfContactId, &id)) != QContactManager::NoError) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to determine self ID while removing contacts"));
            return err;
        }
        selfContactId = ContactId::databaseId(id); // the aggregate self contact id, the local will be less than it.
    }

    // grab the existing contact ids so that we can perform removal detection
    // we also determine whether the contact is an aggregate (and prevent if so).
    QHash<quint32, quint32> existingContactIds; // contact id to collection id
    {
        const QString findExistingContactIds(QStringLiteral(
            " SELECT contactId, collectionId FROM Contacts"
        ));
        ContactsDatabase::Query query(m_database.prepare(findExistingContactIds));
        if (!ContactsDatabase::execute(query)) {
            query.reportError("Failed to fetch existing contact ids during remove");
            return QContactManager::UnspecifiedError;
        }
        while (query.next()) {
            const quint32 contactId = query.value<quint32>(0);
            const quint32 collectionId = query.value<quint32>(1);
            existingContactIds.insert(contactId, collectionId);
        }
    }

    // determine which contacts we actually need to remove
    QContactManager::Error error = QContactManager::NoError;
    QList<QContactId> realRemoveIds;
    QVariantList boundRealRemoveIds;
    QSet<QContactCollectionId> removeChangedCollectionIds;
    for (int i = 0; i < contactIds.size(); ++i) {
        QContactId currId = contactIds.at(i);
        quint32 dbId = ContactId::databaseId(currId);
        if (dbId == 0) {
            if (errorMap)
                errorMap->insert(i, QContactManager::DoesNotExistError);
            error = QContactManager::DoesNotExistError;
        } else if (selfContactId > 0 && dbId <= selfContactId) {
            if (errorMap)
                errorMap->insert(i, QContactManager::BadArgumentError);
            error = QContactManager::BadArgumentError;
        } else if (existingContactIds.contains(dbId)) {
            const quint32 removeContactCollectionId = existingContactIds.value(dbId);
            if (removeContactCollectionId == ContactsDatabase::AggregateAddressbookCollectionId) {
                if (errorMap)
                    errorMap->insert(i, QContactManager::BadArgumentError);
                error = QContactManager::BadArgumentError;
            } else {
                realRemoveIds.append(currId);
                boundRealRemoveIds.append(dbId);
                removeChangedCollectionIds.insert(ContactCollectionId::apiId(removeContactCollectionId, m_managerUri));
            }
        } else {
            if (errorMap)
                errorMap->insert(i, QContactManager::DoesNotExistError);
            error = QContactManager::DoesNotExistError;
        }
    }

    if (realRemoveIds.size() == 0) {
        return error; // no contacts to actually remove.
    }

    if (!m_database.aggregating()) {
        // If we don't perform aggregation, we simply need to remove every
        // (valid, non-self) contact specified in the list.
        if (!withinTransaction && !beginTransaction()) {
            // if we are not already within a transaction, create a transaction.
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to begin database transaction while removing contacts"));
            return QContactManager::UnspecifiedError;
        }
        QContactManager::Error removeError = removeContacts(boundRealRemoveIds);
        if (removeError != QContactManager::NoError) {
            if (!withinTransaction) {
                // only rollback if we created a transaction.
                rollbackTransaction();
            }
            return removeError;
        }
        foreach (const QContactId &rrid, realRemoveIds) {
            m_removedIds.insert(rrid);
        }
        if (!withinTransaction && !commitTransaction()) {
            // only commit if we created a transaction.
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to commit removal"));
            return QContactManager::UnspecifiedError;
        }

        return error;
    }

    // grab the ids of aggregate contacts which aggregate any of the contacts
    // which we're about to remove.  We will regenerate them after successful
    // remove.
    QList<quint32> aggregatesOfRemoved;

    m_database.clearTemporaryContactIdsTable(aggregationIdsTable);
    if (!m_database.createTemporaryContactIdsTable(aggregationIdsTable, boundRealRemoveIds)) {
        return QContactManager::UnspecifiedError;
    } else {
        const QString findAggregateForContactIds(QStringLiteral(
            " SELECT DISTINCT Relationships.firstId"
            " FROM Relationships"
            " JOIN temp.aggregationIds ON Relationships.secondId = temp.aggregationIds.contactId"
            " WHERE Relationships.type = 'Aggregates'"
        ));

        ContactsDatabase::Query query(m_database.prepare(findAggregateForContactIds));
        if (!ContactsDatabase::execute(query)) {
            query.reportError("Failed to fetch aggregator contact ids during remove");
            return QContactManager::UnspecifiedError;
        }
        while (query.next()) {
            aggregatesOfRemoved.append(query.value<quint32>(0));
        }
    }

    if (!withinTransaction && !beginTransaction()) {
        // only create a transaction if we're not already within one
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to begin database transaction while removing contacts"));
        return QContactManager::UnspecifiedError;
    }

    // remove the non-aggregate contacts which were specified for removal.
    if (boundRealRemoveIds.size() > 0) {
        QContactManager::Error removeError = removeContacts(boundRealRemoveIds);
        if (removeError != QContactManager::NoError) {
            if (!withinTransaction) {
                // only rollback if we created a transaction.
                rollbackTransaction();
            }
            return removeError;
        }
    }

    // remove any aggregates which no longer aggregate any contacts.
    QContactManager::Error removeError = removeChildlessAggregates(&realRemoveIds);
    if (removeError != QContactManager::NoError) {
        if (!withinTransaction) {
            // only rollback the transaction if we created it
            rollbackTransaction();
        }
        return removeError;
    }

    foreach (const QContactId &id, realRemoveIds) {
        m_removedIds.insert(id);
    }

    // And notify of any removals.
    if (realRemoveIds.size() > 0) {
        // update our "regenerate list" by purging removed contacts
        foreach (const QContactId &removedId, realRemoveIds) {
            aggregatesOfRemoved.removeAll(ContactId::databaseId(removedId));
        }
    }

    // Now regenerate our remaining aggregates as required.
    if (aggregatesOfRemoved.size() > 0) {
        QContactManager::Error writeError = regenerateAggregates(aggregatesOfRemoved, DetailList(), true);
        if (writeError != QContactManager::NoError)
            return writeError;
    }

    // Success!  If we created a transaction, commit.
    if (!withinTransaction && !commitTransaction()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to commit database after removal"));
        return QContactManager::UnspecifiedError;
    }

    return error;
}

template<typename T>
QContactDetail::DetailType detailType()
{
    return T::Type;
}

QContactDetail::DetailType detailType(const QContactDetail &detail)
{
    return detail.type();
}

template<typename T>
void insert(QMap<QContactDetail::DetailType, const char *> &map, const char *name)
{
    map.insert(T::Type, name);
}

#define PREFIX_LENGTH 8
#define STRINGIZE(T) #T
#define INSERT(map,T) insert<T>(map, STRINGIZE(T) + PREFIX_LENGTH)

QMap<QContactDetail::DetailType, const char *> getDetailTypeNames()
{
    QMap<QContactDetail::DetailType, const char *> rv;

    INSERT(rv, QContactAddress);
    INSERT(rv, QContactAnniversary);
    INSERT(rv, QContactAvatar);
    INSERT(rv, QContactBirthday);
    INSERT(rv, QContactDisplayLabel);
    INSERT(rv, QContactEmailAddress);
    INSERT(rv, QContactExtendedDetail);
    INSERT(rv, QContactFamily);
    INSERT(rv, QContactFavorite);
    INSERT(rv, QContactGender);
    INSERT(rv, QContactGeoLocation);
    INSERT(rv, QContactGlobalPresence);
    INSERT(rv, QContactGuid);
    INSERT(rv, QContactHobby);
    INSERT(rv, QContactName);
    INSERT(rv, QContactNickname);
    INSERT(rv, QContactNote);
    INSERT(rv, QContactOnlineAccount);
    INSERT(rv, QContactOrganization);
    INSERT(rv, QContactPhoneNumber);
    INSERT(rv, QContactPresence);
    INSERT(rv, QContactRingtone);
    INSERT(rv, QContactSyncTarget);
    INSERT(rv, QContactTag);
    INSERT(rv, QContactTimestamp);
    INSERT(rv, QContactType);
    INSERT(rv, QContactUrl);
    INSERT(rv, QContactVersion);

    // Our extensions:
    INSERT(rv, QContactDeactivated);
    INSERT(rv, QContactOriginMetadata);
    INSERT(rv, QContactStatusFlags);

    return rv;
}

#undef INSERT
#undef STRINGIZE
#undef PREFIX_LENGTH

const char *detailTypeName(QContactDetail::DetailType type)
{
    static const QMap<QContactDetail::DetailType, const char *> names(getDetailTypeNames());

    QMap<QContactDetail::DetailType, const char *>::const_iterator it = names.find(type);
    if (it != names.end()) {
        return *it;
    }
    return 0;
}

template<typename T>
const char *detailTypeName()
{
    return detailTypeName(T::Type);
}

QString detailTypeName(const QContactDetail &detail)
{
    return QString::fromLatin1(detailTypeName(detail.type()));
}

static ContactWriter::DetailList getIdentityDetailTypes()
{
    // The list of types for details that identify a contact
    ContactWriter::DetailList rv;
    rv << detailType<QContactSyncTarget>()
       << detailType<QContactGuid>()
       << detailType<QContactType>();
    return rv;
}

static ContactWriter::DetailList getUnpromotedDetailTypes()
{
    // The list of types for details that are not promoted to an aggregate
    ContactWriter::DetailList rv(getIdentityDetailTypes());
    rv << detailType<QContactDisplayLabel>();
    rv << detailType<QContactGlobalPresence>();
    rv << detailType<QContactStatusFlags>();
    rv << detailType<QContactOriginMetadata>();
    rv << detailType<QContactDeactivated>();
    return rv;
}

static ContactWriter::DetailList getAbsolutelyUnpromotedDetailTypes()
{
    // The list of types for details that are not promoted to an aggregate, even if promotion is forced
    ContactWriter::DetailList rv;
    rv << detailType<QContactDisplayLabel>();
    rv << detailType<QContactGlobalPresence>();
    rv << detailType<QContactStatusFlags>();
    rv << detailType<QContactDeactivated>();
    return rv;
}

static ContactWriter::DetailList getCompositionDetailTypes()
{
    // The list of types for details that are composed to form aggregates
    ContactWriter::DetailList rv;
    rv << detailType<QContactName>();
    rv << detailType<QContactTimestamp>();
    rv << detailType<QContactGender>();
    rv << detailType<QContactFavorite>();
    rv << detailType<QContactBirthday>();
    return rv;
}

static ContactWriter::DetailList getPresenceUpdateDetailTypes()
{
    // The list of types for details whose changes constitute presence updates
    ContactWriter::DetailList rv;
    rv << detailType<QContactPresence>();
    rv << detailType<QContactOriginMetadata>();
    rv << detailType<QContactOnlineAccount>();
    return rv;
}

template<typename T>
static bool detailListContains(const ContactWriter::DetailList &list)
{
    return list.contains(detailType<T>());
}

static bool detailListContains(const ContactWriter::DetailList &list, QContactDetail::DetailType type)
{
    return list.contains(type);
}

static bool detailListContains(const ContactWriter::DetailList &list, const QContactDetail &detail)
{
    return list.contains(detailType(detail));
}

bool removeCommonDetails(ContactsDatabase &db, quint32 contactId, const QString &typeName, QContactManager::Error *error)
{
    const QString statement(QStringLiteral("DELETE FROM Details WHERE contactId = :contactId AND detail = :detail"));

    ContactsDatabase::Query query(db.prepare(statement));
    query.bindValue(0, contactId);
    query.bindValue(1, typeName);

    if (!ContactsDatabase::execute(query)) {
        query.reportError(QStringLiteral("Failed to remove common detail for %1").arg(typeName));
        *error = QContactManager::UnspecifiedError;
        return false;
    }

    return true;
}

template <typename T> bool ContactWriter::removeCommonDetails(
            quint32 contactId, QContactManager::Error *error)
{
    return ::removeCommonDetails(m_database, contactId, detailTypeName<T>(), error);
}

template<typename T, typename F>
QVariant detailValue(const T &detail, F field)
{
    return detail.value(field);
}

typedef QMap<int, QVariant> DetailMap;

DetailMap detailValues(const QContactDetail &detail, bool includeProvenance = true, bool includeModifiable = true)
{
    DetailMap rv(detail.values());

    if (!includeProvenance || !includeModifiable) {
        DetailMap::iterator it = rv.begin();
        while (it != rv.end()) {
            if (!includeProvenance && it.key() == QContactDetail__FieldProvenance) {
                it = rv.erase(it);
            } else if (!includeModifiable && it.key() == QContactDetail__FieldModifiable) {
                it = rv.erase(it);
            } else {
                ++it;
            }
        }
    }

    return rv;
}

static bool variantEqual(const QVariant &lhs, const QVariant &rhs)
{
    // Work around incorrect result from QVariant::operator== when variants contain QList<int>
    static const int QListIntType = QMetaType::type("QList<int>");

    const int lhsType = lhs.userType();
    if (lhsType != rhs.userType()) {
        return false;
    }

    if (lhsType == QListIntType) {
        return (lhs.value<QList<int> >() == rhs.value<QList<int> >());
    }
    return (lhs == rhs);
}

static bool detailValuesEqual(const QContactDetail &lhs, const QContactDetail &rhs)
{
    const DetailMap lhsValues(detailValues(lhs, false, false));
    const DetailMap rhsValues(detailValues(rhs, false, false));

    if (lhsValues.count() != rhsValues.count()) {
        return false;
    }

    // Because of map ordering, matching fields should be in the same order in both details
    DetailMap::const_iterator lit = lhsValues.constBegin(), lend = lhsValues.constEnd();
    DetailMap::const_iterator rit = rhsValues.constBegin();
    for ( ; lit != lend; ++lit, ++rit) {
        if (lit.key() != rit.key() || !variantEqual(*lit, *rit)) {
            return false;
        }
    }

    return true;
}

static bool detailsEquivalent(const QContactDetail &lhs, const QContactDetail &rhs)
{
    // Same as operator== except ignores differences in accessConstraints values
    if (detailType(lhs) != detailType(rhs))
        return false;
    return detailValuesEqual(lhs, rhs);
}

QContactManager::Error ContactWriter::fetchSyncContacts(const QContactCollectionId &collectionId, const QDateTime &lastSync, const QList<QContactId> &exportedIds,
                                                        QList<QContact> *syncContacts, QList<QContact> *addedContacts, QList<QContactId> *deletedContactIds,
                                                        QDateTime *maxTimestamp)
{
    // Although this is a read operation, it's probably best to make it a transaction
    QMutexLocker locker(m_database.accessMutex());

    // Exported IDs are those that the sync adaptor has previously exported, that originate locally
    QSet<quint32> exportedDbIds;
    foreach (const QContactId &id, exportedIds) {
        exportedDbIds.insert(ContactId::databaseId(id));
    }

    if (!beginTransaction()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to begin database transaction while fetching sync contacts"));
        return QContactManager::UnspecifiedError;
    }

    QContactManager::Error error = syncFetch(collectionId, lastSync, exportedDbIds, syncContacts, addedContacts, deletedContactIds, maxTimestamp);
    if (error != QContactManager::NoError) {
        rollbackTransaction();
        return error;
    }

    if (!commitTransaction()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to commit database after sync contacts fetch"));
        return QContactManager::UnspecifiedError;
    }

    return QContactManager::NoError;
}

QContactManager::Error ContactWriter::updateSyncContacts(const QContactCollectionId &collectionId,
                                                         QtContactsSqliteExtensions::ContactManagerEngine::ConflictResolutionPolicy conflictPolicy,
                                                         QList<QPair<QContact, QContact> > *remoteChanges)
{
    QMutexLocker locker(m_database.accessMutex());

    if (conflictPolicy != QtContactsSqliteExtensions::ContactManagerEngine::PreserveLocalChanges) {
        // We only support one policy for now
        return QContactManager::NotSupportedError;
    }

    if (!remoteChanges || remoteChanges->isEmpty())
        return QContactManager::NoError;

    if (!beginTransaction()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to begin database transaction while updating sync contacts"));
        return QContactManager::UnspecifiedError;
    }

    m_suppressedCollectionIds.insert(collectionId);

    QContactManager::Error error = syncUpdate(collectionId, conflictPolicy, remoteChanges);
    if (error != QContactManager::NoError) {
        rollbackTransaction();
        return error;
    }

    if (!commitTransaction()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to commit database after sync contacts update"));
        return QContactManager::UnspecifiedError;
    }

    return QContactManager::NoError;
}

bool ContactWriter::storeOOB(const QString &scope, const QMap<QString, QVariant> &values)
{
    QMutexLocker locker(m_database.accessMutex());

    if (values.isEmpty())
        return true;

    if (!beginTransaction()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to begin database transaction while storing OOB"));
        return false;
    }

    QStringList tuples;
    QVariantList dataValues;
    const QChar colon(QChar::fromLatin1(':'));
    const QString bindString(QString::fromLatin1("(?,?,?)"));

    QMap<QString, QVariant>::const_iterator it = values.constBegin(), end = values.constEnd();
    for ( ; it != end; ++it) {
        tuples.append(bindString);
        dataValues.append(scope + colon + it.key());

        // If the data is large, compress it to reduce the IO cost
        const QVariant &var(it.value());
        if (var.type() == static_cast<QVariant::Type>(QMetaType::QByteArray)) {
            const QByteArray uncompressed(var.value<QByteArray>());
            if (uncompressed.size() > 512) {
                // Test the entropy of this data, if it is unlikely to compress significantly, don't try
                if (entropy(uncompressed.constBegin() + 256, uncompressed.constBegin() + 512, 256) < 0.85) {
                    dataValues.append(QVariant(qCompress(uncompressed)));
                    dataValues.append(1);
                    continue;
                }
            }
        } else if (var.type() == static_cast<QVariant::Type>(QMetaType::QString)) {
            const QString uncompressed(var.value<QString>());
            if (uncompressed.size() > 256) {
                dataValues.append(QVariant(qCompress(uncompressed.toUtf8())));
                dataValues.append(2);
                continue;
            }
        }

        // No compression:
        dataValues.append(var);
        dataValues.append(0);
    }

    QString statement(QString::fromLatin1("INSERT OR REPLACE INTO OOB (name, value, compressed) VALUES %1").arg(tuples.join(",")));

    QSqlQuery query(m_database);
    query.setForwardOnly(true);
    if (!query.prepare(statement)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare OOB insert:\n%1\nQuery:\n%2")
                .arg(query.lastError().text())
                .arg(statement));
    } else {
        foreach (const QVariant &v, dataValues) {
            query.addBindValue(v);
        }
        if (!ContactsDatabase::execute(query)) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to insert OOB: %1")
                    .arg(query.lastError().text()));
        } else {
            if (!commitTransaction()) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to commit database after storing OOB"));
                return false;
            }
            return true;
        }
    }

    rollbackTransaction();

    return false;
}

bool ContactWriter::removeOOB(const QString &scope, const QStringList &keys)
{
    QMutexLocker locker(m_database.accessMutex());

    if (!beginTransaction()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to begin database transaction while removing OOB"));
        return false;
    }

    QVariantList keyNames;

    QString statement(QString::fromLatin1("DELETE FROM OOB WHERE name "));

    if (keys.isEmpty()) {
        statement.append(QString::fromLatin1("LIKE '%1%%'").arg(scope));
    } else {
        const QChar colon(QChar::fromLatin1(':'));
        QString keyList;

        foreach (const QString &key, keys) {
            keyNames.append(scope + colon + key);
            keyList.append(QString::fromLatin1(keyList.isEmpty() ? "?" : ",?"));
        }

        statement.append(QString::fromLatin1("IN (%1)").arg(keyList));
    }

    QSqlQuery query(m_database);
    query.setForwardOnly(true);
    if (!query.prepare(statement)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to prepare OOB remove:\n%1\nQuery:\n%2")
                .arg(query.lastError().text())
                .arg(statement));
    } else {
        foreach (const QVariant &name, keyNames) {
            query.addBindValue(name);
        }

        if (!ContactsDatabase::execute(query)) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to query OOB: %1")
                    .arg(query.lastError().text()));
        } else {
            if (!commitTransaction()) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to commit database after removing OOB"));
                return false;
            }
            return true;
        }
    }

    rollbackTransaction();

    return false;
}

QMap<int, QString> contextTypes()
{
    QMap<int, QString> rv;

    rv.insert(QContactDetail::ContextHome, QString::fromLatin1("Home"));
    rv.insert(QContactDetail::ContextWork, QString::fromLatin1("Work"));
    rv.insert(QContactDetail::ContextOther, QString::fromLatin1("Other"));

    return rv;
}

QString contextString(int type)
{
    static const QMap<int, QString> types(contextTypes());

    QMap<int, QString>::const_iterator it = types.find(type);
    if (it != types.end()) {
        return *it;
    }
    return QString();
}

QVariant detailContexts(const QContactDetail &detail)
{
    static const QString separator = QString::fromLatin1(";");

    QStringList contexts;
    foreach (int context, detail.contexts()) {
        contexts.append(contextString(context));
    }
    return QVariant(contexts.join(separator));
}

quint32 writeCommonDetails(ContactsDatabase &db, quint32 contactId, const QContactDetail &detail,
                        bool syncable, bool wasLocal, bool aggregateContact, const QString &typeName, QContactManager::Error *error)
{
    const QString statement(QStringLiteral(
        " INSERT INTO Details ("
        "  contactId,"
        "  detail,"
        "  detailUri,"
        "  linkedDetailUris,"
        "  contexts,"
        "  accessConstraints,"
        "  provenance,"
        "  modifiable,"
        "  nonexportable)"
        " VALUES ("
        "  :contactId,"
        "  :detail,"
        "  :detailUri,"
        "  :linkedDetailUris,"
        "  :contexts,"
        "  :accessConstraints,"
        "  :provenance,"
        "  :modifiable,"
        "  :nonexportable)"
    ));

    ContactsDatabase::Query query(db.prepare(statement));

    const QVariant detailUri = detailValue(detail, QContactDetail::FieldDetailUri);
    const QVariant linkedDetailUris = QVariant(detail.linkedDetailUris().join(QStringLiteral(";")));
    const QVariant contexts = detailContexts(detail);
    const QVariant accessConstraints = static_cast<int>(detail.accessConstraints());
    const QVariant provenance = aggregateContact ? detailValue(detail, QContactDetail__FieldProvenance) : QVariant();
    const QVariant modifiable = wasLocal ? true : (syncable ? detailValue(detail, QContactDetail__FieldModifiable) : QVariant());
    const QVariant nonexportable = detailValue(detail, QContactDetail__FieldNonexportable);

    query.bindValue(":contactId", contactId);
    query.bindValue(":detail", typeName);
    query.bindValue(":detailUri", detailUri);
    query.bindValue(":linkedDetailUris", linkedDetailUris);
    query.bindValue(":contexts", contexts);
    query.bindValue(":accessConstraints", accessConstraints);
    query.bindValue(":provenance", provenance);
    query.bindValue(":modifiable", modifiable);
    query.bindValue(":nonexportable", nonexportable);

    if (!ContactsDatabase::execute(query)) {
        query.reportError(QStringLiteral("Failed to write common details for %1\ndetailUri: %2, linkedDetailUris: %3")
                .arg(typeName)
                .arg(detailUri.value<QString>())
                .arg(linkedDetailUris.value<QString>()));
        *error = QContactManager::UnspecifiedError;
        return 0;
    }

    return query.lastInsertId().value<quint32>();
}

template <typename T> quint32 ContactWriter::writeCommonDetails(
            quint32 contactId, const T &detail, bool syncable, bool wasLocal, bool aggregateContact, QContactManager::Error *error)
{
    return ::writeCommonDetails(m_database, contactId, detail, syncable, wasLocal, aggregateContact, detailTypeName<T>(), error);
}

// Define the type that another type is generated from
template<typename T>
struct GeneratorType { typedef T type; };
template<>
struct GeneratorType<QContactGlobalPresence> { typedef QContactPresence type; };

QContactDetail::DetailType generatorType(QContactDetail::DetailType type)
{
    if (type == QContactGlobalPresence::Type)
        return QContactPresence::Type;

    return type;
}

template<typename T>
struct RemoveStatement {};

template<> struct RemoveStatement<QContactAddress> { static const QString statement; };
const QString RemoveStatement<QContactAddress>::statement(QStringLiteral("DELETE FROM Addresses WHERE contactId = :contactId"));

template<> struct RemoveStatement<QContactAnniversary> { static const QString statement; };
const QString RemoveStatement<QContactAnniversary>::statement(QStringLiteral("DELETE FROM Anniversaries WHERE contactId = :contactId"));

template<> struct RemoveStatement<QContactAvatar> { static const QString statement; };
const QString RemoveStatement<QContactAvatar>::statement(QStringLiteral("DELETE FROM Avatars WHERE contactId = :contactId"));

template<> struct RemoveStatement<QContactBirthday> { static const QString statement; };
const QString RemoveStatement<QContactBirthday>::statement(QStringLiteral("DELETE FROM Birthdays WHERE contactId = :contactId"));

template<> struct RemoveStatement<QContactEmailAddress> { static const QString statement; };
const QString RemoveStatement<QContactEmailAddress>::statement(QStringLiteral("DELETE FROM EmailAddresses WHERE contactId = :contactId"));

template<> struct RemoveStatement<QContactFamily> { static const QString statement; };
const QString RemoveStatement<QContactFamily>::statement(QStringLiteral("DELETE FROM Families WHERE contactId = :contactId"));

template<> struct RemoveStatement<QContactGeoLocation> { static const QString statement; };
const QString RemoveStatement<QContactGeoLocation>::statement(QStringLiteral("DELETE FROM GeoLocations WHERE contactId = :contactId"));

template<> struct RemoveStatement<QContactGlobalPresence> { static const QString statement; };
const QString RemoveStatement<QContactGlobalPresence>::statement(QStringLiteral("DELETE FROM GlobalPresences WHERE contactId = :contactId"));

template<> struct RemoveStatement<QContactGuid> { static const QString statement; };
const QString RemoveStatement<QContactGuid>::statement(QStringLiteral("DELETE FROM Guids WHERE contactId = :contactId"));

template<> struct RemoveStatement<QContactHobby> { static const QString statement; };
const QString RemoveStatement<QContactHobby>::statement(QStringLiteral("DELETE FROM Hobbies WHERE contactId = :contactId"));

template<> struct RemoveStatement<QContactNickname> { static const QString statement; };
const QString RemoveStatement<QContactNickname>::statement(QStringLiteral("DELETE FROM Nicknames WHERE contactId = :contactId"));

template<> struct RemoveStatement<QContactNote> { static const QString statement; };
const QString RemoveStatement<QContactNote>::statement(QStringLiteral("DELETE FROM Notes WHERE contactId = :contactId"));

template<> struct RemoveStatement<QContactOnlineAccount> { static const QString statement; };
const QString RemoveStatement<QContactOnlineAccount>::statement(QStringLiteral("DELETE FROM OnlineAccounts WHERE contactId = :contactId"));

template<> struct RemoveStatement<QContactOrganization> { static const QString statement; };
const QString RemoveStatement<QContactOrganization>::statement(QStringLiteral("DELETE FROM Organizations WHERE contactId = :contactId"));

template<> struct RemoveStatement<QContactPhoneNumber> { static const QString statement; };
const QString RemoveStatement<QContactPhoneNumber>::statement(QStringLiteral("DELETE FROM PhoneNumbers WHERE contactId = :contactId"));

template<> struct RemoveStatement<QContactPresence> { static const QString statement; };
const QString RemoveStatement<QContactPresence>::statement(QStringLiteral("DELETE FROM Presences WHERE contactId = :contactId"));

template<> struct RemoveStatement<QContactRingtone> { static const QString statement; };
const QString RemoveStatement<QContactRingtone>::statement(QStringLiteral("DELETE FROM Ringtones WHERE contactId = :contactId"));

template<> struct RemoveStatement<QContactTag> { static const QString statement; };
const QString RemoveStatement<QContactTag>::statement(QStringLiteral("DELETE FROM Tags WHERE contactId = :contactId"));

template<> struct RemoveStatement<QContactUrl> { static const QString statement; };
const QString RemoveStatement<QContactUrl>::statement(QStringLiteral("DELETE FROM Urls WHERE contactId = :contactId"));

template<> struct RemoveStatement<QContactOriginMetadata> { static const QString statement; };
const QString RemoveStatement<QContactOriginMetadata>::statement(QStringLiteral("DELETE FROM OriginMetadata WHERE contactId = :contactId"));

template<> struct RemoveStatement<QContactExtendedDetail> { static const QString statement; };
const QString RemoveStatement<QContactExtendedDetail>::statement(QStringLiteral("DELETE FROM ExtendedDetails WHERE contactId = :contactId"));

bool removeSpecificDetails(ContactsDatabase &db, quint32 contactId, const QString &statement, const QString &typeName, QContactManager::Error *error)
{
    ContactsDatabase::Query query(db.prepare(statement));
    query.bindValue(0, contactId);

    if (!ContactsDatabase::execute(query)) {
        query.reportError(QStringLiteral("Failed to remove existing details of type %1 for %2").arg(typeName).arg(contactId));
        *error = QContactManager::UnspecifiedError;
        return false;
    }

    return true;
}

template <typename T> bool removeSpecificDetails(ContactsDatabase &db, quint32 contactId, QContactManager::Error *error)
{
    return removeSpecificDetails(db, contactId, RemoveStatement<T>::statement, detailTypeName<T>(), error);
}

static void adjustAggregateDetailProperties(QContactDetail &detail)
{
    // Modify this detail URI to preserve uniqueness - the result must not clash with the
    // URI in the constituent's copy (there won't be any other aggregator of the same detail)

    // If a detail URI is modified for aggregation, we need to insert a prefix
    const QString aggregateTag(QStringLiteral("aggregate"));
    const QString prefix(aggregateTag + QStringLiteral(":"));

    QString detailUri = detail.detailUri();
    if (!detailUri.isEmpty() && !detailUri.startsWith(prefix)) {
        if (detailUri.startsWith(aggregateTag)) {
            // Remove any invalid aggregate prefix that may have been previously stored
            int index = detailUri.indexOf(QChar::fromLatin1(':'));
            detailUri = detailUri.mid(index + 1);
        }
        detail.setDetailUri(prefix + detailUri);
    }

    QStringList linkedDetailUris = detail.linkedDetailUris();
    if (!linkedDetailUris.isEmpty()) {
        QStringList::iterator it = linkedDetailUris.begin(), end = linkedDetailUris.end();
        for ( ; it != end; ++it) {
            QString &linkedUri(*it);
            if (!linkedUri.isEmpty() && !linkedUri.startsWith(prefix)) {
                if (linkedUri.startsWith(aggregateTag)) {
                    // Remove any invalid aggregate prefix that may have been previously stored
                    int index = linkedUri.indexOf(QChar::fromLatin1(':'));
                    linkedUri = linkedUri.mid(index + 1);
                }
                linkedUri.insert(0, prefix);
            }
        }
        detail.setLinkedDetailUris(linkedDetailUris);
    }
}

namespace {

QStringList subTypeList(const QList<int> &subTypes)
{
    QStringList rv;
    foreach (int subType, subTypes) {
        rv.append(QString::number(subType));
    }
    return rv;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, const QContactAddress &detail)
{
    const QString statement(QStringLiteral(
        " INSERT INTO Addresses ("
        "  detailId,"
        "  contactId,"
        "  street,"
        "  postOfficeBox,"
        "  region,"
        "  locality,"
        "  postCode,"
        "  country,"
        "  subTypes)"
        " VALUES ("
        "  :detailId,"
        "  :contactId,"
        "  :street,"
        "  :postOfficeBox,"
        "  :region,"
        "  :locality,"
        "  :postCode,"
        "  :country,"
        "  :subTypes)"
    ));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactAddress T;
    query.addBindValue(detailId);
    query.addBindValue(contactId);
    query.addBindValue(detail.value<QString>(T::FieldStreet).trimmed());
    query.addBindValue(detail.value<QString>(T::FieldPostOfficeBox).trimmed());
    query.addBindValue(detail.value<QString>(T::FieldRegion).trimmed());
    query.addBindValue(detail.value<QString>(T::FieldLocality).trimmed());
    query.addBindValue(detail.value<QString>(T::FieldPostcode).trimmed());
    query.addBindValue(detail.value<QString>(T::FieldCountry).trimmed());
    query.addBindValue(subTypeList(detail.subTypes()).join(QLatin1String(";")));
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, const QContactAnniversary &detail)
{
    const QString statement(QStringLiteral(
        " INSERT INTO Anniversaries ("
        "  detailId,"
        "  contactId,"
        "  originalDateTime,"
        "  calendarId,"
        "  subType,"
        "  event)"
        " VALUES ("
        "  :detailId,"
        "  :contactId,"
        "  :originalDateTime,"
        "  :calendarId,"
        "  :subType,"
        "  :event)"
    ));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactAnniversary T;
    query.addBindValue(detailId);
    query.addBindValue(contactId);
    query.addBindValue(detailValue(detail, T::FieldOriginalDate));
    query.addBindValue(detailValue(detail, T::FieldCalendarId));
    query.addBindValue(detail.hasValue(T::FieldSubType) ? QString::number(detail.subType()) : QString());
    query.addBindValue(detail.value<QString>(T::FieldEvent).trimmed());
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, const QContactAvatar &detail)
{
    const QString statement(QStringLiteral(
        " INSERT INTO Avatars ("
        "  detailId,"
        "  contactId,"
        "  imageUrl,"
        "  videoUrl,"
        "  avatarMetadata)"
        " VALUES ("
        "  :detailId,"
        "  :contactId,"
        "  :imageUrl,"
        "  :videoUrl,"
        "  :avatarMetadata)"
    ));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactAvatar T;
    query.addBindValue(detailId);
    query.addBindValue(contactId);
    query.addBindValue(detail.value<QString>(T::FieldImageUrl).trimmed());
    query.addBindValue(detail.value<QString>(T::FieldVideoUrl).trimmed());
    query.addBindValue(detailValue(detail, QContactAvatar__FieldAvatarMetadata));
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, const QContactBirthday &detail)
{
    const QString statement(QStringLiteral(
        " INSERT INTO Birthdays ("
        "  detailId,"
        "  contactId,"
        "  birthday,"
        "  calendarId)"
        " VALUES ("
        "  :detailId,"
        "  :contactId,"
        "  :birthday,"
        "  :calendarId)"
    ));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactBirthday T;
    query.addBindValue(detailId);
    query.addBindValue(contactId);
    query.addBindValue(detailValue(detail, T::FieldBirthday));
    query.addBindValue(detailValue(detail, T::FieldCalendarId));
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, const QContactEmailAddress &detail)
{
    const QString statement(QStringLiteral(
        " INSERT INTO EmailAddresses ("
        "  detailId,"
        "  contactId,"
        "  emailAddress,"
        "  lowerEmailAddress)"
        " VALUES ("
        "  :detailId,"
        "  :contactId,"
        "  :emailAddress,"
        "  :lowerEmailAddress)"
    ));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactEmailAddress T;
    const QString address(detail.value<QString>(T::FieldEmailAddress).trimmed());
    query.addBindValue(detailId);
    query.addBindValue(contactId);
    query.addBindValue(address);
    query.addBindValue(address.toLower());
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, const QContactFamily &detail)
{
    const QString statement(QStringLiteral(
        " INSERT INTO Families ("
        "  detailId,"
        "  contactId,"
        "  spouse,"
        "  children)"
        " VALUES ("
        "  :detailId,"
        "  :contactId,"
        "  :spouse,"
        "  :children)"
    ));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactFamily T;
    query.addBindValue(detailId);
    query.addBindValue(contactId);
    query.addBindValue(detail.value<QString>(T::FieldSpouse).trimmed());
    query.addBindValue(detail.value<QStringList>(T::FieldChildren).join(QLatin1String(";")));
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, const QContactGeoLocation &detail)
{
    const QString statement(QStringLiteral(
        " INSERT INTO GeoLocations ("
        "  detailId,"
        "  contactId,"
        "  label,"
        "  latitude,"
        "  longitude,"
        "  accuracy,"
        "  altitude,"
        "  altitudeAccuracy,"
        "  heading,"
        "  speed,"
        "  timestamp)"
        " VALUES ("
        "  :detailId,"
        "  :contactId,"
        "  :label,"
        "  :latitude,"
        "  :longitude,"
        "  :accuracy,"
        "  :altitude,"
        "  :altitudeAccuracy,"
        "  :heading,"
        "  :speed,"
        "  :timestamp)"
    ));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactGeoLocation T;
    query.addBindValue(detailId);
    query.addBindValue(contactId);
    query.addBindValue(detail.value<QString>(T::FieldLabel).trimmed());
    query.addBindValue(detail.value<double>(T::FieldLatitude));
    query.addBindValue(detail.value<double>(T::FieldLongitude));
    query.addBindValue(detail.value<double>(T::FieldAccuracy));
    query.addBindValue(detail.value<double>(T::FieldAltitude));
    query.addBindValue(detail.value<double>(T::FieldAltitudeAccuracy));
    query.addBindValue(detail.value<double>(T::FieldHeading));
    query.addBindValue(detail.value<double>(T::FieldSpeed));
    query.addBindValue(ContactsDatabase::dateTimeString(detail.value<QDateTime>(T::FieldTimestamp).toUTC()));
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, const QContactGlobalPresence &detail)
{
    const QString statement(QStringLiteral(
        " INSERT INTO GlobalPresences ("
        "  detailId,"
        "  contactId,"
        "  presenceState,"
        "  timestamp,"
        "  nickname,"
        "  customMessage,"
        "  presenceStateText,"
        "  presenceStateImageUrl)"
        " VALUES ("
        "  :detailId,"
        "  :contactId,"
        "  :presenceState,"
        "  :timestamp,"
        "  :nickname,"
        "  :customMessage,"
        "  :presenceStateText,"
        "  :presenceStateImageUrl)"
    ));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactGlobalPresence T;
    query.addBindValue(detailId);
    query.addBindValue(contactId);
    query.addBindValue(detailValue(detail, T::FieldPresenceState));
    query.addBindValue(ContactsDatabase::dateTimeString(detail.value<QDateTime>(T::FieldTimestamp).toUTC()));
    query.addBindValue(detail.value<QString>(T::FieldNickname).trimmed());
    query.addBindValue(detail.value<QString>(T::FieldCustomMessage).trimmed());
    query.addBindValue(detail.value<QString>(T::FieldPresenceStateText).trimmed());
    query.addBindValue(detail.value<QString>(T::FieldPresenceStateImageUrl).trimmed());
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, const QContactGuid &detail)
{
    const QString statement(QStringLiteral(
        " INSERT INTO Guids ("
        "  detailId,"
        "  contactId,"
        "  guid)"
        " VALUES ("
        "  :detailId,"
        "  :contactId,"
        "  :guid)"
    ));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactGuid T;
    query.addBindValue(detailId);
    query.addBindValue(contactId);
    query.addBindValue(detailValue(detail, T::FieldGuid));
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, const QContactHobby &detail)
{
    const QString statement(QStringLiteral(
        " INSERT INTO Hobbies ("
        "  detailId,"
        "  contactId,"
        "  hobby)"
        " VALUES ("
        "  :detailId,"
        "  :contactId,"
        "  :hobby)"
    ));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactHobby T;
    query.addBindValue(detailId);
    query.addBindValue(contactId);
    query.addBindValue(detailValue(detail, T::FieldHobby));
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, const QContactNickname &detail)
{
    const QString statement(QStringLiteral(
        " INSERT INTO Nicknames ("
        "  detailId,"
        "  contactId,"
        "  nickname,"
        "  lowerNickname)"
        " VALUES ("
        "  :detailId,"
        "  :contactId,"
        "  :nickname,"
        "  :lowerNickname)"
    ));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactNickname T;
    const QString nickname(detail.value<QString>(T::FieldNickname).trimmed());
    query.addBindValue(detailId);
    query.addBindValue(contactId);
    query.addBindValue(nickname);
    query.addBindValue(nickname.toLower());
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, const QContactNote &detail)
{
    const QString statement(QStringLiteral(
        " INSERT INTO Notes ("
        "  detailId,"
        "  contactId,"
        "  note)"
        " VALUES ("
        "  :detailId,"
        "  :contactId,"
        "  :note)"
    ));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactNote T;
    query.addBindValue(detailId);
    query.addBindValue(contactId);
    query.addBindValue(detailValue(detail, T::FieldNote));
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, const QContactOnlineAccount &detail)
{
    const QString statement(QStringLiteral(
        " INSERT INTO OnlineAccounts ("
        "  detailId,"
        "  contactId,"
        "  accountUri,"
        "  lowerAccountUri,"
        "  protocol,"
        "  serviceProvider,"
        "  capabilities,"
        "  subTypes,"
        "  accountPath,"
        "  accountIconPath,"
        "  enabled,"
        "  accountDisplayName,"
        "  serviceProviderDisplayName)"
        " VALUES ("
        "  :detailId,"
        "  :contactId,"
        "  :accountUri,"
        "  :lowerAccountUri,"
        "  :protocol,"
        "  :serviceProvider,"
        "  :capabilities,"
        "  :subTypes,"
        "  :accountPath,"
        "  :accountIconPath,"
        "  :enabled,"
        "  :accountDisplayName,"
        "  :serviceProviderDisplayName)"
    ));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactOnlineAccount T;
    const QString uri(detail.value<QString>(T::FieldAccountUri).trimmed());
    query.addBindValue(detailId);
    query.addBindValue(contactId);
    query.addBindValue(uri);
    query.addBindValue(uri.toLower());
    query.addBindValue(QString::number(detail.protocol()));
    query.addBindValue(detailValue(detail, T::FieldServiceProvider));
    query.addBindValue(detailValue(detail, T::FieldCapabilities).value<QStringList>().join(QLatin1String(";")));
    query.addBindValue(subTypeList(detail.subTypes()).join(QLatin1String(";")));
    query.addBindValue(detailValue(detail, QContactOnlineAccount__FieldAccountPath));
    query.addBindValue(detailValue(detail, QContactOnlineAccount__FieldAccountIconPath));
    query.addBindValue(detailValue(detail, QContactOnlineAccount__FieldEnabled));
    query.addBindValue(detailValue(detail, QContactOnlineAccount__FieldAccountDisplayName));
    query.addBindValue(detailValue(detail, QContactOnlineAccount__FieldServiceProviderDisplayName));
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, const QContactOrganization &detail)
{
    const QString statement(QStringLiteral(
        " INSERT INTO Organizations ("
        "  detailId,"
        "  contactId,"
        "  name,"
        "  role,"
        "  title,"
        "  location,"
        "  department,"
        "  logoUrl,"
        "  assistantName)"
        " VALUES ("
        "  :detailId,"
        "  :contactId,"
        "  :name,"
        "  :role,"
        "  :title,"
        "  :location,"
        "  :department,"
        "  :logoUrl,"
        "  :assistantName)"
    ));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactOrganization T;
    query.addBindValue(detailId);
    query.addBindValue(contactId);
    query.addBindValue(detail.value<QString>(T::FieldName).trimmed());
    query.addBindValue(detail.value<QString>(T::FieldRole).trimmed());
    query.addBindValue(detail.value<QString>(T::FieldTitle).trimmed());
    query.addBindValue(detail.value<QString>(T::FieldLocation).trimmed());
    query.addBindValue(detail.department().join(QLatin1String(";")));
    query.addBindValue(detail.value<QString>(T::FieldLogoUrl).trimmed());
    query.addBindValue(detail.value<QString>(T::FieldAssistantName).trimmed());
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, const QContactPhoneNumber &detail)
{
    const QString statement(QStringLiteral(
        " INSERT INTO PhoneNumbers ("
        "  detailId,"
        "  contactId,"
        "  phoneNumber,"
        "  subTypes,"
        "  normalizedNumber)"
        " VALUES ("
        "  :detailId,"
        "  :contactId,"
        "  :phoneNumber,"
        "  :subTypes,"
        "  :normalizedNumber)"
    ));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactPhoneNumber T;
    query.addBindValue(detailId);
    query.addBindValue(contactId);
    query.addBindValue(detail.value<QString>(T::FieldNumber).trimmed());
    query.addBindValue(subTypeList(detail.subTypes()).join(QLatin1String(";")));
    query.addBindValue(QVariant(ContactsEngine::normalizedPhoneNumber(detail.number())));
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, const QContactPresence &detail)
{
    const QString statement(QStringLiteral(
        " INSERT INTO Presences ("
        "  detailId,"
        "  contactId,"
        "  presenceState,"
        "  timestamp,"
        "  nickname,"
        "  customMessage,"
        "  presenceStateText,"
        "  presenceStateImageUrl)"
        " VALUES ("
        "  :detailId,"
        "  :contactId,"
        "  :presenceState,"
        "  :timestamp,"
        "  :nickname,"
        "  :customMessage,"
        "  :presenceStateText,"
        "  :presenceStateImageUrl)"
    ));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactPresence T;
    query.addBindValue(detailId);
    query.addBindValue(contactId);
    query.addBindValue(detailValue(detail, T::FieldPresenceState));
    query.addBindValue(ContactsDatabase::dateTimeString(detail.value<QDateTime>(T::FieldTimestamp).toUTC()));
    query.addBindValue(detail.value<QString>(T::FieldNickname).trimmed());
    query.addBindValue(detail.value<QString>(T::FieldCustomMessage).trimmed());
    query.addBindValue(detail.value<QString>(T::FieldPresenceStateText).trimmed());
    query.addBindValue(detail.value<QString>(T::FieldPresenceStateImageUrl).trimmed());
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, const QContactRingtone &detail)
{
    const QString statement(QStringLiteral(
        " INSERT INTO Ringtones ("
        "  detailId,"
        "  contactId,"
        "  audioRingtone,"
        "  videoRingtone,"
        "  vibrationRingtone)"
        " VALUES ("
        "  :detailId,"
        "  :contactId,"
        "  :audioRingtone,"
        "  :videoRingtone,"
        "  :vibrationRingtone)"
    ));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactRingtone T;
    query.addBindValue(detailId);
    query.addBindValue(contactId);
    query.addBindValue(detail.value<QString>(T::FieldAudioRingtoneUrl).trimmed());
    query.addBindValue(detail.value<QString>(T::FieldVideoRingtoneUrl).trimmed());
    query.addBindValue(detail.value<QString>(T::FieldVibrationRingtoneUrl).trimmed());
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, const QContactTag &detail)
{
    const QString statement(QStringLiteral(
        " INSERT INTO Tags ("
        "  detailId,"
        "  contactId,"
        "  tag)"
        " VALUES ("
        "  :detailId,"
        "  :contactId,"
        "  :tag)"
    ));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactTag T;
    query.addBindValue(detailId);
    query.addBindValue(contactId);
    query.addBindValue(detail.value<QString>(T::FieldTag).trimmed());
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, const QContactUrl &detail)
{
    const QString statement(QStringLiteral(
        " INSERT INTO Urls ("
        "  detailId,"
        "  contactId,"
        "  url,"
        "  subTypes)"
        " VALUES ("
        "  :detailId,"
        "  :contactId,"
        "  :url,"
        "  :subTypes)"
    ));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactUrl T;
    query.addBindValue(detailId);
    query.addBindValue(contactId);
    query.addBindValue(detail.value<QString>(T::FieldUrl).trimmed());
    query.addBindValue(detail.hasValue(T::FieldSubType) ? QString::number(detail.subType()) : QString());
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, const QContactOriginMetadata &detail)
{
    const QString statement(QStringLiteral(
        " INSERT INTO OriginMetadata ("
        "  detailId,"
        "  contactId,"
        "  id,"
        "  groupId,"
        "  enabled)"
        " VALUES ("
        "  :detailId,"
        "  :contactId,"
        "  :id,"
        "  :groupId,"
        "  :enabled)"
    ));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactOriginMetadata T;
    query.addBindValue(detailId);
    query.addBindValue(contactId);
    query.addBindValue(detailValue(detail, T::FieldId));
    query.addBindValue(detailValue(detail, T::FieldGroupId));
    query.addBindValue(detailValue(detail, T::FieldEnabled));
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, const QContactExtendedDetail &detail)
{
    const QString statement(QStringLiteral(
        " INSERT INTO ExtendedDetails ("
        "  detailId,"
        "  contactId,"
        "  name,"
        "  data)"
        " VALUES ("
        "  :detailId,"
        "  :contactId,"
        "  :name,"
        "  :data)"
    ));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactExtendedDetail T;
    query.addBindValue(detailId);
    query.addBindValue(contactId);
    query.addBindValue(detailValue(detail, T::FieldName));
    query.addBindValue(detailValue(detail, T::FieldData));
    return query;
}

}

template <typename T> bool ContactWriter::writeDetails(
        quint32 contactId,
        QContact *contact,
        const DetailList &definitionMask,
        const QContactCollectionId &collectionId,
        bool syncable,
        bool wasLocal,
        QContactManager::Error *error)
{
    if (!definitionMask.isEmpty() &&                                          // only a subset of detail types are being written
        !detailListContains<T>(definitionMask) &&                             // this type is not in the set
        !detailListContains<typename GeneratorType<T>::type>(definitionMask)) // this type's generator type is not in the set
        return true;

    if (!removeCommonDetails<T>(contactId, error))
        return false;

    if (!removeSpecificDetails<T>(m_database, contactId, error))
        return false;

    const bool aggregateContact(ContactCollectionId::databaseId(collectionId) == ContactsDatabase::AggregateAddressbookCollectionId);

    QList<T> contactDetails(contact->details<T>());
    typename QList<T>::iterator it = contactDetails.begin(), end = contactDetails.end();
    for ( ; it != end; ++it) {
        T &detail(*it);

        if (aggregateContact) {
            adjustAggregateDetailProperties(detail);
        }

        quint32 detailId = writeCommonDetails(contactId, detail, syncable, wasLocal, aggregateContact, error);
        if (detailId == 0) {
            return false;
        }

        if (!aggregateContact) {
            // Insert the provenance value into the detail, now that we have it
            const QString provenance(QStringLiteral("%1:%2:%3").arg(contactId).arg(detailId).arg(ContactCollectionId::databaseId(collectionId)));
            detail.setValue(QContactDetail__FieldProvenance, provenance);
        }

        ContactsDatabase::Query query = bindDetail(m_database, contactId, detailId, detail);
        if (!ContactsDatabase::execute(query)) {
            query.reportError(QStringLiteral("Failed to write details for %1").arg(detailTypeName<T>()));
            *error = QContactManager::UnspecifiedError;
            return false;
        }

        contact->saveDetail(&detail, QContact::IgnoreAccessConstraints);
    }
    return true;
}

static int presenceOrder(QContactPresence::PresenceState state)
{
#ifdef SORT_PRESENCE_BY_AVAILABILITY
    if (state == QContactPresence::PresenceAvailable) {
        return 0;
    } else if (state == QContactPresence::PresenceAway) {
        return 1;
    } else if (state == QContactPresence::PresenceExtendedAway) {
        return 2;
    } else if (state == QContactPresence::PresenceBusy) {
        return 3;
    } else if (state == QContactPresence::PresenceHidden) {
        return 4;
    } else if (state == QContactPresence::PresenceOffline) {
        return 5;
    }
    return 6;
#else
    return static_cast<int>(state);
#endif
}

static bool betterPresence(const QContactPresence &detail, const QContactPresence &best)
{
    if (best.isEmpty())
        return true;

    QContactPresence::PresenceState detailState(detail.presenceState());
    if (detailState == QContactPresence::PresenceUnknown)
        return false;

    return ((presenceOrder(detailState) < presenceOrder(best.presenceState())) ||
            best.presenceState() == QContactPresence::PresenceUnknown);
}

QContactManager::Error ContactWriter::save(
            QList<QContact> *contacts,
            const DetailList &definitionMask,
            QMap<int, bool> *aggregatesUpdated,
            QMap<int, QContactManager::Error> *errorMap,
            bool withinTransaction,
            bool withinAggregateUpdate,
            bool withinSyncUpdate)
{
    QMutexLocker locker(m_database.accessMutex());

    if (contacts->isEmpty())
        return QContactManager::NoError;

    // Check that all of the contacts have the same collectionId.
    // Note that empty == "local" for all intents and purposes.
    if (!withinAggregateUpdate && !withinSyncUpdate) {
        QContactCollectionId collectionId;
        foreach (const QContact &contact, *contacts) {
            // retrieve current contact's collectionId
            const QContactCollectionId currCollectionId = contact.collectionId().isNull()
                    ? ContactCollectionId::apiId(ContactsDatabase::LocalAddressbookCollectionId, m_managerUri)
                    : contact.collectionId();
                    // XXXXXXXXXXXXXXXX TODO: if it's a sync update, use the sync collection id instead of the local addressbook collection id...
                    // but maybe that should be done at different layer (i.e. in syncSave() or whatever, which then calls this save() method..)

            if (collectionId.isNull()) {
                collectionId = currCollectionId;
            }

            // determine whether it's valid
            if (collectionId == ContactCollectionId::apiId(ContactsDatabase::AggregateAddressbookCollectionId, m_managerUri)) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Error: contacts from aggregate collection specified in batch save!"));
                return QContactManager::UnspecifiedError;
            } else if (collectionId != currCollectionId) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Error: contacts from multiple collections specified in single batch save!"));
                return QContactManager::UnspecifiedError;
            }

            // Also verify the type of this contact
            const int contactType(contact.detail<QContactType>().type());
            if (contactType != QContactType::TypeContact) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Error: contact type %1 is not supported").arg(contactType));
                return QContactManager::UnspecifiedError;
            }
        }
    }

    if (!withinTransaction && !beginTransaction()) {
        // only create a transaction if we're not within one already
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to begin database transaction while saving contacts"));
        return QContactManager::UnspecifiedError;
    }

    static const DetailList presenceUpdateDetailTypes(getPresenceUpdateDetailTypes());

    bool presenceOnlyUpdate = false;
    if (definitionMask.contains(detailType<QContactPresence>())) {
        // If we only update presence/origin-metadata/online-account, we will report
        // this change as a presence change only
        presenceOnlyUpdate = true;
        foreach (const DetailList::value_type &type, definitionMask) {
            if (!presenceUpdateDetailTypes.contains(type)) {
                presenceOnlyUpdate = false;
                break;
            }
        }
    }

    bool possibleReactivation = false;
    QContactManager::Error worstError = QContactManager::NoError;
    QContactManager::Error err = QContactManager::NoError;
    for (int i = 0; i < contacts->count(); ++i) {
        QContact &contact = (*contacts)[i];
        QContactId contactId = ContactId::apiId(contact);
        quint32 dbId = ContactId::databaseId(contactId);

        bool aggregateUpdated = false;
        if (dbId == 0) {
            err = create(&contact, definitionMask, true, withinAggregateUpdate);
            if (err == QContactManager::NoError) {
                contactId = ContactId::apiId(contact);
                dbId = ContactId::databaseId(contactId);
                m_addedIds.insert(contactId);
            } else {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Error creating contact: %1 collectionId: %2")
                                          .arg(err).arg(ContactCollectionId::toString(contact.collectionId())));
            }
        } else {
            err = update(&contact, definitionMask, &aggregateUpdated, true, withinAggregateUpdate, presenceOnlyUpdate);
            if (err == QContactManager::NoError) {
                if (presenceOnlyUpdate) {
                    m_presenceChangedIds.insert(contactId);
                } else {
                    possibleReactivation = true;
                    m_changedIds.insert(contactId);
                }
            } else {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Error updating contact %1: %2").arg(ContactId::toString(contactId)).arg(err));
            }
        }
        if (err == QContactManager::NoError) {
            if (aggregatesUpdated) {
                aggregatesUpdated->insert(i, aggregateUpdated);
            }

            const QContactCollectionId currCollectionId = contact.collectionId().isNull()
                    ? ContactCollectionId::apiId(ContactsDatabase::LocalAddressbookCollectionId, m_managerUri)
                    : contact.collectionId();

            if (currCollectionId.isNull() || ContactCollectionId::databaseId(currCollectionId) == ContactsDatabase::LocalAddressbookCollectionId) {
                // This contact would cause changes to the partial aggregate for sync target contacts
                // XXXXXXXXXXXXXXXXXXX TODO: don't report changes to local contacts, up
                m_changedLocalIds.insert(dbId);
            } else if (ContactCollectionId::databaseId(currCollectionId) != ContactsDatabase::AggregateAddressbookCollectionId) {
                if (!m_suppressedCollectionIds.contains(currCollectionId)) {
                    m_changedSyncCollectionIds.insert(currCollectionId);
                }
            }
        } else {
            worstError = err;
            if (errorMap) {
                errorMap->insert(i, err);
            }
        }
    }

    if (!withinAggregateUpdate && possibleReactivation && worstError == QContactManager::NoError) {
        // Some contacts may need to have new aggregates created
        // if they previously had a QContactDeactivated detail
        // and this detail was removed (i.e. reactivated).
        QContactManager::Error aggregateError = aggregateOrphanedContacts(true);
        if (aggregateError != QContactManager::NoError)
            worstError = aggregateError;
    }

    if (!withinTransaction) {
        // only attempt to commit/rollback the transaction if we created it
        if (worstError != QContactManager::NoError) {
            // If anything failed at all, we need to rollback, so that we do not
            // have an inconsistent state between aggregate and constituent contacts

            // Any contacts we 'added' are not actually added - clear their IDs
            for (int i = 0; i < contacts->count(); ++i) {
                QContact &contact = (*contacts)[i];
                const QContactId contactId = ContactId::apiId(contact);
                if (m_addedIds.contains(contactId)) {
                    contact.setId(QContactId());
                    if (errorMap) {
                        // We also need to report an error for this contact, even though there
                        // is no true error preventing it from being updated
                        errorMap->insert(i, QContactManager::LockedError);
                    }
                }
            }

            rollbackTransaction();
            return worstError;
        } else if (!commitTransaction()) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to commit contacts"));
            return QContactManager::UnspecifiedError;
        }
    }

    return worstError;
}

template <typename T> void appendDetailType(ContactWriter::DetailList *list)
{
    list->append(T::Type);
}

static ContactWriter::DetailList allSupportedDetails()
{
    ContactWriter::DetailList details;

    appendDetailType<QContactAddress>(&details);
    appendDetailType<QContactAnniversary>(&details);
    appendDetailType<QContactAvatar>(&details);
    appendDetailType<QContactBirthday>(&details);
    appendDetailType<QContactDeactivated>(&details);
    appendDetailType<QContactDisplayLabel>(&details);
    appendDetailType<QContactEmailAddress>(&details);
    appendDetailType<QContactExtendedDetail>(&details);
    appendDetailType<QContactFamily>(&details);
    appendDetailType<QContactFavorite>(&details);
    appendDetailType<QContactGender>(&details);
    appendDetailType<QContactGeoLocation>(&details);
    appendDetailType<QContactGlobalPresence>(&details);
    appendDetailType<QContactGuid>(&details);
    appendDetailType<QContactHobby>(&details);
    appendDetailType<QContactName>(&details);
    appendDetailType<QContactNickname>(&details);
    appendDetailType<QContactNote>(&details);
    appendDetailType<QContactOnlineAccount>(&details);
    appendDetailType<QContactOrganization>(&details);
    appendDetailType<QContactOriginMetadata>(&details);
    appendDetailType<QContactPhoneNumber>(&details);
    appendDetailType<QContactPresence>(&details);
    appendDetailType<QContactRingtone>(&details);
    appendDetailType<QContactStatusFlags>(&details);
    appendDetailType<QContactSyncTarget>(&details);
    appendDetailType<QContactTag>(&details);
    appendDetailType<QContactTimestamp>(&details);
    appendDetailType<QContactType>(&details);
    appendDetailType<QContactUrl>(&details);

    return details;
}

static ContactWriter::DetailList allSingularDetails()
{
    ContactWriter::DetailList details;

    appendDetailType<QContactDisplayLabel>(&details);
    appendDetailType<QContactName>(&details);
    appendDetailType<QContactSyncTarget>(&details);
    appendDetailType<QContactFavorite>(&details);
    appendDetailType<QContactGender>(&details);
    appendDetailType<QContactTimestamp>(&details);
    appendDetailType<QContactBirthday>(&details);
    appendDetailType<QContactOriginMetadata>(&details);
    appendDetailType<QContactStatusFlags>(&details);
    appendDetailType<QContactDeactivated>(&details);

    return details;
}

static QContactDetail supportedDetailInstance(QContactDetail::DetailType type)
{
    if (allSupportedDetails().contains(type)) {
        return QContactDetail(type);
    }

    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("unsupported detail type: %1").arg(static_cast<int>(type)));
    return QContactDetail();
}

static QContactDetail contactDetailOfType(const QContact &c, QContactDetail::DetailType type)
{
    // this function is guaranteed to return a detail of the correct type
    // as long as the type parameter is a supported detail type.
    const QContactDetail &retn = c.detail(type);
    if (retn.type() == type) { // note, it could be QContactDetail::TypeUndefined, if no such detail existed in the contact.
        return retn;
    }
    return supportedDetailInstance(type); // default constructed detail of the correct type
}

static QContactManager::Error enforceDetailConstraints(QContact *contact)
{
    static const ContactWriter::DetailList supported(allSupportedDetails());
    static const ContactWriter::DetailList singular(allSingularDetails());

    QHash<ContactWriter::DetailList::value_type, int> detailCounts;

    QSet<QString> detailUris;

    // look for unsupported detail data.
    foreach (const QContactDetail &det, contact->details()) {
        if (!detailListContains(supported, det)) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Invalid detail type: %1 %2").arg(detailTypeName(det)).arg(det.type()));
            if (det.isEmpty()) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Detail is also empty!"));
            } else {
                QTCONTACTS_SQLITE_DEBUG(QString::fromLatin1("Dumping detail contents:"));
                dumpContactDetail(det);
            }
            QTCONTACTS_SQLITE_DEBUG(QString::fromLatin1("Dumping contact contents:"));
            dumpContact(*contact);
            return QContactManager::InvalidDetailError;
        } else {
            ++detailCounts[detailType(det)];

            // Verify that detail URIs are unique within the contact
            const QString detailUri(det.detailUri());
            if (!detailUri.isEmpty()) {
                if (detailUris.contains(detailUri)) {
                    // This URI conflicts with one already present in the contact
                    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Detail URI confict on: %1 %2 %3").arg(detailUri).arg(detailTypeName(det)).arg(det.type()));
                    return QContactManager::InvalidDetailError;
                }

                detailUris.insert(detailUri);
            }
        }
    }

    // enforce uniqueness constraints
    foreach (const ContactWriter::DetailList::value_type &type, singular) {
        if (detailCounts[type] > 1) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Invalid count of detail type %1: %2").arg(detailTypeName(type)).arg(detailCounts[type]));
            return QContactManager::LimitReachedError;
        }
    }

    return QContactManager::NoError;
}

static void adjustDetailUrisForLocal(QContactDetail &currDet)
{
    // A local detail should not reproduce the detail URI information from another contact's details
    currDet.setDetailUri(QString());
    currDet.setLinkedDetailUris(QStringList());
}

static void copyNameDetails(const QContact &src, QContact *dst)
{
    QContactName lcn = src.detail<QContactName>();

    bool copyName = (!lcn.firstName().isEmpty() || !lcn.lastName().isEmpty());
    if (!copyName) {
        // This name fails to adequately identify the contact - copy a nickname instead, if available
        copyName = (!lcn.prefix().isEmpty() || !lcn.middleName().isEmpty() || !lcn.suffix().isEmpty());
        foreach (QContactNickname nick, src.details<QContactNickname>()) {
            if (!nick.nickname().isEmpty()) {
                adjustDetailUrisForLocal(nick);
                dst->saveDetail(&nick, QContact::IgnoreAccessConstraints);

                // We have found a usable nickname - ignore the name detail
                copyName = false;
                break;
            }
        }
    }
    if (copyName) {
        adjustDetailUrisForLocal(lcn);
        dst->saveDetail(&lcn, QContact::IgnoreAccessConstraints);
    }
}

static bool promoteDetailType(QContactDetail::DetailType type, const ContactWriter::DetailList &definitionMask, bool forcePromotion)
{
    static const ContactWriter::DetailList unpromotedDetailTypes(getUnpromotedDetailTypes());
    static const ContactWriter::DetailList absolutelyUnpromotedDetailTypes(getAbsolutelyUnpromotedDetailTypes());

    // Timestamp is promoted in every update
    if (type == QContactTimestamp::Type)
        return true;

    if (!definitionMask.isEmpty() && !detailListContains(definitionMask, type))
        return false;

    // Some detail types are not promoted even if promotion is forced
    const ContactWriter::DetailList &unpromotedTypes(forcePromotion ? absolutelyUnpromotedDetailTypes : unpromotedDetailTypes);
    return !detailListContains(unpromotedTypes, type);
}

/*
    For every detail in a contact \a c, this function will check to see if an
    identical detail already exists in the \a aggregate contact.  If not, the
    detail from \a c will be "promoted" (saved in) the \a aggregate contact.

    Note that QContactSyncTarget and QContactGuid details will NOT be promoted,
    nor will QContactDisplayLabel or QContactType details.
*/
static void promoteDetailsToAggregate(const QContact &contact, QContact *aggregate, const ContactWriter::DetailList &definitionMask, bool forcePromotion)
{
    foreach (const QContactDetail &original, contact.details()) {
        if (!promoteDetailType(original.type(), definitionMask, forcePromotion)) {
            // skip this detail
            continue;
        }

        // promote this detail to the aggregate.  Depending on uniqueness,
        // this consists either of composition or duplication.
        // Note: Composed (unique) details won't have any detailUri!
        if (detailType(original) == detailType<QContactName>()) {
            // name involves composition
            QContactName cname(original);
            QContactName aname(aggregate->detail<QContactName>());
            if (!cname.prefix().isEmpty() && aname.prefix().isEmpty())
                aname.setPrefix(cname.prefix());
            if (!cname.firstName().isEmpty() && aname.firstName().isEmpty())
                aname.setFirstName(cname.firstName());
            if (!cname.middleName().isEmpty() && aname.middleName().isEmpty())
                aname.setMiddleName(cname.middleName());
            if (!cname.lastName().isEmpty() && aname.lastName().isEmpty())
                aname.setLastName(cname.lastName());
            if (!cname.suffix().isEmpty() && aname.suffix().isEmpty())
                aname.setSuffix(cname.suffix());
            QString customLabel = cname.value<QString>(QContactName__FieldCustomLabel);
            if (!customLabel.isEmpty() && aname.value<QString>(QContactName__FieldCustomLabel).isEmpty())
                aname.setValue(QContactName__FieldCustomLabel, cname.value(QContactName__FieldCustomLabel));
            aggregate->saveDetail(&aname, QContact::IgnoreAccessConstraints);
        } else if (detailType(original) == detailType<QContactTimestamp>()) {
            // timestamp involves composition
            // Note: From some sync sources, the creation timestamp will precede the existence of the local device.
            QContactTimestamp cts(original);
            QContactTimestamp ats(aggregate->detail<QContactTimestamp>());
            if (cts.lastModified().isValid() && (!ats.lastModified().isValid() || cts.lastModified() > ats.lastModified())) {
                ats.setLastModified(cts.lastModified());
            }
            if (cts.created().isValid() && !ats.created().isValid()) {
                ats.setCreated(cts.created());
            }
            aggregate->saveDetail(&ats, QContact::IgnoreAccessConstraints);
        } else if (detailType(original) == detailType<QContactGender>()) {
            // gender involves composition
            QContactGender cg(original);
            QContactGender ag(aggregate->detail<QContactGender>());
            // In Qtpim, uninitialized gender() does not default to GenderUnspecified...
            if (cg.gender() != QContactGender::GenderUnspecified &&
                (ag.gender() != QContactGender::GenderMale && ag.gender() != QContactGender::GenderFemale)) {
                ag.setGender(cg.gender());
                aggregate->saveDetail(&ag, QContact::IgnoreAccessConstraints);
            }
        } else if (detailType(original) == detailType<QContactFavorite>()) {
            // favorite involves composition
            QContactFavorite cf(original);
            QContactFavorite af(aggregate->detail<QContactFavorite>());
            if (cf.isFavorite() && !af.isFavorite()) {
                af.setFavorite(true);
                aggregate->saveDetail(&af, QContact::IgnoreAccessConstraints);
            }
        } else if (detailType(original) == detailType<QContactBirthday>()) {
            // birthday involves composition (at least, it's unique)
            QContactBirthday cb(original);
            QContactBirthday ab(aggregate->detail<QContactBirthday>());
            if (!ab.dateTime().isValid() && cb.dateTime().isValid()) {
                ab.setDateTime(cb.dateTime());
                aggregate->saveDetail(&ab, QContact::IgnoreAccessConstraints);
            }
        } else {
            // All other details involve duplication.
            // Only duplicate from contact to the aggregate if an identical detail doesn't already exist in the aggregate.
            QContactDetail det(original);

            bool needsPromote = true;
            foreach (const QContactDetail &ad, aggregate->details()) {
                if (detailsEquivalent(det, ad)) {
                    needsPromote = false;
                    break;
                }
            }

            if (needsPromote) {
                // all aggregate details are non-modifiable.
                QContactManagerEngine::setDetailAccessConstraints(&det, QContactDetail::ReadOnly | QContactDetail::Irremovable);
                det.setValue(QContactDetail__FieldModifiable, false);

                // Store the provenance of this promoted detail
                det.setValue(QContactDetail__FieldProvenance, original.value<QString>(QContactDetail__FieldProvenance));

                aggregate->saveDetail(&det, QContact::IgnoreAccessConstraints);
            }
        }
    }
}

// TODO: StringPair is typically used for <provenance>:<detailtype> - now that provenance
// uniquely identifies a detail, we probably don't need detailtype in most uses...
typedef QPair<QString, QString> StringPair;
typedef QPair<QContactDetail, QContactDetail> DetailPair;

static QList<QPair<QContactDetail, StringPair> > promotableDetails(const QContact &contact, bool forcePromotion = false, const ContactWriter::DetailList &definitionMask = ContactWriter::DetailList())
{
    QList<QPair<QContactDetail, StringPair> > rv;

    foreach (const QContactDetail &original, contact.details()) {
        if (!promoteDetailType(original.type(), definitionMask, forcePromotion)) {
            // Ignore details that won't be promoted to the aggregate
            continue;
        }

        // Make immutable copies of the contacts' detail
        QContactDetail copy(original);
        QContactManagerEngine::setDetailAccessConstraints(&copy, QContactDetail::ReadOnly | QContactDetail::Irremovable);

        const QString provenance(original.value<QString>(QContactDetail__FieldProvenance));
        const StringPair identity(qMakePair(provenance, detailTypeName(original)));
        rv.append(qMakePair(copy, identity));
    }

    return rv;
}

static void removeEquivalentDetails(QList<QPair<QContactDetail, StringPair> > &original, QList<QPair<QContactDetail, StringPair> > &updated)
{
    // Determine which details are in the update contact which aren't in the database contact:
    // Detail order is not defined, so loop over the entire set for each, removing matches or
    // superset details (eg, backend added a field (like lastModified to timestamp) on previous save)
    QList<QPair<QContactDetail, StringPair> >::iterator oit = original.begin(), oend;
    while (oit != original.end()) {
        QList<QPair<QContactDetail, StringPair> >::iterator uit = updated.begin(), uend = updated.end();
        for ( ; uit != uend; ++uit) {
            if (detailsEquivalent((*oit).first, (*uit).first)) {
                // These details match - remove from the lists
                updated.erase(uit);
                break;
            }
        }
        if (uit != uend) {
            // We found a match
            oit = original.erase(oit);
        } else {
            ++oit;
        }
    }
}

/*
   This function is called when a new contact is created.  The
   aggregate contacts are searched for a match, and the matching
   one updated if it exists; or a new aggregate is created.
*/
QContactManager::Error ContactWriter::updateOrCreateAggregate(QContact *contact, const DetailList &definitionMask, bool withinTransaction, quint32 *aggregateContactId)
{
    // 1) search for match
    // 2) if exists, update the existing aggregate (by default, non-clobber:
    //    only update empty fields of details, or promote non-existent details.  Never delete or replace details.)
    // 3) otherwise, create new aggregate, consisting of all details of contact, return.

    quint32 existingAggregateId = 0;
    QContact matchingAggregate;

    // We need to search to find an appropriate aggregate
    QString firstName;
    QString lastName;
    QString nickname;
    QVariantList phoneNumbers;
    QVariantList emailAddresses;
    QVariantList accountUris;
    QString syncTarget;
    QString excludeGender;

    foreach (const QContactName &detail, contact->details<QContactName>()) {
        firstName = detail.firstName().toLower();
        lastName = detail.lastName().toLower();
        break;
    }
    foreach (const QContactNickname &detail, contact->details<QContactNickname>()) {
        nickname = detail.nickname().toLower();
        break;
    }
    foreach (const QContactPhoneNumber &detail, contact->details<QContactPhoneNumber>()) {
        phoneNumbers.append(ContactsEngine::normalizedPhoneNumber(detail.number()));
    }
    foreach (const QContactEmailAddress &detail, contact->details<QContactEmailAddress>()) {
        emailAddresses.append(detail.emailAddress().toLower());
    }
    foreach (const QContactOnlineAccount &detail, contact->details<QContactOnlineAccount>()) {
        accountUris.append(detail.accountUri().toLower());
    }
    syncTarget = contact->detail<QContactSyncTarget>().syncTarget();

    const QContactGender gender(contact->detail<QContactGender>());
    if (gender.gender() == QContactGender::GenderMale) {
        excludeGender = QString::number(static_cast<int>(QContactGender::GenderFemale));
    } else if (gender.gender() == QContactGender::GenderFemale) {
        excludeGender = QString::number(static_cast<int>(QContactGender::GenderMale));
    } else {
        excludeGender = QStringLiteral("none");
    }

    /*
    Aggregation heuristic.

    Search existing aggregate contacts, for matchability.
    The aggregate with the highest match score (over the threshold)
    represents the same "actual person".
    The newly saved contact then becomes a constituent of that
    aggregate.

    Note that individual contacts from the same sync collection can
    represent the same actual person (eg, Telepathy might provide
    buddies from different Jabber servers/rosters and thus if
    you have the same buddy on multiple services, they need to
    be aggregated together.

    Stages:
    1) select all possible aggregate ids
    2) join those ids on the tables of interest to get the data we match against
    3) perform the heuristic matching, ordered by "best score"
    4) select highest score; if over threshold, select that as aggregate.
    */
    static const char *possibleAggregatesWhere = /* SELECT contactId FROM Contacts ... */
        " WHERE Contacts.collectionId = 1" // AggregateAddressbookCollectionId
        " AND (COALESCE(Contacts.lowerLastName, '') = '' OR COALESCE(:lastName, '') = '' OR Contacts.lowerLastName = :lastName)"
        " AND COALESCE(Contacts.gender, '') != :excludeGender"
        " AND contactId > 2" // exclude self contact
        " AND isDeactivated = 0" // exclude deactivated
        " AND contactId NOT IN ("
            " SELECT secondId FROM Relationships WHERE firstId = :contactId AND type = 'IsNot'"
            " UNION"
            " SELECT firstId FROM Relationships WHERE secondId = :contactId AND type = 'IsNot'"
        " )";

    // Use a simple match algorithm, looking for exact matches on name fields,
    // or accumulating points for name matches (including partial matches of first name).

    // step one: build the temporary table which contains all "possible" aggregate contact ids.
    m_database.clearTemporaryContactIdsTable(possibleAggregatesTable);

    QString orderBy = QLatin1String("contactId ASC ");
    QString where = QLatin1String(possibleAggregatesWhere);
    QMap<QString, QVariant> bindings;
    bindings.insert(":lastName", lastName);
    bindings.insert(":contactId", ContactId::databaseId(*contact));
    bindings.insert(":excludeGender", excludeGender);
    if (!m_database.createTemporaryContactIdsTable(possibleAggregatesTable,
                                                          QString(), where, orderBy, bindings)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Error creating possibleAggregates temporary table"));
        return QContactManager::UnspecifiedError;
    }

    // step two: query matching data.
    const QString heuristicallyMatchData(QStringLiteral(
        " SELECT Matches.contactId, sum(Matches.score) AS total FROM ("
            " SELECT Contacts.contactId, 20 AS score FROM Contacts"
            " INNER JOIN temp.possibleAggregates ON Contacts.contactId = temp.possibleAggregates.contactId"
                " WHERE lowerLastName != '' AND lowerLastName = :lastName"
                " AND lowerFirstName != '' AND lowerFirstName = :firstName"
            " UNION"
            " SELECT Contacts.contactId, 15 AS score FROM Contacts"
            " INNER JOIN temp.possibleAggregates ON Contacts.contactId = temp.possibleAggregates.contactId"
                " WHERE COALESCE(lowerFirstName, '') = '' AND COALESCE(:firstName,'') = ''"
                " AND COALESCE(lowerLastName, '') = '' AND COALESCE(:lastName,'') = ''"
                " AND EXISTS ("
                    " SELECT * FROM Nicknames"
                    " WHERE Nicknames.contactId = Contacts.contactId"
                    " AND lowerNickName = :nickname)"
            " UNION"
            " SELECT Contacts.contactId, 12 AS score FROM Contacts"
            " INNER JOIN temp.possibleAggregates ON Contacts.contactId = temp.possibleAggregates.contactId"
                " WHERE (COALESCE(lowerLastName, '') = '' OR COALESCE(:lastName, '') = '')"
                " AND lowerFirstName != '' AND lowerFirstName = :firstName"
            " UNION"
            " SELECT Contacts.contactId, 12 AS score FROM Contacts"
            " INNER JOIN temp.possibleAggregates ON Contacts.contactId = temp.possibleAggregates.contactId"
                " WHERE lowerLastName != '' AND lowerLastName = :lastName"
                " AND (COALESCE(lowerFirstName, '') = '' OR COALESCE(:firstName, '') = '')"
            " UNION"
            " SELECT EmailAddresses.contactId, 3 AS score FROM EmailAddresses"
            " INNER JOIN temp.possibleAggregates ON EmailAddresses.contactId = temp.possibleAggregates.contactId"
            " INNER JOIN temp.matchEmailAddresses ON EmailAddresses.lowerEmailAddress = temp.matchEmailAddresses.value"
            " UNION"
            " SELECT PhoneNumbers.contactId, 3 AS score FROM PhoneNumbers"
            " INNER JOIN temp.possibleAggregates ON PhoneNumbers.contactId = temp.possibleAggregates.contactId"
            " INNER JOIN temp.matchPhoneNumbers ON PhoneNumbers.normalizedNumber = temp.matchPhoneNumbers.value"
            " UNION"
            " SELECT OnlineAccounts.contactId, 3 AS score FROM OnlineAccounts"
            " INNER JOIN temp.possibleAggregates ON OnlineAccounts.contactId = temp.possibleAggregates.contactId"
            " INNER JOIN temp.matchOnlineAccounts ON OnlineAccounts.lowerAccountUri = temp.matchOnlineAccounts.value"
            " UNION"
            " SELECT Nicknames.contactId, 1 AS score FROM Nicknames"
            " INNER JOIN temp.possibleAggregates ON Nicknames.contactId = temp.possibleAggregates.contactId"
                " WHERE lowerNickName != '' AND lowerNickName = :nickname"
        " ) AS Matches"
        " GROUP BY Matches.contactId"
        " ORDER BY total DESC"
        " LIMIT 1"
    ));

    m_database.clearTemporaryValuesTable(matchEmailAddressesTable);
    m_database.clearTemporaryValuesTable(matchPhoneNumbersTable);
    m_database.clearTemporaryValuesTable(matchOnlineAccountsTable);

    if (!m_database.createTemporaryValuesTable(matchEmailAddressesTable, emailAddresses) ||
        !m_database.createTemporaryValuesTable(matchPhoneNumbersTable, phoneNumbers) ||
        !m_database.createTemporaryValuesTable(matchOnlineAccountsTable, accountUris)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Error creating possibleAggregates match tables"));
        return QContactManager::UnspecifiedError;
    }

    ContactsDatabase::Query query(m_database.prepare(heuristicallyMatchData));

    query.bindValue(":firstName", firstName);
    query.bindValue(":lastName", lastName);
    query.bindValue(":nickname", nickname);

    if (!ContactsDatabase::execute(query)) {
        query.reportError("Error finding match for updated local contact");
        return QContactManager::UnspecifiedError;
    }
    if (query.next()) {
        const quint32 aggregateId = query.value<quint32>(0);
        const quint32 score = query.value<quint32>(1);

        static const quint32 MinimumMatchScore = 15;
        if (score >= MinimumMatchScore) {
            existingAggregateId = aggregateId;
        }
    }

    if (existingAggregateId) {
        QList<quint32> readIds;
        readIds.append(existingAggregateId);

        QContactFetchHint hint;
        hint.setOptimizationHints(QContactFetchHint::NoRelationships);

        QList<QContact> readList;
        QContactManager::Error readError = m_reader->readContacts(QLatin1String("CreateAggregate"), &readList, readIds, hint);
        if (readError != QContactManager::NoError || readList.size() < 1) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to read aggregate contact %1 during regenerate").arg(existingAggregateId));
            return QContactManager::UnspecifiedError;
        }

        matchingAggregate = readList.at(0);
    } else {
        // need to create an aggregating contact first.
        matchingAggregate.setCollectionId(ContactCollectionId::apiId(ContactsDatabase::AggregateAddressbookCollectionId, m_managerUri));
    }

    // whether it's an existing or new contact, we promote details.
    // TODO: promote non-Aggregates relationships!
    promoteDetailsToAggregate(*contact, &matchingAggregate, definitionMask, false);

    // now save in database.
    QMap<int, QContactManager::Error> errorMap;
    QList<QContact> saveContactList;
    saveContactList.append(matchingAggregate);
    QContactManager::Error err = save(&saveContactList, DetailList(), 0, &errorMap, withinTransaction, true, false); // we're updating (or creating) the aggregate
    if (err != QContactManager::NoError) {
        if (!existingAggregateId) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Could not create new aggregate contact"));
        } else {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Could not update existing aggregate contact"));
        }
        return err;
    }
    matchingAggregate = saveContactList.at(0);

    {
        // add the relationship and save in the database.
        // Note: we DON'T use the existing save(relationshipList, ...) function
        // as it does (expensive) aggregate regeneration which we have already
        // done above (via the detail promotion and aggregate save).
        // Instead, we simply add the "aggregates" relationship directly.
        const QString insertRelationship(QStringLiteral(
            " INSERT INTO Relationships (firstId, secondId, type)"
            " VALUES (:firstId, :secondId, :type)"
        ));

        ContactsDatabase::Query query(m_database.prepare(insertRelationship));
        query.bindValue(":firstId", ContactId::databaseId(matchingAggregate));
        query.bindValue(":secondId", ContactId::databaseId(*contact));
        query.bindValue(":type", relationshipString(QContactRelationship::Aggregates));
        if (!ContactsDatabase::execute(query)) {
            query.reportError("Error inserting Aggregates relationship");
            err = QContactManager::UnspecifiedError;
        }
    }

    if (err == QContactManager::NoError) {
        if (aggregateContactId) {
            *aggregateContactId = ContactId::databaseId(matchingAggregate);
        }
    } else {
        // if the aggregation relationship fails, the entire save has failed.
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to save aggregation relationship!"));

        if (!existingAggregateId) {
            // clean up the newly created contact.
            QList<QContactId> removeList;
            removeList.append(ContactId::apiId(matchingAggregate));
            QContactManager::Error cleanupErr = remove(removeList, &errorMap, withinTransaction);
            if (cleanupErr != QContactManager::NoError) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to cleanup newly created aggregate contact!"));
            }
        }
    }

    return err;
}

/*
    This function is called as part of the "remove contacts" codepath.
    Any aggregate contacts which still exist after the remove operation
    which used to aggregate a contact which was removed during the operation
    needs to be regenerated (as some details may no longer be valid).

    If the operation fails, it's not a huge issue - we don't need to rollback
    the database.  It simply means that the existing aggregates may contain
    some stale data.
*/
QContactManager::Error ContactWriter::regenerateAggregates(const QList<quint32> &aggregateIds, const DetailList &definitionMask, bool withinTransaction)
{
    static const DetailList identityDetailTypes(getIdentityDetailTypes());

    // for each aggregate contact:
    // 1) get the contacts it aggregates
    // 2) build unique details via composition (name / timestamp / gender / favorite - NOT synctarget or guid)
    // 3) append non-unique details
    // In all cases, we "prefer" the 'local' contact's data (if it exists)

    QList<QContact> aggregatesToSave;
    QSet<QContactId> aggregatesToSaveIds;
    QVariantList aggregatesToRemove;

    foreach (quint32 aggId, aggregateIds) {
        const QContactId apiId(ContactId::apiId(aggId, m_managerUri));
        if (aggregatesToSaveIds.contains(apiId)) {
            continue;
        }

        QList<quint32> readIds;
        readIds.append(aggId);

        {
            const QString findConstituentsForAggregate(QStringLiteral(
                " SELECT secondId FROM Relationships"
                " WHERE firstId = :aggregateId AND type = 'Aggregates'"
            ));

            ContactsDatabase::Query query(m_database.prepare(findConstituentsForAggregate));
            query.bindValue(":aggregateId", aggId);
            if (!ContactsDatabase::execute(query)) {
                query.reportError(QStringLiteral("Failed to find constituent contacts for aggregate %1 during regenerate").arg(aggId));
                return QContactManager::UnspecifiedError;
            }
            while (query.next()) {
                readIds.append(query.value<quint32>(0));
            }
        }

        if (readIds.size() == 1) { // only the aggregate?
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Existing aggregate %1 should already have been removed - aborting regenerate").arg(aggId));
            return QContactManager::UnspecifiedError;
        }

        QContactFetchHint hint;
        hint.setOptimizationHints(QContactFetchHint::NoRelationships);

        QList<QContact> readList;
        QContactManager::Error readError = m_reader->readContacts(QLatin1String("RegenerateAggregate"), &readList, readIds, hint);
        if (readError != QContactManager::NoError
                || readList.size() <= 1
                || ContactCollectionId::databaseId(readList.at(0).collectionId()) != ContactsDatabase::AggregateAddressbookCollectionId) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to read constituent contacts for aggregate %1 during regenerate").arg(aggId));
            return QContactManager::UnspecifiedError;
        }

        // See if there are any constituents to aggregate
        bool activeConstituent = false;
        for (int i = 1; i < readList.size(); ++i) { // start from 1 to skip aggregate
            const QContact &curr(readList.at(i));
            if (curr.details<QContactDeactivated>().count() == 0) {
                activeConstituent = true;
                break;
            }
        }
        if (!activeConstituent) {
            // No active constituents - we need to remove this aggregate
            aggregatesToRemove.append(QVariant(aggId));
            continue;
        }

        QContact originalAggregateContact = readList.at(0);

        QContact aggregateContact;
        aggregateContact.setId(originalAggregateContact.id());
        aggregateContact.setCollectionId(originalAggregateContact.collectionId());

        // Copy any existing fields not affected by this update
        foreach (const QContactDetail &detail, originalAggregateContact.details()) {
            if (detailListContains(identityDetailTypes, detail) ||
                !promoteDetailType(detail.type(), definitionMask, false)) {
                // Copy this detail to the new aggregate
                QContactDetail newDetail(detail);
                if (!aggregateContact.saveDetail(&newDetail, QContact::IgnoreAccessConstraints)) {
                    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Contact: %1 Failed to copy existing detail:")
                            .arg(ContactId::toString(aggregateContact)) << detail);
                }
            }
        }

        // Step two: search for the "local" contact and promote its details first
        for (int i = 1; i < readList.size(); ++i) { // start from 1 to skip aggregate
            QContact curr = readList.at(i);
            if (curr.details<QContactDeactivated>().count())
                continue;
            if (ContactCollectionId::databaseId(curr.collectionId()) != ContactsDatabase::LocalAddressbookCollectionId)
                continue;
            QList<QContactDetail> currDetails = curr.details();
            for (int j = 0; j < currDetails.size(); ++j) {
                QContactDetail currDet = currDetails.at(j);
                if (promoteDetailType(currDet.type(), definitionMask, false)) {
                    // promote this detail to the aggregate.
                    aggregateContact.saveDetail(&currDet, QContact::IgnoreAccessConstraints);
                }
            }
            break; // we've successfully promoted the local contact's details to the aggregate.
        }

        // Step Three: promote data from details of other related contacts
        for (int i = 1; i < readList.size(); ++i) { // start from 1 to skip aggregate
            QContact curr = readList.at(i);
            if (curr.details<QContactDeactivated>().count())
                continue;
            if (ContactCollectionId::databaseId(curr.collectionId()) == ContactsDatabase::LocalAddressbookCollectionId) {
                continue; // already promoted the "local" contact's details.
            }

            // need to promote this contact's details to the aggregate
            promoteDetailsToAggregate(curr, &aggregateContact, definitionMask, false);
        }

        // we save the updated aggregates to database all in a batch at the end.
        aggregatesToSave.append(aggregateContact);
        aggregatesToSaveIds.insert(ContactId::apiId(aggregateContact));
    }

    if (!aggregatesToSave.isEmpty()) {
        QMap<int, QContactManager::Error> errorMap;
        QContactManager::Error writeError = save(&aggregatesToSave, definitionMask, 0, &errorMap, withinTransaction, true, false); // we're updating aggregates.
        if (writeError != QContactManager::NoError) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to write updated aggregate contacts during regenerate.  definitionMask:") << definitionMask);
            return writeError;
        }
    }
    if (!aggregatesToRemove.isEmpty()) {
        QContactManager::Error removeError = removeContacts(aggregatesToRemove);
        if (removeError != QContactManager::NoError) {
            return removeError;
        }
    }

    return QContactManager::NoError;
}

QContactManager::Error ContactWriter::removeChildlessAggregates(QList<QContactId> *removedIds)
{
    QVariantList aggregateIds;

    const QString childlessAggregateIds(QStringLiteral(
        " SELECT contactId FROM Contacts"
            " WHERE collectionId = 1" // AggregateAddressbookCollectionId
            " AND contactId NOT IN ("
                " SELECT DISTINCT firstId FROM Relationships WHERE type = 'Aggregates'"
            " )"
    ));

    ContactsDatabase::Query query(m_database.prepare(childlessAggregateIds));
    if (!ContactsDatabase::execute(query)) {
        query.reportError("Failed to fetch childless aggregate contact ids during remove");
        return QContactManager::UnspecifiedError;
    }
    while (query.next()) {
        quint32 aggregateId = query.value<quint32>(0);
        aggregateIds.append(aggregateId);
        removedIds->append(ContactId::apiId(aggregateId, m_managerUri));
    }

    if (aggregateIds.size() > 0) {
        QContactManager::Error removeError = removeContacts(aggregateIds);
        if (removeError != QContactManager::NoError) {
            return removeError;
        }
    }

    return QContactManager::NoError;
}

QContactManager::Error ContactWriter::aggregateOrphanedContacts(bool withinTransaction)
{
    QList<quint32> contactIds;

    {
        const QString orphanContactIds(QStringLiteral(
            " SELECT contactId FROM Contacts"
                " WHERE collectionId != 1" // AggregateAddressbookCollectionId
                " AND isDeactivated = 0 AND contactId NOT IN ("
                    " SELECT DISTINCT secondId FROM Relationships WHERE type = 'Aggregates'"
                " )"
        ));

        ContactsDatabase::Query query(m_database.prepare(orphanContactIds));
        if (!ContactsDatabase::execute(query)) {
            query.reportError("Failed to fetch orphan aggregate contact ids during remove");
            return QContactManager::UnspecifiedError;
        }
        while (query.next()) {
            contactIds.append(query.value<quint32>(0));
        }
    }

    if (contactIds.size() > 0) {
        QContactFetchHint hint;
        hint.setOptimizationHints(QContactFetchHint::NoRelationships);

        QList<QContact> readList;
        QContactManager::Error readError = m_reader->readContacts(QLatin1String("AggregateOrphaned"), &readList, contactIds, hint);
        if (readError != QContactManager::NoError || readList.size() != contactIds.size()) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to read orphaned contacts for aggregation"));
            return QContactManager::UnspecifiedError;
        }

        QList<QContact>::iterator it = readList.begin(), end = readList.end();
        for ( ; it != end; ++it) {
            QContact &orphan(*it);
            QContactManager::Error error = updateOrCreateAggregate(&orphan, DetailList(), withinTransaction);
            if (error != QContactManager::NoError) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to create aggregate for orphaned contact: %1").arg(ContactId::toString(orphan)));
                return error;
            }
        }
    }

    return QContactManager::NoError;
}

static QDateTime epochDateTime()
{
    QDateTime rv;
    rv.setMSecsSinceEpoch(0);
    return rv;
}

struct ConstituentDetails {
    quint32 id;
    quint32 collectionId;
};

QContactManager::Error ContactWriter::syncFetch(const QContactCollectionId &collectionId, const QDateTime &lastSync, const QSet<quint32> &exportedIds,
                                                QList<QContact> *syncContacts, QList<QContact> *addedContacts, QList<QContactId> *deletedContactIds,
                                                QDateTime *maxTimestamp)
{
    const QDateTime sinceTime((lastSync.isValid() ? lastSync : epochDateTime()).toUTC());
    *maxTimestamp = sinceTime; // fall back to current sync timestamp if no data found.

    const QString since(ContactsDatabase::dateTimeString(sinceTime));

    const bool exportUpdate(collectionId == ContactCollectionId::apiId(ContactsDatabase::LocalAddressbookCollectionId, m_managerUri));

    if (syncContacts || addedContacts) {
        // We need the transient timestamps to be available
        if (!m_database.populateTemporaryTransientState(true, false)) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Error populating transient timestamp table"));
            return QContactManager::UnspecifiedError;
        }

        if (exportUpdate) {
            // This is a fetch for the export adaptor - it's a subset of the usual mechanism, since
            // no contact is treated as originating in the exported database.  Instead, changes that
            // occur there are imported back and owned by the primary database.
            QList<quint32> exportIds;

            {
                // Find all aggregates of any kind modified since the last sync
                const QString exportContactIds(QStringLiteral(
                    " SELECT Contacts.contactId FROM Contacts"
                    " LEFT JOIN temp.Timestamps on temp.Timestamps.contactId = Contacts.contactId"
                    " WHERE collectionId = 1" // AggregateAddressbookCollectionId
                    " AND COALESCE(temp.Timestamps.modified, Contacts.modified) > :lastSync"
                ));

                ContactsDatabase::Query query(m_database.prepare(exportContactIds));
                query.bindValue(":lastSync", since);
                if (!ContactsDatabase::execute(query)) {
                    query.reportError("Failed to fetch export contact ids");
                    return QContactManager::UnspecifiedError;
                }
                while (query.next()) {
                    exportIds.append(query.value<quint32>(0));
                }
            }

            // We will return the existing aggregate for these contacts
            QContactFetchHint hint;
            hint.setOptimizationHints(QContactFetchHint::NoRelationships);

            QList<QContact> readList;
            QContactManager::Error readError = m_reader->readContacts(QLatin1String("syncFetch"), &readList, exportIds, hint, true);
            if (readError != QContactManager::NoError || readList.size() != exportIds.size()) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to read contacts for export sync"));
                return QContactManager::UnspecifiedError;
            }

            foreach (QContact aggregate, readList) {
                // Remove any non-exportable details from the contact
                foreach (const QContactDetail detail, aggregate.details()) {
                    if (detail.value<bool>(QContactDetail__FieldNonexportable)) {
                        QContactDetail copy(detail);
                        aggregate.removeDetail(&copy);
                    }
                }

                if (exportedIds.contains(ContactId::databaseId(aggregate.id()))) {
                    syncContacts->append(aggregate);
                } else {
                    addedContacts->append(aggregate);
                }
            }
        } else {
            QSet<quint32> aggregateIds;
            QSet<quint32> addedAggregateIds;

            if (syncContacts) {
                // XXXXXXXXXXXXXXXXXXX TODO: I don't think we want to do this, even.
                //                     instead, just read the timestamps of the contacts in the specified collection...
                // Find all aggregates of contacts from this sync service modified since the last sync
                {
                    const QString syncContactIds(QStringLiteral(
                        " SELECT DISTINCT Relationships.firstId"
                        " FROM Relationships"
                        " JOIN Contacts AS Aggregates ON Aggregates.contactId = Relationships.firstId"
                        " LEFT JOIN temp.Timestamps on temp.Timestamps.contactId = Aggregates.contactId"
                        " JOIN Contacts AS Constituents ON Constituents.contactId = Relationships.secondId"
                        " WHERE Relationships.type = 'Aggregates'"
                        " AND Constituents.collectionId = :collectionId"
                        " AND COALESCE(temp.Timestamps.modified, Aggregates.modified) > :lastSync"
                    ));

                    ContactsDatabase::Query query(m_database.prepare(syncContactIds));
                    query.bindValue(":collectionId", ContactCollectionId::databaseId(collectionId));
                    query.bindValue(":lastSync", since);
                    if (!ContactsDatabase::execute(query)) {
                        query.reportError("Failed to fetch sync contact ids");
                        return QContactManager::UnspecifiedError;
                    }
                    while (query.next()) {
                        aggregateIds.insert(query.value<quint32>(0));
                    }
                }

                if (!exportedIds.isEmpty()) {
                    // Add the previously-exported contact IDs
                    QVariantList ids;
                    foreach (quint32 id, exportedIds) {
                        ids.append(id);
                    }

                    m_database.clearTemporaryContactIdsTable(syncConstituentsTable);

                    if (!m_database.createTemporaryContactIdsTable(syncConstituentsTable, ids)) {
                        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Error populating syncConstituents temporary table"));
                        return QContactManager::UnspecifiedError;
                    }

                    const QString aggregateContactIds(QStringLiteral(
                        " SELECT Relationships.firstId"
                        " FROM Relationships"
                        " JOIN Contacts ON Contacts.contactId = Relationships.firstId"
                        " LEFT JOIN temp.Timestamps on temp.Timestamps.contactId = Contacts.contactId"
                        " WHERE Relationships.type = 'Aggregates' AND secondId IN ("
                        "  SELECT contactId FROM temp.syncConstituents)"
                        " AND COALESCE(temp.Timestamps.modified, Contacts.modified) > :lastSync"
                    ));

                    ContactsDatabase::Query query(m_database.prepare(aggregateContactIds));
                    query.bindValue(":lastSync", since);
                    if (!ContactsDatabase::execute(query)) {
                        query.reportError("Failed to fetch aggregate contact ids for sync");
                        return QContactManager::UnspecifiedError;
                    }
                    while (query.next()) {
                        aggregateIds.insert(query.value<quint32>(0));
                    }
                }
            }

            // XXXXXXXXXXXXXX TODO: we shouldn't report local changes to sync fetch...
            if (addedContacts) {
                // Report added contacts as well
                const QString addedSyncContactIds(QStringLiteral(
                    " SELECT Relationships.firstId"
                    " FROM Contacts"
                    " JOIN Relationships ON Relationships.secondId = Contacts.contactId"
                    " WHERE Contacts.created > :lastSync"
                    " AND Contacts.collectionId = 2" // LocalAddressbookCollectionId
                    " AND Relationships.type = 'Aggregates'"
                ));

                ContactsDatabase::Query query(m_database.prepare(addedSyncContactIds));
                query.bindValue(":lastSync", since);
                if (!ContactsDatabase::execute(query)) {
                    query.reportError("Failed to fetch sync contact ids");
                    return QContactManager::UnspecifiedError;
                }
                while (query.next()) {
                    const quint32 aggregateId(query.value<quint32>(0));

                    // Fetch the aggregates for the added contacts, unless they're constituents of aggregates we're returning anyway
                    if (!aggregateIds.contains(aggregateId)) {
                        aggregateIds.insert(aggregateId);
                        addedAggregateIds.insert(aggregateId);
                    }
                }
            }

            if (aggregateIds.count() > 0) {
                // Return 'partial aggregates' for each of these contacts - any sync adaptor should see
                // the parts of the aggregate that are sourced from the local device or from their own
                // remote data.  Data from other sync adaptors should be excluded.

                // First, find the constituent details for the aggregates
                QMap<quint32, QList<ConstituentDetails> > constituentDetails;
                QMap<quint32, quint32> localIds;

                QVariantList ids;
                foreach (quint32 id, aggregateIds) {
                    ids.append(id);
                }

                m_database.clearTemporaryContactIdsTable(syncAggregatesTable);

                if (!m_database.createTemporaryContactIdsTable(syncAggregatesTable, ids)) {
                    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Error populating syncAggregates temporary table"));
                    return QContactManager::UnspecifiedError;
                }

                {
                    const QString constituentContactDetails(QStringLiteral(
                        " SELECT Relationships.firstId, Contacts.contactId, Contacts.collectionId"
                        " FROM Relationships"
                        " JOIN Contacts ON Contacts.contactId = Relationships.secondId"
                        " WHERE Relationships.type = 'Aggregates'"
                        " AND Relationships.firstId IN ("
                            " SELECT contactId FROM temp.syncAggregates)"
                        " AND Contacts.isDeactivated = 0"
                    ));

                    ContactsDatabase::Query query(m_database.prepare(constituentContactDetails));
                    if (!ContactsDatabase::execute(query)) {
                        query.reportError("Failed to fetch constituent contact details");
                        return QContactManager::UnspecifiedError;
                    }
                    while (query.next()) {
                        int col = 0;
                        const quint32 aggId(query.value<quint32>(col++));
                        const quint32 constituentId(query.value<quint32>(col++));
                        const quint32 constituentCollectionId(query.value<quint32>(col++));

                        ConstituentDetails details = { constituentId, constituentCollectionId };
                        constituentDetails[aggId].append(details);
                        if (constituentCollectionId == ContactsDatabase::LocalAddressbookCollectionId) {
                            localIds[aggId] = constituentId;
                        }
                    }
                }

                QMap<quint32, quint32> partialBaseAggregateIds;
                QSet<quint32> requiredConstituentIds;

                // Find the base constituents for every contact the remote should see, and the constituents
                // that should be combined into the partial aggregate
                QMap<quint32, QList<ConstituentDetails> >::const_iterator it = constituentDetails.constBegin(), end = constituentDetails.constEnd();
                for ( ; it != end; ++it) {
                    quint32 collectionConstituentId = 0;
                    QList<quint32> inclusions;

                    const quint32 aggId(it.key());
                    const QList<ConstituentDetails> &constituents(it.value());

                    QList<ConstituentDetails>::const_iterator cit = constituents.constBegin(), cend = constituents.constEnd();
                    for ( ; cit != cend; ++cit) {
                        const quint32 cid((*cit).id);
                        const quint32 colId((*cit).collectionId);

                        if (colId == ContactCollectionId::databaseId(collectionId) || colId == ContactsDatabase::LocalAddressbookCollectionId) {
                            inclusions.append(cid);
                            if (colId == ContactCollectionId::databaseId(collectionId)) {
                                if (collectionConstituentId != 0) {
                                    // We need to generate partial aggregates for each collection-specific constituent
                                    partialBaseAggregateIds.insert(cid, aggId);
                                } else {
                                    collectionConstituentId = cid;
                                }
                            }
                        }
                    }

                    quint32 baseId = 0;
                    if (collectionConstituentId == 0) {
                        // This aggregate has no constituent in the specific collection - base it on the local
                        baseId = localIds[aggId];
                    } else {
                        baseId = collectionConstituentId;
                    }

                    partialBaseAggregateIds.insert(baseId, aggId);
                    foreach (quint32 id, inclusions) {
                        requiredConstituentIds.insert(id);
                    }
                }

                // Fetch all the contacts we need - either aggregates to return, or constituents to build partial aggregates from
                QList<quint32> readIds(requiredConstituentIds.toList());
                if (!readIds.isEmpty()) {
                    QContactFetchHint hint;
                    hint.setOptimizationHints(QContactFetchHint::NoRelationships);

                    QList<QContact> readList;
                    QContactManager::Error readError = m_reader->readContacts(QLatin1String("syncFetch"), &readList, readIds, hint, true);
                    if (readError != QContactManager::NoError || readList.size() != readIds.size()) {
                        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to read contacts for sync"));
                        return QContactManager::UnspecifiedError;
                    }

                    QMap<quint32, const QContact *> constituentContacts;

                    foreach (const QContact &contact, readList) {
                        const quint32 dbId(ContactId::databaseId(contact.id()));

                        // We need this contact to build the partial aggregates
                        constituentContacts.insert(dbId, &contact);
                    }

                    // Build partial aggregates - keep in sync with related logic in regenerateAggregates
                    QMap<quint32, quint32>::const_iterator pit = partialBaseAggregateIds.constBegin(), pend = partialBaseAggregateIds.constEnd();
                    for ( ; pit != pend; ++pit) {
                        const quint32 baseId(pit.key());
                        const quint32 aggId(pit.value());

                        QContact partialAggregate;
                        partialAggregate.setId(ContactId::apiId(baseId, m_managerUri));

                        // If this aggregate has a local constituent, copy that contact's details first
                        const quint32 localId = localIds[aggId];
                        if (localId) {
                            if (const QContact *localConstituent = constituentContacts[localId]) {
                                foreach (const QContactDetail &detail, localConstituent->details()) {
                                    if (promoteDetailType(detail.type(), DetailList(), false)) {
                                        // promote this detail to the aggregate.
                                        QContactDetail copy(detail);
                                        partialAggregate.saveDetail(&copy, QContact::IgnoreAccessConstraints);
                                    }
                                }
                            } else {
                                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to promote details from missing local: %1").arg(localId));
                                return QContactManager::UnspecifiedError;
                            }
                        }

                        // Promote details from other constituents
                        const QList<ConstituentDetails> &constituents(constituentDetails[aggId]);
                        QList<ConstituentDetails>::const_iterator cit = constituents.constBegin(), cend = constituents.constEnd();
                        for ( ; cit != cend; ++cit) {
                            const quint32 cid((*cit).id);
                            const quint32 colId((*cit).collectionId);

                            if (colId == ContactCollectionId::databaseId(collectionId)) {
                                // Do not include any constituents from this sync service that are not the base itself.
                                // (Note that the base will either be from this service, or a local contact).
                                // That is, contacts from the same sync service won't ever be aggregated together.  CHANGEME?
                                if (cid != baseId) {
                                    continue;
                                }
                                if (const QContact *constituent = constituentContacts[cid]) {
                                    // Force promotion of details from the constituent matching the collectionId
                                    const bool forcePromotion(colId == ContactCollectionId::databaseId(collectionId));
                                    promoteDetailsToAggregate(*constituent, &partialAggregate, DetailList(), forcePromotion);
                                } else {
                                    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to promote details from missing constitutent: %1").arg(cid));
                                    return QContactManager::UnspecifiedError;
                                }
                            }
                        }

                        if (addedAggregateIds.contains(aggId)) {
                            addedContacts->append(partialAggregate);
                        } else {
                            syncContacts->append(partialAggregate);
                        }
                    }
                }
            }
        }
    }

    // determine the max created/modified/deleted timestamp from the data.
    // this timestamp should be used as the syncTimestamp during the next sync.
    updateMaxSyncTimestamp(syncContacts, maxTimestamp);
    updateMaxSyncTimestamp(addedContacts, maxTimestamp);

    if (deletedContactIds) {
        const QString deletedSyncContactIds(QStringLiteral(
            " SELECT contactId, collectionId, deleted"
            " FROM DeletedContacts"
            " WHERE deleted > :lastSync"
        ));

        ContactsDatabase::Query query(m_database.prepare(deletedSyncContactIds));
        query.bindValue(":lastSync", since);
        if (!ContactsDatabase::execute(query)) {
            query.reportError("Failed to fetch sync contact ids");
            return QContactManager::UnspecifiedError;
        }
        while (query.next()) {
            const quint32 dbId(query.value<quint32>(0));
            const quint32 colId(query.value<quint32>(1));
            const QDateTime deleted(query.value<QDateTime>(2));

            // If this contact was from this source, or was exported to this source, report the deletion
            if ((exportUpdate && colId == ContactsDatabase::AggregateAddressbookCollectionId)
                    || (!exportUpdate && (colId == ContactCollectionId::databaseId(collectionId) || exportedIds.contains(dbId)))) {
                deletedContactIds->append(ContactId::apiId(dbId, m_managerUri));
                if (deleted > *maxTimestamp) {
                    *maxTimestamp = deleted;
                }
            }
        }
    }

    return QContactManager::NoError;
}

static void modifyContactDetail(const QContactDetail &original, const QContactDetail &modified,
                                QtContactsSqliteExtensions::ContactManagerEngine::ConflictResolutionPolicy conflictPolicy,
                                QContactDetail *recipient)
{
    // Apply changes field-by-field
    DetailMap originalValues(detailValues(original, false));
    DetailMap modifiedValues(detailValues(modified, false));

    DetailMap::const_iterator mit = modifiedValues.constBegin(), mend = modifiedValues.constEnd();
    for ( ; mit != mend; ++mit) {
        const int field(mit.key());

        const QVariant originalValue(originalValues[field]);
        originalValues.remove(field);

        const QVariant currentValue(recipient->value(field));
        if (!variantEqual(currentValue, originalValue)) {
            // The local value has changed since this data was exported
            if (conflictPolicy == QtContactsSqliteExtensions::ContactManagerEngine::PreserveLocalChanges) {
                // Ignore this remote change
                continue;
            }
        }

        // Update the result value
        recipient->setValue(field, mit.value());
    }

    DetailMap::const_iterator oit = originalValues.constBegin(), oend = originalValues.constEnd();
    for ( ; oit != oend; ++oit) {
        // Any previously existing values that are no longer present should be removed
        const int field(oit.key());
        const QVariant originalValue(oit.value());

        const QVariant currentValue(recipient->value(field));
        if (!variantEqual(currentValue, originalValue)) {
            // The local value has changed since this data was exported
            if (conflictPolicy == QtContactsSqliteExtensions::ContactManagerEngine::PreserveLocalChanges) {
                // Ignore this remote removal
                continue;
            }
        }

        recipient->removeValue(field);
    }

    // set the modifiable flag to true unless the sync adapter has set it explicitly
    if (!recipient->values().contains(QContactDetail__FieldModifiable)) {
        recipient->setValue(QContactDetail__FieldModifiable, true);
    }
}

QContactManager::Error ContactWriter::syncUpdate(const QContactCollectionId &collectionId,
                                                 QtContactsSqliteExtensions::ContactManagerEngine::ConflictResolutionPolicy conflictPolicy,
                                                 QList<QPair<QContact, QContact> > *remoteChanges)
{
    static const ContactWriter::DetailList compositionDetailTypes(getCompositionDetailTypes());

    QMap<quint32, QList<QPair<StringPair, DetailPair> > > contactModifications;
    QMap<quint32, QList<QContactDetail> > contactAdditions;
    QMap<quint32, QList<StringPair> > contactRemovals;

    QSet<quint32> compositionModificationIds;

    QList<QContact *> contactsToAdd;
    QSet<quint32> contactsToRemove;

    const bool exportUpdate(collectionId == ContactCollectionId::apiId(ContactsDatabase::LocalAddressbookCollectionId, m_managerUri));

    // For each pair of contacts, determine the changes to be applied
    QList<QPair<QContact, QContact> >::iterator cit = remoteChanges->begin(), cend = remoteChanges->end();
    for (int index = 0; cit != cend; ++cit, ++index) {
        QPair<QContact, QContact> &pair(*cit);

        const QContact &original(pair.first);
        QContact &updated(pair.second);

        const quint32 contactId(ContactId::databaseId(original.id()));
        const quint32 updatedId(ContactId::databaseId(updated.id()));

        if (original.isEmpty()) {
            if (updatedId != 0) {
                QTCONTACTS_SQLITE_DEBUG(QString::fromLatin1("Invalid ID for new contact: %1").arg(updatedId));
            }
            contactsToAdd.append(&updated);
            continue;
        } else if (updated.isEmpty()) {
            if (contactId == 0) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Invalid ID for contact deletion: %1").arg(contactId));
                return QContactManager::UnspecifiedError;
            }
            contactsToRemove.insert(contactId);
            continue;
        } else {
            if (contactId == 0) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Invalid ID for contact update: %1").arg(contactId));
                return QContactManager::UnspecifiedError;
            }
            if (updatedId != contactId) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Mismatch on sync contact ids:\n%1 != %2")
                        .arg(updatedId).arg(contactId));
                return QContactManager::UnspecifiedError;
            }
        }

        // Extract the details from these contacts
        QList<QPair<QContactDetail, StringPair> > originalDetails(promotableDetails(original, true));
        QList<QPair<QContactDetail, StringPair> > updatedDetails(promotableDetails(updated, true));

        // Remove any details that are equivalent in both contacts
        removeEquivalentDetails(originalDetails, updatedDetails);

        if (originalDetails.isEmpty() && updatedDetails.isEmpty()) {
            continue;
        }

        // See if any of these differences are in-place modifications
        QList<QPair<QContactDetail, StringPair> >::const_iterator uit = updatedDetails.constBegin(), uend = updatedDetails.constEnd();
        for ( ; uit != uend; ++uit) {
            const QContactDetail &detail((*uit).first);
            const StringPair &identity((*uit).second);

            if (identity.first.isEmpty()) {
                const QContactDetail::DetailType type(detailType(detail));
                if (compositionDetailTypes.contains(type)) {
                    // This is a modification of a contacts table detail - we will eventually work out the new composition
                    contactModifications[contactId].append(qMakePair(identity, qMakePair(contactDetailOfType(original, type), detail)));
                    compositionModificationIds.insert(contactId);
                } else {
                    // This is a new detail altogether
                    contactAdditions[contactId].append(detail);
                }
            } else {
                QList<QPair<QContactDetail, StringPair> >::iterator oit = originalDetails.begin(), oend = originalDetails.end();
                for ( ; oit != oend; ++oit) {
                    if ((*oit).second == identity) {
                        // This detail is present in the original contact; the new version is a modification

                        // Find the contact where this detail originates
                        const QStringList provenance(identity.first.split(QChar::fromLatin1(':')));
                        const quint32 originId(provenance.at(0).toUInt());

                        contactModifications[originId].append(qMakePair(identity, qMakePair((*oit).first, detail)));
                        originalDetails.erase(oit);
                        break;
                    }
                }
                if (oit == oend) {
                    // The original detail for this modification cannot be found
                    if (conflictPolicy == QtContactsSqliteExtensions::ContactManagerEngine::PreserveRemoteChanges) {
                        // Handle the updated value as an addition
                        contactAdditions[contactId].append(detail);
                    }
                }
            }
        }

        // Any remaining (non-composition) original details must be removed
        QList<QPair<QContactDetail, StringPair> >::const_iterator oit = originalDetails.constBegin(), oend = originalDetails.constEnd();
        for ( ; oit != oend; ++oit) {
            const StringPair &identity((*oit).second);
            if (!identity.first.isEmpty()) {
                contactRemovals[contactId].append(identity);
            }
        }
    }

    if (exportUpdate) {
        const QList<quint32> aggregateIds(contactAdditions.keys() + compositionModificationIds.toList());
        if (!aggregateIds.isEmpty()) {
            QVariantList ids;
            foreach (quint32 id, aggregateIds.toSet()) {
                ids.append(id);
            }

            // XXXXXXXXXXXXXXXXXXXXX this codepath should never be hit, right?
            // If these changes are for aggregate IDs - we need to find the local constituents to modify instead
            m_database.clearTemporaryContactIdsTable(syncAggregatesTable);

            if (!m_database.createTemporaryContactIdsTable(syncAggregatesTable, ids)) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Error populating syncAggregates temporary table"));
                return QContactManager::UnspecifiedError;
            }

            QMap<quint32, quint32> localConstituentIds;

            const QString localConstituentForAggregate(QStringLiteral(
                " SELECT Contacts.contactId, Relationships.firstId"
                " FROM Contacts"
                " JOIN Relationships ON Relationships.secondId = Contacts.contactId"
                " WHERE Contacts.collectionId = 2" // LocalAddressbookCollectionId
                " AND Relationships.firstId IN ("
                "  SELECT contactId FROM temp.syncAggregates)"
                " AND Relationships.type = 'Aggregates'"
            ));

            ContactsDatabase::Query query(m_database.prepare(localConstituentForAggregate));
            if (!ContactsDatabase::execute(query)) {
                query.reportError("Failed to fetch constituent contact details");
                return QContactManager::UnspecifiedError;
            }
            while (query.next()) {
                const quint32 localId(query.value<quint32>(0));
                const quint32 aggId(query.value<quint32>(1));

                localConstituentIds.insert(aggId, localId);
            }

            // Convert the additions and modifications to affect the local constituents
            QMap<quint32, QList<QContactDetail> > retargetedAdditions;
            QMap<quint32, QList<QPair<StringPair, DetailPair> > > retargetedModifications;

            QMap<quint32, QList<QContactDetail> >::const_iterator ait = contactAdditions.constBegin(), aend = contactAdditions.constEnd();
            for ( ; ait != aend; ++ait) {
                const quint32 aggId = ait.key();
                QMap<quint32, quint32>::iterator localIt = localConstituentIds.find(aggId);
                if (localIt != localConstituentIds.end()) {
                    retargetedAdditions[localIt.value()].append(ait.value());
                } else {
                    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Invalid aggregate contact addition"));
                    return QContactManager::UnspecifiedError;
                }
            }

            QMap<quint32, QList<QPair<StringPair, DetailPair> > >::const_iterator mit = contactModifications.constBegin(), mend = contactModifications.constEnd();
            for ( ; mit != mend; ++mit) {
                // Preserve these changes
                const quint32 contactId = mit.key();
                retargetedModifications[contactId].append(mit.value());
            }

            contactAdditions = retargetedAdditions;
            contactModifications = retargetedModifications;
        }
    } else if (!compositionModificationIds.isEmpty()) {
        // We also need to modify the composited details for the local constituents of these contacts
        QVariantList ids;
        foreach (quint32 id, compositionModificationIds) {
            ids.append(id);
        }

        m_database.clearTemporaryContactIdsTable(syncConstituentsTable);

        if (!m_database.createTemporaryContactIdsTable(syncConstituentsTable, ids)) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Error populating syncConstituents temporary table"));
            return QContactManager::UnspecifiedError;
        }

        const QString syncCollectionConstituentIds(QStringLiteral(
            " SELECT R2.secondId, R1.secondId, R1.firstId"
            " FROM Relationships AS R1"
            " LEFT JOIN Relationships AS R2 ON R2.firstId = R1.firstId"
            " AND R2.type = 'Aggregates'"
            " AND R2.secondId IN ("
                " SELECT contactId FROM CONTACTS WHERE collectionId = :collectionId)"
            " WHERE R1.type = 'Aggregates'"
            " AND R1.secondId IN ("
                " SELECT contactId FROM temp.syncConstituents)"
        ));

        ContactsDatabase::Query query(m_database.prepare(syncCollectionConstituentIds));
        query.bindValue(":collectionId", ContactCollectionId::databaseId(collectionId));
        if (!ContactsDatabase::execute(query)) {
            query.reportError("Failed to fetch local constituent ids for sync update");
            return QContactManager::UnspecifiedError;
        }
        while (query.next()) {
            const quint32 localConstituentId = query.value<quint32>(0);
            const quint32 modifiedConstituentId = query.value<quint32>(1);

            if (localConstituentId && (localConstituentId != modifiedConstituentId)) {
                // Any changes made to the constituent must also be made to the local to be effective
                contactModifications[localConstituentId].append(contactModifications[modifiedConstituentId]);
            }
        }
    }

    QSet<quint32> affectedContactIds((contactModifications.keys() + contactAdditions.keys() + contactRemovals.keys()).toSet());

    QMap<quint32, quint32> syncCollectionConstituents;
    QMap<quint32, quint32> constituentAggregateIds;

    if (exportUpdate) {
        if (!contactsToRemove.isEmpty()) {
            QVariantList ids;
            foreach (quint32 id, contactsToRemove) {
                ids.append(id);
            }

            m_database.clearTemporaryContactIdsTable(aggregationIdsTable);

            if (!m_database.createTemporaryContactIdsTable(aggregationIdsTable, ids)) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Error populating aggregation IDs temporary table"));
                return QContactManager::UnspecifiedError;
            }

            const QString findConstituentsForAggregateIds(QStringLiteral(
                " SELECT Relationships.secondId, Contacts.collectionId"
                " FROM Relationships"
                " JOIN temp.aggregationIds ON Relationships.firstId = temp.aggregationIds.contactId"
                " JOIN Contacts ON Contacts.contactId = Relationships.secondId"
                " WHERE Relationships.type = 'Aggregates'"
            ));

            ContactsDatabase::Query query(m_database.prepare(findConstituentsForAggregateIds));
            if (!ContactsDatabase::execute(query)) {
                query.reportError("Failed to fetch contacts aggregated by removed aggregates");
                return QContactManager::UnspecifiedError;
            }
            while (query.next()) {
                const quint32 constituentId(query.value<quint32>(0));
                const quint32 constituentCollectionId(query.value<quint32>(1));

                if (constituentCollectionId == ContactsDatabase::LocalAddressbookCollectionId) {
                    // We need to remove any local-device data for this aggregate
                    contactsToRemove.insert(constituentId);
                }
            }
        }
    } else {
        // For contacts that are not from our sync collection, we may need to modify the sync collection constituent of the aggregate
        if (!affectedContactIds.isEmpty() || !contactsToRemove.isEmpty()) {
            QVariantList ids;
            foreach (quint32 id, affectedContactIds + contactsToRemove) {
                ids.append(id);
            }

            m_database.clearTemporaryContactIdsTable(syncConstituentsTable);
            if (!m_database.createTemporaryContactIdsTable(syncConstituentsTable, ids)) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Error populating syncConstituents temporary table"));
                return QContactManager::UnspecifiedError;
            }

            const QString syncCollectionConstituentIds(QStringLiteral(
                " SELECT R2.secondId, R1.secondId, R1.firstId"
                " FROM Relationships AS R1"
                " LEFT JOIN Relationships AS R2 ON R2.firstId = R1.firstId"
                " AND R2.type = 'Aggregates'"
                " AND R2.secondId IN ("
                    " SELECT contactId FROM CONTACTS WHERE collectionId = :collectionId)"
                " WHERE R1.type = 'Aggregates'"
                " AND R1.secondId IN ("
                    " SELECT contactId FROM temp.syncConstituents)"
            ));

            ContactsDatabase::Query query(m_database.prepare(syncCollectionConstituentIds));
            query.bindValue(":collectionId", ContactCollectionId::databaseId(collectionId));
            if (!ContactsDatabase::execute(query)) {
                query.reportError("Failed to fetch local constituent ids for sync update");
                return QContactManager::UnspecifiedError;
            }

            QSet<quint32> allSyncCollectionConstituentIds;
            QMultiHash<quint32, quint32> modifiedToSyncCollectionConstituentId;
            QHash<quint32, quint32> aggregateForSyncCollectionConstituentId;
            while (query.next()) {
                const quint32 syncCollectionConstituentId = query.value<quint32>(0);
                const quint32 modifiedConstituentId = query.value<quint32>(1);
                const quint32 aggregateId = query.value<quint32>(2);
                constituentAggregateIds.insert(modifiedConstituentId, aggregateId);
                if (syncCollectionConstituentId) {
                    modifiedToSyncCollectionConstituentId.insertMulti(modifiedConstituentId, syncCollectionConstituentId);
                    allSyncCollectionConstituentIds.insert(syncCollectionConstituentId);
                    aggregateForSyncCollectionConstituentId.insert(syncCollectionConstituentId, aggregateId);
                }
            }

            Q_FOREACH (quint32 modifiedId, modifiedToSyncCollectionConstituentId.keys()) {
                if (allSyncCollectionConstituentIds.contains(modifiedId)) {
                    // this modified constituent is also a sync collection constituent.
                    // we don't need to create a mapping.
                } else {
                    // this modified constituent is NOT a sync collection constituent.
                    // find an appropriate sync target constituent for this modified contact.
                    QList<quint32> syncCollectionConstituentIds = modifiedToSyncCollectionConstituentId.values(modifiedId);
                    quint32 mappedSyncCollectionConstituent = 0;
                    Q_FOREACH (quint32 syncCollectionConstituentId, syncCollectionConstituentIds) {
                        if (modifiedToSyncCollectionConstituentId.contains(syncCollectionConstituentId)) {
                            // this sync collection constituent was also modified.  Fall back to this if no others exist.
                            mappedSyncCollectionConstituent = syncCollectionConstituentId;
                        } else {
                            // unmodified constituent.  Use this if possible, since other modifications
                            // might involve removal (so mapped changes would in that case fail).
                            mappedSyncCollectionConstituent = syncCollectionConstituentId;
                            break;
                        }
                    }
                    // use the found sync collection contact in our mapping.
                    if (!mappedSyncCollectionConstituent) {
                        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("No sync collection constituent found for modified contact:") << modifiedId);
                    } else {
                        syncCollectionConstituents.insert(modifiedId, mappedSyncCollectionConstituent);
                        affectedContactIds.insert(mappedSyncCollectionConstituent);
                        constituentAggregateIds.insert(mappedSyncCollectionConstituent, aggregateForSyncCollectionConstituentId[mappedSyncCollectionConstituent]);
                    }
                }
            }
        }

        // even if we previously affected some constituent, we should remove that
        // constituent if it is contained in the contactsToRemove list.
        QSet<quint32> modifiedContactsToRemove;
        QSet<quint32>::const_iterator rit = contactsToRemove.constBegin(), rend = contactsToRemove.constEnd();
        for ( ; rit != rend; ++rit) {
            const quint32 stId(syncCollectionConstituents.value(*rit));
            if (stId != 0) {
                // Remove the sync target consituent instead of the base constituent
                modifiedContactsToRemove.insert(stId);
                affectedContactIds.insert(stId);
            } else {
                modifiedContactsToRemove.insert(*rit);
                affectedContactIds.insert(*rit);
            }
        }
        contactsToRemove = modifiedContactsToRemove;
    }

    // Fetch all the contacts we want to apply modifications to
    if (!affectedContactIds.isEmpty() || !contactsToAdd.isEmpty() || !contactsToRemove.isEmpty()) {
        QList<QContact> updatedContacts;
        QVariantList removeIds;

        if (!affectedContactIds.isEmpty()) {
            QContactFetchHint hint;
            hint.setOptimizationHints(QContactFetchHint::NoRelationships);

            QList<QContact> readList;
            QContactManager::Error readError = m_reader->readContacts(QLatin1String("syncUpdate"), &readList, affectedContactIds.toList(), hint);
            if ((readError != QContactManager::NoError && readError != QContactManager::DoesNotExistError) || readList.size() != affectedContactIds.size()) {
                // note that if a contact was deleted locally and modified remotely,
                // the remote-modification may reference a contact which no longer exists locally.
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to read contacts for sync update"));
                return QContactManager::UnspecifiedError;
            }

            QMap<quint32, QContact> modifiedContacts;
            foreach (const QContact &contact, readList) {
                // the contact might be empty if it was removed locally and then modified remotely.
                // TODO: support PreserveRemoteChanges semantics.
                if (contact != QContact()) {
                    modifiedContacts.insert(ContactId::databaseId(contact.id()), contact);
                }
            }

            // Create updated versions of the affected contacts
            foreach (quint32 contactId, modifiedContacts.keys()) {
                QContact contact(modifiedContacts.value(contactId));

                const QContactCollectionId colId(contact.collectionId());
                if (colId != collectionId && colId != ContactCollectionId::apiId(ContactsDatabase::LocalAddressbookCollectionId, m_managerUri) && !exportUpdate) {
                    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Invalid update constituent collectionId for sync update: %1").arg(ContactCollectionId::toString(collectionId)));
                    return QContactManager::UnspecifiedError;
                }

                if (contactsToRemove.contains(contactId)) {
                    // This contact should be removed, only if it is in the specified collection.
                    if (colId != collectionId) {
                        // XXXXXXXXXXXXx TODO: simplify? can remove this local block and adjust the check above?
                        if (colId == ContactCollectionId::apiId(ContactsDatabase::LocalAddressbookCollectionId, m_managerUri)) {
                            // We have tried to remove a local contact instead of the collection-specific constituent.  Ignore.
                            // TODO, shouldn't we also check "exportedIds"?
                            QTCONTACTS_SQLITE_DEBUG(QString::fromLatin1("Ignoring local contact removal without sync collection constituent: %1")
                                    .arg(contactId));
                        } else {
                            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Ignoring constituent removal for %1 from different collection: %2 not: %3")
                                    .arg(contactId).arg(ContactCollectionId::toString(colId)).arg(ContactCollectionId::toString(collectionId)));
                        }
                    } else {
                        removeIds.append(contactId);
                    }
                    continue;
                }

                // Apply the changes for this contact
                QSet<StringPair> removals(contactRemovals.value(contactId).toSet());

                QMap<StringPair, DetailPair> modifications;
                QMap<QContactDetail::DetailType, DetailPair> composedModifications;

                const QList<QPair<StringPair, DetailPair> > &mods(contactModifications.value(contactId));
                QList<QPair<StringPair, DetailPair> >::const_iterator mit = mods.constBegin(), mend = mods.constEnd();
                for ( ; mit != mend; ++mit) {
                    const StringPair identity((*mit).first);
                    if (identity.first.isEmpty()) {
                        const QContactDetail::DetailType type(detailType((*mit).second.second));
                        composedModifications.insert(type, (*mit).second);
                        if (type == QContactDetail::TypeUndefined
                                || (*mit).second.first.type() != type
                                || (*mit).second.second.type() != type) {
                            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("error: invalid composed modification:")
                                                      << type
                                                      << (*mit).second.first.type()
                                                      << (*mit).second.second.type());
                        }
                    } else {
                        // identity is a pair of provenance + detailTypeName
                        modifications.insert(identity, (*mit).second);
                        if (identity.second.isEmpty()
                                || (*mit).second.first.type() == QContactDetail::TypeUndefined
                                || (*mit).second.second.type() != (*mit).second.first.type()) {
                            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("error: invalid identified modification:")
                                                      << identity.first << identity.second
                                                      << (*mit).second.first.type()
                                                      << (*mit).second.second.type());
                        }
                    }
                }

                foreach (QContactDetail detail, contact.details()) {
                    // apply server-side modifications to the local contact.
                    // we determine which detail to apply the changes to via provenance field data.
                    const QString provenance(detail.value(QContactDetail__FieldProvenance).toString());
                    if (provenance.isEmpty()) {
                        // we don't have provenance to determine which detail this modification actually is.
                        // instead, attempt to match based on the detail type.
                        QMap<QContactDetail::DetailType, DetailPair>::iterator cit = composedModifications.find(detailType(detail));
                        if (cit != composedModifications.end()) {
                            // Apply this modification
                            modifyContactDetail((*cit).first, (*cit).second, conflictPolicy, &detail);
                            if (detail.type() == QContactDetail::TypeUndefined) {
                                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("error: invalid non-identified detail modification"));
                            } else {
                                contact.saveDetail(&detail, QContact::IgnoreAccessConstraints);
                            }
                            composedModifications.erase(cit);
                        }
                    } else {
                        // We have provenance.  We can determine precisely which detail
                        // is being modified.  The <provenance, type> pair form that identity.
                        const StringPair detailIdentity(qMakePair(provenance, detailTypeName(detail)));
                        QSet<StringPair>::iterator rit = removals.find(detailIdentity);
                        if (rit != removals.end()) {
                            // Remove this detail from the contact
                            contact.removeDetail(&detail);
                            removals.erase(rit);
                        } else {
                            QMap<StringPair, DetailPair>::iterator mit = modifications.find(detailIdentity);
                            if (mit != modifications.end()) {
                                // Apply the modification to this contact's detail
                                modifyContactDetail((*mit).first, (*mit).second, conflictPolicy, &detail);
                                if (detail.type() == QContactDetail::TypeUndefined) {
                                    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("error: invalid identified detail modification"));
                                } else {
                                    contact.saveDetail(&detail, QContact::IgnoreAccessConstraints);
                                }
                                modifications.erase(mit);
                            }
                        }
                    }
                }

                if (!removals.isEmpty()) {
                    // Is there anything that can be done here, for PreserveRemoteChanges?
                }
                if (!modifications.isEmpty()) {
                    // Any server-side modification of a detail which cannot be found
                    // locally (ie, was deleted locally) should either be dropped
                    // (if PreserveRemoteChanges is false) or treated as a new detail
                    // addition (if PreserveRemoteChanges is true).
                    QMap<StringPair, DetailPair>::const_iterator mit = modifications.constBegin(), mend = modifications.constEnd();
                    for ( ; mit != mend; ++mit) {
                        const StringPair identity(mit.key());
                        if (conflictPolicy == QtContactsSqliteExtensions::ContactManagerEngine::PreserveRemoteChanges) {
                            // Handle the updated value as an addition
                            QContactDetail updated(mit.value().second);
                            // Mark the updated detail as Modifiable unless it was explicitly marked (by the sync adapter) as non-modifiable
                            if (!updated.values().contains(QContactDetail__FieldModifiable)) {
                                updated.setValue(QContactDetail__FieldModifiable, true);
                            }
                            if (updated.type() == QContactDetail::TypeUndefined) {
                                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("error: invalid preserved remote detail modification-as-addition detail"));
                            } else {
                                contact.saveDetail(&updated, QContact::IgnoreAccessConstraints);
                            }
                        }
                    }
                }
                if (!composedModifications.isEmpty()) {
                    QMap<QContactDetail::DetailType, DetailPair>::const_iterator cit = composedModifications.constBegin(), cend = composedModifications.constEnd();
                    for ( ; cit != cend; ++cit) {
                        // Apply these modifications to empty details, and add to the contact.
                        // These details are different in that the resulting aggregate value cannot be
                        // accurately traced back to its origin when multiple constituents are aggregated
                        // together, unlike the details which are not subject to composition.
                        QContactDetail detail = contactDetailOfType(contact, cit.key());
                        modifyContactDetail((*cit).first, (*cit).second, conflictPolicy, &detail);
                        if (detail.type() == QContactDetail::TypeUndefined) {
                            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("error: attempting to add invalid composed modification detail"));
                        } else {
                            contact.saveDetail(&detail, QContact::IgnoreAccessConstraints);
                        }
                    }
                }

                // Store any remaining additions to the contact
                const QList<QContactDetail> &additionDetails = contactAdditions.value(contactId);
                if (!additionDetails.isEmpty()) {
                    QContact *contactForAdditions = &contact;
                    QContact syncCollectionContact;
                    quint32 syncCollectionContactId = 0;

                    if (colId != collectionId && !exportUpdate) {
                        // XXXXXXXXXXXXXXXXXX TODO: is this just plain wrong? we shouldn't ever store data to non-collection-specific contact here?
                        qFatal("XXXXXXXXXXXXXXXX broken sync promotion write?");

                        // Do we already have a constituent of this contact with our sync target?
                        syncCollectionContactId = syncCollectionConstituents.value(contactId);
                        if (syncCollectionContactId != 0) {
                            // Get the version of this contact from the retrieved set
                            syncCollectionContact = modifiedContacts.value(syncCollectionContactId);
                        } else {
                            // We need to create a new constituent of this contact, to contain the remote additions
                            syncCollectionContact.setCollectionId(collectionId);

                            // Copy some identifying detail to the new constituent
                            copyNameDetails(contact, &syncCollectionContact);
                        }

                        contactForAdditions = &syncCollectionContact;
                    }

                    foreach (QContactDetail detail, additionDetails) {
                        // Sync contact details should be modifiable unless explicitly marked otherwise by the sync adapter
                        if (!detail.values().contains(QContactDetail__FieldModifiable)) {
                            detail.setValue(QContactDetail__FieldModifiable, true);
                        }
                        if (detail.type() == QContactDetail::TypeUndefined) {
                            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("error: attempting to add invalid pure addition detail"));
                        } else {
                            contactForAdditions->saveDetail(&detail, QContact::IgnoreAccessConstraints);
                        }
                    }

                    if (contactForAdditions == &syncCollectionContact) {
                        if (syncCollectionContactId == 0) {
                            updatedContacts.append(syncCollectionContact);
                        } else {
                            modifiedContacts[syncCollectionContactId] = syncCollectionContact;
                        }
                    }
                }

                // Store the updated version in case we have to make additional modifications to it
                modifiedContacts[contactId] = contact;
            }

            foreach (const QContact &contact, modifiedContacts.values()) {
                // Update modified contacts before additions, to facilitate automatic merging
                updatedContacts.prepend(contact);
            }
        }

        // XXXXXXXXXXXXXXXXX TODO: remove this entire block?
        // create incidental local constituents to store aggregated data
        // which does not originate from the server-side contact.
        // The codepath is hit in the exportUpdates() case, when importing
        // changes from a subset database, not during "normal" sync.
/*
        QMap<quint32, QList<QContactDetail> >::const_iterator ait = aggregateAdditions.constBegin(), aend = aggregateAdditions.constEnd();
        for ( ; ait != aend; ++ait) {
            QContact updatedAggregate(aggregateDetails[ait.key()]);
            if (updatedAggregate.isEmpty()) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Could not find aggregate details to create incidental local"));
                return QContactManager::UnspecifiedError;
            }

            // Create a new local constituent of the aggregate to contain these additions
            QContact localConstituent;

            QContactIncidental incidental;
            incidental.setInitialAggregateId(updatedAggregate.id());
            localConstituent.saveDetail(&incidental, QContact::IgnoreAccessConstraints);

            copyNameDetails(updatedAggregate, &localConstituent);

            foreach (QContactDetail detail, ait.value()) {
                if (detail.type() == QContactDetail::TypeUndefined) {
                    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("error: attempting to downpromote invalid detail from aggregate"));
                } else {
                    localConstituent.saveDetail(&detail, QContact::IgnoreAccessConstraints);
                }
            }

            updatedContacts.append(localConstituent);
        }
*/

        const int additionIndex(updatedContacts.count());

        // add server-side addition contacts as new sync-collection contacts on device.
        foreach (QContact *addContact, contactsToAdd) {
            // Rebuild this contact to our specifications
            QContact newContact;

            // This contact belongs to the sync collection, unless it is from the export data;
            // in which case, it is just local device data
            if (exportUpdate) {
                newContact = *addContact;
                newContact.setCollectionId(ContactCollectionId::apiId(ContactsDatabase::LocalAddressbookCollectionId, m_managerUri));
            } else {
                newContact.setCollectionId(collectionId);
                // Copy the details to mark them all as modifiable unless explicitly marked otherwise
                foreach (QContactDetail detail, addContact->details()) {
                    // Mark the updated detail as Modifiable unless it was explicitly marked (by the sync adapter) as non-modifiable
                    if (!detail.values().contains(QContactDetail__FieldModifiable)) {
                        detail.setValue(QContactDetail__FieldModifiable, true);
                    }
                    if (detail.type() == QContactDetail::TypeUndefined) {
                        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("error: attempting to add invalid detail in new server-side addition contact"));
                    } else {
                        newContact.saveDetail(&detail, QContact::IgnoreAccessConstraints);
                    }
                }
            }

            updatedContacts.append(newContact);
        }

        if (exportUpdate && !contactsToRemove.isEmpty()) {
            // Add any contacts not yet scheduled for removal
            foreach (quint32 id, contactsToRemove) {
                if (!removeIds.contains(id)) {
                    removeIds.append(id);
                }
            }
        }

        // Store the changes we've accumulated
        QMap<int, QContactManager::Error> errorMap;
        QContactManager::Error writeError = save(&updatedContacts, DetailList(), 0, &errorMap, true, false, true);
        if (writeError != QContactManager::NoError) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to save contact changes for sync update"));
            return writeError;
        }

        const QString findAggregateForContactIds(QStringLiteral(
            " SELECT DISTINCT Relationships.firstId"
            " FROM Relationships"
            " JOIN temp.aggregationIds ON Relationships.secondId = temp.aggregationIds.contactId"
            " WHERE Relationships.type = 'Aggregates'"
        ));

        if (updatedContacts.count() > additionIndex) {
            // We added contacts; return their new IDs
            QList<QContact>::const_iterator uit = updatedContacts.constBegin() + additionIndex, uend = updatedContacts.constEnd();
            QList<QContact *>::iterator cit = contactsToAdd.begin(), cend = contactsToAdd.end();

            if (exportUpdate) {
                // The IDs we get back are for the local constituents created; we need to find their
                // aggregates and return those IDs
                QVariantList addedIds;
                for ( ; uit != uend; ++uit) {
                    const QContactId additionId((*uit).id());
                    addedIds.append(ContactId::databaseId(additionId));
                }

                m_database.clearTemporaryContactIdsTable(aggregationIdsTable);
                if (!m_database.createTemporaryContactIdsTable(aggregationIdsTable, addedIds)) {
                    return QContactManager::UnspecifiedError;
                } else {
                    ContactsDatabase::Query query(m_database.prepare(findAggregateForContactIds));
                    if (!ContactsDatabase::execute(query)) {
                        query.reportError("Failed to fetch aggregator contact ids during sync add");
                        return QContactManager::UnspecifiedError;
                    }
                    while (query.next()) {
                        if (cit == cend)  {
                            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to store aggregator contact ids during sync add"));
                            return QContactManager::UnspecifiedError;
                        } else {
                            QContact *additionContact(*cit);
                            additionContact->setId(ContactId::apiId(query.value<quint32>(0), m_managerUri));
                            additionContact->setCollectionId(ContactCollectionId::apiId(ContactsDatabase::AggregateAddressbookCollectionId, m_managerUri));
                            ++cit;
                        }
                    }

                    if (cit != cend) {
                        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to finalize aggregator contact ids during sync add"));
                        return QContactManager::UnspecifiedError;
                    }
                }
            } else {
                for ( ; uit != uend && cit != cend; ++uit, ++cit) {
                    const QContactId additionId((*uit).id());
                    QContact *additionContact(*cit);
                    additionContact->setId(additionId);
                    if (ContactCollectionId::databaseId(additionContact->collectionId()) == 0) {
                        // XXXXXX TODO: verify that this is always safe.
                        additionContact->setCollectionId(collectionId);
                    }
                }
            }
        }

        // Remove any contacts that should no longer exist
        if (!removeIds.isEmpty()) {
            QList<quint32> aggregatesOfRemoved;

            // determine which aggregates will need regeneration after removing the sync collection constituents.
            m_database.clearTemporaryContactIdsTable(aggregationIdsTable);
            if (!m_database.createTemporaryContactIdsTable(aggregationIdsTable, removeIds)) {
                return QContactManager::UnspecifiedError;
            } else {
                ContactsDatabase::Query query(m_database.prepare(findAggregateForContactIds));
                if (!ContactsDatabase::execute(query)) {
                    query.reportError("Failed to fetch aggregator contact ids during sync remove");
                    return QContactManager::UnspecifiedError;
                }
                while (query.next()) {
                    aggregatesOfRemoved.append(query.value<quint32>(0));
                }
            }

            // then remove the sync target constituents
            QContactManager::Error removeError = removeContacts(removeIds);
            if (removeError != QContactManager::NoError) {
                return removeError;
            }

            // add those ids to the change signal accumulator
            foreach (const QVariant &id, removeIds) {
                m_removedIds.insert(ContactId::apiId(id.toUInt(), m_managerUri));

                if (exportUpdate) {
                    // Also remove them from the aggregates if they are themselevs aggregates
                    aggregatesOfRemoved.removeAll(id.toUInt());
                }
            }

            // remove any childless agggregates left over by the above removal
            QList<QContactId> removedAggregateIds;
            removeError = removeChildlessAggregates(&removedAggregateIds);
            if (removeError != QContactManager::NoError) {
                return removeError;
            }

            // add those ids to the change signal accumulator
            foreach (const QContactId &removedAggId, removedAggregateIds) {
                m_removedIds.insert(removedAggId);
                const quint32 aggId(ContactId::databaseId(removedAggId));
                aggregatesOfRemoved.removeAll(aggId);
            }

            // regenerate the non-childless aggregates of the removed sync collection contacts
            QList<quint32> regenerateIds(aggregatesOfRemoved);
            if (!regenerateIds.isEmpty()) {
                removeError = regenerateAggregates(regenerateIds, DetailList(), true);
                if (removeError != QContactManager::NoError) {
                    return removeError;
                }
            }
        }
    }

    return QContactManager::NoError;
}

static bool updateGlobalPresence(QContact *contact)
{
    QContactGlobalPresence globalPresence = contact->detail<QContactGlobalPresence>();

    const QList<QContactPresence> details = contact->details<QContactPresence>();
    if (details.isEmpty()) {
        // No presence - remove global presence if present
        if (!globalPresence.isEmpty()) {
            contact->removeDetail(&globalPresence);
        }
        return true;
    }

    QContactPresence bestPresence;

    foreach (const QContactPresence &detail, details) {
        if (betterPresence(detail, bestPresence)) {
            bestPresence = detail;
        }
    }

    globalPresence.setPresenceState(bestPresence.presenceState());
    globalPresence.setPresenceStateText(bestPresence.presenceStateText());
    globalPresence.setTimestamp(bestPresence.timestamp());
    globalPresence.setNickname(bestPresence.nickname());
    globalPresence.setCustomMessage(bestPresence.customMessage());

    contact->saveDetail(&globalPresence, QContact::IgnoreAccessConstraints);
    return true;
}

static bool updateTimestamp(QContact *contact, bool setCreationTimestamp)
{
    QContactTimestamp timestamp = contact->detail<QContactTimestamp>();
    QDateTime createdTime = timestamp.created().toUTC();
    QDateTime modifiedTime = QDateTime::currentDateTimeUtc();

    // always clobber last modified timestamp.
    timestamp.setLastModified(modifiedTime);
    if (setCreationTimestamp && !createdTime.isValid()) {
        timestamp.setCreated(modifiedTime);
    }

    return contact->saveDetail(&timestamp, QContact::IgnoreAccessConstraints);
}

QContactManager::Error ContactWriter::create(QContact *contact, const DetailList &definitionMask, bool withinTransaction, bool withinAggregateUpdate)
{
    // If not specified, this contact is a "local device" contact
    if (contact->collectionId().isNull()) {
        contact->setCollectionId(ContactCollectionId::apiId(ContactsDatabase::LocalAddressbookCollectionId, m_managerUri));
    }

    // If this contact is local, ensure it has a GUID for import/export stability
    if (contact->collectionId() == ContactCollectionId::apiId(ContactsDatabase::LocalAddressbookCollectionId, m_managerUri)) {
        QContactGuid guid = contact->detail<QContactGuid>();
        if (guid.guid().isEmpty()) {
            guid.setGuid(QUuid::createUuid().toString());
            contact->saveDetail(&guid, QContact::IgnoreAccessConstraints);
        }
    }

    if (definitionMask.isEmpty()
            || detailListContains<QContactPresence>(definitionMask)
            || detailListContains<QContactGlobalPresence>(definitionMask)) {
        // update the global presence (display label may be derived from it)
        updateGlobalPresence(contact);
    }

    // update the display label for this contact
    m_engine.regenerateDisplayLabel(*contact);

    // update the timestamp if necessary (aggregate contacts should have a composed timestamp value)
    if (!m_database.aggregating() || (contact->collectionId() != ContactCollectionId::apiId(ContactsDatabase::AggregateAddressbookCollectionId, m_managerUri))) {
        updateTimestamp(contact, true); // set creation timestamp
    }

    QContactManager::Error writeErr = enforceDetailConstraints(contact);
    if (writeErr != QContactManager::NoError) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Contact failed detail constraints"));
        return writeErr;
    }

    quint32 contactId = 0;

    {
        ContactsDatabase::Query query(bindContactDetails(*contact));
        if (!ContactsDatabase::execute(query)) {
            query.reportError("Failed to create contact");
            return QContactManager::UnspecifiedError;
        }
        contactId = query.lastInsertId().toUInt();
    }

    writeErr = write(contactId, contact, definitionMask);
    if (writeErr == QContactManager::NoError) {
        // successfully saved all data.  Update id.
        contact->setId(ContactId::apiId(contactId, m_managerUri));

        if (m_database.aggregating() && !withinAggregateUpdate) {
            // and either update the aggregate contact (if it exists) or create a new one (unless it is an aggregate contact).
            if (contact->collectionId() != ContactCollectionId::apiId(ContactsDatabase::AggregateAddressbookCollectionId, m_managerUri)) {
                writeErr = setAggregate(contact, contactId, false, definitionMask, withinTransaction);
                if (writeErr != QContactManager::NoError) {
                    return writeErr;
                }
            }
        }
    }

    if (writeErr != QContactManager::NoError) {
        // error occurred.  Remove the failed entry.
        const QString removeContact(QStringLiteral(
            " DELETE FROM Contacts WHERE contactId = :contactId"
        ));

        ContactsDatabase::Query query(m_database.prepare(removeContact));
        query.bindValue(":contactId", contactId);
        if (!ContactsDatabase::execute(query)) {
            query.reportError("Unable to remove stale contact after failed save");
        }
    }

    return writeErr;
}

QContactManager::Error ContactWriter::update(QContact *contact, const DetailList &definitionMask, bool *aggregateUpdated, bool withinTransaction, bool withinAggregateUpdate, bool transientUpdate)
{
    *aggregateUpdated = false;

    quint32 contactId = ContactId::databaseId(*contact);
    int exists = 0;
    QContactCollectionId oldCollectionId;

    {
        const QString checkContactExists(QStringLiteral(
            " SELECT COUNT(contactId), collectionId FROM Contacts WHERE contactId = :contactId"
        ));

        ContactsDatabase::Query query(m_database.prepare(checkContactExists));
        query.bindValue(0, contactId);
        if (!ContactsDatabase::execute(query) || !query.next()) {
            query.reportError("Failed to check contact existence");
            return QContactManager::UnspecifiedError;
        } else {
            exists = query.value<quint32>(0);
            oldCollectionId = ContactCollectionId::apiId(query.value<quint32>(1), m_managerUri);
        }
    }

    if (!exists) {
        return QContactManager::DoesNotExistError;
    }

    // XXXXXXXXXXXXXXXX TODO: ALLOW THIS?  currently, we require moves to be deletion+addition?
    if (!oldCollectionId.isNull() && contact->collectionId() != oldCollectionId) {
        // they are attempting to manually change the collectionId of a contact
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot manually change collectionId!"));
        return QContactManager::UnspecifiedError;
    }


    QContactManager::Error writeError = enforceDetailConstraints(contact);
    if (writeError != QContactManager::NoError) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Contact failed detail constraints"));
        return writeError;
    }

    // update the modification timestamp (aggregate contacts should have a composed timestamp value)
    if (!m_database.aggregating()
            || (contact->collectionId() != ContactCollectionId::apiId(ContactsDatabase::AggregateAddressbookCollectionId, m_managerUri))) {
        updateTimestamp(contact, false);
    }

    if (m_database.aggregating()
            && (!withinAggregateUpdate
                && oldCollectionId == ContactCollectionId::apiId(ContactsDatabase::AggregateAddressbookCollectionId, m_managerUri))) {
        // Attempting to update an aggregate contact directly.
        // This codepath should not be possible, and if hit
        // is always a result of a bug in qtcontacts-sqlite.
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Error: direct modification of aggregate contact %1").arg(contactId));
        return QContactManager::UnspecifiedError;
    }

    if (definitionMask.isEmpty()
            || detailListContains<QContactPresence>(definitionMask)
            || detailListContains<QContactGlobalPresence>(definitionMask)) {
        // update the global presence (display label may be derived from it)
        updateGlobalPresence(contact);
    }

    // update the display label for this contact
    m_engine.regenerateDisplayLabel(*contact);

    // Can this update be transient, or does it need to be durable?
    if (transientUpdate) {
        // Instead of updating the database, store these minor changes only to the transient store
        QList<QContactDetail> transientDetails;
        foreach (const QContactDetail &detail, contact->details()) {
            if (definitionMask.contains(detail.type())
                    || definitionMask.contains(generatorType(detail.type()))) {
                // Only store the details indicated by the detail type mask
                transientDetails.append(detail);
            }
        }

        if (oldCollectionId == ContactCollectionId::apiId(ContactsDatabase::AggregateAddressbookCollectionId, m_managerUri)) {
            // We need to modify the detail URIs in these details
            QList<QContactDetail>::iterator it = transientDetails.begin(), end = transientDetails.end();
            for ( ; it != end; ++it) {
                adjustAggregateDetailProperties(*it);
            }
        }

        const QDateTime lastModified(contact->detail<QContactTimestamp>().lastModified());
        if (!m_database.setTransientDetails(contactId, lastModified, transientDetails)) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Could not perform transient update; fallback to durable update"));
            transientUpdate = false;
        }
    }

    if (!transientUpdate) {
        // This update invalidates any details that may be present in the transient store
        m_database.removeTransientDetails(contactId);

        // Store updated details to the database
        {
            ContactsDatabase::Query query(bindContactDetails(*contact, definitionMask, contactId));
            if (!ContactsDatabase::execute(query)) {
                query.reportError("Failed to update contact");
                return QContactManager::UnspecifiedError;
            }
        }

        writeError = write(contactId, contact, definitionMask);
    }

    if (m_database.aggregating() && writeError == QContactManager::NoError) {
        if (oldCollectionId != ContactCollectionId::apiId(ContactsDatabase::AggregateAddressbookCollectionId, m_managerUri)) {
            QList<quint32> aggregatesOfUpdated;

            {
                const QString findAggregateForContact(QStringLiteral(
                    " SELECT DISTINCT firstId FROM Relationships"
                    " WHERE type = 'Aggregates' AND secondId = :localId"
                ));

                ContactsDatabase::Query query(m_database.prepare(findAggregateForContact));
                query.bindValue(":localId", contactId);
                if (!ContactsDatabase::execute(query)) {
                    query.reportError("Failed to fetch aggregator contact ids during remove");
                    return QContactManager::UnspecifiedError;
                }
                while (query.next()) {
                    aggregatesOfUpdated.append(query.value<quint32>(0));
                }
            }

            if (aggregatesOfUpdated.size() > 0) {
                writeError = regenerateAggregates(aggregatesOfUpdated, definitionMask, withinTransaction);
            } else if (oldCollectionId == ContactCollectionId::apiId(ContactsDatabase::LocalAddressbookCollectionId, m_managerUri)) {
                writeError = setAggregate(contact, contactId, true, definitionMask, withinTransaction);
            }
            if (writeError != QContactManager::NoError) {
                return writeError;
            }

            *aggregateUpdated = true;
        }
    }

    return writeError;
}

QContactManager::Error ContactWriter::setAggregate(QContact *contact, quint32 contactId, bool update, const DetailList &definitionMask, bool withinTransaction)
{
    quint32 aggregateId = 0;
    QContactManager::Error writeErr = updateOrCreateAggregate(contact, definitionMask, withinTransaction, &aggregateId);
    if ((writeErr == QContactManager::NoError) && (update || (aggregateId < contactId))) {
        // The aggregate pre-dates the new contact - it probably had a local constituent already.
        // We must regenerate the aggregate, because the precedence order of the details may have changed.
        writeErr = regenerateAggregates(QList<quint32>() << aggregateId, definitionMask, withinTransaction);
        if (writeErr != QContactManager::NoError) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to regenerate aggregate contact %1 for local insertion").arg(aggregateId));
        }
    }

    return writeErr;
}

QContactManager::Error ContactWriter::write(quint32 contactId, QContact *contact, const DetailList &definitionMask)
{
    // Does this contact belong to a synced addressbook?
    const QContactCollectionId collectionId = contact->collectionId();
    const bool wasLocal = false; // XXXXXXXXXXXXXXXXXXXX TODO fixme?
    const bool syncable = (ContactCollectionId::databaseId(collectionId) != ContactsDatabase::AggregateAddressbookCollectionId) &&
                          (ContactCollectionId::databaseId(collectionId) != ContactsDatabase::LocalAddressbookCollectionId);

    QContactManager::Error error = QContactManager::NoError;
    if (writeDetails<QContactAddress>(contactId, contact, definitionMask, collectionId, syncable, wasLocal, &error)
            && writeDetails<QContactAnniversary>(contactId, contact, definitionMask, collectionId, syncable, wasLocal, &error)
            && writeDetails<QContactAvatar>(contactId, contact, definitionMask, collectionId, syncable, wasLocal, &error)
            && writeDetails<QContactBirthday>(contactId, contact, definitionMask, collectionId, syncable, wasLocal, &error)
            && writeDetails<QContactEmailAddress>(contactId, contact, definitionMask, collectionId, syncable, wasLocal, &error)
            && writeDetails<QContactFamily>(contactId, contact, definitionMask, collectionId, syncable, wasLocal, &error)
            && writeDetails<QContactGeoLocation>(contactId, contact, definitionMask, collectionId, syncable, wasLocal, &error)
            && writeDetails<QContactGlobalPresence>(contactId, contact, definitionMask, collectionId, syncable, wasLocal, &error)
            && writeDetails<QContactGuid>(contactId, contact, definitionMask, collectionId, syncable, wasLocal, &error)
            && writeDetails<QContactHobby>(contactId, contact, definitionMask, collectionId, syncable, wasLocal, &error)
            && writeDetails<QContactNickname>(contactId, contact, definitionMask, collectionId, syncable, wasLocal, &error)
            && writeDetails<QContactNote>(contactId, contact, definitionMask, collectionId, syncable, wasLocal, &error)
            && writeDetails<QContactOnlineAccount>(contactId, contact, definitionMask, collectionId, syncable, wasLocal, &error)
            && writeDetails<QContactOrganization>(contactId, contact, definitionMask, collectionId, syncable, wasLocal, &error)
            && writeDetails<QContactPhoneNumber>(contactId, contact, definitionMask, collectionId, syncable, wasLocal, &error)
            && writeDetails<QContactPresence>(contactId, contact, definitionMask, collectionId, syncable, wasLocal, &error)
            && writeDetails<QContactRingtone>(contactId, contact, definitionMask, collectionId, syncable, wasLocal, &error)
            && writeDetails<QContactTag>(contactId, contact, definitionMask, collectionId, syncable, wasLocal, &error)
            && writeDetails<QContactUrl>(contactId, contact, definitionMask, collectionId, syncable, wasLocal, &error)
            && writeDetails<QContactOriginMetadata>(contactId, contact, definitionMask, collectionId, syncable, wasLocal, &error)
            && writeDetails<QContactExtendedDetail>(contactId, contact, definitionMask, collectionId, syncable, wasLocal, &error)
            ) {
        return QContactManager::NoError;
    }
    return error;
}

ContactsDatabase::Query ContactWriter::bindContactDetails(const QContact &contact, const DetailList &definitionMask, quint32 contactId)
{
    const QString insertContact(QStringLiteral(
        " INSERT INTO Contacts ("
        "  collectionId,"
        "  displayLabel,"
        "  displayLabelGroup,"
        "  displayLabelGroupSortOrder,"
        "  firstName,"
        "  lowerFirstName,"
        "  lastName,"
        "  lowerLastName,"
        "  middleName,"
        "  prefix,"
        "  suffix,"
        "  customLabel,"
        "  syncTarget,"
        "  created,"
        "  modified,"
        "  gender,"
        "  isFavorite,"
        "  hasPhoneNumber,"
        "  hasEmailAddress,"
        "  hasOnlineAccount,"
        "  isOnline,"
        "  isDeactivated)"
        " VALUES ("
        "  :collectionId,"
        "  :displayLabel,"
        "  :displayLabelGroup,"
        "  :displayLabelGroupSortOrder,"
        "  :firstName,"
        "  :lowerFirstName,"
        "  :lastName,"
        "  :lowerLastName,"
        "  :middleName,"
        "  :prefix,"
        "  :suffix,"
        "  :customLabel,"
        "  :syncTarget,"
        "  :created,"
        "  :modified,"
        "  :gender,"
        "  :isFavorite,"
        "  :hasPhoneNumber,"
        "  :hasEmailAccount,"
        "  :hasOnlineAccount,"
        "  :isOnline,"
        "  :isDeactivated)"
    ));
    const QString updateContact(QStringLiteral(
        " UPDATE Contacts SET"
        "  collectionId = :collectionId,"
        "  displayLabel = :displayLabel,"
        "  displayLabelGroup = :displayLabelGroup,"
        "  displayLabelGroupSortOrder = :displayLabelGroupSortOrder,"
        "  firstName = :firstName,"
        "  lowerFirstName = :lowerFirstName,"
        "  lastName = :lastName,"
        "  lowerLastName = :lowerLastName,"
        "  middleName = :middleName,"
        "  prefix = :prefix,"
        "  suffix = :suffix,"
        "  customLabel = :customLabel,"
        "  syncTarget = :syncTarget,"
        "  created = :created,"
        "  modified = :modified,"
        "  gender = :gender,"
        "  isFavorite = :isFavorite,"
        "  hasPhoneNumber = CASE WHEN :valueKnown = 1 THEN :value ELSE hasPhoneNumber END,"
        "  hasEmailAddress = CASE WHEN :valueKnown = 1 THEN :value ELSE hasEmailAddress END,"
        "  hasOnlineAccount = CASE WHEN :valueKnown = 1 THEN :value ELSE hasOnlineAccount END,"
        "  isOnline = CASE WHEN :valueKnown = 1 THEN :value ELSE isOnline END,"
        "  isDeactivated = CASE WHEN :valueKnown = 1 THEN :value ELSE isDeactivated END"
        " WHERE contactId = :contactId;"
    ));

    const bool update(contactId != 0);

    ContactsDatabase::Query query(m_database.prepare(update ? updateContact : insertContact));

    const QContactName name = contact.detail<QContactName>();
    const QString firstName(name.value<QString>(QContactName::FieldFirstName).trimmed());
    const QString lastName(name.value<QString>(QContactName::FieldLastName).trimmed());

    int col = 0;
    const quint32 collectionId = ContactCollectionId::databaseId(contact.collectionId()) > 0
                               ? ContactCollectionId::databaseId(contact.collectionId())
                               : static_cast<quint32>(ContactsDatabase::LocalAddressbookCollectionId);

    query.bindValue(col++, collectionId);

    QContactDisplayLabel label = contact.detail<QContactDisplayLabel>();
    const QString displayLabel = label.label().trimmed();
    query.bindValue(col++, displayLabel);
    const QString displayLabelGroup = m_database.determineDisplayLabelGroup(contact, &m_displayLabelGroupsChanged);
    query.bindValue(col++, displayLabelGroup);
    const int displayLabelGroupSortOrder = m_database.displayLabelGroupSortValue(displayLabelGroup);
    query.bindValue(col++, displayLabelGroupSortOrder);

    query.bindValue(col++, firstName);
    query.bindValue(col++, firstName.toLower());
    query.bindValue(col++, lastName);
    query.bindValue(col++, lastName.toLower());
    query.bindValue(col++, name.value<QString>(QContactName::FieldMiddleName).trimmed());
    query.bindValue(col++, name.value<QString>(QContactName::FieldPrefix).trimmed());
    query.bindValue(col++, name.value<QString>(QContactName::FieldSuffix).trimmed());
    query.bindValue(col++, name.value<QString>(QContactName__FieldCustomLabel).trimmed());

    const QString syncTarget(contact.detail<QContactSyncTarget>().syncTarget());
    query.bindValue(col++, syncTarget);

    const QContactTimestamp timestamp = contact.detail<QContactTimestamp>();
    query.bindValue(col++, ContactsDatabase::dateTimeString(timestamp.value<QDateTime>(QContactTimestamp::FieldCreationTimestamp).toUTC()));
    query.bindValue(col++, ContactsDatabase::dateTimeString(timestamp.value<QDateTime>(QContactTimestamp::FieldModificationTimestamp).toUTC()));

    const QContactGender gender = contact.detail<QContactGender>();
    query.bindValue(col++, QString::number(static_cast<int>(gender.gender())));

    const QContactFavorite favorite = contact.detail<QContactFavorite>();
    query.bindValue(col++, favorite.isFavorite());

    // Does this contact contain the information needed to update hasPhoneNumber?
    bool hasPhoneNumberKnown = definitionMask.isEmpty() || detailListContains<QContactPhoneNumber>(definitionMask);
    bool hasPhoneNumber = hasPhoneNumberKnown ? !contact.detail<QContactPhoneNumber>().isEmpty() : false;

    bool hasEmailAddressKnown = definitionMask.isEmpty() || detailListContains<QContactEmailAddress>(definitionMask);
    bool hasEmailAddress = hasEmailAddressKnown ? !contact.detail<QContactEmailAddress>().isEmpty() : false;

    bool hasOnlineAccountKnown = definitionMask.isEmpty() || detailListContains<QContactOnlineAccount>(definitionMask);
    bool hasOnlineAccount = hasOnlineAccountKnown ? !contact.detail<QContactOnlineAccount>().isEmpty() : false;

    // isOnline is true if any presence details are not offline/unknown
    bool isOnlineKnown = definitionMask.isEmpty() || detailListContains<QContactPresence>(definitionMask);
    bool isOnline = false;
    foreach (const QContactPresence &presence, contact.details<QContactPresence>()) {
        if (presence.presenceState() >= QContactPresence::PresenceAvailable &&
            presence.presenceState() <= QContactPresence::PresenceExtendedAway) {
            isOnline = true;
            break;
        }
    }

    // isDeactivated is true if the contact contains QContactDeactivated
    bool isDeactivatedKnown = definitionMask.isEmpty() || detailListContains<QContactDeactivated>(definitionMask);
    bool isDeactivated = isDeactivatedKnown ? !contact.details<QContactDeactivated>().isEmpty() : false;
    if (isDeactivated) {
        // Deactivation is only possible for syncable contacts
        if (collectionId == ContactsDatabase::AggregateAddressbookCollectionId
                || collectionId == ContactsDatabase::LocalAddressbookCollectionId) {
            isDeactivated = false;
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot set deactivated for collection: %1").arg(collectionId));
        }
    }

    if (update) {
        query.bindValue(col++, hasPhoneNumberKnown);
        query.bindValue(col++, hasPhoneNumber);
        query.bindValue(col++, hasEmailAddressKnown);
        query.bindValue(col++, hasEmailAddress);
        query.bindValue(col++, hasOnlineAccountKnown);
        query.bindValue(col++, hasOnlineAccount);
        query.bindValue(col++, isOnlineKnown);
        query.bindValue(col++, isOnline);
        query.bindValue(col++, isDeactivatedKnown);
        query.bindValue(col++, isDeactivated);
        query.bindValue(col++, contactId);
    } else {
        query.bindValue(col++, hasPhoneNumber);
        query.bindValue(col++, hasEmailAddress);
        query.bindValue(col++, hasOnlineAccount);
        query.bindValue(col++, isOnline);
        query.bindValue(col++, isDeactivated);
    }

    return query;
}

ContactsDatabase::Query ContactWriter::bindCollectionDetails(const QContactCollection &collection)
{
    const QString insertCollection(QStringLiteral(
        " INSERT INTO Collections ("
        "  aggregable,"
        "  name,"
        "  description,"
        "  color,"
        "  secondaryColor,"
        "  image,"
        "  accountId,"
        "  remotePath)"
        " VALUES ("
        "  :aggregable,"
        "  :name,"
        "  :description,"
        "  :color,"
        "  :secondaryColor,"
        "  :image,"
        "  :accountId,"
        "  :remotePath)"
    ));
    const QString updateCollection(QStringLiteral(
        " UPDATE Collections SET"
        "  aggregable = :aggregable,"
        "  name = :name,"
        "  description = :description,"
        "  color = :color,"
        "  secondaryColor = :secondaryColor,"
        "  image = :image,"
        "  accountId = :accountId,"
        "  remotePath = :remotePath"
        " WHERE collectionId = :collectionId;"
    ));

    const bool update(ContactCollectionId::isValid(collection));

    ContactsDatabase::Query query(m_database.prepare(update ? updateCollection : insertCollection));
    query.bindValue(":aggregable", collection.extendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_AGGREGABLE).isNull()
                          ? true : collection.extendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_AGGREGABLE).toBool());
    query.bindValue(":name", collection.metaData(QContactCollection::KeyName).toString());
    query.bindValue(":description", collection.metaData(QContactCollection::KeyDescription).toString());
    query.bindValue(":color", collection.metaData(QContactCollection::KeyColor).toString());
    query.bindValue(":secondaryColor", collection.metaData(QContactCollection::KeySecondaryColor).toString());
    query.bindValue(":image", collection.metaData(QContactCollection::KeyImage).toString());
    query.bindValue(":accountId", collection.extendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID).toInt());
    query.bindValue(":remotePath", collection.extendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH).toString());
    if (update) {
        query.bindValue(":collectionId", ContactCollectionId::databaseId(collection));
    }

    return query;
}

