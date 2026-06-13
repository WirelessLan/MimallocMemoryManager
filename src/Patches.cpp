#include "Patches.hpp"

#include <xbyak/xbyak.h>

#include "Allocator.hpp"
#include "Settings.hpp"

namespace {
	struct asm_patch : Xbyak::CodeGenerator
	{
		asm_patch(std::uintptr_t a_dst)
		{
			mov(rax, a_dst);
			jmp(rax);
		}
	};

	void asm_jump(std::uintptr_t a_from, [[maybe_unused]] std::size_t a_size, std::uintptr_t a_to)
	{
		asm_patch p{ a_to };
		p.ready();
		assert(p.getSize() <= a_size);
		REL::safe_write(
			a_from,
			std::span{ p.getCode<const std::byte*>(), p.getSize() });
	}

	template <std::size_t N, class T>
	void write_thunk_call(std::uintptr_t a_src)
	{
		auto& trampoline = F4SE::GetTrampoline();
		T::func = trampoline.write_call<N>(a_src, T::thunk);
	}
}

namespace Patches
{
	namespace AutoScrapBufferPatch
	{
		namespace
		{
			void CtorLong()
			{
				REL::Relocation<std::uintptr_t> target{ REL::ID(1305199), 0x1D };
				constexpr std::size_t size = 0x15;
				REL::safe_fill(target.address(), REL::NOP, size);
			}

			void CtorShort()
			{
				struct Patch : Xbyak::CodeGenerator
				{
					Patch()
					{
						mov(qword[rcx], 0);
						mov(rax, rcx);
						ret();
					}
				};

				REL::Relocation<std::uintptr_t> target{ REL::ID(1571567) };
				constexpr std::size_t size = 0x1C;
				REL::safe_fill(target.address(), REL::INT3, size);

				Patch p;
				p.ready();
				assert(p.getSize() <= size);
				REL::safe_write(
					target.address(),
					std::span{ p.getCode<const std::byte*>(), p.getSize() });
			}

			void Dtor()
			{
				REL::Relocation<std::uintptr_t> base{ REL::ID(68625) };

				{
					struct Patch : Xbyak::CodeGenerator
					{
						Patch()
						{
							xor_(rax, rax);
							cmp(rbx, rax);
						}
					};

					const auto dst = base.address() + 0x9;
					constexpr std::size_t size = 0x1D;
					REL::safe_fill(dst, REL::NOP, size);

					Patch p;
					p.ready();
					assert(p.getSize() <= size);
					REL::safe_write(
						dst,
						std::span{ p.getCode<const std::byte*>(), p.getSize() });
				}

				{
					const auto dst = base.address() + 0x26;
					REL::safe_write(dst, std::uint8_t{ 0x74 });  // jnz -> jz
				}
			}
		}

		void Install()
		{
			CtorLong();
			CtorShort();
			Dtor();

			logger::info("Installed AutoScrapBuffer patch"sv);
		}
	}

	namespace BSTextureStreamerLocalHeapPatch
	{
		namespace
		{
			void* Allocate(RE::BSTextureStreamer::LocalHeap*, std::uint32_t a_size)
			{
				auto& heap = Allocator::ProxyHeap::get();
				return a_size > 0 ?
				           heap.aligned_alloc(0x10, a_size) :
				           nullptr;
			}

			RE::BSTextureStreamer::LocalHeap* Ctor(RE::BSTextureStreamer::LocalHeap* a_this)
			{
				std::memset(a_this, 0, sizeof(RE::BSTextureStreamer::LocalHeap));
				return a_this;
			}

			void Deallocate(RE::BSTextureStreamer::LocalHeap*, void* a_ptr)
			{
				auto& heap = Allocator::ProxyHeap::get();
				heap.aligned_free(a_ptr);
			}

			void WriteHooks()
			{
				using tuple_t = std::tuple<std::uint64_t, std::size_t, void*>;
				const std::array todo{
					tuple_t{ 790612, 0x1B5, &Allocate },
					tuple_t{ 1493823, 0x74, &Ctor },
					tuple_t{ 1576443, 0x149, &Deallocate },
				};

				for (const auto& [id, size, func] : todo) {
					REL::Relocation<std::uintptr_t> target{ REL::ID(id) };
					REL::safe_fill(target.address(), REL::INT3, size);
					::asm_jump(target.address(), size, reinterpret_cast<std::uintptr_t>(func));
				}
			}

			void WriteStubs()
			{
				using tuple_t = std::tuple<std::uint64_t, std::size_t>;
				const std::array todo{
					tuple_t{ 1100993, 0x7A },  // InsertFreeBlock NG 2275548
					tuple_t{ 318310, 0x11F },  // RemoveFreeBlock
				};

				for (const auto& [id, size] : todo) {
					REL::Relocation<std::uintptr_t> target{ REL::ID(id) };
					REL::safe_fill(target.address(), REL::INT3, size);
					REL::safe_write(target.address(), REL::RET);
				}
			}
		}

		inline void Install()
		{
			WriteStubs();
			WriteHooks();

			logger::info("Installed BSTextureStreamerLocalHeap patch"sv);
		}
	}

	namespace HavokMemorySystemPatch
	{
		namespace
		{
			class hkMemoryAllocator final : public RE::hkMemoryAllocator
			{
			public:
				void* BlockAlloc(std::int32_t a_numBytesIn) override
				{
					return a_numBytesIn > 0 ?
					           _heap.aligned_alloc(0x10, a_numBytesIn) :
					           nullptr;
				}

				void BlockFree(void* a_ptr, std::int32_t) override
				{
					_heap.aligned_free(a_ptr);
				}

				void* BufAlloc(std::int32_t& a_reqNumBytesInOut) override
				{
					return a_reqNumBytesInOut > 0 ?
					           _heap.aligned_alloc(0x10, a_reqNumBytesInOut) :
					           nullptr;
				}

				void BufFree(void* a_ptr, std::int32_t) override
				{
					_heap.aligned_free(a_ptr);
				}

				void* BufRealloc(void* a_old, std::int32_t, std::int32_t& a_reqNumBytesInOut) override
				{
					return _heap.aligned_realloc(0x10, a_old, a_reqNumBytesInOut);
				}

				void BlockAllocBatch(void** a_ptrsOut, std::int32_t a_numPtrs, std::int32_t a_blockSize) override
				{
					std::span range{ a_ptrsOut, static_cast<std::size_t>(a_numPtrs) };
					std::for_each(
						range.begin(),
						range.end(),
						[&](void*& a_elem) {
							a_elem =
								a_blockSize > 0 ?
									_heap.aligned_alloc(0x10, a_blockSize) :
									nullptr;
						});
				}

				void BlockFreeBatch(void** a_ptrsIn, std::int32_t a_numPtrs, std::int32_t) override
				{
					std::span range{ a_ptrsIn, static_cast<std::size_t>(a_numPtrs) };
					std::for_each(
						range.begin(),
						range.end(),
						[&](void* a_elem) {
							_heap.aligned_free(a_elem);
						});
				}

				void GetMemoryStatistics(MemoryStatistics&) const override { return; }
				std::int32_t GetAllocatedSize(const void*, std::int32_t a_numBytes) const override { return a_numBytes; }

			private:
				Allocator::GameHeap _heap;
			};

			class hkMemorySystem final : public RE::hkMemorySystem
			{
			public:
				using FlagBits = RE::hkMemorySystem::FlagBits;

				[[nodiscard]] static hkMemorySystem* GetSingleton()
				{
					static hkMemorySystem singleton;
					return std::addressof(singleton);
				}

				RE::hkMemoryRouter* MainInit(
					const FrameInfo&,
					RE::hkFlags<FlagBits, std::int32_t> a_flags) override
				{
					ThreadInit(_router, "main", a_flags);
					return std::addressof(_router);
				}

				RE::hkResult MainQuit(RE::hkFlags<FlagBits, std::int32_t> a_flags) override
				{
					ThreadQuit(_router, a_flags);
					return { RE::hkResultEnum::kSuccess };
				}

				void ThreadInit(
					RE::hkMemoryRouter& a_router,
					const char*,
					RE::hkFlags<FlagBits, std::int32_t> a_flags) override
				{
					const auto allocator = std::addressof(_allocator);

					if (a_flags & FlagBits::kPersistent) {
						a_router.SetHeap(allocator);
						a_router.SetDebug(allocator);
						a_router.SetTemp(nullptr);
						a_router.SetSolver(nullptr);
					}

					if (a_flags & FlagBits::kTemporary) {
						a_router.GetStack().Init(
							allocator,
							allocator,
							allocator);
						a_router.SetTemp(allocator);
						a_router.SetSolver(allocator);
					}
				}

				void ThreadQuit(
					RE::hkMemoryRouter& a_router,
					RE::hkFlags<FlagBits, std::int32_t> a_flags) override
				{
					if (a_flags & FlagBits::kTemporary) {
						a_router.GetStack().Quit(nullptr);
					}

					if (a_flags & FlagBits::kPersistent) {
						std::memset(
							std::addressof(a_router),
							0,
							sizeof(a_router));
					}

					GarbageCollectThread(a_router);
				}

				virtual void PrintStatistics(RE::hkOstream&) const { return; }

				void GetMemoryStatistics(RE::hkMemorySystem::MemoryStatistics&) { return; }

				RE::hkMemoryAllocator* GetUncachedLockedHeapAllocator() override { return std::addressof(_allocator); }

			private:
				hkMemorySystem() = default;
				hkMemorySystem(const hkMemorySystem&) = delete;
				hkMemorySystem(hkMemorySystem&&) = delete;

				~hkMemorySystem() = default;

				hkMemorySystem& operator=(const hkMemorySystem&) = delete;
				hkMemorySystem& operator=(hkMemorySystem&&) = delete;

#pragma warning(push)
#pragma warning(disable: 4324)
				alignas(0x10) hkMemoryAllocator _allocator;
				alignas(0x10) RE::hkMemoryRouter _router;
#pragma warning(pop)
			};
		}

		void Install()
		{
			auto& trampoline = F4SE::GetTrampoline();
			REL::Relocation<std::uintptr_t> target{ REL::ID(204659), 0x68 };
			trampoline.write_call<5>(target.address(), hkMemorySystem::GetSingleton);

			logger::info("Installed HavokMemorySystem patch"sv);
		}
	}

	namespace MemoryManagerPatch
	{
		namespace
		{
			void* Allocate(RE::MemoryManager*, std::size_t size, std::uint32_t alignment, bool alignmentRequired)
			{
				if (size == 0) {
					return nullptr;
				}

				auto& heap = Allocator::ProxyHeap::get();

				return alignmentRequired ?
				           heap.aligned_alloc(alignment, size) :
				           heap.malloc(size);
			}

			void Deallocate(RE::MemoryManager*, void* mem, bool alignmentRequired)
			{
				auto& heap = Allocator::ProxyHeap::get();

				if (alignmentRequired) {
					heap.aligned_free(mem);
				} else {
					heap.free(mem);
				}
			}

			void* Reallocate(RE::MemoryManager*, void* oldMem, std::size_t newSize, std::uint32_t alignment, bool alignmentRequired)
			{
				auto& heap = Allocator::ProxyHeap::get();

				return alignmentRequired ?
				           heap.aligned_realloc(alignment, oldMem, newSize) :
				           heap.realloc(oldMem, newSize);
			}

			void ReplaceAllocRoutines()
			{
				using tuple_t = std::tuple<std::uint64_t, std::size_t, void*>;
				const std::array todo{
					tuple_t{ 652767, 0x24B, &Allocate },
					tuple_t{ 1582181, 0x115, &Deallocate },
					tuple_t{ 1502917, 0xA2, &Reallocate },
				};

				for (const auto& [id, size, func] : todo) {
					REL::Relocation<std::uintptr_t> target{ REL::ID(id) };
					REL::safe_fill(target.address(), REL::INT3, size);
					::asm_jump(target.address(), size, reinterpret_cast<std::uintptr_t>(func));
				}
			}

			void StubInit()
			{
				REL::Relocation<std::uintptr_t> target{ REL::ID(597736) };
				REL::safe_fill(target.address(), REL::INT3, 0x9C);
				REL::safe_write(target.address(), REL::RET);

				REL::Relocation<std::uint32_t*> initFence{ REL::ID(1570354) };
				*initFence = 2;
			}
		}

		void Install()
		{
			StubInit();
			ReplaceAllocRoutines();

			RE::MemoryManager::GetSingleton().RegisterMemoryManager();
			RE::BSThreadEvent::InitSDM();

			logger::info("Installed MemoryManager patch"sv);
		}
	}

	namespace ScrapHeapPatch
	{
		namespace
		{
			void* Allocate(RE::ScrapHeap*, std::size_t a_size, std::size_t a_alignment)
			{
				auto& heap = Allocator::ProxyHeap::get();
				return a_size > 0 ?
				           heap.aligned_alloc(a_alignment, a_size) :
				           nullptr;
			}

			RE::ScrapHeap* Ctor(RE::ScrapHeap* a_this)
			{
				std::memset(a_this, 0, sizeof(RE::ScrapHeap));
				F4SE::stl::emplace_vtable(a_this);
				return a_this;
			}

			void Deallocate(RE::ScrapHeap*, void* a_mem)
			{
				auto& heap = Allocator::ProxyHeap::get();
				heap.aligned_free(a_mem);
			}

			void WriteHooks()
			{
				using tuple_t = std::tuple<std::uint64_t, std::size_t, void*>;
				const std::array todo{
					tuple_t{ 1085394, 0x5F6, &Allocate },
					tuple_t{ 923307, 0x144, &Deallocate },
					tuple_t{ 48809, 0x12B, &Ctor },
				};

				for (const auto& [id, size, func] : todo) {
					REL::Relocation<std::uintptr_t> target{ REL::ID(id) };
					REL::safe_fill(target.address(), REL::INT3, size);
					::asm_jump(target.address(), size, reinterpret_cast<std::uintptr_t>(func));
				}
			}

			void WriteStubs()
			{
				using tuple_t = std::tuple<std::uint64_t, std::size_t>;
				const std::array todo{
					tuple_t{ 550677, 0xC3 },  // Clean
					tuple_t{ 111657, 0x8 },   // ClearKeepPages
					tuple_t{ 975239, 0xF6 },  // InsertFreeBlock
					tuple_t{ 84225, 0x183 },  // RemoveFreeBlock
					tuple_t{ 1255203, 0x4 },  // SetKeepPages
					tuple_t{ 912706, 0x32 },  // dtor
				};

				for (const auto& [id, size] : todo) {
					REL::Relocation<std::uintptr_t> target{ REL::ID(id) };
					REL::safe_fill(target.address(), REL::INT3, size);
					REL::safe_write(target.address(), REL::RET);
				}
			}

			void WriteHeapSize()
			{
				REL::Relocation<std::uintptr_t> target{ REL::ID(126418), 0x1 };
				REL::safe_write(target.address(), &Settings::MaxScrapHeapSize, sizeof(Settings::MaxScrapHeapSize));
			}
		}

		void Install()
		{
			WriteStubs();
			WriteHooks();
			WriteHeapSize();

			logger::info("Installed ScrapHeap patch"sv);
		}
	}

	namespace ScaleformAllocatorPatch
	{
		namespace {
			class Allocator final : public RE::Scaleform::SysAlloc
			{
			public:
				[[nodiscard]] static Allocator* GetSingleton()
				{
					static Allocator singleton;
					return std::addressof(singleton);
				}

			protected:
				void* Alloc(std::size_t a_size, std::size_t a_align) override
				{
					return a_size > 0 ?
					           _heap.aligned_alloc(a_align, a_size) :
					           nullptr;
				}

				void Free(void* a_ptr, std::size_t, std::size_t) override
				{
					_heap.aligned_free(a_ptr);
				}

				void* Realloc(void* a_oldPtr, std::size_t, std::size_t a_newSize, std::size_t a_align) override
				{
					return _heap.aligned_realloc(a_align, a_oldPtr, a_newSize);
				}

			private:
				using Allocate_t = void*(RE::MemoryManager&, std::size_t, std::uint32_t, bool);
				using Deallocate_t = void(RE::MemoryManager&, void*, bool);
				using Reallocate_t = void*(RE::MemoryManager&, void*, std::size_t, std::uint32_t, bool);

				Allocator() = default;
				Allocator(const Allocator&) = delete;
				Allocator(Allocator&&) = delete;
				~Allocator() = default;
				Allocator& operator=(const Allocator&) = delete;
				Allocator& operator=(Allocator&&) = delete;

				::Allocator::GameHeap _heap;
			};

			struct Init
			{
				static void thunk(const RE::Scaleform::MemoryHeap::HeapDesc& a_rootHeapDesc, RE::Scaleform::SysAllocBase*)
				{
					func(a_rootHeapDesc, Allocator::GetSingleton());
				}

				static inline REL::Relocation<decltype(thunk)> func;
			};

			void WriteHooks()
			{
				REL::Relocation<std::uintptr_t> target{ REL::ID(903830), 0xEC };
				::write_thunk_call<5, Init>(target.address());
			}

			void WriteSizes()
			{
				// GetPageSize
				{
					REL::Relocation<std::uintptr_t> target{ REL::ID(1310500), 0x1 };
					REL::safe_write(target.address(), &Settings::MaxScaleformPageSize, sizeof(Settings::MaxScaleformPageSize));
				}

				// Default PageSize
				{
					REL::Relocation<std::uintptr_t> target{ REL::ID(466425), 0x8B };
					REL::safe_write(target.address(), &Settings::MaxScaleformPageSize, sizeof(Settings::MaxScaleformPageSize));
				}

				// Default HeapSize
				{
					REL::Relocation<std::uintptr_t> target{ REL::ID(466425), 0x91 };
					REL::safe_write(target.address(), &Settings::MaxScaleformHeapSize, sizeof(Settings::MaxScaleformHeapSize));
				}
			}
		}

		void Install()
		{
			WriteHooks();
			WriteSizes();

			logger::info("Installed ScaleformAllocator patch"sv);
		}
	}

	namespace SmallBlockAllocatorPatch
	{
		namespace
		{
			void* Allocate(std::size_t a_size)
			{
				auto& heap = Allocator::ProxyHeap::get();
				return a_size > 0 ?
				           heap.aligned_alloc(0x10, a_size) :
				           nullptr;
			}

			void Deallocate(void* a_ptr)
			{
				auto& heap = Allocator::ProxyHeap::get();
				heap.aligned_free(a_ptr);
			}

			struct AllocPatch :
				Xbyak::CodeGenerator
			{
				AllocPatch(std::size_t a_size, std::uintptr_t a_target)
				{
					mov(rcx, a_size);
					mov(rdx, a_target);
					jmp(rdx);
				}
			};

			struct DeallocPatch :
				Xbyak::CodeGenerator
			{
				DeallocPatch(std::uintptr_t a_target)
				{
					mov(rcx, rdx);
					mov(rdx, a_target);
					jmp(rdx);
				}
			};

			void InstallAllocations()
			{
				constexpr std::size_t funcSize = 0xAC;
				constexpr std::array todo{
					std::make_pair<std::size_t, std::uint64_t>(24, 764468),
					std::make_pair<std::size_t, std::uint64_t>(32, 244379),
					std::make_pair<std::size_t, std::uint64_t>(40, 323762),
					std::make_pair<std::size_t, std::uint64_t>(48, 403216),
					std::make_pair<std::size_t, std::uint64_t>(56, 482791),
					std::make_pair<std::size_t, std::uint64_t>(64, 562495),
					std::make_pair<std::size_t, std::uint64_t>(80, 933298),
					std::make_pair<std::size_t, std::uint64_t>(88, 1012687),
				};

				for (const auto& [size, id] : todo) {
					AllocPatch p{ size, reinterpret_cast<std::uintptr_t>(&Allocate) };
					p.ready();
					REL::Relocation<std::uintptr_t> target{ REL::ID(id) };
					assert(p.getSize() <= funcSize);
					REL::safe_write(
						target.address(),
						std::span{ p.getCode<const std::byte*>(), p.getSize() });
					REL::safe_fill(
						target.address() + p.getSize(),
						REL::INT3,
						funcSize - p.getSize());
				}
			}

			void InstallDeallocations()
			{
				constexpr std::size_t funcSize = 0xB4;
				constexpr std::array todo{
					static_cast<std::uint64_t>(843767),
					static_cast<std::uint64_t>(1357032),
					static_cast<std::uint64_t>(293097),
					static_cast<std::uint64_t>(811714),
					static_cast<std::uint64_t>(1329635),
					static_cast<std::uint64_t>(1037201),
					static_cast<std::uint64_t>(1555743),
					static_cast<std::uint64_t>(491434),
				};

				DeallocPatch p{ reinterpret_cast<std::uintptr_t>(&Deallocate) };
				p.ready();
				assert(p.getSize() <= funcSize);
				for (const auto id : todo) {
					REL::Relocation<std::uintptr_t> target{ REL::ID(id) };
					REL::safe_write(
						target.address(),
						std::span{ p.getCode<const std::byte*>(), p.getSize() });
					REL::safe_fill(
						target.address() + p.getSize(),
						REL::INT3,
						funcSize - p.getSize());
				}
			}
		}

		void Install()
		{
			InstallAllocations();
			InstallDeallocations();

			REL::Relocation<std::uintptr_t> target{ REL::ID(329149), 0x48 };
			REL::safe_fill(target.address(), REL::NOP, 0x5);

			logger::info("Installed SmallBlockAllocator patch"sv);
		}
	}

	void Install()
	{
		AutoScrapBufferPatch::Install();
		BSTextureStreamerLocalHeapPatch::Install();
		HavokMemorySystemPatch::Install();
		MemoryManagerPatch::Install();
		ScaleformAllocatorPatch::Install();
		ScrapHeapPatch::Install();
		SmallBlockAllocatorPatch::Install();
	}
}
