#include <iostream>
#include <string>
#include <chrono>
#include <mutex>
#include <vector>
#include <thread>
#include <sstream>
#include "ThreadPool.h"

std::mutex cout_mutex;

template<typename... Args>
void safePrint(Args&&... args) {
    std::lock_guard<std::mutex> lock(cout_mutex);
    std::ostringstream oss;
    ((oss << std::forward<Args>(args)), ...);
    std::cout << oss.str() << std::endl;
}

int calculate(int a, int b, int delay, int pri) {
    safePrint("start calculate task:", a, "+", b, ", delay:", delay, ", priority:", pri, ", TID:", std::this_thread::get_id());
    std::this_thread::sleep_for(std::chrono::seconds(delay));
    safePrint("end calculate task:", a, "+", b, ", TID:", std::this_thread::get_id());
    return a + b;
}

int main() {
    ThreadPool pool(2);
    safePrint("initial thread count:", pool.get_thread_count());
    std::vector<std::future<int>> results;

    safePrint("add tasks with different priority");
    
    for(int i = 0; i < 2; i++) {
        results.push_back(pool.enqueue(TaskPriority::LOW, calculate, i, i+1, 3, (int)TaskPriority::LOW).first);
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    for(int i = 2; i < 4; i++) {
        results.push_back(pool.enqueue(TaskPriority::MEDIUM, calculate, i, i+1, 2, (int)TaskPriority::MEDIUM).first);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    for(int i = 4; i < 6; i++) {
        results.push_back(pool.enqueue(TaskPriority::HIGH, calculate, i, i+1, 1, (int)TaskPriority::HIGH).first);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    for(int i = 6; i < 8; i++) {
        results.push_back(pool.enqueue(TaskPriority::CRITICAL, calculate, i, i+1, 1, (int)TaskPriority::CRITICAL).first);
    }

    for(auto &&result : results) {
        safePrint("result:", result.get());
    }
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return 0;
}