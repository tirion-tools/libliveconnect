// odbc_quote smoke test. No live SQL Server.

#include "liveconnect/liveconnect.hpp"

#include <cassert>
#include <cstdio>

int main() {
    using liveconnect::odbc_quote;

    assert(odbc_quote("abc") == "{abc}");
    // Embedded brace doubles, per MS connection-string rules.
    assert(odbc_quote("a}b") == "{a}}b}");
    assert(odbc_quote("") == "{}");
    // Whitespace stays inside the braces.
    assert(odbc_quote("  spaced  ") == "{  spaced  }");
    assert(odbc_quote("}}") == "{}}}}}");

    std::printf("libliveconnect smoke test PASS\n");
    return 0;
}
