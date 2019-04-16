/*
 * Copyright (C) 2019 Jolla Ltd. <chris.adams@jollamobile.com>
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

#ifndef DEFAULTDLGGENERATOR_H
#define DEFAULTDLGGENERATOR_H

#include <QObject>
#include <displaylabelgroupgenerator.h>

class DefaultDlgGenerator : public QObject, public QtContactsSqliteExtensions::DisplayLabelGroupGenerator
{
    Q_OBJECT
    Q_INTERFACES(QtContactsSqliteExtensions::DisplayLabelGroupGenerator)

public:
    DefaultDlgGenerator(QObject *parent = Q_NULLPTR);
    QString name() const Q_DECL_OVERRIDE;
    int priority() const Q_DECL_OVERRIDE;
    bool validForLocale(const QLocale &locale) const Q_DECL_OVERRIDE;
    bool preferredForLocale(const QLocale &locale) const Q_DECL_OVERRIDE;
    QString displayLabelGroup(const QString &data) const Q_DECL_OVERRIDE;
    QStringList displayLabelGroups() const Q_DECL_OVERRIDE;
};

#endif // DEFAULTDLGGENERATOR_H
