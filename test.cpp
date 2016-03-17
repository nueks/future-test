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

namespace ex
{

struct ready_future_marker {};

template <typename T>
class future;

template <>
class future<void>;

template <typename T>
class promise;

template <>
class promise<void>;

template <typename T>
struct futurize;

template <typename T>
using futurize_t = typename futurize<T>::type;


enum class future_status
{
	ready,
	timeout,
	deferred
};


class task
{
public:
	virtual ~task() noexcept {}
	virtual void run() noexcept = 0;
};

template <typename Func, typename T>
struct continuation : public task
{
	continuation(Func&& func, T* arg)
		: func_(std::move(func)),
		  arg_(arg)
	{}

	virtual void run() noexcept override
	{
		func_(arg_);
	}

	Func func_;
	T* arg_;
};



template <typename T>
class future
{
	template <typename U>
	friend class promise;

private:
	std::future<T> impl_;
	promise<T>* promise_ = nullptr;

	enum class state {
		invalid,
		future,
		result,
		exception
	} state_ = state::future;

	union any {
		any() {}
		~any() {}
		T value;
		std::exception_ptr ex;
	} u_;

	std::unique_ptr<task> continuation_;

public:
	future() noexcept : impl_(std::future<T>()) {}

	future(future&& x) noexcept
		: impl_(std::move(x.impl_)),
		  promise_(std::exchange(x.promise_, nullptr)),
		  state_(std::exchange(x.state_, state::invalid))
	{
		switch (state_)
		{
			case state::invalid:
				break;
			case state::future:
				break;
			case state::result:
				new (&u_.value) T(std::move(x.u_.value));
				break;
			case state::exception:
				new (&u_.ex) std::exception_ptr(std::move(x.u_.ex));
				x.u_.ex.~exception_ptr();
				break;
			default:
				abort();
		}

		if (promise_)
		{
			promise_->future_ = this;
		}
	}

	future(promise<T>* p)
	{
		impl_ = p->impl_.get_future();
		promise_ = p;
		p->future_ = this;
	}

	future(ready_future_marker, const T& x) noexcept
	{
		set(x);
	}

	future(ready_future_marker, T&& x) noexcept
	{
		set(std::move(x));
	}

	future(std::exception_ptr ex) noexcept
	{
		set_exception(std::move(ex));
	}

	template <typename Exception,
			  typename  = std::enable_if_t<std::is_base_of<std::exception, Exception>::value> >
	future(Exception&& ex) noexcept
	{
		set_exception(
			std::make_exception_ptr(std::forward<Exception>(ex))
		);
	}

	future(const future& x) = delete;

	~future()
	{
		switch (state_)
		{
			case state::invalid:
				break;
			case state::future:
				break;
			case state::result:
				u_.value.~T();
				break;
			case state::exception:
				u_.ex.~exception_ptr();
				break;
			default:
				abort();
		}
	}


	T get()
	{
		std::cout << "get" << std::endl;
		switch (state_)
		{
			case state::invalid:
				abort();
			case state::future:
				return impl_.get();
			case state::result:
				return u_.value;
			case state::exception:
				std::rethrow_exception(u_.ex);
		}
	}

	bool valid() const
	{
		switch (state_)
		{
			case state::invalid:
				return false;
			case state::future:
				return impl_.valid();
			case state::result:
				return true;
			case state::exception:
				return true;
		}
	}

	void wait() const
	{
		if (state_ == state::future)
			impl_.wait();
	}

	template <typename Rep, typename Period>
	future_status wait_for(const std::chrono::duration<Rep, Period>& timeout_duration) const
	{
		if (state_ == state::future)
			return static_cast<future_status>(impl_.wait_for(timeout_duration));
		return future_status::ready;
	}

	template <typename Clock, typename Duration>
	future_status wait_until(const std::chrono::time_point<Clock, Duration>& timeout_time) const
	{
		if (state_ == state::future)
			return static_cast<future_status>(impl_.wait_until(timeout_time));
		return future_status::ready;
	}

	template <typename Func, typename Result = futurize_t<std::result_of_t<Func(T&&)>> >
	Result then(Func&& func) noexcept
	{
		using futurator = futurize<std::result_of_t<Func(T&&)> >;
		if (state_ == state::result)
		{
			try
			{
				return futurator::apply(std::forward<Func>(func), std::move(u_.value));
			}
			catch (...)
			{
				std::cout << "exception....?" << std::endl;
				return Result(std::current_exception());
			}
		}

		if (state_ == state::exception)
		{
			return Result(u_.ex);
		}

		if (state_ == state::future)
		{
			typename futurator::promise_type pr;

			auto fut = pr.get_future();
			schedule([pr = std::move(pr), func = std::forward<Func>(func)](future* fut) mutable {
				try
				{
					// auto f = futurator::apply(func, fut->get());
					// pr.set_value(f.get());

					futurator::apply(func, fut->get()).forward_to(pr);
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

	template <typename Func>
	void schedule(Func&& func)
	{
		continuation_ = std::make_unique<continuation<Func, future<T> > >(std::forward<Func>(func), this);
	}

	void forward_to(promise<T>& pr)
	{
		pr.set_value(get());
	}


private:
	void set(T&& value) noexcept
	{
		assert(state_ == state::future);
		new (&u_.value) T(std::move(value));
		state_ = state::result;
	}

	void set_exception(std::exception_ptr ex) noexcept
	{
		assert(state_ == state::future);
		new (&u_.ex) std::exception_ptr(std::move(ex));
		state_ = state::exception;
	}


};

template <>
class future<void>
{
	template <typename U>
	friend class promise;

private:
	std::future<void> impl_;
	promise<void>* promise_ = nullptr;

	enum class state {
		invalid,
		future,
		result,
		exception
	} state_ = state::future;

	std::exception_ptr ex_;

	std::unique_ptr<task> continuation_;

public:
	future() noexcept : impl_(std::future<void>()) {}

	future(future&& x) noexcept;
	future(promise<void>* p);

	future(ready_future_marker) noexcept
	{
		state_ = state::result;
	}

	future(std::exception_ptr ex) noexcept
	{
		set_exception(std::move(ex));
	}

	template <typename Exception,
			  typename  = std::enable_if_t<std::is_base_of<std::exception, Exception>::value> >
	future(Exception&& ex) noexcept
	{
		set_exception(
			std::make_exception_ptr(std::forward<Exception>(ex))
		);
	}

	future(const future& x) = delete;

	~future()
	{
		switch (state_)
		{
			case state::invalid:
				break;
			case state::future:
				break;
			case state::result:
				break;
			case state::exception:
				ex_.~exception_ptr();
				break;
			default:
				abort();
		}
	}


	void get()
	{
		std::cout << "get" << std::endl;
		switch (state_)
		{
			case state::invalid:
				abort();
			case state::future:
				impl_.get();
				return;
			case state::result:
				return;
			case state::exception:
				std::rethrow_exception(ex_);
		}
	}

	bool valid() const
	{
		switch (state_)
		{
			case state::invalid:
				return false;
			case state::future:
				return impl_.valid();
			case state::result:
				return true;
			case state::exception:
				return true;
		}
	}

	void wait() const
	{
		if (state_ == state::future)
			impl_.wait();
	}

	template <typename Rep, typename Period>
	future_status wait_for(const std::chrono::duration<Rep, Period>& timeout_duration) const
	{
		if (state_ == state::future)
			return static_cast<future_status>(impl_.wait_for(timeout_duration));
		return future_status::ready;
	}

	template <typename Clock, typename Duration>
	future_status wait_until(const std::chrono::time_point<Clock, Duration>& timeout_time) const
	{
		if (state_ == state::future)
			return static_cast<future_status>(impl_.wait_until(timeout_time));
		return future_status::ready;
	}

	template <typename Func, typename Result = futurize_t<std::result_of_t<Func()>> >
	Result then(Func&& func) noexcept
	{
		using futurator = futurize<std::result_of_t<Func()> >;
		if (state_ == state::result)
		{
			try
			{
				return futurator::apply(std::forward<Func>(func));
			}
			catch (...)
			{
				std::cout << "exception....?" << std::endl;
				return Result(std::current_exception());
			}
		}

		if (state_ == state::exception)
		{
			return Result(ex_);
		}

		if (state_ == state::future)
		{
			typename futurator::promise_type pr;

			auto fut = pr.get_future();
			schedule([pr = std::move(pr), func = std::forward<Func>(func)](future* fut) mutable {
				try
				{
					futurator::apply(func).forward_to(pr);
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

	template <typename Func>
	void schedule(Func&& func)
	{
		continuation_ = std::make_unique<continuation<Func, future<void> > >(std::forward<Func>(func), this);
	}

	void forward_to(promise<void>& pr);



private:

	void set_exception(std::exception_ptr ex) noexcept
	{
		assert(state_ == state::future);
		new (&ex_) std::exception_ptr(std::move(ex));
		state_ = state::exception;
	}
};



template <typename T>
inline future<T> make_future(T&& value)
{
	return future<T>(ready_future_marker(), std::forward<T>(value));
}

template <typename T>
inline future<T> make_future(std::exception_ptr ex) noexcept
{
	return future<T>(std::move(ex));
}

template <typename T,
		  typename Exception,
		  typename  = std::enable_if_t<std::is_base_of<std::exception, Exception>::value> >
inline future<T> make_future(Exception&& ex) noexcept
{
	return make_future<T>(std::make_exception_ptr(std::forward<Exception>(ex)));
}

inline future<void> make_future()
{
	return future<void>(ready_future_marker());
}

template <typename T>
inline future<void> make_future()
{
	return future<void>(ready_future_marker());
}





template <typename T>
struct futurize
{
	using type = future<T>;
	using promise_type = promise<T>;

	template <typename Func, typename... Args>
	static inline type apply(Func&& func, Args&&... args) noexcept
	{
		try
		{
			return convert(std::forward<Func>(func)(std::forward<Args>(args)...));
		}
		catch (...)
		{
			return make_future<T>(std::current_exception());
		}
	}

	static inline type convert(T&& value)
	{
		return make_future<T>(std::move(value));
	}

	static inline type convert(type&& value)
	{
		return std::move(value);
	}
};

template <typename T>
struct futurize<future<T> >
{
	using type = future<T>;
	using promise_type = promise<T>;

	template <typename Func, typename... Args>
	static inline type apply(Func&& func, Args&&... args) noexcept
	{
		try
		{
			return convert(std::forward<Func>(func)(std::forward<Args>(args)...));
		}
		catch (...)
		{
			return make_future<T>(std::current_exception());
		}
	}

	static inline type convert(T&& value)
	{
		return make_future<T>(std::move(value));
	}

	static inline type convert()
	{
		return make_future<void>();
	}

	static inline type convert(type&& value)
	{
		return std::move(value);
	}
};

template <>
struct futurize<future<void> >
{
	using type = future<void>;
	using promise_type = promise<void>;

	template <typename Func, typename... Args>
	static inline type apply(Func&& func, Args&&... args) noexcept
	{
		try
		{
			std::forward<Func>(func)(std::forward<Args>(args)...);
			return convert();
		}
		catch (...)
		{
			return make_future<void>(std::current_exception());
		}
	}

	static inline type convert()
	{
		return make_future<void>();
	}

	static inline type convert(type&& value)
	{
		return std::move(value);
	}
};

template <>
struct futurize<void>
{
	using type = future<void>;
	using promise_type = promise<void>;

	template <typename Func, typename... Args>
	static inline type apply(Func&& func, Args&&... args) noexcept
	{
		try
		{
			std::forward<Func>(func)(std::forward<Args>(args)...);
			return convert();
		}
		catch (...)
		{
			return make_future<void>(std::current_exception());
		}
	}

	static inline type convert()
	{
		return make_future<void>();
	}

	static inline type convert(type&& value)
	{
		return std::move(value);
	}
};






template <typename T>
class promise
{
	template <typename U>
	friend class future;

private:
	std::promise<T> impl_;
	future<T>* future_ = nullptr;

public:
	promise() noexcept {}

	promise(promise&& x) noexcept
		: impl_(std::move(x.impl_)),
		  future_(std::exchange(x.future_, nullptr))
	{
		if (future_)
			future_->promise_ = this;
	}

	promise(const promise&) = delete;

	~promise() noexcept
	{
		if (future_)
		{
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

	future<T> get_future() noexcept
	{
		return future<T>(this);
	}

	void set_value(const T& r)
	{
		impl_.set_value(r);
	}

	void set_value(T&& r)
	{
		impl_.set_value(std::move(r));
	}

	void set_exception(std::exception_ptr p) noexcept
	{
		impl_.set_exception(p);
	}

	template <typename Exception>
	void set_exception(Exception&& ex) noexcept
	{
		impl_.set_exception(
			std::make_exception_ptr(std::forward<Exception>(ex))
		);
	}

	void set_value_at_thread_exit(const T& r)
	{
		impl_.set_value_at_thread_exit(r);
	}

	void set_value_at_thread_exit(T&& r)
	{
		impl_.set_value_at_thread_exit(std::move(r));
	}

	void set_exception_at_thread_exit(std::exception_ptr p)
	{
		impl_.set_exception_at_thread_exit(p);
	}

	template <typename Exception>
	void set_exception_at_thread_exit(Exception&& ex)
	{
		impl_.set_exception_at_thread_exit(
			std::make_exception_ptr(std::forward<Exception>(ex))
		);
	}
};

template <>
class promise<void>
{
	template <typename U>
	friend class future;

private:
	std::promise<void> impl_;
	future<void>* future_ = nullptr;

public:
	promise() noexcept {}

	promise(promise&& x) noexcept
		: impl_(std::move(x.impl_)),
		  future_(std::exchange(x.future_, nullptr))
	{
		if (future_)
			future_->promise_ = this;
	}

	promise(const promise&) = delete;

	~promise() noexcept
	{
		if (future_)
		{
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

	future<void> get_future() noexcept
	{
		return future<void>(this);
	}

	void set_value()
	{
		impl_.set_value();
	}

	void set_exception(std::exception_ptr p) noexcept
	{
		impl_.set_exception(p);
	}

	template <typename Exception>
	void set_exception(Exception&& ex) noexcept
	{
		impl_.set_exception(
			std::make_exception_ptr(std::forward<Exception>(ex))
		);
	}

	void set_value_at_thread_exit()
	{
		impl_.set_value_at_thread_exit();
	}

	void set_exception_at_thread_exit(std::exception_ptr p)
	{
		impl_.set_exception_at_thread_exit(p);
	}

	template <typename Exception>
	void set_exception_at_thread_exit(Exception&& ex)
	{
		impl_.set_exception_at_thread_exit(
			std::make_exception_ptr(std::forward<Exception>(ex))
		);
	}
};


future<void>::future(future&& x) noexcept
	: impl_(std::move(x.impl_)),
	  promise_(std::exchange(x.promise_, nullptr)),
	  state_(std::exchange(x.state_, state::invalid))
{
	switch (state_)
	{
		case state::invalid:
			break;
		case state::future:
			break;
		case state::result:
			break;
		case state::exception:
			new (&ex_) std::exception_ptr(std::move(x.ex_));
			x.ex_.~exception_ptr();
			break;
		default:
			abort();
	}

	if (promise_)
	{
		promise_->future_ = this;
	}
}


future<void>::future(promise<void>* p)
{
	impl_ = p->impl_.get_future();
	promise_ = p;
	p->future_ = this;
}

void future<void>::forward_to(promise<void>& pr)
{
	pr.set_value();
}

} // namespace ex






int main(int argc, char* argv[])
{
	// ex::promise<int> pro;
	// ex::future<int> fut = pro.get_future();

	// std::thread thd(
	// 	[pro = std::move(pro)]() mutable {
	// 		sleep(1);
	// 		pro.set_exception(std::runtime_error("Example"));
	// 	}
	// );

	// ex::future_status status = fut.wait_for(std::chrono::seconds(5));
	// if (status == ex::future_status::deferred) {
	// 	std::cout << "deferred" << std::endl;
	// }
	// if (status == ex::future_status::timeout) {
	// 	std::cout << "timeout" << std::endl;
	// }
	// if (status == ex::future_status::ready) {
	// 	std::cout << "ready" << std::endl;
	// }

	// try
	// {
	// 	std::cout << fut.get() << std::endl;
	// }
	// catch(const std::exception& e)
	// {
	// 	std::cout << e.what() << std::endl;
	// }
	// thd.join();

	auto fut0 = ex::make_future<int>(1);
	fut0.then(
		[](int&& n) {
			std::cout << "then1: " << n << std::endl;
		}
	).then(
		[]() {
			std::cout << "then2" << std::endl;
			return 10;
		}

	).then(
		[](int&& n) {
			std::cout << "then3: " << n << std::endl;
			return ex::make_future<std::string>("test");
		}
	).then(
		[](std::string&& s) {
			std::cout << "then4: " << s << std::endl;
			throw std::runtime_error("error");
		}
	).get();
		// then(
	// 	[](){
	// 		std::cout << "then5: " << std::endl;
	// 	}
	// );



	// auto fut1 = ex::make_future(3);
	// fut1.then(
	// 	[](int&& n){
	// 		std::cout << "int return: " << n << std::endl;
	// 		return ex::make_future<int>(10);
	// 	}
	// ).then(
	// 	[](int&& n){
	// 		std::cout << "void return2: " << n << std::endl;
	// 		return 1;
	// 	}
	// );

	// auto fut2 = ex::make_future<int>(4);
	// auto i = fut2.then(
	// 	[](int&& n){
	// 		std::cout << "continuation: " << n << std::endl;
	// 		//return ex::future<std::string>(ex::ready_future_marker(), "test");
	// 		return ex::make_future<std::string>("test");
	// 	}
	// ).get();

	// std::cout << "i = " << i << std::endl;

	// // std::cout << fut2.get() << std::endl;

	// //ex::future<int> fut3(std::runtime_error("Example3"));
	// auto fut3 = ex::make_future<int>(std::runtime_error("Example3"));
	// auto fut4 = fut3.then(
	// 	[](int&& n){
	// 		std::cout << "exception continuation: " << n << std::endl;
	// 		return 1;
	// 	}
	// );

	// try
	// {
	// 	std::cout << fut4.get() << std::endl;
	// }
	// catch (const std::exception& e)
	// {
	// 	std::cout << e.what() << std::endl;
	// }


	return EXIT_SUCCESS;
}
