# Ruby class to access collectd daemon through the UNIX socket
# plugin.
#
# Requires collectd to be configured with the unixsock plugin, like so:
#
# LoadPlugin unixsock
# <Plugin unixsock>
#   SocketFile "/var/run/collectd-unixsock"
#   SocketPerms "0775"
# </Plugin>
#
# Copyright (C) 2009 Novell Inc.
# Author: Duncan Mac-Vicar P. <dmacvicar@suse.de>
#
# Inspired in python version:
# Copyright (C) 2008 Clay Loveless <clay@killersoft.com>
#
# This software is provided 'as-is', without any express or implied
# warranty.  In no event will the author be held liable for any damages
# arising from the use of this software.
#
# Permission is granted to anyone to use this software for any purpose,
# including commercial applications, and to alter it and redistribute it
# freely, subject to the following restrictions:
#
# 1. The origin of this software must not be misrepresented; you must not
#    claim that you wrote the original software. If you use this software
#    in a product, an acknowledgment in the product documentation would be
#    appreciated but is not required.
# 2. Altered source versions must be plainly marked as such, and must not be
#    misrepresented as being the original software.
# 3. This notice may not be removed or altered from any source distribution.
#
require 'socket'

# Access to collectd data using the unix socket
# interface
#
# see http://collectd.org/wiki/index.php/Plugin:UnixSock
#
class CollectdUnixSock
  include Socket::Constants

  # initializes the collectd interface
  # path is the location of the collectd
  # unix socket
  #
  # collectd = CollectdUnixSock.new
  #
  def initialize(path='/var/run/collectd-unixsock')
    @socket = UNIXSocket.open(path)
    # @socket = Socket.new(AF_UNIX, SOCK_STREAM, 0)
    # @socket.connect(path)
    @path = path
  end

  # iterates over available values, passing the
  # identifier to the block and the time
  # the data for this identifier was last
  # updated
  #
  # collectd.each_value do |time, identifier|
  #   ...
  # end
  def each_value
    n_lines = cmd("LISTVAL")
    n_lines.times do
      line = @socket.readline
      time_s, identifier = line.split(' ', 2)
      time = Time.at(time_s.to_i)
      yield time, identifier
    end
  end

  # iterates over each value current data
  #
  # collectd.each_value_data('myhost/swap/swap-free') { |col, val| }
  #
  # each iteration gives the column name and the value for it.
  #
  # You can also disable flushing by specifying it as an option:
  #
  # client.each_value_data('tarro/swap/swap-free',
  #                   :flush => false ) do |col, val|
  #    # .. do something with col and val
  # end
  #
  # :flush option is by default true
  #
  def each_value_data(identifier, opts={})
    n_lines = cmd("GETVAL \"#{identifier}\"")
    n_lines.times do
      line = @socket.readline
      col, val = line.split('=', 2)
      yield col, val
    end

    # unless the user explicitly disabled
    # flush...
    unless opts[:flush] == false
      cmd("FLUSH identifier=\"#{identifier}\"")
    end
    
  end
  
  private
  
  # internal command execution
  def cmd(c)
    @socket.write("#{c}\n")
    line = @socket.readline
    status_string, message = line.split(' ', 2)
    status = status_string.to_i
    raise message if status < 0
    status  
  end
  
end

if __FILE__ == $0

  client = CollectdUnixSock.new
  client.each_value do |time, id|
    puts "#{time.to_i} - #{id}"
  end

  client.each_value_data("tarro/cpu-0/cpu-user") do |col, val|
    puts "#{col} -> #{val}"
  end
  
  client.each_value_data("tarro/interface/if_packets-eth0") do |col, val|
    puts "#{col} -> #{val}"
  end
  
end
