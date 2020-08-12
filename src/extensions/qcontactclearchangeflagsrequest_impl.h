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

#ifndef QCONTACTCLEARCHANGEFLAGSREQUEST_IMPL_H
#define QCONTACTCLEARCHANGEFLAGSREQUEST_IMPL_H

#include "./qcontactclearchangeflagsrequest_p.h"
#include "./contactmanagerengine.h"

#include <QPointer>

QT_BEGIN_NAMESPACE_CONTACTS

QContactClearChangeFlagsRequest::QContactClearChangeFlagsRequest(QObject *parent)
    : QObject(parent)
    , d_ptr(new QContactClearChangeFlagsRequestPrivate(
                this,
                &QContactClearChangeFlagsRequest::stateChanged,
                &QContactClearChangeFlagsRequest::resultsAvailable))
{
}

QContactClearChangeFlagsRequest::~QContactClearChangeFlagsRequest()
{
}

QContactManager *QContactClearChangeFlagsRequest::manager() const
{
    return d_ptr->manager.data();
}

void QContactClearChangeFlagsRequest::setManager(QContactManager *manager)
{
    d_ptr->manager = manager;
}

QContactCollectionId QContactClearChangeFlagsRequest::collectionId() const
{
    return d_ptr->collectionId;
}

void QContactClearChangeFlagsRequest::setCollectionId(const QContactCollectionId &id)
{
    d_ptr->contactIds.clear();
    d_ptr->collectionId = id;
}

QList<QContactId> QContactClearChangeFlagsRequest::contactIds() const
{
    return d_ptr->contactIds;
}

void QContactClearChangeFlagsRequest::setContactIds(const QList<QContactId> &ids)
{
    d_ptr->collectionId = QContactCollectionId();
    d_ptr->contactIds = ids;
}

QContactAbstractRequest::State QContactClearChangeFlagsRequest::state() const
{
    return d_ptr->state;
}

QContactManager::Error QContactClearChangeFlagsRequest::error() const
{
    return d_ptr->error;
}

bool QContactClearChangeFlagsRequest::start()
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

bool QContactClearChangeFlagsRequest::cancel()
{
    if (!d_ptr->manager) {
        // No manager.
    } else if (QtContactsSqliteExtensions::ContactManagerEngine * const engine
               = QtContactsSqliteExtensions::contactManagerEngine(*d_ptr->manager)) {
        return engine->cancelRequest(this);
    }
    return false;
}

bool QContactClearChangeFlagsRequest::waitForFinished(int msecs)
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

#endif // QCONTACTCLEARCHANGEFLAGSREQUEST_IMPL_H
