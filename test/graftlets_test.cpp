#include <gtest/gtest.h>
#include "GraftletLoader.h"
#include "IGraftlet.h"

/*
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
*/

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
            using Sign = std::string (std::string&);
            std::string a = "aaa";
            std::string res = plugin.invokeX<std::string,std::string&>("testGL.getString11", a);

            EXPECT_EQ(a,"getString11");
            EXPECT_EQ(res,"res getString11");
        }
        catch(std::exception& ex)
        {
            std::cout << ex.what() << "\n";
            EXPECT_EQ(true,false);
        }

        try
        {
            using Sign = int (int&);
            int a = 5;
            int res = plugin.invokeS<Sign>("testGL.getInt11", a);

            EXPECT_EQ(a,11);
            EXPECT_EQ(res,21);
        }
        catch(std::exception& ex)
        {
            std::cout << ex.what() << "\n";
            EXPECT_EQ(true,false);
        }

        try
        {
            using Sign = std::string (std::string&);
            std::string a = "aaa";
            std::string res = plugin.invokeS<Sign>("testGL.getString11", a);

            EXPECT_EQ(a,"getString11");
            EXPECT_EQ(res,"res getString11");
        }
        catch(std::exception& ex)
        {
            std::cout << ex.what() << "\n";
            EXPECT_EQ(true,false);
        }

        try
        {
            using Sign = std::string (std::string&& srv, std::string slv, std::string& sr);
            std::string a = "aaa";
            std::string res = plugin.invokeS<Sign>("testGL.getString4", "a", "b", a);
            EXPECT_EQ(a,"ab");
            EXPECT_EQ(res,"res baab");
        }
        catch(std::exception& ex)
        {
            std::cout << ex.what() << "\n";
            EXPECT_EQ(true,false);
        }

        try
        {
            plugin.invokeX<void, std::string&>("testGL.getString1", r);
            bool ok = true;
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
            plugin.invokeX<void,int,int>("testGL.getString2", 9, 8);
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
            plugin.invokeX<void,std::string,std::string&,std::string*,char**>("testGL.getString3", slv, sr, &pstr, &buf);
            bool ok = true;
            if(ok)
            {
                std::cout << "sr=" << sr << "\n";
                std::cout << "pstr=" << pstr << "\n";
                pstr[2] = 'c';
                std::cout << "pstr=" << pstr << "\n";
                strcpy(buf, "aaaaa");
                plugin.invokeX<void,std::string,std::string&,std::string*,char**>("testGL.getString3", slv, sr, &pstr, &buf);
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
    }

    std::cout << "the main() end" << "\n";
}

