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

#ifndef MEMORYTABLE_H
#define MEMORYTABLE_H

#include <QByteArray>

#include <iterator>

class MemoryTablePrivate;
class MemoryTable
{
public:
    typedef quint32 key_type;
    typedef QByteArray value_type;

    class const_iterator
        : public std::iterator<std::forward_iterator_tag, MemoryTable::value_type, std::ptrdiff_t, const MemoryTable::value_type *, const MemoryTable::value_type &>
    {
        friend class MemoryTable;

    protected:
        const_iterator(const MemoryTable *table, quint32 position);

        const MemoryTable *table;
        quint32 position;

    public:
        const_iterator();
        const_iterator(const const_iterator &other);
        const_iterator &operator=(const const_iterator &other);

        bool operator==(const const_iterator &other);
        bool operator!=(const const_iterator &other);

        MemoryTable::key_type key();
        MemoryTable::value_type value();

        const const_iterator &operator++();
    };

    MemoryTable(void *base, size_t size, bool initialize);
    ~MemoryTable();

    enum Error {
        NoError = 0,
        NotAttached,
        InsufficientSpace
    };

    bool isValid() const;

    size_t count() const;
    bool contains(const key_type &key) const;
    value_type value(const key_type &key) const;
    Error insert(const key_type &key, const value_type &value);
    bool remove(const key_type &key);

    key_type keyAt(size_t index) const;
    value_type valueAt(size_t index) const;

    const_iterator constBegin() const;
    const_iterator constEnd() const;

    Error migrateTo(MemoryTable &other) const;

private:
    MemoryTable(const MemoryTable &);
    MemoryTable &operator=(const MemoryTable &);

    friend class MemoryTablePrivate;

    void *mBase;
    size_t mSize;
};

#endif
