#!/usr/bin/env python
# coding=utf-8
#
# Copyright © 2015 Jacky Hu <hudayou@hotmail.com>
#
# Plugin to collect statistics from vCenter
#

import argparse
import atexit
from datetime import timedelta
from time import mktime
import getpass
import re
try:
    from urllib.parse import unquote
except ImportError:
    from urllib import unquote

from pyVim import connect
from pyVmomi import vmodl
from pyVmomi import vim


def buildArgParser():
    """
    Builds a standard argument parser with arguments for talking to vCenter

    -s service_host_name_or_ip
    -o optional_port_number
    -u required_user
    -p optional_password

    """
    parser = argparse.ArgumentParser(
        description='Standard Arguments for talking to vCenter')

    # because -h is reserved for 'help' we use -s for service
    parser.add_argument('-s', '--host',
                        required=True,
                        action='store',
                        help='vSphere service to connect to')

    # because we want -p for password, we use -o for port
    parser.add_argument('-o', '--port',
                        type=int,
                        default=443,
                        action='store',
                        help='Port to connect on')

    parser.add_argument('-u', '--user',
                        required=True,
                        action='store',
                        help='User name to use when connecting to host')

    parser.add_argument('-p', '--password',
                        required=False,
                        action='store',
                        help='Password to use when connecting to host')
    return parser


def promptForPassword(args):
    """
    if no password is specified on the command line, prompt for it
    """
    if not args.password:
        args.password = getpass.getpass(
            prompt='Enter password for host %s and user %s: ' %
                   (args.host, args.user)
        )
    return args


def getArgs():
    """
    Supports the command-line arguments needed to form a connection to vSphere.
    """
    parser = buildArgParser()

    args = parser.parse_args()

    return promptForPassword(args)


class VCenterStat(object):

    def __init__(self, host=None, port=443, user=None, password=None):
        self.host = host
        self.port = port
        self.user = user
        self.password = password
        # Stolen from
        # http://stackoverflow.com/questions/5461322/python-check-if-ip-or-dns
        self.ip4Pattern = (
            "^(([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])"
            "\.){3}([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])$"
        )
        self.hostPattern = (
            "^(([a-zA-Z]|[a-zA-Z][a-zA-Z0-9\-]*[a-zA-Z0-9])"
            "\.)*([A-Za-z]|[A-Za-z][A-Za-z0-9\-]*[A-Za-z0-9])$"
        )

    def read(self):
        try:
            serviceInstance = connect.SmartConnect(host=self.host,
                                                   user=self.user,
                                                   pwd=self.password,
                                                   port=int(self.port))

            if not serviceInstance:
                print("Could not connect to the specified host using specified "
                      "username and password")
                return -1

            atexit.register(connect.Disconnect, serviceInstance)

            perfDict = self.readStats(serviceInstance)
            for entity, metrics in perfDict.items():
                print("#")
                print("# %s" % entity)
                print("#")
                for m in metrics:
                    print("%s,%s,%s,%s,%s,%s" %
                          (m['interval'],
                           m['time'],
                           m['type'],
                           m['name'],
                           m['value'],
                           m['unit']))

        except vmodl.MethodFault as e:
            print("Caught vmodl fault : " + e.msg)
            return -1

        return 0

    def nomarlizeName(self, name):
        """
        Normarlize names to exclude characters not allowed in colllectd,
        graphite and grafana
        """
        if re.match(self.ip4Pattern, name):
            return name.replace('.', '_')
        elif re.match(self.hostPattern, name):
            name = name.split('.')[0]
            return name.replace('-', '_')
        else:
            name = unquote(name)
            for c in ['.', '\0', '%', '\\', '/', '-', ' ', ',', ';', '#']:
                if c in name:
                    name = name.replace(c, '_')
            return name

    def resolveFullName(self, entity, name=None):
        """ Resolve entity name to contain it's hierarchy """
        if entity.parent is None:
            return name
        entityParent = entity.parent
        if isinstance(entity, vim.VirtualMachine):
            entityParent = entity.summary.runtime.host
        elif isinstance(entity, vim.HostSystem):
            pass
        elif isinstance(entity, vim.ClusterComputeResource):
            pass
        elif isinstance(entity, vim.Datacenter):
            pass
        else:
            return self.resolveFullName(entityParent, name)
        entityName = self.nomarlizeName(entity.name)
        if name is not None:
            name = "%s#%s" % (entityName, name)
        else:
            name = entityName
        return self.resolveFullName(entityParent, name)

    def resolveFullNameLoop(self, entity):
        """ Loop implementation of resolveFullName """
        name = None
        while entity.parent is not None:
            entityParent = entity.parent
            if isinstance(entity, vim.VirtualMachine):
                entityParent = entity.summary.runtime.host
            elif isinstance(entity, vim.HostSystem):
                pass
            elif isinstance(entity, vim.ClusterComputeResource):
                pass
            elif isinstance(entity, vim.Datacenter):
                pass
            else:
                entity = entityParent
                continue
            entityName = self.nomarlizeName(entity.name)
            if name is not None:
                name = "%s#%s" % (entityName, name)
            else:
                name = entityName
            entity = entityParent
        return name

    def readStats(self, serviceInstance):
        """ Read performance metrics """
        content = serviceInstance.RetrieveContent()
        objectView = content.viewManager.CreateContainerView(
                content.rootFolder,
                [
                    vim.ClusterComputeResource,
                    vim.Datacenter,
                    vim.HostSystem,
                    vim.VirtualMachine
                ],
                True)
        perfManager = content.perfManager
        perfCounterInfoList = content.perfManager.perfCounter
        perfCounterList = {}
        for counter in perfCounterInfoList:
            counterName = "{}#{}#{}".format(
                    counter.groupInfo.key,
                    counter.nameInfo.key,
                    counter.rollupType)
            counterType = counter.statsType
            counterId = counter.key
            counterUnit = counter.unitInfo.key
            perfCounterList[counterId] = {
                    'unit': counterUnit,
                    'name': counterName,
                    'type': counterType}
        perfDict = {}
        endTime = serviceInstance.CurrentTime()
        for entity in objectView.view:
            fullEntityName = self.resolveFullName(entity)
            perfProviderSummary = perfManager.QueryPerfProviderSummary(entity)
            if perfProviderSummary.currentSupported:
                # Real time statistics
                intervalId = perfProviderSummary.refreshRate
            elif perfProviderSummary.summarySupported:
                # Historical statistics
                intervalId = perfManager.historicalInterval[0].samplingPeriod
            metricIds = perfManager.QueryAvailablePerfMetric(
                    entity=entity,
                    intervalId=intervalId)
            aggregateMetricIds = [m for m in metricIds if m.instance == ""]
            startTime = endTime - timedelta(seconds=intervalId)
            querySpec = vim.PerformanceManager.QuerySpec(
                    intervalId=intervalId,
                    entity=entity,
                    metricId=aggregateMetricIds,
                    maxSample=1,
                    startTime=startTime,
                    endTime=endTime)
            perfMetricList = perfManager.QueryPerf(querySpec=[querySpec])
            # It should contain only one element since we specified only one
            # entity
            if len(perfMetricList) == 1:
                perfMetric = perfMetricList[0]
                perfDict[fullEntityName] = []
                try:
                    sampleInfo = perfMetric.sampleInfo[0]
                except IndexError:
                    continue
                interval = sampleInfo.interval
                # convert datetime to unix timestamp
                time = sampleInfo.timestamp
                time = mktime(time.timetuple())
                for v in perfMetric.value:
                    # In case there is no value, handle it gracefully
                    try:
                        value = v.value[0]
                    except IndexError:
                        continue
                    if perfCounterList[v.id.counterId]['unit'] == 'percent':
                        value = value * 0.01
                    perfDict[fullEntityName].append({
                            'interval': interval,
                            'time': time,
                            'unit': perfCounterList[v.id.counterId]['unit'],
                            'name': perfCounterList[v.id.counterId]['name'],
                            'type': perfCounterList[v.id.counterId]['type'],
                            'value': value})
        objectView.Destroy()
        return perfDict


class VCenter(object):

    def __init__(self):
        self.pluginName = "vcenter"
        self.host = None
        self.port = 443
        self.user = None
        self.password = None
        self.serviceInstance = None

    def submit(self, pluginInstance, typeName, typeInstance, value):
        v = collectd.Values()
        v.host = self.host
        v.plugin = self.pluginName
        v.plugin_instance = pluginInstance
        v.type = typeName
        v.type_instance = typeInstance
        v.values = [value, ]
        v.dispatch()

    def init(self):
        """ collectd init callback """
        try:
            self.serviceInstance = connect.SmartConnect(host=self.host,
                                                        user=self.user,
                                                        pwd=self.password,
                                                        port=int(self.port))

            if not self.serviceInstance:
                collectd.error("Could not connect to the specified host "
                               "using specified username and password")

        except vmodl.MethodFault as e:
            collectd.error("Caught vmodl fault : " + e.msg)

        return

    def read(self):
        """ collectd read callback """
        vCenterStat = VCenterStat()
        perfDict = vCenterStat.readStats(self.serviceInstance)
        for entity, metrics in perfDict.items():
            for m in metrics:
                self.submit(entity, m['type'], m['name'], m['value'])

    def shutdown(self):
        """ collectd shutdown callback """
        connect.Disconnect(self.serviceInstance)

    def config(self, obj):
        """ collectd config callback """
        for node in obj.children:
            if node.key == 'Port':
                self.port = int(node.values[0])
            elif node.key == 'Host':
                self.host = node.values[0]
            elif node.key == 'User':
                self.user = node.values[0]
            elif node.key == 'Password':
                self.password = node.values[0]
            else:
                collectd.warning(
                    "vcenter plugin: Unkown configuration key %s" %
                    node.key
                )


def main():
    args = getArgs()
    vCenterStat = VCenterStat(args.host, args.port, args.user, args.password)
    return vCenterStat.read()

if __name__ == '__main__':
    main()
else:
    import collectd

    vcenter = VCenter()
    collectd.register_init(vcenter.init)
    collectd.register_shutdown(vcenter.shutdown)
    collectd.register_read(vcenter.read)
    collectd.register_config(vcenter.config)
