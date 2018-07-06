#pragma once

#include <memory>

namespace graft {

template<typename C>
class SelfHolder
{
public:
    using Ptr = std::shared_ptr<C>;
public:
    Ptr get_itself() { return m_itself; }

    template<typename T=C, typename ...ARGS>
    static const Ptr Create(ARGS&&... args)
    {
        return (new T(std::forward<ARGS>(args)...))->m_itself;
    }
protected:
    void releaseItself() { m_itself.reset(); }
protected:
    SelfHolder() : m_itself(static_cast<C*>(this)) { }
private:
    Ptr m_itself;
};

}//namespace graft

