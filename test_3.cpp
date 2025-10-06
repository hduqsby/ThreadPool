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

void processTask(const std::string &taskName, int sleepTime) {
    safePrint("Task started:", taskName, ", TID:", std::this_thread::get_id());
    std::this_thread::sleep_for(std::chrono::seconds(sleepTime));
    safePrint("Task completed:", taskName, ", TID:", std::this_thread::get_id());
}

int calculate(int a, int b, int delay, int pri) {
    safePrint("start calculate task:", a, "+", b, ", delay:", delay, ", priority:", pri, ", TID:", std::this_thread::get_id());
    std::this_thread::sleep_for(std::chrono::seconds(delay));
    safePrint("end calculate task:", a, "+", b, ", TID:", std::this_thread::get_id());
    return a + b;
}

int main() {
    ThreadPool pool(1);
    safePrint("Initial thread count:", pool.get_thread_count());
    
    safePrint("Testing task cancellation...");
    std::vector<std::pair<std::future<int>, TaskID>> cancel_tasks;
    for(int i = 0; i < 4; i++) {
        cancel_tasks.push_back(
            pool.enqueue(TaskPriority::LOW, calculate, i, i+1, 5, (int)TaskPriority::LOW)
        );
    }
    
    if (cancel_tasks.size() >= 4) {
        TaskID id3 = cancel_tasks[2].second;
        TaskID id4 = cancel_tasks[3].second;
        safePrint("Cancelling task", id3, "and", id4);
        pool.cancelTask(id3);
        pool.cancelTask(id4);
    }
    
    for(size_t i = 0; i < cancel_tasks.size(); i++) {
        try {
            int result = cancel_tasks[i].first.get();
            safePrint("task", i, "result:", result);
        } catch (const std::exception &e) {
            safePrint("Exception from cancel task", i, ":", e.what());
        }
    }
    
    safePrint("Waiting for all tasks to complete...");
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    safePrint("Test completed.");
    return 0;
}