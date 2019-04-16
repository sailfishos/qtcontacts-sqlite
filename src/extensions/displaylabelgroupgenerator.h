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

#ifndef DISPLAYLABELGROUPGENERATOR_H
#define DISPLAYLABELGROUPGENERATOR_H

#include <QString>
#include <QStringList>
#include <QLocale>

namespace QtContactsSqliteExtensions {

/*
   A display label group generator is able to determine which
   "ribbon group" (bucket) a contact should be placed in.

   The most common bucket to place a contact in is usually
   the first letter of that contact's last name
   (so 'John Smith' would be placed into group 'S').

   However, for languages other than English, other forms
   of grouping are required.
*/

class DisplayLabelGroupGenerator
{
public:
    virtual ~DisplayLabelGroupGenerator() {}
    virtual QString name() const = 0;
    virtual int priority() const = 0; // higher priority will be used before lower priority when generating label groups.
    virtual bool preferredForLocale(const QLocale &locale) const = 0;
    virtual bool validForLocale(const QLocale &locale) const = 0;
    virtual QString displayLabelGroup(const QString &data) const = 0;
    virtual QStringList displayLabelGroups() const = 0;
};

} // namespace QtContactsSqliteExtensions

#define QtContactsSqliteExtensions_DisplayLabelGroupGeneratorInterface_iid "org.nemomobile.qtcontacts-sqlite.extensions.DisplayLabelGroupGeneratorInterface"
Q_DECLARE_INTERFACE(QtContactsSqliteExtensions::DisplayLabelGroupGenerator, QtContactsSqliteExtensions_DisplayLabelGroupGeneratorInterface_iid)

#endif // DISPLAYLABELGROUPGENERATOR_H
