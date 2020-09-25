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

#ifndef TWOWAYCONTACTSYNCADAPTOR_H
#define TWOWAYCONTACTSYNCADAPTOR_H

#include <QDateTime>
#include <QString>
#include <QHash>
#include <QList>
#include <QPair>
#include <QMap>
#include <QSet>

#include <QContactManager>
#include <QContactDetail>
#include <QContact>
#include <QContactCollection>

QTCONTACTS_USE_NAMESPACE

namespace QtContactsSqliteExtensions {
class TwoWayContactSyncAdaptorPrivate;
class TwoWayContactSyncAdaptor
{
public:
    enum ErrorHandlingMode {
        ExitUponError,
        ContinueAfterError
    };

    TwoWayContactSyncAdaptor(int accountId = 0, const QString &applicationName = QString());
    TwoWayContactSyncAdaptor(int accountId, const QString &applicationName, const QMap<QString, QString> &params);
    TwoWayContactSyncAdaptor(int accountId, const QString &applicationName, QContactManager &mananger);
    virtual ~TwoWayContactSyncAdaptor();

    void setManager(QContactManager &manager);

    // step two: start complete sync cycle
    // - determine collection metadata changes made on remote server
    // - determine collection metadata changes made on local device
    // - for each locally-existent collection (which was not deleted remotely),
    //   trigger per-collection sync cycle.
    virtual bool startSync(ErrorHandlingMode mode = ExitUponError);
    virtual bool determineRemoteCollections();
    virtual void remoteCollectionsDetermined( // if the plugin doesn't support retrieving remote deltas
            const QList<QContactCollection> &remoteCollections);
    virtual bool determineRemoteCollectionChanges(
            const QList<QContactCollection> &locallyAddedCollections,
            const QList<QContactCollection> &locallyModifiedCollections,
            const QList<QContactCollection> &locallyRemovedCollections,
            const QList<QContactCollection> &locallyUnmodifiedCollections,
            QContactManager::Error *error);
    virtual void remoteCollectionChangesDetermined(
            const QList<QContactCollection> &remotelyAddedCollections,
            const QList<QContactCollection> &remotelyModifiedCollections,
            const QList<QContactCollection> &remotelyRemovedCollections,
            const QList<QContactCollection> &remotelyUnmodifiedCollections);

    // step three: delete remotely-deleted collections from local database
    virtual bool storeRemoteCollectionDeletionsLocally(const QList<QContactCollectionId> &collectionIds);

    // step four: perform per-collection sync cycle
    // - if the collection was deleted locally, push the deletion to the server
    // - if the collection was added locally, push it (and its contents) to the server
    // - otherwise (modified or unmodified):
    // - determine per-collection contact changes made on local device
    // - determine per-collection contact changes made on remote server
    // - calculate "final result" by performing conflict resolution etc to the two change sets
    // - push local collection metadata changes to remote server
    // - push "final result" contact data to remote server
    // - save "final result" contact data + collection metadata changes (incl ctag) to local.
    virtual void startCollectionSync(const QContactCollection &collection, int changeFlag = 0); // hrm, need the following cases: added locally, modified locally, deleted locally, added remotely.
    virtual bool deleteRemoteCollection(const QContactCollection &collection);
    virtual void remoteCollectionDeleted(const QContactCollection &collection);
    virtual bool determineRemoteContacts(const QContactCollection &collection);
    virtual void remoteContactsDetermined( // if the plugin doesn't support retrieving remote deltas
            const QContactCollection &collection,
            const QList<QContact> &contacts);
    virtual bool determineRemoteContactChanges(
            const QContactCollection &collection,
            const QList<QContact> &localAddedContacts,
            const QList<QContact> &localModifiedContacts,
            const QList<QContact> &localDeletedContacts,
            const QList<QContact> &localUnmodifiedContacts,
            QContactManager::Error *error);
    virtual void remoteContactChangesDetermined(
            const QContactCollection &collection,
            const QList<QContact> &remotelyAddedContacts,
            const QList<QContact> &remotelyModifiedContacts,
            const QList<QContact> &remotelyRemovedContacts);
    virtual bool storeLocalChangesRemotely(
            const QContactCollection &collection,
            const QList<QContact> &addedContacts,
            const QList<QContact> &modifiedContacts,
            const QList<QContact> &deletedContacts);
    virtual void localChangesStoredRemotely( // params will include updated ctag/etags etc.
            const QContactCollection &collection,
            const QList<QContact> &addedContacts,
            const QList<QContact> &modifiedContacts);
    virtual void storeRemoteChangesLocally(
            const QContactCollection &collection,
            const QList<QContact> &addedContacts,
            const QList<QContact> &modifiedContacts,
            const QList<QContact> &deletedContacts);

    // if the account is deleted, the sync plugin needs to purge all related collections.
    bool removeAllCollections();
    const QContactManager &contactManager() const;
    QContactManager &contactManager();

protected:
    friend class TwoWayContactSyncAdaptorPrivate;

    void performNextQueuedOperation();


    struct IgnorableDetailsAndFields {
        QSet<QContactDetail::DetailType> detailTypes;
        QHash<QContactDetail::DetailType, QSet<int> > detailFields;
        QSet<int> commonFields;
    };
    virtual IgnorableDetailsAndFields ignorableDetailsAndFields() const;
    virtual QContact resolveConflictingChanges(const QContact &local, const QContact &remote, bool *identical);

    virtual void syncFinishedSuccessfully();
    virtual void syncFinishedWithError();
    virtual void syncOperationError();

    TwoWayContactSyncAdaptorPrivate *d;
};

} // namespace QtContactsSqliteExtensions

#endif // TWOWAYCONTACTSYNCADAPTOR_H
