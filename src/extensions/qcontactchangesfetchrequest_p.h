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

#ifndef QCONTACTCHANGESFETCHREQUEST_P_H
#define QCONTACTCHANGESFETCHREQUEST_P_H

#include "./qcontactchangesfetchrequest.h"

#include <QPointer>

QT_BEGIN_NAMESPACE_CONTACTS

class QContactChangesFetchRequestPrivate
{
public:
    static QContactChangesFetchRequestPrivate *get(QContactChangesFetchRequest *request) { return request->d_func(); }

    QContactChangesFetchRequestPrivate(
            QContactChangesFetchRequest *q,
            void (QContactChangesFetchRequest::*stateChanged)(QContactAbstractRequest::State state),
            void (QContactChangesFetchRequest::*resultsAvailable)())
        : q_ptr(q)
        , stateChanged(stateChanged)
        , resultsAvailable(resultsAvailable)
    {
    }

    QContactChangesFetchRequest * const q_ptr;
    void (QContactChangesFetchRequest::* const stateChanged)(QContactAbstractRequest::State state);
    void (QContactChangesFetchRequest::* const resultsAvailable)();

    QPointer<QContactManager> manager;
    QContactCollectionId collectionId;
    QContactAbstractRequest::State state = QContactAbstractRequest::InactiveState;
    QContactManager::Error error = QContactManager::NoError;
    QList<QContact> addedContacts;
    QList<QContact> modifiedContacts;
    QList<QContact> removedContacts;
    QList<QContact> unmodifiedContacts;
};

QT_END_NAMESPACE_CONTACTS

#endif // QCONTACTCHANGESFETCHREQUEST_P_H
