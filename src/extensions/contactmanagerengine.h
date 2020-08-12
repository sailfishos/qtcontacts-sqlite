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

#ifndef CONTACTMANAGERENGINE_H
#define CONTACTMANAGERENGINE_H

#include <QContactManagerEngine>

QT_BEGIN_NAMESPACE_CONTACTS
class QContactDetailFetchRequest;
class QContactChangesFetchRequest;
class QContactCollectionChangesFetchRequest;
class QContactChangesSaveRequest;
class QContactClearChangeFlagsRequest;
QT_END_NAMESPACE_CONTACTS

QTCONTACTS_USE_NAMESPACE

namespace QtContactsSqliteExtensions {

/*
 * Parameters recognized by the qtcontact-sqlite engine include:
 *  'mergePresenceChanges' - if true, contact presence changes will be merged with other changes,
 *                           and reported via the contactsChanged signal. Otherwise presence
 *                           changes will be reported separately, via the contactsPresenceChanged
 *                           signal of the QContactManager's engine object.
 *  'nonprivileged'        - if true, the engine will not attempt to use the privileged database
 *                           of contact details, which is not accessible to normal processes. Otherwise
 *                           the privileged database will be preferred if accessible.
 *  'autoTest'             - if true, an alternate database path is accessed, separate to the
 *                           path used by non-auto-test applications
 */

class Q_DECL_EXPORT ContactManagerEngine
    : public QContactManagerEngine
{
    Q_OBJECT

public:
    enum ConflictResolutionPolicy {
        PreserveLocalChanges,
        PreserveRemoteChanges
    };

    ContactManagerEngine() : m_nonprivileged(false), m_mergePresenceChanges(false), m_autoTest(false) {}

    void setNonprivileged(bool b) { m_nonprivileged = b; }
    void setMergePresenceChanges(bool b) { m_mergePresenceChanges = b; }
    void setAutoTest(bool b) { m_autoTest = b; }


    virtual bool clearChangeFlags(const QList<QContactId> &contactIds, QContactManager::Error *error) = 0;
    virtual bool clearChangeFlags(const QContactCollectionId &collectionId, QContactManager::Error *error) = 0;

    // doesn't cause a transaction
    virtual bool fetchCollectionChanges(int accountId,
                                        const QString &applicationName,
                                        QList<QContactCollection> *addedCollections,
                                        QList<QContactCollection> *modifiedCollections,
                                        QList<QContactCollection> *deletedCollections,
                                        QList<QContactCollection> *unmodifiedCollections,
                                        QContactManager::Error *error) = 0;

    // causes a transaction: sets Collection.recordUnhandledChangeFlags, clears Contact+Detail.unhandledChangeFlags
    virtual bool fetchContactChanges(const QContactCollectionId &collectionId,
                                     QList<QContact> *addedContacts,
                                     QList<QContact> *modifiedContacts,
                                     QList<QContact> *deletedContacts,
                                     QList<QContact> *unmodifiedContacts,
                                     QContactManager::Error *error) = 0;

    // causes a transaction
    virtual bool storeChanges(QHash<QContactCollection*, QList<QContact> * /* added contacts */> *addedCollections,
                              QHash<QContactCollection*, QList<QContact> * /* added/modified/deleted contacts */> *modifiedCollections,
                              const QList<QContactCollectionId> &deletedCollections,
                              ConflictResolutionPolicy conflictResolutionPolicy,
                              bool clearChangeFlags,
                              QContactManager::Error *error) = 0;

    virtual bool fetchOOB(const QString &scope, const QString &key, QVariant *value) = 0;
    virtual bool fetchOOB(const QString &scope, const QStringList &keys, QMap<QString, QVariant> *values) = 0;
    virtual bool fetchOOB(const QString &scope, QMap<QString, QVariant> *values) = 0;

    virtual bool fetchOOBKeys(const QString &scope, QStringList *keys) = 0;

    virtual bool storeOOB(const QString &scope, const QString &key, const QVariant &value) = 0;
    virtual bool storeOOB(const QString &scope, const QMap<QString, QVariant> &values) = 0;

    virtual bool removeOOB(const QString &scope, const QString &key) = 0;
    virtual bool removeOOB(const QString &scope, const QStringList &keys) = 0;
    virtual bool removeOOB(const QString &scope) = 0;

    virtual QStringList displayLabelGroups() = 0;

    virtual void requestDestroyed(QObject* request) = 0;
    virtual bool startRequest(QContactDetailFetchRequest* request) = 0;
    virtual bool startRequest(QContactCollectionChangesFetchRequest* request) = 0;
    virtual bool startRequest(QContactChangesFetchRequest* request) = 0;
    virtual bool startRequest(QContactChangesSaveRequest* request) = 0;
    virtual bool startRequest(QContactClearChangeFlagsRequest* request) = 0;
    virtual bool cancelRequest(QObject* request) = 0;
    virtual bool waitForRequestFinished(QObject* req, int msecs) = 0;

Q_SIGNALS:
    void contactsPresenceChanged(const QList<QContactId> &contactsIds);
    void collectionContactsChanged(const QList<QContactCollectionId> &collectionIds);
    void displayLabelGroupsChanged(const QStringList &groups);

protected:
    bool m_nonprivileged;
    bool m_mergePresenceChanges;
    bool m_autoTest;
};

}

#endif
