#pragma once

#include <memory>

namespace graft {

template<typename C>
class SelfHolder
{
public:
    using Ptr = std::shared_ptr<C>;

    Ptr getSelf() { return m_self; }

    template<typename T=C, typename ...ARGS>
    static const Ptr Create(ARGS&&... args)
    {
        return (new T(std::forward<ARGS>(args)...))->m_self;
    }
protected:
    void releaseItself() { m_self.reset(); }

    SelfHolder() : m_self(static_cast<C*>(this)) { }
private:
    Ptr m_self;
};

}//namespace graft

