## Maintainer Guide

This document documents best practises and guidelines for *collectd*
maintainers.

### Ideology

As maintainer of an open-source project, you are one of the most knowledgable
people of the project's structure, best practices, goals, etc. You are most
helping the project by *facilitating change*, in other words "help contributors
make changes to the codebase."

The most common form of helping users is doing *code reviews* and (eventually)
using your commit rights to merge the pull request.

### Code reviews

*   Be friendly, especially with new contributors. Write "Hi" and thank them for their contribution before diving into review comments.
*   Criticize code, not people. Ideally, tell the contributor a better way to do what they need.
*   Clearly mark optional suggestions as such. Best practise, start your comment with *At your option: …*
*   Wait for a successful run of our [continuous integration system](https://ci.collectd.org/) before merging.

### Repository access

You have write access to the *collectd/collectd* repository. Please use it
responsibly.

#### Own work

Open *pull requests* for your own changes, too:

*   For simple changes it's okay to self-approve and merge after a
    successful build on the CI systems.
*   Trivial changes, cherry-picks from *master* and roll-up merges are
    excempt and may be pushed to the version branches and *master* directly.
*   "Simple" and "trivial" are not further defined; use your best judgement.
    We'll revisit this if and when it becomes necessary.
