#define __GRAFTLET__
#include "GraftletRegistry.h"
#include "IGraftlet.h"

#include<cassert>

class TestGraftlet: public IGraftlet
{
    std::string bbb{"bla bla xxx hfnoernergnerng pfgoepgokperogkpergk pgjpgjpergjprgj "};
    char buf[0x100];
public:
    TestGraftlet(const char* name) : IGraftlet(name) { }

    std::string getString(const std::string in, std::string& out)
    {
        out = "out " + in;
        return "res " +in;
    }

    void getString0(std::string&& rs)
    {
        std::string s = std::move(rs);
        std::cout << "getString0 : " << s << std::endl;
    }

    void getString1(std::string& s)
    {
        std::cout << s << std::endl;
        s = "getString1 set result";
    }

    std::string getString11(std::string& s)
    {
        s = "getString11";
        return "res getString11";
    }

    int getInt11(int& s)
    {
        s = 11;
        return 21;
    }

    std::string getString12(std::string& s,std::string& s1)
    {
        s = "getString11";
        s1 = "getString11 1";
        return "res getString11";
    }

    void getString2(int i1, int i2)
    {
        std::cout << i1 + i2 << std::endl;
    }

    void getString3(std::string slv, std::string& sr, std::string* pstr, char** res_buf)
    {
        static_assert( !std::is_reference<decltype(slv)>::value, "slv" );
        static_assert( std::is_lvalue_reference<decltype(sr)>::value, "sr" );
        sr = std::string("sr result = oigjmg  gopk4g5ko4gk gok4gk4g gopk4g[k") + slv;
        *pstr = sr;
        *pstr = bbb.c_str();
        *res_buf = buf;
        std::cout << "in getString3 sr =" << sr  << std::endl;
    }

    std::string getString4(std::string&& srv, std::string slv, std::string& sr)
    {
        sr = srv + slv;
        return "res " + slv + srv + sr;
    }

    virtual void init()
    {
        REGISTER_ACTIONX1(TestGraftlet, getString12);
        REGISTER_ACTIONX1(TestGraftlet, getString11);
        REGISTER_ACTIONX1(TestGraftlet, getInt11);
        REGISTER_ACTIONX1(TestGraftlet, getString0);
        REGISTER_ACTIONX1(TestGraftlet, getString1);
        REGISTER_ACTIONX1(TestGraftlet, getString2);
        REGISTER_ACTIONX1(TestGraftlet, getString3);
        REGISTER_ACTIONX1(TestGraftlet, getString4);
//////////////
//        REGISTER_ENDPOINT("URI/{id:}", TestGraftlet, getString3)
    }
};

GRAFTLET_EXPORTS_BEGIN("myGraftlet", 0x10000);
GRAFTLET_PLUGIN(TestGraftlet, IGraftlet, "testGL");
GRAFTLET_EXPORTS_END

namespace
{

struct Informer
{
    Informer()
    {
        std::cout << "graftlet " << getGraftletName() << " loading\n";
    }
    ~Informer()
    {
        std::cout << "graftlet " << getGraftletName() << " unloading\n";
    }
};

Informer informer;

} //namespace graftlet


