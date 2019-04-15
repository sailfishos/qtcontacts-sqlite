/*
 * Copyright (C) 2013 Jolla Ltd. <mattthew.vogt@jollamobile.com>
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

    virtual bool Q_DECL_DEPRECATED fetchSyncContacts(const QString &syncTarget, const QDateTime &lastSync, const QList<QContactId> &exportedIds,
                                   QList<QContact> *syncContacts, QList<QContact> *addedContacts, QList<QContactId> *deletedContactIds,
                                   QContactManager::Error *error) = 0; // DEPRECATED
    virtual bool fetchSyncContacts(const QString &syncTarget, const QDateTime &lastSync, const QList<QContactId> &exportedIds,
                                   QList<QContact> *syncContacts, QList<QContact> *addedContacts, QList<QContactId> *deletedContactIds,
                                   QDateTime *maxTimestamp, QContactManager::Error *error) = 0;

    virtual bool Q_DECL_DEPRECATED storeSyncContacts(const QString &syncTarget, ConflictResolutionPolicy conflictPolicy,
                                   const QList<QPair<QContact, QContact> > &remoteChanges, QContactManager::Error *error) = 0;
    virtual bool storeSyncContacts(const QString &syncTarget, ConflictResolutionPolicy conflictPolicy,
                                   QList<QPair<QContact, QContact> > *remoteChanges, QContactManager::Error *error) = 0;

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

Q_SIGNALS:
    void contactsPresenceChanged(const QList<QContactId> &contactsIds);
    void syncContactsChanged(const QStringList &syncTargets);
    void displayLabelGroupsChanged(const QStringList &groups);

protected:
    bool m_nonprivileged;
    bool m_mergePresenceChanges;
    bool m_autoTest;
};

}

#endif
