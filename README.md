# A toolchain to parse, analyze, and format Zeek scripts

[![Build and test](https://github.com/zeek/zeekscript/actions/workflows/build_wheels.yml/badge.svg)](https://github.com/zeek/zeekscript/actions/workflows/build_wheels.yml)

`zeekscript` is a Python package that provides tooling to operate on [Zeek](https://zeek.org)
scripts. `zeekscript` comes with command line tools that make common tasks accessible,
but its functionality is just an `import zeekscript` away in your own Python tools.

`zeekscript` is powered by [Tree-Sitter](https://tree-sitter.github.io/tree-sitter/),
its [Python bindings](https://github.com/tree-sitter/py-tree-sitter), and our
[tree-sitter-zeek](https://github.com/zeek/tree-sitter-zeek) grammar. When
working from source, make sure to clone this repository recursively in order
to pull in the right version of the grammar and parser.

## Supported platforms and Python versions

`zeekscript` supports Python 3.7+ on Linux, MacOS, and Windows.

## Installation

    $ pip install zeekscript

For local installation, say `pip install .` in a (recursive!) clone of the
repository. Please report any hiccups during the installation as bugs. There's a
lot going on behind the scene in this package, since the `tree_sitter` Python
package compiles the language parser into a native-code shared library on the
fly.

## Usage

### zeek-format

Most significantly, the package includes `zeek-format`, a tool that formats Zeek
scripts. Our philosophy is similar to `gofmt` and the opposite of
`clang-format`: there is only one way to layout Zeek scripts, and this tool
provides it. Accordingly, it features zero options for tweaking the formatting:

```
$ zeek-format --help
usage: zeek-format [-h] [--inplace] [FILES ...]

A Zeek script formatter

positional arguments:
  FILES          Zeek script(s) to process. Use "-" to specify stdin as a filename. Omitting filenames entirely implies reading from stdin.

options:
  -h, --help     show this help message and exit
  --inplace, -i  change provided files instead of writing to stdout
```

In case of processing error, `zeek-format` exits with a non-zero exit code,
reports the trouble it encountered to stderr, and reports its input unchanged.

```
$ echo 'event  foo( a:count ) {print("hi"); }' | zeek-format
event foo(a: count) {
        print ("hi");
}
```

### zeek-script

The `zeek-script` command is the Swiss army knife in the toolbox: it provides
access to a range of script-processing tools (including formatting) via
subcommands. (Okay, so far "range" == two, but expect that to grow in the future.)

```
$ zeek-script --help
usage: zeek-script [-h] {format,parse} ...

A Zeek script analyzer

options:
  -h, --help      show this help message and exit

commands:
  {format,parse}  See `zeek-script <command> -h` for per-command usage info.
    format        Format/indent Zeek scripts
    parse         Show Zeek script parse tree
```

The `parse` command renders its script input as a parse tree. It resembles
`tree-sitter parse`, but shows more context about the relevant snippets of
content, including parsing errors.

```
$ echo 'event zeek_init() { }' | zeek-script parse
source_file (0.0,1.0) 'event zeek_init() { }\n'
    decl (0.0,0.21) 'event zeek_init() { }'
        func_decl (0.0,0.21) 'event zeek_init() { }'
            func_hdr (0.0,0.17) 'event zeek_init()'
                event (0.0,0.17) 'event zeek_init()'
                    event (0.0,0.5)
                    id (0.6,0.15) 'zeek_init'
                    func_params (0.15,0.17) '()'
                        ( (0.15,0.16)
                        ) (0.16,0.17)
            func_body (0.18,0.21) '{ }'
                { (0.18,0.19)
                } (0.20,0.21)
```

See `zeek-script parse --help` for more information.

## Integration into text editors

You can integrate `zeekscript` into any editor that supports the execution of
shell commands on the currently edited files. The relevant `zeekscript` commands
support reading from stdin or filename.

### Emacs

We offer an [Emacs mode](https://github.com/zeek/emacs-zeek-mode) with support
for script formatting and parse tree inspection via keyboard shortcuts.

### vim

The following snippet hooks up `zeek-format` to format the current script:

```
function RunZeekScript()
    " Create a new undo block for reverting formatting without changing cursor
    " position. https://github.com/rhysd/vim-clang-format/pull/55
    silent execute "noautocmd normal! ii\<esc>\"_x"
    let l:save = winsaveview()
    execute "%!zeek-format"
    call winrestview(l:save)
endfunction

nnoremap <silent><buffer> <leader>cf :call RunZeekScript()<CR>
```
