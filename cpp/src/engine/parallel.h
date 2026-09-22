// A persistent worker pool for CPU-bound, data-parallel loops (building many
// agents' prompt contexts at once, running many independent simulations).
//
// A round's script is inherently sequential -- every `let`, move, and random
// draw depends on the ones before it, and a seed must replay the same run --
// so parallelism lives inside builtins that do independent per-agent work,
// and across whole runs, never between statements.
#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace sl {

class ThreadPool {
 public:
  explicit ThreadPool(unsigned workers = 0) {
    unsigned n = workers ? workers : std::max(1u, std::thread::hardware_concurrency() - 1);
    for (unsigned i = 0; i < n; ++i) threads_.emplace_back([this] { workerLoop(); });
  }
  ~ThreadPool() {
    {
      std::lock_guard<std::mutex> lock(mu_);
      stop_ = true;
    }
    cv_.notify_all();
    for (auto& t : threads_) t.join();
  }

  static ThreadPool& shared() {
    static ThreadPool pool;
    return pool;
  }

  size_t size() const { return threads_.size() + 1; }  // workers + the calling thread

  // Runs fn(i) for i in [0, n), split into chunks across the workers and the
  // calling thread. Blocks until every call finished; rethrows the first
  // exception. Called from inside a worker, it runs inline (no nested
  // parallelism, no deadlock).
  void parallelFor(size_t n, const std::function<void(size_t)>& fn, size_t minChunk = 1) {
    if (n == 0) return;
    if (inWorker_ || threads_.empty() || n <= minChunk) {
      for (size_t i = 0; i < n; ++i) fn(i);
      return;
    }
    std::unique_lock<std::mutex> job(jobMu_);  // one parallel region at a time
    Job j;
    j.fn = &fn;
    j.n = n;
    j.chunk = std::max(minChunk, n / (size() * 4) + 1);
    {
      std::lock_guard<std::mutex> lock(mu_);
      job_ = &j;
      ++generation_;
    }
    cv_.notify_all();
    runChunks(j);
    std::unique_lock<std::mutex> lock(mu_);
    doneCv_.wait(lock, [&] { return j.finishedWorkers == threads_.size(); });
    job_ = nullptr;
    if (j.error) std::rethrow_exception(j.error);
  }

 private:
  struct Job {
    const std::function<void(size_t)>* fn = nullptr;
    size_t n = 0, chunk = 1;
    std::atomic<size_t> next{0};
    size_t finishedWorkers = 0;
    std::exception_ptr error;
    std::mutex errMu;
  };

  static void runChunks(Job& j) {
    for (;;) {
      size_t start = j.next.fetch_add(j.chunk);
      if (start >= j.n) return;
      size_t end = std::min(j.n, start + j.chunk);
      try {
        for (size_t i = start; i < end; ++i) (*j.fn)(i);
      } catch (...) {
        std::lock_guard<std::mutex> lock(j.errMu);
        if (!j.error) j.error = std::current_exception();
        j.next.store(j.n);
      }
    }
  }

  void workerLoop() {
    inWorker_ = true;
    size_t seen = 0;
    for (;;) {
      Job* j;
      {
        std::unique_lock<std::mutex> lock(mu_);
        cv_.wait(lock, [&] { return stop_ || generation_ != seen; });
        if (stop_) return;
        seen = generation_;
        j = job_;
      }
      if (j) runChunks(*j);
      {
        std::lock_guard<std::mutex> lock(mu_);
        if (j) ++j->finishedWorkers;
      }
      doneCv_.notify_all();
    }
  }

  std::vector<std::thread> threads_;
  std::mutex mu_, jobMu_;
  std::condition_variable cv_, doneCv_;
  Job* job_ = nullptr;
  size_t generation_ = 0;
  bool stop_ = false;
  static inline thread_local bool inWorker_ = false;
};

}  // namespace sl
