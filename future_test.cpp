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

TEST(ReadyFutureTest, return_future)
{
	dot::make_ready_future(13).then(
		[](auto fut) {
			return dot::make_ready_future(42);
		}
	);
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

TEST(ReadyFutureTest, failed)
{
	auto f1 = dot::make_ready_future<>();
	EXPECT_FALSE(f1.failed());

	auto f2 = dot::make_exception_future<>(std::runtime_error("f2"));
	EXPECT_TRUE(f2.failed());

	dot::make_ready_future<>().then(
		[](auto fut) {
			EXPECT_FALSE(fut.failed());
			throw std::runtime_error("fut1");
		}
	).then(
		[](auto fut) {
			EXPECT_TRUE(fut.failed());
		}
	);
}

TEST(ReadyFutureTest, when_all_empty)
{
	auto f = dot::when_all();
	EXPECT_EQ(name(typeid(f)), "dot::future<std::tuple<> >");
}

TEST(ReadyFutureTest, when_all_variadic)
{
	auto x = dot::make_ready_future<int>(1);
	auto y = dot::make_ready_future<int>(2);
	when_all(std::move(x), std::move(y)).then(
		[](auto fut) {
			dot::future<int> x;
			dot::future<int> y;
			std::tie(x, y) = fut.get();
			EXPECT_EQ(x.get(), 1);
			EXPECT_EQ(y.get(), 2);
		}
	);
}

TEST(ReadyFutureTest, when_all_iterator)
{
	std::vector<dot::future<int> > futures;
	futures.push_back(dot::make_ready_future(1));
	futures.push_back(dot::make_ready_future(2));
	futures.push_back(dot::make_ready_future(3));

	dot::when_all(begin(futures), end(futures)).then(
		[](auto fut) {
			auto vec = fut.get();
			auto i = 1;
			for (auto f = begin(vec); f == end(vec); f++, i++)
			{
				EXPECT_EQ(f->get(), i);
			}
		}
	);
}

TEST(ReadyFutureTest, when_any_variadic)
{
	auto x = dot::make_ready_future(1);
	auto y = dot::make_ready_future(2);
	when_any(std::move(x), std::move(y)).then(
		[](auto fut) {
			dot::future<int> x;
			dot::future<int> y;
			std::tie(x, y) = fut.get();

			EXPECT_TRUE(x.ready());
			EXPECT_FALSE(y.ready());
			EXPECT_EQ(x.get(), 1);
		}
	);
}

TEST(ReadyFutureTest, when_any_iterator)
{
	std::vector<dot::future<int> > futures;
	futures.push_back(dot::make_ready_future(1));
	futures.push_back(dot::make_ready_future(2));
	futures.push_back(dot::make_ready_future(3));

	dot::when_any(begin(futures), end(futures)).then(
		[](auto fut) {
			auto vec = fut.get();

			EXPECT_TRUE(vec[0].ready());
			EXPECT_FALSE(vec[1].ready());
			EXPECT_FALSE(vec[2].ready());

			EXPECT_EQ(vec[0].get(), 1);
		}
	);
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
		[&counter](auto fut) {
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

TEST_F(FutureTest, set_and_get)
{
	dot::promise<int> pr;
	pr.set_value(13);
	auto fut = pr.get_future();
	EXPECT_EQ(fut.get(), 13);
}

TEST_F(FutureTest, get_and_set)
{
	auto pr = dot::promise<int>();
	auto fut = pr.get_future();
	auto run = async(
		[pr = std::move(pr)]() mutable {
			usleep(1000);
			pr.set_value(13);
		}
	);
	EXPECT_EQ(fut.get(), 13);
	run.get();
}

TEST_F(FutureTest, wait)
{
	auto pr = dot::promise<int>{};
	auto fut = pr.get_future();
	auto run = async(
		[pr = std::move(pr)]() mutable {
			usleep(100000);
			pr.set_exception(std::runtime_error("error"));
		}
	);
	using namespace std::literals::chrono_literals;
	{
		dot::future_status st = fut.wait_for(50ms);
		EXPECT_EQ(st, dot::future_status::timeout);
	}
	{
		dot::future_status st = fut.wait_for(1s);
		EXPECT_EQ(st, dot::future_status::ready);
	}
	run.get();
	EXPECT_THROW(fut.get(), std::runtime_error);
}

TEST_F(FutureTest, failed)
{
	auto pr = dot::promise<int>();
	auto fut = pr.get_future();
	auto run = async(
		[pr = std::move(pr)]() mutable {
			usleep(1000);
			pr.set_exception(std::runtime_error("error"));
		}
	);

	fut.then(
		[](auto fut) {
			EXPECT_TRUE(fut.failed());
		}
	);
}

TEST_F(FutureTest, when_all_variadic)
{
	auto p1 = dot::promise<int>();
	auto p2 = dot::promise<bool>();
	auto p3 = dot::promise<>();
	auto f1 = p1.get_future();
	auto f2 = p2.get_future();
	auto f3 = p3.get_future();

	dot::when_all(std::move(f1), std::move(f2), std::move(f3)).then(
		[](auto fut) {
			dot::future<int> x;
			dot::future<bool> y;
			dot::future<> z;
			std::tie(x, y, z) = fut.get();

			EXPECT_EQ(x.get(), 13);
			EXPECT_EQ(y.get(), true);
			EXPECT_THROW(z.get(), std::runtime_error);
		}
	);

	execute(
		[p1 = std::move(p1), p2 = std::move(p2), p3 = std::move(p3)]() mutable {
			p1.set_value(13);
			p2.set_value(true);
			p3.set_exception(std::runtime_error("test"));
		}
	);
}

TEST_F(FutureTest, when_all_iterator)
{
	std::vector<dot::promise<int> > promises(3);
	std::vector<dot::future<int> > futures;
	for (auto& p : promises)
	{
		futures.push_back(p.get_future());
	}

	dot::when_all(begin(futures), end(futures)).then(
		[](auto fut) {
			auto vec = fut.get();
			auto i = 1;
			for (auto f = begin(vec); f == end(vec); f++, i++)
			{
				EXPECT_EQ(f->get(), i);
			}
		}
	);

	execute(
		[promises = std::move(promises)]() mutable {
			auto i = 1;
			for (auto p = begin(promises); p == end(promises); p++, i++)
			{
				p->set_value(i);
			}
		}
	);
}

TEST_F(FutureTest, when_any_variadic)
{
	auto p1 = dot::promise<>();
	auto p2 = dot::promise<bool>();
	auto p3 = dot::promise<int>();
	auto f1 = p1.get_future();
	auto f2 = p2.get_future();
	auto f3 = p3.get_future();

	dot::when_any(std::move(f1), std::move(f2), std::move(f3)).then(
		[](auto fut) {
			dot::future<> x;
			dot::future<bool> y;
			dot::future<int> z;
			std::tie(x, y, z) = fut.get();
			EXPECT_FALSE(x.ready());
			EXPECT_FALSE(y.ready());
			EXPECT_TRUE(z.ready());
			EXPECT_EQ(z.get(), 13);
		}
	);

	execute(
		[p1 = std::move(p1), p2 = std::move(p2), p3 = std::move(p3)]() mutable {
			//p1.set_value();
			//p2.set_value(true);
			p3.set_value(13);
		}
	);
}

TEST_F(FutureTest, when_any_iterator)
{
	std::vector<dot::promise<int> > promises(3);
	std::vector<dot::future<int> > futures;
	for (auto& p : promises)
	{
		futures.push_back(p.get_future());
	}

	dot::when_any(begin(futures), end(futures)).then(
		[](auto fut) {
			auto vec = fut.get();
			EXPECT_FALSE(vec[0].ready());
			EXPECT_TRUE(vec[1].ready());
			EXPECT_FALSE(vec[2].ready());
			EXPECT_EQ(vec[1].get(), 42);
		}
	);

	execute(
		[promises = std::move(promises)]() mutable {
			promises[1].set_value(42);
		}
	);
}

dot::future<> readable()
{
	std::cout << std::this_thread::get_id() << " readable" << std::endl;
	dot::promise<> pro;
	dot::future<> fut = pro.get_future();
	std::thread t(
		[pro = std::move(pro)]() mutable{
			std::cout << std::this_thread::get_id() << " readable thread"<< std::endl;
			sleep(2);
			pro.set_value();
		}
	);
	t.detach();
	return fut;
}

dot::future<int> recv()
{
	return readable().then(
		[](auto fut){
			std::cout << std::this_thread::get_id() << " readable continuation" << std::endl;
			return dot::make_ready_future<int>(1);
		}
	);
}

TEST_F(FutureTest, fake_recv)
{
	std::cout << std::this_thread::get_id() << " before recv" << std::endl;
	auto f1 = recv().then(
		[](auto fut){
			std::cout << std::this_thread::get_id() << " first recved" << std::endl;
		}
	);
	auto f2 = recv().then(
		[](auto fut){
			std::cout << std::this_thread::get_id() << " second recved" << std::endl;
		}
	);
	dot::when_all(std::move(f1), std::move(f2)).get();
	std::cout << std::this_thread::get_id() << " after recv" << std::endl;
}

void accept()
{
	readable().then(
		[](auto fut){
			std::cout << std::this_thread::get_id() << " accepted" << std::endl;
			recv().then(
				[](auto fut){
					std::cout << std::this_thread::get_id() << " recv in accept" << std::endl;
				}
			);
			accept();
		}
	);
}

TEST_F(FutureTest, fake_accept)
{
	std::cout << std::this_thread::get_id() << " before accept" << std::endl;
	accept();
	sleep(10);
	std::cout << std::this_thread::get_id() << " after accept" << std::endl;
}
