
#include <iostream>

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <iterator>

#include "libfork/algorithm/scan.hpp"
#include "libfork/core.hpp"
#include "libfork/schedule.hpp"

using namespace lf;

namespace {

// for (auto it = ++beg; it != end; ++it) {
//   auto acc = out;
//   ++out;
//   *out = *acc + *it;
// }

// inline constexpr int chunk = 3;

template <typename T>
auto bop(T lhs, T rhs) -> T {
  *rhs = *lhs + *rhs;
  return rhs;
}

template <typename T>
auto scan_up(T beg, T end) {
  switch (auto size = end - beg) {
    case 1:
      return beg; // This is the lhs and rhs child.
    case 2:
      return bop(beg, end - 1); // Returns rhs child.
    default:
      auto mid = beg + size / 2;

      scan_up(beg, mid);
      scan_up(mid, end);

      return bop(mid - 1, end - 1); // Returns rhs child.
  }
}

template <typename T>
void scan_down_l(T beg, T end);

template <typename T>
void scan_down_r(T beg, T end);

/**
 * @brief
 *
 * y0 = x0
 * y1 = x0 + x1
 * y2 = x0 + x1+ x2
 *
 */
template <typename T>
auto scan(T beg, T end) {
  scan_up(beg, end);

  scan_down_l(beg, end);
}

template <typename T>
void scan_down_l(T beg, T end) {
  switch (auto size = end - beg) {
    case 1:
    case 2:
      return;
    default:
      auto mid = beg + size / 2;

      scan_down_l(beg, mid); // Left recursion
      scan_down_r(mid, end); // Right recursion
  }
}

template <typename T>
void scan_down_r(T beg, T end) {
  switch (auto size = end - beg) {
    case 1:
      return;
    case 2:
      *beg += *(beg - 1);
      return;
    default:
      auto mid = beg + size / 2;

      *(mid - 1) += *(beg - 1);

      scan_down_r(beg, mid); // Left recursion
      scan_down_r(mid, end); // Right recursion
  }
}

} // namespace

TEMPLATE_TEST_CASE("scan", "[algorithm][template]", unit_pool /*, busy_pool, lazy_pool*/) {

  for (int n = 1; n <= 15; n++) {

    std::vector<int> v(n, 1);
    std::vector<int> out;

    scan(v.begin(), v.end());

    for (auto &&elem : out) {
      std::cout << elem << " ";
    }

    // std::cout << std::endl;

    for (int i = 0; i < v.size(); i++) {
      CHECK(v[i] == i + 1);
    }
  }

  // for (int n = 1; n < 10; n++) {

  //   std::vector<int> v(n, 1);
  //   std::vector<int> out(v.size());

  //   scan<4>(v.begin(), v.end(), out.begin());

  //   for (auto &&elem : out) {
  //     std::cout << elem << " ";
  //   }

  //   std::cout << std::endl;

  //   for (int i = 0; i < v.size(); i++) {
  //     REQUIRE(out[i] == i + 1);
  //   }
  // }
}