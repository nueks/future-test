#include <memory>
#include <algorithm>

#pragma once

namespace dot
{

template <typename T, typename Alloc>
inline
std::enable_if_t<std::is_nothrow_move_constructible<T>::value, void>
transfer_pass1(Alloc& a, T* from, T* to)
{
	a.construct(to, std::move(*from));
	a.destroy(from);
}

template <typename T, typename Alloc>
inline
std::enable_if_t<!std::is_nothrow_move_constructible<T>::value, void>
transfer_pass1(Alloc& a, T* from, T* to)
{
	a.construct(to, *from);
}

template <typename T, typename Alloc>
inline
std::enable_if_t<std::is_nothrow_move_constructible<T>::value, void>
transfer_pass2(Alloc& a, T* from, T* to)
{
}

template <typename T, typename Alloc>
inline
std::enable_if_t<!std::is_nothrow_move_constructible<T>::value, void>
transfer_pass2(Alloc& a, T* from, T* to)
{
	a.destroy(from);
}


template <typename T, typename Alloc = std::allocator<T> >
class circular_buffer
{
private:
	struct impl : Alloc
	{
		T* storage{nullptr};
		size_t begin{0};
		size_t end{0};
		size_t capacity{0};
	} impl_;

public:
	using value_type = T;
	using size_type = size_t;
	using reference = T&;
	using pointer = T*;
	using const_reference = const T&;
	using const_pointer = const T*;

public:
	circular_buffer() = default;
	circular_buffer(const circular_buffer&) = delete;
	circular_buffer(circular_buffer&& x)
		: impl_(std::exchange(x.impl_, {}))
	{}
	circular_buffer& operator=(const circular_buffer&) = delete;
	circular_buffer& operator=(circular_buffer&&) = delete;

	bool empty() const
	{
		return impl_.begin == impl_.end;
	}

	size_t size() const
	{
		return impl_.end - impl_.begin;
	}

	size_t capacity() const
	{
		return impl_.capacity;
	}

	void push_front(const T& data)
	{
		maybe_expand();
		auto p = &impl_.storage[mask(impl_.begin - 1)];
		impl_.construct(p, data);
		--impl_.begin;
	}

	void push_front(T&& data)
	{
		maybe_expand();
		auto p = &impl_.storage[mask(impl_.begin - 1)];
		impl_.construct(p, std::move(data));
		--impl_.begin;
	}

	template <typename... Args>
	void emplace_front(Args&&... args)
	{
		maybe_expand();
		auto p = &impl_.storage[mask(impl_.begin - 1)];
		impl_.construct(p, std::forward<Args>(args)...);
		--impl_.begin;
	}

	void push_back(const T& data)
	{
		maybe_expand();
		auto p = &impl_.storage[mask(impl_.end)];
		impl_.construct(p, data);
		++impl_.end;
	}

	void push_back(T&& data)
	{
		maybe_expand();
		auto p = &impl_.storage[mask(impl_.end)];
		impl_.construct(p, std::move(data));
		++impl_.end;
	}

	template <typename... Args>
	void emplace_back(Args&&... args)
	{
		maybe_expand();
		auto p = &impl_.storage[mask(impl_.end)];
		impl_.construct(p, std::forward<Args>(args)...);
		++impl_.end;
	}

	T& front()
	{
		return impl_.storage[mask(impl_.begin)];
	}

	T& back()
	{
		return impl_.storage[mask(impl_.end - 1)];
	}

	void pop_front()
	{
		impl_.destroy(&front());
		++impl_.begin;
	}

	void pop_back()
	{
		impl_.destroy(&back());
		--impl_.end;
	}

	T& operator[](size_t idx)
	{
		return impl_.storage[mask(impl_.begin + idx)];
	}

	template <typename Func>
	void for_each(Func func)
	{
		auto s = impl_.storage;
		auto m = impl_.capacity - 1;
		for (auto i = impl_.begin; i != impl_.end; ++i)
		{
			func(s[i & m]);
		}
	}

	template <typename Func>
	void for_each(Func func) const
	{
		auto s = impl_.storage;
		auto m = impl_.capacity - 1;
		for (auto i = impl_.begin; i != impl_.end; ++i)
		{
			func(s[i & m]);
		}
	}

private:
	void expand()
	{
		auto new_capacity = std::max<size_t>(impl_.capacity * 2, 1);
		auto new_storage = impl_.allocate(new_capacity);
		auto p = new_storage;
		try
		{
			for_each([this, &p](T& obj) {
					transfer_pass1(impl_, &obj, p);
					p++;
				}
			);
		}
		catch (...)
		{
			while (p != new_storage)
			{
				impl_.destroy(--p);
			}
			impl_.deallocate(new_storage, new_capacity);
			throw;
		}

		p = new_storage;
		for_each([this, &p](T& obj) {
				transfer_pass2(impl_, &obj, p++);
			}
		);

		std::swap(impl_.storage, new_storage);
		std::swap(impl_.capacity, new_capacity);
		impl_.begin = 0;
		impl_.end = p - impl_.storage;
		impl_.deallocate(new_storage, new_capacity);
	}

	void maybe_expand(size_t nr = 1)
	{
		if (impl_.end - impl_.begin + nr > impl_.capacity)
		{
			expand();
		}
	}

	size_t mask(size_t idx) const
	{
		return idx & (impl_.capacity - 1);
	}

private:
	template <typename CB, typename ValueType>
	struct cbiterator : std::iterator<std::random_access_iterator_tag, ValueType>
	{
		using super_t = std::iterator<std::random_access_iterator_tag, ValueType>;
		using typename std::iterator<std::random_access_iterator_tag, ValueType>::difference_type;

		ValueType& operator*() const { return cb->impl_.storage[cb->mask(idx)]; }
		ValueType* operator->() const { return &cb->impl_.storage[cb->mask(idx)]; }

		cbiterator& operator++()
		{
			idx++;
			return *this;
		}

		cbiterator operator++(int)
		{
			auto v = *this;
			idx++;
			return v;
		}

		cbiterator& operator--()
		{
			idx--;
			return *this;
		}

		cbiterator operator--(int)
		{
			auto v = *this;
			idx--;
			return *this;
		}

		cbiterator operator+(difference_type n) const
		{
			return cbiterator(cb, idx + n);
		}

		cbiterator operator-(difference_type n) const
		{
			return cbiterator(cb, idx - n);
		}

		cbiterator operator+=(difference_type n)
		{
			idx += n;
			return *this;
		}

		cbiterator operator-=(difference_type n)
		{
			idx -= n;
			return *this;
		}

		bool operator==(const cbiterator& rhs) const
		{
			return idx == rhs.idx;
		}

		bool operator!=(const cbiterator& rhs) const
		{
			return idx != rhs.idx;
		}

		bool operator<(const cbiterator& rhs) const
		{
			return idx < rhs.idx;
		}

		bool operator>(const cbiterator& rhs) const
		{
			return idx > rhs.idx;
		}

		bool operator>=(const cbiterator& rhs) const
		{
			return idx >= rhs.idx;
		}

		bool operator<=(const cbiterator& rhs) const
		{
			return idx <= rhs.idx;
		}

		difference_type operator-(const cbiterator& rhs) const
		{
			return idx - rhs.idx;
		}

	private:
		CB* cb;
		size_t idx;
		cbiterator(CB* b, size_t i) : cb(b), idx(i) {}
		friend class circular_buffer;
	};
	friend class iterator;

public:
	using iterator = cbiterator<circular_buffer, T>;
	using const_iterator = cbiterator<const circular_buffer, const T>;

	iterator begin()
	{
		return iterator(this, impl_.begin);
	}

	const_iterator begin() const
	{
		return const_iterator(this, impl_.begin);
	}

	iterator end()
	{
		return iterator(this, impl_.end);
	}

	const_iterator end() const
	{
		return const_iterator(this, impl_.end);
	}

	const_iterator cbegin() const
	{
		return const_iterator(this, impl_.begin);
	}

	const_iterator cend() const
	{
		return const_iterator(this, impl_.end);
	}
};

} // namespace dot
