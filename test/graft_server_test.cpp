#include <gtest/gtest.h>
#include "context.h"

TEST(context, common)
{
	graft::Context ctx;
	ctx.put("aaa", std::string("aaaaaaaaaaaa"));
	ctx.put("bbb", 5);

	std::string s;
	int i;

	bool b = ctx.take("aaa", s) && ctx.take("bbb", i);
	EXPECT_EQ(true, b);
	ASSERT_STREQ(s.c_str(), "aaaaaaaaaaaa");
	EXPECT_EQ(i, 5);

	ctx.put("aaa", std::string("aaaaaaaaaaaa"));
	ctx.purge();
	b = ctx.take("aaa", s);
	EXPECT_EQ(false, b);
}
