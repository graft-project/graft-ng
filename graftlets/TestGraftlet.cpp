#define __GRAFTLET__
#include "GraftletRegistry.h"
#include "IGraftlet.h"

#include<cassert>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "graftlet.TestGraftlet"

class TestGraftlet: public IGraftlet
{
public:
    TestGraftlet(const char* name) : IGraftlet(name) { }

    void testUndefined();

    int testInt1(int a) { return a; }
    int testInt2(int&& a, int b, int& c)
    {
        c = a + b;
        return c + a + b;
    }

    std::string testString1(std::string& s)
    {
        s = "testString1";
        return "res " + s;
    }

    std::string testString2(std::string&& srv, std::string slv, std::string& sr)
    {
        sr = srv + slv;
        return "res " + slv + srv + sr;
    }

    graft::Status testHandler(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        std::string id;
        {
            auto it = vars.find("id");
            if(it != vars.end()) id = it->second;
        }
        output.body = input.data() + id;
        return graft::Status::Ok;
    }

    graft::Status testHandler1(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output)
    {
        std::string id;
        {
            auto it = vars.find("id");
            if(it != vars.end()) id = it->second;
        }
        output.body = input.data() + id;
        return graft::Status::Ok;
    }

    virtual void initOnce()
    {
//        REGISTER_ACTION(TestGraftlet, testUndefined);
        REGISTER_ACTION(TestGraftlet, testInt1);
        REGISTER_ACTION(TestGraftlet, testInt2);
        REGISTER_ACTION(TestGraftlet, testString1);
        REGISTER_ACTION(TestGraftlet, testString2);

        REGISTER_ENDPOINT("/URI/test/{id:[0-9]+}", METHOD_GET | METHOD_POST, TestGraftlet, testHandler);
        REGISTER_ENDPOINT("/URI/test1/{id:[0-9]+}", METHOD_GET | METHOD_POST, TestGraftlet, testHandler1);
    }
};

GRAFTLET_EXPORTS_BEGIN("myGraftlet", GRAFTLET_MKVER(1,1));
GRAFTLET_PLUGIN(TestGraftlet, IGraftlet, "testGL");
GRAFTLET_EXPORTS_END

GRAFTLET_PLUGIN_DEFAULT_CHECK_FW_VERSION(GRAFTLET_MKVER(0,3))

namespace
{

struct Informer
{
    Informer()
    {
        LOG_PRINT_L2("graftlet " << getGraftletName() << " loading");
        std::cout << "graftlet " << getGraftletName() << " loading" << "\n";
    }
    ~Informer()
    {
        LOG_PRINT_L2("graftlet " << getGraftletName() << " unloading");
        std::cout << "graftlet " << getGraftletName() << " unloading" << "\n";
    }
};

Informer informer;

} //namespace


