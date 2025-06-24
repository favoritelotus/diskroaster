# Changelog

## [1.0.0] - 2025-06-20
### Added
- Initial release of `diskroaster`.
- Multi-threaded write/read test with data verification.
- Supports setting block size, number of workers, and number of passes.
- Displays live progress in MB and percentage.
- Cross-platform support for Linux and FreeBSD.
- Clean exit and signal handling (SIGINT).

## [1.1.0] - 2025-06-24
### Added
- Direct I/O support to access devices without using the system cache.
- Detection of disk logical sector size. 
