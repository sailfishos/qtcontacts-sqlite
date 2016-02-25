/*
 * Copyright (C) 2016 Jolla Ltd.
 * Contact: Chris Adams <chris.adams@jolla.com>
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

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>

#include "deltasyncadapter.h"
#include "../../util.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    qsrand(42); // we want consistent runs, with comparable phone numbers.

    QString accountId(QStringLiteral("1"));
    DeltaSyncAdapter dsa(accountId);
    dsa.addRemoteContact(accountId, "First", "Contact", "1111111", DeltaSyncAdapter::ImplicitlyModifiable);
    dsa.addRemoteContact(accountId, "Second", "Contact", "insert250phones", DeltaSyncAdapter::ExplicitlyModifiable);
    dsa.addRemoteContact(accountId, "Third", "Contact", "3333333", DeltaSyncAdapter::ExplicitlyNonModifiable);

    qWarning() << "================================ performing first sync";
    QElapsedTimer et;
    et.start();
    dsa.performTwoWaySync(accountId);
    qDebug() << "first sync took:" << et.elapsed() << "milliseconds.";

    dsa.changeRemoteContactPhone(accountId, QStringLiteral("First"), QStringLiteral("Contact"), QStringLiteral("1111112"));
    //dsa.changeRemoteContactPhone(accountId, QStringLiteral("Second"), QStringLiteral("Contact"), QStringLiteral("modify10phones"));
    dsa.changeRemoteContactPhone(accountId, QStringLiteral("Second"), QStringLiteral("Contact"), QStringLiteral("modifyallphones"));

    qWarning() << "================================ performing second sync";
    et.restart();
    dsa.performTwoWaySync(accountId);
    qDebug() << "second sync took:" << et.elapsed() << "milliseconds.";

    dsa.removeRemoteContact(accountId, "First", "Contact");
    dsa.removeRemoteContact(accountId, "Second", "Contact");
    dsa.removeRemoteContact(accountId, "Third", "Contact");

    qWarning() << "================================ performing third sync";
    et.restart();
    dsa.performTwoWaySync(accountId);
    qDebug() << "third sync took:" << et.elapsed() << "milliseconds.";

    return 0;
}
