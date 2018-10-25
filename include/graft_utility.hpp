#pragma once

#include <string>
#include <thread>
#include <memory>
#include <vector>
#include <functional>
#include <chrono>
#include <mutex>
#include <shared_mutex>

namespace graft
{
    namespace ch = std::chrono;

    template <typename T>
    class TSList
    {
        struct node
        {
            using OnExpired = std::function<void(T&)>;

            mutable std::mutex m;
            std::shared_ptr<T> data;
            std::shared_ptr<node> next;
            ch::seconds ttl;
            ch::seconds expires;
            OnExpired onExpired = nullptr;

            node() : expires(ch::seconds::max()), ttl(ch::seconds(0)) {}

            node(T const& value)
                : expires(ch::seconds::max())
                , ttl(ch::seconds(0))
                , data(std::make_shared<T>(value)) {}

            node(T const& value, std::chrono::seconds ttl, const OnExpired& onExpired = nullptr)
                : ttl(ttl), data(std::make_shared<T>(value)), onExpired(onExpired) { update_time(); }

            void update_time()
            {
                expires = (ttl == ch::seconds(0)) ?
                    ch::seconds::max() : ch::time_point_cast<ch::seconds>(
                        ch::steady_clock::now()
                    ).time_since_epoch() + ttl;
            }

            bool expired(std::chrono::seconds now_sec)
            {
                return expires <= now_sec;
            }

            bool operator <=(const node& rhs) const
            {
                return expires <= rhs.expires;
            }
        };

        std::shared_ptr<node> head = std::make_shared<node>();

    public:
        using func = std::function<bool(T&)>;
        using OnExpired = typename node::OnExpired;

        TSList() {}
        ~TSList()
        {
            removeIf([](T const&) { return true; });
        }

        TSList(TSList const& other) = delete;
        TSList(TSList&& other) = delete;
        TSList& operator=(TSList const& other) = delete;

        void pushFront(T const& value, ch::seconds ttl = ch::seconds(0), OnExpired onExpired = nullptr)
        {
            std::shared_ptr<node> new_node = std::make_shared<node>(value, ttl, onExpired);

            std::lock_guard<std::mutex> lk(head->m);
            new_node->next = std::move(head->next);
            head->next = std::move(new_node);
        }

        void forEach(func f, bool timeUpdate = false)
        {
            node* current = head.get();
            std::unique_lock<std::mutex> lk(head->m);

            while (node* const next = current->next.get())
            {
                std::unique_lock<std::mutex> next_lk(next->m);
                lk.unlock();

                if (timeUpdate)
                    next->update_time();

                f(*next->data);
                current = next;
                lk = std::move(next_lk);
            }
        }

        std::shared_ptr<T> findFirstOf(func p)
        {
            node const *current = head.get();
            std::unique_lock<std::mutex> lk(head->m);
            while (node* const next = current->next.get())
            {
                std::unique_lock<std::mutex> next_lk(next->m);
                lk.unlock();
                if (p(*next->data))
                {
                    next->update_time();
                    return next->data;
                }
                current = next;
                lk = std::move(next_lk);
            }
            return std::shared_ptr<T>();
        }

        bool findAndApplyFirstOf(func p, func f)
        {
            node const *current = head.get();
            std::unique_lock<std::mutex> lk(head->m);
            while (node* const next = current->next.get())
            {
                std::unique_lock<std::mutex> next_lk(next->m);
                lk.unlock();
                if (p(*next->data))
                {
                    next->update_time();
                    return f(*next->data);
                }
                current = next;
                lk = std::move(next_lk);
            }
            return false;
        }

        void removeIf(func p)
        {
            node *current = head.get();
            std::unique_lock<std::mutex> lk(head->m);
            while (node* const next = current->next.get())
            {
                std::unique_lock<std::mutex> next_lk(next->m);
                if (p(*next->data))
                {
                    std::shared_ptr<node> old_next = std::move(current->next);
                    current->next = std::move(next->next);
                    next_lk.unlock();
                }
                else
                {
                    lk.unlock();
                    current = next;
                    lk = std::move(next_lk);
                }
            }
        }

        void  safe_cleanup(std::vector<std::function<void()>>& res)
        {
            ch::seconds now_sec = ch::time_point_cast<ch::seconds>(
                            ch::steady_clock::now()
                        ).time_since_epoch();
            node *current = head.get();
            std::unique_lock<std::mutex> lk(head->m);
            while (node* const next = current->next.get())
            {
                std::unique_lock<std::mutex> next_lk(next->m);
                if (next->expired(now_sec))
                {
                    if(next->onExpired)
                    {
                        auto makeCall = [](std::shared_ptr<T>&& ptr, OnExpired&& onExp )->std::function<void()>
                        {
                            return [ptr,onExp]()->void { onExp(*ptr); };
                        };
                        res.push_back(makeCall(std::move(next->data), std::move(next->onExpired)));
                    }

                    std::shared_ptr<node> old_next = std::move(current->next);
                    current->next = std::move(next->next);
                }
                else
                {
                    current = next;
                    lk = std::move(next_lk);
                }
            }
        }

        void  unsafe_cleanup(std::vector<std::function<void()>>& res)
        {
            ch::seconds now_sec = ch::time_point_cast<ch::seconds>(
                            ch::steady_clock::now()
                        ).time_since_epoch();
            node *current = head.get();
            while (node* const next = current->next.get())
            {
                if (next->expired(now_sec))
                {
                    if(next->onExpired)
                    {
                        auto makeCall = [](std::shared_ptr<T>&& ptr, OnExpired&& onExp )->std::function<void()>
                        {
                            return [ptr,onExp]()->void { onExp(*ptr); };
                        };
                        res.push_back(makeCall(std::move(next->data), std::move(next->onExpired)));
                    }

                    std::shared_ptr<node> old_next = std::move(current->next);
                    current->next = std::move(next->next);
                }
                else
                {
                    current = next;
                }
            }
        }
    };

    template <typename Key, typename Value, typename Hash=std::hash<Key> >
    class TSHashtable
    {
    private:
        class BucketType
        {
        private:
            using BucketValue = std::pair<Key, Value>;
            using BucketData = TSList<BucketValue>;
            using BucketPtr = std::shared_ptr<BucketValue>;
            using OnExpired = typename BucketData::OnExpired;

            BucketData m_data;

            BucketPtr findEntryFor(Key const& key)
            {
                return m_data.findFirstOf(
                    [&](BucketValue const& item)
                    {return item.first == key;}
                );
            }

        public:
            mutable std::shared_mutex blk;

            Value valueFor(Key const& key, Value const& default_value)
            {
                BucketPtr const found_entry = findEntryFor(key);
                return (found_entry == nullptr) ?
                    default_value : found_entry->second;
            }

            void addOrUpdate(Key const& key, Value const& value, ch::seconds ttl = ch::seconds(0), OnExpired onExpired = nullptr)
            {
                BucketPtr const found_entry = findEntryFor(key);
                if (found_entry == nullptr)
                    m_data.pushFront(BucketValue(key,value), ttl, onExpired);
                else
                    found_entry->second = value;
            }

            void remove(Key const& key)
            {
                m_data.removeIf(
                    [&](BucketValue const& item)
                    {return item.first == key;}
                );
            }

            bool hasKey(Key const& key)
            {
                BucketPtr const found_entry = findEntryFor(key);
                return (found_entry != nullptr);
            }

            bool applyFor(Key const& key, std::function<bool(Value&)> f)
            {
                return m_data.findAndApplyFirstOf(
                    [&](BucketValue const& item)
                        {return item.first == key;},
                    [&](BucketValue& item)
                        {return f(item.second);}
                );
            }

            void cleanup(std::vector<std::function<void()>>& res)
            {
//                m_data.unsafe_cleanup(res);
                m_data.safe_cleanup(res);
            }
        };

        std::vector<std::unique_ptr<BucketType>> m_buckets;
        Hash m_hasher;
        typename std::vector<std::unique_ptr<BucketType>>::iterator m_bit;

        BucketType& getBucket(Key const& key) const
        {
            const std::size_t bucket_index = m_hasher(key) % m_buckets.size();
            return *m_buckets[bucket_index];
        }

        BucketType& getNextBucket()
        {
            BucketType& b = *(*m_bit);
            if(++m_bit == m_buckets.end()) m_bit = m_buckets.begin();
            return b;
        }

        void cleanup(BucketType& b)
        {
            std::vector<std::function<void()>> res;
            {
                std::unique_lock<std::shared_mutex> lock(b.blk);
                b.cleanup(res);
            }
            for(auto& f : res)
            {
                f();
            }
        }
    public:
        using OnExpired = typename BucketType::OnExpired;

        TSHashtable(unsigned num_buckets = 64, const Hash& h = Hash())
            : m_buckets(num_buckets), m_hasher(h)
        {
            for (int i = 0; i < num_buckets; ++i)
                m_buckets[i] = std::make_unique<BucketType>();

            m_bit = m_buckets.begin();
        }

        TSHashtable(const TSHashtable& other) = delete;
        TSHashtable& operator=(const TSHashtable& other) = delete;

        Value valueFor(Key const& key, Value const& default_value = Value()) const
        {
            BucketType& b = getBucket(key);
            std::shared_lock<std::shared_mutex> lock(b.blk);
            return b.valueFor(key, default_value);
        }

        void addOrUpdate(const Key& key, const Value& value, ch::seconds ttl = ch::seconds(0), OnExpired onExpired = nullptr)
        {
            getBucket(key).addOrUpdate(key, value, ttl, onExpired);
        }

        void remove(const Key& key)
        {
            BucketType& b = getBucket(key);
            std::shared_lock<std::shared_mutex> lock(b.blk);
            b.remove(key);
        }

        bool hasKey(Key const& key) const
        {
            BucketType& b = getBucket(key);
            std::shared_lock<std::shared_mutex> lock(b.blk);
            return b.hasKey(key);
        }

        bool apply(Key const& key, std::function<bool(Value&)> f)
        {
            BucketType& b = getBucket(key);
            std::shared_lock<std::shared_mutex> lock(b.blk);
            return b.applyFor(key, f);
        }

        void cleanup(bool all = false)
        {
            BucketType* b = &getNextBucket();
            for(BucketType* n = b;;)
            {
                cleanup(*n);
                if(!all) break;
                n = &getNextBucket();
                if(n == b) break;
            }
        }
    };
}

