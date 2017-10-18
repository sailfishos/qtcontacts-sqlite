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

#include "contactsengine.h"

#include "contactsdatabase.h"
#include "contactnotifier.h"
#include "contactreader.h"
#include "contactwriter.h"
#include "trace_p.h"

#include "qtcontacts-extensions.h"
#include "qtcontacts-extensions_impl.h"

#include <QCoreApplication>
#include <QMutex>
#include <QThread>
#include <QWaitCondition>
#include <QElapsedTimer>
#include <QUuid>
#include <QDataStream>

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
        const ContactsEngine &engine;
        ContactsDatabase &database;
        ContactNotifier &notifier;
        ContactReader &reader;
        mutable ContactWriter *writer;

        WriterProxy(const ContactsEngine &e, ContactsDatabase &db, ContactNotifier &n, ContactReader &r)
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

    virtual QContactAbstractRequest *request() = 0;
    virtual void clear() = 0;

    virtual void execute(ContactReader *reader, WriterProxy &writer) = 0;
    virtual void update(QMutex *) {}
    virtual void updateState(QContactAbstractRequest::State state) = 0;
    virtual void setError(QContactManager::Error) {}

    virtual void contactsAvailable(const QList<QContact> &) {}
    virtual void contactIdsAvailable(const QList<QContactId> &) {}

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

    QContactAbstractRequest *request()
    {
        return m_request;
    }

    void clear()
    {
        m_request = 0;
    }

    QContactManager::Error error() const
    {
        return m_error;
    }

    void setError(QContactManager::Error error)
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

    void execute(ContactReader *, WriterProxy &writer)
    {
        m_error = writer->save(&m_contacts, m_definitionMask, 0, &m_errorMap, false, false, false);
    }

    void updateState(QContactAbstractRequest::State state)
    {
         QContactManagerEngine::updateContactSaveRequest(
                     m_request, m_contacts, m_error, m_errorMap, state);
    }

    QString description() const
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

    void execute(ContactReader *, WriterProxy &writer)
    {
        m_errorMap.clear();
        m_error = writer->remove(m_contactIds, &m_errorMap, false);
    }

    void updateState(QContactAbstractRequest::State state)
    {
        QContactManagerEngine::updateContactRemoveRequest(
                m_request,
                m_error,
                m_errorMap,
                state);
    }

    QString description() const
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

    void execute(ContactReader *reader, WriterProxy &)
    {
        QList<QContact> contacts;
        m_error = reader->readContacts(
                QLatin1String("AsynchronousFilter"),
                &contacts,
                m_filter,
                m_sorting,
                m_fetchHint);
    }

    void update(QMutex *mutex)
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

    void updateState(QContactAbstractRequest::State state)
    {
        QContactManagerEngine::updateContactFetchRequest(m_request, m_contacts, m_error, state);
    }

    void contactsAvailable(const QList<QContact> &contacts)
    {
        m_contacts = contacts;
    }

    QString description() const
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

    void execute(ContactReader *reader, WriterProxy &)
    {
        QList<QContactId> contactIds;
        m_error = reader->readContactIds(&contactIds, m_filter, m_sorting);
    }

    void update(QMutex *mutex)
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

    void updateState(QContactAbstractRequest::State state)
    {
        QContactManagerEngine::updateContactIdFetchRequest(
                m_request, m_contactIds, m_error, state);
    }

    void contactIdsAvailable(const QList<QContactId> &contactIds)
    {
        m_contactIds = contactIds;
    }

    QString description() const
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

    void execute(ContactReader *reader, WriterProxy &)
    {
        QList<QContact> contacts;
        m_error = reader->readContacts(
                QLatin1String("AsynchronousIds"),
                &contacts,
                m_contactIds,
                m_fetchHint);
    }

    void update(QMutex *mutex)
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

    void updateState(QContactAbstractRequest::State state)
    {
        QContactManagerEngine::updateContactFetchByIdRequest(
                m_request,
                m_contacts,
                m_error,
                QMap<int, QContactManager::Error>(),
                state);
    }

    void contactsAvailable(const QList<QContact> &contacts)
    {
        m_contacts = contacts;
    }

    QString description() const
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


class RelationshipSaveJob : public TemplateJob<QContactRelationshipSaveRequest>
{
public:
    RelationshipSaveJob(QContactRelationshipSaveRequest *request)
        : TemplateJob(request)
        , m_relationships(request->relationships())
    {
    }

    void execute(ContactReader *, WriterProxy &writer)
    {
        m_error = writer->save(m_relationships, &m_errorMap, false, false);
    }

    void updateState(QContactAbstractRequest::State state)
    {
         QContactManagerEngine::updateRelationshipSaveRequest(
                     m_request, m_relationships, m_error, m_errorMap, state);
    }

    QString description() const
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

    void execute(ContactReader *, WriterProxy &writer)
    {
        m_error = writer->remove(m_relationships, &m_errorMap, false);
    }

    void updateState(QContactAbstractRequest::State state)
    {
        QContactManagerEngine::updateRelationshipRemoveRequest(
                m_request, m_error, m_errorMap, state);
    }

    QString description() const
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
        , m_first(request->first().id())
        , m_second(request->second().id())
    {
    }

    void execute(ContactReader *reader, WriterProxy &)
    {
        m_error = reader->readRelationships(
                &m_relationships,
                m_type,
                m_first,
                m_second);
    }

    void updateState(QContactAbstractRequest::State state)
    {
        QContactManagerEngine::updateRelationshipFetchRequest(
                m_request, m_relationships, m_error, state);
    }

    QString description() const
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

    bool requestDestroyed(QContactAbstractRequest *request)
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
        }
        return false;
    }

    bool cancelRequest(QContactAbstractRequest *request)
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

    bool waitForFinished(QContactAbstractRequest *request, const int msecs)
    {
        long timeout = msecs <= 0
                ? LONG_MAX
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
    JobContactReader(ContactsDatabase &database, JobThread *thread)
        : ContactReader(database)
        , m_thread(thread)
    {
    }

    void contactsAvailable(const QList<QContact> &contacts)
    {
        m_thread->contactsAvailable(contacts);
    }

    void contactIdsAvailable(const QList<QContactId> &contactIds)
    {
        m_thread->contactIdsAvailable(contactIds);
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
        JobContactReader reader(m_database, this);
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
    QCoreApplication::instance()->setProperty(CONTACT_MANAGER_ENGINE_PROP,
                                              QVariant::fromValue(this));
}

ContactsEngine::~ContactsEngine()
{
    QCoreApplication::instance()->setProperty(CONTACT_MANAGER_ENGINE_PROP, 0);
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
                m_notifier->connect("contactsAdded", "au", this, SLOT(_q_contactsAdded(QVector<quint32>)));
                m_notifier->connect("contactsChanged", "au", this, SLOT(_q_contactsChanged(QVector<quint32>)));
                m_notifier->connect("contactsPresenceChanged", "au", this, SLOT(_q_contactsPresenceChanged(QVector<quint32>)));
                m_notifier->connect("syncContactsChanged", "as", this, SLOT(_q_syncContactsChanged(QStringList)));
                m_notifier->connect("contactsRemoved", "au", this, SLOT(_q_contactsRemoved(QVector<quint32>)));
                m_notifier->connect("selfContactIdChanged", "uu", this, SLOT(_q_selfContactIdChanged(quint32,quint32)));
                m_notifier->connect("relationshipsAdded", "au", this, SLOT(_q_relationshipsAdded(QVector<quint32>)));
                m_notifier->connect("relationshipsRemoved", "au", this, SLOT(_q_relationshipsRemoved(QVector<quint32>)));
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
    QContactManager::Error err = writer()->remove(contactIds, errorMap, false);
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
        const QContact &participant,
        QContactRelationship::Role role,
        QContactManager::Error *error) const
{
    QContactId first = ContactId::apiId(participant);
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

void ContactsEngine::requestDestroyed(QContactAbstractRequest* req)
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
    default:
        return false;
    }

    job->updateState(QContactAbstractRequest::ActiveState);
    m_jobThread->enqueue(job);

    return true;
}

bool ContactsEngine::cancelRequest(QContactAbstractRequest* req)
{
    if (m_jobThread)
        return m_jobThread->cancelRequest(req);

    return false;
}

bool ContactsEngine::waitForRequestFinished(QContactAbstractRequest* req, int msecs)
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

void ContactsEngine::regenerateDisplayLabel(QContact &contact) const
{
    QContactManager::Error displayLabelError = QContactManager::NoError;
    QString label = synthesizedDisplayLabel(contact, &displayLabelError);
    if (displayLabelError != QContactManager::NoError) {
        QTCONTACTS_SQLITE_DEBUG(QString::fromLatin1("Unable to regenerate displayLabel for contact: %1").arg(ContactId::toString(contact)));
        return;
    }

    setContactDisplayLabel(&contact, label);
}

bool ContactsEngine::fetchSyncContacts(const QString &syncTarget, const QDateTime &lastSync, const QList<QContactId> &exportedIds,
                                       QList<QContact> *syncContacts, QList<QContact> *addedContacts, QList<QContactId> *deletedContactIds,
                                       QContactManager::Error *error)
{
    Q_ASSERT(error);
    // This function is deprecated and exists for backward compatibility only!
    qWarning() << Q_FUNC_INFO << "DEPRECATED: use the overload which takes a maxTimestamp argument instead!";
    QDateTime maxTimestamp;
    return fetchSyncContacts(syncTarget, lastSync, exportedIds, syncContacts, addedContacts, deletedContactIds, &maxTimestamp, error);
}

bool ContactsEngine::fetchSyncContacts(const QString &syncTarget, const QDateTime &lastSync, const QList<QContactId> &exportedIds,
                                       QList<QContact> *syncContacts, QList<QContact> *addedContacts, QList<QContactId> *deletedContactIds,
                                       QDateTime *maxTimestamp, QContactManager::Error *error)
{
    Q_ASSERT(maxTimestamp);
    Q_ASSERT(error);

    *error = writer()->fetchSyncContacts(syncTarget, lastSync, exportedIds, syncContacts, addedContacts, deletedContactIds, maxTimestamp);
    return (*error == QContactManager::NoError);
}

bool ContactsEngine::storeSyncContacts(const QString &syncTarget, ConflictResolutionPolicy conflictPolicy,
                                       const QList<QPair<QContact, QContact> > &remoteChanges, QContactManager::Error *error)
{
    Q_ASSERT(error);
    // This function is deprecated and exists for backward compatibility only!
    qWarning() << Q_FUNC_INFO << "DEPRECATED: use the alternate overload instead!";

    QList<QPair<QContact, QContact> > remoteChangesCopy(remoteChanges);

    *error = writer()->updateSyncContacts(syncTarget, conflictPolicy, &remoteChangesCopy);
    return (*error == QContactManager::NoError);
}

bool ContactsEngine::storeSyncContacts(const QString &syncTarget, ConflictResolutionPolicy conflictPolicy,
                                       QList<QPair<QContact, QContact> > *remoteChanges, QContactManager::Error *error)
{
    Q_ASSERT(error);

    *error = writer()->updateSyncContacts(syncTarget, conflictPolicy, remoteChanges);
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

bool ContactsEngine::setContactDisplayLabel(QContact *contact, const QString &label)
{
    QContactDisplayLabel detail(contact->detail<QContactDisplayLabel>());
    detail.setLabel(label);
    return contact->saveDetail(&detail);
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
    const QString customLabel = name.value<QString>(QContactName__FieldCustomLabel);
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

static QList<QContactId> idList(const QVector<quint32> &contactIds)
{
    QList<QContactId> ids;
    ids.reserve(contactIds.size());
    foreach (quint32 dbId, contactIds) {
        ids.append(ContactId::apiId(dbId));
    }
    return ids;
}

void ContactsEngine::_q_contactsAdded(const QVector<quint32> &contactIds)
{
    emit contactsAdded(idList(contactIds));
}

void ContactsEngine::_q_contactsChanged(const QVector<quint32> &contactIds)
{
    emit contactsChanged(idList(contactIds));
}

void ContactsEngine::_q_contactsPresenceChanged(const QVector<quint32> &contactIds)
{
    if (m_mergePresenceChanges) {
        emit contactsChanged(idList(contactIds));
    } else {
        emit contactsPresenceChanged(idList(contactIds));
    }
}

void ContactsEngine::_q_syncContactsChanged(const QStringList &syncTargets)
{
    emit syncContactsChanged(syncTargets);
}

void ContactsEngine::_q_contactsRemoved(const QVector<quint32> &contactIds)
{
    emit contactsRemoved(idList(contactIds));
}

void ContactsEngine::_q_selfContactIdChanged(quint32 oldId, quint32 newId)
{
    emit selfContactIdChanged(ContactId::apiId(oldId), ContactId::apiId(newId));
}

void ContactsEngine::_q_relationshipsAdded(const QVector<quint32> &contactIds)
{
    emit relationshipsAdded(idList(contactIds));
}

void ContactsEngine::_q_relationshipsRemoved(const QVector<quint32> &contactIds)
{
    emit relationshipsRemoved(idList(contactIds));
}

ContactsDatabase &ContactsEngine::database()
{
    if (!m_database) {
        QString dbId(QStringLiteral("qtcontacts-sqlite%1-%2"));
        dbId = dbId.arg(m_autoTest ? QStringLiteral("-test") : QString()).arg(databaseUuid());

        m_database.reset(new ContactsDatabase);
        if (!m_database->open(dbId, m_nonprivileged, m_autoTest, true)) {
            QTCONTACTS_SQLITE_WARNING(QString::fromLatin1("Unable to open synchronous engine database connection"));
        }
    }
    return *m_database;
}

ContactReader *ContactsEngine::reader() const
{
    if (!m_synchronousReader) {
        m_synchronousReader.reset(new ContactReader(const_cast<ContactsEngine *>(this)->database()));
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

