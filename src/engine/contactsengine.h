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

#ifndef QTCONTACTSSQLITE_CONTACTSENGINE
#define QTCONTACTSSQLITE_CONTACTSENGINE

#include "contactmanagerengine.h"

#include <QScopedPointer>
#include <QSqlDatabase>

#include "contactsdatabase.h"
#include "contactnotifier.h"
#include "contactreader.h"
#include "contactwriter.h"
#include "contactid_p.h"

// QList<int> is widely used in qtpim
Q_DECLARE_METATYPE(QList<int>)

QTCONTACTS_USE_NAMESPACE

// Force an ambiguity with QContactDetail::operator== so that we can't call it
// It does not compare correctly if the values contains QList<int>
inline void operator==(const QContactDetail &, const QContactDetail &) {}

class JobThread;

class ContactsEngine : public QtContactsSqliteExtensions::ContactManagerEngine
{
    Q_OBJECT
public:
    ContactsEngine(const QString &name, const QMap<QString, QString> &parameters);
    ~ContactsEngine();

    QContactManager::Error open();

    QString managerName() const;
    QMap<QString, QString> managerParameters() const;
    int managerVersion() const;

    QList<QContactId> contactIds(
                const QContactFilter &filter,
                const QList<QContactSortOrder> &sortOrders,
                QContactManager::Error* error) const override;
    QList<QContact> contacts(
                const QList<QContactId> &localIds,
                const QContactFetchHint &fetchHint,
                QMap<int, QContactManager::Error> *errorMap,
                QContactManager::Error *error) const override;
    QContact contact(
            const QContactId &contactId,
            const QContactFetchHint &fetchHint,
            QContactManager::Error* error) const override;

    QList<QContact> contacts(
                const QContactFilter &filter,
                const QList<QContactSortOrder> &sortOrders,
                const QContactFetchHint &fetchHint,
                QContactManager::Error* error) const override;
    QList<QContact> contacts(
                const QContactFilter &filter,
                const QList<QContactSortOrder> &sortOrders,
                const QContactFetchHint &fetchHint,
                QMap<int, QContactManager::Error> *errorMap,
                QContactManager::Error *error) const;
    bool saveContacts(
                QList<QContact> *contacts,
                QMap<int, QContactManager::Error> *errorMap,
                QContactManager::Error *error) override;
    bool saveContacts(
                QList<QContact> *contacts,
                const ContactWriter::DetailList &definitionMask,
                QMap<int, QContactManager::Error> *errorMap,
                QContactManager::Error *error) override;
    bool removeContact(const QContactId& contactId, QContactManager::Error* error);
    bool removeContacts(
                const QList<QContactId> &contactIds,
                QMap<int, QContactManager::Error> *errorMap,
                QContactManager::Error* error) override;

    QContactId selfContactId(QContactManager::Error* error) const override;
    bool setSelfContactId(const QContactId& contactId, QContactManager::Error* error) override;

    QList<QContactRelationship> relationships(
            const QString &relationshipType,
            const QContact &participant,
            QContactRelationship::Role role,
            QContactManager::Error *error) const override;
    bool saveRelationships(
            QList<QContactRelationship> *relationships,
            QMap<int, QContactManager::Error> *errorMap,
            QContactManager::Error *error) override;
    bool removeRelationships(
            const QList<QContactRelationship> &relationships,
            QMap<int, QContactManager::Error> *errorMap,
            QContactManager::Error *error) override;

    void requestDestroyed(QContactAbstractRequest* req) override;
    bool startRequest(QContactAbstractRequest* req) override;
    bool cancelRequest(QContactAbstractRequest* req) override;
    bool waitForRequestFinished(QContactAbstractRequest* req, int msecs) override;

    bool isRelationshipTypeSupported(const QString &relationshipType, QContactType::TypeValues contactType) const override;
    QList<QContactType::TypeValues> supportedContactTypes() const override;

    void regenerateDisplayLabel(QContact &contact) const;

    bool fetchSyncContacts(const QString &syncTarget, const QDateTime &lastSync, const QList<QContactId> &exportedIds,
                           QList<QContact> *syncContacts, QList<QContact> *addedContacts, QList<QContactId> *deletedContactIds,
                           QContactManager::Error *error);
    bool fetchSyncContacts(const QString &syncTarget, const QDateTime &lastSync, const QList<QContactId> &exportedIds,
                           QList<QContact> *syncContacts, QList<QContact> *addedContacts, QList<QContactId> *deletedContactIds,
                           QDateTime *maxTimestamp, QContactManager::Error *error);

    bool storeSyncContacts(const QString &syncTarget, ConflictResolutionPolicy conflictPolicy,
                           const QList<QPair<QContact, QContact> > &remoteChanges, QContactManager::Error *error);
    bool storeSyncContacts(const QString &syncTarget, ConflictResolutionPolicy conflictPolicy,
                           QList<QPair<QContact, QContact> > *remoteChanges, QContactManager::Error *error);

    bool fetchOOB(const QString &scope, const QString &key, QVariant *value);
    bool fetchOOB(const QString &scope, const QStringList &keys, QMap<QString, QVariant> *values);
    bool fetchOOB(const QString &scope, QMap<QString, QVariant> *values);

    bool fetchOOBKeys(const QString &scope, QStringList *keys);

    bool storeOOB(const QString &scope, const QString &key, const QVariant &value);
    bool storeOOB(const QString &scope, const QMap<QString, QVariant> &values);

    bool removeOOB(const QString &scope, const QString &key);
    bool removeOOB(const QString &scope, const QStringList &keys);
    bool removeOOB(const QString &scope);

    static bool setContactDisplayLabel(QContact *contact, const QString &label);

    static QString normalizedPhoneNumber(const QString &input);

    QString synthesizedDisplayLabel(const QContact &contact, QContactManager::Error *error) const;

private slots:
    void _q_contactsChanged(const QVector<quint32> &contactIds);
    void _q_contactsPresenceChanged(const QVector<quint32> &contactIds);
    void _q_syncContactsChanged(const QStringList &syncTargets);
    void _q_contactsAdded(const QVector<quint32> &contactIds);
    void _q_contactsRemoved(const QVector<quint32> &contactIds);
    void _q_selfContactIdChanged(quint32,quint32);
    void _q_relationshipsAdded(const QVector<quint32> &contactIds);
    void _q_relationshipsRemoved(const QVector<quint32> &contactIds);

private:
    QString databaseUuid();
    ContactsDatabase &database();

    ContactReader *reader() const;
    ContactWriter *writer();

    QString m_databaseUuid;
    const QString m_name;
    QMap<QString, QString> m_parameters;
    QScopedPointer<ContactsDatabase> m_database;
    mutable QScopedPointer<ContactReader> m_synchronousReader;
    QScopedPointer<ContactWriter> m_synchronousWriter;
    QScopedPointer<ContactNotifier> m_notifier;
    QScopedPointer<JobThread> m_jobThread;

    Q_DISABLE_COPY(ContactsEngine);
};

#endif

