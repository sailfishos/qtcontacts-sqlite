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

#ifndef QTCONTACTSSQLITE_CONTACTSENGINE
#define QTCONTACTSSQLITE_CONTACTSENGINE

#include "contactmanagerengine.h"

#include <QScopedPointer>
#include <QSqlDatabase>
#include <QObject>
#include <QList>
#include <QMap>
#include <QString>

#include "contactsdatabase.h"
#include "contactnotifier.h"
#include "contactreader.h"
#include "contactwriter.h"

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

    QString managerName() const override;
    QMap<QString, QString> managerParameters() const override;
    QMap<QString, QString> idInterpretationParameters() const override;
    int managerVersion() const override;

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
            const QContactId &participantId,
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

    QContactCollectionId defaultCollectionId() const override;
    QContactCollection collection(const QContactCollectionId &collectionId, QContactManager::Error *error) const override;
    QList<QContactCollection> collections(QContactManager::Error *error) const override;
    bool saveCollection(QContactCollection *collection, QContactManager::Error *error) override;
    bool removeCollection(const QContactCollectionId &collectionId, QContactManager::Error *error) override;
    bool saveCollections(QList<QContactCollection> *collections, QMap<int, QContactManager::Error> *errorMap, QContactManager::Error *error); // non-override.
    bool removeCollections(const QList<QContactCollectionId> &collectionIds, QMap<int, QContactManager::Error> *errorMap, QContactManager::Error *error); // non-override.

    void requestDestroyed(QContactAbstractRequest* req) override;
    void requestDestroyed(QObject* request) override;
    bool startRequest(QContactAbstractRequest* req) override;
    bool startRequest(QContactDetailFetchRequest* request) override;
    bool startRequest(QContactCollectionChangesFetchRequest* request) override;
    bool startRequest(QContactChangesFetchRequest* request) override;
    bool startRequest(QContactChangesSaveRequest* request) override;
    bool startRequest(QContactClearChangeFlagsRequest* request) override;
    bool cancelRequest(QContactAbstractRequest* req) override;
    bool cancelRequest(QObject* request) override;
    bool waitForRequestFinished(QContactAbstractRequest* req, int msecs) override;
    bool waitForRequestFinished(QObject* req, int msecs) override;

    bool isRelationshipTypeSupported(const QString &relationshipType, QContactType::TypeValues contactType) const override;
    QList<QContactType::TypeValues> supportedContactTypes() const override;

    void regenerateDisplayLabel(QContact &contact, bool *emitDisplayLabelGroupChange);

    bool clearChangeFlags(const QList<QContactId> &contactIds, QContactManager::Error *error) override;
    bool clearChangeFlags(const QContactCollectionId &collectionId, QContactManager::Error *error) override;

    bool fetchCollectionChanges(int accountId,
                                const QString &applicationName,
                                QList<QContactCollection> *addedCollections,
                                QList<QContactCollection> *modifiedCollections,
                                QList<QContactCollection> *deletedCollections,
                                QList<QContactCollection> *unmodifiedCollections,
                                QContactManager::Error *error) override;

    bool fetchContactChanges(const QContactCollectionId &collectionId,
                             QList<QContact> *addedContacts,
                             QList<QContact> *modifiedContacts,
                             QList<QContact> *deletedContacts,
                             QList<QContact> *unmodifiedContacts,
                             QContactManager::Error *error) override;

    bool storeChanges(QHash<QContactCollection*, QList<QContact> * /* added contacts */> *addedCollections,
                      QHash<QContactCollection*, QList<QContact> * /* added/modified/deleted contacts */> *modifiedCollections,
                      const QList<QContactCollectionId> &deletedCollections,
                      ConflictResolutionPolicy conflictResolutionPolicy,
                      bool clearChangeFlags,
                      QContactManager::Error *error) override;

    bool fetchOOB(const QString &scope, const QString &key, QVariant *value) override;
    bool fetchOOB(const QString &scope, const QStringList &keys, QMap<QString, QVariant> *values) override;
    bool fetchOOB(const QString &scope, QMap<QString, QVariant> *values) override;

    bool fetchOOBKeys(const QString &scope, QStringList *keys) override;

    bool storeOOB(const QString &scope, const QString &key, const QVariant &value) override;
    bool storeOOB(const QString &scope, const QMap<QString, QVariant> &values) override;

    bool removeOOB(const QString &scope, const QString &key) override;
    bool removeOOB(const QString &scope, const QStringList &keys) override;
    bool removeOOB(const QString &scope) override;

    QStringList displayLabelGroups() override;

    QString synthesizedDisplayLabel(const QContact &contact, QContactManager::Error *error) const;
    static bool setContactDisplayLabel(QContact *contact, const QString &label, const QString &group, int sortOrder);
    static QString normalizedPhoneNumber(const QString &input);

private slots:
    void _q_collectionsAdded(const QVector<quint32> &collectionIds);
    void _q_collectionsChanged(const QVector<quint32> &collectionIds);
    void _q_collectionsRemoved(const QVector<quint32> &collectionIds);
    void _q_collectionContactsChanged(const QVector<quint32> &collectionIds);
    void _q_contactsChanged(const QVector<quint32> &contactIds);
    void _q_contactsPresenceChanged(const QVector<quint32> &contactIds);
    void _q_contactsAdded(const QVector<quint32> &contactIds);
    void _q_contactsRemoved(const QVector<quint32> &contactIds);
    void _q_selfContactIdChanged(quint32,quint32);
    void _q_relationshipsAdded(const QVector<quint32> &contactIds);
    void _q_relationshipsRemoved(const QVector<quint32> &contactIds);
    void _q_displayLabelGroupsChanged();

private:
    bool regenerateAggregatesIfNeeded();
    QString databaseUuid();
    ContactsDatabase &database();

    ContactReader *reader() const;
    ContactWriter *writer();

    QString m_databaseUuid;
    const QString m_name;
    QMap<QString, QString> m_parameters;
    QString m_managerUri;
    QScopedPointer<ContactsDatabase> m_database;
    mutable QScopedPointer<ContactReader> m_synchronousReader;
    QScopedPointer<ContactWriter> m_synchronousWriter;
    QScopedPointer<ContactNotifier> m_notifier;
    QScopedPointer<JobThread> m_jobThread;

    Q_DISABLE_COPY(ContactsEngine);
};

#endif

