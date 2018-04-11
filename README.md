# CPULIMIT

Cpulimit is a tool which limits the CPU usage of a process (expressed in percentage, not in CPU time).

It is useful to control batch jobs, when you don't want them to eat too many CPU cycles.
The goal is prevent a process from running for more than a specified time ratio.
It does not change the nice value or other scheduling priority settings, but the real CPU usage.

Also, it is able to adapt itself to the overall system load, dynamically and quickly.
The control of the used CPU amount is done sending `SIGSTOP` and `SIGCONT` POSIX signals to processes.
All the children processes and threads of the specified process will share the same percentage of CPU.

## Original Author

Thanks to [Angelo Marletta](https://github.com/opsengine) for the original code.

## Install instructions

The latest available code is here: https://github.com/denji/cpulimit

On Linux/macOS:

    $ make
    # cp src/cpulimit /usr/bin

On *BSD:

    $ gmake
    # cp src/cpulimit /usr/bin

Run unit tests:

    $ ./test/process_iterator_test


## Contributions

You are welcome to contribute to cpulimit with bugfixes, new features, or support for a new OS.
