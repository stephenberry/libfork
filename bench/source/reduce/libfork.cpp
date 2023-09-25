#include <algorithm>
#include <span>
#include <thread>

#include <benchmark/benchmark.h>

#include <libfork.hpp>

#include "../util.hpp"
#include "config.hpp"

namespace {

inline constexpr lf::async reduce = [](auto reduce, std::span<float> data, std::size_t n) -> lf::task<float> {
  if (data.size() <= n) {
    co_return std::reduce(data.begin(), data.end());
  }

  auto h = data.size() / 2;
  auto t = data.size() - h;

  float a, b;

  co_await lf::fork(a, reduce)(data.first(h), n);
  co_await lf::call(b, reduce)(data.last(t), n);

  co_await lf::join;

  co_return a + b;
};

template <lf::scheduler Sch>
void reduce_libfork(benchmark::State &state) {

  std::size_t n = state.range(0);
  std::vector data = to_sum();
  auto grain_size = data.size() / (n * 10);
  Sch sch(n);

  volatile float output;

  for (auto _ : state) {
    output = lf::sync_wait(sch, reduce, data, grain_size);
  }
}

} // namespace

BENCHMARK(reduce_libfork<lf::lazy_pool>)->DenseRange(1, num_threads())->UseRealTime();

BENCHMARK(reduce_libfork<lf::busy_pool>)->DenseRange(1, num_threads())->UseRealTime();