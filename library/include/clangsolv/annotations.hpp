#pragma once

// ClangSOLV always defines CLANGSOLV_CHECKING during analysis. Other compilers
// can generate executable code, but that compilation alone performs no checks.
#if defined(CLANGSOLV_CHECKING) && !defined(__clang__)
#error "ClangSOLV analysis requires Clang"
#endif
#if defined(__clang__)
#define CLANGSOLV_ANNOTATE(text) [[clang::annotate(text)]]
#else
#define CLANGSOLV_ANNOTATE(text)
#endif

#define returns(...) CLANGSOLV_ANNOTATE("clangsolv.returns:" #__VA_ARGS__)
#define needs(...) CLANGSOLV_ANNOTATE("clangsolv.requires:" #__VA_ARGS__)
// One-based argument index; an empty argument defaults to 1.
// The checker interprets the empty annotation payload without macro tricks.
#define preserves(...) CLANGSOLV_ANNOTATE("clangsolv.preserves:" #__VA_ARGS__)
// Trusted promise: this call does not invalidate the caller's tracked evidence.
#define readonly CLANGSOLV_ANNOTATE("clangsolv.readonly")

namespace clangsolv {
// These no-op proof markers have no runtime storage and use ordinary T values.
// Establishment is deliberately an explicit trusted application assertion.
template<class... Labels, class T>
CLANGSOLV_ANNOTATE("clangsolv.establish") void establish(T&) noexcept {}
template<class... Labels, class T>
CLANGSOLV_ANNOTATE("clangsolv.forget") void forget(T&) noexcept {}
template<class... Labels, class T>
CLANGSOLV_ANNOTATE("clangsolv.require") void require(const T&) noexcept {}
}
