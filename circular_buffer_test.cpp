#include "gtest/gtest.h"
#include "circular_buffer.hpp"
#include <sstream>

using namespace dot;

template <typename T>
std::string values(const circular_buffer<T>& cb)
{
	std::ostringstream os;
	cb.for_each([&os](T& t) {
			os << t;
		}
	);
	return os.str();
}

TEST(CircularBufferTest, basic)
{
	circular_buffer<int> cb;
	EXPECT_EQ(cb.capacity(), 0);

	cb.push_back(1);
	EXPECT_EQ(cb.capacity(), 1);

	cb.push_back(2);
	EXPECT_EQ(cb.capacity(), 2);

	cb.push_back(3);
	EXPECT_EQ(cb.size(), 3);
	EXPECT_EQ(cb.capacity(), 4);

	cb.push_back(4);
	EXPECT_EQ(cb.size(), 4);
	EXPECT_EQ(cb.capacity(), 4);

	cb.push_front(5);
	EXPECT_EQ(cb.size(), 5);
	EXPECT_EQ(cb.capacity(), 8);
	EXPECT_EQ(values(cb), "51234");
	//std::cout << cb.impl_.begin << "," << cb.impl_.end << std::endl;

	cb.pop_front();
	EXPECT_EQ(cb.size(), 4);
	EXPECT_EQ(cb.capacity(), 8);
	EXPECT_EQ(values(cb), "1234");
	//std::cout << cb.impl_.begin << "," << cb.impl_.end << std::endl;
}

struct node
{
	int id;
	std::string value;
	node(int x, std::string y)
		: id(x), value(std::move(y)) {}
};

inline std::ostream& operator<<(std::ostream& os, const node& n)
{
	os << n.id << n.value;
	return os;
}

TEST(CircularBufferTest, object)
{
	circular_buffer<node> cb;
	cb.emplace_back(1, "one");
	cb.emplace_front(0, "zero");
	cb.push_back({2, "two"});
	EXPECT_EQ(cb.size(), 3);
	EXPECT_EQ(cb.capacity(), 4);
	EXPECT_EQ(values(cb), "0zero1one2two");
	EXPECT_EQ(cb.front().value, "zero");

	cb.pop_back();
	EXPECT_EQ(values(cb), "0zero1one");
}

TEST(CircularBufferTest, empty)
{
	circular_buffer<int> cb;
	cb.emplace_back(2);
	cb.emplace_front(1);

	EXPECT_EQ(cb.back(), 2);
	cb.pop_back();
	EXPECT_EQ(cb.back(), 1);

	cb.pop_back();
	EXPECT_TRUE(cb.empty());
}
