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

## [1.1.1] - 2025-07-01
### Fixed
- Fixed calculation of verified bytes and percentage.

## [1.2.0] - 2025-07-11
### Added
- Warning confirmation before disk write operation to prevent accidental data loss.

## [1.3.0] - 2025-08-10
### Added
- ETA (Estimated Time of Arrival).
- 'y' option to skip warning confirmation prompt.

