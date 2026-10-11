#include <gtest/gtest.h>

#include "greeter.h"

TEST(Greeter, GreetsByName) {
    EXPECT_EQ(greet("World"), "Hello, World!");
}

TEST(Greeter, HandlesEmptyName) {
    EXPECT_EQ(greet(""), "Hello, !");
}
