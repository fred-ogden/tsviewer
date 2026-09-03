# Installing tsviewer

`tsviewer` is a lightweight GTK3 scientific time-series viewer written in C99.

The program is built from source using the supplied `Makefile`.

## Requirements

Building `tsviewer` requires:

- A C compiler supporting C99, such as GCC
- GTK+ 3 development libraries and header files
- Cairo development libraries
- `pkg-config`
- GNU Make
- The standard C math library

GTK3 normally brings in Cairo and the other graphical libraries required by the application.

## Linux

The exact package names depend on the Linux distribution.

### openSUSE / SUSE Linux Enterprise

Install the required development tools and GTK3 development package:

```text
sudo zypper install gcc make pkg-config gtk3-devel
```

### Fedora / RHEL-family distributions

```text
sudo dnf install gcc make pkgconf-pkg-config gtk3-devel
```

### Debian / Ubuntu

```text
sudo apt install build-essential pkg-config libgtk-3-dev
```

## Verify GTK3 development environment

Before building, you can verify that `pkg-config` can locate GTK3:

```text
pkg-config --modversion gtk+-3.0
```

A version number should be printed.

You can also verify the compiler flags detected for GTK3:

```text
pkg-config --cflags gtk+-3.0
```

and the required linker flags:

```text
pkg-config --libs gtk+-3.0
```

If `pkg-config` reports that `gtk+-3.0` cannot be found, the GTK3 development package is either not installed or is not visible in the current `pkg-config` search path.

## Build

Clone the repository:

```text
git clone https://github.com/fred-ogden/tsviewer.git
cd tsviewer
```

Build the program:

```text
make
```

The resulting executable is:

```text
tsviewer
```

Test the executable:

```text
./tsviewer --help
```

## Try the examples

Example data files are included in the `examples` directory.

For example:

```text
./tsviewer examples/model1_temperatures.csv examples/model2_temperatures.csv
```

This opens two model-output data sets simultaneously and provides a useful demonstration of the multi-file comparison capabilities of `tsviewer`.

Additional examples are described in:

```text
examples/README.md
```

## Clean the build

To remove generated build files:

```text
make clean
```

Rebuild with:

```text
make
```

## Installation in your PATH

It is not necessary to install `tsviewer` system-wide. The executable can be run directly from the repository directory.

If desired, copy it to a directory in your shell's `PATH`, for example:

```text
sudo cp tsviewer /usr/local/bin/
```

Afterward it can be invoked from any directory:

```text
tsviewer data.csv
```

Alternatively, individual users can place the executable in a personal `bin` directory without requiring root privileges.

## Runtime environment

`tsviewer` is a graphical GTK3 application and therefore requires a graphical display environment.

On Linux systems it is intended for use under a normal X11 or compatible GTK desktop environment.

When running on a remote machine through SSH, graphical forwarding must be configured if the display is to appear on the local workstation.

For example, depending on the SSH configuration:

```text
ssh -X remote-host
```

or:

```text
ssh -Y remote-host
```

The remote system must also have the necessary GTK3 runtime libraries installed.

## Compiler standard

`tsviewer` is written for ISO C99 and is compiled with:

```text
-std=c99
```

It intentionally uses C99 language and library features and should not be expected to compile as strict C89/ANSI C.

## Problems building

If the build fails because GTK3 cannot be found, first check:

```text
pkg-config --modversion gtk+-3.0
```

If that command fails, install the GTK3 **development** package for your Linux distribution.

Installing only the GTK3 runtime libraries is not sufficient for compiling `tsviewer`; the development headers and `pkg-config` metadata are also required.

For other build problems, please open an issue at:

https://github.com/fred-ogden/tsviewer/issues