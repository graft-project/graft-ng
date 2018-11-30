#include "lib/graft/blacklist.h"
#include "lib/graft/mongoosex.h"
#include "radix_tree/radix_tree.hpp"
#include <sstream>
#include <fstream>
#include <regex>

namespace graft {

class BlackListImpl : public BlackList
{
public:
    ~BlackListImpl() = default;

    //radix table entry
    struct RtEntry
    {
        //host order
        in_addr_t m_addr = 0;
        int m_prefixLen = 0;

        //
        in_addr_t operator [] (int n) const
        {
            return (m_addr & (0x80000000 >> n))? 1 : 0;
        }

        bool operator == (const RtEntry &rhs) const
        {
            return m_prefixLen == rhs.m_prefixLen && m_addr == rhs.m_addr;
        }

        bool operator< (const RtEntry &rhs) const
        {
            if (m_addr == rhs.m_addr)
                return m_prefixLen < rhs.m_prefixLen;
            else
                return m_addr < rhs.m_addr;
        }
    };

private:
    using Table = radix_tree<RtEntry, Allow>;

    Table m_table;
    Allow m_defaultAllow = true;
    std::ostringstream m_warns;

    void addRule(Allow allow, const char* ip, int len, int line)
    {
        assert(0 < len && len <= 32);
        in_addr addr;
        if(inet_aton(ip, &addr) == 0)
        {
            m_warns << "error: invalid address " << ip << " at line " << line << '\n';
            throw std::runtime_error("invalid address error; addRule");
        }
        addr.s_addr = ntohl(addr.s_addr);

        in_addr_t mask = -1;
        if(len < 32)
        {
            mask = ~((1 << (32 - len)) - 1);
        }
        addr.s_addr &= mask;

        if(find(addr.s_addr, false).first)
        {
            m_warns << "warning: the rule at line " << line << " is superceded by one of previous rule\n";
            return;
        }

        RtEntry entry{addr.s_addr, len};
        m_table.insert(std::make_pair(entry,allow));
    }
public:
    virtual std::string getWarnings() override
    {
        return m_warns.str();
    }

    virtual void addEntry(const char* ip, int len, Allow allow) override
    {
        assert(0<len && len<=32);
        in_addr addr;
        int res = inet_aton(ip,&addr);
        assert(res);
        addEntry(addr.s_addr, true, allow, len);
    }
    virtual void addEntry(in_addr_t addr, bool networkOrder, Allow allow, int len) override
    {
        assert(0<len && len<=32);
        if(networkOrder) addr = ntohl(addr);
        RtEntry entry{ addr, len };
        m_table.insert(std::make_pair(entry,allow));
    }

    virtual std::pair<bool, Allow> find(in_addr_t addr, bool networkOrder) override
    {
        if(networkOrder) addr = ntohl(addr);

        RtEntry entry{addr, 32};

        Table::iterator it = m_table.longest_match(entry);
        if (it == m_table.end())
        {
            return std::make_pair(false, m_defaultAllow);
        }
        return std::make_pair(true, it->second);
    }

    virtual std::pair<bool, Allow> find(const char* ip) override
    {
        in_addr addr;
        if(!inet_aton(ip, &addr))
        {
            std::stringstream ss;
            ss << "invalid address " << ip << " in find()";
            throw std::runtime_error(ss.str());
        }
        return find(addr.s_addr, true);
    }

    virtual void readRules(const char* filepath)
    {
        std::ifstream ifs(filepath);
        if(!ifs.is_open())
        {
            std::ostringstream oss;
            oss << "cannot open file: " << filepath;
            throw std::runtime_error(oss.str());
        }
        readRules(ifs);
    }

    virtual void readRules(std::istream& is) override
    {
        m_warns.clear();
        m_table.clear();
        bool terminator_found = false;
        for(int line = 1; ; ++line)
        {
            if(is.eof()) break;
            std::string s;
            std::getline(is, s);
            if(is.fail() && !is.eof())
            {
                m_warns << "error: reading error '" << s << "' at line " << line << '\n';
                throw std::runtime_error("reading error");
            }

            {//remove comment ;;
                std::regex r(R"(^\s*(.*?);;.*(\r)?$)");
                s = std::regex_replace(s, r, "$1");
            }
            {//trim
                std::regex r(R"(^\s*(.*?)\s*(\r)?$)");
                s = std::regex_replace(s, r, "$1");
            }

            if(s.empty()) continue;
            if(terminator_found)
            {
                m_warns << "warning: all rules are superseded starting from line " << line << '\n';
                break;
            }
            std::regex regex(R"(^(allow|deny)\s+(all|((\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})(/(\d{1,2}))?))$)");
            std::smatch m;
            if(!std::regex_match(s, m, regex))
            {
                m_warns << "error: invalid rule format '" << s << "' at line " << line << '\n';
                throw std::runtime_error("invalid rule");
            }
            assert(2 < m.size());
            assert(m[1] == "allow" || m[1] == "deny");
            bool allow = (m[1] == "allow")? true : false;
            if(m[2] == "all")
            {//allow|deny all
                terminator_found = true;
                m_defaultAllow = allow;
            }
            else
            {
                assert(4 < m.size());
                std::string ip = m[4];
                int prefix_len = 32;
                if(6 < m.size() && m[6].matched)
                {
                    prefix_len = std::stoi(m[6]);
                    if(prefix_len == 0 || 32 < prefix_len)
                    {
                        m_warns << "error: invalid mask length " << prefix_len << " at line " << line << '\n';
                        throw std::runtime_error("invalid mask");
                    }
                }
                addRule(allow, ip.c_str(), prefix_len, line);
            }
        }
    }
};

std::unique_ptr<BlackList> BlackList::Create()
{
    return std::make_unique<BlackListImpl>();
}

} //namespace graft

template<>
inline int radix_length<graft::BlackListImpl::RtEntry>(const graft::BlackListImpl::RtEntry &entry)
{
    return entry.m_prefixLen;
}

template<>
inline graft::BlackListImpl::RtEntry radix_substr<graft::BlackListImpl::RtEntry>(const graft::BlackListImpl::RtEntry &entry, int begin, int num)
{
    assert(num + begin <= 32);
    in_addr_t mask;
    if (num == 32) mask = 0;
    else mask = 1 << num;
    mask  -= 1;
    mask <<= 32 - num - begin;

    graft::BlackListImpl::RtEntry res;
    res.m_addr = (entry.m_addr & mask) << begin;
    res.m_prefixLen = num;
    return res;
}
