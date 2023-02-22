
#include <chrono>
#include <string>
#include <thread>

#include <nanobench.h>

// #include <tbb/task_arena.h>
// #include <tbb/task_group.h>

#undef NDEBUG

#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"

std::mutex global_lock;

std::hash<std::thread::id> hasher;

#define DEBUG_TRACKER(message)                                                     \
  do {                                                                             \
    if (!std::is_constant_evaluated()) {                                           \
      global_lock.lock();                                                          \
      spdlog::debug("{:>24} : {}", ::hasher(std::this_thread::get_id()), message); \
      global_lock.unlock();                                                        \
    }                                                                              \
  } while (false)

#define ASSERT(expr, message)                                                            \
  do {                                                                                   \
    if (!(expr)) {                                                                       \
      global_lock.lock();                                                                \
      std::this_thread::sleep_for(std::chrono::seconds(1));                              \
      spdlog::debug("{:>24} : ERROR {}", ::hasher(std::this_thread::get_id()), message); \
      spdlog::dump_backtrace();                                                          \
      std::terminate();                                                                  \
    }                                                                                    \
  } while (0)

#include "libfork/schedule/busy_pool.hpp"
#include "libfork/task.hpp"

namespace {

auto fib(int n) -> int {
  if (n < 2) {
    return n;
  }
  return fib(n - 1) + fib(n - 2);
}

template <lf::context Context>
auto libfork(int n) -> lf::basic_task<int, Context> {
  if (n < 2) {
    co_return n;
  }
  auto a = co_await libfork<Context>(n - 1).fork();
  auto b = co_await libfork<Context>(n - 2);

  co_await lf::join();

  co_return *a + b;
}

auto omp(int n) -> int {
  if (n < 2) {
    return n;
  }

  int a, b;

#pragma omp task untied shared(a)
  a = omp(n - 1);

  b = omp(n - 2);

#pragma omp taskwait

  return a + b;
}

// int fib_tbb(int n) {
//   if (n < 2) {
//     return n;
//   }
//   int x, y;

//   tbb::task_group g;

//   g.run([&] {
//     x = fib_tbb(n - 1);
//   });

//   y = fib_tbb(n - 2);

//   g.wait();

//   return x + y;
// }

}  // namespace

auto benchmark_fib() -> void {
  //
  ankerl::nanobench::Bench bench;

  int volatile fib_number = 20;

  bench.title("Fibonacci");
  bench.unit("fib(" + std::to_string(fib_number) + ")");
  bench.warmup(100);
  bench.relative(true);
  bench.epochs(100);
  bench.minEpochTime(std::chrono::milliseconds(100));
  // bench.minEpochTime(std::chrono::milliseconds(100));
  // bench.maxEpochTime(std::chrono::milliseconds(1000));
  bench.performanceCounters(true);

  lf::busy_pool pool{2};

  spdlog::set_default_logger(spdlog::stderr_color_mt("stderr"));

  spdlog::enable_backtrace(128);

  //
  std::cout << "fib(" << fib_number << ") = " << 1 << std::endl;

  for (std::size_t i = 1; i <= 100'000'000; ++i) {
    //
    DEBUG_TRACKER("\t\ti = " + std::to_string(i));
    int volatile x = pool.schedule(libfork<lf::busy_pool::context>(2));
  }

  std::cout << "fib(" << fib_number << ") = " << 2 << std::endl;

  for (std::size_t i = 1; i <= std::thread::hardware_concurrency(); ++i) {
    //
    lf::busy_pool pool{i};

    bench.run("busy_pool " + std::to_string(i) + " threads", [&] {
      ankerl::nanobench::doNotOptimizeAway(pool.schedule(libfork<lf::busy_pool::context>(fib_number)));
    });
  }

  for (int i = 1; i <= std::thread::hardware_concurrency(); ++i) {
#pragma omp parallel num_threads(i)
#pragma omp single nowait
    {
      bench.run("openMP " + std::to_string(i) + " threads", [&] {
        //
        int x = omp(fib_number);

        ankerl::nanobench::doNotOptimizeAway(x);
      });
    }
  }

  // for (int i = 1; i <= std::thread::hardware_concurrency(); ++i) {
  //   //

  //   tbb::task_arena limited(i);

  //   limited.execute([&] {
  //     bench.run("intel TBB " + std::to_string(i) + " threads", [&] {
  //       ankerl::nanobench::doNotOptimizeAway(fib_tbb(fib_number));
  //     });
  //   });
  // }
}