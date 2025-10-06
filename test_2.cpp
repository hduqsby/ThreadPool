#include <iostream>
#include <string>
#include <chrono>
#include <mutex>
#include <vector>
#include <thread>
#include "ThreadPool.h"

std::mutex cout_mutex;

template<typename... Args>
void safePrint(Args&&... args) {
    std::lock_guard<std::mutex> lock(cout_mutex);
    (std::cout << ... << std::forward<Args>(args)) << std::endl;
}

int calculate(int a, int b, int delay, int pri) {
    safePrint("start calculate task:", a, "+", b, ", delay:", delay, ", priority:", pri, ", TID:", std::this_thread::get_id());
    std::this_thread::sleep_for(std::chrono::seconds(delay));
    safePrint("end calculate task:", a, "+", b, ", TID:", std::this_thread::get_id());
    return a + b;
}

int main() {
    ThreadPool pool(4);
    safePrint("initial thread count:", pool.get_thread_count());
    std::vector<std::future<int>> results;

    safePrint("add tasks with different priority");
    // LOW 优先级
    for(int i = 0; i < 4; i++) {
        results.push_back(pool.enqueue(TaskPriority::LOW, calculate, i, i+1, 3, (int)TaskPriority::LOW).first);
    }
    // HIGH 优先级
    for(int i = 4; i < 8; i++) {
        results.push_back(pool.enqueue(TaskPriority::HIGH, calculate, i, i+1, 1, (int)TaskPriority::HIGH).first);
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));
    safePrint("dynamic add thread");
    pool.resize(8);
    safePrint("thread count after add:", pool.get_thread_count());

    // CRITICAL 优先级
    for(int i = 8; i < 12; i++) {
        results.push_back(pool.enqueue(TaskPriority::CRITICAL, calculate, i, i+1, 1, (int)TaskPriority::CRITICAL).first);
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));
    safePrint("dynamic remove thread");
    pool.remove_threads(4);
    safePrint("thread count after remove:", pool.get_thread_count());

    for(auto &&result : results) {
        safePrint("result:", result.get());
    }
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return 0;
}