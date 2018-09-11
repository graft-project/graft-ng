#include <gtest/gtest.h>
#include "GraftletLoader.h"
#include "IGraftlet.h"

/////////////////////////////////
// Graftlets fixture

class Graftlets : public ::testing::Test
{
public:

protected:
    virtual void SetUp() override
    { }
    virtual void TearDown() override
    { }

};

TEST(Graftlets, commonX)
{
    graftlet::GraftletLoader loader;

    loader.findGraftletsAtDirectory("./", "so");
    loader.findGraftletsAtDirectory("./graftlets", "so");

    graftlet::GraftletExport<IGraftlet> plugin = loader.buildAndResolveGraftletX<IGraftlet>("myGraftlet");
    {
        std::string s = "aaaa";
        std::string& r = s;
        try
        {
//            bool ok = plugin.invoke<std::string&>("testGL.getString1", std::ref(r));
            bool ok = plugin.invoke<std::string&>("testGL.getString1", r);
            if(ok)
            {
                std::cout << r << "\n";
            }
            else
            {
                std::cout << "\t plugin.invoke(\"testGL.getString1\", r); failed!!!\n";
            }
        }
        catch(std::exception& ex)
        {
            std::cout << ex.what() << "\n";
        }

        try
        {

            bool ok = plugin.invoke<int,int>("testGL.getString2", 9, 8);
            if(!ok)
            {
                std::cout << "\t plugin.invoke<int,int>(\"testGL.getString2\", 9, 8); failed!!!\n";
            }
        }
        catch(std::exception& ex)
        {
            std::cout << ex.what() << "\n";
        }

        try
        {
            std::cout << "\n\ninvoke testGL.getString3\n";
            std::string slv = "This is slv";
            std::string sr = "This is sr";
            std::string pstr = "This is pstr";

            char* buf = nullptr;
//            bool ok = plugin.invoke("testGL.getString3", slv, sr, &pstr, &buf);
            bool ok = plugin.invoke<std::string,std::string&,std::string*,char**>("testGL.getString3", slv, sr, &pstr, &buf);
            if(ok)
            {
                std::cout << "sr=" << sr << "\n";
                std::cout << "pstr=" << pstr << "\n";
                pstr[2] = 'c';
                std::cout << "pstr=" << pstr << "\n";
                strcpy(buf, "aaaaa");
                bool ok = plugin.invoke("testGL.getString3", slv, sr, &pstr, &buf);
                std::cout << "pstr=" << pstr << "\n";
            }
            else
            {
                std::cout << "\t plugin.invoke(\"testGL.getString3\", r); failed!!!\n";
            }
        }
        catch(std::exception& ex)
        {
            std::cout << ex.what() << "\n";
        }
/*
        try
        {
//            void getString4(std::string&& srv, std::string slv, std::string& sr)


            std::cout << "\n\ninvoke testGL.getString4\n";
            std::string srv = "This is srv";
            std::string slv = "This is slv";
            std::string sr = "This is sr";

//            bool ok = plugin.invoke("testGL.getString3", slv, sr, &pstr, &buf);
//            bool ok = plugin.invoke<std::string&&, std::string, std::string&>("testGL.getString4", std::move(srv), slv, sr);
            bool ok = plugin.invoke<std::string&&, std::string, std::string&>("testGL.getString4", std::move(srv), slv, sr);
            if(ok)
            {
                std::cout << "sr=" << sr << "\n";
            }
            else
            {
                std::cout << "\t plugin.invoke(\"testGL.getString4\", r); failed!!!\n";
            }
        }
        catch(std::exception& ex)
        {
            std::cout << ex.what() << "\n";
        }
*/
    }

    std::cout << "the main() end" << "\n";
//    return 0;
}

TEST(Graftlets, common)
{
    graftlet::GraftletLoader loader;

    loader.findGraftletsAtDirectory("./", "so");
    loader.findGraftletsAtDirectory("./graftlets", "so");

//    std::vector<std::shared_ptr<IGraftlet>> plugins = loader.buildAndResolveGraftlet<IGraftlet>("myGraftlet");
    std::vector<std::shared_ptr<IGraftlet>> plugins = loader.buildAndResolveGraftlet<IGraftlet>("myGraftlet.testGL");

    for (auto& iter: plugins)
    {
        std::string s = "aaaa";
        std::string& r = s;
        try
        {
//            bool ok = iter->invoke<std::string&>("getString1", std::ref(r));
            bool ok = iter->invoke<std::string&>("getString1", r);
            if(ok)
            {
                std::cout << r << "\n";
            }
            else
            {
                std::cout << "\t iter->invoke(\"getString1\", r); failed!!!\n";
            }
        }
        catch(std::exception& ex)
        {
            std::cout << ex.what() << "\n";
        }

        try
        {

            bool ok = iter->invoke<int,int>("getString2", 9, 8);
            if(!ok)
            {
                std::cout << "\t iter->invoke<int,int>(\"getString2\", 9, 8); failed!!!\n";
            }
        }
        catch(std::exception& ex)
        {
            std::cout << ex.what() << "\n";
        }

        try
        {
            std::cout << "\n\ninvoke getString3\n";
            std::string slv = "This is slv";
            std::string sr = "This is sr";
            std::string pstr = "This is pstr";

            char* buf = nullptr;
//            bool ok = iter->invoke("getString3", slv, sr, &pstr, &buf);
            bool ok = iter->invoke<std::string,std::string&,std::string*,char**>("getString3", slv, sr, &pstr, &buf);
            if(ok)
            {
                std::cout << "sr=" << sr << "\n";
                std::cout << "pstr=" << pstr << "\n";
                pstr[2] = 'c';
                std::cout << "pstr=" << pstr << "\n";
                strcpy(buf, "aaaaa");
                bool ok = iter->invoke("getString3", slv, sr, &pstr, &buf);
                std::cout << "pstr=" << pstr << "\n";
            }
            else
            {
                std::cout << "\t iter->invoke(\"getString3\", r); failed!!!\n";
            }
        }
        catch(std::exception& ex)
        {
            std::cout << ex.what() << "\n";
        }
/*
        try
        {
//            void getString4(std::string&& srv, std::string slv, std::string& sr)


            std::cout << "\n\ninvoke getString4\n";
            std::string srv = "This is srv";
            std::string slv = "This is slv";
            std::string sr = "This is sr";

//            bool ok = iter->invoke("getString3", slv, sr, &pstr, &buf);
//            bool ok = iter->invoke<std::string&&, std::string, std::string&>("getString4", std::move(srv), slv, sr);
            bool ok = iter->invoke<std::string&&, std::string, std::string&>("getString4", std::move(srv), slv, sr);
            if(ok)
            {
                std::cout << "sr=" << sr << "\n";
            }
            else
            {
                std::cout << "\t iter->invoke(\"getString4\", r); failed!!!\n";
            }
        }
        catch(std::exception& ex)
        {
            std::cout << ex.what() << "\n";
        }
*/
    }

    std::cout << "the main() end" << "\n";
//    return 0;
}
