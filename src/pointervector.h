/*
 *  pointervector.h
 *
 *  Copyright (C) 2008 - Niko Ritari
 *
 *  This file is part of Outgun.
 *
 *  Outgun is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Outgun is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Outgun; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef POINTERVECTOR_H_INC
#define POINTERVECTOR_H_INC

#include <vector>

#include "utility.h"

/** Wrapper for STL iterators of containers to pointers, making the pointers transparent.
 * A general class to cover all iterator types, not necessarily fully compliant with the standard
 * interface, but code that compiles should work as expected.
 */
template<class I, class T, class DistanceT> class PointerIterator {
    I i;

public:
    PointerIterator(const I& i_) throw () : i(i_) { }

          I rawIterator()       throw () { return i; }
    const I rawIterator() const throw () { return i; }

    // trivial iterator
    PointerIterator() throw () { }

          T& operator*()       throw () { return **i; }
    const T& operator*() const throw () { return **i; }
          T* operator->()       throw () { return *i; }
    const T* operator->() const throw () { return *i; }

    bool operator==(const PointerIterator& o) const throw () { return i == o.i; }
    bool operator!=(const PointerIterator& o) const throw () { return i != o.i; }

    // forward iterator
    PointerIterator& operator++() throw () { ++i; return *this; }
    PointerIterator operator++(int) throw () { const PointerIterator ret = *this; ++i; return ret; }

    // bidirectional iterator
    PointerIterator& operator--() throw () { --i; return *this; }
    PointerIterator operator--(int) throw () { const PointerIterator ret = *this; --i; return ret; }

    // random access iterator
    PointerIterator& operator+=(DistanceT d) throw () { i += d; return *this; }
    PointerIterator& operator-=(DistanceT d) throw () { i -= d; return *this; }
    PointerIterator operator+(DistanceT d) throw () { PointerIterator ret = *this; ret += d; return ret; }
    PointerIterator operator-(DistanceT d) throw () { PointerIterator ret = *this; ret -= d; return ret; }
    DistanceT operator-(const PointerIterator& o) const throw () { return i - o.i; }

          T& operator[](DistanceT d)       throw () { return **(i + d); }
    const T& operator[](DistanceT d) const throw () { return **(i + d); }

    bool operator<(const PointerIterator& o) const throw () { return i < o.i; }
};

/** Vector for classes that can't be copied, or base classes where derived objects are to be stored.
 * To be used instead of std::vector<T*> when the objects need to be automatically deleted when erased
 * from the container.
 * Other than in adding new elements, the interface matches std::vector<T> (not std::vector<T*>), but
 * some methods that would require copying of elements are missing.
 */
template<class T> class PointerVector : public NoCopying {
    typedef std::vector<T*> PVecT;
    PVecT v;

public:
    typedef T value_type;
    typedef T* pointer;
    typedef T& reference;
    typedef const T& const_reference;
    typedef typename PVecT::size_type size_type;
    typedef typename PVecT::difference_type difference_type;
    typedef PointerIterator<typename PVecT::iterator, T, difference_type> iterator;
    typedef PointerIterator<typename PVecT::const_iterator, const T, difference_type> const_iterator;
    typedef PointerIterator<typename PVecT::reverse_iterator, T, difference_type> reverse_iterator;
    typedef PointerIterator<typename PVecT::const_reverse_iterator, const T, difference_type> const_reverse_iterator;

    PointerVector() throw () { }
    explicit PointerVector(size_type n) throw () : v(n) { }
    ~PointerVector() throw ();

    iterator begin() throw () { return v.begin(); }
    iterator end() throw () { return v.end(); }
    const_iterator begin() const throw () { return v.begin(); }
    const_iterator end() const throw () { return v.end(); }

    reverse_iterator rbegin() throw () { return v.rbegin(); }
    reverse_iterator rend() throw () { return v.rend(); }
    const_reverse_iterator rbegin() const throw () { return v.rbegin(); }
    const_reverse_iterator rend() const throw () { return v.rend(); }

    size_type size() const throw () { return v.size(); }
    size_type max_size() const throw () { return v.max_size(); }
    size_type capacity() const throw () { return v.max_size(); }
    bool empty() const throw () { return v.empty(); }

    reference operator[](size_type n) throw () { return *v[n]; }
    const_reference operator[](size_type n) const throw () { return *v[n]; }

    void reserve(size_type n) throw () { v.reserve(n); }

    reference front() throw () { return *v.front(); }
    const_reference front() const throw () { return *v.front(); }
    reference back() throw () { return *v.back(); }
    const_reference back() const throw () { return *v.back(); }

    void push_back(ControlledPtr<T> p) throw () { v.push_back(p); }
    void pop_back() throw () { delete v.back(); v.pop_back(); }

    void swap(PointerVector& o) throw () { v.swap(o.v); }

    iterator insert(iterator pos, ControlledPtr<T> p) throw () { v.insert(pos.rawIterator(), p); }
    iterator erase(iterator pos) throw ();
    iterator erase(iterator first, iterator last) throw ();

    void clear() throw ();
    void resize(size_type n) throw ();
};

// template implementation

template<class T> PointerVector<T>::~PointerVector() throw () {
    for (typename PVecT::iterator i = v.begin(); i != v.end(); ++i)
        delete *i;
}

template<class T> typename PointerVector<T>::iterator PointerVector<T>::erase(iterator pos) throw () {
    delete *pos.rawIterator();
    return v.erase(pos.rawIterator());
}

template<class T> typename PointerVector<T>::iterator PointerVector<T>::erase(iterator first, iterator last) throw () {
    for (iterator i = first; i != last; ++i)
        delete *i.rawIterator();
    return v.erase(first, last);
}

template<class T> void PointerVector<T>::clear() throw () {
    for (typename PVecT::iterator i = v.begin(); i != v.end(); ++i)
        delete *i;
    v.clear();
}

template<class T> void PointerVector<T>::resize(size_type n) throw () {
    if (n < v.size())
        for (typename PVecT::iterator i = v.begin() + n; i != v.end(); ++i)
            delete *i;
    v.resize(n);
}

#endif
