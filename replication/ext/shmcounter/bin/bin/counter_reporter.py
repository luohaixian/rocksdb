#!/usr/bin/env python
# coding=utf-8
#
# Author : wengcj@rd.netease.com
# Created: 2015 Jun 09 Tue 08:24:36 PM CST

##
#  counter_reporter.py用来把json格式的perf counter报告给falcon系统
#
#  由于历史原因，有两种格式的perf counter
#  1 old格式，目前接受这种格式有frontend-agent-proxy,
#    对应CONF中的配置项是online, staging,
#  2 falon格式，这是目前最新的以后逐步都往这个格式转移
#    会直接向http://127.0.0.1:1988/v1/push POST数据
#
#  counter_reporter.py
#    接受一个参数用来选择post数据的接口
#    从stdin读入json格式的counter，每行一个json
#    日志打印到stderr
#

import sys
import json
import urllib
import logging
import httplib
import fileinput

USAGE="""
usage:
    echo "perf json" | counter_reporter.py [falcon|online|staging]
"""
LOG_FORMAT = "%(asctime)-15s|%(levelname)5s|%(message)s"
logging.basicConfig(level=logging.DEBUG, format=LOG_FORMAT)
log = logging.getLogger()

# https://10.105.200.82:443/manager/perfcounter"
# http://127.0.0.1:1988/v1/push
CONF= {
    "online_proxy_server": ["https", "10.105.200.82", "443", "/manager/perfcounter"],
    "local_falcon_server": ["http", "127.0.0.1",  "1988", "/v1/push"],
    "staging_proxy_server": ["http", "10.237.14.206", "12080", "/manager/perfcounter"],
}
def is_json(s):
    try:
        json.loads(s)
    except:
        return False
    return True

def https_post(host, port, path, content):
    CERTFILE = 'conf/server.crt'
    KEYFILE = 'conf/server.key'
    params = urllib.urlencode({'pf': urllib.quote_plus(content)})
    headers = {'Content-type': 'application/x-www-form-urlencoded'}
    conn = httplib.HTTPSConnection(
                host=host,
                port=port,
                #key_file = KEYFILE,
                #cert_file = CERTFILE
                )
    conn.request('POST', path, params, headers)
    response = conn.getresponse()
    return response.read()

def http_post(host, port, path, content):
    headers = {'Content-type': 'application/x-www-form-urlencoded'}

    conn = httplib.HTTPConnection(
                host=host,
                port=port,
                )
    conn.request('POST', path, content, headers)
    response = conn.getresponse()
    return response.read()

def do_report(method, host, port, path):
    while True:
        line = "init"
        try:
            line = raw_input()
        except EOFError:
            break
        log.info("read from stdin:%s"%( line))
        response = "init"
        if not is_json(line):
            log.warn("not a json:%s"%line)
            continue
	else:
	    parsed = json.loads(line)
	    out = json.dumps(parsed, sort_keys=True, indent=4)
	    log.info("falcon:\n%s"%out)
        if method == 'http':
            response = http_post(host, port, path, line.strip())
        elif method == 'https':
            response = https_post(host, port, path, line.strip())
        else:
            log.warn("unknown method:%s"%method)
            return -1
        log.info("request=%s"%(line))
        log.info("response=%s"%(response))
        log.info("server=%s://%s:%s/%s"%(method, host, port, path))


def main():
    # parse command line options here
    if len(sys.argv) < 2:
        log.error("arg number less than 2\n%s"%USAGE)
        sys.exit(-1)
    conf_name = sys.argv[1]
    if conf_name not in CONF:
        log.error("unknown conf_name=%s\n%s"%(conf_name,USAGE))
        sys.exit(-1)
    method = CONF[conf_name][0]
    host   = CONF[conf_name][1]
    port   = CONF[conf_name][2]
    path   = CONF[conf_name][3]
    do_report(method, host, port, path)

if __name__=="__main__":
    main()

