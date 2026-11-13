#pragma once
#include "SandboxPointerAPI.h"
#include <cstring>
namespace SandboxPointers {
// The Windows executable owns the implementation; the DLL receives only this ABI.
const DFSandboxAPI* runtimeAPI();
void bind(const DFSandboxAPI* api);
const DFSandboxAPI& api();
class Handle {
public:
    Handle() = default;
    explicit Handle(DFSandboxHandle value) : value_(value) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    void reset(DFSandboxHandle value = {}) { if(value_.id)api().release(value_); value_=value; }
    DFSandboxHandle get() const { return value_; }
private:
    DFSandboxHandle value_{};
};
}
