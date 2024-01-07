#ifndef DD0B4328_55BD_452B_A4A5_5A4670A6217B
#define DD0B4328_55BD_452B_A4A5_5A4670A6217B

// Copyright © Conor Williams <conorwilliams@outlook.com>

// SPDX-License-Identifier: MPL-2.0

// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>

#include "libfork/core/tag.hpp"

#include "libfork/core/ext/context.hpp"
#include "libfork/core/ext/tls.hpp"

#include "libfork/core/impl/utility.hpp"

/**
 * @file first_arg.hpp
 *
 * @brief Machinery for the (library-generated) first argument of async functions.
 */

namespace lf {

inline namespace core {

/**
 * @brief Test if the expression `*std::declval<T&>()` is valid and has a referenceable type i.e. non-void.
 */
template <typename I>
concept dereferenceable = requires (I val) {
  { *val } -> impl::can_reference;
};

/**
 * @brief A quasi-pointer if a movable type that can be dereferenced to a referenceable type type i.e.
 * non-void.
 *
 * A quasi-pointer is assumed to be cheap-to-move like an iterator/legacy-pointer.
 */
template <typename I>
concept quasi_pointer = std::default_initializable<I> && std::movable<I> && dereferenceable<I>;

/**
 * @brief A concept that requires a type be a copyable [function
 * object](https://en.cppreference.com/w/cpp/named_req/FunctionObject).
 *
 * An async function object is a function object that returns an `lf::task` when `operator()` is called.
 * with appropriate arguments. The call to `operator()` must create a libfork coroutine. The first argument
 * of an async function must accept a deduced templated-type that satisfies the `lf::core::first_arg` concept.
 * The return type and invocability of an async function must be independent of the first argument except
 * for its tag value.
 *
 * An async function may be copied, its copies must be equivalent to the original and support concurrent
 * invocation from multiple threads. It is assumed that an async function is cheap-to-copy like
 * an iterator/legacy-pointer.
 */
template <typename F>
concept async_function_object = std::is_object_v<F> && std::copy_constructible<F>;

/**
 * @brief This describes the public-API of the first argument passed to an async function.
 *
 * An async functions' invocability and return type must be independent of their first argument except for its
 * tag value. A user may query the first argument's static member `tagged` to obtain this value. Additionally,
 * a user may query the first argument's static member function `context()` to obtain a pointer to the current
 * workers `lf::context`. Finally a user may cache an exception in-flight by calling `.stash_exception()`.
 */
template <typename T>
concept first_arg = async_function_object<T> && requires (T arg) {
  { T::tagged } -> std::convertible_to<tag>;
  { T::context() } -> std::same_as<context *>;
  { arg.stash_exception() } noexcept;
};

} // namespace core

namespace impl {

/**
 * @brief The type passed as the first argument to async functions.
 *
 * Its functions are:
 *
 * - Act as a y-combinator (expose same invocability as F).
 * - Provide a handle to the coroutine frame for exception handling.
 * - Statically inform the return pointer type.
 * - Statically provide the tag.
 * - Statically provide the calling argument types.
 */
template <quasi_pointer I, tag Tag, async_function_object F, typename... Cargs>
class first_arg_t {
 public:
  static constexpr tag tagged = Tag; ///< The way this async function was called.

  first_arg_t() = default;

  static auto context() -> context * { return tls::context(); }

  /**
   * @brief Stash an exception that will be rethrown at the end of the next join.
   */
  void stash_exception() const noexcept {
#if LF_COMPILER_EXCEPTIONS
    m_frame->capture_exception();
#endif
  }

  template <different_from<first_arg_t> T>
    requires std::constructible_from<F, T>
  explicit first_arg_t(T &&expr) noexcept(std::is_nothrow_constructible_v<F, T>)
      : m_fun(std::forward<T>(expr)) {}

  template <typename... Args>
    requires std::invocable<F &, Args...>
  auto operator()(Args &&...args) & noexcept(std::is_nothrow_invocable_v<F &, Args...>)
      -> std::invoke_result_t<F &, Args...> {
    return std::invoke(m_fun, std::forward<Args>(args)...);
  }

  template <typename... Args>
    requires std::invocable<F const &, Args...>
  auto operator()(Args &&...args) const & noexcept(std::is_nothrow_invocable_v<F &, Args...>)
      -> std::invoke_result_t<F const &, Args...> {
    return std::invoke(m_fun, std::forward<Args>(args)...);
  }

  template <typename... Args>
    requires std::invocable<F &&, Args...>
  auto operator()(Args &&...args) && noexcept(std::is_nothrow_invocable_v<F &, Args...>)
      -> std::invoke_result_t<F &&, Args...> {
    return std::invoke(std::move(m_fun), std::forward<Args>(args)...);
  }

  template <typename... Args>
    requires std::invocable<F const &&, Args...>
  auto operator()(Args &&...args) const && noexcept(std::is_nothrow_invocable_v<F &, Args...>)
      -> std::invoke_result_t<F const &&, Args...> {
    return std::invoke(std::move(m_fun), std::forward<Args>(args)...);
  }

 private:
  /**
   * @brief Hidden friend reduces discoverability, this is an implementation detail.
   */
  friend auto unwrap(first_arg_t &&arg) noexcept -> F && { return std::move(arg.m_fun); }

  /**
   * @brief Hidden friend reduces discoverability, this is an implementation detail.
   */
  LF_FORCEINLINE friend auto unsafe_set_frame(first_arg_t &arg, frame *frame) noexcept {
#if LF_COMPILER_EXCEPTIONS
    arg.m_frame = frame;
#endif
  }

  [[no_unique_address]] F m_fun;
#if LF_COMPILER_EXCEPTIONS
  frame *m_frame;
#endif
};

} // namespace impl

} // namespace lf

#endif /* DD0B4328_55BD_452B_A4A5_5A4670A6217B */
