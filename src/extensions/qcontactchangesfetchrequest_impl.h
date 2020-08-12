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

#ifndef QCONTACTCHANGESFETCHREQUEST_IMPL_H
#define QCONTACTCHANGESFETCHREQUEST_IMPL_H

#include "./qcontactchangesfetchrequest_p.h"
#include "./contactmanagerengine.h"

#include <QPointer>

QT_BEGIN_NAMESPACE_CONTACTS

QContactChangesFetchRequest::QContactChangesFetchRequest(QObject *parent)
    : QObject(parent)
    , d_ptr(new QContactChangesFetchRequestPrivate(
                this,
                &QContactChangesFetchRequest::stateChanged,
                &QContactChangesFetchRequest::resultsAvailable))
{
}

QContactChangesFetchRequest::~QContactChangesFetchRequest()
{
}

QContactManager *QContactChangesFetchRequest::manager() const
{
    return d_ptr->manager.data();
}

void QContactChangesFetchRequest::setManager(QContactManager *manager)
{
    d_ptr->manager = manager;
}

QContactCollectionId QContactChangesFetchRequest::collectionId() const
{
    return d_ptr->collectionId;
}

void QContactChangesFetchRequest::setCollectionId(const QContactCollectionId &id)
{
    d_ptr->collectionId = id;
}

QContactAbstractRequest::State QContactChangesFetchRequest::state() const
{
    return d_ptr->state;
}

QContactManager::Error QContactChangesFetchRequest::error() const
{
    return d_ptr->error;
}

QList<QContact> QContactChangesFetchRequest::addedContacts() const
{
    return d_ptr->addedContacts;
}

QList<QContact> QContactChangesFetchRequest::modifiedContacts() const
{
    return d_ptr->modifiedContacts;
}

QList<QContact> QContactChangesFetchRequest::removedContacts() const
{
    return d_ptr->removedContacts;
}

QList<QContact> QContactChangesFetchRequest::unmodifiedContacts() const
{
    return d_ptr->unmodifiedContacts;
}

bool QContactChangesFetchRequest::start()
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

bool QContactChangesFetchRequest::cancel()
{
    if (!d_ptr->manager) {
        // No manager.
    } else if (QtContactsSqliteExtensions::ContactManagerEngine * const engine
               = QtContactsSqliteExtensions::contactManagerEngine(*d_ptr->manager)) {
        return engine->cancelRequest(this);
    }
    return false;
}

bool QContactChangesFetchRequest::waitForFinished(int msecs)
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

#endif // QCONTACTCHANGESFETCHREQUEST_IMPL_H
