#pragma once

#include <cstddef>

namespace nekomote::test::abi_allocations
{
// Enabled only around checked ABI operations on the calling thread. Keep the
// replacement allocation operators in a separate translation unit: GCC's
// per-unit mismatch analysis otherwise sees an inlined free at valid C++ delete
// call sites and reports a false mismatched-deallocation warning.
extern thread_local bool track_allocations;
extern thread_local bool fail_allocations;
extern thread_local std::size_t allocations;
} // namespace nekomote::test::abi_allocations
