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
        *pstr = bbb.c_str(); // std::string("bla bla bla").c_str();
        *res_buf = buf;
        std::cout << "in getString3 sr =" << sr  << std::endl;
    }

    std::string getString4(std::string&& srv, std::string slv, std::string& sr)
    {
        sr = srv + slv;
        return "res " + slv + srv + sr;
/*
        static_assert( std::is_rvalue_reference<decltype(srv)>::value, "srv" );
        static_assert( !std::is_reference<decltype(slv)>::value, "slv" );
        static_assert( std::is_lvalue_reference<decltype(sr)>::value, "sr" );
        sr = std::string("sr result = ") + srv + slv;
        std::cout << "sr =" << sr  << std::endl;
*/
    }

    virtual void init()
    {
/*
        {

//            using This = decltype(*this);
            std::function<std::string (TestGraftlet*, std::string&, std::string&)> fun1 = &TestGraftlet::getString12;
            std::function<std::string (std::string&, std::string&)> fun2 = [this, fun1](std::string& s, std::string& s1)
            {
//                auto f = &TestGraftlet::getString12;
                return fun1(this,s,s1);
            };
//            std::function<std::string (std::string&, std::string&)> fun = [this,]
            //register_handlerX<std::string>(std::string("getString12"), fun1);

            register_handler_memf1(std::string("getString12"), this, fun1);
//            register_handler_memf(std::string("getString12"), this, fun1);
        }
*/
        {
            using std::placeholders::_1;
            using std::placeholders::_2;
            std::function<std::string (std::string& s)> fun = std::bind( &TestGraftlet::getString11, this, _1 );
            std::function<std::string (std::string& s, std::string& s1)> fun1 = std::bind( &TestGraftlet::getString12, this, _1, _2 );
        }

        REGISTER_ACTIONX1(TestGraftlet, getString12);
        REGISTER_ACTIONX1(TestGraftlet, getString11);
        REGISTER_ACTIONX1(TestGraftlet, getInt11);
//        REGISTER_ACTIONX(TestGraftlet, getString);
        REGISTER_ACTIONX1(TestGraftlet, getString0);
//        REGISTER_ACTION(TestGraftlet, getString0);
        REGISTER_ACTIONX(TestGraftlet, getString1);
        REGISTER_ACTION(TestGraftlet, getString1);
        REGISTER_ACTION(TestGraftlet, getString2);
        REGISTER_ACTION(TestGraftlet, getString3);
//        REGISTER_ACTION(TestGraftlet, getString4);
        REGISTER_ACTIONX1(TestGraftlet, getString4);
//////////////
        //////////////
        //////////////
        //////////////
        //////////////
//        REGISTER_ENDPOINT("URI/{id:}", TestGraftlet, getString3)

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


