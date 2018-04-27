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
*   *Don't rebase:* Rebasing your branch destroys the review history. If a review
    takes a long time, we may ask you to rebase on a more recent *master*, but
    please don't do it without being asked.
*   *types.db:* One of the most common mistakes made by new contributors is the
    addition of (many) new *types* in the file `src/types.db`. The majority of
    usecases can be met with one of the existing entries. If you plan to add new
    entries to `src/types.db`, you should talk to us early in the design
    process.

## Other resources

*   [Mailing list](http://mailman.verplant.org/listinfo/collectd)
*   [#collectd IRC channel](https://webchat.freenode.net/?channels=#collectd)
    on *freenode*.
*   [Old patch submission guideline](https://collectd.org/wiki/index.php/Submitting_patches)
