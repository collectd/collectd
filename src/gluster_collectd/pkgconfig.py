# Copyright (c) 2012-2013 Red Hat, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
# implied.
# See the License for the specific language governing permissions and
# limitations under the License.


class PkgInfo(object):
    def __init__(self, canonical_version, release, name, final):
        self.canonical_version = canonical_version
        self.release = release
        self.name = name
        self.final = final
        self.full_version = self.canonical_version + '-' + self.release

    def save_config(self, filename):
        """
        Creates a file with the package configuration which can be sourced by
        a bash script.
        """
        with open(filename, 'w') as fd:
            fd.write("NAME=%s\n" % self.name)
            fd.write("VERSION=%s\n" % self.canonical_version)
            fd.write("RELEASE=%s\n" % self.release)

    @property
    def pretty_version(self):
        if self.final:
            return self.canonical_version
        else:
            return '%s-dev' % (self.canonical_version,)


#
# Change the Package version here
#
_pkginfo = PkgInfo('1.0.0', '0', 'gluster_collectd', False)
__version__ = _pkginfo.pretty_version
__canonical_version__ = _pkginfo.canonical_version

PKGCONFIG = 'pkgconfig.in'
_pkginfo.save_config(PKGCONFIG)
