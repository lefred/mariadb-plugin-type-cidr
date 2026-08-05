# AI-Assisted Changes

This file documents changes made to this repository with AI assistance,
as requested by the project maintainer.

## Session: 2026-08-05

- **Model:** Claude Sonnet 5 (`claude-sonnet-5`), via Claude Code
- **Requested by:** lefred (Frédéric Descamps)
- **Task:** Review the `type_cidr` plugin against the MariaDB Server
  `CODING_STANDARDS.md` and check for security issues, then apply the
  fixes identified during that review.

### Review method

The review cross-referenced this plugin against the live MariaDB source
tree checked out at `/home/fred/workspace/MariaDB` — in particular
`CODING_STANDARDS.md`, the base classes this plugin derives from
(`sql/sql_type_fixedbin.h`, `sql/sql_type_fixedbin_storage.h`), and the
closest sibling plugin (`plugin/type_inet`) as a style/API reference.

After editing, all three `.cc` files were verified with a real
`-fsyntax-only` compile against the project's configured build tree
(`BIN-DEBUG`), using the exact `CXX_FLAGS`/`CXX_INCLUDES` MariaDB's own
CMake generates for a sibling plugin (`type_test`), including
`-Wall -Wextra` and the other project warning flags. All three files
compiled cleanly with zero errors and zero warnings both before and
after the changes below.

### Changes applied

1. **`cidr_functions.cc` — removed STL usage (`same_prefix_bits`,
   `CIDR_ADDRESS_COUNT`)**
   `CODING_STANDARDS.md` explicitly says not to introduce STL containers
   that duplicate what the codebase already provides. The
   `CIDR_ADDRESS_COUNT` implementation used `#include <string>` /
   `std::string` to do decimal bignum doubling. Rewrote it to use a
   fixed-size `char[40]` buffer manipulated in place (39 digits is the
   maximum needed, for 2^128) — same algorithm, no STL. Verified the
   result is unchanged against the existing test
   (`mysql-test/type_cidr/type_cidr.result`,
   `CIDR_ADDRESS_COUNT('::/0')` still resolves to
   `340282366920938463463374607431768211456`).

2. **`cidr_functions.cc` — hardened `same_prefix_bits()` against a
   latent out-of-bounds read**
   The function relied on `||` short-circuit evaluation
   (`!(bits % 8) || (left[bytes] ^ right[bytes]) & ...`) to avoid
   indexing one byte past the 16-byte address buffer whenever a full
   prefix (32 or 128 bits) was compared. This was correct today, but
   fragile: a future refactor of that line (e.g. reordering the
   operands, or evaluating both sides before returning) would silently
   turn it into a real out-of-bounds read. Restructured it into an
   explicit early `return true` for the whole-byte case, followed by a
   `DBUG_ASSERT(bytes < 16)` as a debug-build backstop, before the
   partial-byte comparison.

3. **Line-length (80-char limit) fixes** in `cidr_functions.cc` (the
   `Create_cidr_*` factory classes and their static instantiations) and
   `plugin.cc` (the `CIDR_PLUGIN_ENTRY(...)` table entries for
   `cidr_contained_by`, `cidr_prefix_length`, `cidr_first_address`,
   `cidr_last_address`, `cidr_address_count`) — wrapped onto continuation
   lines per the standard's alignment guidance. No behavioral change.

4. **Trailing blank line at EOF removed** from `plugin.cc`,
   `sql_type_cidr.h`, and `CMakeLists.txt` (each previously ended with
   two newlines instead of one), per the explicit "no trailing blank
   lines at end of file" rule.

## Session: 2026-08-05 (follow-up) — Windows portability

- **Model:** Claude Sonnet 5 (`claude-sonnet-5`), via Claude Code
- **Requested by:** lefred (Frédéric Descamps)
- **Task:** Address the `<arpa/inet.h>` / `inet_pton` / `inet_ntop`
  portability gap flagged. 

### What changed

`cidr_functions.cc` and `sql_type_cidr.cc` no longer include
`<arpa/inet.h>` or call `inet_pton`/`inet_ntop` anywhere. Those live in
a POSIX-only header with no Windows equivalent, which would have broken
a Windows build of this plugin.

Replaced them with a hand-rolled parser/printer for IPv4 and IPv6
addresses, added to `sql_type_cidr.cc` and declared in `sql_type_cidr.h`:

- `ascii_to_ipv4()` / `ascii_to_ipv6()` (file-local) — parse text into
  raw address bytes.
- `cidr_ipv4_to_string()` / `cidr_ipv6_to_string()` (shared with
  `cidr_functions.cc`'s `format_address()`) — print raw address bytes
  back to text.

These are ports of `Inet4`/`Inet6`'s own `ascii_to_fbt()`/`to_string()`
from `plugin/type_inet` — the sibling plugin solves the exact same
Windows-portability problem the same way, by hand-rolling instead of
calling libc. `cidr_ipv6_to_string()` deliberately never emits the
embedded-dotted-decimal form for IPv4-mapped/-compatible addresses
(unlike `inet_ntop`) — always printing the full 8-group hex form keeps
the worst-case output at 39 characters, matching
`Cidr_storage::max_char_length()` (43 = 39 address + `/` + up to 3
prefix digits) exactly, rather than the 45 characters the embedded form
could otherwise reach.

`sql_type_cidr.h` gained the size/length constants
(`CIDR_IN_ADDR_SIZE`, `CIDR_IN6_ADDR_MAX_CHAR_LENGTH`, etc.) that both
`.cc` files now share; `cidr_functions.cc`'s buffer sizing and its
`CIDR_NETMASK`/`CIDR_HOSTMASK`/etc. declared `max_length` were updated
to use them instead of `INET6_ADDRSTRLEN`.

### A real bug caught during the port, fixed before it shipped

My first draft of `cidr_ipv6_to_string()` added an extra condition
(`i + 1 != gap.pos`) meant to look defensive, but it actually suppressed
the separating `:` right before a compressed run of zero groups —
`2001:db8::1` would have printed as `2001:db8:1`, which isn't even
valid IPv6 syntax and can't be parsed back. Traced through it by hand
against several addresses, found the mismatch, and removed the faulty
condition (see the comment left in the code at that spot explaining why
the trailing `:` is needed even immediately before a gap).

### Verification

1. **Real compile**, same as before: all three `.cc` files
   `-fsyntax-only` against the project's actual `BIN-DEBUG` build tree
   with its own `-Wall -Wextra` flags — zero errors, zero warnings.
2. **Standalone test harness** (not part of the repo — built and run
   from the scratch directory only, to check the ported algorithm
   before trusting it): extracted the new parsing/printing functions
   into a throwaway `.cc`, stubbed the handful of MariaDB primitives
   they use (`my_charset_latin1`, `my_isdigit`, `my_tolower`,
   `my_snprintf`), and ran it under `-fsanitize=address,undefined`
   against:
   - ~25 valid/invalid IPv4 and IPv6 literals, each **cross-checked
     against the system's real `inet_pton`** as an oracle (same
     accept/reject verdict, same resulting bytes when both accept).
   - Parse → print → re-parse round-trips, expecting identical bytes.
   - Explicit printed-form checks, including the exact bug above
     (`2001:db8::1`), the all-zero (`::`) and leading-gap (`1::`)
     cases, and confirming a lone single zero group prints as `0`
     rather than being compressed into `::` (matching common
     `inet_ntop`/RFC 5952 practice).
   - Adversarial/boundary inputs: 1000-character strings, lengths one
     past the declared max, a maximal 8-group address with no
     compression — all clean under ASan/UBSan, no overflow, no UB.

   All tests passed.
