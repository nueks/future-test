#include <iostream>
#include <cstdlib>
#include <thread>
#include <future>
#include <exception>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <cassert>
#include <memory>

#pragma once

namespace ex
{


template <typename... T>
class future;

template <typename... T>
class promise;

struct ready_future_marker {};
struct exception_future_marker {};

enum class future_status
{
	ready,
	timeout,
	deferred
};

template <typename... T>
struct futurize;

template <typename... T>
using futurize_t = typename futurize<T...>::type;



class task
{
public:
	//virtual ~task() noexcept {}
	virtual void run() noexcept = 0;
};

template <typename Func, typename T>
struct continuation : public task
{
	continuation(Func&& func, T&& arg)
		: func_(std::move(func)),
		  arg_(std::move(arg))
	{}

	virtual void run() noexcept override
	{
		func_(std::move(arg_));
	}

	Func func_;
	T arg_;
};


template <typename... T>
struct promise_impl_type { using type = std::promise<T...>; };
template <>
struct promise_impl_type<> { using type = std::promise<void>; };

template <typename... T>
struct future_impl_type { using type = std::future<T...>; };
template <>
struct future_impl_type<> { using type = std::future<void>; };

template <typename... T>
struct future_result_type { using type = std::tuple_element_t<0, std::tuple<T...> >; };
template <>
struct future_result_type<> { using type = void; };


template <typename... T, typename R = typename future_result_type<T...>::type>
R get_value_impl(std::tuple<T...>&& x)
{
	return std::get<0>(std::move(x));
};
// template <typename... T, typename R = typename future_result_type<T...>::type>
// R get_value_impl(const std::tuple<T...>& x)
// {
// 	return std::get<0>(x);
// };
template <>
void get_value_impl(std::tuple<>&& x)
{
	return;
};
// template <>
// void get_value_impl(const std::tuple<>& x)
// {
// 	return;
// };


template <typename... T>
class promise
{
	template <typename... U>
	friend class future;

private:
	using impl_type = typename promise_impl_type<T...>::type;
	impl_type impl_;

	future<T...>* future_{nullptr};
	std::unique_ptr<task> continuation_{nullptr};

public:
	promise() noexcept {}

	promise(promise&& x) noexcept
		: impl_(std::move(x.impl_)),
		  future_(std::exchange(x.future_, nullptr)),
		  continuation_(std::exchange(x.continuation_, nullptr))
	{
		if (future_)
			future_->promise_ = this;
	}

	promise(const promise&) = delete;

	~promise() noexcept
	{
		if (future_)
		{
			future_->promise_ = nullptr;
		}
	}

	promise& operator=(promise&& x) noexcept
	{
		if (this != &x)
		{
			this->~promise();
			new (this) promise(std::move(x));
		}
		return *this;
	}

	future<T...> get_future() noexcept
	{
		return future<T...>(this);
	}

	template <typename... A>
	void set_value(A&&... a) noexcept
	{
		impl_.set_value(std::forward<A>(a)...);
		notify();
	}

	void set_exception(std::exception_ptr ex) noexcept
	{
		impl_.set_exception(ex);
		notify();
	}

	template <typename Exception>
	void set_exception(Exception&& ex) noexcept
	{
		impl_.set_exception(
			std::make_exception_ptr(std::forward<Exception>(ex))
		);
		notify();
	}

private:
	void notify()
	{
		if (future_)
		{
			future_->set_ready();
			future_->promise_ = nullptr;
			future_ = nullptr;
		}
		if (continuation_)
		{
			continuation_->run();
			continuation_ = nullptr;
		}
	}
};

template <>
class promise<void> : public promise<> {};



template <typename... T>
class future
{
	template <typename... U>
	friend class promise;

private:
	using impl_type = typename future_impl_type<T...>::type;
	using result_type = typename future_result_type<T...>::type;

	impl_type impl_;
	promise<T...>* promise_{nullptr};

	enum class state
	{
		invalid,
		future,
		future_ready,
		result,
		exception,
	} state_{state::future};

	std::tuple<std::decay_t<T>... > value_;
	std::exception_ptr ex_{nullptr};

public:
	future() noexcept {}

	future(future&& x) noexcept
		: impl_(std::move(x.impl_)),
		  promise_(std::exchange(x.promise_, nullptr)),
		  state_(std::exchange(x.state_, state::invalid)),
		  value_(std::move(x.value_)),
		  ex_(std::move(x.ex_))
	{
		if (promise_)
		{
			promise_->future_ = this;
		}
	}

	future(promise<T...>* pr)
		: impl_(pr->impl_.get_future()),
		  promise_(pr)
	{
		promise_->future_ = this;
	}

	template <typename... A>
	future(ready_future_marker, A&&... a) noexcept
	{
		set(std::forward<A>(a)...);
	}

	future(exception_future_marker, std::exception_ptr ex) noexcept
	{
		set_exception(std::move(ex));
	}

	template <typename Exception>
	future(exception_future_marker, Exception&& ex) noexcept
	{
		set_exception(
			std::make_exception_ptr(std::forward<Exception>(ex))
		);
	}

	future(const future&) = delete;

	~future()
	{
		if (promise_)
		{
			promise_->future_ = nullptr;
		}
	}

	future& operator=(const future&) = delete;
	future& operator=(future&& x) noexcept
	{
		if (this != &x)
		{
			this->~future();
			new (this) future(std::move(x));
		}
		return *this;
	}

	result_type get()
	{
		switch (state_)
		{
			case state::future:
			case state::future_ready:
				return impl_.get();
			case state::result:
			case state::exception:
				return get_value();
			default:
				abort();
		}
	}

	bool valid() const
	{
		switch (state_)
		{
			case state::future:
			case state::future_ready:
				return impl_.valid();
			case state::result:
			case state::exception:
				return true;
			default:
				return false;
		}
	}

	void wait() const
	{
		if (state_ == state::future)
			impl_.wait();
	}

	void set_ready() noexcept
	{
		assert(state_ == state::future);
		state_ = state::future_ready;
	}

	bool ready() const
	{
		switch (state_)
		{
			case state::invalid:
			case state::future:
				return false;
			case state::future_ready:
			case state::result:
			case state::exception:
				return true;
			default:
				abort();
		}
	}

	template <typename Rep, typename Period>
	future_status wait_for(const std::chrono::duration<Rep, Period>& timeout_duration) const
	{
		if (state_ == state::future || state_ == state::future_ready)
			return static_cast<future_status>(impl_.wait_for(timeout_duration));
		return future_status::ready;
	}

	template <typename Clock, typename Duration>
	future_status wait_until(const std::chrono::time_point<Clock, Duration>& timeout_time) const
	{
		if (state_ == state::future || state_ == state::future_ready)
			return static_cast<future_status>(impl_.wait_until(timeout_time));
		return future_status::ready;
	}

	template <typename Func,
			  typename Result = futurize_t<std::result_of_t<Func(future)> > >
	Result then(Func&& func) noexcept
	{
		using futurator = futurize<std::result_of_t<Func(future)> >;
		if (state_ == state::result || state_ == state::exception || state_ == state::future_ready)
		{
			try
			{
				return futurator::apply(std::forward<Func>(func), std::move(*this));
			}
			catch (...)
			{
				return Result(exception_future_marker(), std::current_exception());
			}
		}

		if (state_ == state::future)
		{
			typename futurator::promise_type pr;
			auto fut = pr.get_future();

			schedule(
				[pr = std::move(pr), func = std::forward<Func>(func)](future f) mutable {
					try
					{
						futurator::apply(func, std::move(f)).forward_to(pr);
					}
					catch (...)
					{
						pr.set_exception(std::current_exception());
					}
				}
			);
			return fut;
		}
	}

	template <typename U = result_type>
	std::enable_if_t<!std::is_same<U, void>::value, void>
	forward_to(promise<T...>& pr)
	{
		pr.set_value(get());
	}

	template<typename U = result_type>
	std::enable_if_t<std::is_same<U, void>::value, void>
	forward_to(promise<T...>& pr)
	{
		get();
		pr.set_value();
	}

private:
	template <typename... A>
	void set(A&&... a)
	{
		assert(state_ == state::future);
		value_ = std::tuple<T...>(std::forward<A>(a)...);
		state_ = state::result;
	}

	void set_exception(std::exception_ptr ex) noexcept
	{
		assert(state_ == state::future);
		ex_ = std::move(ex);
		state_ = state::exception;
	}

	result_type get_value()// &&
	{
		assert(state_ != state::future);
		if (state_ == state::exception)
		{
			state_ = state::invalid;
			std::rethrow_exception(std::move(ex_));
		}

		state_ = state::invalid;
		return get_value_impl<T...>(std::move(value_));
	}

	// result_type get_value() const&
	// {
	// 	assert(state_ != state::future);
	// 	if (state_ == state::exception)
	// 	{
	// 		std::rethrow_exception(ex_);
	// 	}
	// 	return get_value_impl<T...>(value_);
	// }

	template <typename Func>
	void schedule(Func&& func)
	{
		auto tmp = promise_;
		tmp->continuation_ = std::make_unique<continuation<Func, future<T...> > >(
			std::forward<Func>(func), std::move(*this)
		);
		tmp->future_ = nullptr;
	}


};

template <>
struct future<void> : public future<>
{
	//using future<>::future;
	future(future<>&& x) : future<>(std::move(x)) {}
};

// template <>
// struct future<const char*> : public future<std::string> {}



template <typename... T>
inline future<T...> make_ready_future(T&&... value) noexcept
{
	return future<T...>(ready_future_marker(), std::forward<T>(value)...);
}

template <std::size_t N>
inline future<std::string> make_ready_future(const char(&ar)[N]) noexcept
{
	return future<std::string>(ready_future_marker(), ar);
}

template <typename... T>
inline future<T...> make_exception_future(std::exception_ptr ex) noexcept
{
	return future<T...>(exception_future_marker(), ex);
}

template <typename... T, typename Exception>
inline future<T...> make_exception_future(Exception&& ex) noexcept
{
	return make_exception_future<T...>(std::make_exception_ptr(std::forward<Exception>(ex)));
}



template <typename... T>
struct futurize_helper
{
	using type = future<T...>;
	using promise_type = promise<T...>;
};

template <typename... T>
struct futurize_helper<future<T...> >
{
	using type = future<T...>;
	using promise_type = promise<T...>;
};

template <typename... T>
struct futurize
{
	using type = typename futurize_helper<T...>::type;
	using promise_type = typename futurize_helper<T...>::promise_type;

	template <typename Func, typename Arg>
	static inline std::enable_if_t<!std::is_same<std::result_of_t<Func(Arg)>, void>::value, type>
	apply(Func&& func, Arg&& arg) noexcept
	{
		try
		{
			return convert(std::forward<Func>(func)(std::forward<Arg>(arg)));
		}
		catch (...)
		{
			return make_exception_future<T...>(std::current_exception());
		}
	}

	template <typename Func, typename Arg>
	static inline std::enable_if_t<std::is_same<std::result_of_t<Func(Arg)>, void>::value, type>
	apply(Func&& func, Arg&& arg) noexcept
	{
		try
		{
			std::forward<Func>(func)(std::forward<Arg>(arg));
			return convert();
		}
		catch (...)
		{
			return make_exception_future<T...>(std::current_exception());
		}
	}

	static inline type convert(T&&... value)
	{
		return make_ready_future<T...>(std::move(value)...);
	}

	static inline type convert(type&& value)
	{
		return std::move(value);
	}
};

template <>
class futurize<void> : public futurize<> {};

} // namespace ex
