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

#include "testdlggplugin.h"

/*
   This test plugin provides a display label group generator
   with the following semantics:

   1) if the name or display label data is empty,
      it returns 'Z' (for "zero-length") as the group.

   2) if the name or display label data is greater than
      zero but less than six characters in length, it
      returns that length as a group
      (i.e. '1', '2', '3', '4', or '5').

   3) otherwise, if the name or display label data has
      an even number of characters it returns 'E' as the
      group, else (odd) it returns 'O' as the group.
*/

TestDlgg::TestDlgg(QObject *parent)
    : QObject(parent)
{
}

QString TestDlgg::name() const
{
    return QStringLiteral("testdlgg");
}

int TestDlgg::priority() const
{
    return 1; // test plugin has slightly higher than the default/fallback.
}

bool TestDlgg::preferredForLocale(const QLocale &) const
{
    return true; // this test plugin is always "preferred".
}

bool TestDlgg::validForLocale(const QLocale &) const
{
    return true; // this test plugin is always "valid".
}

QStringList TestDlgg::displayLabelGroups() const
{
    static QStringList allGroups {
        QStringLiteral("Z"),
        QStringLiteral("1"),
        QStringLiteral("2"),
        QStringLiteral("3"),
        QStringLiteral("4"),
        QStringLiteral("5"),
        QStringLiteral("O"), // sort O before E to test DisplayLabelGroupSortOrder semantics
        QStringLiteral("E"),
        QStringLiteral("#"),
    };
    return allGroups;
}

QString TestDlgg::displayLabelGroup(const QString &data) const
{
    if (data.size() && data.at(0).isDigit()) {
        return QStringLiteral("#"); // default # group for numeric names
    }

    if (data == QStringLiteral("tst_displaylabelgroups_unknown_dlg")) {
        // special case: return a group which is NOT included in the "all groups" above.
        // this allows us to test that dynamic group adding works as expected.
        return QStringLiteral("&"); // should be sorted before '#' but after every other group.
    }

    if (data.size() > 5) {
        return (data.size() % 2 == 0) ? QStringLiteral("E") : QStringLiteral("O");
    }

    switch (data.size()) {
        case 1:  return QStringLiteral("1");
        case 2:  return QStringLiteral("2");
        case 3:  return QStringLiteral("3");
        case 4:  return QStringLiteral("4");
        case 5:  return QStringLiteral("5");
        default: return QStringLiteral("Z");
    }
}
