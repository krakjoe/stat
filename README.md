# stat

Conventional PHP profilers use various Zend API's to overload the engine in order to build and usually dump (or serve) a profile for one single process. This gives us a problem when we want to look at a whole application in profile - we can't enable the profiler for every process in a pool in a production (or even staging) environment. Conventional profilers undertake their work in the same thread as is supposed to be executing your script, severely interfering with the performance characteristics of the code they are meant to be providing a profile for.

Stat is an unconventional provider of profile information: Stat uses an atomic ring buffer to provide realtime profile information about a set of PHP processes over a TCP or unix socket.

# Requirements

  - PHP 7.1+
  - Linux

# How To

Here is a quick run down of how to use Stat

## To build stat

  - `phpize`
  - `./configure`
  - `make`
  - `make install`

## To load stat

Stat must be loaded as a Zend Extension:

  - add `zend_extension=stat.so` to the target configuration

php -v should show something like: `... with Stat vX.X.X-X, Copyright (c) 2019, by krakjoe`

## To configure stat

The following configuration directives are available:

| Name           | Default                   | Purpose                                                        |
|:---------------|:--------------------------|:---------------------------------------------------------------|
|stat.samples    |`10000`                    | Set to the maximum number of samples in the buffer             |
|stat.interval   |`1000`                     | Set interval for sampling in microseconds                      |
|stat.arginfo    |`Off`                      | Enable collection of argument info                             |
|stat.strings    |`32M`                      | Set size of string buffer (supports suffixes, be generous)     |
|stat.socket     |`zend.stat.socket`         | Set path to socket, setting to 0 disables socket               |
|stat.dump       |`0`                        | Set to a file descriptor for dump on shutdown                  |

## To communicate with stat:

Stat can be configured to communicate via a unix or TCP socket, the following are valid examples:

  - `unix://zend.stat.socket`
  - `unix:///var/run/zend.stat.socket`
  - `tcp://127.0.0.1:8010`
  - `tcp://localhost:8010`

*Note: If the scheme is omitted, the scheme is assumed to be unix*

Stat will send each sample as a json encoded packet, one sample per line with the following format, prettified for readability::

    {
        "pid": int,
        "elapsed": double,
        "memory": {
            "used": int,
            "peak": int
        },
        "location": {
            "file":  "string",
            "line":  int
        },
        "symbol": {
            "scope": "string",
            "function": "string"
        },
        "arginfo": ["type(meta)" ...]
    }

Notes:

  - the absence of `location` and `symbol` signifies that the executor is not currently executing
  - the presence of `location` and absence of `symbol` signifies that the executor is currently executing in a file
  - the absence of `location` and presence of `symbol` signifies that the executor is currently executing internal code

### Startup

On startup (MINIT) Stat maps:

  - Strings - region of memory for copying persistent strings: file names, class names, and function names
  - Buffer  - the sample ring buffer

All memory is shared among forks and threads, and stat uses atomics, for maximum glory.

Should mapping fail, because there isn't enough memory for example, Stat will not stop the process from starting up but will only output a warning. Should mapping succeed, the configured socket will be opened. Should opening the socket fail, Stat will be shutdown immediately but allow the process to continue.

On request startup (RINIT) stat creates a sampler for the current request.

### Sampler

Rather than using Zend hooks and interfering with the VM or runtime (function tables etc), Stats sampler is based on parallel uio. When the sampler is created on RINIT, it creates a timer thread which keeps time without repeated syscalls and periodically invokes the sampling routine at the configured interval.

Because sampling occurs in parallel, it's possible to run PHP code at full speed while profiling: In (bench) testing, the overhead of stat running micro bench is statistically insignificant (1-2%, the same margin as without stat loaded) even with an interval of 10ms (100k samples per second).

Using uio in parallel, rather than trying to load from the memory of the target process directly protects stat from segfaults - the module globals which the executor uses at runtime are not manipulated atomically by zend, so that if the sampling thread tries to read a location in memory from the PHP process that changes while the read occurs, a segfault would result even if the sampler performs the read atomically - UIO will simply fail under conditions that would cause faults.

This does mean that it's possible (in theory) for sampling to fail. However, in practice, this is not really an issue: When there is a frame pointer in executor globals, it will be copied at once to the stack of the sampler, so that even if the frame pointer changes in the target process while the sampler is working, it doesn't matter because the sampler is still working on the frame it sampled. Another posibility is that the frame is freed between the read of the frame pointer and the frame, in which case failing is the only sensible thing to do as there would be no useful symbol information available to include in a sample.

Fetching argument information for a frame is disabled by default because this is in theory less reliable. The stack space is allocated with the frame by zend, so when the sampler copies the frame to its stack from the heap of the target process, it doesn't have the arguments (they come after the frame). In the time between the sampler copying the frame (without arguments) to its stack, and the sampler copying the arguments from the end of the frame on the heap of the target process, the arguments and their values may have changed. In practice, this is behaviour we are used too - when Zend gathers a backtrace, the values shown are the values at the time of the trace, not at the time of the call.

### Shutdown

On request shutdown (RSHUTDOWN) the current sampler for the current request is deactivated, this doesn't effect any of the samples it collected.

On shutdown (MSHUTDOWN) the socket is closed, the buffer may be dumped to a file descriptor depending on `stat.dump` before being unmapped, finally strings are unmapped.

### Notes

Stat is forward compatible with the JIT, and allows the JIT to run at as near as makes no difference full speed. However, the JIT is not obliged to maintain the instruction pointer, and I'm not sure what that would look like anyway. Hopefully, before the JIT becomes a production feature, there will be a way to detect easily if a function (or maybe an instruction) is in the JIT'd area so that we can treat those samples slightly differently.

#### TODO

 - Improve communication
 - CI
 - Tests
