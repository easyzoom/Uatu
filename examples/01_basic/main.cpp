// 场景：一个简单的数学计算服务，持续运行
// 演示：用 watch 观测函数参数和返回值，用 stack 看调用来源
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <cmath>

namespace basic {

int add(int a, int b) {
    return a + b;
}

double power(double base, int exp) {
    double result = 1.0;
    for (int i = 0; i < exp; ++i)
        result *= base;
    return result;
}

std::string greet(const std::string& name) {
    return "Hello, " + name + "!";
}

class Counter {
public:
    void increment() { ++count_; }
    int  value() const { return count_; }
private:
    int count_{0};
};

} // namespace basic

int main() {
    basic::Counter counter;
    std::cout << "READY pid=" << getpid() << std::endl;
    std::cout.flush();

    int i = 0;
    while (true) {
        basic::add(i, i * 2);
        basic::power(2.0, i % 10 + 1);
        basic::greet("uatu");
        counter.increment();
        ++i;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}
