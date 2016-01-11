#pragma once

#include <dsn/tool_api.h>
#include <shm_counter.h>

namespace dsn{ namespace apps{
    extern __thread falcon::ShmCounter tls_counter;
    class pegasus_profiler : public dsn::tools::toollet
    {
    public:
        pegasus_profiler(const char* name);
        virtual void install(dsn::service_spec& spec);
    };
}} // namespace

