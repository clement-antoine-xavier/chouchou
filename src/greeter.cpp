#include "greeter.h"

std::string greet(std::string_view name) {
    return "Hello, " + std::string{name} + "!";
}
