/* Definitions of the Lean runtime symbols that src/runtime/mpn.cpp and
   mpz.cpp reference, so that the two files link without the rest of the
   runtime. Allocation goes to libc; every panic aborts. */
#include <cstdio>
#include <cstdlib>
#include <string>
#include "runtime/exception.h"

extern "C" {
__attribute__((weak)) void free_sized(void * ptr, size_t) { free(ptr); }
void lean_internal_panic_out_of_memory(void) {
    fprintf(stderr, "stub: out of memory\n");
    abort();
}
void lean_internal_panic_overflow(void) {
    fprintf(stderr, "stub: size overflow\n");
    abort();
}
void lean_internal_panic(char const * msg) {
    fprintf(stderr, "stub: panic: %s\n", msg);
    abort();
}
}

namespace lean {
throwable::throwable(char const * msg) : m_msg(msg) {}
throwable::throwable(std::string const & msg) : m_msg(msg) {}
throwable::~throwable() noexcept {}
char const * throwable::what() const noexcept { return m_msg.c_str(); }

void notify_assertion_violation(char const * file_name, int line, char const * condition) {
    fprintf(stderr, "assertion violation %s:%d: %s\n", file_name, line, condition);
    abort();
}
void invoke_debugger() { abort(); }
bool is_debug_enabled(char const *) { return false; }
}
