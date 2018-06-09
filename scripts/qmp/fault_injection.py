# Fault injection helper script based on top of QMP.
#
# Developed by KONRAD Frederic <fred.konrad@greensocs.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.
#

import qmp
import json
import ast
import readline
import sys

def die(cause):
    sys.stderr.write('error: %s\n' % cause)
    sys.exit(1)

class FaultInjectionFramework(qmp.QEMUMonitorProtocol):
    qemu_time = 0
    verbose = 0
    callback = {}

    def print_v(self, msg, level):
        if level <= self.verbose:
            print msg

    def print_qemu_version(self):
        version = self._greeting['QMP']['version']['qemu']
        print 'Connected to QEMU %d.%d.%d\n' % (version['major'],
                                                version['minor'],
                                                version['micro'])

    def __init__(self, address, verbose = 0):
        self.verbose = verbose
        qmp.QEMUMonitorProtocol.__init__(self, self.__get_address(address))

        try:
            self._greeting = qmp.QEMUMonitorProtocol.connect(self)
        except qmp.QMPConnectError:
            die('Didn\'t get QMP greeting message')
        except qmp.QMPCapabilitiesError:
            die('Could not negotiate capabilities')
        except self.error:
            die('Could not connect to %s' % address)

        self.print_qemu_version()
        self._completer = None
        self._pretty = False
        self._transmode = False
        self._actions = list()

    def time_print(self, arg):
        self.print_v('%sns: %s' % (self.qemu_time, arg), 1)

    def send(self, qmpcmd):
        self.print_v(qmpcmd, 2)
        resp = self.cmd_obj(qmpcmd)
        if resp is None:
            die('Disconnected')
        self.print_v(resp, 2)
        return resp

    def cont(self):
        qmpcmd = {'execute': 'cont', 'arguments': {}}
        self.send(qmpcmd)

    def run(self):
        # RUN the simulation.
        self.time_print('Simulation is now running')
        self.cont()
        # Wait for an event to appear
        shutdown_evt = False
        while shutdown_evt == False:
            for ev in self.get_events(True):
                self.print_v(ev, 2)
                if ev['event'] == 'FAULT_EVENT':
                    data = ev['data']
                    self.qemu_time = data['time_ns'];
                    self.callback[data['event_id']]()
                    self.cont()
                elif ev['event'] == 'SHUTDOWN':
                    shutdown_evt = True
            self.clear_events()
        self.close()

    def notify(self, time_ns, cb):
        # Notify a callback at qemu time time_ns
        next_index = len(self.callback)
        elt = 0
        for elt in range(0, next_index + 1):
            if elt == next_index:
                break
            if self.callback[elt] == cb:
                break

        self.callback[elt] = cb
        self.time_print('Notify %s in %sns' % (cb, time_ns))
        qmpcmd = {'execute': 'trigger_event',
                  'arguments': {'event_id': elt,
                                'time_ns': time_ns}}
        self.send(qmpcmd)

    def write(self, address, value, size, cpu, debug = False):
        # write a value
        self.time_print('write: 0x%08x @0x%08x size %s from cpu %s' \
                        %(value, address, size, cpu))
        if type(cpu) is int:
                qmpcmd = {'execute': 'write_mem',
                          'arguments': {'size': size,
                                        'addr': address,
                                        'val': value,
                                        'cpu': cpu,
                                        'debug': debug}}
        else:
                qmpcmd = {'execute': 'write_mem',
                          'arguments': {'size': size,
                                        'addr': address,
                                        'val': value,
                                        'qom': cpu,
                                        'debug': debug}}
        self.send(qmpcmd)

    def read(self, address, size, cpu):
        # Read a value
        self.time_print('read value: @0x%8.8X size %s from cpu %s' \
                        %(address, size, cpu))
        if type(cpu) is int:
            qmpcmd = {'execute': 'read_mem',
                      'arguments': {'size': size,
                                   'addr': address,
                                   'cpu': cpu}}
        else:
            qmpcmd = {'execute': 'read_mem',
                      'arguments': {'size': size,
                                   'addr': address,
                                   'qom': cpu}}
        value = self.send(qmpcmd)['return']
        return value

    def get_qom_property(self, path, property):
        # Get a QOM property
        qmpcmd = {'execute': 'qom-get',
                  'arguments': {'path': path,
                                'property': property}}
        value = self.send(qmpcmd)['return']
        return value

    def set_qom_property(self, path, property, value):
        # Set a QOM property
        qmpcmd = {'execute': 'qom-set',
                  'arguments': {'path': path,
                                'property': property,
                                'value': value}}
        self.send(qmpcmd)

    def set_gpio(self, device_name, gpio, num, value):
        # Set a GPIO
        if gpio != "":
            qmpcmd = {'execute': 'inject_gpio',
                      'arguments': {'device_name': device_name,
                                    'gpio': gpio,
                                    'num': num,
                                    'val': value}}
        else:
            qmpcmd = {'execute': 'inject_gpio',
                      'arguments': {'device_name': device_name,
                                    'num': num,
                                    'val': value}}
        self.send(qmpcmd)

    def help(self):
        print "\nFault Injection Framework Commands"
        print "==================================\n"
        print "cont()"
        print " * Resume the simulation when the Virtual Machine is stopped.\n"
        print "run()"
        print " * Start the simulation when the notify are set.\n"
        print "notify(time_ns, cb)"
        print " * Notify the callback cb in guest time time_ns.\n"
        print "write(address, value, size, cpu, debug)"
        print " * Write @value of size @size at @address from @cpu."
        print " * @cpu can be either a qom path or the cpu id.\n"
        print " * set @debug to True to make a write transaction with"
        print " * debug attributes enabled"
        print "read(address, size, cpu)"
        print " * Read a value of size @size at @address from @cpu."
        print " * @cpu can be either a qom path or the cpu id."
        print " * Returns the value.\n"
        print "get_qom_property(path, property)"
        print " * Get a qom property."
        print " * Returns the qom property named @property in @path.\n"
        print "set_qom_property(path, property, value)"
        print " * Set the property named @property in @path with @value.\n"
        print "set_gpio(path, gpio, num, value)"
        print " * Set the gpio named @gpio number @num in @path with the @val."
        print " * @val is a boolean.\n"

    def __get_address(self, arg):
        """
        Figure out if the argument is in the port:host form, if it's not it's
        probably a file path.
        """
        addr = arg.split(':')
        if len(addr) == 2:
            try:
                port = int(addr[1])
            except ValueError:
                raise QMPShellBadPort
            return ( addr[0], port )
        # socket path
        return arg

