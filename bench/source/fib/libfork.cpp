#include <benchmark/benchmark.h>

#include <libfork.hpp>

#include "../util.hpp"
#include "config.hpp"

namespace {

inline constexpr lf::async fib = [](auto fib, int n) LF_STATIC_CALL -> lf::task<int> {
  if (n < 2) {
    co_return n;
  }

  int a, b;

  co_await lf::fork(a, fib)(n - 1);
  co_await lf::call(b, fib)(n - 2);

  co_await lf::join;

  co_return a + b;
};

template <lf::scheduler Sch, lf::numa_strategy Strategy>
void fib_libfork(benchmark::State &state) {

  Sch sch = [&] {
    if constexpr (std::constructible_from<Sch, int>) {
      return Sch(state.range(0));
    } else {
      return Sch{};
    }
  }();

  volatile int secret = work;
  volatile int output;

  for (auto _ : state) {
    output = lf::sync_wait(sch, fib, secret);
  }

  if (output != sfib(work)) {
    std::cout << "error" << std::endl;
  }
}

} // namespace

using namespace lf;

// BENCHMARK(fib_libfork<unit_pool, numa_strategy::seq>)->DenseRange(1, 1)->UseRealTime();
// BENCHMARK(fib_libfork<debug_pool, numa_strategy::seq>)->DenseRange(1, 1)->UseRealTime();

BENCHMARK(fib_libfork<lazy_pool, numa_strategy::seq>)->DenseRange(1, num_threads())->UseRealTime();
// BENCHMARK(fib_libfork<lazy_pool, numa_strategy::fan>)->DenseRange(1, num_threads())->UseRealTime();

BENCHMARK(fib_libfork<busy_pool, numa_strategy::seq>)->DenseRange(1, num_threads())->UseRealTime();
// BENCHMARK(fib_libfork<busy_pool, numa_strategy::fan>)->DenseRange(1, num_threads())->UseRealTime();