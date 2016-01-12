//# include <dsn/dist/replication/replication_service_app.h>
//# include <dsn/apps/pegasus/meta_service_app.h>
//# include <dsn/apps/pegasus/client.h>
//# include <dsn/apps/pegasus/client_error.h>
//# include "pegasus.server.impl.h"
//# include "pegasus.client.fake.h"
//# include "pegasus.check.h"

#include "rrdb_client.h"
# include <boost/lexical_cast.hpp>
#include <stdlib.h>

#include <sys/time.h>
#include <iostream>
#include <cmath>
#include <string>
#include <thread>
#include <mutex>
#include <map>
#include <vector>
#include <iterator>
#include <fstream>
#include <unistd.h>

using namespace std;
using namespace dsn::apps;

static std::string s_pid_str;

struct configParam {

    int qpsper;
    int hashKeySize;
    int sortKeySize;
    int valueSize;
    int qps;
    int time;
    std::string type;  //performance test or pressure test
    int incre_qps;
    int incre_time;
    int max_qps;
    int max_time;
    int client_num;
    std::string log_path;

    configParam () {
        //the data should be read from the config file
        qpsper = 300;
        hashKeySize = 10;
        sortKeySize = 6;
        valueSize = 4;
        qps = 500;
        time = 1;
        type = "performance";
        //type = "pressure";
        incre_qps = 100;
        incre_time = 1;
        max_qps = 2000;
        max_time = 1000;
        client_num = 1;
        log_path = "/home/mi/learning/test/log/";
    }

};

//record all the data which used in result table
struct allRecord {

    int fail_num;
    double sum_time;
    vector<std::string> hash_keys;
    vector<std::string> sort_keys;
    vector<std::string> value_gets;
    vector<std::string> fail_types;

    allRecord() {
        fail_num = 0;
        sum_time = 0;
    }

};

/*
int drop_table(const std::string& table_name)
{
    std::cout<< "Drop table[" << table_name << "] started" << std::endl;
    pegasus::client* client = pegasus::client_factory::create_context("");
    pegasus::client_drop_table_options option;
    option.success_if_not_exist = false;
    int ret = client->drop_table(table_name, option);
    pegasus::client_factory::drop_context(client);
    if (ret != 0) {
        std::cerr << "Drop table failed" << std::endl;
        return -1;
    }
    std::cout<< "Drop table succeed" << std::endl;
    return 0;
}*/

/*
int create_table(const std::string& table_name)
{
    std::cout<< "Create table[" << table_name << "] started" << std::endl;
    pegasus::client* client = pegasus::client_factory::create_context("");
    pegasus::client_create_table_options option;
    option.partition_count = 1;
    option.success_if_exist = false;
    int ret = client->create_table(table_name, option);
    if (ret != 0) {
        std::cerr << "Create table failed, error: " << client->get_error_string(ret) << std::endl;
    }
    else
    {
        std::cout<< "Create table succeed" << std::endl;
    }
    pegasus::client_factory::drop_context(client);
    return ret;
}*/

//string add one
void stringadd(std::string& hash_key, std::string& sort_key) {
    int i = 0;
    int len = sort_key.size();
    while(i < len && sort_key[i]=='z') {
        sort_key[i++] = 'a';
    }
    if(i < len)
        sort_key[i]+=1;
    else stringadd(sort_key, hash_key);//前提是保证hash_key是足够大的，所以hash_key最好设大点字节
}

std::string initString(int size) {
    std::string temp="";
    for(int i = 0; i < size; i++) {
        temp+='a';
    }
    return temp;
}

//generate value base on sort_key
std::string generateValue(std::string sort_key, int valueSize) {
    std::string value = "";
    int len = sort_key.size() <= valueSize ? sort_key.size() : valueSize;
    value = sort_key.substr(0, len);
    for(int i = 0; i < valueSize - len; i++)
    {
        value+="a";
    }
    return value;
}

//calculate the key position
void calPos(std::string& key, int num) {
    int i=0;
    while(num != 0) {
        int temp = num % 26;
        key[i++] += temp;
        num -= temp;
        num/=26;
    }
}

//calculate the position of the par_key and sort_key
void calKeyPos(std::string& hash_key, std::string& sort_key, int num, int parSum) {
    int hashCount = num / parSum;
    int sortCount = num - parSum * hashCount;
    calPos(hash_key, hashCount);
    calPos(sort_key, sortCount);
}

void recordData(const std::string operate,
                const std::string& hash_key,
                const std::string& sort_key,
                const std::string& value_get,
                std::map<std::string, struct allRecord>& result,
                mutex& lock,
                const std::string& fail_type)
{
        lock.lock();
        result[operate].fail_num += 1;
        result[operate].hash_keys.push_back(hash_key);
        result[operate].sort_keys.push_back(sort_key);
        result[operate].value_gets.push_back(value_get);
        result[operate].fail_types.push_back(fail_type);
        lock.unlock();
}

void do_main(irrdb_client* client,
                  std::string hash_key,
                  std::string sort_key,
                  const std::string type,
                  int sum,
                  struct configParam& configPam,
                  std::map<std::string, struct allRecord>& result,
                  mutex& lock,
                  int expect_state)
{

    std::cout << type << " start, count = " << sum << std::endl;
    uint64_t total_us = 0;
    struct timeval start, end, result_time;
    int ret;

    uint64_t par_total = 0;
    uint64_t sleep_time = 0;

    for (int i = 0; i < sum; ++i) {
        std::string value;
        std::string value_get;
        gettimeofday(&start, NULL);
        if (type == "set") {
            value=generateValue(sort_key, configPam.valueSize);
            do {
                ret = client->set(hash_key, sort_key, value);
            } while(configPam.type == "pressure" && ret == dsn::apps::ERROR_TIMEOUT);
        }
        else if (type == "del") {
            do {
                ret = client->del(hash_key, sort_key);
            } while(configPam.type == "pressure" && ret == dsn::apps::ERROR_TIMEOUT);
        }
        else if (type == "check_set" || type == "get") {
            value=generateValue(sort_key, configPam.valueSize);
            if(type == "check_set") {
                do {
                     ret = client->get(hash_key, sort_key, value_get);
                } while(ret == dsn::apps::ERROR_TIMEOUT);
            } else {
                do {
                    ret = client->get(hash_key, sort_key, value_get);
                } while(configPam.type == "pressure" && ret == dsn::apps::ERROR_TIMEOUT);
            }
        }
        else if (type == "check_del") {
            do {
                 ret = client->get(hash_key, sort_key, value_get);
            } while(ret == dsn::apps::ERROR_TIMEOUT);
        }
        gettimeofday(&end, NULL);
        timersub(&end, &start, &result_time);
        total_us += (result_time.tv_sec * 1000000 + result_time.tv_usec);
        if (type == "set" || type == "get" || type == "del") {
            if ( (i + 1) % configPam.qpsper == 0) {
                sleep_time =  total_us - par_total;
                if(sleep_time <= 1000000) {
                    usleep(1000000 - sleep_time);
                    //std::cout<<"I have sleep for "<<sleep_time / 1000 <<"ms"<<std::endl;
                } else {
                    std::cout<<"over  "<< sleep_time/1000 - 1000 <<"ms"<<std::endl;
                }
                par_total = total_us;
            }
        }
        if(ret !=expect_state) {
            recordData(type, hash_key, sort_key, value_get, result, lock, client->get_error_string(ret));
        }

        if(ret == expect_state && (type == "check_set" || type == "get") && value != value_get ) {
            recordData(type, hash_key, sort_key, value_get, result, lock, client->get_error_string(-202));
        }
       stringadd(hash_key, sort_key);
    }
    lock.lock();
    result[type].sum_time += ((double)total_us/1000);
    lock.unlock();
    std::cout << type << " done, avg_latency(ms) = " << ((double)total_us / 1000 / sum) << std::endl;
}

//test multithreading template
void test_multi_model(struct configParam& configPam,
                      const std::string type,
                      std::map<std::string, struct allRecord>& result,
                      mutex& lock,
                      int expect_state,
                      const std::string& process_name)
{

    irrdb_client* client = rrdb_client_factory::get_client("rrdb.instance0");

    int parSum = (int)pow((double)26, (double)configPam.sortKeySize);

    int threadNum=ceil((float)configPam.qps/configPam.qpsper);

    std::string hash_key = initString(configPam.hashKeySize - 2) + process_name;
    std::string sort_key = initString(configPam.sortKeySize);

    thread pthreads[threadNum];

    int nornal_num = configPam.qpsper * configPam.time;

    for(int i = 0; i < threadNum; i++) {
        std::string hash_key2 = hash_key;
        std::string sort_key2 = sort_key;

        //if(i == threadNum - 1 && (configPam.qps * configPam.time) % configPam.qpsper != 0) {
           // nornal_num = (configPam.qps * configPam.time) % configPam.qpsper;
        //}

        if(i == threadNum - 1) {
            nornal_num = (configPam.qps * configPam.time) - configPam.time*i*configPam.qpsper;
        }

        calKeyPos(hash_key2, sort_key2, configPam.time*i*configPam.qpsper, parSum);
        if (type == "test_set") {
            pthreads[i] = thread(do_main, client, hash_key2, sort_key2, "set",
                                 nornal_num, ref(configPam), ref(result), ref(lock), expect_state);
        }
        else if (type == "test_get") {
            pthreads[i] = thread(do_main, client, hash_key2, sort_key2, "get",
                                 nornal_num, ref(configPam), ref(result), ref(lock), expect_state);
        }
        else if (type == "test_del") {
            pthreads[i] = thread(do_main, client, hash_key2, sort_key2, "del",
                                 nornal_num, ref(configPam), ref(result), ref(lock), expect_state);
        }
    }
    for(int i = 0; i < threadNum; i++) {
        pthreads[i].join();
    }

}

//test single thread  template
void test_single_model(struct configParam& configPam,
                       std::string type,
                       std::map<std::string, struct allRecord>& result,
                       mutex& lock,
                       int expect_state,
                       const std::string& process_name)
{

    //create_table(table_name);

    irrdb_client* client = rrdb_client_factory::get_client("rrdb.instance0");

    if (type == "test_check_del")
    {
        do_main(client, initString(configPam.hashKeySize - 2) + process_name, initString(configPam.sortKeySize),
                     "check_del", configPam.time*configPam.qps, configPam, result , lock, expect_state);
    }
    else if (type == "test_check_set")
    {
        do_main(client, initString(configPam.hashKeySize - 2) + process_name, initString(configPam.sortKeySize),
                     "check_set", configPam.time*configPam.qps, configPam, result, lock, expect_state);
    }

}

//translate the number to string
std::string trans(double a) {
    std::stringstream tran;
    tran << a;
    std::string result;
    tran >> result;
    return result;
}

std::string get_time() {
    time_t tt = time(NULL);//这句返回的只是一个时间cuo
    tm* t= localtime(&tt);
    std::string time = "";
    time += trans(t->tm_year + 1900) +"-"+ trans(t->tm_mon + 1) + "-" + trans(t->tm_mday) + "-"
            + trans(t->tm_hour) + ":" + trans(t->tm_min) + ":" + trans(t->tm_sec);
    return time;
}

void write_head_file(std::ofstream& out, const std::string type) {
    out << "<h3>" << "This is " + type +" test " << "</h3>";
    out << "<html><head><meta http-equiv='Content-Type' content='text/html; charset=utf8' /><title>xml Translate to html tool</title>"
           << "<style type='text/css'>table{border-collapse:collapse; border-spacing:0; border-left:1px solid #aaa; border-top:1px solid #aaa; background:#efefef;}"
           << ".errorStyle {background-color:red}th{border-right:1px solid #888; border-bottom:1px solid #888; padding:3px 15px; text-align:center; font-weight:bold;"
           << "background:#ccc; font-size:13px;}tr,td{border-right:1px solid #888; border-bottom:1px solid #888; padding:3px 15px; text-align:center; color:#3C3C3C;}"
           << "</style></head><body>";
}

//html format
void write_to_file(std::string str[], int num, std::ofstream& out, std::string type) {
    out << "<tr>";
    for (int i = 0; i < num; i++) {
        out << "<" + type + ">" << str[i] << "</" + type.substr(0,2) + ">";
    }
    out << "</tr>";
}

//write the result data to a file
void write_result(std::map<std::string, struct allRecord>& result,
                  struct configParam& configPam,
                  std::ofstream& out,
                  std::ofstream& out_detail)
{
    int sum = configPam.qps * configPam.time;
    std::string func[5] = {"set", "get", "del", "check_set", "check_del"};
    std::string head_print[][7] = { {"hash_key_size", "sort_key_size", "value_size", "qps", "sum_data", "client_num"},
                                              {trans(configPam.hashKeySize), trans(configPam.sortKeySize), trans(configPam.valueSize), trans(configPam.qps), trans(sum), trans(configPam.client_num) },
                                              {"function", "success_num", "failure_num", "success_ratio", "avg_time(ms)"},
                                              {"function", "fail_type", "fail_num"},
                                              {"error", "hash_key", "sort_key", "value_get", "rel_value", "fail_type"}
                                            };
    out << "<table>";
    write_to_file(head_print[0], 6, out, "th");
    write_to_file(head_print[1], 6, out, "td");
    out << std::endl;
    write_to_file(head_print[2], 5, out, "th");

    for(int i=0; i<5; i++) {
        write_to_file(new std::string[5]{func[i], trans(sum - result[ func[i] ].fail_num), trans(result[ func[i] ].fail_num), trans((double)(sum - result[ func[i] ].fail_num) / sum * 100)+"%",
                    trans(result[ func[i] ].sum_time / sum) }, 5, out, "td");
    }

    bool is_print_head= true;   //judge have the error or not

    for(int i=0; i < 5; i++) {
        struct allRecord temp = result[ func[i] ];
        std::map<std::string, int> rec;
        for(int j = 0; j < temp.fail_num; j++) {
            if(is_print_head) {
                is_print_head = false;
                write_to_file(head_print[3], 3, out, "th class = 'errorStyle' ");

                out_detail << "<table>";
                write_to_file(head_print[0], 6, out_detail, "th");
                write_to_file(head_print[1], 6, out_detail, "td");
                write_to_file(head_print[4], 6, out_detail, "th");
            }
            rec[temp.fail_types[j] ]++;
            write_to_file(new std::string[7]{func[i], temp.hash_keys[j], temp.sort_keys[j], temp.value_gets[j], generateValue(temp.sort_keys[j],
                                                                                                                         configPam.valueSize), temp.fail_types[j]}, 6, out_detail, "td");
        }
        std::map<std::string, int>::iterator it;
        for(it = rec.begin(); it!=rec.end(); it++) {
            write_to_file(new std::string[3]{func[i], it->first, trans(it->second)}, 3, out, "td");
        }
    }
    if(!is_print_head) {
        out_detail << "</table></br>";
    }
    out << "</table></br>";
    out.flush();
    out_detail.flush();
}

//test the persormance or pressure
void test_performance(struct configParam& configPam,
                      const std::string& time,
                      const std::string& process_name)
{

    mutex lock;
    std::ofstream out, out_detail;
    std::string log_path  = configPam.log_path + process_name + "_" + time;
    out.open(log_path + "_summary", ios::out | ios::app);
    out_detail.open(log_path + "_detail", ios::out | ios::app);
    write_head_file(out, configPam.type);
    write_head_file(out_detail, configPam.type);

    while (configPam.qps <= configPam.max_qps && configPam.time <= configPam.max_time) {

        std::map<std::string, struct allRecord> result;
        struct allRecord set_record;
        struct allRecord get_record;
        struct allRecord del_record;

        result["set"] = set_record;
        result["get"] = get_record;
        result["del"] = del_record;

        std::cout<<"*****start this test   qps:"<<configPam.qps<<"    time:"<<configPam.time<<"*****"<<std::endl;
        std::cout<<std::endl<<"[test_set] start"<<std::endl;
        test_multi_model(configPam, "test_set", result , lock, 0, process_name);
        std::cout<<std::endl<<"[test_check_set] start"<<std::endl;
        test_single_model(configPam, "test_check_set", result, lock, 0, process_name);
        std::cout<<std::endl<<"[test_get] start"<<std::endl;
        test_multi_model(configPam, "test_get", result , lock, 0, process_name);
        std::cout<<std::endl<<"[test_del] start"<<std::endl;
        test_multi_model(configPam, "test_del", result , lock, 0, process_name);
        std::cout<<std::endl<<"[test_check_del] start"<<std::endl;
        test_single_model(configPam, "test_check_del", result , lock, dsn::apps::ERROR_NOT_FOUND, process_name);
        std::cout<<std::endl;
        write_result(result, configPam, out, out_detail);
        configPam.qps += configPam.incre_qps;
        configPam.time += configPam.incre_time;
    }

    //use one sentence to describe the result

    out << "</body></html>";
    out_detail << "</body></html>";
    out.close();
    out_detail.close();
}

int main(int argc, char** argv)
{

    struct configParam configPam;
    std::string time = get_time();
    s_pid_str = boost::lexical_cast<std::string>(getpid());
    //unsigned int seed = (unsigned int)::dsn::utils::get_current_rdtsc();
    //srand(seed);
    //ddebug("pid=%s, rand_seed=%u", s_pid_str.c_str(), seed);

    // register replication application provider
    //dsn::replication::register_replica_provider<::pegasus::pegasus_service_impl>("pegasus");

    // register all possible services
    //dsn::register_app<::dsn::replication::replication_service_app>("replica");
    //dsn::register_app<::pegasus::meta_service_app>("meta");

    // register global checker if necesary
//    dsn_register_app_checker("pegasus.checker",
//        ::pegasus::pegasus_checker::create<::pegasus::pegasus_checker>,
//        ::pegasus::pegasus_checker::apply
//        );

    if(argc > 3)
    {
//        // register fake client
//        dsn::register_app<::pegasus::client_fake_app>("client");
//        // specify what services and tools will run in config file, then run
//        dsn_run(argc, argv, true);
    }
    else if (argc == 3)
    {
        std::cout << "Run client ... " << std::endl;

        std::string path = "../../../../rDSN/builder/bin/dsn.ddlclient/";
        std::string command = path + "dsn.ddlclient " + path + "config.ini create_app -name rrdb.instance0  -type rrdb";
        int ret = system(command.c_str());
        if(ret != 0) {
            std::cout << "create table error" << std::endl;
            return -1;
        }

        bool init = rrdb_client_factory::initialize("config.ini");
        if (!init) {
            std::cerr << "Init failed" << std::endl;
            return -1;
        }
        sleep(1);
        std::cout << "Init succeed" << std::endl;

        //std::string table_name = "test_table";
        std::string test_name = argv[1];
        if (test_name == "test_performance") {
            test_performance(configPam, time, argv[2]);
        }
        else if (test_name == "test_set") {
            //test_multi_model(table_name, configPam, "test_set");
        }
        else if (test_name == "test_get") {
            //test_multi_model(table_name, configPam, "test_get");
        }
        else if (test_name == "test_del") {
             //test_multi_model(table_name, configPam, "test_del");
        }
        else if (test_name == "test_check_set") {
            //test_single_model(table_name, configPam, "test_check_set");
        }
        else if (test_name == "test_check_del") {
            //test_single_model(table_name, configPam, "test_check_del");
        }
        else if (test_name == "test_random") {
            //table_name = "test_random_table";
            //create_table(table_name);
            //test_random(table_name);
        }
        else {
            std::cerr << "Invalid test name" << std::endl;
            return -1;
        }
    }
    else
    {
        std::cout << "Run client ... " << std::endl;
        std::string path = "../../../../rDSN/builder/bin/dsn.ddlclient/";
        std::string command = path + "dsn.ddlclient " + path + "config.ini create_app -name rrdb.instance0  -type rrdb";
        int ret = system(command.c_str());
        if(ret != 0) {
            std::cout << "create table error" << std::endl;
            return -1;
        }
        bool init = rrdb_client_factory::initialize("config.ini");
        if (!init) {
            std::cerr << "Init failed" << std::endl;
            return -1;
        }
        sleep(1);
        std::cout << "Init succeed" << std::endl;

        //std::string table_name = "test2";
        //create_table(table_name);
        //put_get_del(table_name);
    }

    return 0;
}

