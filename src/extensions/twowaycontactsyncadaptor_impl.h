/*
 * Copyright (C) 2014 - 2017 Jolla Ltd.
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

#ifndef TWOWAYCONTACTSYNCADAPTOR_IMPL_H
#define TWOWAYCONTACTSYNCADAPTOR_IMPL_H

#include <qtcontacts-extensions.h>
#include <contactmanagerengine.h>
#include <twowaycontactsyncadaptor.h>
#include <contactdelta_impl.h>
#include <qcontactoriginmetadata.h>
#include <qcontactstatusflags.h>

#include <QContactManager>
#include <QContactGuid>
#include <QContactSyncTarget>
#include <QContactTimestamp>
#include <QContactUrl>
#include <QContactPhoneNumber>
#include <QContactAddress>

#include <QDebug>
#include <QLocale>
#include <QDataStream>

#define QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG(msg)                           \
    do {                                                                 \
        if (Q_UNLIKELY(qtcontacts_sqlite_twcsa_debug_trace_enabled())) { \
            qDebug() << msg;                                             \
        }                                                                \
    } while (0)

namespace QtContactsSqliteExtensions {

    static bool qtcontacts_sqlite_twcsa_debug_trace_enabled()
    {
        static const bool traceEnabled(!QString(QLatin1String(qgetenv("QTCONTACTS_SQLITE_TWCSA_TRACE"))).isEmpty());
        return traceEnabled;
    }

    // QDataStream encoding version to use in OOB storage.
    // Don't change this without scheduling a migration for stored data!
    // (which can be done in contactsdatabase.cpp)
    const QDataStream::Version STREAM_VERSION = QDataStream::Qt_5_1;

    class TwoWayContactSyncAdaptorPrivate
    {
    public:
        TwoWayContactSyncAdaptorPrivate(
                TwoWayContactSyncAdaptor *q,
                int accountId,
                const QString &applicationName);
        TwoWayContactSyncAdaptorPrivate(
                TwoWayContactSyncAdaptor *q,
                int accountId,
                const QString &applicationName,
                const QMap<QString, QString> &params);
        TwoWayContactSyncAdaptorPrivate(
                TwoWayContactSyncAdaptor *q,
                int accountId,
                const QString &applicationName,
                QContactManager &manager);
        ~TwoWayContactSyncAdaptorPrivate();


        struct CollectionChanges {
            QList<QContactCollection> addedCollections;
            QList<QContactCollection> modifiedCollections;
            QList<QContactCollection> removedCollections;
            QList<QContactCollection> unmodifiedCollections;
        };

        struct ContactChanges {
            QList<QContact> addedContacts;
            QList<QContact> modifiedContacts;
            QList<QContact> removedContacts;
            QList<QContact> unmodifiedContacts;
        };

        enum CollectionSyncOperationType {
            Unmodified = 0,
            LocalAddition,
            LocalModification,
            LocalDeletion,
            RemoteAddition,
            RemoteModification
        };

        struct CollectionSyncOperation {
            QContactCollection collection;
            CollectionSyncOperationType operationType;
        };

        CollectionChanges m_collectionChanges;
        QHash<QContactCollectionId, ContactChanges> m_localContactChanges;
        QHash<QContactCollectionId, ContactChanges> m_remoteContactChanges;

        QList<CollectionSyncOperation> m_syncOperations;

        TwoWayContactSyncAdaptor *m_q = nullptr;
        QContactManager *m_manager = nullptr;
        ContactManagerEngine *m_engine = nullptr;
        QString m_oobScope;
        QString m_applicationName;
        int m_accountId = 0;
        bool m_deleteManager = false;
        bool m_busy = false;
        bool m_errorOccurred = false;
        bool m_continueAfterError = false;
    };
}

QTCONTACTS_USE_NAMESPACE
using namespace QtContactsSqliteExtensions;

/*!
 * \class TwoWayContactSyncAdaptor
 * \brief TwoWayContactSyncAdaptor provides an interface which
 *        contact sync plugins can implement in order to correctly
 *        synchronize contact data between a remote datastore and
 *        the local device contacts database.
 *
 * A contact sync plugin which implements this interface must
 * provide implementations for at least the following methods:
 * \list
 * \li determineRemoteCollections()
 * \li determineRemoteContacts()
 * \li deleteRemoteCollection()
 * \li storeLocalChangesRemotely()
 * \li syncFinishedSuccessfully()
 * \li syncFinishedWithError()
 * \endlist
 *
 * If the contact sync plugin is able to determine precisely what
 * has changed in the remote datastore since the last sync (e.g.
 * via ctag or syncToken which can be stored as metadata in the
 * collection), then it can also implement the following methods:
 * \list
 * \li determineRemoteCollectionChanges()
 * \li determineRemoteContactChanges()
 * \endlist
 *
 * Finally, the plugin can define its own conflict resolution
 * semantics by implementing:
 * \list
 * \li resolveConflictingChanges()
 * \endlist
 *
 * Note that this interface is provided merely as a convenience;
 * a contact sync plugin which doesn't wish to utilize this
 * interface may instead use the sync transaction API offered
 * by the ContactManagerEngine directly.
 */

Q_DECLARE_METATYPE(QList<QContactCollection>)
Q_DECLARE_METATYPE(QList<QContactCollectionId>)

namespace {

void registerTypes()
{
    static bool registered = false;
    if (!registered) {
        registered = true;
        qRegisterMetaType<QList<int> >();
        qRegisterMetaTypeStreamOperators<QList<int> >();
        qRegisterMetaType<QList<QContactCollection> >();
        qRegisterMetaType<QList<QContactCollectionId> >();
    }
}

// Input must be UTC
QString dateTimeString(const QDateTime &qdt)
{
    return QLocale::c().toString(qdt, QStringLiteral("yyyy-MM-ddThh:mm:ss.zzz"));
}

QDateTime fromDateTimeString(const QString &s)
{
    QDateTime rv(QLocale::c().toDateTime(s, QStringLiteral("yyyy-MM-ddThh:mm:ss.zzz")));
    rv.setTimeSpec(Qt::UTC);
    return rv;
}

QMap<QString, QString> checkParams(const QMap<QString, QString> &params)
{
    QMap<QString, QString> rv(params);

    const QString presenceKey(QStringLiteral("mergePresenceChanges"));
    if (!rv.contains(presenceKey)) {
        // Don't report presence changes
        rv.insert(presenceKey, QStringLiteral("false"));
    }

    return rv;
}

void modifyContactDetail(const QContactDetail &original, const QContactDetail &modified,
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

void removeEquivalentDetails(QList<QContactDetail> &original, QList<QContactDetail> &updated, QList<QContact> &equivalent)
{
    // Determine which details are in the update contact which aren't in the database contact:
    // Detail order is not defined, so loop over the entire set for each, removing matches or
    // superset details (eg, backend added a field (like lastModified to timestamp) on previous save)
    QList<QContactDetail>::iterator oit = original.begin(), oend = original.end();
    while (oit != oend) {
        QList<QContactDetail>::iterator uit = updated.begin(), uend = updated.end();
        while (uit != uend) {
            if (detailsEquivalent(*oit, *uit)) {
                // These details match - remove from the lists
                uit = updated.erase(uit);
                break;
            }
            ++uit;
        }
        if (uit != uend) {
            // We found a match
            oit = original.erase(oit);
        } else {
            ++oit;
        }
    }
}

}

TwoWayContactSyncAdaptorPrivate::TwoWayContactSyncAdaptorPrivate(
        TwoWayContactSyncAdaptor *q,
        int accountId,
        const QString &applicationName)
    : m_q(q)
    , m_applicationName(applicationName)
    , m_accountId(accountId)
    , m_deleteManager(false)
{
    registerTypes();
}

TwoWayContactSyncAdaptorPrivate::TwoWayContactSyncAdaptorPrivate(
        TwoWayContactSyncAdaptor *q,
        int accountId,
        const QString &applicationName,
        const QMap<QString, QString> &params)
    : m_q(q)
    , m_manager(new QContactManager(QStringLiteral("org.nemomobile.contacts.sqlite"), checkParams(params)))
    , m_engine(contactManagerEngine(*m_manager))
    , m_applicationName(applicationName)
    , m_accountId(accountId)
    , m_deleteManager(true)
{
    registerTypes();
}

TwoWayContactSyncAdaptorPrivate::TwoWayContactSyncAdaptorPrivate(
        TwoWayContactSyncAdaptor *q,
        int accountId,
        const QString &applicationName,
        QContactManager &manager)
    : m_q(q)
    , m_manager(&manager)
    , m_engine(contactManagerEngine(*m_manager))
    , m_applicationName(applicationName)
    , m_accountId(accountId)
    , m_deleteManager(false)
{
    registerTypes();
}

TwoWayContactSyncAdaptorPrivate::~TwoWayContactSyncAdaptorPrivate()
{
    if (m_deleteManager) {
        delete m_manager;
    }
}

TwoWayContactSyncAdaptor::TwoWayContactSyncAdaptor(
        int accountId,
        const QString &applicationName)
    : d(new TwoWayContactSyncAdaptorPrivate(this, accountId, applicationName))
{
}

TwoWayContactSyncAdaptor::TwoWayContactSyncAdaptor(
        int accountId,
        const QString &applicationName,
        const QMap<QString, QString> &params)
    : d(new TwoWayContactSyncAdaptorPrivate(this, accountId, applicationName, params))
{
}

TwoWayContactSyncAdaptor::TwoWayContactSyncAdaptor(
        int accountId,
        const QString &applicationName,
        QContactManager &manager)
    : d(new TwoWayContactSyncAdaptorPrivate(this, accountId, applicationName, manager))
{
}

TwoWayContactSyncAdaptor::~TwoWayContactSyncAdaptor()
{
    delete d;
}

void TwoWayContactSyncAdaptor::setManager(QContactManager &manager)
{
    d->m_manager = &manager;
    d->m_engine = contactManagerEngine(manager);
    d->m_deleteManager = false;
}

bool TwoWayContactSyncAdaptor::startSync(ErrorHandlingMode mode)
{
    if (!d) {
        qWarning() << "Sync adaptor not initialised!";
        return false;
    }

    if (!d->m_engine) {
        qWarning() << "Sync adaptor manager not set!";
        return false;
    }

    if (d->m_busy) {
        qWarning() << "Sync adaptor for application: " << d->m_applicationName
                   << " for account: " << d->m_accountId << " is already busy!";
        return false;
    }

    QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG(
            QStringLiteral("Starting contacts sync by application: %1 for account: %2")
                      .arg(d->m_applicationName).arg(d->m_accountId).toUtf8());

    d->m_busy = true;
    d->m_continueAfterError = mode == TwoWayContactSyncAdaptor::ContinueAfterError;

    QContactManager::Error err = QContactManager::NoError;
    if (!d->m_engine->fetchCollectionChanges(
            d->m_accountId,
            d->m_applicationName,
            &d->m_collectionChanges.addedCollections,
            &d->m_collectionChanges.modifiedCollections,
            &d->m_collectionChanges.removedCollections,
            &d->m_collectionChanges.unmodifiedCollections,
            &err)) {
        qWarning() << "Unable to fetch collection changes for application: " << d->m_applicationName
                   << " for account: " << d->m_accountId << " - " << err;
        d->m_busy = false;
        syncFinishedWithError();
        return false;
    }

    if (!determineRemoteCollectionChanges(
            d->m_collectionChanges.addedCollections,
            d->m_collectionChanges.modifiedCollections,
            d->m_collectionChanges.removedCollections,
            d->m_collectionChanges.unmodifiedCollections,
            &err)) {
        if (err != QContactManager::NotSupportedError) {
            qWarning() << "Unable to determine remote collection changes for application: " << d->m_applicationName
                       << " for account: " << d->m_accountId << " - " << err;
            d->m_busy = false;
            syncFinishedWithError();
            return false;
        } else if (!determineRemoteCollections()) {
            qWarning() << "Unable to determine remote collections for application: " << d->m_applicationName
                       << " for account: " << d->m_accountId << " - " << err;
            d->m_busy = false;
            syncFinishedWithError();
            return false;
        }
    }

    return true;
}

bool TwoWayContactSyncAdaptor::determineRemoteCollectionChanges(
        const QList<QContactCollection> &locallyAddedCollections,
        const QList<QContactCollection> &locallyModifiedCollections,
        const QList<QContactCollection> &locallyRemovedCollections,
        const QList<QContactCollection> &locallyUnmodifiedCollections,
        QContactManager::Error *error)
{
    // By default, we assume that the plugin is unable to determine
    // a precise delta of what collection metadata has changed on
    // the remote server.
    // If this assumption is incorrect, the plugin should override
    // this method to perform the appropriate requests and then
    // invoke remoteCollectionChangesDetermined() once complete.
    *error = QContactManager::NotSupportedError;
    return false;
}

bool TwoWayContactSyncAdaptor::determineRemoteCollections()
{
    // The plugin must implement this method to retrieve
    // information about addressbooks on the remote server,
    // and then invoke remoteCollectionsDetermined() once complete
    // (or syncOperationError() if an error occurred).
    qWarning() << "TWCSA::determineRemoteCollections(): implementation missing";
    return false;
}

// match by id and then by remote path
static QContactCollectionId findMatchingCollection(const QContactCollection &remoteCollection, const QList<QContactCollection> &localCollections)
{
    for (const QContactCollection &localCollection : localCollections) {
        if (!remoteCollection.id().isNull() && remoteCollection.id() == localCollection.id()) {
            return localCollection.id();
        } else if (remoteCollection.id().isNull()
                && !remoteCollection.extendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH).toString().isEmpty()
                && remoteCollection.extendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH).toString()
                    == localCollection.extendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH).toString()) {
            return localCollection.id();
        }
    }

    return QContactCollectionId();
}

static QContactCollection remoteCollectionWithId(const QContactCollection &collection, const QContactCollectionId &id)
{
    QContactCollection ret(collection);
    if (collection.id().isNull()) {
        ret.setId(id);
    }
    return ret;
}

void TwoWayContactSyncAdaptor::remoteCollectionsDetermined(
        const QList<QContactCollection> &remoteCollections)
{
    // we determine the remote collection delta by inspection,
    // comparing them to the local collections fetched earlier.
    QList<QContactCollection> remotelyAddedCollections;
    QList<QContactCollection> remotelyModifiedCollections;
    QList<QContactCollection> remotelyRemovedCollections;
    QList<QContactCollection> remotelyUnmodifiedCollections;
    QSet<QContactCollectionId> seenLocalCollections;
    for (const QContactCollection &remoteCollection : remoteCollections) {
        // attempt to find matching local collection.
        // if we find a matching local collection which was modified,
        // we assume that it was unmodified on the remote side.
        // otherwise, we assume that it was modified on the remote side.
        // client plugins can override this method if they
        // a way to precisely determine change ordering / resolution,
        // although in that case they're probably better off just
        // implementing determineRemoteCollectionChanges() directly.
        bool foundMatch = false;

        // could be one of the added collections, if the previous sync cycle
        // was aborted after the local collection addition was pushed to the server.
        QList<QContactCollection>::iterator ait = d->m_collectionChanges.addedCollections.begin(),
                                           aend = d->m_collectionChanges.addedCollections.end();
        while (ait != aend && !foundMatch) {
            if ((!remoteCollection.id().isNull() && remoteCollection.id() == ait->id())
                    || (remoteCollection.id().isNull()
                        && !remoteCollection.extendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH).toString().isEmpty()
                        && remoteCollection.extendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH).toString()
                            == ait->extendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH).toString())) {
                foundMatch = true;
                seenLocalCollections.insert(ait->id());
                remotelyUnmodifiedCollections.append(remoteCollectionWithId(remoteCollection, ait->id()));
                d->m_collectionChanges.modifiedCollections.append(*ait);
                ait = d->m_collectionChanges.addedCollections.erase(ait);
            } else {
                ++ait;
            }
        }

        // also check locally modified collections for matches.
        if (!foundMatch) {
            const QContactCollectionId matchingLocalModifiedCollectionId = findMatchingCollection(
                    remoteCollection, d->m_collectionChanges.modifiedCollections);
            if (!matchingLocalModifiedCollectionId.isNull()) {
                foundMatch = true;
                remotelyUnmodifiedCollections.append(remoteCollectionWithId(remoteCollection, matchingLocalModifiedCollectionId));
                seenLocalCollections.insert(matchingLocalModifiedCollectionId);
            }
        }

        // also check locally removed collections for matches.
        if (!foundMatch) {
            const QContactCollectionId matchingLocalRemovedCollectionId = findMatchingCollection(
                    remoteCollection, d->m_collectionChanges.removedCollections);
            if (!matchingLocalRemovedCollectionId.isNull()) {
                foundMatch = true;
                remotelyUnmodifiedCollections.append(remoteCollectionWithId(remoteCollection, matchingLocalRemovedCollectionId));
                seenLocalCollections.insert(matchingLocalRemovedCollectionId);
            }
        }

        // finally, check locally unmodified collections for matches.
        if (!foundMatch) {
            const QContactCollectionId matchingLocalUnmodifiedCollectionId = findMatchingCollection(
                    remoteCollection, d->m_collectionChanges.unmodifiedCollections);
            if (!matchingLocalUnmodifiedCollectionId.isNull()) {
                foundMatch = true;
                remotelyModifiedCollections.append(remoteCollectionWithId(remoteCollection, matchingLocalUnmodifiedCollectionId));
                seenLocalCollections.insert(matchingLocalUnmodifiedCollectionId);
            }
        }

        // no matching local collection was found, must have been a remote addition.
        if (!foundMatch) {
            if (remoteCollection.id().isNull()) {
                remotelyAddedCollections.append(remoteCollection);
            } else {
                qWarning() << "Error: manual delta detection found remote collection addition, but collection already has id:"
                           << QString::fromLatin1(remoteCollection.id().localId())
                           << " : " << remoteCollection.metaData(QContactCollection::KeyName).toString();
            }
        }
    }

    // now determine remotely removed collections.
    for (const QContactCollection &col : d->m_collectionChanges.modifiedCollections) {
        if (!seenLocalCollections.contains(col.id())) {
            remotelyRemovedCollections.append(col);
        }
    }
    for (const QContactCollection &col : d->m_collectionChanges.unmodifiedCollections) {
        if (!seenLocalCollections.contains(col.id())) {
            remotelyRemovedCollections.append(col);
        }
    }

    remoteCollectionChangesDetermined(
            remotelyAddedCollections,
            remotelyModifiedCollections,
            remotelyRemovedCollections,
            remotelyUnmodifiedCollections);
}

void TwoWayContactSyncAdaptor::remoteCollectionChangesDetermined(
        const QList<QContactCollection> &remotelyAddedCollections,
        const QList<QContactCollection> &remotelyModifiedCollections,
        const QList<QContactCollection> &remotelyRemovedCollections,
        const QList<QContactCollection> &remotelyUnmodifiedCollections)
{
    QSet<QContactCollectionId> handledCollectionIds;

    // Construct a queue of sync operations to be completed one-at-a-time.
    // Note that the order in which we handle each of these change-sets
    // is important (as e.g. a collection which is remotely modified
    // may also appear as an unmodified local collection, and thus we
    // need to ensure we enqueue only one operation for the collection).
    for (const QContactCollection &col : remotelyRemovedCollections) {
        // we will remove these directly from local storage, at the end.
        // mark them as handled, so that we don't attempt to sync them.
        handledCollectionIds.insert(col.id());
    }
    for (const QContactCollection &col : d->m_collectionChanges.removedCollections) {
        if (!handledCollectionIds.contains(col.id())) {
            handledCollectionIds.insert(col.id());
            d->m_syncOperations.append({col, TwoWayContactSyncAdaptorPrivate::LocalDeletion});
        }
    }
    for (const QContactCollection &col : remotelyModifiedCollections) {
        if (!handledCollectionIds.contains(col.id())) {
            handledCollectionIds.insert(col.id());
            d->m_syncOperations.append({col, TwoWayContactSyncAdaptorPrivate::RemoteModification});
        }
    }
    for (const QContactCollection &col : d->m_collectionChanges.modifiedCollections) {
        if (!handledCollectionIds.contains(col.id())) {
            handledCollectionIds.insert(col.id());
            d->m_syncOperations.append({col, TwoWayContactSyncAdaptorPrivate::LocalModification});
        }
    }
    for (const QContactCollection &col : d->m_collectionChanges.unmodifiedCollections) {
        if (!handledCollectionIds.contains(col.id())) {
            handledCollectionIds.insert(col.id());
            d->m_syncOperations.append({col, TwoWayContactSyncAdaptorPrivate::Unmodified});
        }
    }
    for (const QContactCollection &col : d->m_collectionChanges.addedCollections) {
        if (!handledCollectionIds.contains(col.id())) {
            handledCollectionIds.insert(col.id());
            d->m_syncOperations.append({col, TwoWayContactSyncAdaptorPrivate::LocalAddition});
        }
    }
    for (const QContactCollection &col : remotelyUnmodifiedCollections) {
        if (!handledCollectionIds.contains(col.id())) {
            handledCollectionIds.insert(col.id());
            d->m_syncOperations.append({col, TwoWayContactSyncAdaptorPrivate::Unmodified});
        }
    }
    for (const QContactCollection &col : remotelyAddedCollections) {
        d->m_syncOperations.append({col, TwoWayContactSyncAdaptorPrivate::RemoteAddition});
    }

    QList<QContactCollectionId> remotelyRemovedCollectionIds;
    for (const QContactCollection &col : remotelyRemovedCollections) {
        remotelyRemovedCollectionIds.append(col.id());
    }
    if (remotelyRemovedCollectionIds.size() && !storeRemoteCollectionDeletionsLocally(remotelyRemovedCollectionIds)) {
        qWarning() << "Failed to store remote deletion of collections to local database!";
        syncOperationError();
    } else {
        performNextQueuedOperation();
    }
}

bool TwoWayContactSyncAdaptor::storeRemoteCollectionDeletionsLocally(
        const QList<QContactCollectionId> &collectionIds)
{
    QContactManager::Error err = QContactManager::NoError;
    return d->m_engine->storeChanges(nullptr, nullptr, collectionIds,
                                     ContactManagerEngine::PreserveLocalChanges,
                                     true, &err);
}

void TwoWayContactSyncAdaptor::performNextQueuedOperation()
{
    if (d->m_syncOperations.isEmpty()) {
        d->m_busy = false;
        if (d->m_errorOccurred) {
            syncFinishedWithError();
        } else {
            syncFinishedSuccessfully();
        }
    } else {
        TwoWayContactSyncAdaptorPrivate::CollectionSyncOperation op = d->m_syncOperations.takeFirst();
        startCollectionSync(op.collection, op.operationType);
    }
}

void TwoWayContactSyncAdaptor::startCollectionSync(const QContactCollection &collection, int changeFlag)
{
    TwoWayContactSyncAdaptorPrivate::CollectionSyncOperationType opType
            = static_cast<TwoWayContactSyncAdaptorPrivate::CollectionSyncOperationType>(changeFlag);

    QTCONTACTS_SQLITE_TWCSA_DEBUG_LOG(
            QStringLiteral("Performing sync operation %1 on contacts collection %2 with application: %3 for account: %4")
                      .arg(opType)
                      .arg(collection.extendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH).toString().isEmpty()
                            ? QString::fromLatin1(collection.id().localId())
                            : collection.extendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_REMOTEPATH).toString())
                      .arg(d->m_applicationName)
                      .arg(d->m_accountId).toUtf8());

    if (opType == TwoWayContactSyncAdaptorPrivate::LocalDeletion) {
        if (!deleteRemoteCollection(collection)) {
            qWarning() << "Failed to push the local deletion of the collection "
                       << QString::fromLatin1(collection.id().localId())
                       << " for application " << d->m_applicationName
                       << " for account " << d->m_accountId;
            syncOperationError();
        }
        return;
    }

    if (opType == TwoWayContactSyncAdaptorPrivate::LocalAddition) {
        // no need to determine remote changes for a collection which doesn't exist remotely yet.
        // instead, just determine the local contacts and store them remotely.
        QList<QContact> addedContacts;
        QContactManager::Error err = QContactManager::NoError;
        if (!d->m_engine->fetchContactChanges(collection.id(), &addedContacts, nullptr, nullptr, nullptr, &err)) {
            qWarning() << "Failed to fetch contacts for locally added collection "
                       << QString::fromLatin1(collection.id().localId())
                       << " for application " << d->m_applicationName
                       << " for account " << d->m_accountId;
            syncOperationError();
        } else if (!storeLocalChangesRemotely(collection, addedContacts, QList<QContact>(), QList<QContact>())) {
            qWarning() << "Unable to store local changes remotely for locally added collection "
                       << collection.metaData(QContactCollection::KeyName).toString()
                       << "for application: " << d->m_applicationName
                       << " for account: " << d->m_accountId;
            syncOperationError();
        }
        return;
    }

    if (opType == TwoWayContactSyncAdaptorPrivate::RemoteAddition) {
        // no need to determine local changes for a collection which doesn't exist locally yet.
        // instead, just determine the remote contacts and then store them locally.
        if (!determineRemoteContacts(collection)) {
            qWarning() << "Unable to determine remote contacts for remotely added collection "
                       << collection.metaData(QContactCollection::KeyName).toString()
                       << "for application: " << d->m_applicationName
                       << " for account: " << d->m_accountId;
            syncOperationError();
        }
        return;
    }

    // otherwise, there are both local and remote contact changes to determine and apply.
    QList<QContact> addedContacts, modifiedContacts, removedContacts, unmodifiedContacts;
    QContactManager::Error err = QContactManager::NoError;
    if (!d->m_engine->fetchContactChanges(collection.id(), &addedContacts, &modifiedContacts, &removedContacts, &unmodifiedContacts, &err)) {
        qWarning() << "Failed to fetch contacts for locally represented collection "
                   << QString::fromLatin1(collection.id().localId())
                   << " for application " << d->m_applicationName
                   << " for account " << d->m_accountId;
        syncOperationError();
        return;
    }

    d->m_localContactChanges.insert(collection.id(),
            { addedContacts, modifiedContacts, removedContacts, unmodifiedContacts });
    if (!determineRemoteContactChanges(collection, addedContacts, modifiedContacts, removedContacts, unmodifiedContacts, &err)) {
        if (err != QContactManager::NotSupportedError) {
            qWarning() << "Unable to determine remote changes for collection "
                       << QString::fromLatin1(collection.id().localId())
                       << " for application: " << d->m_applicationName
                       << " for account: " << d->m_accountId << " - " << err;
            syncOperationError();
        } else if (!determineRemoteContacts(collection)) {
            qWarning() << "Unable to determine remote contacts for collection "
                       << QString::fromLatin1(collection.id().localId())
                       << "for application: " << d->m_applicationName
                       << " for account: " << d->m_accountId;
            syncOperationError();
        }
    }
}

bool TwoWayContactSyncAdaptor::deleteRemoteCollection(const QContactCollection &collection)
{
    // The plugin must implement this method to delete
    // a remote addressbook from the server,
    // and then invoke remoteCollectionDeleted() when complete
    // (or syncOperationError() if an error occurred).
    qWarning() << "TWCSA::deleteRemoteCollection(): implementation missing";
    return false;
}

void TwoWayContactSyncAdaptor::remoteCollectionDeleted(const QContactCollection &collection)
{
    QContactManager::Error error = QContactManager::NoError;
    if (!d->m_engine->clearChangeFlags(collection.id(), &error)) {
        qWarning() << "Failed to clear change flags for collection "
                   << QString::fromLatin1(collection.id().localId())
                   << "for application: " << d->m_applicationName
                   << " for account: " << d->m_accountId
                   << " after pushing local deletion to remote.";
        syncOperationError();
    } else {
        performNextQueuedOperation();
    }
}

bool TwoWayContactSyncAdaptor::determineRemoteContacts(const QContactCollection &collection)
{
    // The plugin must implement this method to retrieve
    // information about contacts in an addressbook on the remote server,
    // and call remoteContactsDetermined() once complete
    // (or syncOperationError() if an error occurred).
    qWarning() << "TWCSA::determineRemoteContacts(): implementation missing";
    return false;
}

static QContact findMatchingContact(const QContact &contact, const QList<QContact> &contacts)
{
    for (const QContact &c : contacts) {
        if (!contact.id().isNull() && contact.id() == c.id()) {
            return c;
        } else if (contact.id().isNull()) {
            if (!contact.detail<QContactGuid>().guid().isEmpty()
                    && contact.detail<QContactGuid>().guid() == c.detail<QContactGuid>().guid()) {
                return c;
            } else if (!contact.detail<QContactSyncTarget>().syncTarget().isEmpty()
                    && contact.detail<QContactSyncTarget>().syncTarget() == c.detail<QContactSyncTarget>().syncTarget()) {
                return c;
            }
        }
    }

    return QContact();
}

static QContact contactWithId(const QContact &contact, const QContactId &id)
{
    QContact ret = contact;
    if (contact.id().isNull()) {
        ret.setId(id);
    }
    return ret;
}

void TwoWayContactSyncAdaptor::remoteContactsDetermined(
        const QContactCollection &collection,
        const QList<QContact> &contacts)
{
    if (!d->m_localContactChanges.contains(collection.id())) {
        // must have been a remote collection addition.
        // every contact here will be considered an addition.
        remoteContactChangesDetermined(collection,
                                       contacts,
                                       QList<QContact>(),
                                       QList<QContact>());
        return;
    }

    QList<QContact> remoteAdditions;
    QList<QContact> remoteModifications;
    QList<QContact> remoteDeletions;
    QSet<QContactId> handledContactIds;

    TwoWayContactSyncAdaptorPrivate::ContactChanges &localChanges(d->m_localContactChanges[collection.id()]);

    for (const QContact &contact : contacts) {
        bool matchFound = false;

        // first, search local additions for a matching contact.
        // this can happen if an error occurred after the local additions were
        // successfully pushed to the server, during the previous sync cycle.
        QList<QContact>::iterator ait = localChanges.addedContacts.begin(),
                                 aend = localChanges.addedContacts.end();
        while (ait != aend && !matchFound) {
            if (!contact.id().isNull() && contact.id() == ait->id()) {
                matchFound = true;
            } else if (contact.id().isNull()) {
                if (!contact.detail<QContactGuid>().guid().isEmpty()
                        && contact.detail<QContactGuid>().guid() == ait->detail<QContactGuid>().guid()) {
                    matchFound = true;
                } else if (!contact.detail<QContactSyncTarget>().syncTarget().isEmpty()
                        && contact.detail<QContactSyncTarget>().syncTarget() == ait->detail<QContactSyncTarget>().syncTarget()) {
                    matchFound = true;
                }
            }

            if (matchFound) {
                // treat the matching local addition as a remote modification instead.
                // TODO: perform per-detail delta detection, to determine precise changes.
                handledContactIds.insert(ait->id());
                localChanges.modifiedContacts.append(*ait);
                remoteModifications.append(contactWithId(contact, ait->id()));
                ait = localChanges.addedContacts.erase(ait);
            } else {
                ++ait;
            }
        }

        if (!matchFound) {
            const QContact c = findMatchingContact(contact, localChanges.removedContacts);
            if (!c.id().isNull()) {
                // this contact will be deleted locally anyway.  treat as remote unmodified.
                matchFound = true;
                handledContactIds.insert(c.id());
            }
        }

        if (!matchFound) {
            const QContact c = findMatchingContact(contact, localChanges.modifiedContacts);
            if (!c.id().isNull()) {
                // assume that the remote contact is unmodified, so the local
                // change will be preserved.
                // TODO: perform per-detail delta detection, to determine precise changes.
                matchFound = true;
                handledContactIds.insert(c.id());
            }
        }

        if (!matchFound) {
            const QContact c = findMatchingContact(contact, localChanges.unmodifiedContacts);
            if (!c.id().isNull()) {
                // assume that the remote contact is modified.
                // TODO: perform per-detail delta detection, to determine precise changes.
                matchFound = true;
                handledContactIds.insert(c.id());
                remoteModifications.append(contactWithId(contact, c.id()));
            }
        }

        if (!matchFound) {
            if (contact.id().isNull()) {
                remoteAdditions.append(contact);
            } else {
                qWarning() << "Error: manual delta detection found remote contact addition, but contact already has id:"
                           << QString::fromLatin1(contact.id().localId());
            }
        }
    }

    // now check the local modified/unmodified contacts:
    // any which we haven't seen, must have been deleted remotely.
    QList<QContact>::iterator it = localChanges.modifiedContacts.begin(),
                             end = localChanges.modifiedContacts.end();
    while (it != end) {
        if (!handledContactIds.contains(it->id())) {
            handledContactIds.insert(it->id());
            remoteDeletions.append(*it);
            it = localChanges.modifiedContacts.erase(it);
        } else {
            ++it;
        }
    }

    it = localChanges.unmodifiedContacts.begin();
    end = localChanges.unmodifiedContacts.end();
    while (it != end) {
        if (!handledContactIds.contains(it->id())) {
            handledContactIds.insert(it->id());
            remoteDeletions.append(*it);
            it = localChanges.unmodifiedContacts.erase(it);
        } else {
            ++it;
        }
    }

    remoteContactChangesDetermined(
            collection,
            remoteAdditions,
            remoteModifications,
            remoteDeletions);
}

bool TwoWayContactSyncAdaptor::determineRemoteContactChanges(
        const QContactCollection &collection,
        const QList<QContact> &localAddedContacts,
        const QList<QContact> &localModifiedContacts,
        const QList<QContact> &localDeletedContacts,
        const QList<QContact> &localUnmodifiedContacts,
        QContactManager::Error *error)
{
    // By default, we assume that the plugin is unable to determine
    // a precise delta of what contacts have changed on
    // the remote server.
    // If this assumption is incorrect, the plugin should override
    // this method to perform the appropriate requests and then
    // invoke remoteContactChangesDetermined() once complete.
    *error = QContactManager::NotSupportedError;
    return false;
}

static void setContactChangeFlags(QContact &c, QContactStatusFlags::Flag flag)
{
    QContactStatusFlags flags = c.detail<QContactStatusFlags>();
    if (flag == QContactStatusFlags::IsAdded) {
        flags.setFlag(QContactStatusFlags::IsAdded, true);
        flags.setFlag(QContactStatusFlags::IsModified, false);
        flags.setFlag(QContactStatusFlags::IsDeleted, false);
    } else if (flag == QContactStatusFlags::IsModified) {
        flags.setFlag(QContactStatusFlags::IsAdded, false);
        flags.setFlag(QContactStatusFlags::IsModified, true);
        flags.setFlag(QContactStatusFlags::IsDeleted, false);
    } else if (flag == QContactStatusFlags::IsDeleted) {
        flags.setFlag(QContactStatusFlags::IsAdded, false);
        flags.setFlag(QContactStatusFlags::IsModified, false);
        flags.setFlag(QContactStatusFlags::IsDeleted, true);
    }
    c.saveDetail(&flags, QContact::IgnoreAccessConstraints);
}

void TwoWayContactSyncAdaptor::remoteContactChangesDetermined(
        const QContactCollection &collection,
        const QList<QContact> &remotelyAddedContacts,
        const QList<QContact> &remotelyModifiedContacts,
        const QList<QContact> &remotelyRemovedContacts)
{
    // if this is not a pure remote-collection-addition, then we may have
    // local changes which need to be applied remotely.
    bool haveLocalChanges = false;
    QSet<QContactId> handledContactIds;
    QList<QContact> remoteAdditions, remoteModifications, remoteRemovals;
    if (!collection.id().isNull() && d->m_localContactChanges.contains(collection.id())) {
        TwoWayContactSyncAdaptorPrivate::ContactChanges &localChanges(d->m_localContactChanges[collection.id()]);
        haveLocalChanges = !localChanges.addedContacts.isEmpty()
                        || !localChanges.removedContacts.isEmpty();
        // resolve conflicts between local and remote changes
        QList<QContact> localModifications;
        for (const QContact &c : localChanges.modifiedContacts) {
            bool foundMatch = false;
            for (const QContact &r : remotelyModifiedContacts) {
                if (c.id() == r.id()) {
                    bool identical = false;
                    QContact resolved = resolveConflictingChanges(c, r, &identical);
                    if (!identical) {
                        haveLocalChanges = true;
                        localModifications.append(resolved);
                        QContact modified = resolved;
                        setContactChangeFlags(modified, QContactStatusFlags::IsModified);
                        remoteModifications.append(modified);
                    }
                    handledContactIds.insert(c.id());
                    foundMatch = true;
                    break;
                }
            }
            if (!foundMatch) {
                haveLocalChanges = true;
                localModifications.append(c);
            }
        }
        localChanges.modifiedContacts = localModifications;
    }

    // set the appropriate change flags on the remote changes (which will be applied locally).
    for (const QContact &r : remotelyAddedContacts) {
        QContact added = r;
        setContactChangeFlags(added, QContactStatusFlags::IsAdded);
        remoteAdditions.append(added);
    }
    for (const QContact &r : remotelyRemovedContacts) {
        QContact deleted = r;
        setContactChangeFlags(deleted, QContactStatusFlags::IsDeleted);
        remoteRemovals.append(deleted);
    }
    for (const QContact &r : remotelyModifiedContacts) {
        if (!handledContactIds.contains(r.id())) {
            bool foundMatch = false;
            for (const QContact &c : d->m_localContactChanges[collection.id()].unmodifiedContacts) {
                if (c.id() == r.id()) {
                    foundMatch = true;
                    handledContactIds.insert(c.id());
                    bool identical = false;
                    QContact resolved = resolveConflictingChanges(c, r, &identical);
                    if (!identical) {
                        setContactChangeFlags(resolved, QContactStatusFlags::IsModified);
                        remoteModifications.append(resolved);
                    }
                    break;
                }
            }
            if (!foundMatch) {
                QContact modified = r;
                setContactChangeFlags(modified, QContactStatusFlags::IsModified);
                remoteModifications.append(modified);
            }
        }
    }

    if (collection.id().isNull() || !haveLocalChanges) {
        // no local changes exist to push to the server.
        storeRemoteChangesLocally(collection,
                                  remoteAdditions,
                                  remoteModifications,
                                  remoteRemovals);
    } else {
        // cache the remote changes (which need to be applied locally)
        // while we push the local changes to the server.
        TwoWayContactSyncAdaptorPrivate::ContactChanges remoteChanges;
        remoteChanges.addedContacts = remoteAdditions;
        remoteChanges.modifiedContacts = remoteModifications;
        remoteChanges.removedContacts = remoteRemovals;
        d->m_remoteContactChanges.insert(collection.id(), remoteChanges);

        const TwoWayContactSyncAdaptorPrivate::ContactChanges &localChanges(d->m_localContactChanges[collection.id()]);
        if (!storeLocalChangesRemotely(
                collection,
                localChanges.addedContacts,
                localChanges.modifiedContacts,
                localChanges.removedContacts)) {
            qWarning() << "Failed to push local changes to remote server for collection "
                       << QString::fromLatin1(collection.id().localId())
                       << "for application: " << d->m_applicationName
                       << " for account: " << d->m_accountId;
            syncOperationError();
        }
    }

    // we no longer need the cache of local changes for this collection.
    if (!collection.id().isNull()) {
        d->m_localContactChanges.remove(collection.id());
    }
}

bool TwoWayContactSyncAdaptor::storeLocalChangesRemotely(
        const QContactCollection &collection,
        const QList<QContact> &addedContacts,
        const QList<QContact> &modifiedContacts,
        const QList<QContact> &deletedContacts)
{
    // The plugin must implement this method to store
    // information about contacts to an addressbook on the remote server,
    // and then call localChangesStoredRemotely() once complete
    // (or syncOperationError() if an error occurred).
    qWarning() << "TWCSA::storeLocalChangesRemotely(): implementation missing";
    return false;
}

void TwoWayContactSyncAdaptor::localChangesStoredRemotely(
        const QContactCollection &collection,
        const QList<QContact> &addedContacts,
        const QList<QContact> &modifiedContacts)
{
    // here we get back the updated collection and contacts
    // (e.g. may have updated ctag and etag values).
    {
        TwoWayContactSyncAdaptorPrivate::ContactChanges &remoteChanges(d->m_remoteContactChanges[collection.id()]);

        // every local addition cannot previously have been represented
        // in the remote change set.  Thus, we can mark this as a
        // remote modification (i.e. with updated etag / guid / etc).
        for (const QContact &c : addedContacts) {
            QContact modified = c;
            setContactChangeFlags(modified, QContactStatusFlags::IsModified);
            remoteChanges.modifiedContacts.append(modified);
        }

        // a local modification might already be represented
        // as a remote modification, in which case we need to replace it.
        for (const QContact &c : modifiedContacts) {
            bool foundMatch = false;
            QList<QContact>::iterator it = remoteChanges.modifiedContacts.begin(),
                                     end = remoteChanges.modifiedContacts.end();
            while (it != end) {
                if (c.id() == it->id()) {
                    foundMatch = true;
                    QContact modified = c;
                    setContactChangeFlags(modified, QContactStatusFlags::IsModified);
                    *it = modified; // overwrite with the updated content
                    break;
                }
                it++;
            }

            if (!foundMatch) {
                QContact modified = c;
                setContactChangeFlags(modified, QContactStatusFlags::IsModified);
                remoteChanges.modifiedContacts.append(modified);
            }
        }

        // store the final results locally.
        storeRemoteChangesLocally(collection,
                                  remoteChanges.addedContacts,
                                  remoteChanges.modifiedContacts,
                                  remoteChanges.removedContacts);
    }

    // we no longer need the cache of remote changes for this collection.
    d->m_remoteContactChanges.remove(collection.id());
}

void TwoWayContactSyncAdaptor::storeRemoteChangesLocally(
        const QContactCollection &collection,
        const QList<QContact> &addedContacts,
        const QList<QContact> &modifiedContacts,
        const QList<QContact> &deletedContacts)
{
    if (collection.id().isNull()) {
        // remote collection addition.
        Q_ASSERT(modifiedContacts.isEmpty());
        Q_ASSERT(deletedContacts.isEmpty());
        QHash<QContactCollection *, QList<QContact>*> remotelyAddedCollections;
        QContactCollection remotelyAddedCollection = collection;
        QList<QContact> remotelyAddedContacts = addedContacts;
        remotelyAddedCollections.insert(&remotelyAddedCollection, &remotelyAddedContacts);
        QContactManager::Error err = QContactManager::NoError;
        if (!d->m_engine->storeChanges(&remotelyAddedCollections, nullptr, QList<QContactCollectionId>(),
                                       ContactManagerEngine::PreserveLocalChanges,
                                       true, &err)) {
            qWarning() << "Failed to store remotely added collection to local database for collection "
                       << collection.metaData(QContactCollection::KeyName).toString()
                       << "for application: " << d->m_applicationName
                       << " for account: " << d->m_accountId;
            syncOperationError();
            return;
        }
    } else {
        // update existing collection contents.
        QHash<QContactCollection *, QList<QContact>*> remotelyModifiedCollections;
        QList<QContact> changes = addedContacts + modifiedContacts + deletedContacts;
        QContactCollection remotelyModifiedCollection = collection;
        remotelyModifiedCollections.insert(&remotelyModifiedCollection, &changes);
        QContactManager::Error err = QContactManager::NoError;
        if (!d->m_engine->storeChanges(nullptr, &remotelyModifiedCollections, QList<QContactCollectionId>(),
                                       ContactManagerEngine::PreserveLocalChanges,
                                       true, &err)) {
            qWarning() << "Failed to store remote collection modifications to local database for collection "
                       << QString::fromLatin1(collection.id().localId())
                       << "for application: " << d->m_applicationName
                       << " for account: " << d->m_accountId;
            syncOperationError();
            return;
        }
    }

    performNextQueuedOperation();
}

bool TwoWayContactSyncAdaptor::removeAllCollections()
{
    if (d->m_busy) {
        qWarning() << Q_FUNC_INFO << "busy with ongoing sync!  cannot remove collections!";
        return false;
    }

    if (!d->m_engine) {
        qWarning() << Q_FUNC_INFO << "no connection to qtcontacts-sqlite";
        return false;
    }

    d->m_busy = true;

    const QList<QContactCollection> allCollections = contactManager().collections();
    QList<QContactCollectionId> removeCollectionIds;
    for (const QContactCollection &collection : allCollections) {
        if (collection.extendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_ACCOUNTID).toInt() == d->m_accountId
                && collection.extendedMetaData(COLLECTION_EXTENDEDMETADATA_KEY_APPLICATIONNAME).toString() == d->m_applicationName) {
            removeCollectionIds.append(collection.id());
        }
    }

    QContactManager::Error err = QContactManager::NoError;
    if (!d->m_engine->storeChanges(
            nullptr, nullptr, removeCollectionIds,
            ContactManagerEngine::PreserveRemoteChanges,
            true, &err)) {
        qWarning() << "Failed to remove contact addressbooks for "
                   << d->m_applicationName << " for deleted account:"
                   << d->m_accountId;
        d->m_busy = false;
        return false;
    }

    d->m_busy = false;
    return true;
}

const QContactManager &TwoWayContactSyncAdaptor::contactManager() const
{
    return *d->m_manager;
}

QContactManager &TwoWayContactSyncAdaptor::contactManager()
{
    return *d->m_manager;
}

// Note: this implementation can be overridden if the sync adapter knows
// that the remote service doesn't support some detail or field types,
// and thus these details and fields should not be inspected during
// conflict resolution.
TwoWayContactSyncAdaptor::IgnorableDetailsAndFields
TwoWayContactSyncAdaptor::ignorableDetailsAndFields() const
{
    TwoWayContactSyncAdaptor::IgnorableDetailsAndFields ignorable;

    // Note: we may still upsync these ignorable details+fields, just don't look at them during delta detection.
    // We need to do this, otherwise there can be infinite loops caused due to spurious differences between the
    // in-memory version (QContact) and the exportable version (vCard) resulting in ETag updates server-side.
    // The downside is that changes to these details will not be upsynced unless another change also occurs.
    QSet<QContactDetail::DetailType> ignorableDetailTypes = defaultIgnorableDetailTypes();
    ignorableDetailTypes.insert(QContactDetail::TypeGender);   // ignore differences in X-GENDER field when detecting delta.
    ignorableDetailTypes.insert(QContactDetail::TypeFavorite); // ignore differences in X-FAVORITE field when detecting delta.
    ignorableDetailTypes.insert(QContactDetail::TypeAvatar);   // ignore differences in PHOTO field when detecting delta.
    QHash<QContactDetail::DetailType, QSet<int> > ignorableDetailFields = defaultIgnorableDetailFields();
    ignorableDetailFields[QContactDetail::TypeAddress] << QContactAddress::FieldSubTypes;         // and ADR subtypes
    ignorableDetailFields[QContactDetail::TypePhoneNumber] << QContactPhoneNumber::FieldSubTypes; // and TEL number subtypes
    ignorableDetailFields[QContactDetail::TypeUrl] << QContactUrl::FieldSubType;                  // and URL subtype

    ignorable.detailTypes = ignorableDetailTypes;
    ignorable.detailFields = ignorableDetailFields;
    ignorable.commonFields = defaultIgnorableCommonFields();

    return ignorable;
}

// Note: this implementation can be overridden if the sync adapter knows
// more about how to resolve conflicts (eg persistent detail ids)
QContact TwoWayContactSyncAdaptor::resolveConflictingChanges(
        const QContact &local,
        const QContact &remote,
        bool *identical)
{
    // first, remove duplicate details from both a and b.
    bool detailIsDuplicate = false;
    QList<QContactDetail> ldets = local.details(), rdets = remote.details();
    QList<QContactDetail> nonDupLdets, nonDupRdets;
    while (!ldets.isEmpty()) {
        detailIsDuplicate = false;
        QContactDetail d = ldets.takeLast();
        Q_FOREACH (const QContactDetail &otherd, ldets) {
            if (otherd == d) {
                detailIsDuplicate = true;
                break;
            }
        }
        if (!detailIsDuplicate) {
            nonDupLdets.append(d);
        }
    }
    while (!rdets.isEmpty()) {
        detailIsDuplicate = false;
        QContactDetail d = rdets.takeLast();
        Q_FOREACH (const QContactDetail &otherd, rdets) {
            if (otherd == d) {
                detailIsDuplicate = true;
                break;
            }
        }
        if (!detailIsDuplicate) {
            nonDupRdets.append(d);
        }
    }

    // second, attempt to apply the flagged modifications from local to the resolved contact.
    // any details which remain in the remote detail list should also be saved in the resolved contact.
    QContact resolved, localWithoutDeletedDetails;
    for (int i = nonDupLdets.size() - 1; i >= 0; --i) {
        QContactDetail &ldet(nonDupLdets[i]);
        const quint32 localDetailDbId = ldet.value(QContactDetail__FieldDatabaseId).toUInt();
        const int localDetailChangeFlag = ldet.value(QContactDetail__FieldChangeFlags).toInt();
        if ((localDetailChangeFlag & QContactDetail__ChangeFlag_IsDeleted) == 0) {
            localWithoutDeletedDetails.saveDetail(&ldet, QContact::IgnoreAccessConstraints);
        }

        // apply detail additions directly.
        if (((localDetailChangeFlag & QContactDetail__ChangeFlag_IsAdded) > 0)
                && ((localDetailChangeFlag & QContactDetail__ChangeFlag_IsDeleted) == 0)) {
            ldet.removeValue(QContactDetail__FieldChangeFlags);
            resolved.saveDetail(&ldet, QContact::IgnoreAccessConstraints);
            continue;
        }

        // if the sync adapter provides the persistent detail database ids
        // as detail field values, we can apply modifications and deletions directly.
        if (((localDetailChangeFlag & QContactDetail__ChangeFlag_IsModified) > 0)
                || ((localDetailChangeFlag & QContactDetail__ChangeFlag_IsDeleted) > 0)) {
            for (int j = nonDupRdets.size() - 1; j >= 0; --j) {
                const QContactDetail &rdet(nonDupRdets[j]);
                const quint32 remoteDetailDbId = rdet.value(QContactDetail__FieldDatabaseId).toUInt();
                if (ldet.type() == rdet.type()
                        && (localDetailDbId > 0 && localDetailDbId == remoteDetailDbId)) {
                    if ((localDetailChangeFlag & QContactDetail__ChangeFlag_IsModified) > 0) {
                        // note: this will clobber the remote detail if it was also modified.
                        ldet.removeValue(QContactDetail__FieldChangeFlags);
                        nonDupRdets.replace(j, ldet);
                    } else { // QContactDetail__ChangeFlag_IsDeleted
                        nonDupRdets.removeAt(j);
                    }
                    break;
                }
            }
        }
    }

    // any remaining details from the remote should also be stored into the resolved contact.
    // note that we need to ensure that unique details (name etc) are replaced if they already exist.
    const QSet<QContactDetail::DetailType> uniqueDetailTypes {
        QContactDetail::TypeDisplayLabel,
        QContactDetail::TypeGender,
        QContactDetail::TypeGlobalPresence,
        QContactDetail::TypeGuid,
        QContactDetail::TypeName,
        QContactDetail::TypeSyncTarget,
        QContactDetail::TypeTimestamp,
    };
    for (int j = nonDupRdets.size() - 1; j >= 0; --j) {
        QContactDetail &rdet(nonDupRdets[j]);
        if (uniqueDetailTypes.contains(rdet.type()) && resolved.details(rdet.type()).size()) {
            QContactDetail existing = resolved.detail(rdet.type());
            existing.setValues(rdet.values());
            resolved.saveDetail(&existing, QContact::IgnoreAccessConstraints);
        } else {
            resolved.saveDetail(&rdet, QContact::IgnoreAccessConstraints);
        }
    }

    // set the id as appropriate into the resolved contact.
    resolved.setId(local.id());
    resolved.setCollectionId(local.collectionId());

    // after applying the delta from the local to the remote as best we can,
    // check to see if the resolved contact is identical to the local contact
    // (after removing deleted details from the local contact).
    TwoWayContactSyncAdaptor::IgnorableDetailsAndFields ignorable = ignorableDetailsAndFields();
    *identical = exactContactMatchExistsInList(
            resolved, QList<QContact>() << localWithoutDeletedDetails,
            ignorable.detailTypes,
            ignorable.detailFields,
            ignorable.commonFields,
            true) >= 0;

    return resolved;
}

void TwoWayContactSyncAdaptor::syncFinishedSuccessfully()
{
    // The plugin must implement this method appropriately.
    // Usually this will mean emitting some signal which is
    // handled by the sync framework, etc.
    qWarning() << "TWCSA::syncFinishedSuccessfully(): implementation missing";
}

void TwoWayContactSyncAdaptor::syncFinishedWithError()
{
    // The plugin must implement this method appropriately.
    // Usually this will mean emitting some signal which is
    // handled by the sync framework, etc.
    qWarning() << "TWCSA::syncFinishedWithError(): implementation missing";
}

void TwoWayContactSyncAdaptor::syncOperationError()
{
    // Plugins should invoke this if the most recent operation
    // failed (e.g. network request, etc).
    d->m_errorOccurred = true;
    if (d->m_continueAfterError) {
        performNextQueuedOperation();
    } else {
        d->m_busy = false;
        syncFinishedWithError();
    }
}

#endif // TWOWAYCONTACTSYNCADAPTOR_IMPL_H

