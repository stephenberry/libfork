#ifndef A5349E86_5BAA_48EF_94E9_F0EBF630DE04
#define A5349E86_5BAA_48EF_94E9_F0EBF630DE04

#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>

#include <libfork/core/eventually.hpp>
#include <libfork/core/first_arg.hpp>
#include <libfork/core/task.hpp>

#include <libfork/core/impl/frame.hpp>
#include <libfork/core/impl/utility.hpp>

namespace lf {

namespace impl {

/**
 * @brief A type which can be assigned any value as a noop.
 *
 * Useful to ignore a value tagged with ``[[no_discard]]``.
 */
struct ignore_t {
  constexpr void operator=([[maybe_unused]] auto const &discard) const noexcept {}
};

/**
 * @brief A tag type to indicate an async function's return value will be discarded by the caller.
 *
 * This type is indirectly writable from any value.
 */
struct discard_t {
  constexpr auto operator*() -> ignore_t { return {}; }
};

// ------------ Bare-bones inconsistent invocable ------------ //

template <typename I, typename Task>
struct valid_return : std::false_type {};

template <>
struct valid_return<discard_t, task<void>> : std::true_type {};

template <typename R, std::indirectly_writable<R> I>
struct valid_return<I, task<R>> : std::true_type {};

template <typename I, typename R>
concept return_address_for =         //
    quasi_pointer<I> &&              //
    returnable<R> &&                 //
    valid_return<I, task<R>>::value; //

/**
 * @brief Verify `F` is async `Tag` invocable with `Args...` and returns a task who's result type is
 * returnable via I.
 */
template <typename I, tag Tag, typename F, typename... Args>
concept async_invocable_to_task =
    quasi_pointer<I> &&                                                                             //
    async_function_object<F> &&                                                                     //
    std::invocable<F, impl::first_arg_t<I, Tag, F>, Args...> &&                                     //
    valid_return<I, std::invoke_result_t<F, impl::first_arg_t<discard_t, Tag, F>, Args...>>::value; //

template <typename I, tag Tag, typename F, typename... Args>
  requires async_invocable_to_task<I, Tag, F, Args...>
using unsafe_result_t = std::invoke_result_t<F, impl::first_arg_t<I, Tag, F>, Args...>::type;

// --------------------- //

/**
 * @brief Check that I1, I2 and I3 are the same type, invariant under permutations.
 */
template <typename I1, typename I2, typename I3>
concept same_as = std::same_as<I1, I2> && std::same_as<I2, I3> && std::same_as<I3, I1>;

/**
 * @brief Check that F can be 'Tag'-invoked with I1, I2, I3 and all calls produce the same result.
 *
 * Symmetric under all permutations of I1, I2 and I3.
 */
template <typename I1, typename I2, typename I3, tag Tag, typename F, typename... Args>
concept return_consistent =                         //
    async_invocable_to_task<I1, Tag, F, Args...> && //
    async_invocable_to_task<I2, Tag, F, Args...> && //
    async_invocable_to_task<I3, Tag, F, Args...> && //
    same_as<                                        //
        unsafe_result_t<I1, Tag, F, Args...>,       //
        unsafe_result_t<I2, Tag, F, Args...>,       //
        unsafe_result_t<I3, Tag, F, Args...>        //
        >;

/**
 * @brief Check that F can be async-invoked with any combination of IA, IB, T1, T2 and all calls produce
 * the same result.
 *
 * Symmetric in permutations of I's and T's.
 */
template <typename IA, typename IB, typename IC, tag T1, tag T2, typename F, typename... Args>
concept consistent =                                 //
    return_consistent<IA, IB, IC, T1, F, Args...> && //
    return_consistent<IA, IB, IC, T2, F, Args...> && //
    std::same_as<                                    //
        unsafe_result_t<IA, T1, F, Args...>,         //
        unsafe_result_t<IA, T2, F, Args...>          //
        > &&                                         //
    std::same_as<                                    //
        unsafe_result_t<IB, T1, F, Args...>,         //
        unsafe_result_t<IB, T2, F, Args...>          //
        > &&                                         //
    std::same_as<                                    //
        unsafe_result_t<IC, T1, F, Args...>,         //
        unsafe_result_t<IC, T2, F, Args...>          //
        >;

// --------------------- //

template <typename R>
struct as_eventually : std::type_identity<eventually<R> *> {};

template <>
struct as_eventually<void> : std::type_identity<discard_t> {};

template <typename I, tag Tag, typename F, typename... Args>
  requires async_invocable_to_task<I, Tag, F, Args...>
using as_eventually_t = as_eventually<impl::unsafe_result_t<I, Tag, F, Args...>>::type;

template <typename I, tag Tag, typename F, typename... Args>
concept consistent_invocable =                                                                 //
    async_invocable_to_task<I, Tag, F, Args...> &&                                             //
    consistent<I, discard_t, as_eventually_t<I, Tag, F, Args...>, tag::call, Tag, F, Args...>; //

// --------------------- //

} // namespace impl

/**
 * @brief Check `F` is `Tag`-invocable with `Args...` and returns a task who's result is returnable via `I`.
 *
 * In the following description "invoking" or "async invoking" means to call `F` with `Args...` via the
 * appropriate libfork function i.e. `fork` corresponds to `lf::fork[r, f](args...)` and the library will
 * generate the appropriate (opaque) first-argument.
 *
 * This requires:
 *  - `F` is 'Tag'/call invocable with `Args...` when writing the result to `I` or discarding it.
 *  - The result of all of these calls has the same type.
 *  - The result of all of these calls is an instance of type `lf::task<R>`.
 *  - `I` is movable and dereferenceable.
 *  - `I` is indirectly writable from `R` or `R` is `void` while `I` is `discard_t`.
 *  - If `R` is non-void then `F` is `async_invocable` when `I` is `eventually<R> *`.
 *
 * This concept is provided as a building block for higher-level concepts.
 */
template <typename I, tag Tag, typename F, typename... Args>
concept async_invocable = impl::consistent_invocable<I, Tag, F, Args...>;

// --------- //

template <typename F, typename... Args>
concept invocable = async_invocable<impl::discard_t, tag::call, F, Args...>;

template <typename F, typename... Args>
concept rootable = invocable<F, Args...> && async_invocable<impl::discard_t, tag::root, F, Args...>;

template <typename F, typename... Args>
concept forkable = invocable<F, Args...> && async_invocable<impl::discard_t, tag::fork, F, Args...>;

// --------- //

template <typename F, typename... Args>
  requires invocable<F, Args...>
using async_result_t = impl::unsafe_result_t<impl::discard_t, tag::call, F, Args...>;

} // namespace lf

#endif /* A5349E86_5BAA_48EF_94E9_F0EBF630DE04 */