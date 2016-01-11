#!/usr/bin/env python
# coding=utf-8
#
# Author : wengcj@rd.netease.com
# Created: 2015 Jun 08 Mon 05:25:59 PM CST

import os
import time
import glob
import json
import logging
LOG_FORMAT = "%(asctime)-15s|%(levelname)5s|%(message)s"
logging.basicConfig(level=logging.DEBUG, format=LOG_FORMAT)
log = logging.getLogger()

class CounterReporter(object):
    """ watch /dev/shm/counters, report counter to falcon system
    """
    def __init__(self, report_interval=30):
        self.counter_root_ = "/dev/shm/counters"
        self.report_interval_ = report_interval
        self.report_format_ = "falcon"
        self.report_server_ = "local_falcon_server"

    def SetReportFormat(self, format):
        self.report_format_ = format

    def SetReportServer(self, server):
        self.report_server_ = server

    def Report(self):
        while True:
            try:
                begin= time.time()
                self.DoReport()
                end = time.time()
                sleep_time = self.report_interval_-(end-begin)
                log.info("sleep %s seconds"%sleep_time)
                time.sleep(sleep_time)
            except KeyboardInterrupt:
                break
            except:
                log.exception('report')

    def DeleteEmptyTable(self, table_path):
        files_in_table = glob.glob("%s/*"%(table_path))
        if not files_in_table:
            log.info("delete %s"%(table_path))
            os.rmdir(table_path)

    def DoReport(self):
        tables = glob.glob("%s/*"%self.counter_root_)
        if not tables:
            log.info("no tables in %s"%(self.counter_root_))
            return
        for table in tables:
            self.DeleteEmptyTable(table)
            table_name = os.path.basename(table)
            cmd = "./bin/ccollector %s %s | bin/counter_reporter.py %s"%(self.report_format_, table_name, self.report_server_)
            log.info(cmd)
            os.system(cmd)
def main():
    conf_file = "conf/collector.conf"
    with open(conf_file, "r") as f:
	conf = json.load(f)
    print conf
    reporter = CounterReporter()
    reporter.SetReportFormat(conf["report_format"])
    reporter.SetReportServer(conf["report_server"])
    reporter.Report()
if __name__=="__main__":
    main()

