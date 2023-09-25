#ifndef CE977DFD_3A46_4443_81E7_243C91B6B416
#define CE977DFD_3A46_4443_81E7_243C91B6B416

#include <thread>

inline auto num_threads() noexcept -> std::size_t { return std::thread::hardware_concurrency() / 2; }

#endif /* CE977DFD_3A46_4443_81E7_243C91B6B416 */