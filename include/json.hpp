#pragma once

#include <utility>
#include <string>
#include <vector>
#include <tuple>
#include <functional>
#include <sstream>
#include <boost/hana.hpp>

namespace hana = boost::hana;

namespace graft { namespace json
{
	template <typename Xs>
	std::string join(Xs&& xs, std::string sep)
	{
		return hana::fold(hana::intersperse(
			std::forward<Xs>(xs), sep), "", std::plus<>{});
	}

	std::string quote(std::string s) { return "\"" + s + "\""; }

	template <typename T>
	auto to_json(T const& x) -> decltype(std::to_string(x))
	{
		return std::to_string(x);
	}

	std::string to_json(char c) { return quote({c}); }
	std::string to_json(std::string s) { return quote(s); }

	template <typename T>
	std::enable_if_t<hana::Struct<T>::value, std::string> to_json(T const& x)
	{
		auto json = hana::transform(hana::keys(x), [&](auto name)
		{
			auto const& member = hana::at_key(x, name);
			return quote(hana::to<char const*>(name)) + " : " + to_json(member);
		});

		return "{" + join(std::move(json), ", ") + "}";
	}

	template <typename T>
	std::enable_if_t<hana::Sequence<T>::value, std::string> to_json(T const& xs)
	{
		auto json = hana::transform(xs, [](auto const& x)
		{
			return to_json(x);
		});

		return "[" + join(std::move(json), ", ") + "]";
	}

	template <typename T>
	std::enable_if_t<std::is_same<T, int>::value, T> from_json(std::istream& in)
	{
		T result;
		in >> result;
		return result;
	}

	template <typename T>
	std::enable_if_t<std::is_same<T, std::string>::value, T> from_json(std::istream& in)
	{
		char quote;
		in >> quote;

		T result;
		char c;
		while (in.get(c) && c != '"')
			result += c;

		return result;
	}

	template <typename T>
	std::enable_if_t<hana::Struct<T>::value, T> from_json(std::istream& in)
	{
		T result;
		char brace;

		in >> brace;

		hana::for_each(hana::keys(result), [&](auto key)
		{
			in.ignore(std::numeric_limits<std::streamsize>::max(), ':');
			auto& member = hana::at_key(result, key);
			using Member = std::remove_reference_t<decltype(member)>;
			member = from_json<Member>(in);
		});
		in >> brace;
		return result;
	}

	template <typename T>
	std::enable_if_t<hana::Sequence<T>::value, T> from_json(std::istream& in)
	{
		T result;
		char bracket;
		in >> bracket;

		hana::length(result).times.with_index([&](auto i)
		{
			if (i != 0u)
			{
				char comma;
				in >> comma;
			}

			auto& element = hana::at(result, i);
			using Element = std::remove_reference_t<decltype(element)>;
			element = from_json<Element>(in);
		});
		in >> bracket;
		return result;
	}
}}

