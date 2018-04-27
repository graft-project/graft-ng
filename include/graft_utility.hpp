#pragma once

#include <string>
#include <thread>
#include <memory>
#include <vector>
#include <functional>

namespace graft
{
    template <typename T>
    class TSList
    {
        struct node
        {
            mutable std::mutex m;
            std::shared_ptr<T> data;
            std::unique_ptr<node>  next;

            node() : next() {}
            node(T const& value) : data(std::make_shared<T>(value)) {}
        };

        node head;

    public:
        using func = std::function<bool(T&)>;

        TSList() {}
        ~TSList()
        {
            removeIf([](T const&) { return true; });
        }

        TSList(TSList const& other) = delete;
        TSList(TSList&& other) = delete;
        TSList& operator=(TSList const& other) = delete;

        void pushFront(T const& value)
        {
            std::unique_ptr<node> new_node(new node(value));

            std::lock_guard<std::mutex> lk(head.m);
            new_node->next = std::move(head.next);
            head.next = std::move(new_node);
        }

        void forEach(func f)
        {
            node* current = &head;
            std::unique_lock<std::mutex> lk(head.m);

            while (node* const next = current->next.get())
            {
                std::unique_lock<std::mutex> next_lk(next->m);
                lk.unlock();
                f(*next->data);
                current = next;
                lk = std::move(next_lk);
            }
        }

        std::shared_ptr<T> findFirstOf(func p) const
        {
            node const *current = &head;
            std::unique_lock<std::mutex> lk(head.m);
            while (node* const next = current->next.get())
            {
                std::unique_lock<std::mutex> next_lk(next->m);
                lk.unlock();
                if (p(*next->data))
                {
                    return next->data;
                }
                current = next;
                lk = std::move(next_lk);
            }
            return std::shared_ptr<T>();
        }

        bool findAndApplyFirstOf(func p, func f)
        {
            node const *current = &head;
            std::unique_lock<std::mutex> lk(head.m);
            while (node* const next = current->next.get())
            {
                std::unique_lock<std::mutex> next_lk(next->m);
                lk.unlock();
                if (p(*next->data))
                {
                    return f(*next->data);
                }
                current = next;
                lk = std::move(next_lk);
            }
            return false;
        }
        void removeIf(func p)
        {
            node *current = &head;
            std::unique_lock<std::mutex> lk(head.m);
            while (node* const next = current->next.get())
            {
                std::unique_lock<std::mutex> next_lk(next->m);
                if (p(*next->data))
                {
                    std::unique_ptr<node> old_next = std::move(current->next);
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

            BucketData m_data;

            BucketPtr findEntryFor(Key const& key) const
            {
                return m_data.findFirstOf(
                    [&](BucketValue const& item)
                    {return item.first == key;}
                );
            }

        public:
            Value valueFor(Key const& key, Value const& default_value) const
            {
                BucketPtr const found_entry = findEntryFor(key);
                return (found_entry == nullptr) ?
                    default_value : found_entry->second;
            }

            void addOrUpdate(Key const& key, Value const& value)
            {
                BucketPtr const found_entry = findEntryFor(key);
                if (found_entry == nullptr)
                    m_data.pushFront(BucketValue(key,value));
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

            bool hasKey(Key const& key) const
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
        };

        std::vector<std::unique_ptr<BucketType>> m_buckets;
        Hash m_hasher;

        BucketType& getBucket(Key const& key) const
        {
            const std::size_t bucket_index = m_hasher(key) % m_buckets.size();
            return *m_buckets[bucket_index];
        }

    public:
        TSHashtable(unsigned num_buckets = 64, const Hash& h = Hash())
            : m_buckets(num_buckets), m_hasher(h)
        {
            for (int i = 0; i < num_buckets; ++i)
                m_buckets[i].reset(new BucketType);
        }

        TSHashtable(const TSHashtable& other) = delete;
        TSHashtable& operator=(const TSHashtable& other) = delete;

        Value valueFor(Key const& key, Value const& default_value = Value()) const
        {
            return getBucket(key).valueFor(key, default_value);
        }

        void addOrUpdate(const Key& key, const Value& value)
        {
            getBucket(key).addOrUpdate(key, value);
        }

        void remove(const Key& key)
        {
            getBucket(key).remove(key);
        }

        bool hasKey(Key const& key) const
        {
            return getBucket(key).hasKey(key);
        }

        bool apply(Key const& key, std::function<bool(Value&)> f)
        {
            return getBucket(key).applyFor(key, f);
        }
    };
}
