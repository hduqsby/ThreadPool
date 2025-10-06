#ifndef THREAD_POOL_H
#define THREAD_POOL_H

// 1.任务优先级支持
// 2.动态线程数量调整
// 3.任务取消机制
// 4.任务组提交和取消
#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>
#include <algorithm>
#include <atomic>
#include <unordered_map>
#include <tuple>
#include <utility>

// 任务优先级
enum class TaskPriority
{
    LOW = 0,
    MEDIUM = 1,
    HIGH = 2,
    CRITICAL = 3
};

typedef uint64_t TaskID;

// 任务取消状态枚举
enum class TaskCancelStatus {
    PENDING,    // 任务等待执行
    RUNNING,    // 任务正在执行
    COMPLETED,  // 任务已完成
    CANCELLED   // 任务已取消
};

// 任务组ID类型
using TaskGroupID = uint64_t;

class ThreadPool
{
public:
    ThreadPool(size_t);

    template <class F, class... Args>
    auto enqueue(TaskPriority priority, F &&f, Args &&...args)
        -> std::pair<std::future<typename std::result_of<F(Args...)>::type>, TaskID>;

    // 任务组批量提交
    template <class F, class... Args>
    std::pair<std::vector<std::pair<std::future<typename std::result_of<F(Args...)>::type>, TaskID>>, TaskGroupID>
    enqueue_group(TaskPriority priority, size_t count, F &&f, Args &&...args);

    bool cancelTask(TaskID id);
    bool cancelTaskGroup(TaskGroupID group_id);
    TaskCancelStatus getTaskStatus(TaskID id) const;
    
    void resize(size_t new_thread_count);
    void add_threads(size_t count);
    void remove_threads(size_t count);
    size_t get_thread_count() const;

    ~ThreadPool();

private:
    struct TaskItem
    {
        std::function<void()> task;
        TaskPriority priority;
        TaskID id;
        bool operator<(const TaskItem &other) const
        {
            // 优先级小的被认为小于，大的在堆顶
            return priority < other.priority;
        }
    };

    struct Worker
    {
        std::thread thread;
        bool stop_flag = false;
    };
    std::vector<Worker> workers;
    std::priority_queue<TaskItem> tasks;

    mutable std::mutex task_map_mutex;
    std::unordered_map<TaskID, std::shared_ptr<std::atomic<TaskCancelStatus>>> task_status_map;

    // 任务组相关映射
    std::unordered_map<TaskID, TaskGroupID> taskid_to_groupid;
    std::unordered_map<TaskGroupID, std::vector<TaskID>> groupid_to_taskids;
    std::atomic<TaskGroupID> next_group_id{1};

    mutable std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
    std::atomic<TaskID> next_task_id; 

    void wait_for_worker_to_finish();
    void start_worker(Worker &worker);
};

namespace detail {
    template <typename F, typename Tuple, std::size_t... I>
    auto invoke_tuple_impl(F &&f, Tuple &&t, std::index_sequence<I...>)
        -> decltype(std::declval<F>()(std::get<I>(std::declval<Tuple>())...))
    {
        return std::forward<F>(f)(std::get<I>(std::forward<Tuple>(t))...);
    }

    template <typename F, typename Tuple>
    auto invoke_tuple(F &&f, Tuple &&t)
        -> decltype(invoke_tuple_impl(std::forward<F>(f), std::forward<Tuple>(t),
                                     std::make_index_sequence<std::tuple_size<typename std::decay<Tuple>::type>::value>{}))
    {
        return invoke_tuple_impl(std::forward<F>(f), std::forward<Tuple>(t),
                                 std::make_index_sequence<std::tuple_size<typename std::decay<Tuple>::type>::value>{});
    }
}

inline ThreadPool::ThreadPool(size_t threads)
    : stop(false), next_task_id(1)
{
    add_threads(threads);
}


template <class F, class... Args>
auto ThreadPool::enqueue(TaskPriority priority, F &&f, Args &&...args)
    -> std::pair<std::future<typename std::result_of<F(Args...)>::type>, TaskID>
{
    using return_type = typename std::result_of<F(Args...)>::type;

    TaskID task_id = next_task_id++; 
    
    auto cancel_status = std::make_shared<std::atomic<TaskCancelStatus>>(TaskCancelStatus::PENDING);
    
    {
        std::lock_guard<std::mutex> lock(task_map_mutex);
        task_status_map[task_id] = cancel_status;
    }

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        [f = std::forward<F>(f), args = std::make_tuple(std::forward<Args>(args)...), cancel_status]() -> return_type {
            if (cancel_status->load() == TaskCancelStatus::CANCELLED) {
                throw std::runtime_error("Task cancelled");
            }

            cancel_status->store(TaskCancelStatus::RUNNING);

            try {
                auto result = detail::invoke_tuple(f, args);
                cancel_status->store(TaskCancelStatus::COMPLETED);
                return result;
            } catch (...) {
                cancel_status->store(TaskCancelStatus::COMPLETED);
                throw;
            }
        });

    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex);

        if (stop)
            throw std::runtime_error("enqueue on stopped ThreadPool");

        tasks.push(TaskItem{
            [task, cancel_status]() {
                if (cancel_status->load() != TaskCancelStatus::CANCELLED) {
                    (*task)();
                }
            },
            priority,
            task_id
        });
    }
    condition.notify_one();
    return std::make_pair(std::move(res), task_id);
}

// 任务组批量提交实现
// 返回所有future和taskid，以及groupid
// 用法：auto group = pool.enqueue_group(priority, n, func, ...args);
template <class F, class... Args>
std::pair<std::vector<std::pair<std::future<typename std::result_of<F(Args...)>::type>, TaskID>>, TaskGroupID>
ThreadPool::enqueue_group(TaskPriority priority, size_t count, F &&f, Args &&...args)
{
    using return_type = typename std::result_of<F(Args...)>::type;
    std::vector<std::pair<std::future<return_type>, TaskID>> futures;
    TaskGroupID group_id = next_group_id++;
    std::vector<TaskID> ids;
    futures.reserve(count);  // 预留空间，避免重新分配时复制
    for (size_t i = 0; i < count; ++i) {
        auto ret = enqueue(priority, std::forward<F>(f), std::forward<Args>(args)...);
        futures.push_back(std::move(ret));
        ids.push_back(ret.second);
        {
            std::lock_guard<std::mutex> lock(task_map_mutex);
            taskid_to_groupid[ret.second] = group_id;
        }
    }
    {
        std::lock_guard<std::mutex> lock(task_map_mutex);
        groupid_to_taskids[group_id] = ids;
    }
    return std::make_pair(std::move(futures), group_id);
}

inline bool ThreadPool::cancelTask(TaskID id) {
    std::shared_ptr<std::atomic<TaskCancelStatus>> status_ptr;
    
    {
        std::lock_guard<std::mutex> lock(task_map_mutex);
        auto it = task_status_map.find(id);
        if (it == task_status_map.end()) {
            return false; 
        }
        status_ptr = it->second;
    }
    
    TaskCancelStatus expected = TaskCancelStatus::PENDING;
    bool success = status_ptr->compare_exchange_strong(expected, TaskCancelStatus::CANCELLED);
    
    if (!success) {
        return false;
    }
    
    return true;
}

inline bool ThreadPool::cancelTaskGroup(TaskGroupID group_id) {
    std::lock_guard<std::mutex> lock(task_map_mutex);
    auto it = groupid_to_taskids.find(group_id);
    if (it == groupid_to_taskids.end()) return false;
    bool any_cancelled = false;
    for (TaskID tid : it->second) {
        auto status_it = task_status_map.find(tid);
        if (status_it != task_status_map.end()) {
            TaskCancelStatus expected = TaskCancelStatus::PENDING;
            if (status_it->second->compare_exchange_strong(expected, TaskCancelStatus::CANCELLED)) {
                any_cancelled = true;
            }
        }
    }
    return any_cancelled;
}

inline TaskCancelStatus ThreadPool::getTaskStatus(TaskID id) const {
    std::lock_guard<std::mutex> lock(task_map_mutex);
    auto it = task_status_map.find(id);
    if (it == task_status_map.end()) {
        throw std::runtime_error("Task not found");
    }
    return it->second->load();
}

inline void ThreadPool::resize(size_t new_thread_count)
{
    std::unique_lock<std::mutex> lock(queue_mutex);
    size_t current_count = workers.size();

    if (new_thread_count > current_count)
    {
        add_threads(new_thread_count - current_count);
    }
    else if (new_thread_count < current_count)
    {
        remove_threads(current_count - new_thread_count);
    }
}

inline void ThreadPool::add_threads(size_t count)
{
    if (stop)
        return;
    for (size_t i = 0; i < count; i++)
    {
        workers.push_back({});
        Worker &worker = workers.back();
        start_worker(worker);
    }
}

inline void ThreadPool::start_worker(Worker &worker)
{
    worker.thread = std::thread([this, &worker]() mutable {
        while (true)
        {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(this->queue_mutex);
                this->condition.wait(lock, [this, &worker]() {
                    return worker.stop_flag || this->stop || !this->tasks.empty();
                });
                if (worker.stop_flag || this->stop)
                    break;
                if (!this->tasks.empty()) {
                    task = std::move(this->tasks.top().task);
                    this->tasks.pop();
                } else {
                    continue;
                }
            }
            task();
        }
    });
}


inline void ThreadPool::remove_threads(size_t count)
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        size_t threads_to_remove = std::min(count, workers.size());

        // 从后向前标记要停止的 worker
        for (size_t i = 0; i < threads_to_remove; ++i) {
            workers[workers.size() - 1 - i].stop_flag = true;
        }
    }
    condition.notify_all();
    wait_for_worker_to_finish();
}

inline size_t ThreadPool::get_thread_count() const
{
    std::lock_guard<std::mutex> lock(queue_mutex);
    return workers.size();
}


inline void ThreadPool::wait_for_worker_to_finish()
{
    // 这个函数必须在没有持有 queue_mutex 的情况下被调用
    std::vector<std::thread> threads_to_join;
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        // 将需要停止的线程移动到临时 vector，并从 a'workers' 中移除
        workers.erase(
            std::remove_if(workers.begin(), workers.end(),
                           [&](Worker &w) {
                               if (w.stop_flag) {
                                   threads_to_join.push_back(std::move(w.thread));
                                   return true; // 返回 true 表示要移除
                               }
                               return false;
                           }),
            workers.end());
    }

    // 在锁外 join 线程
    for (auto &t : threads_to_join) {
        if (t.joinable()) {
            t.join();
        }
    }
}

inline ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;

        for (auto &worker : workers)
        {
            worker.stop_flag = true;
        }
    }

    condition.notify_all();

    for (auto &worker : workers)
    {
        if (worker.thread.joinable())
        {
            worker.thread.join();
        }
    }
}

#endif