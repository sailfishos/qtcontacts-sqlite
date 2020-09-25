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

#include "../extensions/contactdelta_impl.h"
#include "../extensions/qcontactundelete.h"
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
#include <QElapsedTimer>

namespace {
    void dumpContact(const QContact &c)
    {
        Q_FOREACH (const QContactDetail &det, c.details()) {
            dumpContactDetail(det);
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

static const QString aggregateSyncTarget(QStringLiteral("aggregate"));
static const QString localSyncTarget(QStringLiteral("local"));
static const QString wasLocalSyncTarget(QStringLiteral("was_local"));
static const QString exportSyncTarget(QStringLiteral("export"));

static const QString aggregationIdsTable(QStringLiteral("aggregationIds"));
static const QString modifiableContactsTable(QStringLiteral("modifiableContacts"));
static const QString syncConstituentsTable(QStringLiteral("syncConstituents"));
static const QString syncAggregatesTable(QStringLiteral("syncAggregates"));

static const QString possibleAggregatesTable(QStringLiteral("possibleAggregates"));
static const QString matchEmailAddressesTable(QStringLiteral("matchEmailAddresses"));
static const QString matchPhoneNumbersTable(QStringLiteral("matchPhoneNumbers"));
static const QString matchOnlineAccountsTable(QStringLiteral("matchOnlineAccounts"));

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
    if (m_suppressedCollectionIds.size()) {
        QSet<QContactCollectionId> collectionContactsChanged = m_collectionContactsChanged;
        Q_FOREACH (const QContactCollectionId &suppressed, m_suppressedCollectionIds) {
            collectionContactsChanged.remove(suppressed);
        }
        m_collectionContactsChanged = collectionContactsChanged;
    }
    m_suppressedCollectionIds.clear();
    if (!m_collectionContactsChanged.isEmpty()) {
        m_notifier->collectionContactsChanged(m_collectionContactsChanged.toList());
        m_collectionContactsChanged.clear();
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
    m_collectionContactsChanged.clear();
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
    QMutexLocker locker(withinTransaction ? nullptr : m_database.accessMutex());

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
            " SELECT contactId FROM Contacts WHERE changeFlags < 4" // ChangeFlags::IsDeleted
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
    QString queryString = QStringLiteral("INSERT INTO Relationships");
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
                queryString += QStringLiteral("\n SELECT :firstId%1 as firstId, :secondId%1 as secondId, :type%1 as type")
                                         .arg(QString::number(realInsertions));
            } else {
                queryString += QStringLiteral("\n UNION SELECT :firstId%1, :secondId%1, :type%1")
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
        multiInsertQuery.bindValue(QStringLiteral(":firstId%1").arg(QString::number(i)), firstIdsToBind.at(i));
        multiInsertQuery.bindValue(QStringLiteral(":secondId%1").arg(QString::number(i)), secondIdsToBind.at(i));
        multiInsertQuery.bindValue(QStringLiteral(":type%1").arg(QString::number(i)), typesToBind.at(i));
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
    QMutexLocker locker(withinTransaction ? nullptr : m_database.accessMutex());

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
            " WHERE firstId NOT IN (SELECT contactId FROM Contacts WHERE changeFlags >= 4)"
            "  AND secondId NOT IN (SELECT contactId FROM Contacts WHERE changeFlags >= 4)" // ChangeFlags::IsDeleted
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
        QContactManager::Error aggregateError = aggregateOrphanedContacts(true, false);
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

    int extendedMetadataCount = 0;
    ContactsDatabase::Query metadataQuery(bindCollectionMetadataDetails(*collection, &extendedMetadataCount));
    if (extendedMetadataCount > 0 && !ContactsDatabase::executeBatch(metadataQuery)) {
        query.reportError("Failed to save collection metadata");
        return QContactManager::UnspecifiedError;
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

    QMutexLocker locker(withinTransaction ? nullptr : m_database.accessMutex());

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
            query.bindValue(QStringLiteral(":collectionId"), ContactCollectionId::databaseId(collection.id()));
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

QContactManager::Error ContactWriter::removeCollection(const QContactCollectionId &collectionId, bool onlyIfFlagged)
{
    const QString removeCollectionStatement(QStringLiteral(
        " DELETE FROM Collections WHERE collectionId = :collectionId %1"
    ).arg(onlyIfFlagged ? QStringLiteral("AND changeFlags >= 4") : QString())); // ChangeFlags::IsDeleted
    ContactsDatabase::Query remove(m_database.prepare(removeCollectionStatement));
    remove.bindValue(QStringLiteral(":collectionId"), ContactCollectionId::databaseId(collectionId));
    if (!ContactsDatabase::execute(remove)) {
        remove.reportError("Failed to remove collection");
        return QContactManager::UnspecifiedError;
    }
    return QContactManager::NoError;
}

QContactManager::Error ContactWriter::deleteCollection(const QContactCollectionId &collectionId)
{
    const QString deleteCollectionStatement(QStringLiteral(
        " UPDATE Collections SET"
          " changeFlags = changeFlags | 4" // ChangeFlags::IsDeleted
        " WHERE collectionId = :collectionId"
    ));
    ContactsDatabase::Query deleteCollection(m_database.prepare(deleteCollectionStatement));
    deleteCollection.bindValue(QStringLiteral(":collectionId"), ContactCollectionId::databaseId(collectionId));
    if (!ContactsDatabase::execute(deleteCollection)) {
        deleteCollection.reportError("Failed to delete collection");
        return QContactManager::UnspecifiedError;
    }

    const QString deleteCollectionContactsStatement(QStringLiteral(
        " UPDATE Contacts SET"
          " changeFlags = changeFlags | 4," // ChangeFlags::IsDeleted
          " deleted = strftime('%Y-%m-%dT%H:%M:%fZ', 'now')"
        " WHERE collectionId = :collectionId"
    ));
    ContactsDatabase::Query deleteCollectionContacts(m_database.prepare(deleteCollectionContactsStatement));
    deleteCollectionContacts.bindValue(QStringLiteral(":collectionId"), ContactCollectionId::databaseId(collectionId));
    if (!ContactsDatabase::execute(deleteCollectionContacts)) {
        deleteCollectionContacts.reportError("Failed to delete collection contacts");
        return QContactManager::UnspecifiedError;
    }

    return QContactManager::NoError;
}

QContactManager::Error ContactWriter::remove(
        const QList<QContactCollectionId> &collectionIds,
        QMap<int, QContactManager::Error> *errorMap,
        bool withinTransaction,
        bool withinSyncUpdate)
{
    QMutexLocker locker(withinTransaction ? nullptr : m_database.accessMutex());

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
                " SELECT ContactId FROM Contacts WHERE collectionId = :collectionId AND changeFlags < 4" // ChangeFlags::IsDeleted
            ));
            ContactsDatabase::Query query(m_database.prepare(queryContactIds));
            query.bindValue(QStringLiteral(":collectionId"), ContactCollectionId::databaseId(collectionId));
            if (!ContactsDatabase::execute(query)) {
                query.reportError("Failed to query collection contacts");
                removeError = QContactManager::UnspecifiedError;
            } else while (query.next()) {
                collectionContacts.append(ContactId::apiId(query.value<quint32>(0), m_managerUri));
            }

            if (removeError == QContactManager::NoError) {
                removeError = remove(collectionContacts, nullptr, true, withinSyncUpdate);
                if (removeError != QContactManager::NoError) {
                    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to remove contacts while removing collection"));
                } else {
                    foreach (const QContactId &rid, collectionContacts) {
                        removedContactIds.insert(rid);
                    }
                    removeError = deleteCollection(collectionId);
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

QContactManager::Error ContactWriter::removeContacts(const QVariantList &ids, bool onlyIfFlagged)
{
    const QString removeContact(QStringLiteral(
        " DELETE FROM Contacts WHERE contactId = :contactId %1"
    ).arg(onlyIfFlagged ? QStringLiteral("AND changeFlags >= 4 AND unhandledChangeFlags < 4") // ChangeFlags::IsDeleted
                        : QString()));

    // do it in batches, otherwise the query can fail due to too many bound values.
    for (int i = 0; i < ids.size(); i += 167) {
        const QVariantList cids = ids.mid(i, qMin(ids.size() - i, 167));
        ContactsDatabase::Query query(m_database.prepare(removeContact));
        query.bindValue(QStringLiteral(":contactId"), cids);
        if (!ContactsDatabase::executeBatch(query)) {
            query.reportError("Failed to remove contacts");
            return QContactManager::UnspecifiedError;
        }
    }

    return QContactManager::NoError;
}

QContactManager::Error ContactWriter::removeDetails(const QVariantList &contactIds, bool onlyIfFlagged)
{
    const QString removeDetail(QStringLiteral(
        " DELETE FROM Details WHERE contactId = :contactId %1"
    ).arg(onlyIfFlagged ? QStringLiteral("AND changeFlags >= 4 AND unhandledChangeFlags < 4") // ChangeFlags::IsDeleted
                        : QString()));

    // do it in batches, otherwise the query can fail due to too many bound values.
    for (int i = 0; i < contactIds.size(); i += 167) {
        const QVariantList cids = contactIds.mid(i, qMin(contactIds.size() - i, 167));
        ContactsDatabase::Query query(m_database.prepare(removeDetail));
        query.bindValue(QStringLiteral(":contactId"), cids);
        if (!ContactsDatabase::executeBatch(query)) {
            query.reportError("Failed to remove details");
            return QContactManager::UnspecifiedError;
        }
    }

    return QContactManager::NoError;
}

// NOTE: this should NEVER be used for synced contacts, only local contacts (for undo support).
QContactManager::Error ContactWriter::undeleteContacts(const QVariantList &ids, bool recordUnhandledChangeFlags)
{
    // TODO: CONSIDER THE POSSIBLE SYNC ISSUES RELATED TO THIS OPERATION... I SUSPECT THIS CAN NEVER WORK
    const QString undeleteContact(QStringLiteral(
        " UPDATE Contacts SET"
          " changeFlags = CASE WHEN changeFlags >= 4 THEN changeFlags - 4 ELSE changeFlags END," // ChangeFlags::IsDeleted
          " unhandledChangeFlags = %1,"
          " deleted = NULL"
        " WHERE contactId = :contactId"
    ).arg(recordUnhandledChangeFlags ? QStringLiteral("CASE WHEN unhandledChangeFlags >= 4 THEN unhandledChangeFlags - 4 ELSE unhandledChangeFlags END")
                                     : QStringLiteral("unhandledChangeFlags")));

    // do it in batches, otherwise the query can fail due to too many bound values.
    for (int i = 0; i < ids.size(); i += 167) {
        const QVariantList cids = ids.mid(i, qMin(ids.size() - i, 167));
        ContactsDatabase::Query query(m_database.prepare(undeleteContact));
        query.bindValue(QStringLiteral(":contactId"), cids);
        if (!ContactsDatabase::executeBatch(query)) {
            query.reportError("Failed to undelete contact");
            return QContactManager::UnspecifiedError;
        }
    }

    return QContactManager::NoError;
}

QContactManager::Error ContactWriter::deleteContacts(const QVariantList &ids, bool recordUnhandledChangeFlags)
{
    const QString deleteContact(QStringLiteral(
        " UPDATE Contacts SET"
          " changeFlags = changeFlags | 4," // ChangeFlags::IsDeleted
          " %1"
          " deleted = strftime('%Y-%m-%dT%H:%M:%fZ', 'now')"
        " WHERE contactId = :contactId"
    ).arg(recordUnhandledChangeFlags ? QStringLiteral(" unhandledChangeFlags = unhandledChangeFlags | 4,") : QString()));

    // do it in batches, otherwise the query can fail due to too many bound values.
    for (int i = 0; i < ids.size(); i += 167) {
        const QVariantList cids = ids.mid(i, qMin(ids.size() - i, 167));
        ContactsDatabase::Query query(m_database.prepare(deleteContact));
        query.bindValue(QStringLiteral(":contactId"), cids);
        if (!ContactsDatabase::executeBatch(query)) {
            query.reportError("Failed to delete contacts");
            return QContactManager::UnspecifiedError;
        }
    }

    return QContactManager::NoError;
}

QContactManager::Error ContactWriter::remove(const QList<QContactId> &contactIds, QMap<int, QContactManager::Error> *errorMap, bool withinTransaction, bool withinSyncUpdate)
{
    QMutexLocker locker(withinTransaction ? nullptr : m_database.accessMutex());

    if (contactIds.isEmpty())
        return QContactManager::NoError;

    // grab the self-contact id so we can avoid removing it.
    quint32 selfContactId = 0;
    {
        QContactId id;
        QContactManager::Error err;
        if ((err = m_reader->getIdentity(ContactsDatabase::SelfContactId, &id)) != QContactManager::NoError) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to determine self ID while deleting contacts"));
            return err;
        }
        selfContactId = ContactId::databaseId(id); // the aggregate self contact id, the local will be less than it.
    }

    // grab the existing contact ids so that we can perform removal detection
    // we also determine whether the contact is an aggregate (and prevent if so).
    QHash<quint32, quint32> existingContactIds; // contactId to collectionId
    {
        const QString findExistingContactIds(QStringLiteral(
            " SELECT contactId, collectionId FROM Contacts WHERE changeFlags < 4" // ChangeFlags::IsDeleted
        ));
        ContactsDatabase::Query query(m_database.prepare(findExistingContactIds));
        if (!ContactsDatabase::execute(query)) {
            query.reportError("Failed to fetch existing contact ids during delete");
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
    quint32 collectionId = 0;
    for (int i = 0; i < contactIds.size(); ++i) {
        QContactId currId = contactIds.at(i);
        quint32 dbId = ContactId::databaseId(currId);
        if (dbId == 0) {
            if (errorMap)
                errorMap->insert(i, QContactManager::DoesNotExistError);
            error = QContactManager::DoesNotExistError;
        } else if (selfContactId > 0 && dbId <= selfContactId) {
            QTCONTACTS_SQLITE_DEBUG(QString::fromLatin1("Cannot delete special self contacts"));
            if (errorMap)
                errorMap->insert(i, QContactManager::BadArgumentError);
            error = QContactManager::BadArgumentError;
        } else if (existingContactIds.contains(dbId)) {
            const quint32 removeContactCollectionId = existingContactIds.value(dbId);
            if (removeContactCollectionId == ContactsDatabase::AggregateAddressbookCollectionId) {
                QTCONTACTS_SQLITE_DEBUG(QString::fromLatin1("Cannot delete contacts from aggregate collection"));
                if (errorMap)
                    errorMap->insert(i, QContactManager::BadArgumentError);
                error = QContactManager::BadArgumentError;
            } else {
                if (collectionId == 0) {
                    collectionId = existingContactIds.value(dbId);
                }

                if (collectionId != existingContactIds.value(dbId)) {
                    QTCONTACTS_SQLITE_DEBUG(QString::fromLatin1("Cannot delete contacts from multiple collections in a single batch"));
                    if (errorMap)
                        errorMap->insert(i, QContactManager::BadArgumentError);
                    error = QContactManager::BadArgumentError;
                } else {
                    realRemoveIds.append(currId);
                    boundRealRemoveIds.append(dbId);
                    removeChangedCollectionIds.insert(ContactCollectionId::apiId(removeContactCollectionId, m_managerUri));
                }
            }
        } else {
            if (errorMap)
                errorMap->insert(i, QContactManager::DoesNotExistError);
            error = QContactManager::DoesNotExistError;
        }
    }

    if (realRemoveIds.size() == 0 || error != QContactManager::NoError) {
        return error;
    }

    bool recordUnhandledChangeFlags = false;
    if (!withinSyncUpdate
            && m_reader->recordUnhandledChangeFlags(ContactCollectionId::apiId(collectionId, realRemoveIds.first().managerUri()),
                                                    &recordUnhandledChangeFlags) != QContactManager::NoError) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to determine recordUnhandledChangeFlags value for collection: %1")
                                                 .arg(collectionId));
        return QContactManager::UnspecifiedError;
    }

    if (!m_database.aggregating()) {
        // If we don't perform aggregation, we simply need to remove every
        // (valid, non-self) contact specified in the list.
        if (!withinTransaction && !beginTransaction()) {
            // if we are not already within a transaction, create a transaction.
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to begin database transaction while deleting contacts"));
            return QContactManager::UnspecifiedError;
        }
        QContactManager::Error removeError = deleteContacts(boundRealRemoveIds, recordUnhandledChangeFlags);
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
        foreach (const QContactCollectionId &rccid, removeChangedCollectionIds) {
            m_collectionContactsChanged.insert(rccid);
        }
        if (!withinTransaction && !commitTransaction()) {
            // only commit if we created a transaction.
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to commit deletion"));
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
            query.reportError("Failed to fetch aggregator contact ids during delete");
            return QContactManager::UnspecifiedError;
        }
        while (query.next()) {
            aggregatesOfRemoved.append(query.value<quint32>(0));
        }
    }

    if (!withinTransaction && !beginTransaction()) {
        // only create a transaction if we're not already within one
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to begin database transaction while deleting contacts"));
        return QContactManager::UnspecifiedError;
    }

    // remove the non-aggregate contacts which were specified for removal.
    if (boundRealRemoveIds.size() > 0) {
        QContactManager::Error removeError = deleteContacts(boundRealRemoveIds, recordUnhandledChangeFlags);
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

    // And notify of any removals.
    if (realRemoveIds.size() > 0) {
        // update our "regenerate list" by purging deleted contacts
        foreach (const QContactId &removedId, realRemoveIds) {
            aggregatesOfRemoved.removeAll(ContactId::databaseId(removedId));
        }
    }

    // Now regenerate our remaining aggregates as required.
    if (aggregatesOfRemoved.size() > 0) {
        QContactManager::Error writeError = regenerateAggregates(aggregatesOfRemoved, DetailList(), true);
        if (writeError != QContactManager::NoError) {
            if (!withinTransaction) {
                // only rollback the transaction if we created it
                rollbackTransaction();
            }
            return writeError;
        }
    }

    foreach (const QContactId &id, realRemoveIds) {
        m_removedIds.insert(id);
    }

    foreach (const QContactCollectionId &rccid, removeChangedCollectionIds) {
        m_collectionContactsChanged.insert(rccid);
    }

    // Success!  If we created a transaction, commit.
    if (!withinTransaction && !commitTransaction()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to commit database after removal"));
        return QContactManager::UnspecifiedError;
    }

    return error;
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

template<typename T>
QContactDetail::DetailType detailType()
{
    return T::Type;
}

QContactDetail::DetailType detailType(const QContactDetail &detail)
{
    return detail.type();
}

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

/*
 Steps:
 - begin transaction
 - apply deletions for contacts and details according to changeFlags & !unhandledChangeFlags
   i.e. delete ONLY IF changeFlags has isDeleted AND unhandledChangeFlags does NOT have isDeleted
        to ensure that we report the deletion properly during the next fetch.
 - for every Contact in the list:
   set changeFlags = unhandledChangeFlags, unhandledChangeFlags = 0
 - for every Detail in each contact:
   set changeFlags = unhandledChangeFlags, unhandledChangeFlags = 0
 - end transaction.
*/
QContactManager::Error ContactWriter::clearChangeFlags(const QList<QContactId> &contactIds, bool withinTransaction)
{
    QMutexLocker locker(withinTransaction ? nullptr : m_database.accessMutex());

    QVariantList boundIds;
    for (const QContactId &id : contactIds) {
        boundIds.append(ContactId::databaseId(id));
    }

    if (!withinTransaction && !beginTransaction()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to begin database transaction while clearing contact change flags"));
        return QContactManager::UnspecifiedError;
    }

    // first, purge any deleted contacts specified in the list.
    QContactManager::Error error = removeContacts(boundIds, true);
    if (error != QContactManager::NoError) {
        rollbackTransaction();
        return error;
    }

    // second, purge any deleted details of contacts specified in the list.
    error = removeDetails(boundIds, true);
    if (error != QContactManager::NoError) {
        if (!withinTransaction) {
            rollbackTransaction();
        }
        return error;
    }

    // do it in batches, otherwise the query can fail due to too many bound values.
    for (int i = 0; i < boundIds.size(); i += 167) {
        const QVariantList cids = boundIds.mid(i, qMin(boundIds.size() - i, 167));

        // third, clear any added/modified change flags for contacts specified in the list.
        const QString statement(QStringLiteral("UPDATE Contacts SET changeFlags = unhandledChangeFlags, unhandledChangeFlags = 0 WHERE contactId = :contactId"));
        ContactsDatabase::Query query(m_database.prepare(statement));
        query.bindValue(QStringLiteral(":contactId"), cids);
        if (!ContactsDatabase::executeBatch(query)) {
            query.reportError("Failed to clear contact change flags");
            if (!withinTransaction) {
                rollbackTransaction();
            }
            return QContactManager::UnspecifiedError;
        }

        // fourth, clear any added/modified change flags for details of contacts specified in the list.
        const QString detstatement(QStringLiteral("UPDATE Details SET changeFlags = unhandledChangeFlags, unhandledChangeFlags = 0 WHERE contactId = :contactId"));
        ContactsDatabase::Query detquery(m_database.prepare(detstatement));
        detquery.bindValue(QStringLiteral(":contactId"), cids);
        if (!ContactsDatabase::executeBatch(detquery)) {
            detquery.reportError("Failed to clear detail change flags");
            if (!withinTransaction) {
                rollbackTransaction();
            }
            return QContactManager::UnspecifiedError;
        }
    }

    if (!withinTransaction && !commitTransaction()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to commit database after clearing contact change flags"));
        return QContactManager::UnspecifiedError;
    }

    return QContactManager::NoError;
}

/*
 Steps:
 - begin transaction
 - set Collection.recordUnhandledChangeFlags = false
 - apply deletion to the collection according to its changeFlags
 - apply deletions for contacts and details according to changeFlags & !unhandledChangeFlags
   i.e. delete ONLY IF changeFlags has isDeleted AND unhandledChangeFlags does NOT have isDeleted
        to ensure that we report the deletion properly during the next fetch.
 - for every Contact in the collection:
   set changeFlags = unhandledChangeFlags, unhandledChangeFlags = 0
 - for every Detail in the contact:
   set changeFlags = unhandledChangeFlags, unhandledChangeFlags = 0
 - end transaction.
 */
QContactManager::Error ContactWriter::clearChangeFlags(const QContactCollectionId &collectionId, bool withinTransaction)
{
    QMutexLocker locker(withinTransaction ? nullptr : m_database.accessMutex());

    if (!withinTransaction && !beginTransaction()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to begin database transaction while clearing collection change flags"));
        return QContactManager::UnspecifiedError;
    }

    const QString statement(QStringLiteral("SELECT contactId FROM Contacts WHERE collectionId = :collectionId"));
    ContactsDatabase::Query query(m_database.prepare(statement));
    query.bindValue(QStringLiteral(":collectionId"), ContactCollectionId::databaseId(collectionId));

    QContactManager::Error err = QContactManager::NoError;
    QList<QContactId> contactIds;
    if (!ContactsDatabase::execute(query)) {
        query.reportError("Failed to retrieve contacts in collection while clearing change flags");
        err = QContactManager::UnspecifiedError;
    } else while (query.next()) {
        contactIds.append(ContactId::apiId(query.value<quint32>(0), m_managerUri));
    }

    if (contactIds.size()) {
        err = clearChangeFlags(contactIds, true);
    }

    if (err == QContactManager::NoError) {
        err = removeCollection(collectionId, true /* only purge if delete flag is set */);
    }

    if (err == QContactManager::NoError) {
        const QString clearFlagsStatement(QStringLiteral(
                " UPDATE Collections SET"
                  " changeFlags = 0"
                " WHERE collectionId = :collectionId"
        ));
        ContactsDatabase::Query clearQuery(m_database.prepare(clearFlagsStatement));
        clearQuery.bindValue(QStringLiteral(":collectionId"), ContactCollectionId::databaseId(collectionId));

        if (!ContactsDatabase::execute(clearQuery)) {
            clearQuery.reportError("Failed to clear collection change flags");
            err = QContactManager::UnspecifiedError;
        }
    }

    if (err != QContactManager::NoError && !withinTransaction) {
        rollbackTransaction();
    } else if (err == QContactManager::NoError && !withinTransaction && !commitTransaction()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to commit database after clearing contact change flags"));
        err = QContactManager::UnspecifiedError;
    }

    return err;
}

/*
 This method returns collections associated with the specified accountId or applicationName which have been added, modified, or deleted.
 For the purposes of this method, a collection is only considered modified if its metadata has changed.
 Changes to the content of the collection (i.e. contact additions, modifications, or deletions) are ignored
 for the purposes of this method.

 Fetch all collections whose COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID value is the specified accountId,
 and whose COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME value is the specified applicationName.
 If the specified accountId value is zero, it matches on applicationName only, and vice versa.
 Append any collection which has ChangeFlags::IsDeleted to deletedCollections.
 Append any collection which has ChangeFlags::IsAdded (and not IsDeleted) to addedCollections.
 Append any collection which has ChangeFlags::IsModified (and not IsAdded or IsDeleted) to modifiedCollections.
*/
QContactManager::Error ContactWriter::fetchCollectionChanges(
        int accountId,
        const QString &applicationName,
        QList<QContactCollection> *addedCollections,
        QList<QContactCollection> *modifiedCollections,
        QList<QContactCollection> *deletedCollections,
        QList<QContactCollection> *unmodifiedCollections)
{
    return m_reader->fetchCollections(accountId, applicationName, addedCollections, modifiedCollections, deletedCollections, unmodifiedCollections);
}

/*
 Steps:
 - begin transaction.
 - set Collection.recordUnhandledChangeFlags = true
   any subsequent "normal" updates to a contact in the collection will result in both changeFlags and unhandledChangeFlags being set for it.
   we will report these "unhandled" changes during the next sync cycle.
 - clear Contact.unhandledChangeFlags, and all Detail.unhandledChangeFlags
   it seems counter-intuitive, but it's basically saying: the previous "unhandled" changes have now been handled as a result of the fetch.
   doing this prevents us from reporting the SAME CHANGE TWICE, in subsequent fetch calls.
 - retrieve all Contact + Detail data, including the changeFlags field.
 - end transaction.
 - return the Contact+Detail info to caller via the outparams.
*/
QContactManager::Error ContactWriter::fetchContactChanges(
        const QContactCollectionId &collectionId,
        QList<QContact> *addedContacts,
        QList<QContact> *modifiedContacts,
        QList<QContact> *deletedContacts,
        QList<QContact> *unmodifiedContacts)
{
    QContactManager::Error error = QContactManager::NoError;
    const quint32 dbColId = ContactCollectionId::databaseId(collectionId);

    QMutexLocker locker(m_database.accessMutex());

    if (!beginTransaction()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to begin database transaction while fetching contact changes"));
        error = QContactManager::UnspecifiedError;
    }

    if (error == QContactManager::NoError) {
        // set Collection.recordUnhandledChangeFlags = true
        const QString setRecordUnhandledChangeFlags(QStringLiteral(
            " UPDATE Collections SET"
            "  recordUnhandledChangeFlags = 1"
            " WHERE collectionId = :collectionId;"
        ));

        ContactsDatabase::Query query(m_database.prepare(setRecordUnhandledChangeFlags));
        query.bindValue(":collectionId", dbColId);

        if (!ContactsDatabase::execute(query)) {
            query.reportError("Failed to set collection.recordUnhandledChangeFlags while fetching contact changes");
            error = QContactManager::UnspecifiedError;
        }
    }

    if (error == QContactManager::NoError) {
        // clear Contact.unhandledChangeFlags
        const QString clearUnhandledChangeFlags(QStringLiteral(
            " UPDATE Contacts SET"
            "  unhandledChangeFlags = 0"
            " WHERE collectionId = :collectionId"
        ));

        ContactsDatabase::Query query(m_database.prepare(clearUnhandledChangeFlags));
        query.bindValue(":collectionId", dbColId);

        if (!ContactsDatabase::execute(query)) {
            query.reportError("Failed to clear contact.unhandledChangeFlags while fetching contact changes");
            error = QContactManager::UnspecifiedError;
        }
    }

    if (error == QContactManager::NoError) {
        // clear Detail.unhandledChangeFlags
        const QString clearUnhandledChangeFlags(QStringLiteral(
            " UPDATE Details SET"
            "  unhandledChangeFlags = 0"
            " WHERE contactId IN ("
            "  SELECT ContactId"
            "  FROM Contacts"
            "  WHERE collectionId = :collectionId"
            " )"
        ));

        ContactsDatabase::Query query(m_database.prepare(clearUnhandledChangeFlags));
        query.bindValue(":collectionId", dbColId);

        if (!ContactsDatabase::execute(query)) {
            query.reportError("Failed to clear contact.unhandledChangeFlags while fetching contact changes");
            error = QContactManager::UnspecifiedError;
        }
    }

    if (error == QContactManager::NoError) {
        // retrieve all contact+detail data.
        // this fetch should NOT strip out the added/modified/deleted info.
        error = m_reader->fetchContacts(collectionId, addedContacts, modifiedContacts, deletedContacts, unmodifiedContacts);
        if (error != QContactManager::NoError) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to fetch contact changes for collection %1").arg(dbColId));
        }
    }

    if (error != QContactManager::NoError) {
        rollbackTransaction();
    } else if (!commitTransaction()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to commit database after sync contacts fetch"));
        error = QContactManager::UnspecifiedError;
    }

    return error;
}

/*
 Steps:
 - begin transaction.
 - read the current db state of the contact.  if it's deleted, skip / don't apply.
 - the input contact should contain change flags to specify which details should be added/modified/removed.
   apply changes as best as possible, but "keep" the unhandled changes.
   resolve conflicts according to the conflictResolutionPolicy.
 - if clearChangeFlags is true, call clearChangeFlags(collectionId).
 - end transaction.
*/
QContactManager::Error ContactWriter::storeChanges(
        QHash<QContactCollection*, QList<QContact> * /* added contacts */> *addedCollections,
        QHash<QContactCollection*, QList<QContact> * /* added/modified/deleted contacts */> *modifiedCollections,
        const QList<QContactCollectionId> &deletedCollections,
        QtContactsSqliteExtensions::ContactManagerEngine::ConflictResolutionPolicy conflictResolutionPolicy,
        bool clearChangeFlags)
{
    Q_UNUSED(conflictResolutionPolicy); // TODO.

    QMutexLocker locker(m_database.accessMutex());

    if (!beginTransaction()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to begin database transaction for store changes"));
        return QContactManager::UnspecifiedError;
    }

    QContactManager::Error error = QContactManager::NoError;
    QList<QContactCollectionId> touchedCollections;

    // handle additions
    if (addedCollections) {
        QHash<QContactCollection*, QList<QContact> *>::iterator ait = addedCollections->begin(), aend = addedCollections->end();
        for ( ; ait != aend; ++ait) {
            QContactCollection *collection = ait.key();

            if (!collection->id().isNull()) {
                QTCONTACTS_SQLITE_DEBUG(QString::fromLatin1("Invalid attempt to add an already-existing collection %1 with id %2 within store changes")
                    .arg(collection->metaData(QContactCollection::KeyName).toString(), QString::fromLatin1(collection->id().localId())));
                error = QContactManager::BadArgumentError;
                break;
            }

            QList<QContactCollection> collections;
            collections.append(*collection);
            error = save(&collections, nullptr, true, true);
            if (error != QContactManager::NoError) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to save added collection %1 within store changes")
                    .arg(collection->metaData(QContactCollection::KeyName).toString()));
                break;
            }

            *collection = collections.first();
            touchedCollections.append(collection->id());

            QList<QContact> *addedContacts = ait.value();
            QList<QContact>::iterator cit = addedContacts->begin(), cend = addedContacts->end();
            for ( ; cit != cend; ++cit) {
                cit->setCollectionId(collection->id());
            }

            error = save(addedContacts, QList<QContactDetail::DetailType>(), nullptr, nullptr, true, false, true);
            if (error != QContactManager::NoError) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to save added contacts for added collection %1 within store changes")
                    .arg(collection->metaData(QContactCollection::KeyName).toString()));
                break;
            }
        }
    }

    // handle modifications
    if (error == QContactManager::NoError && modifiedCollections) {
        QHash<QContactCollection*, QList<QContact> *>::iterator mit = modifiedCollections->begin(), mend = modifiedCollections->end();
        for ( ; mit != mend; ++mit) {
            QContactCollection *collection = mit.key();

            if (collection->id().isNull()) {
                QTCONTACTS_SQLITE_DEBUG(QString::fromLatin1("Invalid attempt to modify a non-added collection %1 within store changes")
                    .arg(collection->metaData(QContactCollection::KeyName).toString()));
                error = QContactManager::BadArgumentError;
                break;
            }

            touchedCollections.append(collection->id());

            QList<QContactCollection> collections;
            collections.append(*collection);
            error = save(&collections, nullptr, true, true);
            if (error != QContactManager::NoError) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to save modified collection %1 within store changes")
                    .arg(QString::fromLatin1(collection->id().localId())));
                break;
            }

            *collection = collections.first();
            QList<QContact> *contacts = mit.value();
            QList<QContact> addedContacts;
            QList<QContact> modifiedContacts;
            QList<QContact> deletedContacts;
            QList<QContactId> deletedContactIds;

            // for every modified contact, determine the change type.
            {
                QList<QContact>::iterator mcit = contacts->begin(), mcend = contacts->end();
                for ( ; mcit != mcend; ++mcit) {
                    const QContactStatusFlags &flags = mcit->detail<QContactStatusFlags>();
                    if (flags.testFlag(QContactStatusFlags::IsDeleted)) {
                        deletedContacts.append(*mcit);
                        deletedContactIds.append(mcit->id());
                    } else if (flags.testFlag(QContactStatusFlags::IsAdded)) {
                        addedContacts.append(*mcit);
                    } else if (flags.testFlag(QContactStatusFlags::IsModified)) {
                        modifiedContacts.append(*mcit);
                    } else {
                        QTCONTACTS_SQLITE_DEBUG(QString::fromLatin1("Ignoring unchanged contact %1 within modified collection %2 within store changes")
                            .arg(QString::fromLatin1(mcit->id().localId()))
                            .arg(QString::fromLatin1(collection->id().localId())));
                    }
                }
            }

            // now apply the changes
            // first, contact additions
            if (addedContacts.size()) {
                QList<QContact>::iterator cit = addedContacts.begin(), cend = addedContacts.end();
                for ( ; cit != cend; ++cit) {
                    cit->setCollectionId(collection->id());
                }
                error = save(&addedContacts, QList<QContactDetail::DetailType>(), nullptr, nullptr, true, false, true);
                if (error != QContactManager::NoError) {
                    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to save added contacts for modified collection %1 within store changes")
                        .arg(QString::fromLatin1(collection->id().localId())));
                    break;
                }
            }

            // then contact modifications
            if (modifiedContacts.size()) {
                QList<QContact>::iterator cit = modifiedContacts.begin(), cend = modifiedContacts.end();
                for ( ; cit != cend; ++cit) {
                    cit->setCollectionId(collection->id());
                }
                error = save(&modifiedContacts, QList<QContactDetail::DetailType>(), nullptr, nullptr, true, false, true);
                if (error != QContactManager::NoError) {
                    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to save added contacts for modified collection %1 within store changes")
                        .arg(QString::fromLatin1(collection->id().localId())));
                    break;
                }
            }

            // finally contact deletions
            if (deletedContactIds.size()) {
                error = remove(deletedContactIds, nullptr, true, true);
                if (error != QContactManager::NoError) {
                    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to delete deleted contacts for modified collection %1 within store changes")
                        .arg(QString::fromLatin1(collection->id().localId())));
                    break;
                }
            }

            // update the input parameter with the potentially modified values.
            // this is important primarily for additions, which get updated ids.
            contacts->clear();
            contacts->append(addedContacts);
            contacts->append(modifiedContacts);
            contacts->append(deletedContacts);
        }
    }

    // handle deletions
    if (error == QContactManager::NoError && deletedCollections.size()) {
        error = remove(deletedCollections, nullptr, true, true);
        touchedCollections.append(deletedCollections);
    }

    // clear change flags (including purging items marked for deletion) if required.
    if (clearChangeFlags) {
        for (const QContactCollectionId &touchedCollection : touchedCollections) {
            error = this->clearChangeFlags(touchedCollection, true);
            if (error != QContactManager::NoError) {
                break;
            }
        }
    }

    if (error != QContactManager::NoError) {
        rollbackTransaction();
    } else if (!commitTransaction()) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to commit database after store changes"));
        error = QContactManager::UnspecifiedError;
    }

    return error;
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
    const QString bindString(QStringLiteral("(?,?,?)"));

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

    QString statement(QStringLiteral("INSERT OR REPLACE INTO OOB (name, value, compressed) VALUES %1").arg(tuples.join(",")));

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

    QString statement(QStringLiteral("DELETE FROM OOB WHERE name "));

    if (keys.isEmpty()) {
        statement.append(QStringLiteral("LIKE '%1%%'").arg(scope));
    } else {
        const QChar colon(QChar::fromLatin1(':'));
        QString keyList;

        foreach (const QString &key, keys) {
            keyNames.append(scope + colon + key);
            keyList.append(keyList.isEmpty() ? QStringLiteral("?") : QStringLiteral(",?"));
        }

        statement.append(QStringLiteral("IN (%1)").arg(keyList));
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

    rv.insert(QContactDetail::ContextHome, QStringLiteral("Home"));
    rv.insert(QContactDetail::ContextWork, QStringLiteral("Work"));
    rv.insert(QContactDetail::ContextOther, QStringLiteral("Other"));

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
    static const QString separator = QStringLiteral(";");

    QStringList contexts;
    foreach (int context, detail.contexts()) {
        contexts.append(contextString(context));
    }
    return QVariant(contexts.join(separator));
}

quint32 writeCommonDetails(ContactsDatabase &db, quint32 contactId, quint32 detailId, const QContactDetail &detail,
                           bool syncable, bool wasLocal, bool aggregateContact, bool recordUnhandledChangeFlags,
                           const QString &typeName, QContactManager::Error *error)
{
    const QString statement(detailId == 0
        ? QStringLiteral(
            " INSERT INTO Details ("
            "  contactId,"
            "  detail,"
            "  detailUri,"
            "  linkedDetailUris,"
            "  contexts,"
            "  accessConstraints,"
            "  provenance,"
            "  modifiable,"
            "  nonexportable,"
            "  changeFlags,"
            "  unhandledChangeFlags)"
            " VALUES ("
            "  :contactId,"
            "  :detail,"
            "  :detailUri,"
            "  :linkedDetailUris,"
            "  :contexts,"
            "  :accessConstraints,"
            "  :provenance,"
            "  :modifiable,"
            "  :nonexportable,"
            "  %1,"
            "  %2)").arg(aggregateContact ? QStringLiteral("0") : QStringLiteral("1"))  // ChangeFlags::IsAdded
                    .arg((aggregateContact || !recordUnhandledChangeFlags) ? QStringLiteral("0") : QStringLiteral("1"))
        : QStringLiteral(
            " UPDATE Details SET"
            "  detail = :detail,"
            "  detailUri = :detailUri,"
            "  linkedDetailUris = :linkedDetailUris,"
            "  contexts = :contexts,"
            "  accessConstraints = :accessConstraints,"
            "  provenance = :provenance,"
            "  modifiable = :modifiable,"
            "  nonexportable = :nonexportable"
            " %1 %2"
            " WHERE contactId = :contactId AND detailId = :detailId")
                .arg(aggregateContact ? QString() : QStringLiteral(", ChangeFlags = ChangeFlags | 2")) // ChangeFlags::IsModified
                .arg((aggregateContact || !recordUnhandledChangeFlags) ? QString() : QStringLiteral(", UnhandledChangeFlags = UnhandledChangeFlags | 2")));

    ContactsDatabase::Query query(db.prepare(statement));

    const QVariant detailUri = detailValue(detail, QContactDetail::FieldDetailUri);
    const QVariant linkedDetailUris = QVariant(detail.linkedDetailUris().join(QStringLiteral(";")));
    const QVariant contexts = detailContexts(detail);
    const QVariant accessConstraints = static_cast<int>(detail.accessConstraints());
    const QVariant provenance = aggregateContact ? detailValue(detail, QContactDetail::FieldProvenance) : QVariant();
    const QVariant modifiable = wasLocal ? true : (syncable && detail.hasValue(QContactDetail__FieldModifiable)
                                                   ? detailValue(detail, QContactDetail__FieldModifiable)
                                                   : QVariant());
    const QVariant nonexportable = detailValue(detail, QContactDetail__FieldNonexportable);

    if (detailId > 0) {
        query.bindValue(":detailId", detailId);
    }

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

    return detailId == 0 ? query.lastInsertId().value<quint32>() : detailId;
}

template <typename T> quint32 ContactWriter::writeCommonDetails(
            quint32 contactId, quint32 detailId, const T &detail,
            bool syncable, bool wasLocal, bool aggregateContact, bool recordUnhandledChangeFlags,
            QContactManager::Error *error)
{
    return ::writeCommonDetails(
            m_database, contactId, detailId, detail,
            syncable, wasLocal, aggregateContact, recordUnhandledChangeFlags,
            detailTypeName<T>(), error);
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

bool deleteDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, const QString &typeName, bool recordUnhandledChangeFlags, QContactManager::Error *error)
{
    const QString deleteDetailStatement(QStringLiteral(
            "UPDATE Details SET"
            " ChangeFlags = ChangeFlags | 4" // ChangeFlags::IsDeleted
            " %1"
            " WHERE detailId = :detailId"
            " AND contactId = :contactId").arg(recordUnhandledChangeFlags
                    ? QStringLiteral(", unhandledChangeFlags = unhandledChangeFlags | 4")
                    : QString()));

    ContactsDatabase::Query query(db.prepare(deleteDetailStatement));
    query.bindValue(":contactId", contactId);
    query.bindValue(":detailId", detailId);

    if (!ContactsDatabase::execute(query)) {
        query.reportError(QStringLiteral("Failed to delete existing detail of type %1 with id %2 for contact %3").arg(typeName).arg(detailId).arg(contactId));
        *error = QContactManager::UnspecifiedError;
        return false;
    }

    return true;
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

template<> struct RemoveStatement<QContactDisplayLabel> { static const QString statement; };
const QString RemoveStatement<QContactDisplayLabel>::statement(QStringLiteral("DELETE FROM DisplayLabels WHERE contactId = :contactId"));

template<> struct RemoveStatement<QContactEmailAddress> { static const QString statement; };
const QString RemoveStatement<QContactEmailAddress>::statement(QStringLiteral("DELETE FROM EmailAddresses WHERE contactId = :contactId"));

template<> struct RemoveStatement<QContactFamily> { static const QString statement; };
const QString RemoveStatement<QContactFamily>::statement(QStringLiteral("DELETE FROM Families WHERE contactId = :contactId"));

template<> struct RemoveStatement<QContactFavorite> { static const QString statement; };
const QString RemoveStatement<QContactFavorite>::statement(QStringLiteral("DELETE FROM Favorites WHERE contactId = :contactId"));

template<> struct RemoveStatement<QContactGender> { static const QString statement; };
const QString RemoveStatement<QContactGender>::statement(QStringLiteral("DELETE FROM Genders WHERE contactId = :contactId"));

template<> struct RemoveStatement<QContactGeoLocation> { static const QString statement; };
const QString RemoveStatement<QContactGeoLocation>::statement(QStringLiteral("DELETE FROM GeoLocations WHERE contactId = :contactId"));

template<> struct RemoveStatement<QContactGlobalPresence> { static const QString statement; };
const QString RemoveStatement<QContactGlobalPresence>::statement(QStringLiteral("DELETE FROM GlobalPresences WHERE contactId = :contactId"));

template<> struct RemoveStatement<QContactGuid> { static const QString statement; };
const QString RemoveStatement<QContactGuid>::statement(QStringLiteral("DELETE FROM Guids WHERE contactId = :contactId"));

template<> struct RemoveStatement<QContactHobby> { static const QString statement; };
const QString RemoveStatement<QContactHobby>::statement(QStringLiteral("DELETE FROM Hobbies WHERE contactId = :contactId"));

template<> struct RemoveStatement<QContactName> { static const QString statement; };
const QString RemoveStatement<QContactName>::statement(QStringLiteral("DELETE FROM Names WHERE contactId = :contactId"));

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

template<> struct RemoveStatement<QContactSyncTarget> { static const QString statement; };
const QString RemoveStatement<QContactSyncTarget>::statement(QStringLiteral("DELETE FROM SyncTargets WHERE contactId = :contactId"));

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
        query.reportError(QStringLiteral("Failed to remove existing details of type %1 for contact %2").arg(typeName).arg(contactId));
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

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, bool update, const QContactAddress &detail)
{
    const QString statement(update
        ? QStringLiteral(
            " UPDATE Addresses SET"
            "  street = :street,"
            "  postOfficeBox = :postOfficeBox,"
            "  region = :region,"
            "  locality = :locality,"
            "  postCode = :postCode,"
            "  country = :country,"
            "  subTypes = :subTypes"
            " WHERE detailId = :detailId"
            " AND contactId = :contactId")
        : QStringLiteral(
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
            "  :subTypes)"));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactAddress T;
    query.bindValue(":detailId", detailId);
    query.bindValue(":contactId", contactId);
    query.bindValue(":street", detail.value<QString>(T::FieldStreet).trimmed());
    query.bindValue(":postOfficeBox", detail.value<QString>(T::FieldPostOfficeBox).trimmed());
    query.bindValue(":region", detail.value<QString>(T::FieldRegion).trimmed());
    query.bindValue(":locality", detail.value<QString>(T::FieldLocality).trimmed());
    query.bindValue(":postCode", detail.value<QString>(T::FieldPostcode).trimmed());
    query.bindValue(":country", detail.value<QString>(T::FieldCountry).trimmed());
    query.bindValue(":subTypes", subTypeList(detail.subTypes()).join(QStringLiteral(";")));
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, bool update, const QContactAnniversary &detail)
{
    const QString statement(update
        ? QStringLiteral(
            " UPDATE Anniversaries SET"
            "  originalDateTime = :originalDateTime,"
            "  calendarId = :calendarId,"
            "  subType = :subType,"
            "  event = :event"
            " WHERE detailId = :detailId"
            " AND contactId = :contactId")
        : QStringLiteral(
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
    query.bindValue(":detailId", detailId);
    query.bindValue(":contactId", contactId);
    query.bindValue(":originalDateTime", detailValue(detail, T::FieldOriginalDate));
    query.bindValue(":calendarId", detailValue(detail, T::FieldCalendarId));
    query.bindValue(":subType", detail.hasValue(T::FieldSubType) ? QString::number(detail.subType()) : QString());
    query.bindValue(":event", detail.value<QString>(T::FieldEvent).trimmed());
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, bool update, const QContactAvatar &detail)
{
    const QString statement(update
        ? QStringLiteral(
            " UPDATE Avatars SET"
            "  imageUrl = :imageUrl,"
            "  videoUrl = :videoUrl,"
            "  avatarMetadata = :avatarMetadata"
            " WHERE detailId = :detailId"
            " AND contactId = :contactId")
        : QStringLiteral(
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
            "  :avatarMetadata)"));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactAvatar T;
    query.bindValue(":detailId", detailId);
    query.bindValue(":contactId", contactId);
    query.bindValue(":imageUrl", detail.value<QString>(T::FieldImageUrl).trimmed());
    query.bindValue(":videoUrl", detail.value<QString>(T::FieldVideoUrl).trimmed());
    query.bindValue(":avatarMetadata", detailValue(detail, QContactAvatar::FieldMetaData));
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, bool update, const QContactBirthday &detail)
{
    const QString statement(update
        ? QStringLiteral(
            " UPDATE Birthdays SET"
            "  birthday = :birthday,"
            "  calendarId = :calendarId"
            " WHERE detailId = :detailId"
            " AND contactId = :contactId")
        : QStringLiteral(
            " INSERT INTO Birthdays ("
            "  detailId,"
            "  contactId,"
            "  birthday,"
            "  calendarId)"
            " VALUES ("
            "  :detailId,"
            "  :contactId,"
            "  :birthday,"
            "  :calendarId)"));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactBirthday T;
    query.bindValue(":detailId", detailId);
    query.bindValue(":contactId", contactId);
    query.bindValue(":birthday", detailValue(detail, T::FieldBirthday));
    query.bindValue(":calendarId", detailValue(detail, T::FieldCalendarId));
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, bool update, const QContactDisplayLabel &detail)
{
    const QString statement(update
        ? QStringLiteral(
            " UPDATE DisplayLabels SET"
            "  displayLabel = :displayLabel,"
            "  displayLabelGroup = :displayLabelGroup,"
            "  displayLabelGroupSortOrder = :displayLabelGroupSortOrder"
            " WHERE detailId = :detailId"
            " AND contactId = :contactId")
        : QStringLiteral(
            " INSERT INTO DisplayLabels ("
            "  detailId,"
            "  contactId,"
            "  displayLabel,"
            "  displayLabelGroup,"
            "  displayLabelGroupSortOrder)"
            " VALUES ("
            "  :detailId,"
            "  :contactId,"
            "  :displayLabel,"
            "  :displayLabelGroup,"
            "  :displayLabelGroupSortOrder)"));

    ContactsDatabase::Query query(db.prepare(statement));

    query.bindValue(":detailId", detailId);
    query.bindValue(":contactId", contactId);
    query.bindValue(":displayLabel", detail.label());
    query.bindValue(":displayLabelGroup", detail.value<QString>(QContactDisplayLabel__FieldLabelGroup));
    query.bindValue(":displayLabelGroupSortOrder", detail.value<int>(QContactDisplayLabel__FieldLabelGroupSortOrder));
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, bool update, const QContactEmailAddress &detail)
{
    const QString statement(update
        ? QStringLiteral(
            " UPDATE EmailAddresses SET"
            "  emailAddress = :emailAddress,"
            "  lowerEmailAddress = :lowerEmailAddress"
            " WHERE detailId = :detailId"
            " AND contactId = :contactId")
        : QStringLiteral(
            " INSERT INTO EmailAddresses ("
            "  detailId,"
            "  contactId,"
            "  emailAddress,"
            "  lowerEmailAddress)"
            " VALUES ("
            "  :detailId,"
            "  :contactId,"
            "  :emailAddress,"
            "  :lowerEmailAddress)"));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactEmailAddress T;
    const QString address(detail.value<QString>(T::FieldEmailAddress).trimmed());
    query.bindValue(":detailId", detailId);
    query.bindValue(":contactId", contactId);
    query.bindValue(":emailAddress", address);
    query.bindValue(":lowerEmailAddress", address.toLower());
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, bool update, const QContactFamily &detail)
{
    const QString statement(update
        ? QStringLiteral(
            " UPDATE Families SET"
            "  spouse = :spouse,"
            "  children = :children"
            " WHERE detailId = :detailId"
            " AND contactId = :contactId")
        : QStringLiteral(
            " INSERT INTO Families ("
            "  detailId,"
            "  contactId,"
            "  spouse,"
            "  children)"
            " VALUES ("
            "  :detailId,"
            "  :contactId,"
            "  :spouse,"
            "  :children)"));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactFamily T;
    query.bindValue(":detailId", detailId);
    query.bindValue(":contactId", contactId);
    query.bindValue(":spouse", detail.value<QString>(T::FieldSpouse).trimmed());
    query.bindValue(":children", detail.value<QStringList>(T::FieldChildren).join(QStringLiteral(";")));
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, bool update, const QContactFavorite &detail)
{
    const QString statement(update
        ? QStringLiteral(
            " UPDATE Favorites SET"
            "  isFavorite = :isFavorite"
            " WHERE detailId = :detailId"
            " AND contactId = :contactId")
        : QStringLiteral(
            " INSERT INTO Favorites ("
            "  detailId,"
            "  contactId,"
            "  isFavorite)"
            " VALUES ("
            "  :detailId,"
            "  :contactId,"
            "  :isFavorite)"));

    ContactsDatabase::Query query(db.prepare(statement));

    query.bindValue(":detailId", detailId);
    query.bindValue(":contactId", contactId);
    query.bindValue(":isFavorite", detail.isFavorite());
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, bool update, const QContactGender &detail)
{
    const QString statement(update
        ? QStringLiteral(
            " UPDATE Genders SET"
            "  gender = :gender"
            " WHERE detailId = :detailId"
            " AND contactId = :contactId")
        : QStringLiteral(
            " INSERT INTO Genders ("
            "  detailId,"
            "  contactId,"
            "  gender)"
            " VALUES ("
            "  :detailId,"
            "  :contactId,"
            "  :gender)"));

    ContactsDatabase::Query query(db.prepare(statement));

    query.bindValue(":detailId", detailId);
    query.bindValue(":contactId", contactId);
    query.bindValue(":gender", QString::number(static_cast<int>(detail.gender())));
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, bool update, const QContactGeoLocation &detail)
{
    const QString statement(update
        ? QStringLiteral(
            " UPDATE GeoLocations SET"
            "  label = :label,"
            "  latitude = :latitude,"
            "  longitude = :longitude,"
            "  accuracy = :accuracy,"
            "  altitude = :altitude,"
            "  altitudeAccuracy = :altitudeAccuracy,"
            "  heading = :heading,"
            "  speed = :speed,"
            "  timestamp = :timestamp)"
            " WHERE detailId = :detailId"
            " AND contactId = :contactId")
        : QStringLiteral(
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
            "  :timestamp)"));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactGeoLocation T;
    query.bindValue(":detailId", detailId);
    query.bindValue(":contactId", contactId);
    query.bindValue(":label", detail.value<QString>(T::FieldLabel).trimmed());
    query.bindValue(":latitude", detail.value<double>(T::FieldLatitude));
    query.bindValue(":longitude", detail.value<double>(T::FieldLongitude));
    query.bindValue(":accuracy", detail.value<double>(T::FieldAccuracy));
    query.bindValue(":altitude", detail.value<double>(T::FieldAltitude));
    query.bindValue(":altitudeAccuracy", detail.value<double>(T::FieldAltitudeAccuracy));
    query.bindValue(":heading", detail.value<double>(T::FieldHeading));
    query.bindValue(":speed", detail.value<double>(T::FieldSpeed));
    query.bindValue(":timestamp", ContactsDatabase::dateTimeString(detail.value<QDateTime>(T::FieldTimestamp).toUTC()));
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, bool update, const QContactGlobalPresence &detail)
{
    const QString statement(update
        ? QStringLiteral(
            " UPDATE GlobalPresences SET"
            "  presenceState = :presenceState,"
            "  timestamp = :timestamp,"
            "  nickname = :nickname,"
            "  customMessage = :customMessage,"
            "  presenceStateText = :presenceStateText,"
            "  presenceStateImageUrl = :presenceStateImageUrl"
            " WHERE detailId = :detailId"
            " AND contactId = :contactId")
        : QStringLiteral(
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
            "  :presenceStateImageUrl)"));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactGlobalPresence T;
    query.bindValue(":detailId", detailId);
    query.bindValue(":contactId", contactId);
    query.bindValue(":presenceState", detailValue(detail, T::FieldPresenceState));
    query.bindValue(":timestamp", ContactsDatabase::dateTimeString(detail.value<QDateTime>(T::FieldTimestamp).toUTC()));
    query.bindValue(":nickname", detail.value<QString>(T::FieldNickname).trimmed());
    query.bindValue(":customMessage", detail.value<QString>(T::FieldCustomMessage).trimmed());
    query.bindValue(":presenceStateText", detail.value<QString>(T::FieldPresenceStateText).trimmed());
    query.bindValue(":presenceStateImageUrl", detail.value<QString>(T::FieldPresenceStateImageUrl).trimmed());
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, bool update, const QContactGuid &detail)
{
    const QString statement(update
        ? QStringLiteral(
            " UPDATE Guids SET"
            "  guid = :guid"
            " WHERE detailId = :detailId"
            " AND contactId = :contactId")
        : QStringLiteral(
            " INSERT INTO Guids ("
            "  detailId,"
            "  contactId,"
            "  guid)"
            " VALUES ("
            "  :detailId,"
            "  :contactId,"
            "  :guid)"));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactGuid T;
    query.bindValue(":detailId", detailId);
    query.bindValue(":contactId", contactId);
    query.bindValue(":guid", detailValue(detail, T::FieldGuid));
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, bool update, const QContactHobby &detail)
{
    const QString statement(update
        ? QStringLiteral(
            " UPDATE Hobbies SET"
            "  hobby = :hobby"
            " WHERE detailId = :detailId"
            " AND contactId = :contactId")
        : QStringLiteral(
            " INSERT INTO Hobbies ("
            "  detailId,"
            "  contactId,"
            "  hobby)"
            " VALUES ("
            "  :detailId,"
            "  :contactId,"
            "  :hobby)"));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactHobby T;
    query.bindValue(":detailId", detailId);
    query.bindValue(":contactId", contactId);
    query.bindValue(":hobby", detailValue(detail, T::FieldHobby));
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, bool update, const QContactName &detail)
{
    const QString statement(update
        ? QStringLiteral(
            " UPDATE Names SET"
            "  firstName = :firstName,"
            "  lowerFirstName = :lowerFirstName,"
            "  lastName = :lastName,"
            "  lowerLastName = :lowerLastName,"
            "  middleName = :middleName,"
            "  prefix = :prefix,"
            "  suffix = :suffix,"
            "  customLabel = :customLabel"
            " WHERE detailId = :detailId"
            " AND contactId = :contactId")
        : QStringLiteral(
            " INSERT INTO Names ("
            "  detailId,"
            "  contactId,"
            "  firstName,"
            "  lowerFirstName,"
            "  lastName,"
            "  lowerLastName,"
            "  middleName,"
            "  prefix,"
            "  suffix,"
            "  customLabel)"
            " VALUES ("
            "  :detailId,"
            "  :contactId,"
            "  :firstName,"
            "  :lowerFirstName,"
            "  :lastName,"
            "  :lowerLastName,"
            "  :middleName,"
            "  :prefix,"
            "  :suffix,"
            "  :customLabel)"));

    ContactsDatabase::Query query(db.prepare(statement));

    const QString firstName(detail.value<QString>(QContactName::FieldFirstName).trimmed());
    const QString lastName(detail.value<QString>(QContactName::FieldLastName).trimmed());

    query.bindValue(":detailId", detailId);
    query.bindValue(":contactId", contactId);
    query.bindValue(":firstName", firstName);
    query.bindValue(":lowerFirstName", firstName.toLower());
    query.bindValue(":lastName", lastName);
    query.bindValue(":lowerLastName", lastName.toLower());
    query.bindValue(":middleName", detail.value<QString>(QContactName::FieldMiddleName).trimmed());
    query.bindValue(":prefix", detail.value<QString>(QContactName::FieldPrefix).trimmed());
    query.bindValue(":suffix", detail.value<QString>(QContactName::FieldSuffix).trimmed());
    query.bindValue(":customLabel", detail.value<QString>(QContactName::FieldCustomLabel).trimmed());

    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, bool update, const QContactNickname &detail)
{
    const QString statement(update
        ? QStringLiteral(
            " UPDATE Nicknames SET"
            "  nickname = :nickname,"
            "  lowerNickname = :lowerNickname"
            " WHERE detailId = :detailId"
            " AND contactId = :contactId")
        : QStringLiteral(
            " INSERT INTO Nicknames ("
            "  detailId,"
            "  contactId,"
            "  nickname,"
            "  lowerNickname)"
            " VALUES ("
            "  :detailId,"
            "  :contactId,"
            "  :nickname,"
            "  :lowerNickname)"));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactNickname T;
    const QString nickname(detail.value<QString>(T::FieldNickname).trimmed());
    query.bindValue(":detailId", detailId);
    query.bindValue(":contactId", contactId);
    query.bindValue(":nickname", nickname);
    query.bindValue(":lowerNickname", nickname.toLower());
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, bool update, const QContactNote &detail)
{
    const QString statement(update
        ? QStringLiteral(
            " UPDATE Notes SET"
            "  note = :note"
            " WHERE detailId = :detailId"
            " AND contactId = :contactId")
        : QStringLiteral(
            " INSERT INTO Notes ("
            "  detailId,"
            "  contactId,"
            "  note)"
            " VALUES ("
            "  :detailId,"
            "  :contactId,"
            "  :note)"));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactNote T;
    query.bindValue(":detailId", detailId);
    query.bindValue(":contactId", contactId);
    query.bindValue(":note", detailValue(detail, T::FieldNote));
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, bool update, const QContactOnlineAccount &detail)
{
    const QString statement(update
        ? QStringLiteral(
            " UPDATE OnlineAccounts SET"
            "  accountUri = :accountUri,"
            "  lowerAccountUri = :lowerAccountUri,"
            "  protocol = :protocol,"
            "  serviceProvider = :serviceProvider,"
            "  capabilities = :capabilities,"
            "  subTypes = :subTypes,"
            "  accountPath = :accountPath,"
            "  accountIconPath = :accountIconPath,"
            "  enabled = :enabled,"
            "  accountDisplayName = :accountDisplayName,"
            "  serviceProviderDisplayName = :serviceProviderDisplayName"
            " WHERE detailId = :detailId"
            " AND contactId = :contactId")
        : QStringLiteral(
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
            "  :serviceProviderDisplayName)"));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactOnlineAccount T;
    const QString uri(detail.value<QString>(T::FieldAccountUri).trimmed());
    query.bindValue(":detailId", detailId);
    query.bindValue(":contactId", contactId);
    query.bindValue(":accountUri", uri);
    query.bindValue(":lowerAccountUri", uri.toLower());
    query.bindValue(":protocol", QString::number(detail.protocol()));
    query.bindValue(":serviceProvider", detailValue(detail, T::FieldServiceProvider));
    query.bindValue(":capabilities", detailValue(detail, T::FieldCapabilities).value<QStringList>().join(QStringLiteral(";")));
    query.bindValue(":subTypes", subTypeList(detail.subTypes()).join(QStringLiteral(";")));
    query.bindValue(":accountPath", detailValue(detail, QContactOnlineAccount__FieldAccountPath));
    query.bindValue(":accountIconPath", detailValue(detail, QContactOnlineAccount__FieldAccountIconPath));
    query.bindValue(":enabled", detailValue(detail, QContactOnlineAccount__FieldEnabled));
    query.bindValue(":accountDisplayName", detailValue(detail, QContactOnlineAccount__FieldAccountDisplayName));
    query.bindValue(":serviceProviderDisplayName", detailValue(detail, QContactOnlineAccount__FieldServiceProviderDisplayName));
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, bool update, const QContactOrganization &detail)
{
    const QString statement(update
        ? QStringLiteral(
            " UPDATE Organizations SET"
            "  name = :name,"
            "  role = :role,"
            "  title = :title,"
            "  location = :location,"
            "  department = :department,"
            "  logoUrl = :logoUrl,"
            "  assistantName = :assistantName"
            " WHERE detailId = :detailId"
            " AND contactId = :contactId")
        : QStringLiteral(
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
            "  :assistantName)"));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactOrganization T;
    query.bindValue(":detailId", detailId);
    query.bindValue(":contactId", contactId);
    query.bindValue(":name", detail.value<QString>(T::FieldName).trimmed());
    query.bindValue(":role", detail.value<QString>(T::FieldRole).trimmed());
    query.bindValue(":title", detail.value<QString>(T::FieldTitle).trimmed());
    query.bindValue(":location", detail.value<QString>(T::FieldLocation).trimmed());
    query.bindValue(":department", detail.department().join(QStringLiteral(";")));
    query.bindValue(":logoUrl", detail.value<QString>(T::FieldLogoUrl).trimmed());
    query.bindValue(":assistantName", detail.value<QString>(T::FieldAssistantName).trimmed());
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, bool update, const QContactPhoneNumber &detail)
{
    const QString statement(update
        ? QStringLiteral(
            " UPDATE PhoneNumbers SET"
            "  phoneNumber = :phoneNumber,"
            "  subTypes = :subTypes,"
            "  normalizedNumber = :normalizedNumber"
            " WHERE detailId = :detailId"
            " AND contactId = :contactId")
        : QStringLiteral(
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
            "  :normalizedNumber)"));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactPhoneNumber T;
    query.bindValue(":detailId", detailId);
    query.bindValue(":contactId", contactId);
    query.bindValue(":phoneNumber", detail.value<QString>(T::FieldNumber).trimmed());
    query.bindValue(":subTypes", subTypeList(detail.subTypes()).join(QStringLiteral(";")));
    query.bindValue(":normalizedNumber", QVariant(ContactsEngine::normalizedPhoneNumber(detail.number())));
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, bool update, const QContactPresence &detail)
{
    const QString statement(update
        ? QStringLiteral(
            " UPDATE Presences SET"
            "  presenceState = :presenceState,"
            "  timestamp = :timestamp,"
            "  nickname = :nickname,"
            "  customMessage = :customMessage,"
            "  presenceStateText = :presenceStateText,"
            "  presenceStateImageUrl = :presenceStateImageUrl"
            " WHERE detailId = :detailId"
            " AND contactId = :contactId")
        : QStringLiteral(
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
            "  :presenceStateImageUrl)"));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactPresence T;
    query.bindValue(":detailId", detailId);
    query.bindValue(":contactId", contactId);
    query.bindValue(":presenceState", detailValue(detail, T::FieldPresenceState));
    query.bindValue(":timestamp", ContactsDatabase::dateTimeString(detail.value<QDateTime>(T::FieldTimestamp).toUTC()));
    query.bindValue(":nickname", detail.value<QString>(T::FieldNickname).trimmed());
    query.bindValue(":customMessage", detail.value<QString>(T::FieldCustomMessage).trimmed());
    query.bindValue(":presenceStateText", detail.value<QString>(T::FieldPresenceStateText).trimmed());
    query.bindValue(":presenceStateImageUrl", detail.value<QString>(T::FieldPresenceStateImageUrl).trimmed());
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, bool update, const QContactRingtone &detail)
{
    const QString statement(update
        ? QStringLiteral(
            " UPDATE Ringtones SET"
            "  audioRingtone = :audioRingtone,"
            "  videoRingtone = :videoRingtone,"
            "  vibrationRingtone = :vibrationRingtone"
            " WHERE detailId = :detailId"
            " AND contactId = :contactId")
        : QStringLiteral(
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
            "  :vibrationRingtone)"));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactRingtone T;
    query.bindValue(":detailId", detailId);
    query.bindValue(":contactId", contactId);
    query.bindValue(":audioRingtone", detail.value<QString>(T::FieldAudioRingtoneUrl).trimmed());
    query.bindValue(":videoRingtone", detail.value<QString>(T::FieldVideoRingtoneUrl).trimmed());
    query.bindValue(":vibrationRingtone", detail.value<QString>(T::FieldVibrationRingtoneUrl).trimmed());
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, bool update, const QContactSyncTarget &detail)
{
    const QString statement(update
        ? QStringLiteral(
            " UPDATE SyncTargets SET"
            "  syncTarget = :syncTarget"
            " WHERE detailId = :detailId"
            " AND contactId = :contactId")
        : QStringLiteral(
            " INSERT INTO SyncTargets ("
            "  detailId,"
            "  contactId,"
            "  syncTarget)"
            " VALUES ("
            "  :detailId,"
            "  :contactId,"
            "  :syncTarget)"));

    ContactsDatabase::Query query(db.prepare(statement));

    query.bindValue(":detailId", detailId);
    query.bindValue(":contactId", contactId);
    query.bindValue(":syncTarget", detail.syncTarget());

    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, bool update, const QContactTag &detail)
{
    const QString statement(update
        ? QStringLiteral(
            " UPDATE Tags SET"
            "  tag = :tag"
            " WHERE detailId = :detailId"
            " AND contactId = :contactId")
        : QStringLiteral(
            " INSERT INTO Tags ("
            "  detailId,"
            "  contactId,"
            "  tag)"
            " VALUES ("
            "  :detailId,"
            "  :contactId,"
            "  :tag)"));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactTag T;
    query.bindValue(":detailId", detailId);
    query.bindValue(":contactId", contactId);
    query.bindValue(":tag", detail.value<QString>(T::FieldTag).trimmed());
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, bool update, const QContactUrl &detail)
{
    const QString statement(update
        ? QStringLiteral(
            " UPDATE Urls SET"
            "  url = :url,"
            "  subTypes = :subTypes"
            " WHERE detailId = :detailId"
            " AND contactId = :contactId")
        : QStringLiteral(
            " INSERT INTO Urls ("
            "  detailId,"
            "  contactId,"
            "  url,"
            "  subTypes)"
            " VALUES ("
            "  :detailId,"
            "  :contactId,"
            "  :url,"
            "  :subTypes)"));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactUrl T;
    query.bindValue(":detailId", detailId);
    query.bindValue(":contactId", contactId);
    query.bindValue(":url", detail.value<QString>(T::FieldUrl).trimmed());
    query.bindValue(":subTypes", detail.hasValue(T::FieldSubType) ? QString::number(detail.subType()) : QString());
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, bool update, const QContactOriginMetadata &detail)
{
    const QString statement(update
        ? QStringLiteral(
            " UPDATE OriginMetadata SET"
            "  id = :id,"
            "  groupId = :groupId,"
            "  enabled = :enabled"
            " WHERE detailId = :detailId"
            " AND contactId = :contactId")
        : QStringLiteral(
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
            "  :enabled)"));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactOriginMetadata T;
    query.bindValue(":detailId", detailId);
    query.bindValue(":contactId", contactId);
    query.bindValue(":id", detailValue(detail, T::FieldId));
    query.bindValue(":groupId", detailValue(detail, T::FieldGroupId));
    query.bindValue(":enabled", detailValue(detail, T::FieldEnabled));
    return query;
}

ContactsDatabase::Query bindDetail(ContactsDatabase &db, quint32 contactId, quint32 detailId, bool update, const QContactExtendedDetail &detail)
{
    const QString statement(update
        ? QStringLiteral(
            " UPDATE ExtendedDetails SET"
            "  name = :name,"
            "  data = :data"
            " WHERE detailId = :detailId"
            " AND contactId = :contactId")
        : QStringLiteral(
            " INSERT INTO ExtendedDetails ("
            "  detailId,"
            "  contactId,"
            "  name,"
            "  data)"
            " VALUES ("
            "  :detailId,"
            "  :contactId,"
            "  :name,"
            "  :data)"));

    ContactsDatabase::Query query(db.prepare(statement));

    typedef QContactExtendedDetail T;
    query.bindValue(":detailId", detailId);
    query.bindValue(":contactId", contactId);
    query.bindValue(":name", detailValue(detail, T::FieldName));
    query.bindValue(":data", detailValue(detail, T::FieldData));
    return query;
}

template <typename T> void removeDuplicateDetails(QList<T> *details)
{
    for (int i = 0; i < details->size() - 1; ++i) {
        for (int j = details->size() - 1; j >= i+1; --j) {
            if (detailPairExactlyMatches(
                    details->at(i), details->at(j),
                    QtContactsSqliteExtensions::defaultIgnorableDetailFields(),
                    QtContactsSqliteExtensions::defaultIgnorableCommonFields())) {
                details->removeAt(j);
            }
        }
    }
}

}

template <typename T> bool ContactWriter::writeDetails(
        quint32 contactId,
        const QtContactsSqliteExtensions::ContactDetailDelta &delta,
        QContact *contact,
        const DetailList &definitionMask,
        const QContactCollectionId &collectionId,
        bool syncable,
        bool wasLocal,
        bool uniqueDetail,
        bool recordUnhandledChangeFlags,
        QContactManager::Error *error)
{
    if (!definitionMask.isEmpty() &&                                          // only a subset of detail types are being written
        !detailListContains<T>(definitionMask) &&                             // this type is not in the set
        !detailListContains<typename GeneratorType<T>::type>(definitionMask)) // this type's generator type is not in the set
        return true;

    const bool aggregateContact(ContactCollectionId::databaseId(collectionId) == ContactsDatabase::AggregateAddressbookCollectionId);

    if (delta.isValid) {
        // perform delta update.
        QList<T> deletions(delta.deleted<T>());
        typename QList<T>::iterator dit = deletions.begin(), dend = deletions.end();
        for ( ; dit != dend; ++dit) {
            T &detail(*dit);
            const quint32 detailId = detail.value(QContactDetail__FieldDatabaseId).toUInt();
            if (detailId == 0) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Invalid detail deletion specified for %1 in contact %2").arg(detailTypeName<T>()).arg(contactId));
                return false;
            } else if (!deleteDetail(m_database, contactId, detailId, detailTypeName<T>(), recordUnhandledChangeFlags, error)) {
                return false;
            }
        }

        QList<T> modifications(delta.modified<T>());
        typename QList<T>::iterator mit = modifications.begin(), mend = modifications.end();
        for ( ; mit != mend; ++mit) {
            T &detail(*mit);
            const quint32 detailId = detail.value(QContactDetail__FieldDatabaseId).toUInt();
            if (detailId == 0) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Invalid detail modification specified for %1 in contact %2").arg(detailTypeName<T>()).arg(contactId));
                return false;
            }

            if (aggregateContact) {
                adjustAggregateDetailProperties(detail);
            }

            if (!writeCommonDetails(contactId, detailId, detail, syncable, wasLocal, aggregateContact, recordUnhandledChangeFlags, error)) {
                return false;
            }

            if (!aggregateContact) {
                // Insert the provenance value into the detail, now that we have it
                const QString provenance(QStringLiteral("%1:%2:%3").arg(ContactCollectionId::databaseId(collectionId)).arg(contactId).arg(detailId));
                detail.setValue(QContactDetail::FieldProvenance, provenance);
            }

            ContactsDatabase::Query query = bindDetail(m_database, contactId, detailId, true, detail);
            if (!ContactsDatabase::execute(query)) {
                query.reportError(QStringLiteral("Failed to update %1 detail %2 for contact %3").arg(detailTypeName<T>()).arg(detailId).arg(contactId));
                *error = QContactManager::UnspecifiedError;
                return false;
            }

            // the delta must be generated such that modifications re-use
            // the correct detail (with correct internal detailId), so that
            // this saveDetail() doesn't result in a new detail being added.
            contact->saveDetail(&detail, QContact::IgnoreAccessConstraints);

            if (uniqueDetail) {
                break;
            }
        }

        QList<T> additions(delta.added<T>());
        typename QList<T>::iterator ait = additions.begin(), aend = additions.end();
        for ( ; ait != aend; ++ait) {
            T &detail(*ait);
            if (aggregateContact) {
                adjustAggregateDetailProperties(detail);
            }

            const quint32 detailId = writeCommonDetails(contactId, 0, detail, syncable, wasLocal, aggregateContact, recordUnhandledChangeFlags, error);
            if (detailId == 0) {
                return false;
            }

            detail.setValue(QContactDetail__FieldDatabaseId, detailId);

            if (!aggregateContact) {
                // Insert the provenance value into the detail, now that we have it
                const QString provenance(QStringLiteral("%1:%2:%3").arg(ContactCollectionId::databaseId(collectionId)).arg(contactId).arg(detailId));
                detail.setValue(QContactDetail::FieldProvenance, provenance);
            }

            ContactsDatabase::Query query = bindDetail(m_database, contactId, detailId, false, detail);
            if (!ContactsDatabase::execute(query)) {
                query.reportError(QStringLiteral("Failed to add %1 detail %2 for contact %3").arg(detailTypeName<T>()).arg(detailId).arg(contactId));
                *error = QContactManager::UnspecifiedError;
                return false;
            }

            contact->saveDetail(&detail, QContact::IgnoreAccessConstraints);

            if (uniqueDetail) {
                break;
            }
        }
    } else {
        // clobber all detail values for this contact.
        if (!removeSpecificDetails<T>(m_database, contactId, error))
            return false;
        if (!removeCommonDetails<T>(contactId, error))
            return false;

        QList<T> contactDetails(contact->details<T>());
        if (aggregateContact) {
            removeDuplicateDetails(&contactDetails);
        }

        typename QList<T>::iterator it = contactDetails.begin(), end = contactDetails.end();
        for ( ; it != end; ++it) {
            T &detail(*it);

            if (aggregateContact) {
                adjustAggregateDetailProperties(detail);
            }

            const quint32 detailId = writeCommonDetails(contactId, 0, detail, syncable, wasLocal, aggregateContact, recordUnhandledChangeFlags, error);
            if (detailId == 0) {
                return false;
            }

            detail.setValue(QContactDetail__FieldDatabaseId, detailId);

            if (!aggregateContact) {
                // Insert the provenance value into the detail, now that we have it
                const QString provenance(QStringLiteral("%1:%2:%3").arg(ContactCollectionId::databaseId(collectionId)).arg(contactId).arg(detailId));
                detail.setValue(QContactDetail::FieldProvenance, provenance);
            }

            ContactsDatabase::Query query = bindDetail(m_database, contactId, detailId, false, detail);
            if (!ContactsDatabase::execute(query)) {
                query.reportError(QStringLiteral("Failed to write details for %1").arg(detailTypeName<T>()));
                *error = QContactManager::UnspecifiedError;
                return false;
            }

            contact->saveDetail(&detail, QContact::IgnoreAccessConstraints);

            if (uniqueDetail) {
                break;
            }
        }
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
    QMutexLocker locker(withinTransaction ? nullptr : m_database.accessMutex());

    if (contacts->isEmpty())
        return QContactManager::NoError;

    // Check that all of the contacts have the same collectionId.
    // Note that empty == "local" for all intents and purposes.
    QContactCollectionId collectionId;
    if (!withinAggregateUpdate && !withinSyncUpdate) {
        foreach (const QContact &contact, *contacts) {
            // retrieve current contact's collectionId
            const QContactCollectionId currCollectionId = contact.collectionId().isNull()
                    ? ContactCollectionId::apiId(ContactsDatabase::LocalAddressbookCollectionId, m_managerUri)
                    : contact.collectionId();

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

    // If this is a non-sync update, and non-aggregate update,
    // then we may need to record the change as an "unhandled" change
    // if the collection is marked as such.
    // These "unhandled" changes occur between fetchChanges and storeChanges/clearChangeFlags
    // and need to be recorded for reporting in the next fetchChanges result.
    bool recordUnhandledChangeFlags = false;
    if (!withinSyncUpdate && !withinAggregateUpdate
            && m_reader->recordUnhandledChangeFlags(collectionId, &recordUnhandledChangeFlags) != QContactManager::NoError) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to determine recordUnhandledChangeFlags value for collection: %1").arg(QString::fromLatin1(collectionId.localId())));
        return QContactManager::UnspecifiedError;
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
            err = create(&contact, definitionMask, true, withinAggregateUpdate, withinSyncUpdate, recordUnhandledChangeFlags);
            if (err == QContactManager::NoError) {
                contactId = ContactId::apiId(contact);
                dbId = ContactId::databaseId(contactId);
                m_addedIds.insert(contactId);
            } else {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Error creating contact: %1 collectionId: %2")
                                          .arg(err).arg(ContactCollectionId::toString(contact.collectionId())));
            }
        } else {
            err = update(&contact, definitionMask, &aggregateUpdated, true, withinAggregateUpdate, withinSyncUpdate, recordUnhandledChangeFlags, presenceOnlyUpdate);
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

            if (ContactCollectionId::databaseId(currCollectionId) != ContactsDatabase::AggregateAddressbookCollectionId
                    && !m_suppressedCollectionIds.contains(currCollectionId)) {
                m_collectionContactsChanged.insert(currCollectionId);
            }
        } else {
            worstError = err;
            if (errorMap) {
                errorMap->insert(i, err);
            }
        }
    }

    if (m_database.aggregating() && !withinAggregateUpdate && possibleReactivation && worstError == QContactManager::NoError) {
        // Some contacts may need to have new aggregates created
        // if they previously had a QContactDeactivated detail
        // and this detail was removed (i.e. reactivated).
        QContactManager::Error aggregateError = aggregateOrphanedContacts(true, withinSyncUpdate);
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
            QString customLabel = cname.value<QString>(QContactName::FieldCustomLabel);
            if (!customLabel.isEmpty() && aname.value<QString>(QContactName::FieldCustomLabel).isEmpty())
                aname.setValue(QContactName::FieldCustomLabel, cname.value(QContactName::FieldCustomLabel));
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
            if (cg.gender() != QContactGender::GenderUnspecified
                    && (ag.gender() != QContactGender::GenderMale && ag.gender() != QContactGender::GenderFemale)) {
                ag.setGender(cg.gender());
                aggregate->saveDetail(&ag, QContact::IgnoreAccessConstraints);
            }
        } else if (detailType(original) == detailType<QContactFavorite>()) {
            // favorite involves composition
            QContactFavorite cf(original);
            QContactFavorite af(aggregate->detail<QContactFavorite>());
            if ((cf.isFavorite() && !af.isFavorite()) || aggregate->details<QContactFavorite>().isEmpty()) {
                af.setFavorite(cf.isFavorite());
                aggregate->saveDetail(&af, QContact::IgnoreAccessConstraints);
            }
        } else if (detailType(original) == detailType<QContactBirthday>()) {
            // birthday involves composition (at least, it's unique)
            QContactBirthday cb(original);
            QContactBirthday ab(aggregate->detail<QContactBirthday>());
            if (!ab.dateTime().isValid() || aggregate->details<QContactBirthday>().isEmpty()) {
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
                det.setValue(QContactDetail::FieldProvenance, original.value<QString>(QContactDetail::FieldProvenance));

                aggregate->saveDetail(&det, QContact::IgnoreAccessConstraints);
            }
        }
    }
}

/*
   This function is called when a new contact is created.  The
   aggregate contacts are searched for a match, and the matching
   one updated if it exists; or a new aggregate is created.
*/
QContactManager::Error ContactWriter::updateOrCreateAggregate(QContact *contact, const DetailList &definitionMask, bool withinTransaction, bool withinSyncUpdate, bool createOnly, quint32 *aggregateContactId)
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
    static const QString possibleAggregatesWhere(QStringLiteral(
        /* SELECT contactId FROM Contacts ... */
        " WHERE Contacts.collectionId = 1" // AggregateAddressbookCollectionId
        " AND Contacts.contactId IN ("
            " SELECT contactId FROM Names"
            " WHERE COALESCE(:lastName, '') = ''"
            "    OR COALESCE(lowerLastName, '') = ''"
            "    OR lowerLastName = :lastName"
            " UNION"
            " SELECT contactId FROM Nicknames"
            " WHERE contactId NOT IN (SELECT contactId FROM Names))"
        " AND Contacts.contactId NOT IN ("
            " SELECT contactId FROM Genders"
            " WHERE gender = :excludeGender)"
        " AND contactId > 2" // exclude self contact
        " AND isDeactivated = 0" // exclude deactivated
        " AND contactId NOT IN ("
            " SELECT secondId FROM Relationships WHERE firstId = :contactId AND type = 'IsNot'"
            " UNION"
            " SELECT firstId FROM Relationships WHERE secondId = :contactId AND type = 'IsNot'"
        " )"));

    // Use a simple match algorithm, looking for exact matches on name fields,
    // or accumulating points for name matches (including partial matches of first name).

    // step one: build the temporary table which contains all "possible" aggregate contact ids.
    m_database.clearTemporaryContactIdsTable(possibleAggregatesTable);

    const QString orderBy = QStringLiteral("contactId ASC ");
    const QString where = possibleAggregatesWhere;
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
            " SELECT Names.contactId, 20 AS score FROM Names"
            " INNER JOIN temp.possibleAggregates ON Names.contactId = temp.possibleAggregates.contactId"
                " WHERE lowerLastName  != '' AND lowerLastName  = :lastName"
                "   AND lowerFirstName != '' AND lowerFirstName = :firstName"
            " UNION"
            " SELECT Names.contactId, 15 AS score FROM Names"
            " INNER JOIN temp.possibleAggregates ON Names.contactId = temp.possibleAggregates.contactId"
                " WHERE COALESCE(lowerFirstName,'') = '' AND COALESCE(:firstName,'') = ''"
                "   AND COALESCE(lowerLastName, '') = '' AND COALESCE(:lastName, '') = ''"
                "   AND EXISTS ("
                      " SELECT * FROM Nicknames"
                      " WHERE Nicknames.contactId = Names.contactId"
                      "   AND lowerNickName = :nickname)"
            " UNION"
            " SELECT Nicknames.contactId, 15 AS score FROM Nicknames"
            " INNER JOIN temp.possibleAggregates ON Nicknames.contactId = temp.possibleAggregates.contactId"
                " WHERE lowerNickName = :nickname"
                "   AND COALESCE(:firstName,'') = ''"
                "   AND COALESCE(:lastName, '') = ''"
                "   AND NOT EXISTS ("
                    " SELECT * FROM Names WHERE Names.contactId = Nicknames.contactId )"
            " UNION"
            " SELECT Names.contactId, 12 AS score FROM Names"
            " INNER JOIN temp.possibleAggregates ON Names.contactId = temp.possibleAggregates.contactId"
                " WHERE (COALESCE(lowerLastName, '') = '' OR COALESCE(:lastName, '') = '')"
                "   AND lowerFirstName != '' AND lowerFirstName = :firstName"
            " UNION"
            " SELECT Names.contactId, 12 AS score FROM Names"
            " INNER JOIN temp.possibleAggregates ON Names.contactId = temp.possibleAggregates.contactId"
                " WHERE lowerLastName != '' AND lowerLastName = :lastName"
                "   AND (COALESCE(lowerFirstName, '') = '' OR COALESCE(:firstName, '') = '')"
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

    if (!existingAggregateId) {
        // need to create an aggregating contact first.
        matchingAggregate.setCollectionId(ContactCollectionId::apiId(ContactsDatabase::AggregateAddressbookCollectionId, m_managerUri));
    } else if (!createOnly) {
        // aggregate already exists.
        QList<quint32> readIds;
        readIds.append(existingAggregateId);

        QContactFetchHint hint;
        hint.setOptimizationHints(QContactFetchHint::NoRelationships);

        QList<QContact> readList;
        QContactManager::Error readError = m_reader->readContacts(QStringLiteral("CreateAggregate"), &readList, readIds, hint);
        if (readError != QContactManager::NoError || readList.size() < 1) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to read aggregate contact %1 during regenerate").arg(existingAggregateId));
            return QContactManager::UnspecifiedError;
        }

        matchingAggregate = readList.at(0);
    }

    QContactManager::Error err = QContactManager::NoError;
    QMap<int, QContactManager::Error> errorMap;
    QContactId matchingAggregateId;
    if (existingAggregateId && createOnly) {
        // the caller has specified that we should not update existing aggregates.
        // this is because it will manually regenerate the aggregates themselves,
        // with specific detail promotion order (e.g. prefer local contact details).
        matchingAggregateId = QContactId(ContactId::apiId(existingAggregateId, m_managerUri));
    } else {
        // whether it's an existing or new contact, we promote details.
        // TODO: promote non-Aggregates relationships!
        promoteDetailsToAggregate(*contact, &matchingAggregate, definitionMask, false);

        // now save in database.
        QList<QContact> saveContactList;
        saveContactList.append(matchingAggregate);
        err = save(&saveContactList, DetailList(), 0, &errorMap, withinTransaction, true, false); // we're updating (or creating) the aggregate
        if (err != QContactManager::NoError) {
            if (!existingAggregateId) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Could not create new aggregate contact"));
            } else {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Could not update existing aggregate contact"));
            }
            return err;
        }
        matchingAggregateId = saveContactList.at(0).id();
    }

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
        query.bindValue(":firstId", ContactId::databaseId(matchingAggregateId));
        query.bindValue(":secondId", ContactId::databaseId(*contact));
        query.bindValue(":type", relationshipString(QContactRelationship::Aggregates));
        if (!ContactsDatabase::execute(query)) {
            query.reportError("Error inserting Aggregates relationship");
            err = QContactManager::UnspecifiedError;
        }
    }

    if (err == QContactManager::NoError) {
        if (aggregateContactId) {
            *aggregateContactId = ContactId::databaseId(matchingAggregateId);
        }
    } else {
        // if the aggregation relationship fails, the entire save has failed.
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to save aggregation relationship!"));

        if (!existingAggregateId) {
            // clean up the newly created contact.
            QList<QContactId> removeList;
            removeList.append(matchingAggregateId);
            QContactManager::Error cleanupErr = remove(removeList, &errorMap, withinTransaction, withinSyncUpdate);
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
                " AND secondId NOT IN (SELECT contactId FROM Contacts WHERE changeFlags >= 4)"
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
        QContactManager::Error readError = m_reader->readContacts(QStringLiteral("RegenerateAggregate"), &readList, readIds, hint);
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

        // Step two: search for the "local" contacts and promote their details first
        bool foundFirstLocal = false;
        for (int i = 1; i < readList.size(); ++i) { // start from 1 to skip aggregate
            QContact curr = readList.at(i);
            if (curr.details<QContactDeactivated>().count())
                continue;
            if (ContactCollectionId::databaseId(curr.collectionId()) != ContactsDatabase::LocalAddressbookCollectionId)
                continue;
            if (!foundFirstLocal) {
                foundFirstLocal = true;
                const QList<QContactDetail> currDetails = curr.details();
                for (int j = 0; j < currDetails.size(); ++j) {
                    QContactDetail currDet = currDetails.at(j);
                    if (promoteDetailType(currDet.type(), definitionMask, false)) {
                        // unconditionally promote this detail to the aggregate.
                        aggregateContact.saveDetail(&currDet, QContact::IgnoreAccessConstraints);
                    }
                }
            } else {
                promoteDetailsToAggregate(curr, &aggregateContact, definitionMask, false);
            }
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
                " SELECT DISTINCT firstId FROM Relationships"
                " WHERE type = 'Aggregates'"
                " AND secondId NOT IN ("
                    " SELECT contactId FROM Contacts WHERE changeFlags >= 4" // ChangeFlags::IsDeleted
                " )"
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

QContactManager::Error ContactWriter::aggregateOrphanedContacts(bool withinTransaction, bool withinSyncUpdate)
{
    QList<quint32> contactIds;

    {
        const QString orphanContactIds(QStringLiteral(
            " SELECT contactId FROM Contacts"
                " WHERE isDeactivated = 0"
                " AND changeFlags < 4" // ChangeFlags::IsDeleted
                " AND collectionId IN ("
                    " SELECT collectionId FROM Collections WHERE aggregable = 1"
                " )"
                " AND contactId NOT IN ("
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
        QContactManager::Error readError = m_reader->readContacts(QStringLiteral("AggregateOrphaned"), &readList, contactIds, hint);
        if (readError != QContactManager::NoError || readList.size() != contactIds.size()) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to read orphaned contacts for aggregation"));
            return QContactManager::UnspecifiedError;
        }

        QList<QContact>::iterator it = readList.begin(), end = readList.end();
        for ( ; it != end; ++it) {
            QContact &orphan(*it);
            QContactManager::Error error = updateOrCreateAggregate(&orphan, DetailList(), withinTransaction, withinSyncUpdate);
            if (error != QContactManager::NoError) {
                QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to create aggregate for orphaned contact: %1").arg(ContactId::toString(orphan)));
                return error;
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

QContactManager::Error ContactWriter::create(QContact *contact, const DetailList &definitionMask, bool withinTransaction, bool withinAggregateUpdate, bool withinSyncUpdate, bool recordUnhandledChangeFlags)
{
    // If not specified, this contact is a "local device" contact
    bool contactIsLocal = false;
    const QContactCollectionId localAddressbookId(ContactCollectionId::apiId(ContactsDatabase::LocalAddressbookCollectionId, m_managerUri));
    if (contact->collectionId().isNull()) {
        contact->setCollectionId(localAddressbookId);
    }

    // If this contact is local, ensure it has a GUID for import/export stability
    if (contact->collectionId() == localAddressbookId) {
        contactIsLocal = true;
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
    m_engine.regenerateDisplayLabel(*contact, &m_displayLabelGroupsChanged);

    // update the timestamp if necessary (aggregate contacts should have a composed timestamp value)
    if (!m_database.aggregating() || (contact->collectionId() != ContactCollectionId::apiId(ContactsDatabase::AggregateAddressbookCollectionId, m_managerUri))) {
        // only update the timestamp for "normal" modifications, not updates caused by sync,
        // as we should retain the revision timestamp for synced contacts.
        if (!withinSyncUpdate) {
            updateTimestamp(contact, true);
        }
    }

    QContactManager::Error writeErr = enforceDetailConstraints(contact);
    if (writeErr != QContactManager::NoError) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Contact failed detail constraints"));
        return writeErr;
    }

    quint32 contactId = 0;

    {
        ContactsDatabase::Query query(bindContactDetails(*contact, withinSyncUpdate || withinAggregateUpdate, recordUnhandledChangeFlags));
        if (!ContactsDatabase::execute(query)) {
            query.reportError("Failed to create contact");
            return QContactManager::UnspecifiedError;
        }
        contactId = query.lastInsertId().toUInt();
    }

    writeErr = write(contactId, QContact(), contact, definitionMask, recordUnhandledChangeFlags);
    if (writeErr == QContactManager::NoError) {
        // successfully saved all data.  Update id.
        contact->setId(ContactId::apiId(contactId, m_managerUri));

        if (m_database.aggregating() && !withinAggregateUpdate) {
            // and either update the aggregate contact (if it exists) or create a new one
            // (unless it is an aggregate contact, or should otherwise not be aggregated).
            bool aggregable = contactIsLocal; // local contacts are always aggregable.
            if (!aggregable) {
                writeErr = collectionIsAggregable(contact->collectionId(), &aggregable);
                if (writeErr != QContactManager::NoError) {
                    return writeErr;
                }
            }

            if (aggregable) {
                writeErr = setAggregate(contact, contactId, false, definitionMask, withinTransaction, withinSyncUpdate);
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

QContactManager::Error ContactWriter::update(QContact *contact, const DetailList &definitionMask, bool *aggregateUpdated, bool withinTransaction, bool withinAggregateUpdate, bool withinSyncUpdate, bool recordUnhandledChangeFlags, bool transientUpdate)
{
    *aggregateUpdated = false;

    quint32 contactId = ContactId::databaseId(*contact);
    int exists = 0;
    int changeFlags = 0;
    QContactCollectionId oldCollectionId;

    {
        const QString checkContactExists(QStringLiteral(
            " SELECT COUNT(contactId), collectionId, changeFlags FROM Contacts WHERE contactId = :contactId"
        ));

        ContactsDatabase::Query query(m_database.prepare(checkContactExists));
        query.bindValue(0, contactId);
        if (!ContactsDatabase::execute(query) || !query.next()) {
            query.reportError("Failed to check contact existence");
            return QContactManager::UnspecifiedError;
        } else {
            exists = query.value<quint32>(0);
            oldCollectionId = ContactCollectionId::apiId(query.value<quint32>(1), m_managerUri);
            changeFlags = query.value<int>(2);
        }
    }

    if (!exists) {
        return QContactManager::DoesNotExistError;
    }

    if (ContactCollectionId::databaseId(oldCollectionId) == ContactsDatabase::LocalAddressbookCollectionId
            && contact->collectionId().isNull()) {
        contact->setCollectionId(oldCollectionId);
    }

    if (!oldCollectionId.isNull() && contact->collectionId() != oldCollectionId) {
        // they are attempting to manually change the collectionId of a contact
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot manually change collectionId: %1 to %2")
                .arg(ContactCollectionId::databaseId(oldCollectionId)).arg(ContactCollectionId::databaseId(contact->collectionId())));
        return QContactManager::UnspecifiedError;
    }

    // check to see if this is an attempted undeletion.
    QContactManager::Error writeError = QContactManager::NoError;
    if (changeFlags >= ContactsDatabase::IsDeleted) {
        QList<QContactUndelete> undeleteDetails = contact->details<QContactUndelete>();
        if (undeleteDetails.size() == 0) {
            // the only modification we allow to deleted contacts is undeletion.
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Cannot modify deleted contact: %1").arg(contactId));
            return QContactManager::DoesNotExistError;
        }

        // undelete the contact.
        writeError = undeleteContacts(QVariantList() << contactId, recordUnhandledChangeFlags);
        if (writeError != QContactManager::NoError) {
            return writeError;
        }

        // regenerate the undeleted contact data from the database.
        QContactFetchHint hint;
        hint.setOptimizationHints(QContactFetchHint::NoRelationships);
        QList<QContact> undeletedList;
        QContactManager::Error readError = m_reader->readContacts(QStringLiteral("RegenerateUndeleted"), &undeletedList, QList<quint32>() << contactId, hint);
        if (readError != QContactManager::NoError || undeletedList.size() != 1) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to read undeleted contact data for regenerate: %1").arg(contactId));
            return QContactManager::UnspecifiedError;
        }
        *contact = undeletedList.first();

        // if the database is aggregating, fall through, as we may need to
        // recreate or regenerate the aggregate, below.
        if (!m_database.aggregating()) {
            return writeError;
        }
    } else {
        writeError = enforceDetailConstraints(contact);
        if (writeError != QContactManager::NoError) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Contact failed detail constraints"));
            return writeError;
        }

        // update the modification timestamp (aggregate contacts should have a composed timestamp value)
        if (!m_database.aggregating()
                || (contact->collectionId() != ContactCollectionId::apiId(ContactsDatabase::AggregateAddressbookCollectionId, m_managerUri))) {
            // only update the timestamp for "normal" modifications, not updates caused by sync,
            // as we should retain the revision timestamp for synced contacts.
            if (!withinSyncUpdate) {
                updateTimestamp(contact, false);
            }
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
        m_engine.regenerateDisplayLabel(*contact, &m_displayLabelGroupsChanged);

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
            QList<QContact> oldContacts;
            if (!withinAggregateUpdate) {
                // read the existing contact data from the database, to perform delta detection.
                QContactManager::Error readOldContactError = m_reader->readContacts(QStringLiteral("UpdateContact"), &oldContacts, QList<quint32>() << contactId, QContactFetchHint());
                if (readOldContactError != QContactManager::NoError || oldContacts.size() != 1) {
                    QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to read existing data during update for contact: %1").arg(contactId));
                    return QContactManager::UnspecifiedError;
                }
            }

            // This update invalidates any details that may be present in the transient store
            m_database.removeTransientDetails(contactId);

            // Store updated details to the database
            {
                ContactsDatabase::Query query(bindContactDetails(*contact, withinSyncUpdate || withinAggregateUpdate, recordUnhandledChangeFlags, definitionMask, contactId));
                if (!ContactsDatabase::execute(query)) {
                    query.reportError("Failed to update contact");
                    return QContactManager::UnspecifiedError;
                }
            }

            writeError = write(contactId, withinAggregateUpdate ? QContact() : oldContacts.first(), contact, definitionMask, recordUnhandledChangeFlags);
        }
    }

    if (m_database.aggregating() && writeError == QContactManager::NoError) {
        if (oldCollectionId != ContactCollectionId::apiId(ContactsDatabase::AggregateAddressbookCollectionId, m_managerUri)) {
            bool aggregable = false;
            writeError = collectionIsAggregable(contact->collectionId(), &aggregable);
            if (writeError != QContactManager::NoError) {
                return writeError;
            }

            if (aggregable) {
                const QString findAggregateForContact(QStringLiteral(
                    " SELECT DISTINCT firstId FROM Relationships"
                    " WHERE type = 'Aggregates' AND secondId = :localId"
                ));

                ContactsDatabase::Query query(m_database.prepare(findAggregateForContact));
                query.bindValue(":localId", contactId);
                if (!ContactsDatabase::execute(query)) {
                    query.reportError("Failed to fetch aggregator contact ids during update");
                    return QContactManager::UnspecifiedError;
                }

                QList<quint32> aggregatesOfUpdated;
                while (query.next()) {
                    aggregatesOfUpdated.append(query.value<quint32>(0));
                }

                if (aggregatesOfUpdated.size() > 0) {
                    writeError = regenerateAggregates(aggregatesOfUpdated, definitionMask, withinTransaction);
                } else if (oldCollectionId == ContactCollectionId::apiId(ContactsDatabase::LocalAddressbookCollectionId, m_managerUri)) {
                    writeError = setAggregate(contact, contactId, true, definitionMask, withinTransaction, withinSyncUpdate);
                }
                if (writeError != QContactManager::NoError) {
                    return writeError;
                }

                *aggregateUpdated = true;
            }
        }
    }

    return writeError;
}

QContactManager::Error ContactWriter::collectionIsAggregable(const QContactCollectionId &collectionId, bool *aggregable)
{
    *aggregable = false;

    const QString contactShouldBeAggregated(QStringLiteral(
        " SELECT aggregable FROM Collections WHERE collectionId = :collectionId"
    ));

    ContactsDatabase::Query query(m_database.prepare(contactShouldBeAggregated));
    query.bindValue(":collectionId", ContactCollectionId::databaseId(collectionId));
    if (!ContactsDatabase::execute(query)) {
        query.reportError("Failed to determine aggregability during update");
        return QContactManager::UnspecifiedError;
    }

    if (query.next()) {
        *aggregable = query.value<bool>(0);
    }

    return QContactManager::NoError;
}

QContactManager::Error ContactWriter::setAggregate(QContact *contact, quint32 contactId, bool update, const DetailList &definitionMask, bool withinTransaction, bool withinSyncUpdate)
{
    quint32 aggregateId = 0;

    const bool createOnly = true;
    QContactManager::Error writeErr = updateOrCreateAggregate(contact, definitionMask, withinTransaction, withinSyncUpdate, createOnly, &aggregateId);
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

QContactManager::Error ContactWriter::write(
        quint32 contactId,
        const QContact &oldContact,
        QContact *contact,
        const DetailList &definitionMask,
        bool recordUnhandledChangeFlags)
{
    // Does this contact belong to a synced addressbook?
    const QContactCollectionId collectionId = contact->collectionId();
    const bool wasLocal = false; // XXXXXXXXXXXXXXXXXXXX TODO fixme?
    const bool syncable = (ContactCollectionId::databaseId(collectionId) != ContactsDatabase::AggregateAddressbookCollectionId) &&
                          (ContactCollectionId::databaseId(collectionId) != ContactsDatabase::LocalAddressbookCollectionId);

    // if the oldContact doesn't match this one,
    // don't perform delta detection and update;
    // instead, clobber all detail values for this contact.
    const bool performDeltaDetection = ContactId::databaseId(oldContact) == contactId;
    const QtContactsSqliteExtensions::ContactDetailDelta delta
            = performDeltaDetection
            ? QtContactsSqliteExtensions::determineContactDetailDelta(
                        oldContact.details(), contact->details())
            : QtContactsSqliteExtensions::ContactDetailDelta();

    QContactManager::Error error = QContactManager::NoError;
    if (writeDetails<QContactAddress>(contactId, delta, contact, definitionMask, collectionId, syncable, wasLocal, false, recordUnhandledChangeFlags, &error)
            && writeDetails<QContactAnniversary>(contactId, delta, contact, definitionMask, collectionId, syncable, wasLocal, false, recordUnhandledChangeFlags, &error)
            && writeDetails<QContactAvatar>(contactId, delta, contact, definitionMask, collectionId, syncable, wasLocal, false, recordUnhandledChangeFlags, &error)
            && writeDetails<QContactBirthday>(contactId, delta, contact, definitionMask, collectionId, syncable, wasLocal, false, recordUnhandledChangeFlags, &error)
            && writeDetails<QContactDisplayLabel>(contactId, delta, contact, definitionMask, collectionId, syncable, wasLocal, true, recordUnhandledChangeFlags, &error)
            && writeDetails<QContactEmailAddress>(contactId, delta, contact, definitionMask, collectionId, syncable, wasLocal, false, recordUnhandledChangeFlags, &error)
            && writeDetails<QContactFamily>(contactId, delta, contact, definitionMask, collectionId, syncable, wasLocal, false, recordUnhandledChangeFlags, &error)
            && writeDetails<QContactFavorite>(contactId, delta, contact, definitionMask, collectionId, syncable, wasLocal, true, recordUnhandledChangeFlags, &error)
            && writeDetails<QContactGender>(contactId, delta, contact, definitionMask, collectionId, syncable, wasLocal, true, recordUnhandledChangeFlags, &error)
            && writeDetails<QContactGeoLocation>(contactId, delta, contact, definitionMask, collectionId, syncable, wasLocal, false, recordUnhandledChangeFlags, &error)
            && writeDetails<QContactGlobalPresence>(contactId, delta, contact, definitionMask, collectionId, syncable, wasLocal, true, recordUnhandledChangeFlags, &error)
            && writeDetails<QContactGuid>(contactId, delta, contact, definitionMask, collectionId, syncable, wasLocal, false, recordUnhandledChangeFlags, &error)
            && writeDetails<QContactHobby>(contactId, delta, contact, definitionMask, collectionId, syncable, wasLocal, false, recordUnhandledChangeFlags, &error)
            && writeDetails<QContactName>(contactId, delta, contact, definitionMask, collectionId, syncable, wasLocal, true, recordUnhandledChangeFlags, &error)
            && writeDetails<QContactNickname>(contactId, delta, contact, definitionMask, collectionId, syncable, wasLocal, false, recordUnhandledChangeFlags, &error)
            && writeDetails<QContactNote>(contactId, delta, contact, definitionMask, collectionId, syncable, wasLocal, false, recordUnhandledChangeFlags, &error)
            && writeDetails<QContactOnlineAccount>(contactId, delta, contact, definitionMask, collectionId, syncable, wasLocal, false, recordUnhandledChangeFlags, &error)
            && writeDetails<QContactOrganization>(contactId, delta, contact, definitionMask, collectionId, syncable, wasLocal, false, recordUnhandledChangeFlags, &error)
            && writeDetails<QContactPhoneNumber>(contactId, delta, contact, definitionMask, collectionId, syncable, wasLocal, false, recordUnhandledChangeFlags, &error)
            && writeDetails<QContactPresence>(contactId, delta, contact, definitionMask, collectionId, syncable, wasLocal, false, recordUnhandledChangeFlags, &error)
            && writeDetails<QContactRingtone>(contactId, delta, contact, definitionMask, collectionId, syncable, wasLocal, false, recordUnhandledChangeFlags, &error)
            && writeDetails<QContactSyncTarget>(contactId, delta, contact, definitionMask, collectionId, syncable, wasLocal, true, recordUnhandledChangeFlags, &error)
            && writeDetails<QContactTag>(contactId, delta, contact, definitionMask, collectionId, syncable, wasLocal, false, recordUnhandledChangeFlags, &error)
            && writeDetails<QContactUrl>(contactId, delta, contact, definitionMask, collectionId, syncable, wasLocal, false, recordUnhandledChangeFlags, &error)
            && writeDetails<QContactOriginMetadata>(contactId, delta, contact, definitionMask, collectionId, syncable, wasLocal, false, recordUnhandledChangeFlags, &error)
            && writeDetails<QContactExtendedDetail>(contactId, delta, contact, definitionMask, collectionId, syncable, wasLocal, false, recordUnhandledChangeFlags, &error)
            ) {
        return QContactManager::NoError;
    }
    return error;
}

ContactsDatabase::Query ContactWriter::bindContactDetails(const QContact &contact, bool keepChangeFlags, bool recordUnhandledChangeFlags, const DetailList &definitionMask, quint32 contactId)
{
    const QString insertContact(QStringLiteral(
        " INSERT INTO Contacts ("
        "  collectionId,"
        "  created,"
        "  modified,"
        "  hasPhoneNumber,"
        "  hasEmailAddress,"
        "  hasOnlineAccount,"
        "  isOnline,"
        "  isDeactivated,"
        "  changeFlags,"
        "  unhandledChangeFlags)"
        " VALUES ("
        "  :collectionId,"
        "  :created,"
        "  :modified,"
        "  :hasPhoneNumber,"
        "  :hasEmailAccount,"
        "  :hasOnlineAccount,"
        "  :isOnline,"
        "  :isDeactivated,"
        "  %1,"
        "  %2)"
    ).arg(keepChangeFlags ? 0 : 1) // if addition is due to sync, don't set Added flag.  Aggregates don't get flags either.
     .arg((!keepChangeFlags && recordUnhandledChangeFlags) ? 1 : 0));

    const QString updateContact(QStringLiteral(
        " UPDATE Contacts SET"
        "  collectionId = :collectionId,"
        "  created = :created,"
        "  modified = :modified,"
        "  hasPhoneNumber = CASE WHEN :valueKnown = 1 THEN :value ELSE hasPhoneNumber END,"
        "  hasEmailAddress = CASE WHEN :valueKnown = 1 THEN :value ELSE hasEmailAddress END,"
        "  hasOnlineAccount = CASE WHEN :valueKnown = 1 THEN :value ELSE hasOnlineAccount END,"
        "  isOnline = CASE WHEN :valueKnown = 1 THEN :value ELSE isOnline END,"
        "  isDeactivated = CASE WHEN :valueKnown = 1 THEN :value ELSE isDeactivated END,"
        "  changeFlags = %1,"
        "  unhandledChangeFlags = %2"
        " WHERE contactId = :contactId;"
    ).arg(keepChangeFlags ? QStringLiteral("changeFlags") // if modification is due to sync, don't set Modified flag.  Aggregates don't get flags either.
                          : QStringLiteral("changeFlags | 2")) // ChangeFlags::IsModified
     .arg((!keepChangeFlags && recordUnhandledChangeFlags) ? QStringLiteral("unhandledChangeFlags | 2") : QStringLiteral("unhandledChangeFlags")));

    const bool update(contactId != 0);

    ContactsDatabase::Query query(m_database.prepare(update ? updateContact : insertContact));

    int col = 0;
    const quint32 collectionId = ContactCollectionId::databaseId(contact.collectionId()) > 0
                               ? ContactCollectionId::databaseId(contact.collectionId())
                               : static_cast<quint32>(ContactsDatabase::LocalAddressbookCollectionId);

    query.bindValue(col++, collectionId);

    const QContactTimestamp timestamp = contact.detail<QContactTimestamp>();
    query.bindValue(col++, ContactsDatabase::dateTimeString(timestamp.value<QDateTime>(QContactTimestamp::FieldCreationTimestamp).toUTC()));
    query.bindValue(col++, ContactsDatabase::dateTimeString(timestamp.value<QDateTime>(QContactTimestamp::FieldModificationTimestamp).toUTC()));

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
        // TODO: should we also disallow deactivation of local addressbook contacts?
        if (collectionId == ContactsDatabase::AggregateAddressbookCollectionId) {
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
        "  applicationName,"
        "  accountId,"
        "  remotePath,"
        "  changeFlags)"
        " VALUES ("
        "  :aggregable,"
        "  :name,"
        "  :description,"
        "  :color,"
        "  :secondaryColor,"
        "  :image,"
        "  :applicationName,"
        "  :accountId,"
        "  :remotePath,"
        "  1)" // ChangeFlags::IsAdded
    ));
    const QString updateCollection(QStringLiteral(
        " UPDATE Collections SET"
        "  aggregable = :aggregable,"
        "  name = :name,"
        "  description = :description,"
        "  color = :color,"
        "  secondaryColor = :secondaryColor,"
        "  image = :image,"
        "  applicationName = :applicationName,"
        "  accountId = :accountId,"
        "  remotePath = :remotePath,"
        "  changeFlags = changeFlags | 2" // ChangeFlags::IsModified
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
    query.bindValue(":applicationName", collection.extendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME).toString());
    query.bindValue(":accountId", collection.extendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID).toInt());
    query.bindValue(":remotePath", collection.extendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH).toString());
    if (update) {
        query.bindValue(":collectionId", ContactCollectionId::databaseId(collection));
    }

    return query;
}

ContactsDatabase::Query ContactWriter::bindCollectionMetadataDetails(const QContactCollection &collection, int *count)
{
    const QString insertMetadata(QStringLiteral(
        " INSERT OR REPLACE INTO CollectionsMetadata ("
        "  collectionId,"
        "  key,"
        "  value)"
        " VALUES ("
        "  :collectionId,"
        "  :key,"
        "  :value)"
    ));

    QVariantList boundIds;
    QVariantList boundKeys;
    QVariantList boundValues;
    const QVariantMap extendedMetadata = collection.extendedMetaData();
    for (QVariantMap::const_iterator it = extendedMetadata.constBegin(); it != extendedMetadata.constEnd(); it++) {
        // store the key/value pairs which we haven't stored already in the Collections table
        if (it.key() != COLLECTION_EXTENDEDMETADATA_KEY_AGGREGABLE
                && it.key() != COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME
                && it.key() != COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID
                && it.key() != COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH) {
            boundIds.append(ContactCollectionId::databaseId(collection.id()));
            boundKeys.append(it.key());
            boundValues.append(it.value());
        }
    }

    ContactsDatabase::Query query(m_database.prepare(insertMetadata));
    query.bindValue(":collectionId", boundIds);
    query.bindValue(":key", boundKeys);
    query.bindValue(":value", boundValues);

    *count = boundValues.size();
    return query;
}

