/*
 * Copyright (c) 2020 Open Mobile Platform LLC.
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

#ifndef QCONTACTCHANGESSAVEREQUEST_IMPL_H
#define QCONTACTCHANGESSAVEREQUEST_IMPL_H

#include "./qcontactchangessaverequest_p.h"
#include "./contactmanagerengine.h"

#include <QPointer>

QT_BEGIN_NAMESPACE_CONTACTS

QContactChangesSaveRequest::QContactChangesSaveRequest(QObject *parent)
    : QObject(parent)
    , d_ptr(new QContactChangesSaveRequestPrivate(
                this,
                &QContactChangesSaveRequest::stateChanged,
                &QContactChangesSaveRequest::resultsAvailable))
{
}

QContactChangesSaveRequest::~QContactChangesSaveRequest()
{
}

QContactManager *QContactChangesSaveRequest::manager() const
{
    return d_ptr->manager.data();
}

void QContactChangesSaveRequest::setManager(QContactManager *manager)
{
    d_ptr->manager = manager;
}

QContactChangesSaveRequest::ConflictResolutionPolicy QContactChangesSaveRequest::conflictResolutionPolicy() const
{
    return d_ptr->policy;
}

void QContactChangesSaveRequest::setConflictResolutionPolicy(QContactChangesSaveRequest::ConflictResolutionPolicy policy)
{
    d_ptr->policy = policy;
}

bool QContactChangesSaveRequest::clearChangeFlags() const
{
    return d_ptr->clearChangeFlags;
}

void QContactChangesSaveRequest::setClearChangeFlags(bool clear)
{
    d_ptr->clearChangeFlags = clear;
}

QHash<QContactCollection, QList<QContact> > QContactChangesSaveRequest::addedCollections() const
{
    return d_ptr->addedCollections;
}

void QContactChangesSaveRequest::setAddedCollections(const QHash<QContactCollection, QList<QContact> > &added)
{
    d_ptr->addedCollections = added;
}

QHash<QContactCollection, QList<QContact> > QContactChangesSaveRequest::modifiedCollections() const
{
    return d_ptr->modifiedCollections;
}

void QContactChangesSaveRequest::setModifiedCollections(const QHash<QContactCollection, QList<QContact> > &modified)
{
    d_ptr->modifiedCollections = modified;
}

QList<QContactCollectionId> QContactChangesSaveRequest::removedCollections() const
{
    return d_ptr->removedCollections;
}

void QContactChangesSaveRequest::setRemovedCollections(const QList<QContactCollectionId> &removed)
{
    d_ptr->removedCollections = removed;
}

QContactAbstractRequest::State QContactChangesSaveRequest::state() const
{
    return d_ptr->state;
}

QContactManager::Error QContactChangesSaveRequest::error() const
{
    return d_ptr->error;
}

bool QContactChangesSaveRequest::start()
{
    if (d_ptr->state == QContactAbstractRequest::ActiveState) {
        // Already executing.
    } else if (!d_ptr->manager) {
        // No manager.
    } else if (QtContactsSqliteExtensions::ContactManagerEngine * const engine
               = QtContactsSqliteExtensions::contactManagerEngine(*d_ptr->manager)) {
        return engine->startRequest(this);
    }
    return false;
}

bool QContactChangesSaveRequest::cancel()
{
    if (!d_ptr->manager) {
        // No manager.
    } else if (QtContactsSqliteExtensions::ContactManagerEngine * const engine
               = QtContactsSqliteExtensions::contactManagerEngine(*d_ptr->manager)) {
        return engine->cancelRequest(this);
    }
    return false;
}

bool QContactChangesSaveRequest::waitForFinished(int msecs)
{
    if (!d_ptr->manager) {
        // No manager.
    } else if (QtContactsSqliteExtensions::ContactManagerEngine * const engine
               = QtContactsSqliteExtensions::contactManagerEngine(*d_ptr->manager)) {
        return engine->waitForRequestFinished(this, msecs);
    }
    return false;
}

QT_END_NAMESPACE_CONTACTS

#endif // QCONTACTCHANGESSAVEREQUEST_IMPL_H
