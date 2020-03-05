#pragma once
#include <algorithm>
#include <mutex>
#include <optional>
#include <vector>

namespace RStein::AsyncCpp::Collections
{
  //Be aware of limitations of this class.
  template<typename T>
  class ThreadSafeMinimalisticVector
  {
  public:
    using size_type = typename std::vector<T>::size_type;
    using reference = typename std::vector<T>::reference;
    using const_reference = typename std::vector<T>::const_reference;
    using iterator = typename std::vector<T>::iterator;
    using const_iterator = typename std::vector<T>::const_iterator;
    using reverse_iterator = typename std::vector<T>::reverse_iterator;
    using const_reverse_iterator = typename std::vector<T>::const_reverse_iterator;
    explicit ThreadSafeMinimalisticVector(size_type count);
    explicit ThreadSafeMinimalisticVector(std::vector<T> innerVector);
    ThreadSafeMinimalisticVector(const ThreadSafeMinimalisticVector& other) = delete;
    ThreadSafeMinimalisticVector(ThreadSafeMinimalisticVector&& other) noexcept = delete;
    ThreadSafeMinimalisticVector& operator=(const ThreadSafeMinimalisticVector& other) = delete;
    ThreadSafeMinimalisticVector& operator=(ThreadSafeMinimalisticVector&& other) noexcept = delete;
    virtual ~ThreadSafeMinimalisticVector() = default;
    void Add(const T& item);
    void Add(T&& item);
    void Remove(const T& item);
    size_type Count;
    size_type Capacity;
    void Reserve(size_type newCapacity);
    std::vector<T> GetSnapshot();
    void Clear();
    std::optional<T> TryTake();
    
    reference operator [] (int index);
    const_reference operator [] (int index) const;

  private:
    std::vector<T> _innerVector;
    mutable std::mutex _mutex;

    iterator Begin() noexcept;
    const_iterator CBegin() const noexcept;
    iterator End() noexcept;
    const_iterator CEnd() const noexcept;
    iterator RBegin() noexcept;
    const_iterator CRBegin() const noexcept;
    iterator REnd() noexcept;
    const_iterator CREnd() const noexcept;

  };

  template <typename T>
  ThreadSafeMinimalisticVector<T>::ThreadSafeMinimalisticVector(size_type count) : _innerVector{count},
                                                                                    _mutex{}
  {

  }

  template <typename T>
  ThreadSafeMinimalisticVector<T>::ThreadSafeMinimalisticVector(std::vector<T> innerVector) : _innerVector(std::move(innerVector)),
                                                                                              _mutex{}
 
  {
  }


  template <typename T>
  void ThreadSafeMinimalisticVector<T>::Add(const T& item)
  {
     std::lock_guard lock{_mutex};
    _innerVector.push_back(item);
  }

  template <typename T>
  void ThreadSafeMinimalisticVector<T>::Add(T&& item)
  {
    std::lock_guard lock{_mutex};
    _innerVector.push_back(std::move(item));
  }

  template <typename T>
  void ThreadSafeMinimalisticVector<T>::Remove(const T& item)
  {
     std::lock_guard lock{_mutex};
    _innerVector.erase(std::remove(_innerVector.begin(), _innerVector.end(), item), _innerVector.end());
  }

  template <typename T>
  void ThreadSafeMinimalisticVector<T>::Reserve(size_type newCapacity)
  {
    std::lock_guard lock{_mutex};
    _innerVector.reserve(newCapacity);
  }

  template <typename T>
  std::vector<T> ThreadSafeMinimalisticVector<T>::GetSnapshot()
  {
    std::lock_guard lock{_mutex};
    auto copy = _innerVector;
    return copy;
  }

  template <typename T>
  void ThreadSafeMinimalisticVector<T>::Clear()
  {
    std::lock_guard lock{_mutex};
    _innerVector.clear();
  }

  template <typename T>
  typename ThreadSafeMinimalisticVector<T>::iterator ThreadSafeMinimalisticVector<T>::Begin() noexcept
  {
    std::lock_guard lock{_mutex};
    return _innerVector.begin();
  }

  template <typename T>
  typename ThreadSafeMinimalisticVector<T>::const_iterator ThreadSafeMinimalisticVector<T>::CBegin() const noexcept
  {
    std::lock_guard lock{_mutex};
    return  _innerVector.cbegin();
  }

  template <typename T>
  typename ThreadSafeMinimalisticVector<T>::iterator ThreadSafeMinimalisticVector<T>::End() noexcept
  {
    std::lock_guard lock{_mutex};
    return _innerVector.end();
  }

  template <typename T>
  typename ThreadSafeMinimalisticVector<T>::const_iterator ThreadSafeMinimalisticVector<T>::CEnd() const noexcept
  {
    std::lock_guard lock{_mutex};
    return _innerVector.cend();
  }

  template <typename T>
  typename ThreadSafeMinimalisticVector<T>::iterator ThreadSafeMinimalisticVector<T>::RBegin() noexcept
  {
    std::lock_guard lock{_mutex};
    return _innerVector.rbegin();
  }

  template <typename T>
  typename ThreadSafeMinimalisticVector<T>::const_iterator ThreadSafeMinimalisticVector<T>::CRBegin() const noexcept
  {
    std::lock_guard lock{_mutex};
    return _innerVector.crbegin();
  }

  template <typename T>
  typename ThreadSafeMinimalisticVector<T>::iterator ThreadSafeMinimalisticVector<T>::REnd() noexcept
  {
    std::lock_guard lock{_mutex};
    return _innerVector.rend();
  }

  template <typename T>
  typename ThreadSafeMinimalisticVector<T>::const_iterator ThreadSafeMinimalisticVector<T>::CREnd() const noexcept
  {
    std::lock_guard lock{_mutex};
    return _innerVector.crend();
  }

  template <typename T>
  typename ThreadSafeMinimalisticVector<T>::reference ThreadSafeMinimalisticVector<T>::operator[](int index)
  {
    std::lock_guard lock{_mutex};
    return _innerVector.at(index);
  }

  template <typename T>
  typename ThreadSafeMinimalisticVector<T>::const_reference ThreadSafeMinimalisticVector<T>::operator[](int index) const
  {
    std::lock_guard lock{_mutex};
    return _innerVector.at(index);
  }
}

