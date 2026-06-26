// 场景：某些请求特别慢，但不知道慢在哪一层
// 演示：用 trace 找到慢子调用
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <algorithm>
#include <cstdlib>

namespace slow_path {

void fast_op(int n) {
    int sum = 0;
    for (int i = 0; i < n; ++i) sum += i;
    asm volatile("" : "+r"(sum));  // prevent optimizer from eliding the loop
}

void slow_op(int n) {
    // 模拟慢操作：随机 sleep
    std::this_thread::sleep_for(std::chrono::milliseconds(n * 10));
}

void medium_op(int n) {
    std::vector<int> v(n);
    for (int i = 0; i < n; ++i) v[i] = rand() % 1000;
    std::sort(v.begin(), v.end());
}

// 请求处理：大部分快，偶尔触发 slow_op
void handle_request(int request_id) {
    fast_op(1000);
    medium_op(500);

    // 每 4 次触发一次慢路径
    if (request_id % 4 == 0)
        slow_op(5);  // 慢 50ms
    else
        fast_op(100);
}

} // namespace slow_path

int main() {
    std::cout << "READY pid=" << getpid() << std::endl;
    std::cout.flush();

    int id = 0;
    while (true) {
        slow_path::handle_request(id++);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
