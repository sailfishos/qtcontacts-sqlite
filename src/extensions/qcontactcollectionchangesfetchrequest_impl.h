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

#ifndef QCONTACTCOLLECTIONCHANGESFETCHREQUEST_IMPL_H
#define QCONTACTCOLLECTIONCHANGESFETCHREQUEST_IMPL_H

#include "./qcontactcollectionchangesfetchrequest_p.h"
#include "./contactmanagerengine.h"

#include <QPointer>

QT_BEGIN_NAMESPACE_CONTACTS

QContactCollectionChangesFetchRequest::QContactCollectionChangesFetchRequest(QObject *parent)
    : QObject(parent)
    , d_ptr(new QContactCollectionChangesFetchRequestPrivate(
                this,
                &QContactCollectionChangesFetchRequest::stateChanged,
                &QContactCollectionChangesFetchRequest::resultsAvailable))
{
}

QContactCollectionChangesFetchRequest::~QContactCollectionChangesFetchRequest()
{
}

QContactManager *QContactCollectionChangesFetchRequest::manager() const
{
    return d_ptr->manager.data();
}

void QContactCollectionChangesFetchRequest::setManager(QContactManager *manager)
{
    d_ptr->manager = manager;
}

int QContactCollectionChangesFetchRequest::accountId() const
{
    return d_ptr->accountId;
}

void QContactCollectionChangesFetchRequest::setAccountId(int id)
{
    d_ptr->accountId = id;
}

QString QContactCollectionChangesFetchRequest::applicationName() const
{
    return d_ptr->applicationName;
}

void QContactCollectionChangesFetchRequest::setApplicationName(const QString &name)
{
    d_ptr->applicationName = name;
}

QContactAbstractRequest::State QContactCollectionChangesFetchRequest::state() const
{
    return d_ptr->state;
}

QContactManager::Error QContactCollectionChangesFetchRequest::error() const
{
    return d_ptr->error;
}

QList<QContactCollection> QContactCollectionChangesFetchRequest::addedCollections() const
{
    return d_ptr->addedCollections;
}

QList<QContactCollection> QContactCollectionChangesFetchRequest::modifiedCollections() const
{
    return d_ptr->modifiedCollections;
}

QList<QContactCollection> QContactCollectionChangesFetchRequest::removedCollections() const
{
    return d_ptr->removedCollections;
}

QList<QContactCollection> QContactCollectionChangesFetchRequest::unmodifiedCollections() const
{
    return d_ptr->unmodifiedCollections;
}

bool QContactCollectionChangesFetchRequest::start()
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

bool QContactCollectionChangesFetchRequest::cancel()
{
    if (!d_ptr->manager) {
        // No manager.
    } else if (QtContactsSqliteExtensions::ContactManagerEngine * const engine
               = QtContactsSqliteExtensions::contactManagerEngine(*d_ptr->manager)) {
        return engine->cancelRequest(this);
    }
    return false;
}

bool QContactCollectionChangesFetchRequest::waitForFinished(int msecs)
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

#endif // QCONTACTCOLLECTIONCHANGESFETCHREQUEST_IMPL_H
