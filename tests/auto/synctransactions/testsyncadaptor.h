/*
 * Copyright (C) 2014 - 2015 Jolla Ltd.
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

#ifndef TESTSYNCADAPTOR_H
#define TESTSYNCADAPTOR_H

#include "../../../src/extensions/twowaycontactsyncadaptor.h"
#include <QContactManager>
#include <QContact>
#include <QTimer>
#include <QDateTime>
#include <QList>
#include <QPair>
#include <QMap>

QTCONTACTS_USE_NAMESPACE

class TestSyncAdaptor : public QObject, public QtContactsSqliteExtensions::TwoWayContactSyncAdaptor
{
    Q_OBJECT

public:
    TestSyncAdaptor(int accountId, const QString &applicationName, QContactManager &manager, QObject *parent = 0);
    ~TestSyncAdaptor();

    enum PhoneModifiability {
        ImplicitlyModifiable = 0,
        ExplicitlyModifiable,
        ExplicitlyNonModifiable
    };

    // for testing purposes
    void addRemoteContact(const QString &fname, const QString &lname, const QString &phone, PhoneModifiability mod = ImplicitlyModifiable);
    void removeRemoteContact(const QString &fname, const QString &lname);
    QContact setRemoteContact(const QString &fname, const QString &lname, const QContact &contact);
    void changeRemoteContactPhone(const QString &fname, const QString &lname, const QString &modPhone);
    void changeRemoteContactEmail(const QString &fname, const QString &lname, const QString &modEmail);
    void changeRemoteContactName(const QString &fname, const QString &lname, const QString &modfname, const QString &modlname);
    void addRemoteDuplicates(const QString &fname, const QString &lname, const QString &phone);
    void mergeRemoteDuplicates();

    // triggering sync and checking state.
    void performTwoWaySync();
    bool upsyncWasRequired() const;
    bool downsyncWasRequired() const;
    QContact remoteContact(const QString &fname, const QString &lname) const;
    QSet<QContactId> modifiedIds() const;

Q_SIGNALS:
    void finished();
    void failed();

protected:
    // implementing the TWCSA interface
    bool determineRemoteCollections();
    bool deleteRemoteCollection(const QContactCollection &collection);
    bool determineRemoteContacts(const QContactCollection &collection);
    bool storeLocalChangesRemotely(
            const QContactCollection &collection,
            const QList<QContact> &addedContacts,
            const QList<QContact> &modifiedContacts,
            const QList<QContact> &deletedContacts);
    void syncFinishedSuccessfully();
    void syncFinishedWithError();

private:
    void cleanUp();
    int m_accountId;
    QString m_applicationName;

    // we simulate 3 collections:
    // one of which is empty and is read-only
    // one of which has content and is read-only
    // one of which has content and is read-write
    QContactCollection m_emptyCollection;
    QContactCollection m_readOnlyCollection;
    QContactCollection m_readWriteCollection;
    bool m_readWriteCollectionDeleted = false;

    // the readonly non-empty collection has two fixed contacts:
    QContact m_alice;
    QContact m_bob;

    // simulating server-side changes:
    mutable bool m_downsyncWasRequired = false;
    mutable bool m_upsyncWasRequired = false;
    mutable QList<QContact> m_remoteDeletions;
    mutable QSet<QString> m_remoteAdditions; // guids used to lookup into m_remoteServerContacts
    mutable QSet<QString> m_remoteModifications; // guids used to lookup into m_remoteServerContacts
    mutable QMap<QString, QContact> m_remoteServerContacts; // guid to contact
    mutable QSet<QContactId> m_modifiedIds;
    mutable QMultiMap<QString, QString> m_remoteServerDuplicates; // originalGuid to duplicateGuids.
};

#endif
