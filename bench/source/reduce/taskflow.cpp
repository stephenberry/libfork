#include <algorithm>
#include <span>
#include <thread>

#include <benchmark/benchmark.h>

#include <taskflow/taskflow.hpp>

#include "../util.hpp"
#include "config.hpp"

namespace {

// https://taskflow.github.io/taskflow/fibonacci.html

auto reduce(std::span<float> data, std::size_t n, tf::Subflow &sbf) -> float {

  if (data.size() <= n) {
    return std::reduce(data.begin(), data.end());
  }

  auto h = data.size() / 2;
  auto t = data.size() - h;

  float a, b;

  sbf.emplace([&](tf::Subflow &sbf) {
    a = reduce(data.first(h), n, sbf);
  });

  sbf.emplace([&](tf::Subflow &sbf) {
    b = reduce(data.last(t), n, sbf);
  });

  sbf.join();

  return a + b;
}

void reduce_taskflow(benchmark::State &state) {

  std::size_t n = state.range(0);
  std::vector data = to_sum();
  auto grain_size = data.size() / (n * 10);
  tf::Executor executor(n);

  volatile float output;

  tf::Taskflow taskflow;

  taskflow.emplace([&](tf::Subflow &sbf) {
    output = reduce(data, grain_size, sbf);
  });

  for (auto _ : state) {
    executor.run(taskflow).wait();
  }
}

} // namespace

BENCHMARK(reduce_taskflow)->DenseRange(1, num_threads())->UseRealTime();
