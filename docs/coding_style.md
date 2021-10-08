# Code Style
While we're very liberal when it comes to coding style, there are a few rules you ought to stick to. They're mostly obvious, but I don't write this down because I'm bored either..

Please note that these are more guidelines than a fixed set of rules. If you have a good reason for breaking one of these points, go ahead. But you may be bothered to explain why you did so.

There is one absolutely unbreakable rule though:
> Programs must be written for people to read, and only incidentally for machines to execute.
— Abelson / Sussman

## Formatting
All code *must* be formatted with [clang-format](https://clang.llvm.org/docs/ClangFormat.html). Since the exact formatting sometimes differs between versions of clang-format, we recommend you use the `contrib/format.sh` shell script which uses the same service for formatting as the check on Github.

## Function and Variable Names
* Names should follow the **snake_case** (lowercase with underscores) naming scheme, e.g.: "`submit_value`", "`temperature_current`". Mixed-case names, as popular with Java developers, should not be used, i.e. don't use "`submitValue`".
* Do not use non-ASCII characters in variable and function names.
* Names should be as long as necessary – not longer, but not shorter either. If in doubt, use the more descriptive (longer) name.
* All-capital names are reserved for, and *should* be used by, defines, macros and enum-members.
* If several variables or functions with similar meaning exist, such as minimum, average and maximum temperature, the common part *should* be in front, e.g. "`temp_max`", "`temp_min`" and so on.
* Non-static functions must be declared in a header file that has the same base name as the .c file defining the function. `static` functions should not have a forward declaration.

## Plugins
* *All* functions within a plugin should be declared `static`. The obvious exception is the "`module_register`" function (see [plugin architecture](https://collectd.org/wiki/index.php/Plugin_architecture)).
* The behavior of a plugin should not depend on compile time settings. If this cannot be guaranteed, for example because the library a plugin uses must be a certain version for an optional feature, this has to be documented in the [collectd.conf(5)](https://collectd.org/documentation/manpages/collectd.conf.5.shtml) manual page.

## Standard Functions
* Only reentrant- and thread-safe functions and libraries may be used.

## Strings
* Many convenience functions are available from **`"common.h"`**.
* The functions `strcpy`, `strcat`, `strtok` and `sprintf` don't take a buffer size and must not be used. If possible, use an alternative from `"common.h"`.
* Instead of `strncpy` use `sstrncpy`. This function assures a null byte at the end of the buffer.
* Only explicitly give the size of the buffer when declaring it. Later, use `sizeof` to get its size in bytes and the `STATIC_ARRAY_SIZE` macro to get the number of elements. For example:
   ```c
   example_t *ex = calloc(1, sizeof(*ex));
   sstrncpy(buffer, "example", sizeof(buffer));
   size_t keys_num = STATIC_ARRAY_SIZE(keys);
   ```

## C Standard
Most of *collectd* is using the C99 standard, but if you'd like to use C11 features for a plugin we're not going to stop you. Regularly used C99 features include:
* Mixed declarations, i.e. defining variables as late as possible. See also: https://collectd.org/review-comments#define-variables-on-first-use
* Designated struct initializers, for example:
   ```c
   /* initialize using designated initializers */
   struct timespec ts = {
     .tv_sec = 2,
     .tv_nsec = 500000000,
   }
   ```
*  Compound literals, for example:
   ```c
   /* Initialize allocated memory: */
   *ptr = (struct example){
     .answer = 42,
   };

   /* Compound literals can also be used to "cast" a gauge_t (and friends) to a value_t: */
   submit("example", (value_t){.gauge = g});
   ```
*  Variable Length Arrays, for example:
   ```c
   char copy[strlen(orig) + 1] = {0};
   ```
* Please do not mix the `// …` and `/* … */` comment styles. Using `/* … */` for the license header and `// …` for everything else is okay.
* Please do not use *flexible array members* (FAM).
* Please use the integer types found in `<stdint.h>` if you need a fixed size integer, e.g. for parsing binary network packets and the like. If you need to print such a variable, please use the printing macros provided by `<inttypes.h>`.
* Feel free to use the `bool` type. Assign only the values `true` and `false` to these variables.

## Miscellaneous
* Do not compare `int` and `size_t` without a cast. Those two types cannot be cast to one another automatically on many platforms.
* Use the `%zu` format when printing `size_t`.

## License Information and Copyright Notice
* All source files must begin with a short license note including a copyright statement. We recommended to copy and adapt the comment from another file.
* Any GPLv2 compatible, OSI approved, free / open-source license is acceptable.
* For new files, we *recommend* to use the [MIT license](https://collectd.org/wiki/index.php/Category:MIT_License).
* *GPL*: Please note that most files in *collectd* are licensed under the terms of the *GPLv2 only*, not the otherwise widely used *GPLv2 or later* schema. If you want to permit the use of your code under the terms of the GPLv3, please adapt the header.
* Please spell your name in the copyright notice as it should be written according to your native language. If you need non-ASCII characters for this, make sure the file is encoded using the UTF-8 character set.
