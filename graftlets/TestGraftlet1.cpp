#define __GRAFTLET__
#include "GraftletRegistry.h"
#include "IGraftlet.h"

#include<cassert>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "graftlet.TestGraftlet1"

class TestGraftlet1: public IGraftlet
{
public:
    TestGraftlet1(const char* name) : IGraftlet(name) { }

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
//        REGISTER_ACTION(TestGraftlet1, testUndefined);
        REGISTER_ACTION(TestGraftlet1, testInt1);
        REGISTER_ACTION(TestGraftlet1, testInt2);
        REGISTER_ACTION(TestGraftlet1, testString1);
        REGISTER_ACTION(TestGraftlet1, testString2);

        REGISTER_ENDPOINT("/URI1/test/{id:[0-9]+}", METHOD_GET | METHOD_POST, TestGraftlet1, testHandler);
        REGISTER_ENDPOINT("/URI1/test1/{id:[0-9]+}", METHOD_GET | METHOD_POST, TestGraftlet1, testHandler1);
    }
};

GRAFTLET_EXPORTS_BEGIN("myGraftlet1", GRAFTLET_MKVER(4,2));
GRAFTLET_PLUGIN(TestGraftlet1, IGraftlet, "testGL1");
GRAFTLET_EXPORTS_END

GRAFTLET_PLUGIN_DEFAULT_CHECK_FW_VERSION(GRAFTLET_MKVER(1,0))

namespace
{

struct Informer
{
    Informer()
    {
        LOG_PRINT_L2("graftlet " << getGraftletName() << " loading");
    }
    ~Informer()
    {
        LOG_PRINT_L2("graftlet " << getGraftletName() << " unloading");
    }
};

Informer informer;

} //namespace


