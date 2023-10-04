#include <benchmark/benchmark.h>

#include <libfork.hpp>

#include "../util.hpp"
#include "config.hpp"

namespace {

using namespace lf;

using dbl = double;

constexpr async integrate = [](auto integrate, dbl x1, dbl y1, dbl x2, dbl y2, dbl area) -> task<dbl> {
  //
  double half = (x2 - x1) / 2;
  double x0 = x1 + half;
  double y0 = fn(x0);

  double area_x1x0 = (y1 + y0) / 2 * half;
  double area_x0x2 = (y0 + y2) / 2 * half;
  double area_x1x2 = area_x1x0 + area_x0x2;

  if (area_x1x2 - area < epsilon && area - area_x1x2 < epsilon) {
    co_return area_x1x2;
  }

  co_await lf::fork(area_x1x0, integrate)(x1, y1, x0, y0, area_x1x0);
  co_await lf::call(area_x0x2, integrate)(x0, y0, x2, y2, area_x0x2);

  co_await lf::join;

  co_return area_x1x0 + area_x0x2;
};

template <lf::scheduler Sch, lf::numa_strategy Strategy>
void integrate_libfork(benchmark::State &state) {

  Sch sch = [&] {
    if constexpr (std::constructible_from<Sch, int>) {
      return Sch(state.range(0));
    } else {
      return Sch{};
    }
  }();

  volatile double in = n;
  volatile double out;

  for (auto _ : state) {
    out = sync_wait(sch, integrate, 0, fn(0), n, fn(n), 0);
  }

  double expect = integral_fn(0, n);

  if (out - expect < epsilon && expect - out < epsilon) {
    return;
  }

  std::cout << "error: " << out << "!=" << expect << std::endl;
}

} // namespace

using namespace lf;

BENCHMARK(integrate_libfork<lazy_pool, numa_strategy::seq>)->DenseRange(1, num_threads())->UseRealTime();
BENCHMARK(integrate_libfork<busy_pool, numa_strategy::seq>)->DenseRange(1, num_threads())->UseRealTime();
