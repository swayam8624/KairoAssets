# Portability

Asset importers must produce identical results across supported standard libraries and operating systems. Numeric parsing is locale-independent and must not depend on platform-specific floating-point `std::from_chars` availability.

All importer changes are validated on Linux, macOS, and Windows. Integer parsing may use `std::from_chars`; floating-point source formats must use the portable parser policy implemented by the importer layer. Exported artifact value types use explicit semantic equality where synthesized STL comparisons are not reliable across C++ module boundaries.
