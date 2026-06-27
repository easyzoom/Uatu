#include <iostream>
#include <string>
#include <thread>
#include <chrono>

namespace fixtures {

class Calculator {
public:
    int add(int a, int b) { return a + b; }
    double divide(double a, double b) {
        if (b == 0.0) throw std::runtime_error("division by zero");
        return a / b;
    }
};

// Free function with const char* param — used to test string capture.
std::size_t str_len(const char* s) {
    if (!s) return 0;
    std::size_t n = 0;
    while (s[n]) ++n;
    return n;
}

class Foo {
public:
    std::string bar(int x, const std::string& msg) {
        std::this_thread::sleep_for(std::chrono::microseconds(x * 10));
        return msg + std::to_string(x);
    }

    int slow(int n) {
        int sum = 0;
        for (int i = 0; i < n; ++i) sum += add_internal(i);
        return sum;
    }

private:
    int add_internal(int x) { return x * 2; }
};

} // namespace fixtures

int main() {
    fixtures::Calculator calc;
    fixtures::Foo foo;

    std::cout << "READY" << std::endl;
    std::cout.flush();

    while (true) {
        calc.add(1, 2);
        calc.divide(10.0, 3.0);
        foo.bar(5, "hello");
        foo.slow(100);
        fixtures::str_len("hello world");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
