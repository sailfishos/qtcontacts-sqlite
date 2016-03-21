/*
 * Copyright (C) 2014 Jolla Ltd.
 * Contact: Matt Vogt <matthew.vogt@jollamobile.com>
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

#include "contactstransientstore.h"

#include "memorytable_p.h"
#include "semaphore_p.h"
#include "trace_p.h"

#include <QContactDetail>

#include <QByteArray>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QMap>
#include <QMutex>
#include <QSharedMemory>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QSystemSemaphore>

#include <QtDebug>

#include <cstring>
#include <tr1/functional>

class SharedMemoryManager
{
    // Maintain a connection to a shared memory region containing table data, and a MemoryTable addressing that data
    struct SharedMemoryTable
    {
        explicit SharedMemoryTable(QSharedPointer<QSharedMemory> region)
            : m_region(region)
            , m_table(m_region->data(), m_region->size(), false)
        {
        }
        ~SharedMemoryTable()
        {
        }

        QSharedPointer<QSharedMemory> m_region;
        MemoryTable m_table;
    };

public:
    typedef std::tr1::function<void ()> Function;

    SharedMemoryManager()
        : m_mutex(QMutex::Recursive)
    {
    }

    // This handle references a shared memory table - it keeps it alive while the client
    // holds a reference, and unlocks it on closure, if required.
    struct TableHandle
    {
        TableHandle()
            : m_table(0)
        {
        }
        explicit TableHandle(QSharedPointer<SharedMemoryTable> table, Function release = Function())
            : m_table(table)
            , m_release(release)
        {
        }
        ~TableHandle()
        {
            if (m_release)
                m_release();
        }

        bool isNull() const { return m_table.isNull(); }
        operator bool() const { return !isNull(); }

        MemoryTable *operator->() { return &(m_table->m_table); }
        const MemoryTable *operator->() const { return &(m_table->m_table); }

        operator const MemoryTable *() const { return &(m_table->m_table); }

    private:
        QSharedPointer<SharedMemoryTable> m_table;
        Function m_release;
    };

    bool open(const QString &identifier, bool createIfNecessary, bool reinitialize);

    TableHandle table(const QString &identifier);
    TableHandle reallocateTable(const QString &identifier);

private:
    // For each database (privileged/nonprivileged), we have a shared memory region that holds the data,
    // and another with a fixed key, that contains the identifier needed to access the data region.  If the
    // data region is exhausted, a new region is allocated, the data is copied, and the table is updated
    // reference the new region.  The key region is updated to refer to the new region.
    struct TableData
    {
        TableData(QSharedPointer<QSharedMemory> keyRegion, QSharedPointer<SharedMemoryTable> dataTable, quint32 generation)
            : m_keyRegion(keyRegion)
            , m_dataTable(dataTable)
            , m_generation(generation)
        {
        }
        ~TableData()
        {
        }

        QSharedPointer<QSharedMemory> m_keyRegion;
        QSharedPointer<SharedMemoryTable> m_dataTable;
        quint32 m_generation;
    };

    struct SemaphoreLock
    {
        explicit SemaphoreLock(Function release) : m_release(release)
        {
        }
        ~SemaphoreLock()
        {
            if (m_release)
                m_release();
        }

        operator bool() const
        {
            return m_release;
        }

    private:
        Function m_release;
    };

    static const quint32 keyDataFormatVersion = 1;
    static const quint32 initialGeneration = 1;
    static const int keyIndex = 0;
    static const int dataIndex = 1;

    QString getNativeIdentifier(const QString &identifier, bool createIfNecessary) const;

    quint32 getRegionGeneration(QSharedPointer<QSharedMemory> keyRegion) const;
    void setRegionGeneration(QSharedPointer<QSharedMemory> keyRegion, quint32 regionGeneration);

    QSharedPointer<QSharedMemory> getDataRegion(const QString &identifier, quint32 generation, bool createIfNecessary, size_t dataSize = 0, bool reinitialize = false) const;

    enum { DefaultWaitMs = 5000 };

    Function lockKeyRegion() const;
    Function lockDataRegion(int waitMs = DefaultWaitMs) const;

    Function acquire(int index, int waitMs = DefaultWaitMs) const;
    void release(int index) const;

    QMap<QString, TableData> m_tables;
    QScopedPointer<Semaphore> m_semaphore;
    QMutex m_mutex;
};

Q_GLOBAL_STATIC(SharedMemoryManager, sharedMemory);

bool SharedMemoryManager::open(const QString &identifier, bool createIfNecessary, bool reinitialize)
{
    QMutexLocker threadLock(&m_mutex);

    // Is this region already open?
    if (m_tables.contains(identifier))
        return true;

    const QString semaphoreToken(getNativeIdentifier(identifier + QStringLiteral("-semaphore"), true));
    if (semaphoreToken.isEmpty()) {
        QTCONTACTS_SQLITE_WARNING(QStringLiteral("Failed to create semaphore token for %1")
                .arg(identifier));
        return false;
    }

    // Create semaphores to be able to lock the key and data region
    const int initialSemaphoreValues[] = { 1, 1 };

    m_semaphore.reset(new Semaphore(semaphoreToken.toLatin1(), 2, initialSemaphoreValues));
    if (!m_semaphore) {
        QTCONTACTS_SQLITE_WARNING(QStringLiteral("Failed to create semaphore for %1")
                .arg(identifier));
        return false;
    }

    const QString nativeKey(getNativeIdentifier(identifier, true));
    if (nativeKey.isEmpty()) {
        QTCONTACTS_SQLITE_WARNING(QStringLiteral("Failed to create key token for %1")
                .arg(identifier));
        return false;
    }

    // Attach to the memory region where the key to the data region is stored
    QSharedPointer<QSharedMemory> keyRegion(new QSharedMemory());
    keyRegion->setNativeKey(nativeKey);

    // Table reallocation requires the process holding the data lock to acquire the key lock,
    // while we need to acquire the locks in the other order.  To avoid potential deadlock,
    // give up and try again if we can't acquire the data lock while holding the key lock
    int lockAttempts = 0;
    while (true) {
        // Lock the region, so that only one process can find the region nonexisting
        SemaphoreLock keyLock(lockKeyRegion());
        if (!keyLock) {
            QTCONTACTS_SQLITE_WARNING(QStringLiteral("Failed to lock key memory region for %1")
                    .arg(identifier));
            return false;
        }

        if (!keyRegion->isAttached() && !keyRegion->attach()) {
            if (keyRegion->error() != QSharedMemory::NotFound || !createIfNecessary) {
                QTCONTACTS_SQLITE_WARNING(QStringLiteral("Failed to attach key memory region for %1: %2")
                        .arg(identifier).arg(keyRegion->errorString()));
                return false;
            }

            // Allow far more space than we need in the key region, in case we want to use it for something else
            const int keyRegionSize = 512;
            if (!keyRegion->create(keyRegionSize)) {
                QTCONTACTS_SQLITE_WARNING(QStringLiteral("Failed to create key memory region for %1: %2")
                        .arg(identifier).arg(keyRegion->errorString()));
                return false;
            } else {
                // Write the key details to the key region
                setRegionGeneration(keyRegion, initialGeneration);
            }
        }

        // Find the details from the key region
        const QByteArray keyData(QByteArray::fromRawData(reinterpret_cast<char *>(keyRegion->data()), keyRegion->size()));
        QDataStream is(keyData);

        quint32 formatVersion;
        quint32 regionGeneration;

        is >> formatVersion;
        if (formatVersion == keyDataFormatVersion) {
            is >> regionGeneration;
        } else {
            QTCONTACTS_SQLITE_WARNING(QStringLiteral("Invalid key data format in key region for %1: %2")
                    .arg(identifier).arg(formatVersion));
            return false;
        }

        // We need the data lock in order to validate the data region
        SemaphoreLock dataLock(lockDataRegion(100));
        if (!dataLock) {
            if (++lockAttempts >= 50) {
                QTCONTACTS_SQLITE_WARNING(QStringLiteral("Failed to lock data memory region during open for %1")
                        .arg(identifier));
                return false;
            } else if ((lockAttempts % 10) == 0) {
                QTCONTACTS_SQLITE_WARNING(QStringLiteral("Failed to lock data memory region for %1 after %2 attempts")
                        .arg(identifier).arg(lockAttempts));
            }
            continue;
        }

        // Try to open the data region
        // What size should we use? Using an estimate of 512 bytes per contact, we could store about 2K contacts in a 1M region
        const int memoryRegionSize = 1024 * 1024;

        QSharedPointer<QSharedMemory> dataRegion(getDataRegion(identifier, regionGeneration, true, memoryRegionSize, reinitialize));
        if (!dataRegion || !dataRegion->isAttached())
            return false;

        QSharedPointer<SharedMemoryTable> dataTable(new SharedMemoryTable(dataRegion));

        // Store our handle to this table
        TableData tableData(keyRegion, dataTable, regionGeneration);
        m_tables.insert(identifier, tableData);
        return true;
    }
}

SharedMemoryManager::TableHandle SharedMemoryManager::table(const QString &identifier)
{
    QMutexLocker threadLock(&m_mutex);

    // Table reallocation requires the process holding the data lock to acquire the key lock,
    // while we need to acquire the locks in the other order.  To avoid potential deadlock,
    // give up and try again if we can't acquire the data lock while holding the key lock
    int lockAttempts = 0;
    while (true) {
        SemaphoreLock keyLock(lockKeyRegion());
        if (!keyLock) {
            QTCONTACTS_SQLITE_WARNING(QStringLiteral("Failed to lock key memory region for %1")
                    .arg(identifier));
            return TableHandle();
        }

        QMap<QString, TableData>::iterator it = m_tables.find(identifier);
        if (it == m_tables.end()) {
            QTCONTACTS_SQLITE_WARNING(QStringLiteral("Cannot open unknown shared memory table: %1")
                    .arg(identifier));
            return TableHandle();
        }

        TableData &tableData(*it);

        // Find the current generation of the table
        quint32 regionGeneration = getRegionGeneration(tableData.m_keyRegion);

        // Lock the data region
        Function dataRelease(lockDataRegion(100));
        if (!dataRelease) {
            if (++lockAttempts >= 50) {
                QTCONTACTS_SQLITE_WARNING(QStringLiteral("Failed to lock data region for table access for %1")
                        .arg(identifier));
                return TableHandle();
            } else if ((lockAttempts % 10) == 0) {
                QTCONTACTS_SQLITE_WARNING(QStringLiteral("Failed to lock data region for table access for %1 after %2 attempts")
                        .arg(identifier).arg(lockAttempts));
            }
            continue;
        }

        // Release the data lock if we can't yield an initialized table handle
        struct Cleanup {
            Function release;
            bool active;

            Cleanup(Function f) : release(f), active(true) {}
            ~Cleanup() { if (active) release(); }
        } cleanup(dataRelease);

        if (regionGeneration != tableData.m_generation) {
            // We need to attach to the new version of the table
            QSharedPointer<QSharedMemory> newRegion(getDataRegion(identifier, regionGeneration, false));
            if (!newRegion || !newRegion->isAttached()) {
                QTCONTACTS_SQLITE_WARNING(QStringLiteral("Failed to attach to new data region for %1")
                        .arg(identifier));
                return TableHandle();
            }

            QSharedPointer<SharedMemoryTable> dataTable(new SharedMemoryTable(newRegion));

            // Update the table with the new region
            tableData.m_generation = regionGeneration;
            tableData.m_dataTable = dataTable;
        }

        // The handle will release the lock on destruction
        cleanup.active = false;
        return TableHandle(tableData.m_dataTable, dataRelease);
    }
}

SharedMemoryManager::TableHandle SharedMemoryManager::reallocateTable(const QString &identifier)
{
    QMutexLocker threadLock(&m_mutex);

    QMap<QString, TableData>::iterator it = m_tables.find(identifier);
    if (it == m_tables.end()) {
        QTCONTACTS_SQLITE_WARNING(QStringLiteral("Cannot reallocate unknown table: %1")
                .arg(identifier));
        return TableHandle();
    }

    TableData &tableData(*it);

    // We already hold the data lock, we need the key lock also
    SemaphoreLock keyLock(lockKeyRegion());
    if (!keyLock) {
        QTCONTACTS_SQLITE_WARNING(QStringLiteral("Failed to lock key memory region for %1")
                .arg(identifier));
        return TableHandle();
    }

    quint32 nextGeneration = tableData.m_generation + 1;
    int nextSize = tableData.m_dataTable->m_region->size() * 2;

    // We need to create a new, bigger region to migrate the table to
    QSharedPointer<QSharedMemory> nextRegion(getDataRegion(identifier, nextGeneration, true, nextSize, false));
    if (!nextRegion) {
        QTCONTACTS_SQLITE_WARNING(QStringLiteral("Cannot allocate new shared memory region for table: %1 %2 %3")
                .arg(identifier).arg(nextGeneration).arg(nextSize));
        return TableHandle();
    }

    // Create a new table for the new memory region
    QSharedPointer<SharedMemoryTable> nextDataTable(new SharedMemoryTable(nextRegion));

    // Migrate the existing data to the new region
    MemoryTable::Error error = tableData.m_dataTable->m_table.migrateTo(nextDataTable->m_table);
    if (error != MemoryTable::NoError) {
        QTCONTACTS_SQLITE_WARNING(QStringLiteral("Cannot migrate to new shared memory region for table: %1")
                .arg(identifier));
        return TableHandle();
    }

    // Update the key region to store the new generation value
    setRegionGeneration(tableData.m_keyRegion, nextGeneration);

    // Replace the old table with the new table
    tableData.m_dataTable = nextDataTable;
    tableData.m_generation = nextGeneration;

    // Return an unlocked handle to the next table (the handle to the predecessor table is still active)
    return TableHandle(tableData.m_dataTable);
}

QString SharedMemoryManager::getNativeIdentifier(const QString &identifier, bool createIfNecessary) const
{
    // Despite the documentation, QSharedMemory on unix needs the identifier to be the path
    // of an existing file, in order to use ftok.  Create a file to use, if necessary
    QString path(QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QDir::separator() + identifier);
    if (!QFile::exists(path)) {
        if (createIfNecessary) {
            // Try to create this file
            QFile pathFile;
            pathFile.setFileName(path);
            pathFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::WriteGroup);
            if (!pathFile.open(QIODevice::WriteOnly)) {
                QTCONTACTS_SQLITE_WARNING(QStringLiteral("Failed to create native lock file %1: %2")
                        .arg(identifier).arg(path));
                path = QString();
            } else {
                pathFile.close();
            }
        } else {
            path = QString();
        }
    }

    return path;
}

quint32 SharedMemoryManager::getRegionGeneration(QSharedPointer<QSharedMemory> keyRegion) const
{
    // Extract the current generation from the key region
    const QByteArray keyData(QByteArray::fromRawData(reinterpret_cast<char *>(keyRegion->data()), keyRegion->size()));
    QDataStream is(keyData);

    quint32 regionGeneration = 0;

    quint32 formatVersion;
    is >> formatVersion;
    if (formatVersion == 1) {
        is >> regionGeneration;
    }

    return regionGeneration;
}

void SharedMemoryManager::setRegionGeneration(QSharedPointer<QSharedMemory> keyRegion, quint32 regionGeneration)
{
    QByteArray keyData;
    QDataStream os(&keyData, QIODevice::WriteOnly);
    os << keyDataFormatVersion
       << regionGeneration;

    std::memcpy(keyRegion->data(), keyData.constData(), keyData.size());
}

QSharedPointer<QSharedMemory> SharedMemoryManager::getDataRegion(const QString &identifier, quint32 generation, bool createIfNecessary, size_t dataSize, bool reinitialize) const
{
    // We must hold the data lock before calling this function
    const QString dataIdentifier(QStringLiteral("%1-data-%2").arg(identifier).arg(generation));

    const QString nativeKey(getNativeIdentifier(dataIdentifier, true));
    if (nativeKey.isEmpty()) {
        QTCONTACTS_SQLITE_WARNING(QStringLiteral("Failed to open token file: %1").arg(dataIdentifier));
        return QSharedPointer<QSharedMemory>();
    }

    QSharedPointer<QSharedMemory> memoryRegion(new QSharedMemory());
    memoryRegion->setNativeKey(nativeKey);

    bool attached = memoryRegion->attach();
    if (!attached &&
        (memoryRegion->error() == QSharedMemory::NotFound) &&
        (generation > initialGeneration)) {
        // It's possible that the current generation has been destroyed, but the previous generation
        // is still active; try to connect to that.  We could fall back all the way to the initial
        // generation, but the possible benefit rapidly decreases...
        const QString previousIdentifier(QStringLiteral("%1-data-%2").arg(identifier).arg(generation - 1));
        const QString previousKey(getNativeIdentifier(previousIdentifier, false));
        if (!previousKey.isEmpty()) {
            memoryRegion->setNativeKey(previousKey);
            attached = memoryRegion->attach();
        }
    }

    if (!attached) {
        // Only the initial process can create the key region
        if (memoryRegion->error() != QSharedMemory::NotFound || !createIfNecessary) {
            QTCONTACTS_SQLITE_WARNING(QStringLiteral("Failed to attach data memory region for %1: %2")
                    .arg(dataIdentifier).arg(memoryRegion->errorString()));
            return memoryRegion;
        }

        memoryRegion->setNativeKey(nativeKey);
        if (!memoryRegion->create(dataSize)) {
            QTCONTACTS_SQLITE_WARNING(QStringLiteral("Failed to create data memory region for %1 (%2): %3")
                    .arg(dataIdentifier).arg(dataSize).arg(memoryRegion->errorString()));
            return memoryRegion;
        }

        // Initialize the data region as a MemoryTable
        MemoryTable mt(memoryRegion->data(), memoryRegion->size(), true);

        if (!mt.isValid()) {
            QTCONTACTS_SQLITE_WARNING(QStringLiteral("Failed to initialize table in data memory region for %1")
                    .arg(dataIdentifier));
            memoryRegion->detach();
        }
    } else {
        // Verify that the region contains a valid memory table, or reinitialize if required
        MemoryTable mt(memoryRegion->data(), memoryRegion->size(), reinitialize);

        if (!mt.isValid()) {
            QTCONTACTS_SQLITE_WARNING(QStringLiteral("Failed to initialize table in existing data memory region for %1")
                    .arg(dataIdentifier));
            memoryRegion->detach();
        }
    }

    return memoryRegion;
}

SharedMemoryManager::Function SharedMemoryManager::lockKeyRegion() const
{
    return acquire(keyIndex);
}

SharedMemoryManager::Function SharedMemoryManager::lockDataRegion(int waitMs) const
{
    return acquire(dataIndex, waitMs);
}

SharedMemoryManager::Function SharedMemoryManager::acquire(int index, int waitMs) const
{
    if (m_semaphore) {
        if (m_semaphore->decrement(index, true, waitMs)) {
            return std::tr1::bind(&SharedMemoryManager::release, this, index);
        }
    }

    return Function();
}

void SharedMemoryManager::release(int index) const
{
    if (m_semaphore) {
        if (index >= keyIndex && index <= dataIndex) {
            m_semaphore->increment(index);
        } else {
            QTCONTACTS_SQLITE_WARNING(QStringLiteral("Invalid index to release: %1").arg(index));
        }
    }
}

ContactsTransientStore::const_iterator::const_iterator(const MemoryTable *table, quint32 position)
    : MemoryTable::const_iterator(table, position)
{
}

ContactsTransientStore::const_iterator::const_iterator(const const_iterator &other)
    : MemoryTable::const_iterator(other)
{
}

ContactsTransientStore::const_iterator &ContactsTransientStore::const_iterator::operator=(const const_iterator &other)
{
    MemoryTable::const_iterator::operator=(other);
    return *this;
}

quint32 ContactsTransientStore::const_iterator::key()
{
    return MemoryTable::const_iterator::key();
}

class DataLockPrivate
{
public:
    SharedMemoryManager::TableHandle m_table;

    DataLockPrivate(SharedMemoryManager::TableHandle table)
        : m_table(table)
    {
    }
};

ContactsTransientStore::DataLock::DataLock(DataLockPrivate *p)
    : lock(p)
{
}

ContactsTransientStore::DataLock::~DataLock()
{
}

ContactsTransientStore::DataLock::DataLock(const DataLock &other)
{
    *this = other;
}

ContactsTransientStore::DataLock &ContactsTransientStore::DataLock::operator=(const DataLock &other)
{
    lock = other.lock;
    return *this;
}

ContactsTransientStore::DataLock::operator bool() const
{
    return lock->m_table;
}

QPair<QDateTime, QList<QContactDetail> > ContactsTransientStore::const_iterator::value()
{
    const QByteArray data(MemoryTable::const_iterator::value());
    if (!data.isEmpty()) {
        QDataStream is(data);
        QDateTime dt;
        QList<QContactDetail> details;
        is >> dt >> details;
        return qMakePair(dt, details);
    }

    return qMakePair(QDateTime(), QList<QContactDetail>());
}

ContactsTransientStore::ContactsTransientStore()
{
}

ContactsTransientStore::~ContactsTransientStore()
{
}

bool ContactsTransientStore::open(bool nonprivileged, bool createIfNecessary, bool reinitialize)
{
    const QString identifier(nonprivileged ? QStringLiteral("qtcontacts-sqlite-np") : QStringLiteral("qtcontacts-sqlite"));

    if (!m_identifier.isNull()) {
        QTCONTACTS_SQLITE_WARNING(QStringLiteral("Cannot re-open active transient store: %1 (%2)")
                .arg(identifier).arg(m_identifier));
        return false;
    }

    if (sharedMemory()->open(identifier, createIfNecessary, reinitialize)) {
        m_identifier = identifier;
        return true;
    }

    return false;
}

bool ContactsTransientStore::contains(quint32 contactId) const
{
    const SharedMemoryManager::TableHandle table(sharedMemory()->table(m_identifier));
    if (table) {
        return table->contains(contactId);
    }

    return false;
}

QPair<QDateTime, QList<QContactDetail> > ContactsTransientStore::contactDetails(quint32 contactId) const
{
    const SharedMemoryManager::TableHandle table(sharedMemory()->table(m_identifier));
    if (table) {
        const QByteArray data(table->value(contactId));
        if (!data.isEmpty()) {
            QDataStream is(data);
            QDateTime dt;
            QList<QContactDetail> details;
            is >> dt >> details;
            return qMakePair(dt, details);
        }
    }

    return qMakePair(QDateTime(), QList<QContactDetail>());
}

bool ContactsTransientStore::setContactDetails(quint32 contactId, const QDateTime &timestamp, const QList<QContactDetail> &details)
{
    SharedMemoryManager::TableHandle table(sharedMemory()->table(m_identifier));
    if (table) {
        QByteArray data;
        QDataStream os(&data, QIODevice::WriteOnly);
        os << timestamp << details;

        MemoryTable::Error err = table->insert(contactId, data);
        if (err == MemoryTable::InsufficientSpace) {
            // Reallocate the table to provide more space
            SharedMemoryManager::TableHandle newTable(sharedMemory()->reallocateTable(m_identifier));
            if (newTable) {
                // Perform the write to the new table
                err = newTable->insert(contactId, data);
            } else {
                QTCONTACTS_SQLITE_WARNING(QStringLiteral("Cannot reallocate exhausted transient store: %1")
                        .arg(m_identifier));
                return false;
            }
        }
        if (err == MemoryTable::NoError)
            return true;

        QTCONTACTS_SQLITE_WARNING(QStringLiteral("Cannot store contact details to transient store: %1")
                .arg(m_identifier));
    }

    return false;
}

bool ContactsTransientStore::remove(quint32 contactId)
{
    SharedMemoryManager::TableHandle table(sharedMemory()->table(m_identifier));
    if (table) {
        return table->remove(contactId);
    }

    return false;
}

bool ContactsTransientStore::remove(const QList<quint32> &contactIds)
{
    SharedMemoryManager::TableHandle table(sharedMemory()->table(m_identifier));
    if (table) {
        bool removed(false);
        foreach (quint32 contactId, contactIds) {
            removed |= table->remove(contactId);
        }
        return removed;
    }

    return false;
}

ContactsTransientStore::DataLock ContactsTransientStore::dataLock() const
{
    SharedMemoryManager::TableHandle table(sharedMemory()->table(m_identifier));
    return DataLock(new DataLockPrivate(table));
}

ContactsTransientStore::const_iterator ContactsTransientStore::constBegin(const DataLock &lock) const
{
    if (!lock) {
        QTCONTACTS_SQLITE_WARNING(QStringLiteral("Cannot iterate over unlocked data: %1")
                .arg(m_identifier));
        return const_iterator(0, 0);
    }

    const MemoryTable *tablePtr(lock.lock->m_table);
    return const_iterator(tablePtr, 0);
}

ContactsTransientStore::const_iterator ContactsTransientStore::constEnd(const DataLock &lock) const
{
    if (!lock) {
        QTCONTACTS_SQLITE_WARNING(QStringLiteral("Cannot iterate over unlocked data: %1")
                .arg(m_identifier));
        return const_iterator(0, 0);
    }

    const MemoryTable *tablePtr(lock.lock->m_table);
    return const_iterator(tablePtr, tablePtr->count());
}

