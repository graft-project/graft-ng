#define __GRAFTLET__
#include "GraftletRegistry.h"
#include "IGraftlet.h"

#include<cassert>

class TestGraftlet: public IGraftlet
{
    std::string bbb{"bla bla xxx hfnoernergnerng pfgoepgokperogkpergk pgjpgjpergjprgj "};
    char buf[0x100];
public:
//    TestGraftlet() : IGraftlet("##testGL") { }
    TestGraftlet(const char* name) : IGraftlet(name) { }

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
        *pstr = bbb.c_str(); // std::string("bla bla bla").c_str();
        *res_buf = buf;
        std::cout << "in getString3 sr =" << sr  << std::endl;
    }

    void getString4(std::string&& srv, std::string slv, std::string& sr)
    {
        static_assert( std::is_rvalue_reference<decltype(srv)>::value, "srv" );
        static_assert( !std::is_reference<decltype(slv)>::value, "slv" );
        static_assert( std::is_lvalue_reference<decltype(sr)>::value, "sr" );
        sr = std::string("sr result = ") + srv + slv;
        std::cout << "sr =" << sr  << std::endl;
    }

    virtual void init()
    {
//        REGISTER_ACTION(TestGraftlet, getString0);
        REGISTER_ACTIONX(TestGraftlet, getString1);
        REGISTER_ACTION(TestGraftlet, getString1);
        REGISTER_ACTION(TestGraftlet, getString2);
        REGISTER_ACTION(TestGraftlet, getString3);
//        REGISTER_ACTION(TestGraftlet, getString4);

//        auto a = &
        assert(std::type_index(typeid(void(int))) != std::type_index(typeid(void(const char*))));

        auto a = &TestGraftlet::getString1;
        std::cout << "type_indexes\n";
        std::cout << "1 = " << typeid(void(int)).name() << " hash = " << typeid(void(int)).hash_code() << "\n";
        std::cout << "1 = " << typeid(void(const char*)).name() << " hash = " << typeid(void(const char*)).hash_code() << "\n";
        std::cout << "getString1 = " << typeid(decltype(&TestGraftlet::getString1)).name() << " hash = " << typeid(decltype(&TestGraftlet::getString1)).hash_code() << "\n";
        std::cout << "getString2 = " << typeid(decltype(&TestGraftlet::getString2)).name() << " hash = " << typeid(decltype(&TestGraftlet::getString2)).hash_code() << "\n";
        std::cout << "getString3 = " << typeid(decltype(&TestGraftlet::getString3)).name() << " hash = " << typeid(decltype(&TestGraftlet::getString3)).hash_code() << "\n\n";
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


