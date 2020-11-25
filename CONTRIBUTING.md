# Contributing

:tada:Thanks for taking the time to contribute!:tada:

The following is a set of guidelines for contributing to hiredis-cluster.

The basics about setting up the project, building and testing is covered in
the [README](README.md).

## Coding conventions

### Code style

Adhere to the existing coding style and make sure to mimic best possible.

### Code formatting

To have a common look-and-feel [clang-format](https://clang.llvm.org/docs/ClangFormat.html)
is used for code formatting. The formatting rules can be applied to the
source code by running the following make target in your build directory:

```sh
$ make format
```

## Submitting changes

* Run the formatter before committing when contributing to this project (`make format`).
* Cover new behaviour with tests when possible.

## Links

* [clang-format](https://apt.llvm.org/) for code formatting
