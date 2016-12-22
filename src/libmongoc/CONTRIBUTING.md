## Contributing to the MongoDB C Driver

Thank you for your interest in contributing to the MongoDB C driver.

We are building this software together and strongly encourage contributions
from the community that are within the guidelines set forth below.

Bug Fixes and New Features
--------------------------

Before starting to write code, look for existing [tickets]
(https://jira.mongodb.org/browse/CDRIVER) or [create one]
(https://jira.mongodb.org/secure/CreateIssue!default.jspa)
for your bug, issue, or feature request.
This helps the community avoid working on something that might not be of interest
or which has already been addressed.

Pull Requests
-------------

Pull requests should be made against the master (development)
branch and include relevant tests, if applicable. The driver follows
the Git-Flow branching model where the traditional master branch is
known as release and the master (default) branch is considered under
development.

Tests should pass on your environment with the C compiler selected by the build process.
If possible, please run tests on other environments and with other C compilers as well.
Please verify with both of the following

* make test
* scons test

The results of pull request testing will be appended to the request.
If any tests do not pass, or relevant tests are not included,
the pull request will not be accepted.

Talk To Us
----------

We love to hear from you. If you want to work on something or have
questions / complaints please reach out to us by creating a [question]
(https://jira.mongodb.org/secure/CreateIssue.jspa?pid=10005&issuetype=6).
