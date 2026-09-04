# tsviewer

**An interactive scientific time-series viewer designed for rapid comparison of observational data and model output.**

`tsviewer` is a lightweight GTK3 application for plotting, inspecting, and comparing columnar scientific data sets.

It was developed by **Fred L. Ogden** from decades of experience working with environmental observations, hydrologic models, numerical experiments, and model-development workflows. The design reflects a recurring practical problem:

> When comparing two versions of a model, changes in parameter value, observations against simulations, or the output before and after a code change, the fastest way to understand what happened is often to look carefully at the time series.

Many plotting packages can produce beautiful publication figures. `tsviewer` is aimed at a different job: **getting scientific data on the screen quickly, navigating it interactively, and making differences between data sets easy to find.**

It is particularly useful during model development and regression testing, where the important questions are often very simple:

- Are these two outputs identical?
- If not, when do they first diverge?
- Is the difference systematic or confined to a few events?
- Which state variables or fluxes changed?
- Are differences small numerical perturbations or meaningful changes in model behavior?
- How well does a modeled series reproduce a reference or observed series?

`tsviewer` was built to answer those questions with as little friction as possible.

## Features

### Rapid multi-file comparison

Load as many as four data files simultaneously:

```text
tsviewer file1 [file2] [file3] [file4]
```

The first file normally serves as the reference or observational data set, while additional files can contain alternative model runs, implementations, parameter sets, or other comparison data.

Variables can be selected independently or paired automatically across files.

When **Pair sets** is enabled, matching variables are synchronized across the loaded files. The corresponding variable in File 1 becomes the statistical reference, while matching selected variables in Files 2-4 are treated as model series.

This makes it easy to move rapidly through large collections of model output variables without repeatedly configuring each comparison.

### Designed to expose differences

For multi-file plots:

- **Color identifies the variable.**
- **Line style identifies the source file.**

Matching variables can therefore be superimposed directly.

When two model implementations produce identical output, their traces lie on top of one another. When they differ, the separation is immediately visible.

This has proven especially useful for:

- regression testing;
- model refactoring;
- numerical-method comparisons;
- standalone-versus-coupled model comparisons;
- parameter sensitivity experiments;
- calibration diagnostics;
- observational/model comparisons; and
- finding exactly when two simulations begin to diverge.

### Interactive navigation

Large data sets are intended to be explored rather than reduced to a static figure.

The plot supports:

- **Mouse wheel** - zoom in or out around the cursor;
- **Left-click and drag** - select and zoom to an interval;
- **Horizontal scroll bar** - pan through a zoomed data set;
- **Right-click** - return to the full record;
- **R** - return to the full record;
- **Esc** - quit.

The cursor position and current viewing interval are displayed interactively.

This makes it practical to begin with years of data, identify an interesting period, and rapidly drill down to individual events or time steps.

### Reference/model statistics

Any displayed series can be explicitly designated as the **Reference** and another as the **Model**.

For the selected or visible interval, `tsviewer` reports basic statistics for the reference series and computes:

- **Nash-Sutcliffe Efficiency (NSE)**
- **Kling-Gupta Efficiency (KGE)**

In paired multi-file mode, one reference series can be compared automatically against the corresponding series from several model files.

NSE and KGE are calculated only when the coordinates match exactly. `tsviewer` deliberately performs **no interpolation** when calculating these statistics.

That behavior is intentional: the program does not silently alter the data in order to make two series comparable.

### Flexible Y-axis display

The viewer provides:

- all values;
- positive values only;
- negative values only;
- logarithmic Y-axis display; and
- cumulative integration.

The **Integrate** option performs trapezoidal integration along the abscissa. For calendar time series, the integration interval is expressed in hours.

These controls are useful when examining fluxes, accumulated quantities, water-balance terms, discharge spanning several orders of magnitude, or variables containing both positive and negative values.

## Input philosophy

One of the principal design goals of `tsviewer` is:

**Do not make the user reformat a perfectly reasonable scientific data file merely to plot it.**

The program therefore attempts to recognize common columnar ASCII formats automatically.

Supported input includes:

- generic delimited text with headers;
- generic delimited text without headers;
- Campbell Scientific TOA5 files;
- AmeriFlux-style timestamps;
- comma-delimited files;
- tab-delimited files;
- pipe-delimited files;
- whitespace-delimited files; and
- comment lines beginning with `#`.

Daily, sub-daily, monthly, annual, and generic numeric-coordinate data can be displayed.

## Recognized coordinates

`tsviewer` recognizes a variety of time coordinates, including:

```text
YYYY-MM-DD
YYYY-MM-DD HH:MM
YYYY-MM-DD HH:MM:SS
YYYYMMDDHHMM
YYYYMMDDHHMMSS
Julian Date
Modified Julian Date
Unix epoch seconds
Unix epoch milliseconds
year,month
year
```

A recognized timestamp may appear in the first or second column.

Generic finite numeric values in the first column are also supported. These can represent, for example:

- time step;
- elapsed time;
- simulation time;
- distance;
- fractional day; or
- an arbitrary numeric coordinate.

For a generic numeric abscissa, `tsviewer` deliberately does **not** invent units, a sampling interval, a time origin, or calendar meaning.

Calendar timestamps are treated as timezone-independent civil time so that plotting does not change because of the computer's local timezone or daylight-saving-time rules.

## Example file layouts

Typical files include:

```text
time,precipitation,runoff,soil_moisture
2026-01-01 00:00:00,0.0,0.0021,0.314
2026-01-01 01:00:00,1.2,0.0024,0.316
2026-01-01 02:00:00,4.7,0.0048,0.325
```

or:

```text
JD,value1,value2
2461041.5000,12.3,14.1
2461041.5417,12.7,14.4
2461041.5833,13.2,14.8
```

or simply:

```text
step value1 value2
1    0.125  0.126
2    0.137  0.137
3    0.151  0.149
```

The intent is that common scientific output should normally be viewable directly.

## Building

`tsviewer` is written in ISO C99 and uses GTK3 and Cairo.

On a Linux system with the GTK3 development package and `pkg-config` installed:

```text
make
```

The supplied Makefile builds:

```text
tsviewer
```

The equivalent compilation uses:

```text
gcc -Wall -Wextra -O2 -std=c99 -pedantic \
    $(pkg-config --cflags gtk+-3.0) \
    -o tsviewer main.c \
    $(pkg-config --libs gtk+-3.0) -lm
```

To remove the executable and object files:

```text
make clean
```

### Dependencies

- C99 compiler
- GTK+ 3
- Cairo
- `pkg-config`
- standard C math library

## Running

Display a single data set:

```text
./tsviewer observations.csv
```

Compare observations with one model:

```text
./tsviewer observations.csv model.csv
```

Compare several model runs:

```text
./tsviewer observations.csv model_A.csv model_B.csv model_C.csv
```

Display command-line help:

```text
./tsviewer --help
```

or:

```text
./tsviewer -h
```

## A model-development tool

Although `tsviewer` began as a program for viewing Campbell Scientific TOA5 data, it evolved into a general scientific time-series comparison tool.

Its development has been strongly influenced by hydrologic and environmental model development, where a programmer may need to compare dozens of output variables across thousands or hundreds of thousands of time steps.

A typical use is remarkably simple:

```text
./tsviewer old_version.csv new_version.csv
```

Select a variable, enable **Pair sets**, and zoom into any region where the curves separate.

That workflow can reveal in seconds what a numerical diff or summary statistic may take much longer to explain.

Conversely, when two curves remain perfectly superimposed through a long simulation, that is powerful visual evidence that a code modification has preserved model behavior. Exact identity should of course be established with an appropriate numerical or bitwise comparison when required; `tsviewer` provides the rapid visual reconnaissance that tells the developer where to look.

## Design principles

`tsviewer` intentionally remains a relatively small, native scientific utility rather than attempting to become a general plotting framework.

Its design priorities are:

1. **Get the data on the screen quickly.**
2. **Recognize common scientific data formats automatically.**
3. **Make comparison of multiple data sets effortless.**
4. **Provide useful interactive navigation for long records.**
5. **Expose differences rather than obscure them.**
6. **Never silently interpolate or modify input data.**
7. **Require minimal configuration.**
8. **Remain fast enough to use routinely during model development.**

The program is meant to be the plotting tool that is easy enough to invoke repeatedly while debugging, testing, calibrating, or simply asking:

**"What did the model actually do?"**

## Development history

`tsviewer` has evolved incrementally as real scientific-data comparison needs arose:

- **v0.1-v0.3** - Basic CSV plotting
- **v0.4** - Multiple file formats and statistics
- **v0.5** - Generalized parser and monthly/annual data
- **v0.6** - Y-axis modes and logarithmic scaling
- **v0.7** - Multi-file comparison and NSE/KGE
- **v0.7.1** - Generic numeric abscissa support
- **v0.7.2** - Timezone-independent calendar handling, nonfatal abscissa reversals, and improved redraw performance
- **v0.8** - Set-selection controls and paired multi-file selection
- **v0.9** - First- or second-column time-coordinate detection
- **v1.0** - Explicit reference/model selection and multi-model NSE/KGE evaluation
- **v1.01** - Screen-relative initial window sizing and stable status layout
- **v1.02** - Mouse-wheel zoom centered on the cursor and constrained to data extents
- **v1.03** - Improved series-panel layout and automatic paired statistical-role selection
- **v1.04** - Added built-in F1 help; improved Pair sets Ref/Model visual
              feedback; added NSE_log(x) and KGE_log(x) evaluation for positive data
              displayed on the logarithmic Y axis; log display now operates on the
              selected interval without rejecting series because of non-positive values
              elsewhere in the record; compiler-warning cleanup.

## Author

**Fred L. Ogden**

Conceived and developed by Fred L. Ogden, with considerable programming assistance from ChatGPT.

The program reflects the author's experience with scientific observation, hydrologic modeling, numerical model development, calibration, regression testing, and the practical problem of comparing large quantities of model output efficiently.

## License

Copyright 2026 Fred L. Ogden

Licensed under the **Apache License, Version 2.0**.

See `LICENSE-2.0.txt` for the full license text.
