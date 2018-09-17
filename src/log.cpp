#include "log.h"

namespace graft
{

std::string make_dump_output(const std::string& in, int trunc_size)
{
    static char mask[0x100];
    static std::string val[0x100];
    static bool inited = false;
    if(!inited)
    {
        inited = true;
        for(int i=0; i<0x100; ++i)
        {
            int i1 = i/16, i2 = i%16;
            char ch1 = (i1<10)? ('0'+i1) : ('A'+i1-10);
            char ch2 = (i2<10)? ('0'+i2) : ('A'+i2-10);
            val[i] = std::string("\\x") + ch1 + ch2;
        }
        val['\t'] = "\\t"; val['\r'] = "\\r"; val['\n'] = "\\n";

        for(int i=0; i<0x100; ++i) mask[i] = 2;
        mask['\t'] = 1; mask['\r'] = 1; mask['\n'] = 1;
        for(int i=0x20; i<0x7F; ++i) mask[i] = 0;
    }

    std::string res;
    bool bin_found = false;
    for(auto it = in.begin(), eit = in.end(); it !=eit; ++it)
    {
        int ch = (unsigned char)*it;
        if(mask[ch]==2) bin_found = true;
        if(mask[ch]==0)
        {
            res += ch;
        }
        else
        {
            res += val[ch];
        }
//        if(bin_found && hint_len < res.size())
        if(0<=trunc_size && trunc_size <= res.size())
        {
            res += "...";
            break;
        }
    }
    return res;
}

}//namespace graft
