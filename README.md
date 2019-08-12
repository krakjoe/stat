# stat

Conventional PHP profilers use various Zend API's to overload the engine in order to build and usually dump (or serve) a profile for one single process. This gives us a problem when we want to look at a whole application in profile - we can't enable the profiler for every process in a pool in a production (or even staging) environment. Conventional profilers undertake their work in the same thread as is supposed to be executing your script, severely interfering with the performance characteristics of the code they are meant to be providing a profile for.

Stat is an unconventional provider of profile information: Stat uses an atomic ring buffer to provide realtime profile information for a set of PHP processes over a TCP or unix socket.

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
|stat.auto       |`On`                       | Disable automatic creation of samplers for every request       |
|stat.samplers   |`0` (unlimited)            | Set to limit number of concurrent samplers                     |
|stat.samples    |`10000`                    | Set to the maximum number of samples in the buffer             |
|stat.interval   |`100`                      | Set interval for sampling in microseconds, minimum 10ms        |
|stat.arginfo    |`Off`                      | Enable collection of argument info                             |
|stat.strings    |`32M`                      | Set size of string buffer (supports suffixes, be generous)     |
|stat.stream     |`zend.stat.stream`         | Set stream socket, setting to 0 disables stream                |
|stat.control    |`zend.stat.control`        | Set control socket, setting to 0 disables control              |
|stat.dump       |`0` (disabled)             | Set to a file descriptor for dump on shutdown                  |

## To retrieve samples from Stat:

Stat can stream over a unix or TCP socket, the following are valid examples:

  - `unix://zend.stat.socket`
  - `unix:///var/run/zend.stat.socket`
  - `tcp://127.0.0.1:8010`
  - `tcp://localhost:8010`

*Note: If the scheme is omitted, the scheme is assumed to be unix*

Upon connection, stat will stream the ring buffer with each sample on a new line, encoded as json with the following schema:


    {
        "type": "string",
        "request": {
            "pid": int,
            "elapsed": double,
            "path": "string",
            "method": "string",
            "uri": "string"
        },
        "elapsed": double,
        "memory": {
            "used": int,
            "peak": int
        },
        "symbol": {
            "scope": "string",
            "function": "string"
        },
        "arginfo": ["type(meta)" ...]
    }

The nature of a ring buffer means that the samples may not be in the correct temporal sequence (as contained in `elapsed`), the receiving software must be prepared to deal with that.

Notes:

  - `type` may be `memory`, `internal`, or `user`
  - the absence of `location` and `symbol` signifies that the executor is not currently executing
  - the presence of `location` and absence of `symbol` signifies that the executor is currently executing in a file
  - the absense of `line` in `location` signifies that a line number is not available for the current instruction
  - the `offset` in `location` refers to the offset of `opcode` from entry to `symbol` (always available)

## To control Stat:

The stream of samples that Stat provides is uninterruptable; Stat is controlled by a separate unix or TCP socket.

This control protocol is a work in progress, and this section is intended for the authors of integrating software.

A control has the following structure:

```
struct {
    int64_t control;
    int64_t param;
};
```

The following controls are defined:

| Name           | Control                   | Information                                                    |
|:---------------|:--------------------------|:---------------------------------------------------------------|
| auto           | `1<<1`                    | Enable/disable automatic creation of samplers                  |
| samplers       | `1<<2`                    | Controls the maximum number of samplers                        |
| interval       | `1<<3`                    | Sets the interval for sampling                                 |
| arginfo        | `1<<4`                    | Enables/disables the collection of arginfo                     |

*Note: the specifier 'q' should be used for pack (signed long long in machine byte order)*

### Control: auto

Changing the auto option will effect the subsequent creation of samplers without effecting any currently active samplers.

### Control: samplers

Changing the samplers option will effect the subsequent creation of samplers without effecting currently active samplers.

### Control: interval

Changing the interval option will effect subsequent ticks of the clock in every active sampler, and subsequently created samplers.

### Control: arginfo

Changing the arginfo option will effect all subsequently collected samples.

## Stat API:

Stat is a first class citizen in PHP, so there are a few API functions to control and interface with Stat:

```php
<?php
namespace stat {
    /**
    * Shall return the identifier of the current process as identified by Stat
    **/
    function pid() : int;

    /**
    * Shall return seconds elapsed since startup
    **/
    function elapsed() : double;
}

namespace stat\sampler {
    /**
    * Shall activate the sampler for the current request
    **/
    function activate() : bool;

    /**
    * Shall detect if the sampler for the current request is active
    **/
    function active() : bool;

    /**
    * Shall deactivate the sampler for the current request
    **/
    function deactivate() : bool;
}

namespace stat\buffer {
    /* NOT IMPLEMENTED YET */
    
}
?>
```

### Startup

On startup (MINIT) Stat maps:

  - Strings - region of memory for copying persistent strings: file names, class names, and function names
  - Buffer  - the sample ring buffer

All memory is shared among forks and threads, and stat uses atomics, for maximum glory.

Should mapping fail, because there isn't enough memory for example, Stat will not stop the process from starting up but will only output a warning. Should mapping succeed, the configured socket will be opened. Should opening the socket fail, Stat will be shutdown immediately but allow the process to continue.

On request startup (RINIT) stat creates a sampler for the current request.

### Sampler

Rather than using Zend hooks and interfering with the VM or runtime (function tables etc), Stats sampler is based on parallel uio. When the sampler is created on RINIT, it creates a timer thread which keeps time without repeated syscalls and periodically invokes the sampling routine at the configured interval.

Because sampling occurs in parallel, it's possible to run PHP code at full speed while profiling: In (bench) testing, the overhead of stat running micro bench is statistically insignificant (1-2%, the same margin as without stat loaded) even with an interval of 10us (100k samples per second).

Using uio in parallel, rather than trying to load from the memory of the target process directly protects stat from segfaults - the module globals which the executor uses at runtime are not manipulated atomically by zend, so that if the sampling thread tries to read a location in memory from the PHP process that changes while the read occurs, a segfault would result even if the sampler performs the read atomically - UIO will simply fail under conditions that would cause faults.

This does mean that it's possible (in theory) for sampling to fail. However, in practice, this is not really an issue: When there is a frame pointer in executor globals, it will be copied at once to the stack of the sampler, so that even if the frame pointer changes in the target process while the sampler is working, it doesn't matter because the sampler is still working on the frame it sampled. Another posibility is that the frame is freed between the read of the frame pointer and the frame, in which case failing is the only sensible thing to do as there would be no useful symbol information available to include in a sample.

Fetching argument information for a frame is disabled by default because this is in theory less reliable. The stack space is allocated with the frame by zend, so when the sampler copies the frame to its stack from the heap of the target process, it doesn't have the arguments (they come after the frame). In the time between the sampler copying the frame (without arguments) to its stack, and the sampler copying the arguments from the end of the frame on the heap of the target process, the arguments and their values may have changed. In practice, this is behaviour we are used too - when Zend gathers a backtrace, the values shown are the values at the time of the trace, not at the time of the call.

### Shutdown

On request shutdown (RSHUTDOWN) the sampler for the current request is deactivated, this doesn't effect any of the samples it collected.

On shutdown (MSHUTDOWN) the socket is shutdown, any clients connected will recieve the rest of the buffer (beware this may cause a delay in shutting down the process) before the buffer and strings are unmapped.

### Notes

Stat is forward compatible with the JIT, and allows the JIT to run at as near as makes no difference full speed. However, the JIT is not obliged to maintain the instruction pointer, and I'm not sure what that would look like anyway. Hopefully, before the JIT becomes a production feature, there will be a way to detect easily if a function (or maybe an instruction) is in the JIT'd area so that we can treat those samples slightly differently.

#### TODO

 - Improve communication
 - CI
 - Tests
