#pragma once

#include <utility>
#include <string>
#include <vector>
#include <tuple>
#include <boost/hana.hpp>

#include "json.hpp"

namespace hana = boost::hana;

namespace graft 
{
	class OutJson
	{
	public:
		template <typename T>
		void load(const T& out)
		{
			m_buf.assign(json::to_json(out));
		}

		std::pair<const char *, size_t> get() const
		{
			return std::make_pair(m_buf.c_str(), m_buf.length());
		}
	private:
		std::string m_buf;
	};

	class InJson
	{
	public:
		template <typename T>
		T get() const
		{
			std::istringstream iss;
			iss.str(m_buf);
			return json::from_json<T>(iss);
		}

		void load(const char *buf, size_t size) { m_buf.assign(buf, buf + size); }

		void assign(const OutJson& o)
		{
			const char *buf; size_t size;
			std::tie(buf, size) = o.get();
			load(buf, size);
		}
	private:
		std::string m_buf;
	};

	using Input = InJson;
	using Output = OutJson;

} //namespace graft

#if 0
#include <iostream>

int main()
{
	struct Test {
		BOOST_HANA_DEFINE_STRUCT(Test,
			(std::string, name),
			(int, id)
		);
	};

	graft::InJson in;
	graft::OutJson out;
	std::string ts = "{\"name\": \"aaa\", \"id\": 123}";

	in.load(ts.c_str(), ts.length());
	auto t = in.get<Test>();

	std::cout << "t.name = " << t.name << "; t.id = " << t.id << std::endl;

	out.load(t);

	std::cout << out.get().first << std::endl;
}
#endif

