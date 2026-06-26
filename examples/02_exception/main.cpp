// 场景：偶发异常的服务（模拟真实线上 bug）
// 演示：用 watch 观测异常路径，找到触发条件
#include <iostream>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <cstdlib>

namespace exception_demo {

double safe_divide(double a, double b) {
    if (b == 0.0)
        throw std::invalid_argument("division by zero");
    return a / b;
}

int parse_input(const std::string& s) {
    if (s.empty())
        throw std::runtime_error("empty input");
    return std::stoi(s);
}

double process(int request_id) {
    // 每 5 次请求触发一次除零
    double divisor = (request_id % 5 == 0) ? 0.0 : static_cast<double>(request_id);
    try {
        return safe_divide(100.0, divisor);
    } catch (const std::exception& e) {
        // 吞掉异常，线上常见反模式
        return -1.0;
    }
}

} // namespace exception_demo

int main() {
    std::cout << "READY pid=" << getpid() << std::endl;
    std::cout.flush();

    int id = 1;
    while (true) {
        exception_demo::process(id++);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
}
