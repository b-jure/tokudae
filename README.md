## Build & Installation

### Linux and Mingw (possibly other UNIX platforms)
In case of mingw or linux (and most UNIX) we use `make` when building from
source, make sure you have it installed.

There are two configuration files `config_linux.mk` and `config_mingw.mk`,
these are to be included near the top of the `Makefile`,
depending on the platform.
By default `config_linux.mk` is used and `config_mingw.mk` is commented out.

You are free to modify the configuration file to your needs when configuring
installation root, installation program, compiler, compiler flags, archiver,
binary/library targets and other utility programs.

Before building do:
```sh
make echo
```
in order to see if the build configuration is what you expected.

Building the binary and library:
```sh
make
```

Installing onto the system (add `sudo` in front if you lack the permission):
```sh
make install
```

Local Installation (in the current directory under `local` directory):
```sh
make local
```

Uninstalling (add `sudo` in front if you lack the permission):
```sh
make uninstall
```

Clean build files and targets:
```sh
make clean
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
`tokudae`, the C API, standard libraries and the language grammar.
