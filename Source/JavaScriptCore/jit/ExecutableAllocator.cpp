/*
 * Copyright (C) 2008-2024 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "ExecutableAllocator.h"

#if ENABLE(JIT)

#include "ExecutableAllocationFuzz.h"
#include "JITOperationValidation.h"
#include "LinkBuffer.h"
#include <bit>
#include <wtf/ByteOrder.h>
#include <wtf/CryptographicallyRandomNumber.h>
#include <wtf/FastBitVector.h>
#include <wtf/FileSystem.h>
#include <wtf/FixedVector.h>
#include <wtf/IterationStatus.h>
#include <wtf/MallocSpan.h>
#include <wtf/PageReservation.h>
#include <wtf/ProcessID.h>
#include <wtf/RedBlackTree.h>
#include <wtf/Scope.h>
#include <wtf/SequesteredMalloc.h>
#include <wtf/SystemTracing.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/UUID.h>
#include <wtf/WorkQueue.h>

#if ENABLE(LIBPAS_JIT_HEAP)
WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN
#include <bmalloc/jit_heap.h>
#include <bmalloc/jit_heap_config.h>
WTF_ALLOW_UNSAFE_BUFFER_USAGE_END
#else
#include <wtf/MetaAllocator.h>
#endif

#if HAVE(IOS_JIT_RESTRICTIONS) || HAVE(MAC_JIT_RESTRICTIONS)
#include <wtf/cocoa/Entitlements.h>
#endif

#if OS(DARWIN)
#include <fcntl.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <wtf/spi/cocoa/MachVMSPI.h>
#endif

#if USE(INLINE_JIT_PERMISSIONS_API)
#include <wtf/darwin/WeakLinking.h>
WTF_WEAK_LINK_FORCE_IMPORT(be_memory_inline_jit_restrict_with_witness_supported);
#endif

namespace JSC {

using namespace WTF;

#if OS(DARWIN) && CPU(ARM64)
// We already rely on page size being CeilingOnPageSize elsewhere (e.g. MarkedBlock).
// Just use the constexpr CeilingOnPageSize for better efficiency.
static inline constexpr size_t executablePageSize() { return CeilingOnPageSize; }
#else
static inline size_t executablePageSize() { return WTF::pageSize(); }
#endif

#if ENABLE(LIBPAS_JIT_HEAP)
static constexpr size_t minimumPoolSizeForSegregatedHeap = 256 * MB;
#endif

#if defined(FIXED_EXECUTABLE_MEMORY_POOL_SIZE_IN_MB) && FIXED_EXECUTABLE_MEMORY_POOL_SIZE_IN_MB > 0
static constexpr size_t fixedExecutableMemoryPoolSize = FIXED_EXECUTABLE_MEMORY_POOL_SIZE_IN_MB * MB;
#elif CPU(ARM64)
#if ENABLE(JUMP_ISLANDS)
static constexpr size_t fixedExecutableMemoryPoolSize = 512 * MB;
#else
static constexpr size_t fixedExecutableMemoryPoolSize = 128 * MB;
#endif
#elif CPU(ARM_THUMB2)
#if ENABLE(JUMP_ISLANDS)
static constexpr size_t fixedExecutableMemoryPoolSize = 32 * MB;
#else
static constexpr size_t fixedExecutableMemoryPoolSize = 16 * MB;
#endif
#elif CPU(X86_64)
static constexpr size_t fixedExecutableMemoryPoolSize = 1 * GB;
#else
static constexpr size_t fixedExecutableMemoryPoolSize = 32 * MB;
#endif

#if ENABLE(JUMP_ISLANDS)
#if CPU(ARM64)
static constexpr double islandRegionSizeFraction = 0.125;
static constexpr size_t islandSizeInBytes = 4;
#elif CPU(ARM_THUMB2)
static constexpr double islandRegionSizeFraction = 0.05;
static constexpr size_t islandSizeInBytes = 4;
#endif
#endif

// Quick sanity check, in case FIXED_EXECUTABLE_MEMORY_POOL_SIZE_IN_MB was set.
#if !ENABLE(JUMP_ISLANDS)
static_assert(fixedExecutableMemoryPoolSize <= MacroAssembler::nearJumpRange, "Executable pool size is too large for near jump/call without JUMP_ISLANDS");
#endif

#if CPU(ARM)
static constexpr double executablePoolReservationFraction = 0.15;
#else
static constexpr double executablePoolReservationFraction = 0.25;
#endif

#if ENABLE(LIBPAS_JIT_HEAP)
// This size is derived from jit_config's medium table size.
static constexpr size_t minimumExecutablePoolReservationSize = 256 * KB;
static_assert(fixedExecutableMemoryPoolSize * executablePoolReservationFraction >= minimumExecutablePoolReservationSize);
static_assert(fixedExecutableMemoryPoolSize < 4 * GB, "ExecutableMemoryHandle assumes it is less than 4GB");
#endif

#if HAVE(KDEBUG_H)
// 325696c8-e7cc-11ee-9f4e-325096b39f47
static constexpr WTF::UUID jscJITNamespace { static_cast<UInt128>(0x325696c8e7cc11eeULL) << 64 | (0x9f4e325096b39f47ULL) };
#endif

static bool isJITEnabled()
{
    bool jitEnabled = !g_jscConfig.jitDisabled;
#if HAVE(IOS_JIT_RESTRICTIONS)
    jitEnabled = jitEnabled && (processHasEntitlement("dynamic-codesigning"_s) || processHasEntitlement("com.apple.developer.cs.allow-jit"_s));
#elif HAVE(MAC_JIT_RESTRICTIONS) && USE(APPLE_INTERNAL_SDK)
    jitEnabled = jitEnabled && processHasEntitlement("com.apple.security.cs.allow-jit"_s);
#endif
    return jitEnabled;
}

void ExecutableAllocator::disableJIT()
{
    ASSERT(!g_jscConfig.fixedVMPoolExecutableAllocator);
    if (g_jscConfig.jitDisabled) {
        RELEASE_ASSERT(!Options::useJIT());
        return;
    }

    g_jscConfig.jitDisabled = true;
    Options::useJIT() = false;

#if HAVE(IOS_JIT_RESTRICTIONS) || HAVE(MAC_JIT_RESTRICTIONS) && USE(APPLE_INTERNAL_SDK)
#if HAVE(IOS_JIT_RESTRICTIONS)
    bool shouldDisableJITMemory = processHasEntitlement("dynamic-codesigning"_s) || processHasEntitlement("com.apple.developer.cs.allow-jit"_s);
#else
    bool shouldDisableJITMemory = processHasEntitlement("com.apple.security.cs.allow-jit"_s) && !isKernOpenSource();
#endif
    if (shouldDisableJITMemory) {
        // Because of an OS quirk, even after the JIT region has been unmapped,
        // the OS thinks that region is reserved, and as such, can cause Gigacage
        // allocation to fail. We work around this by initializing the Gigacage
        // first.
        // Note: when called, disableJIT() is always called extra early in the
        // process bootstrap. Under normal operation (when disableJIT() isn't
        // called at all), we will naturally initialize the Gigacage before we
        // allocate the JIT region. Hence, this workaround is merely ensuring the
        // same behavior of allocation ordering.
        Gigacage::ensureGigacage();

        constexpr size_t size = 1;
        constexpr int protection = PROT_READ | PROT_WRITE | PROT_EXEC;
        constexpr int fd = OSAllocator::JSJITCodePages;
        int flags = MAP_PRIVATE | MAP_ANON | (Options::useJITCage() ? MAP_EXECUTABLE_FOR_JIT_WITH_JIT_CAGE : MAP_EXECUTABLE_FOR_JIT);
        void* allocation = mmap(nullptr, size, protection, flags, fd, 0);
        const void* executableMemoryAllocationFailure = reinterpret_cast<void*>(-1);
        RELEASE_ASSERT_WITH_MESSAGE(allocation && allocation != executableMemoryAllocationFailure, "We should not have allocated executable memory before disabling the JIT.");
        RELEASE_ASSERT_WITH_MESSAGE(!munmap(allocation, size), "Unmapping executable memory should succeed so we do not have any executable memory in the address space");
        RELEASE_ASSERT_WITH_MESSAGE(mmap(nullptr, size, protection, flags, fd, 0) == executableMemoryAllocationFailure, "Allocating executable memory should fail after disableJIT() is called.");
    }
#endif
}

#if OS(DARWIN) && HAVE(REMAP_JIT)

#if USE(EXECUTE_ONLY_JIT_WRITE_FUNCTION)
static ALWAYS_INLINE MacroAssemblerCodeRef<JITThunkPtrTag> jitWriteThunkGenerator(void* writableAddr, void* stubBase, size_t stubSize)
{
    auto exitScope = makeScopeExit([] {
        RELEASE_ASSERT(!g_jscConfig.useFastJITPermissions);
    });

    using namespace ARM64Registers;
    using TrustedImm32 = MacroAssembler::TrustedImm32;

    MacroAssembler jit;

    jit.tagReturnAddress();
    jit.move(MacroAssembler::TrustedImmPtr(writableAddr), x7);
    jit.addPtr(x7, x0);

    jit.move(x0, x3);
    MacroAssembler::Jump smallCopy = jit.branch64(MacroAssembler::Below, x2, MacroAssembler::TrustedImm64(64));

    jit.add64(TrustedImm32(32), x3);
    jit.and64(TrustedImm32(-32), x3);
    jit.loadPair64(x1, x12, x13);
    jit.loadPair64(x1, TrustedImm32(16), x14, x15);
    jit.sub64(x3, x0, x5);
    jit.addPtr(x5, x1);

    jit.loadPair64(x1, x8, x9);
    jit.loadPair64(x1, TrustedImm32(16), x10, x11);
    jit.add64(TrustedImm32(32), x1);
    jit.sub64(x5, x2);
    jit.storePair64(x12, x13, x0);
    jit.storePair64(x14, x15, x0, TrustedImm32(16));
    MacroAssembler::Jump cleanup = jit.branchSub64(MacroAssembler::BelowOrEqual, TrustedImm32(64), x2);

    MacroAssembler::Label copyLoop = jit.label();
    jit.storePair64WithNonTemporalAccess(x8, x9, x3);
    jit.storePair64WithNonTemporalAccess(x10, x11, x3, TrustedImm32(16));
    jit.add64(TrustedImm32(32), x3);
    jit.loadPair64WithNonTemporalAccess(x1, x8, x9);
    jit.loadPair64WithNonTemporalAccess(x1, TrustedImm32(16), x10, x11);
    jit.add64(TrustedImm32(32), x1);
    jit.branchSub64(MacroAssembler::Above, TrustedImm32(32), x2).linkTo(copyLoop, &jit);

    cleanup.link(&jit);
    jit.add64(x2, x1);
    jit.loadPair64(x1, x12, x13);
    jit.loadPair64(x1, TrustedImm32(16), x14, x15);
    jit.storePair64(x8, x9, x3);
    jit.storePair64(x10, x11, x3, TrustedImm32(16));
    jit.addPtr(x2, x3);
    jit.storePair64(x12, x13, x3, TrustedImm32(32));
    jit.storePair64(x14, x15, x3, TrustedImm32(48));
    jit.ret();

    MacroAssembler::Label local0 = jit.label();
    jit.load64(MacroAssembler::PostIndexAddress(x1, 8), x6);
    jit.store64(x6, MacroAssembler::PostIndexAddress(x3, 8));
    smallCopy.link(&jit);
    jit.branchSub64(MacroAssembler::AboveOrEqual, TrustedImm32(8), x2).linkTo(local0, &jit);
    MacroAssembler::Jump local2 = jit.branchAdd64(MacroAssembler::Equal, TrustedImm32(8), x2);
    MacroAssembler::Label local1 = jit.label();
    jit.load8(x1, PostIndex(1), x6);
    jit.store8(x6, x3, PostIndex(1));
    jit.branchSub64(MacroAssembler::NotEqual, TrustedImm32(1), x2).linkTo(local1, &jit);
    local2.link(&jit);
    jit.ret();

    auto stubBaseCodePtr = CodePtr<LinkBufferPtrTag>(tagCodePtr<LinkBufferPtrTag>(stubBase));
    LinkBuffer linkBuffer(jit, stubBaseCodePtr, stubSize, LinkBuffer::Profile::Thunk);
    // We don't use FINALIZE_CODE() for two reasons.
    // The first is that we don't want the writeable address, as disassembled instructions,
    // to appear in the console or anywhere in memory, via the PrintStream buffer.
    // The second is we can't guarantee that the code is readable when using the
    // asyncDisassembly option as our caller will set our pages execute only.
    return linkBuffer.finalizeCodeWithoutDisassembly<JITThunkPtrTag>(nullptr);
}
#else // not USE(EXECUTE_ONLY_JIT_WRITE_FUNCTION)
static void genericWriteToJITRegion(off_t offset, const void* data, size_t dataSize)
{
WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN
    memcpy((void*)(g_jscConfig.startOfFixedWritableMemoryPool + offset), data, dataSize);
WTF_ALLOW_UNSAFE_BUFFER_USAGE_END
}

static MacroAssemblerCodeRef<JITThunkPtrTag> ALWAYS_INLINE jitWriteThunkGenerator(void* address, void*, size_t)
{
    g_jscConfig.startOfFixedWritableMemoryPool = reinterpret_cast<uintptr_t>(address);
    void* function = reinterpret_cast<void*>(&genericWriteToJITRegion);
#if CPU(ARM_THUMB2)
    // Handle thumb offset
    uintptr_t functionAsInt = reinterpret_cast<uintptr_t>(function);
    functionAsInt -= 1;
    function = reinterpret_cast<void*>(functionAsInt);
#endif
    auto codePtr = CodePtr<JITThunkPtrTag>(tagCFunctionPtr<JITThunkPtrTag>(function));
    return MacroAssemblerCodeRef<JITThunkPtrTag>::createSelfManagedCodeRef(codePtr);
}
#endif // USE(EXECUTE_ONLY_JIT_WRITE_FUNCTION)

static ALWAYS_INLINE void initializeSeparatedWXHeaps(void* stubBase, size_t stubSize, void* jitBase, size_t jitSize)
{
    auto exitScope = makeScopeExit([] {
        RELEASE_ASSERT(!g_jscConfig.useFastJITPermissions);
    });

    mach_vm_address_t writableAddr = 0;

    // Create a second mapping of the JIT region at a random address.
    vm_prot_t cur, max;
    int remapFlags = VM_FLAGS_ANYWHERE;
#if defined(VM_FLAGS_RANDOM_ADDR)
    remapFlags |= VM_FLAGS_RANDOM_ADDR;
#endif
    kern_return_t ret = mach_vm_remap(mach_task_self(), &writableAddr, jitSize, 0,
        remapFlags,
        mach_task_self(), (mach_vm_address_t)jitBase, FALSE,
        &cur, &max, VM_INHERIT_DEFAULT);

    bool remapSucceeded = (ret == KERN_SUCCESS);
    if (!remapSucceeded)
        return;

    // Assemble a thunk that will serve as the means for writing into the JIT region.
    MacroAssemblerCodeRef<JITThunkPtrTag> writeThunk = jitWriteThunkGenerator(reinterpret_cast<void*>(writableAddr), stubBase, stubSize);

    int result = 0;

#if USE(EXECUTE_ONLY_JIT_WRITE_FUNCTION)
    // Prevent reading the write thunk code.
    result = vm_protect(mach_task_self(), reinterpret_cast<vm_address_t>(stubBase), stubSize, true, VM_PROT_EXECUTE);
    RELEASE_ASSERT(!result);
#endif

    // Prevent writing into the executable JIT mapping.
    result = vm_protect(mach_task_self(), reinterpret_cast<vm_address_t>(jitBase), jitSize, true, VM_PROT_READ | VM_PROT_EXECUTE);
    RELEASE_ASSERT(!result);

    // Prevent execution in the writable JIT mapping.
    result = vm_protect(mach_task_self(), static_cast<vm_address_t>(writableAddr), jitSize, true, VM_PROT_READ | VM_PROT_WRITE);
    RELEASE_ASSERT(!result);

WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN
    // Zero out writableAddr to avoid leaking the address of the writable mapping.
    memset_s(&writableAddr, sizeof(writableAddr), 0, sizeof(writableAddr));
WTF_ALLOW_UNSAFE_BUFFER_USAGE_END

#if ENABLE(SEPARATED_WX_HEAP)
    g_jscConfig.jitWriteSeparateHeaps = reinterpret_cast<JITWriteSeparateHeapsFunction>(writeThunk.code().taggedPtr());
#endif
}

#endif // OS(DARWIN) && HAVE(REMAP_JIT)

struct JITReservation {
    PageReservation pageReservation;
    void* base { nullptr };
    size_t size { 0 };
};

WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN

static ALWAYS_INLINE JITReservation initializeJITPageReservation()
{
    JITReservation reservation;
    if (!isJITEnabled())
        return reservation;

#if OS(DARWIN)
    // Call pageSize() to run its assertions to enforce invariants that executablePageSize() relies on.
    WTF::pageSize();
#endif
    reservation.size = fixedExecutableMemoryPoolSize;

    if (Options::jitMemoryReservationSize()) {
        reservation.size = Options::jitMemoryReservationSize();
#if ENABLE(LIBPAS_JIT_HEAP)
        if (reservation.size * executablePoolReservationFraction < minimumExecutablePoolReservationSize)
            reservation.size += minimumExecutablePoolReservationSize;
#endif
    }
    reservation.size = std::max(roundUpToMultipleOf(executablePageSize(), reservation.size), executablePageSize() * 2);

#if !ENABLE(JUMP_ISLANDS)
    RELEASE_ASSERT(reservation.size <= MacroAssembler::nearJumpRange, "Executable pool size is too large for near jump/call without JUMP_ISLANDS");
#endif

#if ENABLE(LIBPAS_JIT_HEAP)
    if (reservation.size < minimumPoolSizeForSegregatedHeap)
        jit_heap_runtime_config.max_segregated_object_size = 0;
#endif

    auto tryCreatePageReservation = [] (size_t reservationSize) {
#if OS(LINUX)
        // On Linux, if we use uncommitted reservation, mmap operation is recorded with small page size in perf command's output.
        // This makes the following JIT code logging broken and some of JIT code is not recorded correctly.
        // To avoid this problem, we use committed reservation if we need perf JITDump logging.
        if (Options::useJITDump())
            return PageReservation::tryReserveAndCommitWithGuardPages(reservationSize, OSAllocator::JSJITCodePages, EXECUTABLE_POOL_WRITABLE, true, false);
#endif
        if (Options::useJITCage() && JSC_ALLOW_JIT_CAGE_SPECIFIC_RESERVATION)
            return PageReservation::tryReserve(reservationSize, OSAllocator::JSJITCodePages, EXECUTABLE_POOL_WRITABLE, true, Options::useJITCage());
        return PageReservation::tryReserveWithGuardPages(reservationSize, OSAllocator::JSJITCodePages, EXECUTABLE_POOL_WRITABLE, true, false);
    };

    reservation.pageReservation = tryCreatePageReservation(reservation.size);

    if (Options::verboseExecutablePoolAllocation())
        dataLog(getpid(), ": Got executable pool reservation at ", RawPointer(reservation.pageReservation.base()), "...", RawPointer(reservation.pageReservation.end()), ", while I'm at ", RawPointer(reinterpret_cast<void*>(initializeJITPageReservation)), "\n");
    
    if (reservation.pageReservation) {
        ASSERT(reservation.pageReservation.size() == reservation.size);
        reservation.base = reservation.pageReservation.base();
        g_jscConfig.useFastJITPermissions = threadSelfRestrictSupported<MemoryRestriction::kRwxToRw>();

        if (g_jscConfig.useFastJITPermissions)
            threadSelfRestrict<MemoryRestriction::kRwxToRx>();

#if ENABLE(SEPARATED_WX_HEAP)
        if (!g_jscConfig.useFastJITPermissions) {
            // First page of our JIT allocation is reserved.
            ASSERT(reservation.size >= executablePageSize() * 2);
            reservation.base = (void*)((uintptr_t)(reservation.base) + executablePageSize());
            reservation.size -= executablePageSize();
            initializeSeparatedWXHeaps(reservation.pageReservation.base(), executablePageSize(), reservation.base, reservation.size);
        }
#endif

        void* reservationEnd = static_cast<uint8_t*>(reservation.base) + reservation.size;
        g_jscConfig.startExecutableMemory = reservation.base;
        g_jscConfig.endExecutableMemory = reservationEnd;

#if !USE(SYSTEM_MALLOC)
        static_assert(WebConfig::reservedSlotsForExecutableAllocator >= 2);
        WebConfig::g_config[0] = std::bit_cast<uintptr_t>(reservation.base);
        WebConfig::g_config[1] = std::bit_cast<uintptr_t>(reservationEnd);
#endif

#if HAVE(KDEBUG_H)
        {
            uint64_t pid = getCurrentProcessID();
            auto uuid = WTF::UUID::createVersion5(jscJITNamespace, std::span { std::bit_cast<const uint8_t*>(&pid), sizeof(pid) });
            kdebug_trace(KDBG_CODE(DBG_DYLD, DBG_DYLD_UUID, DBG_DYLD_UUID_MAP_A), std::byteswap(uuid.high()), std::byteswap(uuid.low()), std::bit_cast<uintptr_t>(reservation.base), 0);
        }
#elif USE(SYSPROF_CAPTURE)
        WTFEmitSignpost(reservation, InitJITPageReservation);
#endif
    }

    return reservation;
}

WTF_ALLOW_UNSAFE_BUFFER_USAGE_END

class FixedVMPoolExecutableAllocator final {
    // This does not need to be TZONE_ALLOCATED because it's only used as a singleton
    // and is only allocated once long before any scripts are executed.
    WTF_MAKE_SEQUESTERED_IMMORTAL_ALLOCATED(FixedVMPoolExecutableAllocator);

#if ENABLE(JUMP_ISLANDS)
    class Islands;
    class RegionAllocator;
#endif

public:
    FixedVMPoolExecutableAllocator()
#if !ENABLE(JUMP_ISLANDS)
        : m_allocator(*this)
#endif
    {
        JITReservation reservation = initializeJITPageReservation();
        m_reservation = WTFMove(reservation.pageReservation);
        if (m_reservation) {
#if ENABLE(JUMP_ISLANDS)
            // Consider this scenario:
            //
            //                                    <------------- nearJumpRange ------------->
            //    <------------- nearJumpRange -------------->
            //    [  island 0 ] [ JIT region 1  ] [ island 1 ] [ JIT region 2  ] [ island 2 ] [ JIT region3  ]
            //
            //                         C1 ---jump---> I1 --------------jump---------> I2 ---jump---> C3
            //
            // In order to jump across a distance that spans multiple nearJumpRanges, we currently
            // use chaining near jumps. Hence, a near jump in a jump island also needs to be able
            // to reach its neighboring jump islands in order to form this chain.
            //
            // For example, let say we have code in JIT region 1 that needs to jump to code in JIT region 3 in
            // the illustration above. That jump will be implemented as:
            //   1. Code C1 in JIT region 1 near jumps to island I1 in island 1.
            //   2. Island I1 near jumps to island I2 in island 2.
            //   3. Island I2 near jumps to code C3 in JIT region 3.
            //
            // Each of these near jumps need to be within the range of MacroAssembler::nearJumpRange.
            //
            // As a result, the maximum size of each JIT region is:
            //     MacroAssembler::nearJumpRange - 2 * islandRegionSize
            //
            // This is why each RegionAllocator tracks a range of m_regionSize instead of
            // MacroAssembler::nearJumpRange.
            //
            // Note: the illustration above shows a jump chain in the forward direction. The jump island
            // scheme also allows for a jump chain in the backward direction e.g. from C3 to C1.
            
            const size_t islandRegionSize = roundUpToMultipleOf(executablePageSize(), static_cast<size_t>(MacroAssembler::nearJumpRange * islandRegionSizeFraction));
            m_regionSize = MacroAssembler::nearJumpRange - islandRegionSize;
            RELEASE_ASSERT(isPageAligned(executablePageSize(), islandRegionSize));
            RELEASE_ASSERT(isPageAligned(executablePageSize(), m_regionSize));
            const unsigned numAllocators = (reservation.size + m_regionSize - 1) / m_regionSize;
            m_allocators = FixedVector<RegionAllocator>::createWithSizeAndConstructorArguments(numAllocators, *this);

            uintptr_t start = std::bit_cast<uintptr_t>(memoryStart());
            uintptr_t reservationEnd = std::bit_cast<uintptr_t>(memoryEnd());
            for (size_t i = 0; i < numAllocators; ++i) {
                uintptr_t end = start + m_regionSize;
                uintptr_t islandBegin = end - islandRegionSize;
                // The island in the very last region is never actually used (everything goes backwards), but we
                // can't put code there in case they do need to use a backward jump island, so set up accordingly.
                if (i == numAllocators - 1)
                    islandBegin = end = std::min(islandBegin, reservationEnd);
                RELEASE_ASSERT(end <= reservationEnd);
                m_allocators[i].configure(start, islandBegin, end);
                m_bytesReserved += m_allocators[i].allocatorSize();
                start += m_regionSize;
            }
#else
            m_allocator.addFreshFreeSpace(reservation.base, reservation.size);
            m_bytesReserved += reservation.size;
#endif

#if ENABLE(MPROTECT_RX_TO_RWX)
            ptrdiff_t pagesInReservation = (std::bit_cast<uint8_t*>(g_jscConfig.endExecutableMemory) - std::bit_cast<uint8_t*>(g_jscConfig.startExecutableMemory)) / executablePageSize();
            m_pageWriterCounts = std::bit_cast<uint8_t*>(WTF::fastZeroedMalloc(pagesInReservation));
#endif
        }
    }

    ~FixedVMPoolExecutableAllocator()
    {
        m_reservation.deallocate();
    }

    void* memoryStart() { return g_jscConfig.startExecutableMemory; }
    void* memoryEnd() { return g_jscConfig.endExecutableMemory; }
    bool isValid() { return !!m_reservation; }

    RefPtr<ExecutableMemoryHandle> allocate(size_t sizeInBytes)
    {
#if ENABLE(LIBPAS_JIT_HEAP)
        Vector<void*, 0> randomAllocations;
        if (Options::useRandomizingExecutableIslandAllocation()) [[unlikely]] {
            // Let's fragment the executable memory agressively
            auto bytesAllocated = m_bytesAllocated.load(std::memory_order_relaxed);
            uint64_t allocationRoom = (m_reservation.size() - bytesAllocated) * 1 / 100 / sizeInBytes;
            if (!allocationRoom)
                allocationRoom = 1;
            int count = cryptographicallyRandomNumber<uint32_t>() % allocationRoom;

            randomAllocations.grow(count);

            for (int i = 0; i < count; ++i) {
                void* result = jit_heap_try_allocate(sizeInBytes);
                if (!result) {
                    // We are running out of memory, so make sure this allocation will succeed.
                    for (int j = 0; j < i; ++j)
                        jit_heap_deallocate(randomAllocations[j]);
                    randomAllocations.shrink(0);
                    break;
                }
                randomAllocations[i] = result;
            }
        }
        auto result = ExecutableMemoryHandle::createImpl(sizeInBytes);
        if (result) [[likely]]
            m_bytesAllocated.fetch_add(result->sizeInBytes(), std::memory_order_relaxed);
        if (Options::useRandomizingExecutableIslandAllocation()) [[unlikely]] {
            for (unsigned i = 0; i < randomAllocations.size(); ++i)
                jit_heap_deallocate(randomAllocations[i]);
        }
        return result;
#elif ENABLE(JUMP_ISLANDS)
        Locker locker { getLock() };

        unsigned start = 0;
        if (Options::useRandomizingExecutableIslandAllocation()) [[unlikely]]
            start = cryptographicallyRandomNumber<uint32_t>() % m_allocators.size();

        unsigned i = start;
        while (true) {
            RegionAllocator& allocator = m_allocators[i];
            if (RefPtr<ExecutableMemoryHandle> result = allocator.allocate(locker, sizeInBytes))
                return result;
            i = (i + 1) % m_allocators.size();
            if (i == start)
                break;
        }
        return nullptr;
#else
        return m_allocator.allocate(sizeInBytes);
#endif
    }

    Lock& getLock() WTF_RETURNS_LOCK(m_lock) { return m_lock; }

#if ENABLE(LIBPAS_JIT_HEAP)
    void shrinkBytesAllocated(size_t oldSizeInBytes, size_t newSizeInBytes)
    {
        m_bytesAllocated.fetch_add(newSizeInBytes - oldSizeInBytes, std::memory_order_relaxed);
    }
#endif

    // Non atomic
    size_t bytesAllocated()
    {
#if ENABLE(LIBPAS_JIT_HEAP)
        return m_bytesAllocated.load(std::memory_order_relaxed);
#else
        size_t result = 0;
        forEachAllocator([&] (Allocator& allocator) {
            result += allocator.bytesAllocated();
        });
        return result;
#endif
    }

    size_t bytesReserved() const
    {
        return m_bytesReserved;
    }

    size_t bytesAvailable()
    {
        size_t bytesReserved = this->bytesReserved();
#if ENABLE(LIBPAS_JIT_HEAP)
        size_t nonAvailableSize = static_cast<size_t>(bytesReserved * executablePoolReservationFraction);
        if (nonAvailableSize < minimumExecutablePoolReservationSize)
            return bytesReserved - minimumExecutablePoolReservationSize;
        return bytesReserved - nonAvailableSize;
#else
        return static_cast<size_t>(bytesReserved * (1 - executablePoolReservationFraction));
#endif
    }

#if !ENABLE(LIBPAS_JIT_HEAP)
    size_t bytesCommitted()
    {
        size_t result = 0;
        forEachAllocator([&] (Allocator& allocator) {
            result += allocator.bytesCommitted();
        });
        return result;
    }
#endif

    bool isInAllocatedMemory(const AbstractLocker& locker, void* address)
    {
#if ENABLE(JUMP_ISLANDS)
        if (RegionAllocator* allocator = findRegion(std::bit_cast<uintptr_t>(address)))
            return allocator->isInAllocatedMemory(locker, address);
        return false;
#else
        return m_allocator.isInAllocatedMemory(locker, address);
#endif
    }

#if ENABLE(META_ALLOCATOR_PROFILE)
    void dumpProfile()
    {
        forEachAllocator([&] (Allocator& allocator) {
            allocator.dumpProfile();
        });
    }
#endif

#if ENABLE(MPROTECT_RX_TO_RWX)
    static std::pair<size_t, size_t> pageRangeForWrittenRegion(const void* start, size_t sizeInBytes, size_t pageSize)
    {
        size_t startPage = std::bit_cast<uintptr_t>(std::bit_cast<uint8_t*>(start) - std::bit_cast<uint8_t*>(g_jscConfig.startExecutableMemory)) / pageSize;
        size_t endPage = WTF::roundUpToMultipleOf(pageSize, std::bit_cast<uintptr_t>(start) - std::bit_cast<uintptr_t>(g_jscConfig.startExecutableMemory) + sizeInBytes) / pageSize;
        return { startPage, endPage };
    }

    void startWriting(const void* start, size_t sizeInBytes)
    {
        size_t pageSize = executablePageSize();
        auto [startPage, endPage] = pageRangeForWrittenRegion(start, sizeInBytes, pageSize);
        uint8_t* startAddress = std::bit_cast<uint8_t*>(g_jscConfig.startExecutableMemory);

        {
            Locker locker(m_pageLock);
            ssize_t firstFirstWriterPage = -1; // We use this to track runs of pages for which we are the first writer, since this means their mprotect() calls can be batched.
            for (size_t i = startPage; i < endPage; i ++) {
                if (!(m_pageWriterCounts[i]++)) {
                    if (firstFirstWriterPage == -1)
                        firstFirstWriterPage = i;
                } else if (firstFirstWriterPage != -1) {
                    mprotect(startAddress + pageSize * firstFirstWriterPage, (i - firstFirstWriterPage) * pageSize, PROT_READ | PROT_WRITE | PROT_EXEC);
                    firstFirstWriterPage = -1;
                }
            }
            if (firstFirstWriterPage != -1)
                mprotect(startAddress + pageSize * firstFirstWriterPage, (endPage - firstFirstWriterPage) * pageSize, PROT_READ | PROT_WRITE | PROT_EXEC);
        }
    }

    void finishWriting(const void* start, size_t sizeInBytes)
    {
        size_t pageSize = executablePageSize();
        auto [startPage, endPage] = pageRangeForWrittenRegion(start, sizeInBytes, pageSize);
        uint8_t* startAddress = std::bit_cast<uint8_t*>(g_jscConfig.startExecutableMemory);

        {
            Locker locker(m_pageLock);
            ssize_t firstLastWriterPage = -1; // We use this to track runs of pages for which we are the last writer, since this means their mprotect() calls can be batched.
            for (size_t i = startPage; i < endPage; i ++) {
                if (!--m_pageWriterCounts[i]) {
                    if (firstLastWriterPage == -1)
                        firstLastWriterPage = i;
                } else if (firstLastWriterPage != -1) {
                    mprotect(startAddress + pageSize * firstLastWriterPage, (i - firstLastWriterPage) * pageSize, PROT_READ | PROT_EXEC);
                    firstLastWriterPage = -1;
                }
            }
            if (firstLastWriterPage != -1)
                mprotect(startAddress + pageSize * firstLastWriterPage, (endPage - firstLastWriterPage) * pageSize, PROT_READ | PROT_EXEC);
        }
    }
#endif

#if !ENABLE(LIBPAS_JIT_HEAP)
    MetaAllocator::Statistics currentStatistics()
    {
        Locker locker { getLock() };
        MetaAllocator::Statistics result { 0, 0, 0 };
        forEachAllocator([&] (Allocator& allocator) {
            auto allocatorStats = allocator.currentStatistics(locker);
            result.bytesAllocated += allocatorStats.bytesAllocated;
            result.bytesReserved += allocatorStats.bytesReserved;
            result.bytesCommitted += allocatorStats.bytesCommitted;
        });
        return result;
    }
#endif // !ENABLE(LIBPAS_JIT_HEAP)

#if ENABLE(LIBPAS_JIT_HEAP)
    void handleWillBeReleased(ExecutableMemoryHandle& handle, size_t sizeInBytes)
    {
        m_bytesAllocated.fetch_sub(sizeInBytes, std::memory_order_relaxed);
#if ENABLE(JUMP_ISLANDS)
        if (m_islandsForJumpSourceLocation.isEmpty())
            return;
        
        Locker locker { getLock() };
        handleWillBeReleased(locker, handle);
#else // ENABLE(JUMP_ISLANDS) -> so !ENABLE(JUMP_ISLANDS)
        UNUSED_PARAM(handle);
#endif // ENABLE(JUMP_ISLANDS) -> so end of !ENABLE(JUMP_ISLANDS)
    }
#endif // ENABLE(LIBPAS_JIT_HEAP)

#if ENABLE(JUMP_ISLANDS)
    void handleWillBeReleased(const Locker<Lock>& locker, ExecutableMemoryHandle& handle)
    {
        if (m_islandsForJumpSourceLocation.isEmpty())
            return;

        Vector<Islands*, 16> toRemove;
        void* start = handle.start().untaggedPtr();
        void* end = handle.end().untaggedPtr();
        m_islandsForJumpSourceLocation.iterate([&] (Islands& islands, bool& visitLeft, bool& visitRight) {
            if (start <= islands.key() && islands.key() < end)
                toRemove.append(&islands);
            if (islands.key() > start)
                visitLeft = true;
            if (islands.key() < end)
                visitRight = true;
        });

        for (Islands* islands : toRemove)
            freeIslands(locker, islands);

        if (ASSERT_ENABLED) {
            m_islandsForJumpSourceLocation.iterate([&] (Islands& islands, bool& visitLeft, bool& visitRight) {
                if (start <= islands.key() && islands.key() < end) {
                    dataLogLn("did not remove everything!");
                    RELEASE_ASSERT_NOT_REACHED();
                }
                visitLeft = true;
                visitRight = true;
            });
        }
    }

    void* makeIsland(uintptr_t jumpLocation, uintptr_t newTarget, bool concurrently, bool useMemcpy)
    {
        Locker locker { getLock() };
        return islandForJumpLocation(locker, jumpLocation, newTarget, concurrently, useMemcpy);
    }

private:
    RegionAllocator* findRegion(uintptr_t ptr)
    {
        RegionAllocator* result = nullptr;
        forEachAllocator([&] (RegionAllocator& allocator) {
            if (allocator.start() <= ptr && ptr < allocator.end()) {
                result = &allocator;
                return IterationStatus::Done;
            }
            return IterationStatus::Continue;
        });
        return result;
    }

    void freeJumpIslands(const Locker<Lock>&, Islands* islands)
    {
        for (CodeLocationLabel<ExecutableMemoryPtrTag> jumpIsland : islands->jumpIslands) {
            uintptr_t untaggedJumpIsland = std::bit_cast<uintptr_t>(jumpIsland.dataLocation());
            RegionAllocator* allocator = findRegion(untaggedJumpIsland);
            RELEASE_ASSERT(allocator);
            allocator->freeIsland(untaggedJumpIsland);
        }
        islands->jumpIslands.clear();
    }

    void freeIslands(const Locker<Lock>& locker, Islands* islands)
    {
        freeJumpIslands(locker, islands);
        m_islandsForJumpSourceLocation.remove(islands);
        delete islands;
    }

    void* islandForJumpLocation(const Locker<Lock>& locker, uintptr_t jumpLocation, uintptr_t target, bool concurrently, bool useMemcpy)
    {
        Islands* islands = m_islandsForJumpSourceLocation.findExact(std::bit_cast<void*>(jumpLocation));
        if (islands) {
            // FIXME: We could create some method of reusing already allocated islands here, but it's
            // unlikely to matter in practice.
            if (!concurrently)
                freeJumpIslands(locker, islands);
        } else {
            islands = new Islands;
            islands->jumpSourceLocation = CodeLocationLabel<ExecutableMemoryPtrTag>(tagCodePtr<ExecutableMemoryPtrTag>(std::bit_cast<void*>(jumpLocation)));
            m_islandsForJumpSourceLocation.insert(islands);
        }

        RegionAllocator* allocator = findRegion(jumpLocation > target ? jumpLocation - m_regionSize : jumpLocation);
        RELEASE_ASSERT(allocator);
        void* result = allocator->allocateIsland();
        void* currentIsland = result;
        jumpLocation = std::bit_cast<uintptr_t>(currentIsland);
        while (true) {
            islands->jumpIslands.append(CodeLocationLabel<ExecutableMemoryPtrTag>(tagCodePtr<ExecutableMemoryPtrTag>(currentIsland)));

            auto emitJumpTo = [&] (void* target) {
                RELEASE_ASSERT(Assembler::canEmitJump(std::bit_cast<void*>(jumpLocation), target));
                if (useMemcpy)
                    Assembler::fillNearTailCall<MachineCodeCopyMode::Memcpy>(currentIsland, target);
                else
                    Assembler::fillNearTailCall<MachineCodeCopyMode::JITMemcpy>(currentIsland, target);
            };

            if (Assembler::canEmitJump(std::bit_cast<void*>(jumpLocation), std::bit_cast<void*>(target))) {
                emitJumpTo(std::bit_cast<void*>(target));
                break;
            }

            uintptr_t nextIslandRegion;
            if (jumpLocation > target)
                nextIslandRegion = jumpLocation - m_regionSize;
            else
                nextIslandRegion = jumpLocation + m_regionSize;

            RegionAllocator* allocator = findRegion(nextIslandRegion);
            RELEASE_ASSERT(allocator);
            void* nextIsland = allocator->allocateIsland();
            emitJumpTo(nextIsland);
            jumpLocation = std::bit_cast<uintptr_t>(nextIsland);
            currentIsland = nextIsland;
        }

        return result;
    }
#endif // ENABLE(JUMP_ISLANDS)

private:
    class Allocator
#if !ENABLE(LIBPAS_JIT_HEAP)
        : public MetaAllocator
#endif
    {
#if !ENABLE(LIBPAS_JIT_HEAP)
        using Base = MetaAllocator;
#endif
    public:
        Allocator(FixedVMPoolExecutableAllocator& allocator)
#if !ENABLE(LIBPAS_JIT_HEAP)
            : Base(allocator.getLock(), jitAllocationGranule, executablePageSize()) // round up all allocations to 32 bytes
            ,
#else
            :
#endif
            m_fixedAllocator(allocator)
        {
        }

#if ENABLE(LIBPAS_JIT_HEAP)
        void addFreshFreeSpace(void* start, size_t sizeInBytes)
        {
            RELEASE_ASSERT(!m_start);
            RELEASE_ASSERT(!m_end);
            m_start = reinterpret_cast<uintptr_t>(start);
            m_end = m_start + sizeInBytes;
            jit_heap_add_fresh_memory(pas_range_create(m_start, m_end));
        }

        bool isInAllocatedMemory(const AbstractLocker&, void* address)
        {
            uintptr_t addressAsInt = reinterpret_cast<uintptr_t>(address);
            return addressAsInt >= m_start && addressAsInt < m_end;
        }
#endif // ENABLE(LIBPAS_JIT_HEAP)

#if !ENABLE(LIBPAS_JIT_HEAP)
        FreeSpacePtr allocateNewSpace(size_t&) override
        {
            // We're operating in a fixed pool, so new allocation is always prohibited.
            return nullptr;
        }

        void notifyNeedPage(void* page, size_t count) override
        {
            m_fixedAllocator.m_reservation.commit(page, executablePageSize() * count);
        }

        void notifyPageIsFree(void* page, size_t count) override
        {
            m_fixedAllocator.m_reservation.decommit(page, executablePageSize() * count);
        }
#endif // !ENABLE(LIBPAS_JIT_HEAP)

        FixedVMPoolExecutableAllocator& m_fixedAllocator;
#if ENABLE(LIBPAS_JIT_HEAP)
        uintptr_t m_start { 0 };
        uintptr_t m_end { 0 };
#endif // ENABLE(LIBPAS_JIT_HEAP)
    };

#if ENABLE(JUMP_ISLANDS)
    class RegionAllocator final : public Allocator {
        using Base = Allocator;
    public:
        RegionAllocator(FixedVMPoolExecutableAllocator& allocator)
            : Base(allocator)
        {
            RELEASE_ASSERT(!(executablePageSize() % islandSizeInBytes), "Current implementation relies on this");
        }

        void configure(uintptr_t start, uintptr_t islandBegin, uintptr_t end)
        {
            RELEASE_ASSERT(start < islandBegin);
            RELEASE_ASSERT(islandBegin <= end);
            m_start = std::bit_cast<void*>(start);
            m_islandBegin = std::bit_cast<void*>(islandBegin);
            m_end = std::bit_cast<void*>(end);
            RELEASE_ASSERT(!((this->islandBegin() - this->start()) % executablePageSize()));
            RELEASE_ASSERT(!((this->end() - this->islandBegin()) % executablePageSize()));
            addFreshFreeSpace(std::bit_cast<void*>(this->start()), allocatorSize());
        }

        //  ------------------------------------
        //  | jit allocations -->   <-- islands |
        //  -------------------------------------

        uintptr_t start() { return reinterpret_cast<uintptr_t>(m_start); }
        uintptr_t islandBegin() { return reinterpret_cast<uintptr_t>(m_islandBegin); }
        uintptr_t end() { return reinterpret_cast<uintptr_t>(m_end); }

        size_t maxIslandsInThisRegion() { return (end() - islandBegin()) / islandSizeInBytes; }

        uintptr_t allocatorSize()
        {
            return islandBegin() - start();
        }

        size_t islandsPerPage()
        {
            size_t islandsPerPage = executablePageSize() / islandSizeInBytes;
            ASSERT(islandsPerPage * islandSizeInBytes == executablePageSize());
            ASSERT(isPowerOfTwo(islandsPerPage));
            return islandsPerPage;
        }

#if !ENABLE(LIBPAS_JIT_HEAP)
        void release(const Locker<Lock>& locker, MetaAllocatorHandle& handle) final
        {
            AssemblyCommentRegistry::singleton().unregisterCodeRange(handle.start().untaggedPtr(), handle.end().untaggedPtr());
            m_fixedAllocator.handleWillBeReleased(locker, handle);
            Base::release(locker, handle);
        }
#endif

        void* allocateIsland()
        {
            uintptr_t end = this->end();
            auto findResult = [&] () -> void* {
                size_t resultBit = islandBits.findClearBit(0);
                if (resultBit == islandBits.size())
                    return nullptr;
                islandBits[resultBit] = true;
                uintptr_t result = end - ((resultBit + 1) * islandSizeInBytes); 
                return std::bit_cast<void*>(result);
            };

            if (void* result = findResult())
                return result;

            const size_t oldSize = islandBits.size();
            const size_t maxIslandsInThisRegion = this->maxIslandsInThisRegion();

            RELEASE_ASSERT(oldSize <= maxIslandsInThisRegion);
            if (oldSize == maxIslandsInThisRegion) [[unlikely]]
                crashOnJumpIslandExhaustion();

            const size_t newSize = std::min(oldSize + islandsPerPage(), maxIslandsInThisRegion);
            islandBits.resize(newSize);

            uintptr_t islandsBegin = end - (newSize * islandSizeInBytes); // [islandsBegin, end)
            m_fixedAllocator.m_reservation.commit(std::bit_cast<void*>(islandsBegin), (newSize - oldSize) * islandSizeInBytes);

            void* result = findResult();
            RELEASE_ASSERT(result);
            return result;
        }

        NEVER_INLINE NO_RETURN_DUE_TO_CRASH void crashOnJumpIslandExhaustion()
        {
            CRASH();
        }

        std::optional<size_t> islandBit(uintptr_t island)
        {
            uintptr_t end = this->end();
            if (islandBegin() <= island && island < end)
                return ((end - island) / islandSizeInBytes) - 1;
            return std::nullopt;
        }

        void freeIsland(uintptr_t island)
        {
            RELEASE_ASSERT(islandBegin() <= island && island < end());
            size_t bit = islandBit(island).value();
            RELEASE_ASSERT(!!islandBits[bit]);
            islandBits[bit] = false;
        }

        bool isInAllocatedMemory(const AbstractLocker& locker, void* address)
        {
            if (Base::isInAllocatedMemory(locker, address))
                return true;
            if (std::optional<size_t> bit = islandBit(std::bit_cast<uintptr_t>(address))) {
                if (bit.value() < islandBits.size())
                    return !!islandBits[bit.value()];
            }
            return false;
        }

    private:
#define REGION_ALLOCATOR_CODEPTR(field) \
    WTF_FUNCPTR_PTRAUTH_STR("RegionAllocator." #field) field

        // Range: [start, end)
        void* REGION_ALLOCATOR_CODEPTR(m_start);
        void* REGION_ALLOCATOR_CODEPTR(m_islandBegin);
        void* REGION_ALLOCATOR_CODEPTR(m_end);
        FastBitVector islandBits;
    };
#endif // ENABLE(JUMP_ISLANDS)

    template <typename Function>
    void forEachAllocator(Function function)
    {
#if ENABLE(JUMP_ISLANDS)
        for (RegionAllocator& allocator : m_allocators) {
            using FunctionResultType = decltype(function(allocator));
            if constexpr (std::is_same<IterationStatus, FunctionResultType>::value) {
                if (function(allocator) == IterationStatus::Done)
                    break;
            } else {
                static_assert(std::is_same<void, FunctionResultType>::value);
                function(allocator);
            }
        }
#else
        function(m_allocator);
#endif // ENABLE(JUMP_ISLANDS)
    }

#if ENABLE(JUMP_ISLANDS)
    class Islands : public RedBlackTree<Islands, void*>::Node {
        WTF_MAKE_TZONE_ALLOCATED(Islands);
    public:
        void* key() { return jumpSourceLocation.dataLocation(); }
        CodeLocationLabel<ExecutableMemoryPtrTag> jumpSourceLocation;
        Vector<CodeLocationLabel<ExecutableMemoryPtrTag>> jumpIslands;
    };
#endif // ENABLE(JUMP_ISLANDS)

    Lock m_lock;
    PageReservation m_reservation;
#if ENABLE(JUMP_ISLANDS)
    size_t m_regionSize;
    FixedVector<RegionAllocator> m_allocators;
    RedBlackTree<Islands, void*> m_islandsForJumpSourceLocation;
#else
    Allocator m_allocator;
#endif // ENABLE(JUMP_ISLANDS)

#if ENABLE(MPROTECT_RX_TO_RWX)
    Lock m_pageLock;
    uint8_t* m_pageWriterCounts;
#endif

    size_t m_bytesReserved { 0 };
#if ENABLE(LIBPAS_JIT_HEAP)
    std::atomic<size_t> m_bytesAllocated { 0 };
#endif
};

#if ENABLE(JUMP_ISLANDS)
WTF_MAKE_TZONE_ALLOCATED_IMPL(FixedVMPoolExecutableAllocator::Islands);
#endif // ENABLE(JUMP_ISLANDS)

// Keep this pointer in a mutable global variable to help Leaks find it.
// But we do not use this pointer.
static FixedVMPoolExecutableAllocator* globalFixedVMPoolExecutableAllocatorToWorkAroundLeaks = nullptr;
void ExecutableAllocator::initializeUnderlyingAllocator()
{
    RELEASE_ASSERT(!g_jscConfig.fixedVMPoolExecutableAllocator);
    g_jscConfig.fixedVMPoolExecutableAllocator = new FixedVMPoolExecutableAllocator();
    globalFixedVMPoolExecutableAllocatorToWorkAroundLeaks = g_jscConfig.fixedVMPoolExecutableAllocator;
}

bool ExecutableAllocator::isValid() const
{
    FixedVMPoolExecutableAllocator* allocator = g_jscConfig.fixedVMPoolExecutableAllocator;
    if (!allocator)
        return Base::isValid();
    return allocator->isValid();
}

bool ExecutableAllocator::underMemoryPressure()
{
    FixedVMPoolExecutableAllocator* allocator = g_jscConfig.fixedVMPoolExecutableAllocator;
    if (!allocator)
        return Base::underMemoryPressure();
    return allocator->bytesAllocated() > allocator->bytesReserved() / 2;
}

double ExecutableAllocator::memoryPressureMultiplier(size_t addedMemoryUsage)
{
    FixedVMPoolExecutableAllocator* allocator = g_jscConfig.fixedVMPoolExecutableAllocator;
    if (!allocator)
        return Base::memoryPressureMultiplier(addedMemoryUsage);
    ASSERT(allocator->bytesAllocated() <= allocator->bytesReserved());
    size_t bytesAllocated = allocator->bytesAllocated() + addedMemoryUsage;
    size_t bytesAvailable = allocator->bytesAvailable();
    if (bytesAllocated >= bytesAvailable)
        bytesAllocated = bytesAvailable;
    double result = 1.0;
    size_t divisor = bytesAvailable - bytesAllocated;
    if (divisor)
        result = static_cast<double>(bytesAvailable) / divisor;
    if (result < 1.0)
        result = 1.0;
    return result;
}

RefPtr<ExecutableMemoryHandle> ExecutableAllocator::allocate(size_t sizeInBytes, JITCompilationEffort effort)
{
    FixedVMPoolExecutableAllocator* allocator = g_jscConfig.fixedVMPoolExecutableAllocator;
    if (!allocator)
        return Base::allocate(sizeInBytes, effort);
#if !ENABLE(LIBPAS_JIT_HEAP)
    if (Options::logExecutableAllocation()) {
        MetaAllocator::Statistics stats = allocator->currentStatistics();
        dataLog("Allocating ", sizeInBytes, " bytes of executable memory with ", stats.bytesAllocated, " bytes allocated, ", stats.bytesReserved, " bytes reserved, and ", stats.bytesCommitted, " committed.\n");
    }
#endif

    if (effort != JITCompilationCanFail && Options::reportMustSucceedExecutableAllocations()) {
        dataLog("Allocating ", sizeInBytes, " bytes of executable memory with JITCompilationMustSucceed.\n");
        WTFReportBacktrace();
    }

    if (effort == JITCompilationCanFail
        && doExecutableAllocationFuzzingIfEnabled() == PretendToFailExecutableAllocation)
        return nullptr;

    if (effort == JITCompilationCanFail) {
        // Don't allow allocations if we are down to reserve.
        size_t bytesAllocated = allocator->bytesAllocated() + sizeInBytes;
        size_t bytesAvailable = allocator->bytesAvailable();
        if (bytesAllocated > bytesAvailable) {
            if (Options::logExecutableAllocation())
                dataLog("Allocation failed because bytes allocated ", bytesAllocated,  " > ", bytesAvailable, " bytes available.\n");
            return nullptr;
        }
    }

    RefPtr<ExecutableMemoryHandle> result = allocator->allocate(sizeInBytes);
    if (!result) {
        if (effort != JITCompilationCanFail) {
            dataLog("Ran out of executable memory while allocating ", sizeInBytes, " bytes.\n");
            CRASH();
        }
        return nullptr;
    }

    void* start = allocator->memoryStart();
    void* end = allocator->memoryEnd();
    void* resultStart = result->start().untaggedPtr();
    void* resultEnd = result->end().untaggedPtr();
    RELEASE_ASSERT(start <= resultStart && resultStart < end);
    RELEASE_ASSERT(start < resultEnd && resultEnd <= end);
    return result;
}

bool ExecutableAllocator::isValidExecutableMemory(const AbstractLocker& locker, void* address)
{
    FixedVMPoolExecutableAllocator* allocator = g_jscConfig.fixedVMPoolExecutableAllocator;
    if (!allocator)
        return Base::isValidExecutableMemory(locker, address);
    return allocator->isInAllocatedMemory(locker, address);
}

Lock& ExecutableAllocator::getLock() const
{
    FixedVMPoolExecutableAllocator* allocator = g_jscConfig.fixedVMPoolExecutableAllocator;
    if (!allocator)
        return Base::getLock();
    return allocator->getLock();
}

size_t ExecutableAllocator::committedByteCount()
{
#if ENABLE(LIBPAS_JIT_HEAP)
    return Base::committedByteCount();
#else // ENABLE(LIBPAS_JIT_HEAP) -> so start of !ENABLE(LIBPAS_JIT_HEAP)
    FixedVMPoolExecutableAllocator* allocator = g_jscConfig.fixedVMPoolExecutableAllocator;
    if (!allocator)
        return Base::committedByteCount();
    return allocator->bytesCommitted();
#endif // ENABLE(LIBPAS_JIT_HEAP) -> so end of !ENABLE(LIBPAS_JIT_HEAP)
}

#if ENABLE(META_ALLOCATOR_PROFILE)
void ExecutableAllocator::dumpProfile()
{
    FixedVMPoolExecutableAllocator* allocator = g_jscConfig.fixedVMPoolExecutableAllocator;
    if (!allocator)
        return;
    allocator->dumpProfile();
}
#endif

#if ENABLE(JUMP_ISLANDS)
void* ExecutableAllocator::getJumpIslandToUsingJITMemcpy(void* from, void* newDestination)
{
    FixedVMPoolExecutableAllocator* allocator = g_jscConfig.fixedVMPoolExecutableAllocator;
    if (!allocator)
        RELEASE_ASSERT_NOT_REACHED();

    constexpr bool concurrently = false;
    constexpr bool useMemcpy = false;
    return allocator->makeIsland(std::bit_cast<uintptr_t>(from), std::bit_cast<uintptr_t>(newDestination), concurrently, useMemcpy);
}

void* ExecutableAllocator::getJumpIslandToUsingMemcpy(void* from, void* newDestination)
{
    FixedVMPoolExecutableAllocator* allocator = g_jscConfig.fixedVMPoolExecutableAllocator;
    if (!allocator)
        RELEASE_ASSERT_NOT_REACHED();

    constexpr bool concurrently = false;
    constexpr bool useMemcpy = true;
    return allocator->makeIsland(std::bit_cast<uintptr_t>(from), std::bit_cast<uintptr_t>(newDestination), concurrently, useMemcpy);
}

void* ExecutableAllocator::getJumpIslandToConcurrently(void* from, void* newDestination)
{
    FixedVMPoolExecutableAllocator* allocator = g_jscConfig.fixedVMPoolExecutableAllocator;
    if (!allocator)
        RELEASE_ASSERT_NOT_REACHED();

    constexpr bool concurrently = true;
    constexpr bool useMemcpy = false;
    return allocator->makeIsland(std::bit_cast<uintptr_t>(from), std::bit_cast<uintptr_t>(newDestination), concurrently, useMemcpy);
}
#endif

void* startOfFixedExecutableMemoryPoolImpl()
{
    FixedVMPoolExecutableAllocator* allocator = g_jscConfig.fixedVMPoolExecutableAllocator;
    if (!allocator)
        return nullptr;
    return allocator->memoryStart();
}

void* endOfFixedExecutableMemoryPoolImpl()
{
    FixedVMPoolExecutableAllocator* allocator = g_jscConfig.fixedVMPoolExecutableAllocator;
    if (!allocator)
        return nullptr;
    return allocator->memoryEnd();
}

WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN

void dumpJITMemory(const void* dst, const void* src, size_t size)
{
    RELEASE_ASSERT(Options::dumpJITMemoryPath());

#if OS(DARWIN)
    static Lock dumpJITMemoryLock;
    static int fd WTF_GUARDED_BY_LOCK(dumpJITMemoryLock) = -1;
    static uint8_t* buffer;
    static constexpr size_t bufferSize = fixedExecutableMemoryPoolSize;
    static size_t offset WTF_GUARDED_BY_LOCK(dumpJITMemoryLock) = 0;
    static bool needsToFlush WTF_GUARDED_BY_LOCK(dumpJITMemoryLock) = false;
    static LazyNeverDestroyed<Ref<WorkQueue>> flushQueue;
    static auto flushQueueSingleton = []() { return flushQueue.get(); };
    struct DumpJIT {
        static void flush() WTF_REQUIRES_LOCK(dumpJITMemoryLock)
        {
            if (fd == -1) {
                auto path = String::fromLatin1(Options::dumpJITMemoryPath());
                path = makeStringByReplacingAll(path, "%pid"_s, String::number(getCurrentProcessID()));
                fd = open(FileSystem::fileSystemRepresentation(path).data(), O_CREAT | O_TRUNC | O_APPEND | O_WRONLY | O_EXLOCK | O_NONBLOCK, 0666);
                RELEASE_ASSERT(fd != -1);
            }
            ::write(fd, buffer, offset);
            offset = 0;
            needsToFlush = false;
        }

        static void enqueueFlush() WTF_REQUIRES_LOCK(dumpJITMemoryLock)
        {
            if (needsToFlush)
                return;

            needsToFlush = true;
            flushQueueSingleton()->dispatchAfter(Seconds(Options::dumpJITMemoryFlushInterval()), [] {
                Locker locker { dumpJITMemoryLock };
                if (!needsToFlush)
                    return;
                flush();
            });
        }

        static void write(const void* src, size_t size) WTF_REQUIRES_LOCK(dumpJITMemoryLock)
        {
            if (offset + size > bufferSize) [[unlikely]]
                flush();
            memcpy(buffer + offset, src, size);
            offset += size;
            enqueueFlush();
        }
    };

    static std::once_flag once;
    std::call_once(once, [] {
        buffer = std::bit_cast<uint8_t*>(malloc(bufferSize));
        flushQueue.construct(WorkQueue::create("jsc.dumpJITMemory.queue"_s, WorkQueue::QOS::Background));
        std::atexit([] {
            Locker locker { dumpJITMemoryLock };
            DumpJIT::flush();
            close(fd);
            fd = -1;
        });
    });

    Locker locker { dumpJITMemoryLock };
    uint64_t time = mach_absolute_time();
    uint64_t dst64 = std::bit_cast<uintptr_t>(dst);
    uint64_t size64 = size;
    TraceScope(DumpJITMemoryStart, DumpJITMemoryStop, time, dst64, size64);
    DumpJIT::write(&time, sizeof(time));
    DumpJIT::write(&dst64, sizeof(dst64));
    DumpJIT::write(&size64, sizeof(size64));
    DumpJIT::write(src, size);
#else
    UNUSED_PARAM(dst);
    UNUSED_PARAM(src);
    UNUSED_PARAM(size);
    RELEASE_ASSERT_NOT_REACHED();
#endif
}

WTF_ALLOW_UNSAFE_BUFFER_USAGE_END

#if ENABLE(MPROTECT_RX_TO_RWX)
void ExecutableAllocator::startWriting(const void* start, size_t sizeInBytes) { g_jscConfig.fixedVMPoolExecutableAllocator->startWriting(start, sizeInBytes); }
void ExecutableAllocator::finishWriting(const void* start, size_t sizeInBytes) { g_jscConfig.fixedVMPoolExecutableAllocator->finishWriting(start, sizeInBytes); }

void* performJITMemcpyWithMProtect(void *dst, const void *src, size_t n)
{
    g_jscConfig.fixedVMPoolExecutableAllocator->startWriting(dst, n);
    memcpyAtomicIfPossible(dst, src, n);
    g_jscConfig.fixedVMPoolExecutableAllocator->finishWriting(dst, n);
    return dst;
}
#endif

#if ENABLE(LIBPAS_JIT_HEAP) && ENABLE(JIT)
RefPtr<ExecutableMemoryHandle> ExecutableMemoryHandle::createImpl(size_t sizeInBytes)
{
    void* key = jit_heap_try_allocate(sizeInBytes);
    if (!key)
        return nullptr;
    return adoptRef(new ExecutableMemoryHandle(MemoryPtr::fromUntaggedPtr(key), jit_heap_get_size(key)));
}

ExecutableMemoryHandle::~ExecutableMemoryHandle()
{
    AssemblyCommentRegistry::singleton().unregisterCodeRange(start().untaggedPtr(), end().untaggedPtr());
    FixedVMPoolExecutableAllocator* allocator = g_jscConfig.fixedVMPoolExecutableAllocator;
    allocator->handleWillBeReleased(*this, sizeInBytes());
    if (Options::zeroExecutableMemoryOnFree()) [[unlikely]] {
        // We don't have a performJITMemset so just use a zeroed buffer.
        auto zeros = MallocSpan<uint8_t>::zeroedMalloc(sizeInBytes());
        auto span = zeros.span();
        performJITMemcpy(start().untaggedPtr(), span.data(), span.size());
    }
    jit_heap_deallocate(key());
}

void ExecutableMemoryHandle::shrink(size_t newSizeInBytes)
{
    size_t oldSizeInBytes = sizeInBytes();
    jit_heap_shrink(key(), newSizeInBytes);
    m_sizeInBytes = jit_heap_get_size(key());
    if (oldSizeInBytes != sizeInBytes()) {
        FixedVMPoolExecutableAllocator* allocator = g_jscConfig.fixedVMPoolExecutableAllocator;
        allocator->shrinkBytesAllocated(oldSizeInBytes, sizeInBytes());
    }
}
#endif // ENABLE(LIBPAS_JIT_HEAP) && ENABLE(JIT)

} // namespace JSC

#endif // ENABLE(JIT)

namespace JSC {

// Keep this pointer in a mutable global variable to help Leaks find it.
// But we do not use this pointer.
static ExecutableAllocator* globalExecutableAllocatorToWorkAroundLeaks = nullptr;
void ExecutableAllocator::initialize()
{
    if (g_jscConfig.jitDisabled)
        return;
    g_jscConfig.executableAllocator = new ExecutableAllocator;
    globalExecutableAllocatorToWorkAroundLeaks = g_jscConfig.executableAllocator;
}

ExecutableAllocator& ExecutableAllocator::singleton()
{
    ASSERT(g_jscConfig.executableAllocator);
    return *g_jscConfig.executableAllocator;
}

} // namespace JSC
