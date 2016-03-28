#include "future.hpp"
#include "gtest/gtest.h"
#include <typeinfo>
#include <cxxabi.h>



std::string name(const std::type_info& info)
{
	return abi::__cxa_demangle(info.name(), NULL, NULL, NULL);
}

TEST(ReadyFutureTest, get)
{
	auto fut = dot::make_ready_future(13);
	EXPECT_EQ(fut.get(), 13);
	EXPECT_THROW(fut.get(), std::future_error);
}

TEST(ReadyFutureTest, exception)
{
	auto fut = dot::make_exception_future<int>(std::runtime_error("err"));
	EXPECT_THROW(fut.get(), std::runtime_error);
	EXPECT_THROW(fut.get(), std::future_error);
}

TEST(ReadyFutureTest, exception_void)
{
	auto fut = dot::make_exception_future<>(std::runtime_error("err"));

	fut.then(
		[](dot::future<> fut) {
			EXPECT_THROW(fut.get(), std::runtime_error);
			throw std::runtime_error("err");
			return "test";
			}
	).then(
		[](auto fut) {
			EXPECT_THROW(fut.get(), std::runtime_error);
		}
	);
}

TEST(ReadyFutureTest, void_get)
{
	auto fut = dot::make_ready_future<>();
	fut.get();
	EXPECT_THROW(fut.get(), std::future_error);
	EXPECT_THROW(fut.get(), std::future_error);
}

TEST(ReadyFutureTest, then)
{
	auto counter = int{0};
	auto fut = dot::make_ready_future<bool>(true);
	fut.then(
		[&counter](dot::future<bool> fut) {
			counter++; // 1
			if (fut.get())
				return 13;
			else
				return 42;
		}
	).then(
		[&counter](dot::future<int> fut) {
			counter++; // 2
			EXPECT_EQ(fut.get(), 13);
		}
	).then(
		[&counter](dot::future<> fut) {
			counter++; // 3
		}
	).then(
		[&counter](dot::future<> fut) {
			counter++; // 4
			return 42;
		}
	).then(
		[&counter](dot::future<int> fut) {
			counter++; // 5
			EXPECT_EQ(fut.get(), 42);
			throw std::runtime_error("error");
		}
	).then(
		[&counter](dot::future<> fut) {
			counter++; // 6
			EXPECT_THROW(fut.get(), std::runtime_error);
		}
	);
	EXPECT_EQ(counter, 6);
}


struct FutureTest : public ::testing::Test
{
	struct task_base
	{
		virtual ~task_base() {}
		virtual void run() = 0;
	};

	template <typename Func, typename T>
	struct task : public task_base
	{
		Func func_;
		std::promise<T> pr_;
		task(Func&& func, std::promise<T>&& pr)
			: func_(std::move(func)),
			  pr_(std::move(pr))
		{}
		virtual void run() override
		{
			pr_.set_value(func_());
		}
	};

	template <typename Func>
	struct task<Func, void> : public task_base
	{
		Func func_;
		std::promise<void> pr_;
		task(Func&& fun, std::promise<void>&& pr)
			: func_(std::move(fun)),
			  pr_(std::move(pr))
		{}
		virtual void run() override
		{
			func_();
			pr_.set_value();
		}
	};

	std::unique_ptr<std::thread> thread_{nullptr};
	bool exit_{false};
	std::unique_ptr<task_base> task_{nullptr};
	std::mutex lock_;
	std::condition_variable cond_;

	void thread_main()
	{
		while (true)
		{
			std::unique_lock<std::mutex> lock(lock_);
			cond_.wait(lock, [this]{ return exit_ || task_ != nullptr; });
			if (exit_) break;
			task_->run();
			task_ = nullptr;
		}
	}

	template <typename Func, typename Res = std::result_of_t<Func()> >
	std::future<Res> async(Func&& func)
	{
		std::promise<Res> pr;
		std::future<Res> fut = pr.get_future();
		{
			std::unique_lock<std::mutex> lock(lock_);
			EXPECT_EQ(task_, nullptr);
			task_ = std::make_unique<task<Func, Res> >(std::forward<Func>(func), std::move(pr));
		}
		cond_.notify_one();
		return fut;
	}

	template <typename Func, typename Res = std::result_of_t<Func()> >
	Res execute(Func&& func)
	{
		auto fut = async(std::forward<Func>(func));
		return fut.get();
	}

	void SetUp()
	{
		std::unique_lock<std::mutex> lock(lock_);
		thread_ = std::make_unique<std::thread>(&FutureTest::thread_main, this);
	}

	void TearDown()
	{
		{
			std::unique_lock<std::mutex> lock(lock_);
			exit_ = true;
		}

		if (thread_)
		{
			cond_.notify_one();
			thread_->join();
		}
	}
};

TEST_F(FutureTest, fixture_self)
{
	int out{0};
	execute([&out]{ out = 13; });
	EXPECT_EQ(out, 13);

	auto res = execute([]{ return "test"; });
	EXPECT_EQ(res, "test");
}

TEST_F(FutureTest, get)
{
	auto pr = dot::promise<int>();
	auto fut = pr.get_future();
	auto run = async(
		[pr = std::move(pr)]() mutable {
			usleep(1000);
			pr.set_value(13);
		}
	);

	using namespace std::literals::chrono_literals;
	auto status = fut.wait_for(100us);
	EXPECT_EQ(status, dot::future_status::timeout);

	run.get();
	EXPECT_EQ(fut.get(), 13);
}

TEST_F(FutureTest, exception)
{
	auto pr = dot::promise<int>();
	auto fut = pr.get_future();
	auto run = async(
		[pr = std::move(pr)]() mutable {
			usleep(1000);
			pr.set_exception(std::runtime_error("error"));
		}
	);

	using namespace std::literals::chrono_literals;
	auto status = fut.wait_for(100us);
	EXPECT_EQ(status, dot::future_status::timeout);

	run.get();
	EXPECT_THROW(fut.get(), std::runtime_error);
}

TEST_F(FutureTest, set_and_then)
{
	auto counter = int{0};
	auto pr = dot::promise<bool>();
	auto fut = pr.get_future();
	execute(
		[pr = std::move(pr)]() mutable {
			usleep(1000);
			pr.set_value(true);
		}
	);
	fut.then(
		[&counter](dot::future<bool> fut) {
			counter++; // 1
			if (fut.get())
				return 13;
			else
				return 42;
		}
	).then(
		[&counter](dot::future<int> fut) {
			counter++; // 2
			EXPECT_EQ(fut.get(), 13);
		}
	).then(
		[&counter](dot::future<void> fut) {
			counter++; // 3
		}
	).then(
		[&counter](dot::future<> fut) {
			counter++; // 4
			return 42;
		}
	).then(
		[&counter](dot::future<auto> fut) {
			counter++; // 5
			EXPECT_EQ(fut.get(), 42);
			throw std::runtime_error("error");
		}
	).then(
		[&counter](auto fut) {
			counter++; // 6
			EXPECT_THROW(fut.get(), std::runtime_error);
		}
	);
	EXPECT_EQ(counter, 6);
}

TEST_F(FutureTest, then_and_set)
{
	auto counter = int{0};
	auto pr = dot::promise<int>();
	auto fut = pr.get_future();
	auto f = fut.then(
		[&counter](dot::future<int> fut) mutable {
			counter++;
			throw std::runtime_error("error");
			return 10;
		}
	).then(
		[&counter](dot::future<int> fut) {
			counter++;
			EXPECT_EQ(fut.get(), 13);
		}
	).then(
		[&counter](dot::future<> fut) {
			counter++;
		}
	).then(
		[&counter](dot::future<> fut) {
			counter++;
			return 42;
		}
	).then(
		[&counter](dot::future<int> fut) {
			counter++;
			EXPECT_EQ(fut.get(), 42);
			throw std::runtime_error("error");
			return;

		}
	).then(
		[&counter](dot::future<> fut) {
			counter++;
			EXPECT_THROW(fut.get(), std::runtime_error);
			return 13;
		}
	);
	auto run = execute(
		[pr = std::move(pr)]() mutable {
			usleep(1000);
			pr.set_value(13);
			return true;
		}
	);
	EXPECT_EQ(run, true);

	EXPECT_EQ(f.get(), 13);
	EXPECT_EQ(counter, 6);
}

TEST_F(FutureTest, set_get)
{
	dot::promise<int> pr;
	pr.set_value(13);
	auto fut = pr.get_future();
	EXPECT_EQ(fut.get(), 13);
}

TEST_F(FutureTest, wait)
{
	auto pr = dot::promise<int>{};
	auto fut = pr.get_future();
	auto run = async(
		[pr = std::move(pr)]() mutable {
			usleep(1000000);
			pr.set_exception(std::runtime_error("error"));
		}
	);
	using namespace std::literals::chrono_literals;
	{
		dot::future_status st = fut.wait_for(500ms);
		EXPECT_EQ(st, dot::future_status::timeout);
	}
	{
		dot::future_status st = fut.wait_for(1s);
		EXPECT_EQ(st, dot::future_status::ready);
	}
	run.get();
	EXPECT_THROW(fut.get(), std::runtime_error);
}


int main(int argc, char* argv[])
{
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}

#if 0
int main(int argc, char* argv[])
{
	{
		auto pro = dot::promise<int>();
		auto fut = pro.get_future();

		std::thread thd(
			[pro = std::move(pro)]() mutable {
				sleep(1);
				pro.set_value(10);
			}
		);

		// auto fut = dot::make_ready_future<void>();

		//std::cout << "fut: " << &fut << std::endl;
		auto a = fut.then(
			[](dot::future<int> n) {
				std::cout << "1st: " << n.get() << std::endl;
			}
		).then(
			[](dot::future<void> n) {
				std::cout << "2nd: " << std::endl;
				return std::string("test");
			}
		);
		//std::cout << "b: " << &b << std::endl;
		thd.join();
	}


	{
		auto pro = dot::promise<int>();
		auto fut = pro.get_future();

		std::thread thd(
			[pro = std::move(pro)]() mutable {
				sleep(1);
				pro.set_exception(std::runtime_error("blabla"));
			}
		);

		fut.then(
			[](dot::future<int> f){
				try	{
					std::cout << f.get() << std::endl;
				}
				catch (const std::exception& ex)
				{
					std::cout << ex.what() << std::endl;
				}
			}
		);
		thd.join();
	}



	{
		dot::promise<int> pro;
		dot::future<int> fut = pro.get_future();

		std::thread thd(
			[pro = std::move(pro)]() mutable {
				sleep(1);
				pro.set_exception(std::runtime_error("Example"));
			}
		);

		dot::future_status status = fut.wait_for(std::chrono::seconds(5));
		if (status == dot::future_status::deferred) {
			std::cout << "deferred" << std::endl;
		}
		if (status == dot::future_status::timeout) {
			std::cout << "timeout" << std::endl;
		}
		if (status == dot::future_status::ready) {
			std::cout << "ready" << std::endl;
		}

		try
		{
			std::cout << fut.get() << std::endl;
		}
		catch(const std::exception& e)
		{
			std::cout << e.what() << std::endl;
		}
		thd.join();
	}


	auto fut0 = dot::make_ready_future<int>(1);
	fut0.then(
		[](dot::future<int> n) {
			std::cout << "then1: " << n.get() << std::endl;
		}
	).then(
		[](dot::future<void> f) {
			std::cout << "then2" << std::endl;
			return 10;
		}

	).then(
		[](dot::future<int> n) {
			std::cout << "then3: " << n.get() << std::endl;
			return dot::make_ready_future<std::string>("test");
		}
	).then(
		[](dot::future<std::string> s) {
			std::cout << "then4: " << s.get() << std::endl;
			throw std::runtime_error("error");
			return std::string("test");
		}
	).then(
		[](dot::future<std::string> fut) {
			try
			{
				std::cout << fut.get() << std::endl;
			}
			catch (...)
			{
				std::cout << "!!!!!!!!!!!" << std::endl;
			}
		}
	).then(
		[](dot::future<void> n){
			std::cout << "then5: " << std::endl;
		}
	);



	auto fut1 = dot::make_ready_future(3);
	fut1.then(
		[](dot::future<int> n){
			std::cout << "int return: " << n.get() << std::endl;
			return dot::make_ready_future<int>(10);
		}
	).then(
		[](dot::future<int> n){
			std::cout << "void return2: " << n.get() << std::endl;
			return 1;
		}
	);

	auto fut2 = dot::make_ready_future<int>(4);
	auto i = fut2.then(
		[](dot::future<int> n){
			std::cout << "continuation: " << n.get() << std::endl;
			//return dot::future<std::string>(dot::ready_future_marker(), "test");
			return dot::make_ready_future<std::string>("test");
		}
	).get();

	std::cout << "i = " << i << std::endl;

	// std::cout << fut2.get() << std::endl;

	//dot::future<int> fut3(std::runtime_error("Example3"));
	auto fut3 = dot::make_ready_future<int>(std::runtime_error("Example3"));
	auto fut4 = fut3.then(
		[](dot::future<int> n){
			std::cout << "exception continuation: " << n.get() << std::endl;
			return 1;
		}
	);

	try
	{
		std::cout << fut4.get() << std::endl;
	}
	catch (const std::exception& e)
	{
		std::cout << e.what() << std::endl;
	}


	return EXIT_SUCCESS;
}
#endif
