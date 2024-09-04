/*
 * Copyright (C) 2013 Jolla Ltd. <matthew.vogt@jollamobile.com>
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

#include "semaphore_p.h"

#include <errno.h>
#include <unistd.h>

#include <sys/sem.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ipc.h>

#include <QDebug>

namespace {

// Defined as required for ::semun
union semun {
    int              val;
    struct semid_ds *buf;
    unsigned short  *array;
    struct seminfo  *__buf;
};

void semaphoreError(const char *msg, const char *id, int error)
{
    qWarning() << QString::fromLatin1("%1 %2: %3 (%4)").arg(msg).arg(id).arg(::strerror(error)).arg(error);
}

int semaphoreInit(const char *id, size_t count, const int *initialValues)
{
    int rv = -1;

    // It doesn't matter what proj_id we use, there are no other ftok uses on this ID
    key_t key = ::ftok(id, 1);

    rv = ::semget(key, count, 0);
    if (rv == -1) {
        if (errno != ENOENT) {
            semaphoreError("Unable to get semaphore", id, errno);
        } else {
            // The semaphore does not currently exist
            rv = ::semget(key, count, IPC_CREAT | IPC_EXCL | S_IRWXO | S_IRWXG | S_IRWXU);
            if (rv == -1) {
                if (errno == EEXIST) {
                    // Someone else won the race to create the semaphore - retry get 
                    rv = ::semget(key, count, 0);
                }

                if (rv == -1) {
                    semaphoreError("Unable to create semaphore", id, errno);
                }
            } else {
                // Set the initial value
                for (size_t i = 0; i < count; ++i) {
                    union semun arg = { 0 };
                    arg.val = *initialValues++;

                    int status = ::semctl(rv, static_cast<int>(i), SETVAL, arg);
                    if (status == -1) {
                        rv = -1;
                        semaphoreError("Unable to initialize semaphore", id, errno);
                    }
                }
            }
        }
    }

    return rv;
}

bool semaphoreIncrement(int id, size_t index, bool wait, size_t ms, int value)
{
    if (id == -1) {
        errno = 0;
        return false;
    }

    struct sembuf op;
    op.sem_num = index;
    op.sem_op = value;
    op.sem_flg = SEM_UNDO;
    if (!wait) {
        op.sem_flg |= IPC_NOWAIT;
    }

    struct timespec timeout;
    timeout.tv_sec = 0;
    timeout.tv_nsec = ms * 1000;

    do {
        int rv = ::semtimedop(id, &op, 1, (wait && ms > 0 ? &timeout : 0));
        if (rv == 0)
            return true;
    } while (errno == EINTR);

    return false;
}

}

Semaphore::Semaphore(const char *id, int initial)
    : m_identifier(id)
    , m_id(-1)
{
    m_id = semaphoreInit(m_identifier.toUtf8().constData(), 1, &initial);
}

Semaphore::Semaphore(const char *id, size_t count, const int *initialValues)
    : m_identifier(id)
    , m_id(-1)
{
    m_id = semaphoreInit(m_identifier.toUtf8().constData(), count, initialValues);
}

Semaphore::~Semaphore()
{
}

bool Semaphore::isValid() const
{
    return (m_id != -1);
}

bool Semaphore::decrement(size_t index, bool wait, size_t timeoutMs)
{
    if (!semaphoreIncrement(m_id, index, wait, timeoutMs, -1)) {
        if (errno != EAGAIN || wait) {
            error("Unable to decrement semaphore", errno);
        }
        return false;
    }
    return true;
}

bool Semaphore::increment(size_t index, bool wait, size_t timeoutMs)
{
    if (!semaphoreIncrement(m_id, index, wait, timeoutMs, 1)) {
        if (errno != EAGAIN || wait) {
            error("Unable to increment semaphore", errno);
        }
        return false;
    }
    return true;
}

int Semaphore::value(size_t index) const
{
    if (m_id == -1)
        return -1;

    return ::semctl(m_id, index, GETVAL, 0);
}

void Semaphore::error(const char *msg, int error)
{
    semaphoreError(msg, m_identifier.toUtf8().constData(), error);
}

