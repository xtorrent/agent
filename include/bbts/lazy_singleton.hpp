/***************************************************************************
 * 
 * Copyright (c) 2013 Baidu.com, Inc. All Rights Reserved
 * 
 **************************************************************************/

/**
 * @file   lazy_singleton.hpp
 *
 * @author liuming03
 * @date   2014-3-24
 * @brief 
 */

#ifndef OP_OPED_NOAH_BBTS_AGENT_LAZY_SINGLETON_HPP
#define OP_OPED_NOAH_BBTS_AGENT_LAZY_SINGLETON_HPP

#include <assert.h>

#include <boost/noncopyable.hpp>
#include <boost/scoped_ptr.hpp>

#include "bbts/type_lock.hpp"

namespace bbts {

template<typename Type>
class LazySingleton : public boost::noncopyable {
public:
    static Type* instance() {
        if (!_s_instance) {
            TypeLock<LazySingleton<Type> > lock;
            if (!_s_instance) {
                _s_instance.reset(new Type());
            }
        }
        assert(_s_instance);
        return _s_instance.get();
    }

private:
    LazySingleton();
    ~LazySingleton();

    static boost::scoped_ptr<Type> _s_instance;
};

template<typename Type>
boost::scoped_ptr<Type> LazySingleton<Type>::_s_instance;

}  // namespace bbts

#endif // OP_OPED_NOAH_BBTS_AGENT_LAZY_SINGLETON_HPP
