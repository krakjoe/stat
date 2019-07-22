# stat

Conventional PHP profilers use various Zend API's to overload the engine in order to build and usually dump (or serve) a profile for one single process. This gives us a problem when we want to look at a whole application in profile - we can't enable the profiler for every process in a pool in a production (or even staging) environment. Conventional profilers undertake their work in the same thread as is supposed to be executing your script, severely interfering with the performance characteristics of the code they are meant to be providing a profile for.

Stat is an unconventional provider of profile information: Stat uses an atomic ring buffer to provide live profile information about a set of PHP processes over a TCP or unix socket.

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
|stat.slots      |`10000`                    | Set to the maximum number of samples in the buffer             |
|stat.strings    |`32M`                      | Set size of string buffer (supports suffixes, be generous)     |
|stat.socket     |`zend.stat.socket`         | Set path to socket, setting to 0 disables socket               |
|stat.interval   |`1000`                     | Set interval for sampling in microseconds                      |
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
        }
    }

Notes:

  - the absense`location` and `symbol` signifies that the executor is not currently executing
  - the presence of `location` and absense of `symbol` signifies that the executor is currently executing in a file
  - the absense of `location` and presence of `symbol` sifnifies that the executor is currently executing internal code

### Internals

On startup (MINIT) Stat maps:

  - Strings - region of memory for copying persistent strings: file names, class names, and function names
  - Buffer  - the sample ring buffer

All memory is shared among forks and threads, and stat uses atomics, for maximum glory.

Should mapping fail, because there isn't enough memory for example, Stat will not stop the process from starting up but will only output a warning. Should mapping succeed, the configured socket will be opened. Should opening the socket fail, Stat will be shutdown immediately but allow the process to continue.

On request (RINIT) stat creates a timer that executes Stat's sampler for the current request at the configured interval.

### Sampler

The sampler is always executed in a thread separate to the thread meant to be executing PHP code.

Rather than using Zend hooks and overloads to extract information from the request, it reads directly from the process memory using uio.
