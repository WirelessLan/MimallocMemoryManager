#pragma once

#include <cstddef>
#include <mimalloc.h>

namespace Allocator
{
	class ProxyHeap
	{
	public:
		ProxyHeap(const ProxyHeap&) = delete;
		ProxyHeap(ProxyHeap&&) = delete;

		ProxyHeap& operator=(const ProxyHeap&) = delete;
		ProxyHeap& operator=(ProxyHeap&&) = delete;

		[[nodiscard]] static ProxyHeap& get() noexcept
		{
			static ProxyHeap singleton;
			return singleton;
		}

		[[nodiscard]] void* malloc(std::size_t size) noexcept
		{
			return mi_malloc(size);
		}

		[[nodiscard]] void* aligned_alloc(std::size_t alignment, std::size_t size) noexcept
		{
			return mi_malloc_aligned(size, alignment);
		}

		[[nodiscard]] void* realloc(void* ptr, std::size_t newSize) noexcept
		{
			return mi_realloc(ptr, newSize);
		}

		[[nodiscard]] void* aligned_realloc(std::size_t alignment, void* ptr, std::size_t newSize) noexcept
		{
			return mi_realloc_aligned(ptr, newSize, alignment);
		}

		void free(void* ptr) noexcept
		{
			mi_free(ptr);
		}

		void aligned_free(void* ptr) noexcept
		{
			mi_free(ptr);
		}

	private:
		ProxyHeap() noexcept = default;
		~ProxyHeap() noexcept = default;
	};

	class GameHeap
	{
	public:
		GameHeap() = default;
		GameHeap(const GameHeap&) = delete;
		GameHeap(GameHeap&&) = delete;
		~GameHeap() = default;
		GameHeap& operator=(const GameHeap&) = delete;
		GameHeap& operator=(GameHeap&&) = delete;

		[[nodiscard]] void* malloc(std::size_t a_size) { return _allocate(_proxy, a_size, 0x0, false); }
		[[nodiscard]] void* aligned_alloc(std::size_t a_alignment, std::size_t a_size) { return _allocate(_proxy, a_size, static_cast<std::uint32_t>(a_alignment), true); }

		[[nodiscard]] void* realloc(void* a_ptr, std::size_t a_newSize) { return _reallocate(_proxy, a_ptr, a_newSize, 0x0, false); }
		[[nodiscard]] void* aligned_realloc(std::size_t a_alignment, void* a_ptr, std::size_t a_newSize) { return _reallocate(_proxy, a_ptr, a_newSize, static_cast<std::uint32_t>(a_alignment), true); }

		void free(void* a_ptr) { _deallocate(_proxy, a_ptr, false); }
		void aligned_free(void* a_ptr) { _deallocate(_proxy, a_ptr, true); }

	private:
		using Allocate_t = void*(RE::MemoryManager&, std::size_t, std::uint32_t, bool);
		using Deallocate_t = void(RE::MemoryManager&, void*, bool);
		using Reallocate_t = void*(RE::MemoryManager&, void*, std::size_t, std::uint32_t, bool);

		RE::MemoryManager& _proxy{ RE::MemoryManager::GetSingleton() };
		Allocate_t* const _allocate{ reinterpret_cast<Allocate_t*>(REL::ID(652767).address()) };
		Deallocate_t* const _deallocate{ reinterpret_cast<Deallocate_t*>(REL::ID(1582181).address()) };
		Reallocate_t* const _reallocate{ reinterpret_cast<Reallocate_t*>(REL::ID(1502917).address()) };
	};
}
