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

#include "contactsengine.h"

#include "contactsdatabase.h"
#include "contactnotifier.h"
#include "contactreader.h"
#include "contactwriter.h"
#include "trace_p.h"

#include "qtcontacts-extensions.h"
#include "qtcontacts-extensions_impl.h"
#include "qcontactdetailfetchrequest_p.h"
#include "qcontactcollectionchangesfetchrequest_p.h"
#include "qcontactchangesfetchrequest_p.h"
#include "qcontactchangessaverequest_p.h"
#include "qcontactclearchangeflagsrequest_p.h"
#include "displaylabelgroupgenerator.h"

#include <QCoreApplication>
#include <QMutex>
#include <QThread>
#include <QWaitCondition>
#include <QElapsedTimer>
#include <QUuid>
#include <QDataStream>

#include <QContactCollection>
#include <QContact>
#include <QContactAbstractRequest>
#include <QContactFetchRequest>
#include <QContactCollectionFetchRequest>
#include <QContactCollectionFilter>

// ---- for schema modification ------
#include <QtContacts/QContactDisplayLabel>
#include <QtContacts/QContactFamily>
#include <QtContacts/QContactGeoLocation>
#include <QtContacts/QContactFavorite>
#include <QtContacts/QContactAddress>
#include <QtContacts/QContactEmailAddress>
#include <QtContacts/QContactPhoneNumber>
#include <QtContacts/QContactRingtone>
#include <QtContacts/QContactPresence>
#include <QtContacts/QContactGlobalPresence>
#include <QtContacts/QContactName>
// -----------------------------------

#include <QtDebug>

class Job
{
public:
    struct WriterProxy {
        ContactsEngine &engine;
        ContactsDatabase &database;
        ContactNotifier &notifier;
        ContactReader &reader;
        mutable ContactWriter *writer;

        WriterProxy(ContactsEngine &e, ContactsDatabase &db, ContactNotifier &n, ContactReader &r)
            : engine(e), database(db), notifier(n), reader(r), writer(0)
        {
        }

        ContactWriter *operator->() const
        {
            if (!writer) {
                writer = new ContactWriter(engine, database, &notifier, &reader);
            }
            return writer;
        }
    };

    Job()
    {
    }

    virtual ~Job()
    {
    }

    virtual QObject *request() = 0;
    virtual void clear() = 0;

    virtual void execute(ContactReader *reader, WriterProxy &writer) = 0;
    virtual void update(QMutex *) {}
    virtual void updateState(QContactAbstractRequest::State state) = 0;
    virtual void setError(QContactManager::Error) {}

    virtual void contactsAvailable(const QList<QContact> &) {}
    virtual void contactIdsAvailable(const QList<QContactId> &) {}
    virtual void collectionsAvailable(const QList<QContactCollection> &) {}

    virtual QString description() const = 0;
    virtual QContactManager::Error error() const = 0;
};

template <typename T>
class TemplateJob : public Job
{
public:
    TemplateJob(T *request)
        : m_request(request)
        , m_error(QContactManager::NoError)
    {
    }

    QObject *request()
    {
        return m_request;
    }

    void clear() override
    {
        m_request = 0;
    }

    QContactManager::Error error() const
    {
        return m_error;
    }

    void setError(QContactManager::Error error) override
    {
        m_error = error;
    }

protected:
    T *m_request;
    QContactManager::Error m_error;
};

class ContactSaveJob : public TemplateJob<QContactSaveRequest>
{
public:
    ContactSaveJob(QContactSaveRequest *request)
        : TemplateJob(request)
        , m_contacts(request->contacts())
        , m_definitionMask(request->typeMask())
    {
    }

    void execute(ContactReader *, WriterProxy &writer) override
    {
        m_error = writer->save(&m_contacts, m_definitionMask, 0, &m_errorMap, false, false, false);
    }

    void updateState(QContactAbstractRequest::State state) override
    {
         QContactManagerEngine::updateContactSaveRequest(
                     m_request, m_contacts, m_error, m_errorMap, state);
    }

    QString description() const override
    {
        QString s(QLatin1String("Save"));
        foreach (const QContact &c, m_contacts) {
            s.append(' ').append(ContactId::toString(c));
        }
        return s;
    }

private:
    QList<QContact> m_contacts;
    ContactWriter::DetailList m_definitionMask;
    QMap<int, QContactManager::Error> m_errorMap;
};

class ContactRemoveJob : public TemplateJob<QContactRemoveRequest>
{
public:
    ContactRemoveJob(QContactRemoveRequest *request)
        : TemplateJob(request)
        , m_contactIds(request->contactIds())
    {
    }

    void execute(ContactReader *, WriterProxy &writer) override
    {
        m_errorMap.clear();
        m_error = writer->remove(m_contactIds, &m_errorMap, false, false);
    }

    void updateState(QContactAbstractRequest::State state) override
    {
        QContactManagerEngine::updateContactRemoveRequest(
                m_request,
                m_error,
                m_errorMap,
                state);
    }

    QString description() const override
    {
        QString s(QLatin1String("Remove"));
        foreach (const QContactId &id, m_contactIds) {
            s.append(' ').append(ContactId::toString(id));
        }
        return s;
    }

private:
    QList<QContactId> m_contactIds;
    QMap<int, QContactManager::Error> m_errorMap;
};

class ContactFetchJob : public TemplateJob<QContactFetchRequest>
{
public:
    ContactFetchJob(QContactFetchRequest *request)
        : TemplateJob(request)
        , m_filter(request->filter())
        , m_fetchHint(request->fetchHint())
        , m_sorting(request->sorting())
    {
    }

    void execute(ContactReader *reader, WriterProxy &) override
    {
        QList<QContact> contacts;
        m_error = reader->readContacts(
                QLatin1String("AsynchronousFilter"),
                &contacts,
                m_filter,
                m_sorting,
                m_fetchHint);
    }

    void update(QMutex *mutex) override
    {
        QList<QContact> contacts;      {
            QMutexLocker locker(mutex);
            contacts = m_contacts;
        }
        QContactManagerEngine::updateContactFetchRequest(
                m_request,
                contacts,
                QContactManager::NoError,
                QContactAbstractRequest::ActiveState);
    }

    void updateState(QContactAbstractRequest::State state) override
    {
        QContactManagerEngine::updateContactFetchRequest(m_request, m_contacts, m_error, state);
    }

    void contactsAvailable(const QList<QContact> &contacts) override
    {
        m_contacts = contacts;
    }

    QString description() const override
    {
        QString s(QLatin1String("Fetch"));
        return s;
    }

private:
    QContactFilter m_filter;
    QContactFetchHint m_fetchHint;
    QList<QContactSortOrder> m_sorting;
    QList<QContact> m_contacts;
};

class IdFetchJob : public TemplateJob<QContactIdFetchRequest>
{
public:
    IdFetchJob(QContactIdFetchRequest *request)
        : TemplateJob(request)
        , m_filter(request->filter())
        , m_sorting(request->sorting())
    {
    }

    void execute(ContactReader *reader, WriterProxy &) override
    {
        QList<QContactId> contactIds;
        m_error = reader->readContactIds(&contactIds, m_filter, m_sorting);
    }

    void update(QMutex *mutex) override
    {
        QList<QContactId> contactIds;
        {
            QMutexLocker locker(mutex);
            contactIds = m_contactIds;
        }
        QContactManagerEngine::updateContactIdFetchRequest(
                m_request,
                contactIds,
                QContactManager::NoError,
                QContactAbstractRequest::ActiveState);
    }

    void updateState(QContactAbstractRequest::State state) override
    {
        QContactManagerEngine::updateContactIdFetchRequest(
                m_request, m_contactIds, m_error, state);
    }

    void contactIdsAvailable(const QList<QContactId> &contactIds) override
    {
        m_contactIds = contactIds;
    }

    QString description() const override
    {
        QString s(QLatin1String("Fetch IDs"));
        return s;
    }

private:
    QContactFilter m_filter;
    QList<QContactSortOrder> m_sorting;
    QList<QContactId> m_contactIds;
};

class ContactFetchByIdJob : public TemplateJob<QContactFetchByIdRequest>
{
public:
    ContactFetchByIdJob(QContactFetchByIdRequest *request)
        : TemplateJob(request)
        , m_contactIds(request->contactIds())
        , m_fetchHint(request->fetchHint())
    {
    }

    void execute(ContactReader *reader, WriterProxy &) override
    {
        QList<QContact> contacts;
        m_error = reader->readContacts(
                QLatin1String("AsynchronousIds"),
                &contacts,
                m_contactIds,
                m_fetchHint);
    }

    void update(QMutex *mutex) override
    {
        QList<QContact> contacts;
        {
            QMutexLocker locker(mutex);
            contacts = m_contacts;
        }
        QContactManagerEngine::updateContactFetchByIdRequest(
                m_request,
                contacts,
                QContactManager::NoError,
                QMap<int, QContactManager::Error>(),
                QContactAbstractRequest::ActiveState);
    }

    void updateState(QContactAbstractRequest::State state) override
    {
        QContactManagerEngine::updateContactFetchByIdRequest(
                m_request,
                m_contacts,
                m_error,
                QMap<int, QContactManager::Error>(),
                state);
    }

    void contactsAvailable(const QList<QContact> &contacts) override
    {
        m_contacts = contacts;
    }

    QString description() const override
    {
        QString s(QLatin1String("FetchByID"));
        foreach (const QContactId &id, m_contactIds) {
            s.append(' ').append(ContactId::toString(id));
        }
        return s;
    }

private:
    QList<QContactId> m_contactIds;
    QContactFetchHint m_fetchHint;
    QList<QContact> m_contacts;
};


class CollectionSaveJob : public TemplateJob<QContactCollectionSaveRequest>
{
public:
    CollectionSaveJob(QContactCollectionSaveRequest *request)
        : TemplateJob(request)
        , m_collections(request->collections())
    {
    }

    void execute(ContactReader *, WriterProxy &writer) override
    {
        m_error = writer->save(&m_collections, 0, &m_errorMap, false);
    }

    void updateState(QContactAbstractRequest::State state) override
    {
         QContactManagerEngine::updateCollectionSaveRequest(
                     m_request, m_collections, m_error, m_errorMap, state);
    }

    QString description() const override
    {
        QString s(QLatin1String("Save"));
        foreach (const QContactCollection &c, m_collections) {
            s.append(' ').append(ContactCollectionId::toString(c));
        }
        return s;
    }

private:
    QList<QContactCollection> m_collections;
    QMap<int, QContactManager::Error> m_errorMap;
};

class CollectionRemoveJob : public TemplateJob<QContactCollectionRemoveRequest>
{
public:
    CollectionRemoveJob(QContactCollectionRemoveRequest *request)
        : TemplateJob(request)
        , m_collectionIds(request->collectionIds())
    {
    }

    void execute(ContactReader *, WriterProxy &writer) override
    {
        m_errorMap.clear();
        m_error = writer->remove(m_collectionIds, &m_errorMap, false, false);
    }

    void updateState(QContactAbstractRequest::State state) override
    {
        QContactManagerEngine::updateCollectionRemoveRequest(
                m_request,
                m_error,
                m_errorMap,
                state);
    }

    QString description() const override
    {
        QString s(QLatin1String("Remove"));
        foreach (const QContactCollectionId &id, m_collectionIds) {
            s.append(' ').append(ContactCollectionId::toString(id));
        }
        return s;
    }

private:
    QList<QContactCollectionId> m_collectionIds;
    QMap<int, QContactManager::Error> m_errorMap;
};

class CollectionFetchJob : public TemplateJob<QContactCollectionFetchRequest>
{
public:
    CollectionFetchJob(QContactCollectionFetchRequest *request)
        : TemplateJob(request)
    {
    }

    void execute(ContactReader *reader, WriterProxy &) override
    {
        QList<QContactCollection> collections;
        m_error = reader->readCollections(
                QLatin1String("AsynchronousFilter"),
                &collections);
    }

    void update(QMutex *mutex) override
    {
        QList<QContactCollection> collections; {
            QMutexLocker locker(mutex);
            collections = m_collections;
        }
        QContactManagerEngine::updateCollectionFetchRequest(
                m_request,
                collections,
                QContactManager::NoError,
                QContactAbstractRequest::ActiveState);
    }

    void updateState(QContactAbstractRequest::State state) override
    {
        QContactManagerEngine::updateCollectionFetchRequest(m_request, m_collections, m_error, state);
    }

    void collectionsAvailable(const QList<QContactCollection> &collections) override
    {
        m_collections = collections;
    }

    QString description() const override
    {
        QString s(QLatin1String("CollectionFetch"));
        return s;
    }

private:
    QList<QContactCollection> m_collections;
};


class RelationshipSaveJob : public TemplateJob<QContactRelationshipSaveRequest>
{
public:
    RelationshipSaveJob(QContactRelationshipSaveRequest *request)
        : TemplateJob(request)
        , m_relationships(request->relationships())
    {
    }

    void execute(ContactReader *, WriterProxy &writer) override
    {
        m_error = writer->save(m_relationships, &m_errorMap, false, false);
    }

    void updateState(QContactAbstractRequest::State state) override
    {
         QContactManagerEngine::updateRelationshipSaveRequest(
                     m_request, m_relationships, m_error, m_errorMap, state);
    }

    QString description() const override
    {
        QString s(QLatin1String("Relationship Save"));
        return s;
    }

private:
    QList<QContactRelationship> m_relationships;
    QMap<int, QContactManager::Error> m_errorMap;
};

class RelationshipRemoveJob : public TemplateJob<QContactRelationshipRemoveRequest>
{
public:
    RelationshipRemoveJob(QContactRelationshipRemoveRequest *request)
        : TemplateJob(request)
        , m_relationships(request->relationships())
    {
    }

    void execute(ContactReader *, WriterProxy &writer) override
    {
        m_error = writer->remove(m_relationships, &m_errorMap, false);
    }

    void updateState(QContactAbstractRequest::State state) override
    {
        QContactManagerEngine::updateRelationshipRemoveRequest(
                m_request, m_error, m_errorMap, state);
    }

    QString description() const override
    {
        QString s(QLatin1String("Relationship Remove"));
        return s;
    }

private:
    QList<QContactRelationship> m_relationships;
    QMap<int, QContactManager::Error> m_errorMap;
};

class RelationshipFetchJob : public TemplateJob<QContactRelationshipFetchRequest>
{
public:
    RelationshipFetchJob(QContactRelationshipFetchRequest *request)
        : TemplateJob(request)
        , m_type(request->relationshipType())
        , m_first(request->first())
        , m_second(request->second())
    {
    }

    void execute(ContactReader *reader, WriterProxy &) override
    {
        m_error = reader->readRelationships(
                &m_relationships,
                m_type,
                m_first,
                m_second);
    }

    void updateState(QContactAbstractRequest::State state) override
    {
        QContactManagerEngine::updateRelationshipFetchRequest(
                m_request, m_relationships, m_error, state);
    }

    QString description() const override
    {
        QString s(QLatin1String("Relationship Fetch"));
        return s;
    }

private:
    QString m_type;
    QContactId m_first;
    QContactId m_second;
    QList<QContactRelationship> m_relationships;
};

class DetailFetchJob : public TemplateJob<QContactDetailFetchRequest>
{
public:
    DetailFetchJob(QContactDetailFetchRequest *request, QContactDetailFetchRequestPrivate *d)
        : TemplateJob(request)
        , m_filter(d->filter)
        , m_fetchHint(d->hint)
        , m_sorting(d->sorting)
        , m_fields(d->fields)
        , m_type(d->type)
    {
    }

    void execute(ContactReader *reader, WriterProxy &) override
    {
        m_error = reader->readDetails(
                &m_details,
                m_type,
                m_fields,
                m_filter,
                m_sorting,
                m_fetchHint);
    }

    void updateState(QContactAbstractRequest::State state) override
    {
        if (m_request) {
            QContactDetailFetchRequestPrivate * const d = QContactDetailFetchRequestPrivate::get(m_request);

            d->details = m_details;
            d->error = m_error;
            d->state = state;

            if (state == QContactAbstractRequest::FinishedState) {
                emit (m_request->*(d->resultsAvailable))();
            }
            emit (m_request->*(d->stateChanged))(state);
        }
    }

    QString description() const override
    {
        QString s(QLatin1String("Detail Fetch"));
        return s;
    }

private:
    const QContactFilter m_filter;
    const QContactFetchHint m_fetchHint;
    const QList<QContactSortOrder> m_sorting;
    const QList<int> m_fields;
    QList<QContactDetail> m_details;
    const QContactDetail::DetailType m_type;
};

class CollectionChangesFetchJob : public TemplateJob<QContactCollectionChangesFetchRequest>
{
public:
    CollectionChangesFetchJob(QContactCollectionChangesFetchRequest *request, QContactCollectionChangesFetchRequestPrivate *d)
        : TemplateJob(request)
        , m_accountId(d->accountId)
        , m_applicationName(d->applicationName)
        , m_addedCollections(d->addedCollections)
        , m_modifiedCollections(d->modifiedCollections)
        , m_removedCollections(d->removedCollections)
        , m_unmodifiedCollections(d->unmodifiedCollections)
    {
    }

    void execute(ContactReader *, WriterProxy &writer) override
    {
        m_error = writer->fetchCollectionChanges(
                m_accountId,
                m_applicationName,
                &m_addedCollections,
                &m_modifiedCollections,
                &m_removedCollections,
                &m_unmodifiedCollections);
    }

    void updateState(QContactAbstractRequest::State state) override
    {
        if (m_request) {
            QContactCollectionChangesFetchRequestPrivate * const d = QContactCollectionChangesFetchRequestPrivate::get(m_request);

            d->error = m_error;
            d->state = state;

            if (state == QContactAbstractRequest::FinishedState) {
                d->addedCollections = m_addedCollections;
                d->modifiedCollections = m_modifiedCollections;
                d->removedCollections = m_removedCollections;
                d->unmodifiedCollections = m_unmodifiedCollections;
                emit (m_request->*(d->resultsAvailable))();
            }
            emit (m_request->*(d->stateChanged))(state);
        }
    }

    QString description() const override
    {
        QString s(QLatin1String("Collection Changes Fetch"));
        return s;
    }

private:
    const int m_accountId;
    const QString m_applicationName;
    QList<QContactCollection> m_addedCollections;
    QList<QContactCollection> m_modifiedCollections;
    QList<QContactCollection> m_removedCollections;
    QList<QContactCollection> m_unmodifiedCollections;
};

class ContactChangesFetchJob : public TemplateJob<QContactChangesFetchRequest>
{
public:
    ContactChangesFetchJob(QContactChangesFetchRequest *request, QContactChangesFetchRequestPrivate *d)
        : TemplateJob(request)
        , m_collectionId(d->collectionId)
        , m_addedContacts(d->addedContacts)
        , m_modifiedContacts(d->modifiedContacts)
        , m_removedContacts(d->removedContacts)
        , m_unmodifiedContacts(d->unmodifiedContacts)
    {
    }

    void execute(ContactReader *, WriterProxy &writer) override
    {
        m_error = writer->fetchContactChanges(
                m_collectionId,
                &m_addedContacts,
                &m_modifiedContacts,
                &m_removedContacts,
                &m_unmodifiedContacts);
    }

    void updateState(QContactAbstractRequest::State state) override
    {
        if (m_request) {
            QContactChangesFetchRequestPrivate * const d = QContactChangesFetchRequestPrivate::get(m_request);

            d->error = m_error;
            d->state = state;

            if (state == QContactAbstractRequest::FinishedState) {
                d->addedContacts = m_addedContacts;
                d->modifiedContacts = m_modifiedContacts;
                d->removedContacts = m_removedContacts;
                d->unmodifiedContacts = m_unmodifiedContacts;
                emit (m_request->*(d->resultsAvailable))();
            }
            emit (m_request->*(d->stateChanged))(state);
        }
    }

    QString description() const override
    {
        QString s(QLatin1String("Collection Changes Fetch"));
        return s;
    }

private:
    const QContactCollectionId m_collectionId;
    QList<QContact> m_addedContacts;
    QList<QContact> m_modifiedContacts;
    QList<QContact> m_removedContacts;
    QList<QContact> m_unmodifiedContacts;
};

class ContactChangesSaveJob : public TemplateJob<QContactChangesSaveRequest>
{
public:
    ContactChangesSaveJob(QContactChangesSaveRequest *request, QContactChangesSaveRequestPrivate *d)
        : TemplateJob(request)
        , m_addedCollections(d->addedCollections)
        , m_modifiedCollections(d->modifiedCollections)
        , m_removedCollections(d->removedCollections)
        , m_policy(d->policy == QContactChangesSaveRequest::PreserveLocalChanges
                   ? QtContactsSqliteExtensions::ContactManagerEngine::PreserveLocalChanges
                   : QtContactsSqliteExtensions::ContactManagerEngine::PreserveRemoteChanges)
        , m_clearChangeFlags(d->clearChangeFlags)
    {
    }

    void execute(ContactReader *, WriterProxy &writer) override
    {
        QList<QContactCollection> collections;
        QList<QList<QContact> > contacts;
        QHash<QContactCollection*, QList<QContact>* > addedCollections_ptrs;
        QHash<QContactCollection*, QList<QContact>* > modifiedCollections_ptrs;

        // the storeSyncChanges method parameters are in+out parameters.
        // construct the appropriate data structures.
        QHash<int, int> addedCollectionsIndexes;
        QHash<int, int> modifiedCollectionsIndexes;
        QHash<QContactCollection, QList<QContact> >::iterator ait, aend, mit, mend;
        ait = m_addedCollections.begin(), aend = m_addedCollections.end();
        for ( ; ait != aend; ++ait) {
            addedCollectionsIndexes.insert(collections.size(), contacts.size());
            collections.append(ait.key());
            contacts.append(ait.value());
        }
        mit = m_modifiedCollections.begin(), mend = m_modifiedCollections.end();
        for ( ; mit != mend; ++mit) {
            modifiedCollectionsIndexes.insert(collections.size(), contacts.size());
            collections.append(mit.key());
            contacts.append(mit.value());
        }

        // do this as a second phase to avoid non-const operations causing potential detach
        // and thus invalidating our references.
        QHash<int, int>::const_iterator iit, iend;
        iit = addedCollectionsIndexes.constBegin(), iend = addedCollectionsIndexes.constEnd();
        for ( ; iit != iend; ++iit) {
            addedCollections_ptrs.insert(&collections[iit.key()], &contacts[iit.value()]);
        }
        iit = modifiedCollectionsIndexes.constBegin(), iend = modifiedCollectionsIndexes.constEnd();
        for ( ; iit != iend; ++iit) {
            modifiedCollections_ptrs.insert(&collections[iit.key()], &contacts[iit.value()]);
        }

        m_error = writer->storeChanges(
            &addedCollections_ptrs,
            &modifiedCollections_ptrs,
            m_removedCollections,
            m_policy,
            m_clearChangeFlags);

        if (m_error == QContactManager::NoError) {
            m_addedCollections.clear();
            QHash<QContactCollection*, QList<QContact>* >::iterator ait, aend, mit, mend;
            ait = addedCollections_ptrs.begin(), aend = addedCollections_ptrs.end();
            for ( ; ait != aend; ++ait) {
                m_addedCollections.insert(*ait.key(), *ait.value());
            }
            m_modifiedCollections.clear();
            mit = modifiedCollections_ptrs.begin(), mend = modifiedCollections_ptrs.end();
            for ( ; mit != mend; ++mit) {
                m_modifiedCollections.insert(*mit.key(), *mit.value());
            }
        }
    }

    void updateState(QContactAbstractRequest::State state) override
    {
        if (m_request) {
            QContactChangesSaveRequestPrivate * const d = QContactChangesSaveRequestPrivate::get(m_request);

            d->error = m_error;
            d->state = state;

            if (state == QContactAbstractRequest::FinishedState) {
                d->addedCollections = m_addedCollections;
                d->modifiedCollections = m_modifiedCollections;
                emit (m_request->*(d->resultsAvailable))();
            }
            emit (m_request->*(d->stateChanged))(state);
        }
    }

    QString description() const override
    {
        QString s(QLatin1String("Changes Save"));
        return s;
    }

private:
    QHash<QContactCollection, QList<QContact> > m_addedCollections;
    QHash<QContactCollection, QList<QContact> > m_modifiedCollections;
    QList<QContactCollectionId> m_removedCollections;
    const QtContactsSqliteExtensions::ContactManagerEngine::ConflictResolutionPolicy m_policy;
    const bool m_clearChangeFlags;
};

class ClearChangeFlagsJob : public TemplateJob<QContactClearChangeFlagsRequest>
{
public:
    ClearChangeFlagsJob(QContactClearChangeFlagsRequest *request, QContactClearChangeFlagsRequestPrivate *d)
        : TemplateJob(request)
        , m_collectionId(d->collectionId)
        , m_contactIds(d->contactIds)
    {
    }

    void execute(ContactReader *, WriterProxy &writer) override
    {
        m_error = m_collectionId.isNull()
                ? writer->clearChangeFlags(
                      m_contactIds,
                      false)
                : writer->clearChangeFlags(
                      m_collectionId,
                      false);
    }

    void updateState(QContactAbstractRequest::State state) override
    {
        if (m_request) {
            QContactClearChangeFlagsRequestPrivate * const d = QContactClearChangeFlagsRequestPrivate::get(m_request);

            d->error = m_error;
            d->state = state;

            if (state == QContactAbstractRequest::FinishedState) {
                emit (m_request->*(d->resultsAvailable))();
            }
            emit (m_request->*(d->stateChanged))(state);
        }
    }

    QString description() const override
    {
        QString s(QLatin1String("Clear Change Flags"));
        return s;
    }

private:
    const QContactCollectionId m_collectionId;
    const QList<QContactId> m_contactIds;
};

class JobThread : public QThread
{
    struct MutexUnlocker {
        QMutexLocker &m_locker;

        explicit MutexUnlocker(QMutexLocker &locker) : m_locker(locker)
        {
            m_locker.unlock();
        }
        ~MutexUnlocker()
        {
            m_locker.relock();
        }
    };

public:
    JobThread(ContactsEngine *engine, const QString &databaseUuid, bool nonprivileged, bool autoTest)
        : m_currentJob(0)
        , m_engine(engine)
        , m_database(engine)
        , m_databaseUuid(databaseUuid)
        , m_updatePending(false)
        , m_running(false)
        , m_nonprivileged(nonprivileged)
        , m_autoTest(autoTest)
    {
        start(QThread::IdlePriority);

        // Don't return until the started thread has indicated it is running
        QMutexLocker locker(&m_mutex);
        if (!m_running) {
            m_wait.wait(&m_mutex);
        }
    }

    ~JobThread()
    {
        {
            QMutexLocker locker(&m_mutex);
            m_running = false;
        }
        m_wait.wakeOne();
        wait();
    }

    void run();

    bool databaseOpen() const
    {
        return m_database.isOpen();
    }

    bool nonprivileged() const
    {
        return m_nonprivileged;
    }

    void enqueue(Job *job)
    {
        QMutexLocker locker(&m_mutex);
        m_pendingJobs.append(job);
        m_wait.wakeOne();
    }

    bool requestDestroyed(QObject *request)
    {
        QMutexLocker locker(&m_mutex);
        for (QList<Job*>::iterator it = m_pendingJobs.begin(); it != m_pendingJobs.end(); it++) {
            if ((*it)->request() == request) {
                delete *it;
                m_pendingJobs.erase(it);
                return true;
            }
        }

        if (m_currentJob && m_currentJob->request() == request) {
            m_currentJob->clear();
            return false;
        }

        for (QList<Job*>::iterator it = m_finishedJobs.begin(); it != m_finishedJobs.end(); it++) {
            if ((*it)->request() == request) {
                delete *it;
                m_finishedJobs.erase(it);
                return false;
            }
        }

        for (QList<Job*>::iterator it = m_cancelledJobs.begin(); it != m_cancelledJobs.end(); it++) {
            if ((*it)->request() == request) {
                delete *it;
                m_cancelledJobs.erase(it);
                return false;
            }
        } return false;
    }

    bool cancelRequest(QObject *request)
    {
        QMutexLocker locker(&m_mutex);
        for (QList<Job*>::iterator it = m_pendingJobs.begin(); it != m_pendingJobs.end(); it++) {
            if ((*it)->request() == request) {
                m_cancelledJobs.append(*it);
                m_pendingJobs.erase(it);
                return true;
            }
        }
        return false;
    }

    bool waitForFinished(QObject *request, const int msecs)
    {
        long timeout = msecs <= 0
                ? INT32_MAX
                : msecs;

        Job *finishedJob = 0;
        {
            QMutexLocker locker(&m_mutex);
            for (;;) {
                bool pendingJob = false;
                if (m_currentJob && m_currentJob->request() == request) {
                    QTCONTACTS_SQLITE_DEBUG(QString::fromLatin1("Wait for current job: %1 ms").arg(timeout));
                    // wait for the current job to updateState.
                    if (!m_finishedWait.wait(&m_mutex, timeout))
                        return false;
                } else for (int i = 0; i < m_pendingJobs.size(); i++) {
                    Job *job = m_pendingJobs[i];
                    if (job->request() == request) {
                        // If the job is pending, move it to the front of the queue and wait for
                        // the current job to end.
                        QElapsedTimer timer;
                        timer.start();
                        m_pendingJobs.move(i, 0);
                        if (!m_finishedWait.wait(&m_mutex, timeout))
                            return false;
                        timeout -= timer.elapsed();
                        if (timeout <= 0)
                            return false;
                        pendingJob = true;

                        break;
                    }
                }
                // Job is either finished, cancelled, or there is no job.
                if (!pendingJob)
                    break;
            }

            for (QList<Job*>::iterator it = m_finishedJobs.begin(); it != m_finishedJobs.end(); it++) {
                if ((*it)->request() == request) {
                    finishedJob = *it;
                    m_finishedJobs.erase(it);
                    break;
                }
            }
        }
        if (finishedJob) {
            finishedJob->updateState(QContactAbstractRequest::FinishedState);
            delete finishedJob;
            return true;
        } else for (QList<Job*>::iterator it = m_cancelledJobs.begin(); it != m_cancelledJobs.end(); it++) {
            if ((*it)->request() == request) {
                (*it)->updateState(QContactAbstractRequest::CanceledState);
                delete *it;
                m_cancelledJobs.erase(it);
                return true;
            }
        }
        return false;
    }

    void postUpdate()
    {
        if (!m_updatePending) {
            m_updatePending = true;
            QCoreApplication::postEvent(this, new QEvent(QEvent::UpdateRequest));
        }
    }

    void contactsAvailable(const QList<QContact> &contacts)
    {
        QMutexLocker locker(&m_mutex);
        m_currentJob->contactsAvailable(contacts);
        postUpdate();
    }

    void contactIdsAvailable(const QList<QContactId> &contactIds)
    {
        QMutexLocker locker(&m_mutex);
        m_currentJob->contactIdsAvailable(contactIds);
        postUpdate();
    }

    void collectionsAvailable(const QList<QContactCollection> &collections)
    {
        QMutexLocker locker(&m_mutex);
        m_currentJob->collectionsAvailable(collections);
        postUpdate();
    }

    bool event(QEvent *event)
    {
        if (event->type() == QEvent::UpdateRequest) {
            QList<Job*> finishedJobs;
            QList<Job*> cancelledJobs;
            Job *currentJob;
            {
                QMutexLocker locker(&m_mutex);
                finishedJobs = m_finishedJobs;
                cancelledJobs = m_cancelledJobs;
                m_finishedJobs.clear();
                m_cancelledJobs.clear();

                currentJob = m_currentJob;
                m_updatePending = false;
            }

            while (!finishedJobs.isEmpty()) {
                Job *job = finishedJobs.takeFirst();
                job->updateState(QContactAbstractRequest::FinishedState);
                delete job;
            }

            while (!cancelledJobs.isEmpty()) {
                Job *job = cancelledJobs.takeFirst();
                job->updateState(QContactAbstractRequest::CanceledState);
                delete job;
            }

            if (currentJob)
                currentJob->update(&m_mutex);
            return true;
        } else {
            return QThread::event(event);
        }
    }

private:
    QMutex m_mutex;
    QWaitCondition m_wait;
    QWaitCondition m_finishedWait;
    QList<Job*> m_pendingJobs;
    QList<Job*> m_finishedJobs;
    QList<Job*> m_cancelledJobs;
    Job *m_currentJob;
    ContactsEngine *m_engine;
    ContactsDatabase m_database;
    QString m_databaseUuid;
    bool m_updatePending;
    bool m_running;
    bool m_nonprivileged;
    bool m_autoTest;
};

class JobContactReader : public ContactReader
{
public:
    JobContactReader(ContactsDatabase &database, const QString &managerUri, JobThread *thread)
        : ContactReader(database, managerUri)
        , m_thread(thread)
    {
    }

    void contactsAvailable(const QList<QContact> &contacts) override
    {
        m_thread->contactsAvailable(contacts);
    }

    void contactIdsAvailable(const QList<QContactId> &contactIds) override
    {
        m_thread->contactIdsAvailable(contactIds);
    }

    void collectionsAvailable(const QList<QContactCollection> &collections) override
    {
        m_thread->collectionsAvailable(collections);
    }

private:
    JobThread *m_thread;
};

void JobThread::run()
{
    QString dbId(QStringLiteral("qtcontacts-sqlite%1-job-%2"));
    dbId = dbId.arg(m_autoTest ? QStringLiteral("-test") : QString()).arg(m_databaseUuid);

    QMutexLocker locker(&m_mutex);

    m_database.open(dbId, m_nonprivileged, m_autoTest);
    m_nonprivileged = m_database.nonprivileged();
    m_running = true;

    {
        MutexUnlocker unlocker(locker);
        m_wait.wakeOne();
    }

    if (!m_database.isOpen()) {
        while (m_running) {
            if (m_pendingJobs.isEmpty()) {
                m_wait.wait(&m_mutex);
            } else {
                m_currentJob = m_pendingJobs.takeFirst();
                m_currentJob->setError(QContactManager::UnspecifiedError);
                m_finishedJobs.append(m_currentJob);
                m_currentJob = 0;
                postUpdate();
                m_finishedWait.wakeOne();
            }
        }
    } else {
        ContactNotifier notifier(m_nonprivileged);
        JobContactReader reader(m_database, m_engine->managerUri(), this);
        Job::WriterProxy writer(*m_engine, m_database, notifier, reader);

        while (m_running) {
            if (m_pendingJobs.isEmpty()) {
                m_wait.wait(&m_mutex);
            } else {
                m_currentJob = m_pendingJobs.takeFirst();

                {
                    MutexUnlocker unlocker(locker);

                    QElapsedTimer timer;
                    timer.start();
                    m_currentJob->execute(&reader, writer);
                    QTCONTACTS_SQLITE_DEBUG(QString::fromLatin1("Job executed in %1 ms : %2 : error = %3")
                            .arg(timer.elapsed()).arg(m_currentJob->description()).arg(m_currentJob->error()));
                }

                m_finishedJobs.append(m_currentJob);
                m_currentJob = 0;
                postUpdate();
                m_finishedWait.wakeOne();
            }
        }
    }
}

ContactsEngine::ContactsEngine(const QString &name, const QMap<QString, QString> &parameters)
    : m_name(name)
    , m_parameters(parameters)
{
    static bool registered = qRegisterMetaType<QList<int> >("QList<int>") &&
                             qRegisterMetaType<QList<QContactDetail::DetailType> >("QList<QContactDetail::DetailType>") &&
                             qRegisterMetaTypeStreamOperators<QList<int> >();
    Q_UNUSED(registered)

    QString nonprivileged = m_parameters.value(QString::fromLatin1("nonprivileged"));
    if (nonprivileged.toLower() == QLatin1String("true") ||
        nonprivileged.toInt() == 1) {
        setNonprivileged(true);
    }

    QString mergePresenceChanges = m_parameters.value(QString::fromLatin1("mergePresenceChanges"));
    if (mergePresenceChanges.isEmpty()) {
        qWarning("The 'mergePresenceChanges' option has not been configured - presence changes will only be reported via ContactManagerEngine::contactsPresenceChanged()");
    } else if (mergePresenceChanges.toLower() == QLatin1String("true") ||
               mergePresenceChanges.toInt() == 1) {
        setMergePresenceChanges(true);
    }

    QString autoTest = m_parameters.value(QString::fromLatin1("autoTest"));
    if (autoTest.toLower() == QLatin1String("true") ||
        autoTest.toInt() == 1) {
        setAutoTest(true);
    }

    /* Store the engine into a property of QCoreApplication, so that it can be
     * retrieved by the extension code */
    QCoreApplication *app = QCoreApplication::instance();
    QList<QVariant> engines = app->property(CONTACT_MANAGER_ENGINE_PROP).toList();
    engines.append(QVariant::fromValue(this));
    app->setProperty(CONTACT_MANAGER_ENGINE_PROP, engines);

    m_managerUri = managerUri();
}

ContactsEngine::~ContactsEngine()
{
    QCoreApplication *app = QCoreApplication::instance();
    QList<QVariant> engines = app->property(CONTACT_MANAGER_ENGINE_PROP).toList();
    for (int i = 0; i < engines.size(); ++i) {
        QContactManagerEngine *engine = static_cast<QContactManagerEngine*>(engines[i].value<QObject*>());
        if (engine == this) {
            engines.removeAt(i);
            break;
        }
    }
    app->setProperty(CONTACT_MANAGER_ENGINE_PROP, engines);
}

QString ContactsEngine::databaseUuid()
{
    if (m_databaseUuid.isEmpty()) {
        m_databaseUuid = QUuid::createUuid().toString();
    }

    return m_databaseUuid;
}

QContactManager::Error ContactsEngine::open()
{
    // Start the async thread, and wait to see if it can open the database
    if (!m_jobThread) {
        m_jobThread.reset(new JobThread(this, databaseUuid(), m_nonprivileged, m_autoTest));

        if (m_jobThread->databaseOpen()) {
            // We may not have got privileged access if we requested it
            setNonprivileged(m_jobThread->nonprivileged());

            if (!m_notifier) {
                m_notifier.reset(new ContactNotifier(m_nonprivileged));
                m_notifier->connect("collectionsAdded", "au", this, SLOT(_q_collectionsAdded(QVector<quint32>)));
                m_notifier->connect("collectionsChanged", "au", this, SLOT(_q_collectionsChanged(QVector<quint32>)));
                m_notifier->connect("collectionsRemoved", "au", this, SLOT(_q_collectionsRemoved(QVector<quint32>)));
                m_notifier->connect("collectionContactsChanged", "au", this, SLOT(_q_collectionContactsChanged(QVector<quint32>)));
                m_notifier->connect("contactsAdded", "au", this, SLOT(_q_contactsAdded(QVector<quint32>)));
                m_notifier->connect("contactsChanged", "au", this, SLOT(_q_contactsChanged(QVector<quint32>)));
                m_notifier->connect("contactsPresenceChanged", "au", this, SLOT(_q_contactsPresenceChanged(QVector<quint32>)));
                m_notifier->connect("contactsRemoved", "au", this, SLOT(_q_contactsRemoved(QVector<quint32>)));
                m_notifier->connect("selfContactIdChanged", "uu", this, SLOT(_q_selfContactIdChanged(quint32,quint32)));
                m_notifier->connect("relationshipsAdded", "au", this, SLOT(_q_relationshipsAdded(QVector<quint32>)));
                m_notifier->connect("relationshipsRemoved", "au", this, SLOT(_q_relationshipsRemoved(QVector<quint32>)));
                m_notifier->connect("displayLabelGroupsChanged", "", this, SLOT(_q_displayLabelGroupsChanged()));
            }
        } else {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to open asynchronous engine database connection"));
        }
    }

    return m_jobThread->databaseOpen() ? QContactManager::NoError : QContactManager::UnspecifiedError;
}

QString ContactsEngine::managerName() const
{
    return m_name;
}

QMap<QString, QString> ContactsEngine::managerParameters() const
{
    return m_parameters;
}

QMap<QString, QString> ContactsEngine::idInterpretationParameters() const
{
    const bool nonprivileged = m_parameters.value(QString::fromLatin1("nonprivileged")).compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0
                            || m_parameters.value(QString::fromLatin1("nonprivileged")).compare(QStringLiteral("1"),    Qt::CaseInsensitive) == 0;
    const bool autoTest = m_parameters.value(QString::fromLatin1("autoTest")).compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0
                       || m_parameters.value(QString::fromLatin1("autoTest")).compare(QStringLiteral("1"),    Qt::CaseInsensitive) == 0;

    if (nonprivileged && autoTest) {
        return {
            { QString::fromLatin1("nonprivileged"), QString::fromLatin1("true") },
            { QString::fromLatin1("autoTest"),      QString::fromLatin1("true") }
        };
    } else if (nonprivileged) {
        return {
            { QString::fromLatin1("nonprivileged"), QString::fromLatin1("true") }
        };
    } else if (autoTest) {
        return {
            { QString::fromLatin1("autoTest"),      QString::fromLatin1("true") }
        };
    } else {
        return QMap<QString, QString>();
    }
}

int ContactsEngine::managerVersion() const
{
    return 1;
}

QList<QContactId> ContactsEngine::contactIds(
            const QContactFilter &filter,
            const QList<QContactSortOrder> &sortOrders,
            QContactManager::Error* error) const
{
    QList<QContactId> contactIds;

    QContactManager::Error err = reader()->readContactIds(&contactIds, filter, sortOrders);
    if (error)
        *error = err;
    return contactIds;
}

QList<QContact> ContactsEngine::contacts(
            const QContactFilter &filter,
            const QList<QContactSortOrder> &sortOrders,
            const QContactFetchHint &fetchHint,
            QContactManager::Error* error) const
{
    QList<QContact> contacts;

    QContactManager::Error err = reader()->readContacts(
                QLatin1String("SynchronousFilter"),
                &contacts,
                filter,
                sortOrders,
                fetchHint);
    if (error)
        *error = err;
    return contacts;
}

QList<QContact> ContactsEngine::contacts(
        const QContactFilter &filter,
        const QList<QContactSortOrder> &sortOrders,
        const QContactFetchHint &fetchHint,
        QMap<int, QContactManager::Error> *errorMap,
        QContactManager::Error *error) const
{
    Q_UNUSED(errorMap);

    return contacts(filter, sortOrders, fetchHint, error);
}

QList<QContact> ContactsEngine::contacts(
            const QList<QContactId> &localIds,
            const QContactFetchHint &fetchHint,
            QMap<int, QContactManager::Error> *errorMap,
            QContactManager::Error *error) const
{
    Q_UNUSED(errorMap);

    QList<QContact> contacts;

    QContactManager::Error err = reader()->readContacts(
                QLatin1String("SynchronousIds"),
                &contacts,
                localIds,
                fetchHint);
    if (error)
        *error = err;
    return contacts;
}

QContact ContactsEngine::contact(
        const QContactId &contactId,
        const QContactFetchHint &fetchHint,
        QContactManager::Error* error) const
{
    QMap<int, QContactManager::Error> errorMap;

    QList<QContact> contacts = ContactsEngine::contacts(
                QList<QContactId>() << contactId, fetchHint, &errorMap, error);
    return !contacts.isEmpty()
            ? contacts.first()
            : QContact();
}

bool ContactsEngine::saveContacts(
            QList<QContact> *contacts,
            QMap<int, QContactManager::Error> *errorMap,
            QContactManager::Error *error)
{
    return saveContacts(contacts, ContactWriter::DetailList(), errorMap, error);
}

bool ContactsEngine::saveContacts(
            QList<QContact> *contacts,
            const ContactWriter::DetailList &definitionMask,
            QMap<int, QContactManager::Error> *errorMap,
            QContactManager::Error *error)
{
    QContactManager::Error err = writer()->save(contacts, definitionMask, 0, errorMap, false, false, false);

    if (error)
        *error = err;
    return err == QContactManager::NoError;
}

bool ContactsEngine::removeContact(const QContactId &contactId, QContactManager::Error* error)
{
    QMap<int, QContactManager::Error> errorMap;

    return removeContacts(QList<QContactId>() << contactId, &errorMap, error);
}

bool ContactsEngine::removeContacts(
            const QList<QContactId> &contactIds,
            QMap<int, QContactManager::Error> *errorMap,
            QContactManager::Error* error)
{
    QContactManager::Error err = writer()->remove(contactIds, errorMap, false, false);
    if (error)
        *error = err;
    return err == QContactManager::NoError;
}

QContactId ContactsEngine::selfContactId(QContactManager::Error* error) const
{
    QContactId contactId;
    QContactManager::Error err = reader()->getIdentity(
            ContactsDatabase::SelfContactId, &contactId);
    if (error)
        *error = err;
    return contactId;
}

bool ContactsEngine::setSelfContactId(
        const QContactId&, QContactManager::Error* error)
{
    *error = QContactManager::NotSupportedError;
    return false;
}

QList<QContactRelationship> ContactsEngine::relationships(
        const QString &relationshipType,
        const QContactId &participantId,
        QContactRelationship::Role role,
        QContactManager::Error *error) const
{
    QContactId first = participantId;
    QContactId second;

    if (role == QContactRelationship::Second)
        qSwap(first, second);

    QList<QContactRelationship> relationships;
    QContactManager::Error err = reader()->readRelationships(
                &relationships, relationshipType, first, second);
    if (error)
        *error = err;
    return relationships;
}

bool ContactsEngine::saveRelationships(
        QList<QContactRelationship> *relationships,
        QMap<int, QContactManager::Error> *errorMap,
        QContactManager::Error *error)
{
    QContactManager::Error err = writer()->save(*relationships, errorMap, false, false);
    if (error)
        *error = err;

    if (err == QContactManager::NoError) {
        return true;
    }

    return false;
}

bool ContactsEngine::removeRelationships(
        const QList<QContactRelationship> &relationships,
        QMap<int, QContactManager::Error> *errorMap,
        QContactManager::Error *error)
{
    QContactManager::Error err = writer()->remove(relationships, errorMap, false);
    if (error)
        *error = err;
    return err == QContactManager::NoError;
}

QContactCollectionId ContactsEngine::defaultCollectionId() const
{
    QContactCollectionId collectionId;
    QContactManager::Error err = reader()->getCollectionIdentity(
            ContactsDatabase::LocalAddressbookCollectionId, &collectionId);
    return err == QContactManager::NoError ? collectionId : QContactCollectionId();
}

QContactCollection ContactsEngine::collection(
        const QContactCollectionId &collectionId,
        QContactManager::Error *error) const
{
    const QList<QContactCollection> collections = ContactsEngine::collections(error);

    if (*error == QContactManager::NoError) {
        for (const QContactCollection &collection : collections) {
            if (collection.id() == collectionId) {
                return collection;
            }
        }
        *error = QContactManager::DoesNotExistError;
    }

    return QContactCollection();
}

QList<QContactCollection> ContactsEngine::collections(
        QContactManager::Error *error) const
{
    QList<QContactCollection> collections;

    QContactManager::Error err = reader()->readCollections(
                QLatin1String("SynchronousFilter"),
                &collections);
    if (error)
        *error = err;
    return collections;
}

bool ContactsEngine::saveCollections(
        QList<QContactCollection> *collections,
        QMap<int, QContactManager::Error> *errorMap,
        QContactManager::Error *error)
{
    QContactManager::Error err = writer()->save(collections, errorMap, false, false);

    if (error)
        *error = err;
    return err == QContactManager::NoError;
}

bool ContactsEngine::saveCollection(
        QContactCollection *collection,
        QContactManager::Error *error)
{
    bool ret = false;

    if (collection) {
        QList<QContactCollection> collections;
        collections.append(*collection);

        QMap<int, QContactManager::Error> errorMap;
        ret = saveCollections(&collections, &errorMap, error);

        if (errorMap.size()) {
            *error = errorMap.constBegin().value();
        }

        *collection = collections.first();
    } else {
        *error = QContactManager::BadArgumentError;
    }

    return ret;
}

bool ContactsEngine::removeCollections(
        const QList<QContactCollectionId> &collectionIds,
        QMap<int, QContactManager::Error> *errorMap,
        QContactManager::Error *error)
{
    QContactManager::Error err = writer()->remove(collectionIds, errorMap, false, false);

    if (error)
        *error = err;
    return err == QContactManager::NoError;
}

bool ContactsEngine::removeCollection(
        const QContactCollectionId &collectionId,
        QContactManager::Error *error)
{
    QMap<int, QContactManager::Error> errorMap;
    return removeCollections(QList<QContactCollectionId>() << collectionId, &errorMap, error);
}

void ContactsEngine::requestDestroyed(QContactAbstractRequest* req)
{
    requestDestroyed(static_cast<QObject *>(req));
}

void ContactsEngine::requestDestroyed(QObject* req)
{
    if (m_jobThread)
        m_jobThread->requestDestroyed(req);
}


bool ContactsEngine::startRequest(QContactAbstractRequest* request)
{
    Job *job = 0;

    switch (request->type()) {
    case QContactAbstractRequest::ContactSaveRequest:
        job = new ContactSaveJob(qobject_cast<QContactSaveRequest *>(request));
        break;
    case QContactAbstractRequest::ContactRemoveRequest:
        job = new ContactRemoveJob(qobject_cast<QContactRemoveRequest *>(request));
        break;
    case QContactAbstractRequest::ContactFetchRequest:
        job = new ContactFetchJob(qobject_cast<QContactFetchRequest *>(request));
        break;
    case QContactAbstractRequest::ContactIdFetchRequest:
        job = new IdFetchJob(qobject_cast<QContactIdFetchRequest *>(request));
        break;
    case QContactAbstractRequest::ContactFetchByIdRequest:
        job = new ContactFetchByIdJob(qobject_cast<QContactFetchByIdRequest *>(request));
        break;
    case QContactAbstractRequest::RelationshipFetchRequest:
        job = new RelationshipFetchJob(qobject_cast<QContactRelationshipFetchRequest *>(request));
        break;
    case QContactAbstractRequest::RelationshipSaveRequest:
        job = new RelationshipSaveJob(qobject_cast<QContactRelationshipSaveRequest *>(request));
        break;
    case QContactAbstractRequest::RelationshipRemoveRequest:
        job = new RelationshipRemoveJob(qobject_cast<QContactRelationshipRemoveRequest *>(request));
        break;
    case QContactAbstractRequest::CollectionFetchRequest:
        job = new CollectionFetchJob(qobject_cast<QContactCollectionFetchRequest *>(request));
        break;
    case QContactAbstractRequest::CollectionSaveRequest:
        job = new CollectionSaveJob(qobject_cast<QContactCollectionSaveRequest *>(request));
        break;
    case QContactAbstractRequest::CollectionRemoveRequest:
        job = new CollectionRemoveJob(qobject_cast<QContactCollectionRemoveRequest *>(request));
        break;
    default:
        return false;
    }

    job->updateState(QContactAbstractRequest::ActiveState);
    m_jobThread->enqueue(job);

    return true;
}

bool ContactsEngine::startRequest(QContactDetailFetchRequest* request)
{
    Job *job = new DetailFetchJob(request, QContactDetailFetchRequestPrivate::get(request));

    job->updateState(QContactAbstractRequest::ActiveState);
    m_jobThread->enqueue(job);

    return true;
}

bool ContactsEngine::startRequest(QContactCollectionChangesFetchRequest* request)
{
    Job *job = new CollectionChangesFetchJob(request, QContactCollectionChangesFetchRequestPrivate::get(request));

    job->updateState(QContactAbstractRequest::ActiveState);
    m_jobThread->enqueue(job);

    return true;
}

bool ContactsEngine::startRequest(QContactChangesFetchRequest* request)
{
    Job *job = new ContactChangesFetchJob(request, QContactChangesFetchRequestPrivate::get(request));

    job->updateState(QContactAbstractRequest::ActiveState);
    m_jobThread->enqueue(job);

    return true;
}

bool ContactsEngine::startRequest(QContactChangesSaveRequest* request)
{
    Job *job = new ContactChangesSaveJob(request, QContactChangesSaveRequestPrivate::get(request));

    job->updateState(QContactAbstractRequest::ActiveState);
    m_jobThread->enqueue(job);

    return true;
}

bool ContactsEngine::startRequest(QContactClearChangeFlagsRequest* request)
{
    Job *job = new ClearChangeFlagsJob(request, QContactClearChangeFlagsRequestPrivate::get(request));

    job->updateState(QContactAbstractRequest::ActiveState);
    m_jobThread->enqueue(job);

    return true;
}

bool ContactsEngine::cancelRequest(QContactAbstractRequest* req)
{
    return cancelRequest(static_cast<QObject *>(req));
}

bool ContactsEngine::cancelRequest(QObject* req)
{
    if (m_jobThread)
        return m_jobThread->cancelRequest(req);

    return false;
}

bool ContactsEngine::waitForRequestFinished(QContactAbstractRequest* req, int msecs)
{
    return waitForRequestFinished(static_cast<QObject *>(req), msecs);
}

bool ContactsEngine::waitForRequestFinished(QObject* req, int msecs)
{
    if (m_jobThread)
        return m_jobThread->waitForFinished(req, msecs);
    return true;
}

bool ContactsEngine::isRelationshipTypeSupported(const QString &relationshipType, QContactType::TypeValues contactType) const
{
    Q_UNUSED(relationshipType);

    return contactType == QContactType::TypeContact;
}

QList<QContactType::TypeValues> ContactsEngine::supportedContactTypes() const
{
    return QList<QContactType::TypeValues>() << QContactType::TypeContact;
}

void ContactsEngine::regenerateDisplayLabel(QContact &contact, bool *emitDisplayLabelGroupChange)
{
    QContactManager::Error displayLabelError = QContactManager::NoError;
    const QString label = synthesizedDisplayLabel(contact, &displayLabelError);
    if (displayLabelError != QContactManager::NoError) {
        QTCONTACTS_SQLITE_DEBUG(QString::fromLatin1("Unable to regenerate displayLabel for contact: %1").arg(ContactId::toString(contact)));
    }

    QContact tempContact(contact);
    setContactDisplayLabel(&tempContact, label, QString(), -1);
    const QString group = m_database ? m_database->determineDisplayLabelGroup(tempContact, emitDisplayLabelGroupChange) : QString();
    const int sortOrder = m_database ? m_database->displayLabelGroupSortValue(group) : -1;
    setContactDisplayLabel(&contact, label, group, sortOrder);
}

bool ContactsEngine::clearChangeFlags(const QList<QContactId> &contactIds, QContactManager::Error *error)
{
    Q_ASSERT(error);
    *error = writer()->clearChangeFlags(contactIds, false);
    return (*error == QContactManager::NoError);
}

bool ContactsEngine::clearChangeFlags(const QContactCollectionId &collectionId, QContactManager::Error *error)
{
    Q_ASSERT(error);
    *error = writer()->clearChangeFlags(collectionId, false);
    return (*error == QContactManager::NoError);
}

bool ContactsEngine::fetchCollectionChanges(int accountId,
                                            const QString &applicationName,
                                            QList<QContactCollection> *addedCollections,
                                            QList<QContactCollection> *modifiedCollections,
                                            QList<QContactCollection> *deletedCollections,
                                            QList<QContactCollection> *unmodifiedCollections,
                                            QContactManager::Error *error)
{
    Q_ASSERT(error);
    *error = writer()->fetchCollectionChanges(accountId,
                                              applicationName,
                                              addedCollections,
                                              modifiedCollections,
                                              deletedCollections,
                                              unmodifiedCollections);
    return (*error == QContactManager::NoError);
}

bool ContactsEngine::fetchContactChanges(const QContactCollectionId &collectionId,
                                         QList<QContact> *addedContacts,
                                         QList<QContact> *modifiedContacts,
                                         QList<QContact> *deletedContacts,
                                         QList<QContact> *unmodifiedContacts,
                                         QContactManager::Error *error)
{
    Q_ASSERT(error);
    *error = writer()->fetchContactChanges(collectionId,
                                           addedContacts,
                                           modifiedContacts,
                                           deletedContacts,
                                           unmodifiedContacts);
    return (*error == QContactManager::NoError);
}

bool ContactsEngine::storeChanges(QHash<QContactCollection*, QList<QContact> * /* added contacts */> *addedCollections,
                                  QHash<QContactCollection*, QList<QContact> * /* added/modified/deleted contacts */> *modifiedCollections,
                                  const QList<QContactCollectionId> &deletedCollections,
                                  ConflictResolutionPolicy conflictResolutionPolicy,
                                  bool clearChangeFlags,
                                  QContactManager::Error *error)
{
    Q_ASSERT(error);
    *error = writer()->storeChanges(addedCollections,
                                    modifiedCollections,
                                    deletedCollections,
                                    conflictResolutionPolicy,
                                    clearChangeFlags);
    return (*error == QContactManager::NoError);
}

bool ContactsEngine::fetchOOB(const QString &scope, const QString &key, QVariant *value)
{
    QMap<QString, QVariant> values;
    if (reader()->fetchOOB(scope, QStringList() << key, &values)) {
        *value = values[key];
        return true;
    }

    return false;
}

bool ContactsEngine::fetchOOB(const QString &scope, const QStringList &keys, QMap<QString, QVariant> *values)
{
    return reader()->fetchOOB(scope, keys, values);
}

bool ContactsEngine::fetchOOB(const QString &scope, QMap<QString, QVariant> *values)
{
    return reader()->fetchOOB(scope, QStringList(), values);
}

bool ContactsEngine::fetchOOBKeys(const QString &scope, QStringList *keys)
{
    return reader()->fetchOOBKeys(scope, keys);
}

bool ContactsEngine::storeOOB(const QString &scope, const QString &key, const QVariant &value)
{
    QMap<QString, QVariant> values;
    values.insert(key, value);
    return writer()->storeOOB(scope, values);
}

bool ContactsEngine::storeOOB(const QString &scope, const QMap<QString, QVariant> &values)
{
    return writer()->storeOOB(scope, values);
}

bool ContactsEngine::removeOOB(const QString &scope, const QString &key)
{
    return writer()->removeOOB(scope, QStringList() << key);
}

bool ContactsEngine::removeOOB(const QString &scope, const QStringList &keys)
{
    return writer()->removeOOB(scope, keys);
}

bool ContactsEngine::removeOOB(const QString &scope)
{
    return writer()->removeOOB(scope, QStringList());
}

QStringList ContactsEngine::displayLabelGroups()
{
    return database().displayLabelGroups();
}

bool ContactsEngine::setContactDisplayLabel(QContact *contact, const QString &label, const QString &group, int sortOrder)
{
    QContactDisplayLabel detail(contact->detail<QContactDisplayLabel>());
    bool needSave = false;
    if (!label.trimmed().isEmpty()) {
        detail.setLabel(label);
        needSave = true;
    }
    if (!group.trimmed().isEmpty()) {
        detail.setValue(QContactDisplayLabel__FieldLabelGroup, group);
        needSave = true;
    }
    if (sortOrder >= 0) {
        detail.setValue(QContactDisplayLabel__FieldLabelGroupSortOrder, sortOrder);
        needSave = true;
    }

    if (needSave) {
        return contact->saveDetail(&detail, QContact::IgnoreAccessConstraints);
    }

    return true;
}

QString ContactsEngine::normalizedPhoneNumber(const QString &input)
{
    // TODO: Use a configuration variable to specify max characters:
    static const int maxCharacters = QtContactsSqliteExtensions::DefaultMaximumPhoneNumberCharacters;

    return QtContactsSqliteExtensions::minimizePhoneNumber(input, maxCharacters);
}

QString ContactsEngine::synthesizedDisplayLabel(const QContact &contact, QContactManager::Error *error) const
{
    *error = QContactManager::NoError;

    QContactName name = contact.detail<QContactName>();

    // If a custom label has been set, return that
    const QString customLabel = name.value<QString>(QContactName::FieldCustomLabel);
    if (!customLabel.isEmpty())
        return customLabel;

    QString displayLabel;

    if (!name.firstName().isEmpty())
        displayLabel.append(name.firstName());

    if (!name.lastName().isEmpty()) {
        if (!displayLabel.isEmpty())
            displayLabel.append(" ");
        displayLabel.append(name.lastName());
    }

    if (!displayLabel.isEmpty()) {
        return displayLabel;
    }

    foreach (const QContactNickname& nickname, contact.details<QContactNickname>()) {
        if (!nickname.nickname().isEmpty()) {
            return nickname.nickname();
        }
    }

    foreach (const QContactGlobalPresence& gp, contact.details<QContactGlobalPresence>()) {
        if (!gp.nickname().isEmpty()) {
            return gp.nickname();
        }
    }

    foreach (const QContactOrganization& organization, contact.details<QContactOrganization>()) {
        if (!organization.name().isEmpty()) {
            return organization.name();
        }
    }

    foreach (const QContactOnlineAccount& account, contact.details<QContactOnlineAccount>()) {
        if (!account.accountUri().isEmpty()) {
            return account.accountUri();
        }
    }

    foreach (const QContactEmailAddress& email, contact.details<QContactEmailAddress>()) {
        if (!email.emailAddress().isEmpty()) {
            return email.emailAddress();
        }
    }

    foreach (const QContactPhoneNumber& phone, contact.details<QContactPhoneNumber>()) {
        if (!phone.number().isEmpty())
            return phone.number();
    }

    *error = QContactManager::UnspecifiedError;
    return QString();
}

static QList<QContactId> idList(const QVector<quint32> &contactIds, const QString &manager_uri)
{
    QList<QContactId> ids;
    ids.reserve(contactIds.size());
    foreach (quint32 dbId, contactIds) {
        ids.append(ContactId::apiId(dbId, manager_uri));
    }
    return ids;
}

static QList<QContactCollectionId> collectionIdList(const QVector<quint32> &collectionIds, const QString &manager_uri)
{
    QList<QContactCollectionId> ids;
    ids.reserve(collectionIds.size());
    foreach (quint32 dbId, collectionIds) {
        ids.append(ContactCollectionId::apiId(dbId, manager_uri));
    }
    return ids;
}

void ContactsEngine::_q_collectionsAdded(const QVector<quint32> &collectionIds)
{
    emit collectionsAdded(collectionIdList(collectionIds, m_managerUri));
}

void ContactsEngine::_q_collectionsChanged(const QVector<quint32> &collectionIds)
{
    emit collectionsChanged(collectionIdList(collectionIds, m_managerUri));
}

void ContactsEngine::_q_collectionsRemoved(const QVector<quint32> &collectionIds)
{
    emit collectionsRemoved(collectionIdList(collectionIds, m_managerUri));
}

void ContactsEngine::_q_contactsAdded(const QVector<quint32> &contactIds)
{
    emit contactsAdded(idList(contactIds, m_managerUri));
}

void ContactsEngine::_q_contactsChanged(const QVector<quint32> &contactIds)
{
    // TODO: also emit the detail types..
    emit contactsChanged(idList(contactIds, m_managerUri), QList<QContactDetail::DetailType>());
}

void ContactsEngine::_q_contactsPresenceChanged(const QVector<quint32> &contactIds)
{
    if (m_mergePresenceChanges) {
        // TODO: also emit the detail types..
        emit contactsChanged(idList(contactIds, m_managerUri), QList<QContactDetail::DetailType>());
    } else {
        emit contactsPresenceChanged(idList(contactIds, m_managerUri));
    }
}

void ContactsEngine::_q_collectionContactsChanged(const QVector<quint32> &collectionIds)
{
    emit collectionContactsChanged(collectionIdList(collectionIds, m_managerUri));
}

void ContactsEngine::_q_displayLabelGroupsChanged()
{
    emit displayLabelGroupsChanged(displayLabelGroups());
}

void ContactsEngine::_q_contactsRemoved(const QVector<quint32> &contactIds)
{
    emit contactsRemoved(idList(contactIds, m_managerUri));
}

void ContactsEngine::_q_selfContactIdChanged(quint32 oldId, quint32 newId)
{
    emit selfContactIdChanged(ContactId::apiId(oldId, m_managerUri), ContactId::apiId(newId, m_managerUri));
}

void ContactsEngine::_q_relationshipsAdded(const QVector<quint32> &contactIds)
{
    emit relationshipsAdded(idList(contactIds, m_managerUri));
}

void ContactsEngine::_q_relationshipsRemoved(const QVector<quint32> &contactIds)
{
    emit relationshipsRemoved(idList(contactIds, m_managerUri));
}

ContactsDatabase &ContactsEngine::database()
{
    if (!m_database) {
        QString dbId(QStringLiteral("qtcontacts-sqlite%1-%2"));
        dbId = dbId.arg(m_autoTest ? QStringLiteral("-test") : QString()).arg(databaseUuid());

        m_database.reset(new ContactsDatabase(this));
        if (!m_database->open(dbId, m_nonprivileged, m_autoTest, true)) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to open synchronous engine database connection"));
        } else if (!m_nonprivileged && !regenerateAggregatesIfNeeded()) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to regenerate aggregates after schema upgrade"));
        }
    }
    return *m_database;
}

bool ContactsEngine::regenerateAggregatesIfNeeded()
{
    QContactManager::Error err = QContactManager::NoError;
    QContactCollectionFilter aggregatesFilter, localsFilter;
    aggregatesFilter.setCollectionId(QContactCollectionId(m_managerUri, QByteArrayLiteral("col-1")));
    localsFilter.setCollectionId(QContactCollectionId(m_managerUri, QByteArrayLiteral("col-2")));

    const QList<QContactId> aggregateIds = contactIds(aggregatesFilter, QList<QContactSortOrder>(), &err);
    if (err != QContactManager::NoError) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to read aggregate contact ids during attempt to regenerate aggregates"));
        return false;
    }

    if (!aggregateIds.isEmpty()) {
        // if we already have aggregates, then aggregates must
        // have been regenerated already.
        return true;
    }

    const QList<QContactId> localIds = contactIds(localsFilter, QList<QContactSortOrder>(), &err);
    if (err != QContactManager::NoError) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to read local contact ids during attempt to regenerate aggregates"));
        return false;
    }

    if (localIds.isEmpty()) {
        // no local contacts in database to be aggregated.
        return true;
    }

    // We need to regenerate aggregates for our local contacts, due to
    // the database schema upgrade from version 20 to version 21.
    QList<QContact> localContacts = contacts(
            localsFilter,
            QList<QContactSortOrder>(),
            QContactFetchHint(),
            &err);
    if (err != QContactManager::NoError) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to read local contacts during attempt to regenerate aggregates"));
        return false;
    }

    // Simply save them all; this should regenerate aggregates as required.
    if (!saveContacts(&localContacts, nullptr, &err)) {
        QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Failed to save local contacts during attempt to regenerate aggregates"));
        return false;
    }

    return true;
}

ContactReader *ContactsEngine::reader() const
{
    if (!m_synchronousReader) {
        m_synchronousReader.reset(new ContactReader(const_cast<ContactsEngine *>(this)->database(), const_cast<ContactsEngine *>(this)->managerUri()));
    }
    return m_synchronousReader.data();
}

ContactWriter *ContactsEngine::writer()
{
    if (!m_synchronousWriter) {
        m_synchronousWriter.reset(new ContactWriter(*this, database(), m_notifier.data(), reader()));
    }
    return m_synchronousWriter.data();
}

