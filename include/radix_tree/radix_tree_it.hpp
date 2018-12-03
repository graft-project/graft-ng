/*
Copyright (c) 2010, Yuuki Takano <ytakanoster@gmail.com>, All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef RADIX_TREE_IT
#define RADIX_TREE_IT

#include <iterator>
#include <functional>

// forward declaration
template <typename K, typename T, class Compare = std::less<K> > class radix_tree;
template <typename K, typename T, class Compare = std::less<K> > class radix_tree_node;

template <typename K, typename T, class Compare = std::less<K> >
class radix_tree_it : public std::iterator<std::forward_iterator_tag, std::pair<K, T> > {
    friend class radix_tree<K, T, Compare>;

public:
    radix_tree_it() : m_pointee(0) { }
    radix_tree_it(const radix_tree_it& r) : m_pointee(r.m_pointee) { }
    radix_tree_it& operator=(const radix_tree_it& r) { m_pointee = r.m_pointee; return *this; }
    ~radix_tree_it() { }

    std::pair<const K, T>& operator*  () const;
    std::pair<const K, T>* operator-> () const;
    const radix_tree_it<K, T, Compare>& operator++ ();
    radix_tree_it<K, T, Compare> operator++ (int);
    // const radix_tree_it<K, T, Compare>& operator-- ();
    bool operator!= (const radix_tree_it<K, T, Compare> &lhs) const;
    bool operator== (const radix_tree_it<K, T, Compare> &lhs) const;

private:
    radix_tree_node<K, T, Compare> *m_pointee;
    radix_tree_it(radix_tree_node<K, T, Compare> *p) : m_pointee(p) { }

    radix_tree_node<K, T, Compare>* increment(radix_tree_node<K, T, Compare>* node) const;
    radix_tree_node<K, T, Compare>* descend(radix_tree_node<K, T, Compare>* node) const;
};

template <typename K, typename T, typename Compare>
radix_tree_node<K, T, Compare>* radix_tree_it<K, T, Compare>::increment(radix_tree_node<K, T, Compare>* node) const
{
	radix_tree_node<K, T, Compare>* parent = node->m_parent;

    if (parent == NULL)
        return NULL;

    typename radix_tree_node<K, T, Compare>::it_child it = parent->m_children.find(node->m_key);
    assert(it != parent->m_children.end());
    ++it;

    if (it == parent->m_children.end())
        return increment(parent);
    else
        return descend(it->second);
}

template <typename K, typename T, typename Compare>
radix_tree_node<K, T, Compare>* radix_tree_it<K, T, Compare>::descend(radix_tree_node<K, T, Compare>* node) const
{
    if (node->m_is_leaf)
        return node;

    typename radix_tree_node<K, T, Compare>::it_child it = node->m_children.begin();

    assert(it != node->m_children.end());

    return descend(it->second);
}

template <typename K, typename T, typename Compare>
std::pair<const K, T>& radix_tree_it<K, T, Compare>::operator* () const
{
    return *m_pointee->m_value;
}

template <typename K, typename T, typename Compare>
std::pair<const K, T>* radix_tree_it<K, T, Compare>::operator-> () const
{
    return m_pointee->m_value;
}

template <typename K, typename T, typename Compare>
bool radix_tree_it<K, T, Compare>::operator!= (const radix_tree_it<K, T, Compare> &lhs) const
{
    return m_pointee != lhs.m_pointee;
}

template <typename K, typename T, typename Compare>
bool radix_tree_it<K, T, Compare>::operator== (const radix_tree_it<K, T, Compare> &lhs) const
{
    return m_pointee == lhs.m_pointee;
}

template <typename K, typename T, typename Compare>
const radix_tree_it<K, T, Compare>& radix_tree_it<K, T, Compare>::operator++ ()
{
    if (m_pointee != NULL) // it is undefined behaviour to dereference iterator that is out of bounds...
        m_pointee = increment(m_pointee);
    return *this;
}

template <typename K, typename T, typename Compare>
radix_tree_it<K, T, Compare> radix_tree_it<K, T, Compare>::operator++ (int)
{
    radix_tree_it<K, T, Compare> copy(*this);
    ++(*this);
    return copy;
}

/*
template <typename K, typename T>
const radix_tree_it<K, T, Compare>& radix_tree_it<K, T, Compare>::operator-- ()
{
    assert(m_pointee != NULL);

    return *this;
}
*/

#endif // RADIX_TREE_IT
