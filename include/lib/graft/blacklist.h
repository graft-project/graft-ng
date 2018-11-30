#pragma once

#include <utility>
#include <cstdint>
#include <functional>
#include <memory>

typedef uint32_t in_addr_t;

namespace graft {

class BlackList
{
public:
    using Allow = bool;

    static std::unique_ptr<BlackList> Create();

    virtual ~BlackList() = default;

    virtual void addEntry(const char* ip, int len = 32, Allow allow = false) = 0;
    virtual void addEntry(in_addr_t addr, bool networkOrder = true, Allow allow = false, int len = 32) = 0;
    //returns a pair, first is true if a corresponding entry found, second is true if the addr is effectively allowed
    virtual std::pair<bool, Allow> find(in_addr_t addr, bool networkOrder = true) = 0;
    virtual std::pair<bool, Allow> find(const char* ip) = 0;
    virtual void readRules(const char* filepath) = 0;
    virtual void readRules(std::istream& is) = 0;
    virtual std::string getWarnings() = 0;
protected:
    BlackList(const BlackList&) = delete;
    BlackList& operator = (const BlackList&) = delete;
    BlackList() = default;
};

} //namespace graft
