/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   type_lock.hpp
 *
 * @author liuming03
 * @date   2014-3-24
 * @brief 
 */

#ifndef OP_OPED_NOAH_BBTS_AGENT_TYPE_LOCK_HPP
#define OP_OPED_NOAH_BBTS_AGENT_TYPE_LOCK_HPP

#include <boost/noncopyable.hpp>
#include <boost/thread/mutex.hpp>

namespace bbts {

template<typename Type>
class TypeLock : public boost::noncopyable {
public:
    // TypeLock lock;
    TypeLock() : _lock(_s_mutex) {}

    ~TypeLock() {}

    // TypeLock<T>::lock();
    static void lock() {
        _s_mutex.lock();
    }

    // TypeLock<T>::unlock();
    static void unlock() {
        _s_mutex.unlock();
    }

private:
    TypeLock* operator &();
    TypeLock* operator &() const;

    boost::mutex::scoped_lock _lock;
    static boost::mutex _s_mutex;
};

template<typename Type>
boost::mutex TypeLock<Type>::_s_mutex;

}  // namespace bbts

#endif // OP_OPED_NOAH_BBTS_AGENT_TYPE_LOCK_HPP
