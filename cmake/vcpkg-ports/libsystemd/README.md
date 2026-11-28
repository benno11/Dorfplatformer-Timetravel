# Linux compatibility overlay

This port is based on microsoft/vcpkg's libsystemd 260.2 (port version 1).
The upstream port files and existing patches are retained. The additional
`glibc231-mallinfo.patch` uses the older allocator statistics API for debug
logging when building against glibc older than 2.33. Newer glibc keeps mallinfo2.
This overlay is used only by the compatible Linux release build.
