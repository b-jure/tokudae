## Build & Installation

### Linux, Mingw (possibly other UNIX) and POSIX platforms
GNU `make` is used for building from source, make sure you have it installed.

`PLATFORM` variable in the following examples should be one of:
`linux`, `linux_readline`, `mingw` or `posix`.
If `PLATFORM` is not defined (or is an empty string),
then `linux_readline` is assumed.

You are free to modify the configuration files `config_*.mk`
(where `*` is your `PLATFORM`) in order to configure installation root,
installation program, compiler, compiler flags, archiver, binary/library
targets and other utility programs.

Before building do:
```sh
PLATFORM=linux make echo
```
in order to see if the build configuration is what you expect it to be.

Building the binary and library:
```sh
PLATFORM=linux make
```

Installing onto the system (add `sudo` in front if you lack the permission):
```sh
PLATFORM=linux make install
```

Local Installation (in the current directory under `local` directory):
```sh
PLATFORM=linux make local
```

Uninstalling (add `sudo` in front if you lack the permission):
```sh
PLATFORM=linux make uninstall
```

Clean build files and targets:
```sh
PLATFORM=linux make clean
```

For help on other make targets do:
```sh
make help
```

### Windows
For Windows the `winmake.bat` is provided.

If needed, all build and installation utilities inside the `winmake.bat` can
be changed, by default the script expects to be executed in powershell under
administration privileges and with access to `cl` (MSVC compiler) and `link`
(MSVC linker).

If you are missing `cl` and `link` programs you should download Visual Studio
(`https://visualstudio.microsoft.com/`) and install it, you should then
have Visual Studio powershell that has `cl` and `link` in it's path.

Building the binary and library files:
```sh
.\winmake.bat
```

Installing onto the system (might require administrator privileges):
```sh
.\winmake.bat install
```

Local installation (in the current directory under `local` directory):
```sh
.\winmake.bat local
```

Uninstalling (might require administrator privileges):
```
.\winmake.bat uninstall
```

Clean build files and targets:
```sh
.\winmake.bat clean
```

## Manual
Under `doc` directory is the `manual.html` and `contents.html` files that
document the current `Tokudae` distribution, stand-alone interpreter called
`tokudae`, compiler called `tokuc`, the C API, standard libraries and
the language grammar.
