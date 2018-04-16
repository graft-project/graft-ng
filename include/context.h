#pragma once
#include <any>
#include <map>

namespace graft
{

class Context {
public:
	Context() = default;
	Context(const Context&) = delete;
	Context(Context&&) = delete;
	~Context() = default;

	template <typename T>
//		T& take(const std::string& key) noexcept
	bool take(const std::string& key, T& val) noexcept
	{
		//std::map<std::string, std::any>::iterator
		auto it = m_map.find(key);

		if (it == m_map.end())
			return false;

		try {
			val = std::any_cast<T&&>(it->second); // move
		} catch(const std::bad_any_cast& e) {
			return false;
		}
		m_map.erase(it);

		return true;
	}

	template <typename T>
	bool put(const std::string& key, const T& val)
	{
		static_assert(std::is_nothrow_move_constructible<T>::value, 
				"not move constructible");
		auto p = m_map.emplace(key, std::make_any<T>(val));
		return p.second;
	}

	void purge()
	{
		m_map.clear();
	}

private:
	std::map<std::string, std::any> m_map;
};

}//namespace graft
