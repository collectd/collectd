# Contribution guidelines

Thanks for taking the time to contribute to the [collectd
project](https://collectd.org/)! This document tries to give some guidance to
make the process of contributing to *collectd* as pleasant as possible.

## Bug reports

Please report bugs as [GitHub
Issues](https://github.com/collectd/collectd/issues). Try to answer the
following questions:

*   Which version of *collectd* are you using?
*   Which operating system (distribution) are you using at which version?
*   What is the expected behavior / output?
*   What is the actual (observed) behavior / output?
*   How can we reproduce the problem you're having?
*   If *collectd* crashes, try to get a
    [stack trace](https://collectd.org/wiki/index.php/Core_file).

Please monitor your issue for a couple of days and reply to questions. To keep
the project manageable, we have to do some housekeeping; meaning we will close
issues that have become stale.

## Code contributions

Please open a [GitHub Pull Request](https://github.com/collectd/collectd/pulls)
(PR) to contribute bug fixes, features, cleanups, new plugins, … Patches sent to
the mailing list have a tendency to fall through the cracks.

*   *Focus:* Fix *one thing* in your PR. The smaller your change, the faster it
    will be reviewed and merged.
*   *Coding style:* Please run `clang-format -style=file -i $FILE` after editing
    `.c`, `.h` and `.proto` files. If you don't want to install *clang-format*
    locally or your version produces a different result than the formatting
    check on Github, use `contrib/format.sh` to format files using the same web
    service used by our check.
*   *Documentation:* New config options need to be documented in two places: the
    manpage (`src/collectd.conf.pod`) and the example config
    (`src/collectd.conf.in`). New plugins need to be added to the `README` file.
*   *Continuous integration:* Once your PR is created, our continuous
    integration environment will try to build it on a number of platforms. If
    this reports a failure, please investigate and fix the problem. We will at
    best do a very casual review for failing PRs.
*   *Don't rebase:* Rebasing your branch destroys the review history. If a
    review takes a long time, we may ask you to rebase on a more recent
    *master*, but please don't do that without being asked.
*   *types.db:* One of the most common mistakes made by new contributors is the
    addition of (many) new *types* in the file `src/types.db`. The majority of
    usecases can be met with one of the existing entries. If you plan to add new
    entries to `src/types.db`, you should talk to us early in the design
    process.

### ChangeLog

PRs need to have a one-line summary in the *PR description*. This information
is used to automatically generate release notes. If you got here after creating
the PR, you need to go to the *PR description* (shown as the first "comment" on
the PR, made by yourself) and *edit* that description. Editing a PR will
trigger the "ChangeLog" status to be updated.

For the summary itself, follow this style:

```
ChangeLog: Foo plugin: A specific issue people had has been fixed.
```

The summary must be on a line of its own, with a "ChangeLog:" prefix at the
beginning of the line. The text should start with "Foo plugin" to give the
reader context for the information. Other common contexts are "collectd" for
the core daemon, "Build system", and "Documentation". Use past tense and
passive voice the for remainder, e.g. "a bug has been fixed", "a feature has
been added".

Some PRs should be excluded from the release notes, e.g. changes to project
internal documentation (such as this file). Those changes are not interesting
for external users of the project and would reduce the value of the release
notes. Maintainers may use the `Unlisted Change` label to mark those PRs.

## Other resources

*   [Mailing list](http://mailman.verplant.org/listinfo/collectd)
*   [#collectd IRC channel](https://webchat.freenode.net/?channels=#collectd)
    on *freenode*.
*   [Old patch submission guideline](https://collectd.org/wiki/index.php/Submitting_patches)
