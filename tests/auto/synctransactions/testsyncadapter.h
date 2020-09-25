/*
 * Copyright (C) 2014 Jolla Ltd.
 * Contact: Chris Adams <chris.adams@jolla.com>
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

#ifndef TESTSYNCADAPTER_H
#define TESTSYNCADAPTER_H

#include "../../../src/extensions/twowaycontactsyncadapter.h"
#include <QContactManager>
#include <QContact>
#include <QTimer>
#include <QDateTime>
#include <QList>
#include <QPair>
#include <QMap>

QTCONTACTS_USE_NAMESPACE

class TestSyncAdapter : public QObject, public QtContactsSqliteExtensions::TwoWayContactSyncAdapter
{
    Q_OBJECT

public:
    TestSyncAdapter(const QString &accountId, QObject *parent = 0);
    ~TestSyncAdapter();

    enum PhoneModifiability {
        ImplicitlyModifiable = 0,
        ExplicitlyModifiable,
        ExplicitlyNonModifiable
    };

    // for testing purposes
    void addRemoteContact(const QString &accountId, const QString &fname, const QString &lname, const QString &phone, PhoneModifiability mod = ImplicitlyModifiable);
    void removeRemoteContact(const QString &accountId, const QString &fname, const QString &lname);
    void setRemoteContact(const QString &accountId, const QString &fname, const QString &lname, const QContact &contact);
    void changeRemoteContactPhone(const QString &accountId, const QString &fname, const QString &lname, const QString &modPhone);
    void changeRemoteContactEmail(const QString &accountId, const QString &fname, const QString &lname, const QString &modEmail);
    void changeRemoteContactName(const QString &accountId, const QString &fname, const QString &lname, const QString &modfname, const QString &modlname);
    void addRemoteDuplicates(const QString &accountId, const QString &fname, const QString &lname, const QString &phone);
    void mergeRemoteDuplicates(const QString &accountId);

    // triggering sync and checking state.
    void performTwoWaySync(const QString &accountId);
    bool upsyncWasRequired(const QString &accountId) const;
    bool downsyncWasRequired(const QString &accountId) const;
    QContact remoteContact(const QString &accountId, const QString &fname, const QString &lname) const;
    QSet<QContactId> modifiedIds(const QString &accountId) const;

Q_SIGNALS:
    void finished();
    void failed();

protected:
    // implementing the TWCSA interface
    void determineRemoteChanges(const QDateTime &remoteSince,
                                const QString &accountId);

    bool testAccountProvenance(const QContact &contact, const QString &accountId);
    void upsyncLocalChanges(const QDateTime &localSince,
                            const QList<QContact> &locallyAdded,
                            const QList<QContact> &locallyModified,
                            const QList<QContact> &locallyDeleted,
                            const QString &accountId);

    // simulating asynchronous network operations
private Q_SLOTS:
    void continueTwoWaySync();
    void finalizeTwoWaySync();

private:
    void cleanUp(const QString &accountId);
    QString m_accountId;
    // simulating server-side changes, per account:
    mutable QMap<QString, QTimer*> m_simulationTimers;
    mutable QMap<QString, bool> m_downsyncWasRequired;
    mutable QMap<QString, bool> m_upsyncWasRequired;
    mutable QMap<QString, QDateTime> m_remoteSince;
    mutable QMap<QString, QList<QContact> > m_remoteDeletions;
    mutable QMap<QString, QSet<QString> > m_remoteAddMods; // used to lookup into m_remoteServerContacts
    mutable QMap<QString, QMap<QString, QContact> > m_remoteServerContacts;
    mutable QMap<QString, QSet<QContactId> > m_modifiedIds;
    mutable QMap<QString, QMultiMap<QString, QString> > m_remoteServerDuplicates; // accountId to originalGuid to duplicateGuids.
};

#endif
