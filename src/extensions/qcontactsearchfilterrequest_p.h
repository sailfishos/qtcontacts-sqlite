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

#ifndef QCONTACTSEARCHFILTERREQUEST_P_H
#define QCONTACTSEARCHFILTERREQUEST_P_H

#include "./qcontactsearchfilterrequest.h"

#include <QPointer>
#include <QSet>
#include <QList>

QT_BEGIN_NAMESPACE_CONTACTS

class QContactFetchRequest;

class QContactSearchFilterRequestPrivate
{
public:
    static QContactSearchFilterRequestPrivate *get(QContactSearchFilterRequest *request) { return request->d_func(); }

    QContactSearchFilterRequestPrivate(
            QContactSearchFilterRequest *q,
            void (QContactSearchFilterRequest::*stateChanged)(QContactAbstractRequest::State state),
            void (QContactSearchFilterRequest::*resultsAvailable)())
        : q_ptr(q)
        , stateChanged(stateChanged)
        , resultsAvailable(resultsAvailable)
    {
    }

    QContactSearchFilterRequest * const q_ptr;
    void (QContactSearchFilterRequest::* const stateChanged)(QContactAbstractRequest::State state);
    void (QContactSearchFilterRequest::* const resultsAvailable)();

    QContactFetchHint hint;
    QList<QContactSearchFilterRequest::SearchFilter> searchFilters;
    QString searchFilterValue;
    QList<QContact> contacts;
    QPointer<QContactManager> manager;
    QContactAbstractRequest::State state = QContactAbstractRequest::InactiveState;
    QContactManager::Error error = QContactManager::NoError;

    QList<QContactFetchRequest*> requests; // order matters.  we return results from earlier requests ordered before results from later requests.
    QList<QContactFetchRequest*> requestResultsHandled;
    QSet<QContactId> seenContacts;
    bool isCanceled = false;
};

QT_END_NAMESPACE_CONTACTS

#endif // QCONTACTSEARCHFILTERREQUEST_P_H
