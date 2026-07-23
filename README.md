# Bash EXtension Manager

Split your messy `.bashrc` into small files. Turn each one on or off
whenever you want.

## The idea

Instead of one giant `.bashrc`, you get:

```
~/.bashrc.d/
├── available/       <- all your modules live here (aliases, prompt, etc.)
└── enabled.conf      <- plain text list: which ones load, and in what order
```

Each module is just a normal shell script. `enabled.conf` decides which
ones actually run when you open a terminal.

## Install

```bash
sudo dpkg -i bexm_0.5_amd64.deb
```

This puts `bexm` on your system and drops one example file at
`~/.bashrc.d/available/welcome`.

## Set it up

```bash
bexm init
source ~/.bashrc
```

`bexm init` adds a small block to your `.bashrc` that:
- loads your enabled modules
- makes `bexm` auto-reload your shell after you enable/disable/move something

You only run this once.

## Everyday commands

```bash
bexm new mymodule       # create a new empty module
bexm enable mymodule 10 # turn it on, load order 10 (lower = earlier)
bexm disable mymodule   # turn it off (keeps the file)
bexm move mymodule 5    # change its load order
bexm list                # see what's available and what's on
bexm order                # see current load order
bexm edit                 # open enabled.conf directly
```

That's it. `enable`, `disable`, and `move` reload your shell for you
automatically. No need to type `source ~/.bashrc` yourself.

## Writing a module

A module is just shell code. Nothing special:

```bash
# ~/.bashrc.d/available/aliases
alias ll='ls -alh'
alias gs='git status'
```

## The config file

`~/.bashrc.d/enabled.conf` is plain text, one module per line:

```
10 aliases
20 prompt
# 30 dashboard    <- commented out = disabled
```

You can edit it by hand any time — `bexm` is just a shortcut for the
common edits.
