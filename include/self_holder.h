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
        Dummy dummy;
        return (new T(std::forward<ARGS>(args)..., dummy))->m_self;
    }

    //class helper to protect call of constructors of derivative classes
    class Dummy
    {
        friend class SelfHolder;
        Dummy() = default;
    };
protected:
    void releaseItself() { m_self.reset(); }

    SelfHolder() : m_self(static_cast<C*>(this)) { }
private:
    Ptr m_self;
};

}//namespace graft

