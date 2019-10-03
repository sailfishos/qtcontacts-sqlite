/*
 * Copyright (c) 2019 Open Mobile Platform LLC.
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

#ifndef QCONTACTDETAILFETCHREQUEST_IMPL_H
#define QCONTACTDETAILFETCHREQUEST_IMPL_H

#include "./qcontactdetailfetchrequest_p.h"
#include "./contactmanagerengine.h"

#include <QPointer>

QT_BEGIN_NAMESPACE_CONTACTS

QContactDetailFetchRequest::QContactDetailFetchRequest(QObject *parent)
    : QObject(parent)
    , d_ptr(new QContactDetailFetchRequestPrivate(
                this,
                &QContactDetailFetchRequest::stateChanged,
                &QContactDetailFetchRequest::resultsAvailable))
{
}

QContactDetailFetchRequest::~QContactDetailFetchRequest()
{
}

QContactManager *QContactDetailFetchRequest::manager() const
{
    return d_ptr->manager.data();
}

void QContactDetailFetchRequest::setManager(QContactManager *manager)
{
    d_ptr->manager = manager;
}

QContactDetail::DetailType QContactDetailFetchRequest::type() const
{
    return d_ptr->type;
}

void QContactDetailFetchRequest::setType(QContactDetail::DetailType type)
{
    d_ptr->type = type;
}

QList<int> QContactDetailFetchRequest::fields() const
{
    return d_ptr->fields;
}

void QContactDetailFetchRequest::setFields(const QList<int> fields)
{
    d_ptr->fields = fields;
}

QContactFilter QContactDetailFetchRequest::filter() const
{
    return d_ptr->filter;
}

void QContactDetailFetchRequest::setFilter(const QContactFilter &filter)
{
    d_ptr->filter = filter;
}

QList<QContactSortOrder> QContactDetailFetchRequest::sorting() const
{
    return d_ptr->sorting;
}

void QContactDetailFetchRequest::setSorting(const QList<QContactSortOrder> &sorting)
{
    d_ptr->sorting = sorting;
}

QContactFetchHint QContactDetailFetchRequest::fetchHint() const
{
    return d_ptr->hint;
}

void QContactDetailFetchRequest::setFetchHint(const QContactFetchHint &hint)
{
    d_ptr->hint = hint;
}

QContactAbstractRequest::State QContactDetailFetchRequest::state() const
{
    return d_ptr->state;
}

QContactManager::Error QContactDetailFetchRequest::error() const
{
    return d_ptr->error;
}

QList<QContactDetail> QContactDetailFetchRequest::details() const
{
    return d_ptr->details;
}

bool QContactDetailFetchRequest::start()
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

bool QContactDetailFetchRequest::cancel()
{
    if (!d_ptr->manager) {
        // No manager.
    } else if (QtContactsSqliteExtensions::ContactManagerEngine * const engine
               = QtContactsSqliteExtensions::contactManagerEngine(*d_ptr->manager)) {
        return engine->cancelRequest(this);
    }
    return false;
}

bool QContactDetailFetchRequest::waitForFinished(int msecs)
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

#endif
