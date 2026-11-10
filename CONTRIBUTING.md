Contributing
============

_TODO: Move this to a development guide, rather than the contribution guidelines. Include detailed building steps and options as well (like logging levels, debug/release, etc.)_

Coding Style
------------

The project uses `clang-format` to check for coding style. You will need to format the files accordingly before opening a Pull Request.

If you want to format a file, you can use the `clang-format -i -- myfile.c` (where `myfile.c` is the name of the file to format) to format a file in-place.

Alternatively, you can configure your editor to use `clang-format` on save automatically.

Static Analysis and Linting
---------------------------

The project also uses `cppcheck` to check for common mistakes and errors in C code.

It checks for any undefined behavior and dangerous code constructs in an attempt to ensure we aren't introducing bugs into the LibreSplit codebase.

You can run it with `meson test -C build -v` to show the output of the code formatter and analysis tools.

Generating the developer documentation
--------------------------------------

The code is commented in a way that allows the creation of developer documentation using [Doxygen](https://doxygen.nl).

First of all, you'll need to install Doxygen:

- Debian-based systems

    ```sh
    sudo apt update
    sudo apt install doxygen
    ```

- Arch-based systems

    ```sh
    sudo pacman -Sy
    sudo pacman -S doxygen
    ```

Then you can generate the documentation by running:

```sh
doxygen Doxyfile
```

From the root of the project. Doxygen will automatically generate HTML documentation and place it under the `developer_docs/html/` subdirectory.

To give it a quick read, you can use your favourite browser directly or spin up a quick local server using Python:

```sh
python -m http.server --bind 127.0.0.1 5000
```

And then navigating to `localhost:5000`.

---

## Dray's WIP notes on project organization

### Build targets

#### Katana Auto-splitting Timer `timer/`

This is the main program, it contains the core functionality of the timer.
Of particular note: the lua auto splitter, split file management, and the
timer backend.

It also bundles an optional (WIP) frontend UI as a plugin, which allows the
timer to serve as a standalone application.

If building a separate frontend program, it is recommended to be mindful of
the event timestamps made available by the IPC API (WIP) for synchronization.
If a frontend hosts its own timer and there is too much latency in requests,
the timer will appear to split late, even if the time is accurate, which could
lead to complications in run verification.

#### Katana Lib `lib/`

Standalone library that any katana-related program will be built against.
Optionally, standalone frontends may be built on this was well. It contains
common timer structures and behaviors. It is intended to statically link this
library.

#### Katana Plugins `plugins/`

Plugins are early enough in development that this section is hard to assert.
Will allow additional functionality to the main timer directly. Main points of
injection are: (I think, lmao.)
- Additional auto splitter behaviors
- UI Frontends
- IPC interface
- Timer transition hooks

(Links against katana lib.)

#### Katana Controller (CTL) `ctl/`

One such standalone frontend. This issues commands to the main program over its
IPC channel. It also reads the state made available by the IPC.

Any tool that seeks to interface with the timer may do so by interfacing with
the socket directly, or by running the controller utility as a sub-process.
With that use case in mind, this tool is designed to be as minimal as
reasonably possible.

(NOTE to myself -- terrible location, I know -- that one of the IPC commands I
want to add is one that indicates a "hook mode" where, just like ui_notify, if
active, a timer_status message will be sent to the IPC socket for anything
listening. This would be a sister-function to `katana-ctl status` which returns
the current status immediately, instead, it would be `katana-ctl wait` or
something of that sort; the program would exit only once the timer does
something.)

#### Splits Editor `editor/`

Very very premature. This is a placeholder target for what will become a long-
awaited feature. There's nothing there yet. Sorry. :(

### Sources hierarchy

#### `main` (top-level)

The main level is intended to be the aggregator of all sub-behaviors. Thus, any
source at the top-level is either an interface used by or an instance managed
by main, or main itself. This will always be the start and end of the logic.

Main itself should enumerate the top level of the program states. I.E., the
loop in main should make it clear what sub-module is responsible for any
given application behavior. Main itself should not maintain a detailed context,
only hold the references to the instances it needs to track.

#### (Subdirectories, WIP, not sure what all to write down)

Anything that is a sub-feature should be relatively self-contained. It is
allowed to pass logic "up" one level (i.e., to the parent folder) and "down"
but not side-to-side. Any module may reference anything in the lib directly.

Of course, this is unrealistic to keep perfect. Alas, the goal is really to
reduce odd dependency chains and context merging. This is the primary rationale
for the event-broker loop in main at all.

##### `events/`

Detailed logic for the stateful app commands when handled by main. Might change
if this turns out to be excessive. Alas, this handles the "confirm to quit" 
state, which essentially requires the QUIT command twice and thus is stateful.

##### `frontends/`

Standalone UIs. Will become plugins, not quite sure how I want to reformat this
yet.

##### `splitter/`

Autosplitter functionality. Handled exclusively in the main thread so nothing
should break the hierarchy rule here in particular.

### Misc code layout

#### Includes

- Every header file will contain common.h from the lib, thus no source file
will.
- Source files should contain as few local headers as need be, but as many
system headers as they would if including no other local headers.
- Prefer <stdint.h> types whenever width is if concern (rather than long, char,
etc. Note, char is fine for strings or i/o buffers.)

### Katana behavior

(Lot more to write here)

- Something about the seperate frontend / backend
- Core flow is a simple state machine, no need to loop other than the auto-
splitter polling rate
- During any "normal running" mode (i.e. not startup/init or shutdown/release)
the threads should not block each other. In particular, the auto-splitter
should never be blocked during a run.
