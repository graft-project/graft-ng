#include "forwardrequest.h"
#include <fstream>
#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>

namespace graft {

void test()
{
    std::string s;
    {
        std::ifstream fs("req_dump.x", std::ios::binary);
        size_t sz = 0;
        fs.read(reinterpret_cast<char*>(&sz), sizeof(sz));
        s.resize(sz);
        fs.read(&s[0], sz);
        fs.close();
        //temp
        std::string r;
        int k = 0xaacc55dd;
        for(int i = 0; i<100; ++i)
        {
            std::string s1 = s;
//            for(auto& ch : s1) { ch+=k; ++k; bool s = k<0; k <<=1; if if(k%2) k*=3, ++k; else k/=2; }
            for(auto& ch : s1) { ch+=k; bool s = k<0; k <<=1; if(s) k|=1; }
            r += s1;
        }
        s = r;
    }
    std::string d;
    {
        size_t sz =ZSTD_compressBound(s.size());
        d.resize(sz);
    }
    std::cout << "\ncompression inpup size = " << s.size() << std::endl;
    for(int lvl = 1; lvl <= ZSTD_maxCLevel(); ++lvl)
    {
        auto begin = std::chrono::high_resolution_clock::now();
        size_t sz = ZSTD_compress(&d[0], d.size(), s.c_str(), s.size(), lvl);
        auto end = std::chrono::high_resolution_clock::now();

        std::cout << "level " << lvl << " sz = " << sz << "; time = " <<
                     std::chrono::duration_cast<std::chrono::microseconds>(end-begin).count() << std::endl;
    }

/*
    char src[] = "abcdefgijklmnopqrstuvwxyz";
    char* dst = new char[26];
    size_t sz = ZSTD_compress(dst, 26, src, 26, 5);
*/
}

void registerForwardRequest(Router &router)
{
    auto forward = [](const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)->graft::Status
    {
        if(ctx.local.getLastStatus() == graft::Status::None)
        {
            auto it = vars.equal_range("forward");
            auto& path = it.first->second;
            if(it.first == vars.end())
            {
                throw std::runtime_error("cannot find 'forward' var");
            }
            if(++it.first != it.second)
            {
                throw std::runtime_error("multiple 'forward' vars found");
            }
            output.body = input.body;
            output.path = path;
            return graft::Status::Forward;
        }
        if(ctx.local.getLastStatus() == graft::Status::Forward)
        {
            output.body = input.body;

            if("getblocks.bin" == vars.equal_range("forward").first->second)
            {
                std::ofstream fs("req_dump.x", std::ios::binary | std::ios::trunc);
                size_t sz = output.body.size();
                fs.write(reinterpret_cast<char*>(&sz), sizeof(sz));
                fs.write(output.body.c_str(), output.body.size());
                fs.close();
                //
                test();
            }
            return graft::Status::Ok;
        }
        return graft::Status::Error;
    };

    router.addRoute("/{forward:gethashes.bin|json_rpc|getblocks.bin|gettransactions|sendrawtransaction|getheight}",
                               METHOD_POST|METHOD_GET, graft::Router::Handler3(forward,nullptr,nullptr));
}

}
