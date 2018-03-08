from setuptools import setup
from pkgconfig import _pkginfo

setup(
    name=_pkginfo.name,
    version=_pkginfo.full_version,
    packages=['src', 'src.gluster_plugins'],
    author="Venkata Edara",
    author_email="redara@redhat.com",
    description="library for sending statistics over UDP to collectd servers",
    license="BSD",
    url="https://github.com/collectd/collectd",
    include_package_data=True,
    classifiers=[
        "Programming Language :: Python",
        "Programming Language :: Python :: 2.6",
        "Programming Language :: Python :: 2.7",
        "License :: OSI Approved :: BSD License",
        "Operating System :: OS Independent",
        "Development Status :: 4 - Beta",
        "Intended Audience :: Developers",
        "Topic :: Software Development :: Libraries :: Python Modules",
        "Topic :: System :: Logging",
        "Topic :: System :: Networking",
        "Topic :: System :: Monitoring",
        "Topic :: System :: Networking :: Monitoring",
    ],

    long_description="""\
This Python module implements the binary protocol used by the collectd Network
plugin to let you send gluster metrics to collectd servers. Its based on python
collectd plugin, collectd.d/python.conf file has to be modified to load
gluster plugin.
"""
)
