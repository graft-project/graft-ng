#include <gtest/gtest.h>
#include "GraftletLoader.h"
#include "IGraftlet.h"

TEST(Graftlets, common)
{
    graftlet::GraftletLoader loader;

    loader.findGraftletsAtDirectory("./", "so");
    loader.findGraftletsAtDirectory("./graftlets", "so");

    graftlet::GraftletExport<IGraftlet> plugin = loader.buildAndResolveGraftletX<IGraftlet>("myGraftlet");

    try
    {//testInt1, testInt2
        int res = plugin.invoke<int,int>("testGL.testInt1", 5);
        EXPECT_EQ(res, 5);
        res = plugin.invokeS<int (int a)>("testGL.testInt1", 7);
        EXPECT_EQ(res, 7);

        int a = 7;
        res = plugin.invoke<int, int&&, int, int&>("testGL.testInt2", 3, 5, a);
        EXPECT_EQ(a, 3 + 5);
        EXPECT_EQ(res, a + 3 + 5);
        res = plugin.invokeS<int (int&& a, int b, int& c)>("testGL.testInt2", 7, 11, a);
        EXPECT_EQ(a, 7 + 11);
        EXPECT_EQ(res, a + 7 + 11);
    }
    catch(std::exception& ex)
    {
        std::cout << ex.what() << "\n";
        EXPECT_EQ(true,false);
    }

    try
    {//testString1
        std::string a = "aaa";
        std::string res = plugin.invoke<std::string,std::string&>("testGL.testString1", a);
        EXPECT_EQ(a,"testString1");
        EXPECT_EQ(res,"res " + a);

        a = "aaa";
        res = plugin.invokeS<std::string (std::string&)>("testGL.testString1", a);
        EXPECT_EQ(a,"testString1");
        EXPECT_EQ(res,"res " + a);
    }
    catch(std::exception& ex)
    {
        std::cout << ex.what() << "\n";
        EXPECT_EQ(true,false);
    }

    try
    {//testString2
        using Sign = std::string (std::string&& srv, std::string slv, std::string& sr);
        std::string a = "aaa";
        std::string res = plugin.invokeS<Sign>("testGL.testString2", "a", "b", a);
        EXPECT_EQ(a,"ab");
        EXPECT_EQ(res,"res baab");
    }
    catch(std::exception& ex)
    {
        std::cout << ex.what() << "\n";
        EXPECT_EQ(true,false);
    }

    try
    {//testHandler
        graft::Router::vars_t vars;
        graft::Input input;
        graft::GlobalContextMap m;
        graft::Context ctx(m);
        graft::Output output;
        using Handler = graft::Status(const graft::Router::vars_t& vars, const graft::Input& input, graft::Context& ctx, graft::Output& output);
        graft::Status res = plugin.invokeS<Handler>("testGL.URI/{id:}", vars, input, ctx, output);
        EXPECT_EQ(res,graft::Status::Ok);
    }
    catch(std::exception& ex)
    {
        std::cout << ex.what() << "\n";
        EXPECT_EQ(true,false);
    }

}

