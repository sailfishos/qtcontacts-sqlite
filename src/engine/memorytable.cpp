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

#include "memorytable_p.h"

#include <QtDebug>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>

// Class to manage a table of key/value pairs in a memory buffer, using offsets rather
// than addresses, to be suitable for placement in shared memory.
//
// The lower end of the available space holds a sorted index of keys to offsets; the
// upper end of the space holds a heap allocated into variable sized blocks, growing
// down toward the index.
//
// Deallocated blocks are added to a free list, from which they can be reallocated.
// No defragmentation is currently performed; when allocation fails, it is required
// that the content be migrated to a new memory buffer. No compaction is currently
// performed.
//
// Key and value types are currently fixed as quint32/QByteArray, but could be changed
// without much difficulty.

namespace {

template<typename T>
T extractData(const char *src, size_t len);

// Functions to work with QByteArray (change for different value_type)
size_t dataSize(const QByteArray &data) { return data.size(); }
void insertData(char *dst, size_t len, const QByteArray &data) { std::memcpy(dst, data.constData(), len); }

template<>
QByteArray extractData<QByteArray>(const char *src, size_t len) { return QByteArray::fromRawData(src, len); }

// Structures used in the table management
struct IndexElement {
    MemoryTable::key_type key;
    quint32 offset;
};

struct Allocation {
    quint16 size;           // size of the allocated block
    quint16 dataSize;       // size of the stored data, when in use, or FreeBlock
    union {
        char data[1];
        quint32 nextOffset; // offset of the next free block, when on the free list
    };
};

struct TableMetadata {
    quint32 size;           // size of the table memory region
    quint32 count;          // number of items
    quint32 freeOffset;     // position of the free space
    quint32 freeList;       // offset of the first free block
    IndexElement index[1];
};

bool operator<(const IndexElement &lhs, const MemoryTable::key_type &rhs)
{
    return lhs.key < rhs;
}

template<typename T>
T roundUp(T n, T m)
{
    T rv = n + m - 1;
    return rv - (rv % m);
}

template<typename T>
T roundDown(T n, T m)
{
    return n - (n % m);
}

}

class MemoryTablePrivate
{
public:
    typedef MemoryTable::key_type key_type;
    typedef MemoryTable::value_type value_type;
    typedef MemoryTable::Error Error;

    enum { FreeBlock = UINT_MAX };

    static TableMetadata *metadata(MemoryTable *table);
    static const TableMetadata *metadata(const MemoryTable *table);

    static size_t count(const TableMetadata *table);
    static bool contains(const key_type &key, const TableMetadata *table);
    static const value_type value(const key_type &key, const TableMetadata *table);
    static Error insert(const key_type &key, const value_type &value, TableMetadata *table);
    static bool remove(const key_type &key, TableMetadata *table);

    static Error migrateTo(TableMetadata *other, const TableMetadata *table);

    static IndexElement *begin(TableMetadata *table);
    static IndexElement *end(TableMetadata *table);

    static const IndexElement *begin(const TableMetadata *table);
    static const IndexElement *end(const TableMetadata *table);

    static Allocation *allocationAt(quint32 offset, TableMetadata *table);
    static const Allocation *allocationAt(quint32 offset, const TableMetadata *table);

    static const value_type valueAt(quint32 offset, const TableMetadata *table);

    static key_type keyAtIndex(size_t index, const TableMetadata *table);
    static const value_type valueAtIndex(size_t index, const TableMetadata *table);

    static size_t freeSpace(const TableMetadata *table);
    static size_t requiredSpace(quint32 size);

    static quint32 allocate(quint32 size, TableMetadata *table, bool indexRequired);
    static void deallocate(quint32 offset, TableMetadata *table);

    static void updateValue(const value_type &value, quint32 valueSize, quint32 offset, TableMetadata *table);
};

TableMetadata *MemoryTablePrivate::metadata(MemoryTable *memoryTable)
{
    return reinterpret_cast<TableMetadata *>(memoryTable->mBase);
}

const TableMetadata *MemoryTablePrivate::metadata(const MemoryTable *memoryTable)
{
    return reinterpret_cast<const TableMetadata *>(memoryTable->mBase);
}

size_t MemoryTablePrivate::count(const TableMetadata *table)
{
    return table->count;
}

bool MemoryTablePrivate::contains(const key_type &key, const TableMetadata *table)
{
    const IndexElement *tableEnd = end(table);
    const IndexElement *position = std::lower_bound(begin(table), tableEnd, key);
    return (position != tableEnd && position->key == key);
}

const MemoryTablePrivate::value_type MemoryTablePrivate::value(const key_type &key, const TableMetadata *table)
{
    const IndexElement *tableEnd = end(table);
    const IndexElement *position = std::lower_bound(begin(table), tableEnd, key);
    if (position == tableEnd || position->key != key)
        return value_type();

    return valueAt(position->offset, table);
}

MemoryTablePrivate::Error MemoryTablePrivate::insert(const key_type &key, const value_type &value, TableMetadata *table)
{
    const quint32 valueSize = dataSize(value);

    IndexElement *tableEnd = end(table);
    IndexElement *position = std::lower_bound(begin(table), tableEnd, key);
    if (position != tableEnd && position->key == key) {
        // This is a replacement - the item has an allocation already
        Allocation *allocation = allocationAt(position->offset, table);
        if (allocation->size < requiredSpace(valueSize)) {
            // Replace the existing allocation with a bigger one
            quint32 newOffset = allocate(valueSize, table, false);
            if (!newOffset)
                return MemoryTable::InsufficientSpace;

            deallocate(position->offset, table);
            position->offset = newOffset;
        } else {
            // Reuse this allocation
            // TODO: swap with a better fit from the free list?
        }
    } else {
        // This item needs to be added to the index
        if (table->count == std::numeric_limits<quint32>::max())
            return MemoryTable::InsufficientSpace;

        quint32 offset = allocate(valueSize, table, true);
        if (!offset)
            return MemoryTable::InsufficientSpace;

        // For index insertion, move the displaced elements of the index
        if (position != tableEnd)
            std::memmove(position + 1, position, (tableEnd - position) * sizeof(IndexElement));

        ++(table->count);
        position->key = key;
        position->offset = offset;
    }

    Q_ASSERT(contains(key, table));

    // Ensure that the data allocation does not overlap the index space
    Q_ASSERT((reinterpret_cast<char *>(table) + table->freeOffset) >= reinterpret_cast<char *>(end(table)));

    // Update the value stored at the position
    updateValue(value, valueSize, position->offset, table);

    return MemoryTable::NoError;
}

bool MemoryTablePrivate::remove(const key_type &key, TableMetadata *table)
{
    IndexElement *tableEnd = end(table);
    IndexElement *position = std::lower_bound(begin(table), tableEnd, key);
    if (position == tableEnd || position->key != key)
        return false;

    deallocate(position->offset, table);

    std::memmove(position, position + 1, (tableEnd - position - 1) * sizeof(IndexElement));
    --(table->count);

    Q_ASSERT(!contains(key, table));
    return true;
}

MemoryTablePrivate::Error MemoryTablePrivate::migrateTo(TableMetadata *other, const TableMetadata *table)
{
    // Copy all live elements to the other table
    const IndexElement *tableEnd(end(table));
    for (const IndexElement *it = begin(table); it != tableEnd; ++it) {
        MemoryTable::Error error = insert((*it).key, valueAt((*it).offset, table), other);
        if (error != MemoryTable::NoError)
            return error;
    }

    return MemoryTable::NoError;
}

IndexElement *MemoryTablePrivate::begin(TableMetadata *table)
{
    return &table->index[0];
}

IndexElement *MemoryTablePrivate::end(TableMetadata *table)
{
    return &table->index[table->count];
}

const IndexElement *MemoryTablePrivate::begin(const TableMetadata *table)
{
    return &table->index[0];
}

const IndexElement *MemoryTablePrivate::end(const TableMetadata *table)
{
    return &table->index[table->count];
}

Allocation *MemoryTablePrivate::allocationAt(quint32 offset, TableMetadata *table)
{
    return reinterpret_cast<Allocation *>(reinterpret_cast<char *>(table) + offset);
}

const Allocation *MemoryTablePrivate::allocationAt(quint32 offset, const TableMetadata *table)
{
    return reinterpret_cast<const Allocation *>(reinterpret_cast<const char *>(table) + offset);
}

const MemoryTablePrivate::value_type MemoryTablePrivate::valueAt(quint32 offset, const TableMetadata *table)
{
    const Allocation *allocation = allocationAt(offset, table);
    return extractData<value_type>(allocation->data, allocation->dataSize);
}

MemoryTablePrivate::key_type MemoryTablePrivate::keyAtIndex(size_t index, const TableMetadata *table)
{
    if (index >= table->count)
        return key_type();

    return table->index[index].key;
}

const MemoryTablePrivate::value_type MemoryTablePrivate::valueAtIndex(size_t index, const TableMetadata *table)
{
    if (index >= table->count)
        return key_type();

    return valueAt(table->index[index].offset, table);
}

size_t MemoryTablePrivate::freeSpace(const TableMetadata *table)
{
    // Free space lies between the index and the allocated blocks
    return table->freeOffset - ((table->count * sizeof(IndexElement)) + offsetof(TableMetadata, index));
}

size_t MemoryTablePrivate::requiredSpace(quint32 size)
{
    return std::max<size_t>(sizeof(Allocation), offsetof(Allocation, data) + size); // overhead of Allocation + size
}

quint32 MemoryTablePrivate::allocate(quint32 size, TableMetadata *table, bool indexRequired)
{
    const quint32 availableSpace = freeSpace(table);
    if (indexRequired) {
        // Even if satisfied from the free list, this allocation requires there to be space for expanded index
        if (availableSpace < sizeof(IndexElement)) {
            return 0;
        }
    }

    // Align the allocation so that the header is directly accessible
    quint32 allocationSize = static_cast<quint32>(requiredSpace(size));
    allocationSize = roundUp(allocationSize, static_cast<quint32>(sizeof(quint32)));

    if (table->freeList) {
        // Try to reuse a freed block
        quint32 *bestOffset = 0;
        Allocation *bestBlock = 0;

        quint32 *freeOffset = &table->freeList;
        while (*freeOffset) {
            Allocation *freeBlock = allocationAt(*freeOffset, table);
            if (freeBlock->size >= allocationSize) {
                // This block is large enough
                if (!bestBlock || bestBlock->size > freeBlock->size) {
                    // It's our best fit so far
                    bestBlock = freeBlock;
                    bestOffset = freeOffset;
                }
            }

            freeOffset = &freeBlock->nextOffset;
        }

        if (bestOffset) {
            // TODO: if this block is too large, should it be partitioned?
            quint32 rv = *bestOffset;
            *bestOffset = bestBlock->nextOffset;
            return rv;
        }
    }

    // Is there enough space for this allocation?
    if (availableSpace < (allocationSize + (indexRequired ? sizeof(IndexElement) : 0))) {
        return 0;
    }

    // Allocate the space immediately below the already-allocated space
    table->freeOffset -= allocationSize;

    Allocation *allocation = allocationAt(table->freeOffset, table);
    allocation->size = allocationSize;

    return table->freeOffset;
}

void MemoryTablePrivate::deallocate(quint32 offset, TableMetadata *table)
{
    Allocation *allocation = allocationAt(offset, table);

    // TODO: attempt merge with adjoining blocks?

    // Add this block to the free list
    allocation->dataSize = static_cast<quint16>(FreeBlock);
    allocation->nextOffset = table->freeList;
    table->freeList = offset;
}

void MemoryTablePrivate::updateValue(const value_type &value, quint32 valueSize, quint32 offset, TableMetadata *table)
{
    Allocation *allocation = allocationAt(offset, table);
    Q_ASSERT(allocation->size >= requiredSpace(valueSize));

    allocation->dataSize = valueSize;
    insertData(allocation->data, allocation->dataSize, value);
}

MemoryTable::MemoryTable(void *base, size_t size, bool initialize)
    : mBase(0)
    , mSize(0)
{
#ifndef __GNUG__
#error "Alignment testing required"
#endif
    // base address must be aligned for the metadata struct
    if (!base) {
        qWarning() << "Invalid address for table:" << base;
        return;
    }
    if ((reinterpret_cast<size_t>(base) % __alignof__(TableMetadata)) != 0) {
        qWarning() << "Invalid address alignment for table:" << base << "requires:" << __alignof__(TableMetadata);
        return;
    }

    // We must align the allocation space with the Allocation struct
    size_t managedSize = roundDown(size, __alignof__(Allocation));
    if (managedSize <= sizeof(TableMetadata)) {
        qWarning() << "Invalid size alignment for table:" << base << "requires:" << __alignof__(Allocation);
        return;
    }

    TableMetadata *table = reinterpret_cast<TableMetadata *>(base);

    if (initialize) {
        table->size = static_cast<quint64>(managedSize);
        table->count = 0;
        table->freeOffset = table->size;
        table->freeList = 0;
    } else {
        if (table->size != managedSize) {
            qWarning() << "Invalid size for initialized table:" << table->size << "!=" << managedSize;
            return;
        }
    }

    mBase = base;
    mSize = table->size;
}

MemoryTable::~MemoryTable()
{
}

bool MemoryTable::isValid() const
{
    return mBase != 0;
}

size_t MemoryTable::count() const
{
    if (!mBase)
        return 0u;

    return MemoryTablePrivate::count(MemoryTablePrivate::metadata(this));
}

bool MemoryTable::contains(const key_type &key) const
{
    if (!mBase)
        return false;

    return MemoryTablePrivate::contains(key, MemoryTablePrivate::metadata(this));
}

MemoryTable::value_type MemoryTable::value(const key_type &key) const
{
    if (!mBase)
        return value_type();

    return MemoryTablePrivate::value(key, MemoryTablePrivate::metadata(this));
}

MemoryTable::Error MemoryTable::insert(const key_type &key, const value_type &value)
{
    if (!mBase)
        return NotAttached;

    return MemoryTablePrivate::insert(key, value, MemoryTablePrivate::metadata(this));
}

bool MemoryTable::remove(const key_type &key)
{
    if (!mBase)
        return false;

    return MemoryTablePrivate::remove(key, MemoryTablePrivate::metadata(this));
}

MemoryTable::key_type MemoryTable::keyAt(size_t index) const
{
    if (!mBase)
        return key_type();

    return MemoryTablePrivate::keyAtIndex(index, MemoryTablePrivate::metadata(this));
}

MemoryTable::value_type MemoryTable::valueAt(size_t index) const
{
    if (!mBase)
        return value_type();

    return MemoryTablePrivate::valueAtIndex(index, MemoryTablePrivate::metadata(this));
}

MemoryTable::const_iterator MemoryTable::constBegin() const
{
    return const_iterator(this, 0);
}

MemoryTable::const_iterator MemoryTable::constEnd() const
{
    return const_iterator(this, count());
}

MemoryTable::Error MemoryTable::migrateTo(MemoryTable &other) const
{
    if (!mBase || !other.mBase)
        return NotAttached;

    return MemoryTablePrivate::migrateTo(MemoryTablePrivate::metadata(&other), MemoryTablePrivate::metadata(this));
}

MemoryTable::const_iterator::const_iterator(const MemoryTable *table, quint32 position)
    : table(table)
    , position(position)
{
}

MemoryTable::const_iterator::const_iterator()
    : table(0)
    , position(0)
{
}

MemoryTable::const_iterator::const_iterator(const const_iterator &other)
{
    *this = other;
}

MemoryTable::const_iterator &MemoryTable::const_iterator::operator=(const const_iterator &other)
{
    table = other.table;
    position = other.position;
    return *this;
}

bool MemoryTable::const_iterator::operator==(const const_iterator &other)
{
    return (table == other.table && position == other.position);
}

bool MemoryTable::const_iterator::operator!=(const const_iterator &other)
{
    return !(*this == other);
}

MemoryTable::key_type MemoryTable::const_iterator::key()
{
    if (!table)
        return MemoryTable::key_type();

    return table->keyAt(position);
}

MemoryTable::value_type MemoryTable::const_iterator::value()
{
    if (!table)
        return MemoryTable::value_type();

    return table->valueAt(position);
}

const MemoryTable::const_iterator &MemoryTable::const_iterator::operator++()
{
    ++position;
    return *this;
}

