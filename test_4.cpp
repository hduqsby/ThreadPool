#include <iostream>
#include <string>
#include <chrono>
#include <mutex>
#include <vector>
#include <thread>
#include <sstream>
#include "ThreadPool.h"

std::mutex cout_mutex;

template<typename T>
void appendToStream(std::ostringstream &oss, T &&arg) {
    oss << std::forward<T>(arg);
}

template<typename T, typename... Args>
void appendToStream(std::ostringstream &oss, T &&first, Args&&... rest) {
    oss << std::forward<T>(first);
    appendToStream(oss, std::forward<Args>(rest)...);
}

template<typename... Args>
void safePrint(Args&&... args) {
    std::lock_guard<std::mutex> lock(cout_mutex);
    std::ostringstream oss;
    appendToStream(oss, std::forward<Args>(args)...);
    std::cout << oss.str() << std::endl;
}

int calculate(int a, int b, int delay) {
    safePrint("Calculate task started:", a, "+", b, ", TID:", std::this_thread::get_id());
    std::this_thread::sleep_for(std::chrono::seconds(delay));
    return a + b;
}

int main() {
    ThreadPool pool(4);
    safePrint("Initial thread count:", pool.get_thread_count());
    
    // 测试任务组提交和取消机制
    safePrint("Testing task group submission and cancellation...");
    
    // 提交一个任务组，包含4个任务
    auto group = pool.enqueue_group(TaskPriority::MEDIUM, 4, calculate, 100, 200, 5);
    safePrint("Task group submitted, group id:", group.second);
    
    // 等待一会儿，让一些任务开始执行
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // 取消整个任务组
    bool groupCancelRes = pool.cancelTaskGroup(group.second);
    safePrint("Cancel task group result:", groupCancelRes ? "Success" : "Failed");
    
    // 获取所有任务的结果
    for (size_t i = 0; i < group.first.size(); ++i) {
        try {
            int result = group.first[i].first.get();
            safePrint("Group Task", i, "result:", result);
        } catch (const std::exception &e) {
            safePrint("Exception from group task", i, ":", e.what());
        }
    }
    
    safePrint("Waiting for all tasks to complete...");
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    safePrint("Test completed.");
    return 0;
}
