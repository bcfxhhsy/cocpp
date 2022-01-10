_Pragma("once");

#include "cocpp/core/co_define.h"
#include "cocpp/sync/co_binary_semaphore.h"
#include "cocpp/sync/co_condition_variable.h"
#include "cocpp/sync/co_mutex.h"
#include "cocpp/utils/co_noncopyable.h"
#include <deque>
#include <optional>

CO_NAMESPACE_BEGIN

template <typename ValueType, int MaxSize>
class co_chan : private co_noncopyable
{
private:
    std::deque<ValueType> data__;
    bool                  closed__ { false };
    mutable co_mutex      mu__;
    co_condition_variable full_cond__;
    co_condition_variable empty_cond__;

    co_binary_semaphore read_sem__ { 0 };
    co_binary_semaphore write_sem__ { 0 };

public:
    class iterator
    {
    private:
        co_chan*  ch__;
        ValueType value__;
        bool      closed__ { true };

    public:
        iterator(co_chan* ch);
        iterator() = default;
        iterator   operator++();
        ValueType& operator*();
        ValueType* operator->();
        bool       operator!=(const iterator& other);
    };

    iterator begin();
    iterator end();

    bool                     push(ValueType value);
    std::optional<ValueType> pop();
    void                     close();
    bool                     closed() const;
};

template <typename ValueType, int MaxSize>
bool operator<(co_chan<ValueType, MaxSize>& ch, ValueType value);

template <typename ValueType, int MaxSize>
bool operator>(co_chan<ValueType, MaxSize>& ch, ValueType& value);

template <typename ValueType, int MaxSize>
co_chan<ValueType, MaxSize>& operator<<(co_chan<ValueType, MaxSize>& ch, ValueType value);

template <typename ValueType, int MaxSize>
co_chan<ValueType, MaxSize>& operator>>(co_chan<ValueType, MaxSize>& ch, ValueType& value);

// 模板实现

template <typename ValueType, int MaxSize>
bool operator<(co_chan<ValueType, MaxSize>& ch, ValueType value)
{
    return ch.push(value);
}

template <typename ValueType, int MaxSize>
bool operator>(co_chan<ValueType, MaxSize>& ch, ValueType& value)
{
    auto ret = ch.pop();
    if (!ret)
    {
        return false;
    }
    value = ret.value();
    return true;
}

template <typename ValueType, int MaxSize>
co_chan<ValueType, MaxSize>& operator<<(co_chan<ValueType, MaxSize>& ch, ValueType value)
{
    ch.push(value);
    return ch;
}

template <typename ValueType, int MaxSize>
co_chan<ValueType, MaxSize>& operator>>(co_chan<ValueType, MaxSize>& ch, ValueType& value)
{
    value = ch.pop().value();
    return ch;
}

template <typename ValueType, int MaxSize>
bool co_chan<ValueType, MaxSize>::push(ValueType value)
{
    std::unique_lock lock(mu__);
    if (closed__)
    {
        return false;
    }
    if constexpr (MaxSize > 0) // 当MaxSize < 0，是无限长度的chan
    {
        if (data__.size() == MaxSize)
        {
            full_cond__.wait(lock, [this] { return closed__ || data__.size() < MaxSize; });
            if (closed__)
            {
                return false;
            }
        }
    }
    else if constexpr (MaxSize == 0) // 无缓冲chan
    {
        lock.unlock();
        read_sem__.acquire();
        lock.lock();
    }
    data__.push_back(value);
    if constexpr (MaxSize == 0)
    {
        lock.unlock();
        write_sem__.release();
        lock.lock();
    }
    else
    {
        empty_cond__.notify_one();
    }
    return true;
}

template <typename ValueType, int MaxSize>
std::optional<ValueType> co_chan<ValueType, MaxSize>::pop()
{
    std::optional<ValueType> ret;
    std::unique_lock         lock(mu__);

    if constexpr (MaxSize != 0)
    {
        if (data__.empty())
        {
            if (closed__)
            {
                return ret;
            }
            empty_cond__.wait(lock, [this] { return closed__ || !data__.empty(); });
            if (data__.empty())
            {
                return ret;
            }
        }
    }
    else
    {
        if (closed__)
        {
            return ret;
        }
        lock.unlock();
        read_sem__.release();
        write_sem__.acquire();
        lock.lock();
        if (data__.empty())
        {
            return ret;
        }
    }
    ret = data__.front();
    data__.pop_front();
    if constexpr (MaxSize != 0)
    {
        full_cond__.notify_one();
    }
    return ret;
}

template <typename ValueType, int MaxSize>
void co_chan<ValueType, MaxSize>::close()
{
    std::scoped_lock lock(mu__);
    closed__ = true;
    if constexpr (MaxSize == 0)
    {
        write_sem__.release();
    }
    else
    {
        full_cond__.notify_all();
        empty_cond__.notify_all();
    }
}

template <typename ValueType, int MaxSize>
bool co_chan<ValueType, MaxSize>::closed() const
{
    std::scoped_lock lock(mu__);
    return closed__;
}

// 迭代器相关

template <typename ValueType, int MaxSize>
co_chan<ValueType, MaxSize>::iterator::iterator(co_chan<ValueType, MaxSize>* ch)
    : ch__(ch)
    , closed__(ch->closed__)
{
    ++*this; // 填充第一个元素
}

template <typename ValueType, int MaxSize>
typename co_chan<ValueType, MaxSize>::iterator co_chan<ValueType, MaxSize>::iterator::operator++()
{
    auto curr = ch__->pop();
    if (curr)
    {
        value__ = curr.value();
    }
    else
    {
        closed__ = true;
    }
    return *this;
}

template <typename ValueType, int MaxSize>
ValueType& co_chan<ValueType, MaxSize>::iterator::operator*()
{
    return value__;
}

template <typename ValueType, int MaxSize>
ValueType* co_chan<ValueType, MaxSize>::iterator::operator->()
{
    return &value__;
}

template <typename ValueType, int MaxSize>
bool co_chan<ValueType, MaxSize>::iterator::operator!=(const iterator& other)
{
    if (closed__ && other.closed__)
    {
        return false;
    }
    return true;
}

template <typename ValueType, int MaxSize>
typename co_chan<ValueType, MaxSize>::iterator co_chan<ValueType, MaxSize>::begin()
{
    return co_chan<ValueType, MaxSize>::iterator(this);
}

template <typename ValueType, int MaxSize>
typename co_chan<ValueType, MaxSize>::iterator co_chan<ValueType, MaxSize>::end()
{
    return co_chan<ValueType, MaxSize>::iterator();
}

CO_NAMESPACE_END