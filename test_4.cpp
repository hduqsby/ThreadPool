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
    
    safePrint("Testing task group submission and cancellation...");
    
    auto group = pool.enqueue_group(TaskPriority::MEDIUM, 4, calculate, 100, 200, 5);
    safePrint("Task group submitted, group id:", group.second);
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    bool groupCancelRes = pool.cancelTaskGroup(group.second);
    safePrint("Cancel task group result:", groupCancelRes ? "Success" : "Failed");
    
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
