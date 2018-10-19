# Code Review Comments

This is a collection of frequent code review comments, collected here for
reference and discussed in more depth than a typical code review would allow.

The intended use for this document is to point to it from a code review to make
a point quickly while still providing the contributor with enough information
to resolve the issue. For example, a good review comment would be:

![Please initialize variables at declaration. Link to comment.](review_comments_example.png)

A link to each paragraph is provided at the beginning for easy copy'n'pasting.

## Initialize variables

→ [https://collectd.org/review-comments#initialize-variables](https://collectd.org/review-comments#initialize-variables)

Initialize variables when declaring them. By default, C does not initialize
variables when they are declared. If a code path ends up reading the variable
before it is initialized, for example because a loop body is never
executed, it will read random data, causing undefined behavior. Worst case,
pointers will point to random memory causing a segmentation fault.

**Examples:**

```c
/* Initialize scalar with to literal: */
int status = 0;

/* Initialize pointer with function call: */
char *buffer = calloc(1, buffer_size);

/* Initialize struct with struct initializer: */
struct addrinfo ai = {
  .ai_family = AF_UNSPEC,
  .ai_flags = AI_ADDRCONFIG,
  .ai_socktype = SOCK_STREAM,
};

/* Initialize struct with zero: */
struct stat statbuf = {0};
```

In the last example, `{0}` is the universal struct initializer that, in theory,
should be able to zero-initialize any struct. In practise, however, some
compilers don't implement this correctly and will get confused when the first
member is a struct or a union. Our *continuous integration* framework will
catch these cases.

In many cases, this means declaring variables later. For example, this *bad*
example:

```c
int status; /* BAD */

/* other code */

status = function_call();
```

Would become:

```c
/* other code */

int status = function_call(); /* GOOD */
```
