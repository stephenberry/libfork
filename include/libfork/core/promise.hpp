#ifndef FF9F3B2C_DC2B_44D2_A3C2_6E40F211C5B0
#define FF9F3B2C_DC2B_44D2_A3C2_6E40F211C5B0

// Copyright © Conor Williams <conorwilliams@outlook.com>

// SPDX-License-Identifier: MPL-2.0

// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <atomic>
#include <concepts>
#include <type_traits>
#include <utility>

#include "libfork/macro.hpp"
#include "libfork/utility.hpp"

#include "libfork/core/async.hpp"
#include "libfork/core/call.hpp"
#include "libfork/core/coroutine.hpp"
#include "libfork/core/meta.hpp"
#include "libfork/core/result.hpp"
#include "libfork/core/stack.hpp"

/**
 * @file promise.hpp
 *
 * @brief The `promise_type` for tasks.
 */

namespace lf::impl {

namespace detail {

// -------------------------------------------------------------------------- //

template <thread_context Context>
struct switch_awaitable {

  auto await_ready() const noexcept { return tls::get_ctx<Context>() == non_null(dest); }

  void await_suspend(stdx::coroutine_handle<>) noexcept { non_null(dest)->submit(&self); }

  void await_resume() const noexcept {}

  intruded_h<Context> self;
  Context *dest;
};

// -------------------------------------------------------------------------- //

template <thread_context Context>
struct fork_awaitable : stdx::suspend_always {
  auto await_suspend(stdx::coroutine_handle<>) const noexcept -> stdx::coroutine_handle<> {
    LF_LOG("Forking, push parent to context");
    m_parent->debug_inc();
    // Need it here (on real stack) in case *this is destructed after push.
    stdx::coroutine_handle child = m_child->coro();
    tls::get_ctx<Context>()->task_push(std::bit_cast<task_h<Context> *>(m_parent));
    return child;
  }
  frame_block *m_parent;
  frame_block *m_child;
};

// -------------------------------------------------------------------------- //

struct call_awaitable : stdx::suspend_always {
  auto await_suspend(stdx::coroutine_handle<>) const noexcept -> stdx::coroutine_handle<> {
    LF_LOG("Calling");
    return m_child->coro();
  }
  frame_block *m_child;
};

// -------------------------------------------------------------------------- //

template <thread_context Context, repackable Packet>
struct invoke_awaitable : stdx::suspend_always {
 private:
  using repack = repack_t<Packet>;

 public:
  auto await_suspend(stdx::coroutine_handle<>) noexcept -> stdx::coroutine_handle<> {

    LF_LOG("Invoking");

    repack new_packet = std::move(m_packet).apply([&](auto, auto &&...args) -> repack {
      return {{m_res}, std::forward<decltype(args)>(args)...};
    });

    return std::move(new_packet).template patch_with<Context>().invoke(parent)->coro();
  }

  [[nodiscard]] auto await_resume() -> value_of<repack> { return *std::move(m_res); }

  frame_block *parent;
  Packet m_packet;
  eventually<value_of<repack>> m_res;
};

// -------------------------------------------------------------------------- //

template <thread_context Context, bool IsRoot>
struct join_awaitable {
 private:
  void take_stack_reset_control() const noexcept {
    // Steals have happened so we cannot currently own this tasks stack.
    LF_ASSERT(self->steals() != 0);

    if constexpr (!IsRoot) {
      tls::push_asp<Context>(self->top());
    }
    // Some steals have happened, need to reset the control block.
    self->reset();
  }

 public:
  auto await_ready() const noexcept -> bool {
    // If no steals then we are the only owner of the parent and we are ready to join.
    if (self->steals() == 0) {
      LF_LOG("Sync ready (no steals)");
      // Therefore no need to reset the control block.
      return true;
    }
    // Currently:            joins() = k_u32_max - num_joined
    // Hence:       k_u32_max - joins() = num_joined

    // Could use (relaxed) + (fence(acquire) in truthy branch) but, it's
    // better if we see all the decrements to joins() and avoid suspending
    // the coroutine if possible. Cannot fetch_sub() here and write to frame
    // as coroutine must be suspended first.
    auto joined = k_u32_max - self->load_joins(std::memory_order_acquire);

    if (self->steals() == joined) {
      LF_LOG("Sync is ready");
      take_stack_reset_control();
      return true;
    }

    LF_LOG("Sync not ready");
    return false;
  }

  auto await_suspend(stdx::coroutine_handle<> task) const noexcept -> stdx::coroutine_handle<> {
    // Currently        joins  = k_u32_max  - num_joined
    // We set           joins  = joins()    - (k_u32_max - num_steals)
    //                         = num_steals - num_joined

    // Hence               joined = k_u32_max - num_joined
    //         k_u32_max - joined = num_joined

    auto steals = self->steals();
    auto joined = self->fetch_sub_joins(k_u32_max - steals, std::memory_order_release);

    if (steals == k_u32_max - joined) {
      // We set joins after all children had completed therefore we can resume the task.
      // Need to acquire to ensure we see all writes by other threads to the result.
      std::atomic_thread_fence(std::memory_order_acquire);
      LF_LOG("Wins join race");
      take_stack_reset_control();
      return task;
    }
    LF_LOG("Looses join race");
    // Someone else is responsible for running this task and we have run out of work.
    // We cannot touch *this or deference self as someone may have resumed already!
    // We cannot currently own this stack (checking would violate above).
    return stdx::noop_coroutine();
  }

  void await_resume() const noexcept {
    LF_LOG("join resumes");
    // Check we have been reset.
    LF_ASSERT(self->steals() == 0);
    LF_ASSERT(self->load_joins(std::memory_order_relaxed) == k_u32_max);

    self->debug_reset();

    if constexpr (!IsRoot) {
      LF_ASSERT(self->top() == tls::get_asp());
    }
  }

  frame_block *self;
};

// -------------------------------------------------------------------------- //

template <thread_context Context>
auto final_await_suspend(frame_block *parent) noexcept -> std::coroutine_handle<> {

  Context *context = non_null(tls::get_ctx<Context>());

  if (task_h<Context> *parent_task = context->task_pop()) {
    // No-one stole continuation, we are the exclusive owner of parent, just keep ripping!
    LF_LOG("Parent not stolen, keeps ripping");
    LF_ASSERT(byte_cast(parent_task) == byte_cast(parent));
    // This must be the same thread that created the parent so it already owns the stack.
    // No steals have occurred so we do not need to call reset().;
    return parent->coro();
  }

  // We are either: the thread that created the parent or a thread that completed a forked task.

  // Note: emptying stack implies finished a stolen task or finished a task forked from root.

  // Cases:
  // 1. We are fork_from_root_t
  //    - Every task forked from root is the the first task on a stack -> stack is empty now.
  //      Parent (root) is not on a stack so do not need to take/release control
  // 2. We are fork_t
  //    - Stack is empty -> we cannot be the thread that created the parent as it would be on our stack.
  //    - Stack is non-empty -> we must be the creator of the parent

  // If we created the parent then our current stack is non empty (unless the parent is a root task).
  // If we did not create the parent then we just cleared our current stack and it is now empty.

  LF_LOG("Task's parent was stolen");

  // Need to copy these onto stack for else branch later.
  auto [is_root, top] = parent->locale();

  // Register with parent we have completed this child task.
  // If we are not the last we cannot deference the parent pointer as frame may now be freed.
  if (parent->fetch_sub_joins(1, std::memory_order_release) == 1) {
    // Acquire all writes before resuming.
    std::atomic_thread_fence(std::memory_order_acquire);

    // Parent has reached join and we are the last child task to complete.
    // We are the exclusive owner of the parent therefore, we must continue parent.

    LF_LOG("Task is last child to join, resumes parent");

    if (!is_root) [[likely]] {
      if (top != tls::get_asp()) {
        tls::push_asp<Context>(top);
      }
    }

    // Must reset parents control block before resuming parent.
    parent->reset();

    return parent->coro();
  }

  // Parent has not reached join or we are not the last child to complete.
  // We are now out of jobs, must yield to executor.

  LF_LOG("Task is not last to join");

  if (!is_root) [[likely]] {
    if (top == tls::get_asp()) {
      // We are unable to resume the parent, as the resuming thread will take
      // ownership of the parent's stack we must give it up.
      LF_LOG("Thread releases control of parent's stack");

      tls::set_asp(stack_as_bytes(context->stack_pop()));
    }
  }

  return stdx::noop_coroutine();
}

// -------------------------------------------------------------------------- //

} // namespace detail

/**
 * @brief Use to change a `first_arg` s tag to `tag::call`.
 */
template <first_arg Head>
struct rewrite_tag : Head {
  static constexpr tag tag_value = tag::call; ///< The tag value.
};

/**
 * @brief Selects the allocator used by `promise_type` depending on tag.
 */
template <tag Tag>
using allocator = std::conditional_t<Tag == tag::root, promise_alloc_heap, promise_alloc_stack>;

template <typename P>
LF_NOINLINE auto destroy(stdx::coroutine_handle<P> child) -> frame_block * {
  frame_block *parent = child.promise().parent();
  child.destroy();
  return parent;
}

/**
 * @brief The promise type for all tasks/coroutines.
 *
 * @tparam R The type of the return address.
 * @tparam T The value type of the coroutine (what it promises to return).
 * @tparam Context The type of the context this coroutine is running on.
 * @tparam Tag The dispatch tag of the coroutine.
 */
template <typename R, typename T, thread_context Context, tag Tag>
struct promise_type : allocator<Tag>, promise_result<R, T> {
 private:
  static_assert(Tag == tag::fork || Tag == tag::call || Tag == tag::root);
  static_assert(Tag != tag::root || is_root_result_v<R>);

  struct final_awaitable : stdx::suspend_always {
    static auto await_suspend(stdx::coroutine_handle<promise_type> child) noexcept -> stdx::coroutine_handle<> {

      if constexpr (Tag == tag::root) {
        LF_LOG("Root task at final suspend, releases semaphore");
        // Finishing a root task implies our stack is empty and should have no exceptions.
        child.promise().address()->semaphore.release();
        destroy(child);
        LF_LOG("Root task yields to executor");
        return stdx::noop_coroutine();
      }

      LF_LOG("Task reaches final suspend");

      frame_block *parent = destroy(child);

      if constexpr (Tag == tag::call) {
        LF_LOG("Inline task resumes parent");
        // Inline task's parent cannot have been stolen as its continuation was not pushed to a queue,
        // Hence, no need to reset control block.

        // We do not attempt to push_asp the stack because stack eats only occur at a sync point.
        return parent->coro();
      }
      return detail::final_await_suspend<Context>(parent);
    }
  };

 public:
  /**
   * @brief Construct promise with void return type.
   */
  promise_type() noexcept : allocator<Tag>(stdx::coroutine_handle<promise_type>::from_promise(*this)) {}

  /**
   * @brief Construct promise, sets return address.
   */
  template <first_arg Head, typename... Tail>
  explicit promise_type(Head const &head, Tail const &...)
    requires std::constructible_from<promise_result<R, T>, R *>
      : allocator<Tag>{stdx::coroutine_handle<promise_type>::from_promise(*this)},
        promise_result<R, T>{head.address()} {}

  /**
   * @brief Construct promise, sets return address.
   *
   * For member function coroutines.
   */
  template <not_first_arg Self, first_arg Head, typename... Tail>
  explicit promise_type(Self const &, Head const &head, Tail const &...tail)
    requires std::constructible_from<promise_result<R, T>, R *>
      : promise_type{head, tail...} {}

  auto get_return_object() noexcept -> frame_block * { return this; }

  static auto initial_suspend() noexcept -> stdx::suspend_always { return {}; }

  /**
   * @brief Terminates the program.
   */
  static void unhandled_exception() noexcept {
    noexcept_invoke([] {
      LF_RETHROW;
    });
  }

  /**
   * @brief Try to resume the parent.
   */
  auto final_suspend() const noexcept -> final_awaitable {

    LF_LOG("At final suspend call");

    // Completing a non-root task means we currently own the async_stack this child is on

    LF_ASSERT(this->debug_count() == 0);
    LF_ASSERT(this->steals() == 0);                                      // Fork without join.
    LF_ASSERT(this->load_joins(std::memory_order_relaxed) == k_u32_max); // Destroyed in invalid state.

    return final_awaitable{};
  }

  /**
   * @brief Transform a context pointer into a context switch awaitable.
   */
  auto await_transform(Context *dest) -> detail::switch_awaitable<Context> {

    auto *fb = static_cast<frame_block *>(this);
    auto *sh = std::bit_cast<submit_h<Context> *>(fb);

    return {intruded_h<Context>{sh}, dest};
  }

  /**
   * @brief Transform a fork packet into a fork awaitable.
   */
  template <first_arg_tagged<tag::fork> Head, typename... Args>
  constexpr auto await_transform(packet<Head, Args...> &&packet) noexcept -> detail::fork_awaitable<Context> {
    return {{}, this, std::move(packet).template patch_with<Context>().invoke(this)};
  }

  /**
   * @brief Transform a call packet into a call awaitable.
   */
  template <first_arg_tagged<tag::call> Head, typename... Args>
  constexpr auto await_transform(packet<Head, Args...> &&packet) noexcept -> detail::call_awaitable {
    return {{}, std::move(packet).template patch_with<Context>().invoke(this)};
  }

  /**
   * @brief Transform a fork packet into a call awaitable.
   *
   * This subsumes the above `await_transform()` for forked packets if `Context::max_threads() == 1` is true.
   */
  template <first_arg_tagged<tag::fork> Head, typename... Args>
    requires single_thread_context<Context> && valid_packet<rewrite_tag<Head>, Args...>
  constexpr auto await_transform(packet<Head, Args...> &&pack) noexcept -> detail::call_awaitable {
    return await_transform(std::move(pack).apply([](Head head, Args &&...args) -> packet<rewrite_tag<Head>, Args...> {
      return {{std::move(head)}, std::forward<Args>(args)...};
    }));
  }

  /**
   * @brief Get a join awaitable.
   */
  constexpr auto await_transform(join_type) noexcept -> detail::join_awaitable<Context, Tag == tag::root> { return {this}; }

  /**
   * @brief Transform an invoke packet into an invoke_awaitable.
   */
  template <impl::non_reference Packet>
  constexpr auto await_transform(Packet &&pack) noexcept -> detail::invoke_awaitable<Context, Packet> {
    return {{}, this, std::forward<Packet>(pack), {}};
  }
};

// -------------------------------------------------------------------------- //

/**
 * @brief Disable rvalue references for T&& template types if an async function is forked.
 *
 * This is to prevent the user from accidentally passing a temporary object to
 * an async function that will then destructed in the parent task before the
 * child task returns.
 */
template <typename T, tag Tag>
concept no_dangling = Tag != tag::fork || !std::is_rvalue_reference_v<T>;

namespace detail {

template <first_arg Head, is_task Task>
using promise_for = impl::promise_type<return_of<Head>, value_of<Task>, context_of<Head>, tag_of<Head>>;

} // namespace detail

} // namespace lf::impl

/**
 * @brief Specialize coroutine_traits for task<...> from functions.
 */
template <lf::impl::is_task Task, lf::first_arg Head, lf::impl::no_dangling<lf::tag_of<Head>>... Args>
struct lf::stdx::coroutine_traits<Task, Head, Args...> {
  using promise_type = lf::impl::detail::promise_for<Head, Task>;
};

/**
 * @brief Specialize coroutine_traits for task<...> from member functions.
 */
template <lf::impl::is_task Task, lf::impl::not_first_arg This, lf::first_arg Head,
          lf::impl::no_dangling<lf::tag_of<Head>>... Args>
struct lf::stdx::coroutine_traits<Task, This, Head, Args...> : lf::stdx::coroutine_traits<Task, Head, Args...> {};

#endif /* FF9F3B2C_DC2B_44D2_A3C2_6E40F211C5B0 */
