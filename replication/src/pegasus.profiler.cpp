/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Microsoft Corporation
 *
 * -=- Robust Distributed System Nucleus (rDSN) -=-
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*
 * Description:
 *     profiler for pegasus project
 *     execute all perf counter in this
 *
 * Revision history:
 *     2015-12-05, zhangxiaotong3, first version
 */
#include <dsn/toollet/profiler.h>
#include <dsn/service_api_c.h>
#include "pegasus.profiler.h"
#include <shm_counter.h>

using namespace dsn;

namespace dsn{ namespace apps{

    __thread struct falcon::ShmCounter tls_counter;
    std::string* code_to_task_qps;
    std::string* code_to_task_queue_lantency;
    std::string* code_to_task_execute_time;
    std::string* code_to_rpc_server_lantency;
    std::string* code_to_rpc_client_lantency;
    std::string* code_to_rpc_timeout;

    typedef uint64_extension_helper<task> task_ext_for_profiler;
    typedef uint64_extension_helper<message_ex> message_ext_for_profiler;

    // call normal task
    static void profiler_on_task_enqueue(task* caller, task* callee)
    {
        task_ext_for_profiler::get(callee) = dsn_now_ns();
    }


    static void profiler_on_task_begin(task* this_)
    {
        uint64_t& qts = task_ext_for_profiler::get(this_);
        uint64_t now = dsn_now_ns();
        tls_counter.Set(code_to_task_queue_lantency[this_->spec().code].c_str(), now - qts, falcon::kNonAdditiveGauge);
        qts = now;
    }

    static void profiler_on_task_end(task* this_)
    {
        uint64_t qts = task_ext_for_profiler::get(this_);
        uint64_t now = dsn_now_ns();
        tls_counter.Set(code_to_task_execute_time[this_->spec().code].c_str(), now - qts, falcon::kNonAdditiveGauge);
        tls_counter.Inc(code_to_task_qps[this_->spec().code].c_str(), falcon::kCounter);
    }

    static void profiler_on_rpc_call(task* caller, message_ex* req, rpc_response_task* callee)
    {
        // time rpc starts
        if (nullptr != callee)
        {
            task_ext_for_profiler::get(callee) = dsn_now_ns();
        }
    }

    static void profiler_on_rpc_request_enqueue(rpc_request_task* callee)
    {
        uint64_t now = dsn_now_ns();
        task_ext_for_profiler::get(callee) = now;
        message_ext_for_profiler::get(callee->get_request()) = now;
    }

    static void profiler_on_rpc_create_response(message_ex* req, message_ex* resp)
    {
        message_ext_for_profiler::get(resp) = message_ext_for_profiler::get(req);
    }

    // return true means continue, otherwise early terminate with task::set_error_code
    static void profiler_on_rpc_reply(task* caller, message_ex* msg)
    {
        uint64_t qts = message_ext_for_profiler::get(msg);
        uint64_t now = dsn_now_ns();
        auto code = task_spec::get(msg->local_rpc_code)->rpc_paired_code;
        tls_counter.Set(code_to_rpc_server_lantency[code].c_str(), now - qts, falcon::kNonAdditiveGauge);
    }

    static void profiler_on_rpc_response_enqueue(rpc_response_task* resp)
    {
        uint64_t& cts = task_ext_for_profiler::get(resp);
        uint64_t now = dsn_now_ns();
        if (resp->get_response() != nullptr)
        {
            tls_counter.Set(code_to_rpc_client_lantency[resp->spec().code].c_str(), now - cts, falcon::kNonAdditiveGauge);
        }
        else
        {
            tls_counter.Inc(code_to_rpc_timeout[resp->spec().code].c_str(), falcon::kCounter);
        }
        cts = now;
    }

    pegasus_profiler::pegasus_profiler(const char* name)
        : toollet(name)
    {
    }

    void pegasus_profiler::install(service_spec& spec)
    {
        task_ext_for_profiler::register_ext();
        message_ext_for_profiler::register_ext();

        code_to_task_qps = new std::string[dsn_task_code_max() + 1];
        code_to_task_queue_lantency = new std::string[dsn_task_code_max() + 1];
        code_to_task_execute_time = new std::string[dsn_task_code_max() + 1];
        code_to_rpc_server_lantency = new std::string[dsn_task_code_max() + 1];
        code_to_rpc_client_lantency = new std::string[dsn_task_code_max() + 1];
        code_to_rpc_timeout = new std::string[dsn_task_code_max() + 1];
        for (int i = 0; i <= dsn_task_code_max(); i++)
        {
            if (i == TASK_CODE_INVALID)
                continue;
            code_to_task_qps[i] = std::string("pegasus.") + std::string(dsn_task_code_to_string(i)) + std::string(".qps");
            code_to_task_queue_lantency[i] = std::string("pegasus.") + std::string(dsn_task_code_to_string(i)) + std::string(".task_lantency_ns");
            code_to_task_execute_time[i] = std::string("pegasus.") + std::string(dsn_task_code_to_string(i)) + std::string(".execute_time_ns");
            code_to_rpc_server_lantency[i] = std::string("pegasus.") + std::string(dsn_task_code_to_string(i)) + std::string(".rpc_server_lantency_ns");
            code_to_rpc_client_lantency[i] = std::string("pegasus.") + std::string(dsn_task_code_to_string(i)) + std::string(".rpc_client_lantency_ns");
            code_to_rpc_timeout[i] = std::string("pegasus.") + std::string(dsn_task_code_to_string(i)) + std::string(".rpc_timeout");

            task_spec* t_spec = task_spec::get(i);

            //task
            t_spec->on_task_enqueue.put_back(profiler_on_task_enqueue, "profiler");
            t_spec->on_task_begin.put_back(profiler_on_task_begin, "profiler");
            t_spec->on_task_end.put_back(profiler_on_task_end, "profiler");

            //rpc
            t_spec->on_rpc_call.put_back(profiler_on_rpc_call, "profiler");
            t_spec->on_rpc_request_enqueue.put_back(profiler_on_rpc_request_enqueue, "profiler");
            t_spec->on_rpc_create_response.put_back(profiler_on_rpc_create_response, "profiler");
            t_spec->on_rpc_reply.put_back(profiler_on_rpc_reply, "profiler");
            t_spec->on_rpc_response_enqueue.put_back(profiler_on_rpc_response_enqueue, "profiler");
        }
    }
}} // namespace

