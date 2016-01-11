/*
 * Copyright (c) 2014, Xiaomi Inc.
 * All rights reserved.
 *
 * @file shm_counter.h
 * @brief
 *
 * @author wengcijie@xiaomi.com.
 * @date 2015-06-02
 */

/**
 * 详见READM.md
 * 使用示例
 *   std::string table_name = "frontend";
 *   ShmCounterImpl cntr;
 *   int err = cntr.init(table_name)；
 *   if (err) {
 *      return err;
 *   }
 *
 *   cntr.SetType("connections", falcon::kAdditiveGauge);
 *   cntr.SetType("fe-bind_round_trip_time_p99", falcon::kNonAdditiveGauge);
 *   cntr.Inc("connections");
 *   cntr.Inc("connections");
 *   cntr.Inc("connections");
 *   cntr.Dec("connections");
 *
 *   cntr.Inc("fe-5-login_timeout);
 *   cntr.Inc("fe-5-login_timeout);
 *
 *   cntr.Set("fe-bind_round_trip_time_p99", 106);
 *
 *
 *
 */
#ifndef  __FALCON_SHM_COUNTER_H
#define  __FALCON_SHM_COUNTER_H

#include <stdint.h>
#include <map>
#include <string>
#include <atomic>
#include <thread>

#define MAX_TAG_LEN 1024
#define MAX_COUNTER_LEN 128
#define MAX_HOSTNAME_LEN 64


#ifdef DEBUG_LOG
#define LOG(...)   fprintf(stderr, __VA_ARGS__)
#define INFO(...)   fprintf(stderr, __VA_ARGS__)
#define WARN(...)  fprintf(stderr, __VA_ARGS__)
#define DEBUG(...) fprintf(stderr, __VA_ARGS__)
#else
#define LOG(...)
#define INFO(...)
#define WARN(...)
#define DEBUG(...)
#endif


namespace falcon {


enum Type{
    kCounter            = 0,
    kAdditiveGauge,
    kNonAdditiveGauge,
};

enum {
    kOk                    = 0,
    kNotFound,
    kIndexOverflow,
    kCounterNameInConsistent,
    kCounterNumberOverflow,
    kSysMkdir,
    kSysShmOpen,
    kSysFstat,
    kSysFtruncate,
    kSysMmap,
    kSysShmUnlink,
    kNotOwner,
};
class ShmCounterImpl {
public:
struct Entry {
    int64_t   value;
    char      type;
    char      name[MAX_COUNTER_LEN-1]; // 8 bytes allignment
};
struct Table {
    char            endpoint[MAX_HOSTNAME_LEN];
    char            tag[MAX_TAG_LEN];
    int             last_timestamp;
    int             max_entry_num;
    int             n_entry;
    int             padding; // for 8 bytes allignment
    Entry           entries[0];
};

public:
    ShmCounterImpl();
    ~ShmCounterImpl();

    /**
     * @ table_name
     *   用来标识多个进程/线程共同访问的一组counter
     *   比如，frontend的多个线程使用的counter就使用相同的table_name
     * @ tag
     *   对应falcon系统的tag域, 可以为空
     *   比如，cluster=production-lg,cop=xiaomi,job=frontend,owt=miliao,pdl=im,service=frontend,servicegroup=common
     * @max_counter_num
     *   指的是一个table_name下最多的counter数量，默认1024
     *   超出这个数量将会报错
     * return 0，成功，非0，失败
     */
    //int init(const std::string &table_name,
            //const std::string &tag = "",
            //int max_counter_num=1024);

public:

    int Inc(const char*counter_name, int64_t value, Type type=kCounter);
    int Inc(const char*counter_name, Type type=kCounter);

    int Dec(const char*counter_name, Type type=kCounter);
    int Dec(const char*counter_name, int64_t value, Type type=kCounter);

    int Set(const char*counter_name, int64_t value, Type type=kCounter);



    int64_t Get(const char *counter_name) ;

    /**
     * if not specified, using default type kCOUNTER
     */
    int SetType(const char *counter_name, Type type);

    int GetType(const char *counter_name);

    int SetTag(const char *tag);

private:
    /**
     * 把ShmCounterImpl对象挂载到共享内存中，必须检查返回值，
     * return 0，成功，非0，失败
     */
    int Attach();

    int Detach();

    int Inc(int idx, Type type=kCounter);
    int Inc(int idx, int64_t value, Type type=kCounter);

    int Dec(int idx, Type type=kCounter);
    int Dec(int idx, int64_t value, Type type=kCounter);

    int Set(int idx, int64_t value, Type type=kCounter);
    int64_t Get(int idx) ;

    int GetCounterIndex(const char *counter_name);
private:
    // 表格首指针
    Table  *table_;

    Entry  *entries_;


    std::map<std::string, int> name_index_map_;


    // hostname of sample point, if not passed in from ctor, Counter will
    // gethostname itself
    std::string endpoint_;


    pid_t       tpid_;
    std::string table_name_;
    std::string shm_file_;
    std::string shm_dir_;
    std::string shm_root_;
    int         tablelet_index_;
    int         shm_fd_;
    int         max_counter_num_;
    int         shm_size_;
    int         table_size_;

    int         err_;
    int         errno_;
};

class ShmCounter {
public:
    static ShmCounter* instance();
public:
    int Inc(const char*counter_name, int64_t value, Type type=kCounter);
    int Inc(const char*counter_name, Type type=kCounter);

    int Dec(const char*counter_name, Type type=kCounter);
    int Dec(const char*counter_name, int64_t value, Type type=kCounter);

    int Set(const char*counter_name, int64_t value, Type type=kCounter);

    int64_t Get(const char*counter_name) ;

    /**
     * if not specified, using default type kCOUNTER
     */
    int SetType(const char*counter_name, Type type);
    int GetType(const char*counter_name);

    int SetTag(const char*tag);
private:
    static ShmCounter cntr_;
};

} // namespace falcon

#endif  // __FALCON_SHM_COUNTER_H

