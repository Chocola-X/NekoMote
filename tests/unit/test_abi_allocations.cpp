#include "test_abi_allocations.hpp"

#include <cstdlib>
#include <new>

namespace nekomote::test::abi_allocations
{
thread_local bool track_allocations = false;
thread_local bool fail_allocations = false;
thread_local std::size_t allocations = 0;
} // namespace nekomote::test::abi_allocations

void* operator new(std::size_t size)
{
    if (nekomote::test::abi_allocations::fail_allocations)
        throw std::bad_alloc();
    if (nekomote::test::abi_allocations::track_allocations)
        ++nekomote::test::abi_allocations::allocations;
    if (auto* p = std::malloc(size ? size : 1))
        return p;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
    return ::operator new(size);
}

void operator delete(void* p) noexcept
{
    std::free(p);
}

void operator delete[](void* p) noexcept
{
    std::free(p);
}

void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}

void operator delete[](void* p, std::size_t) noexcept
{
    std::free(p);
}
