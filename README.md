# Nimble

**Nimble** is an interpreted scripting language written in **C++17, single file**
(`nimble.cpp`), built for two uses:

- **As a CLI**: run `.nimble` scripts from the terminal, with an interactive REPL.
- **Embedded in C++**: add scripting to a game, tool or app, with fine-grained control
  over what the script can and can't do (sandboxed permissions, execution limits,
  recursion depth, time budget).

Clean syntax with `end`-terminated blocks (Ruby/Lua style), dynamic typing, and a
standard library aimed at real tasks: JSON, regex, CSV, HTTP, TCP/UDP sockets,
filesystem, time, built-in tests.

```nimble
use net

func greet(name)
    return "Hello, {name}!"
end

srv = net.tcp_listen("127.0.0.1", 9000)
print("listening on {srv.port}")

conn = srv.accept()
conn.send(greet(conn.recv()))
conn.close()
```

> 📘 The full language guide (syntax, standard library and embedding API) is in
> [`nimble_guia.html`](./nimble_guia.html).

---

## Features

- **Single source file.** No build system, no mandatory dependencies — a plain `g++`
  is enough.
- **Dynamically typed**, with lists, maps, structs, enums, classes with inheritance and
  closures.
- **Real error handling**: `try` / `catch` / `finally`, with stack traces.
- **Cycle collector** for the circular references plain refcounting can't free
  (closures, objects pointing at each other) — with `gc.collect()` / `gc.stats()` for
  manual control.
- **Practical standard library**: `math`, `random`, `time`, `system`, `json`, `path`,
  `regex`, `hex`, `env`, `csv`, `http` (via libcurl, optional) and `net` (raw TCP/UDP
  sockets, no dependencies).
- **Built-in test framework**: `test "..."` blocks and `assert_eq` / `assert_true` /
  etc., runnable with `nimble test file.nimble`.
- **Designed for embedding untrusted scripts**: `SandboxPermissions` controls
  filesystem, network, `exec`, environment variables and `exit()` independently, plus
  limits on steps, time and recursion depth.

## Building

Requires a C++17 compiler (GCC, Clang or MSVC).

```bash
# Linux / macOS
g++ -std=c++17 -O2 nimble.cpp -o nimble

# With HTTP support (http.get / http.post)
g++ -std=c++17 -O2 -DHAVE_CURL nimble.cpp -o nimble -lcurl

# Windows (MinGW) — the net module needs Winsock linked manually
g++ -std=c++17 -O2 nimble.cpp -o nimble.exe -lws2_32
```

## Usage

```bash
./nimble script.nimble              # run a script
./nimble run script.nimble arg1     # run passing arguments (system.args)
./nimble test script.nimble         # run the test "..." blocks in the file
./nimble --no-warn script.nimble    # silence parser warnings
./nimble                            # interactive REPL
```

## A quick look at the language

```nimble
# Variables, control flow, functions
func fib(n)
    if n < 2 return n end
    return fib(n - 1) + fib(n - 2)
end

for i in range(10)
    print("fib({i}) = {fib(i)}")
end

# Lists, maps, comprehensions
evens = [x for x in range(20) if x % 2 == 0]
person = {name: "Ana", age: 30}
print("{person.name} is {person.age} years old")

# Error handling
try
    obj = json.decode(untrusted_text)
catch err
    print("Invalid JSON: {err}")
end

# Classes
class Animal
    func init(name)
        self.name = name
    end
    func speak()
        return "{self.name} makes a sound"
    end
end
```

## Standard library

| Module   | What it's for |
|----------|----------------|
| `math`   | Trig functions, rounding, `pow`, `pi` |
| `random` | Integers, floats and random choice |
| `time`   | Epoch, date formatting/parsing, sleep |
| `system` | Running commands (`run`/`exec`), CLI args |
| `json`   | `encode` / `decode` with safe validation |
| `path`   | Join, basename, dirname (cross-platform) |
| `regex`  | Match, find_all, groups, replace, split |
| `hex`    | Hex encode/decode |
| `env`    | Read and write environment variables |
| `csv`    | Parse and generate CSV, with or without header |
| `http`   | GET/POST via libcurl (requires `-DHAVE_CURL -lcurl`) |
| `net`    | Raw, blocking TCP/UDP sockets, no dependencies |
| `gc`     | Manual control of the cycle collector |

Global I/O functions with no module: `read`, `write`, `append`, `exists`, `delete`,
`listdir`, `mkdir`, `rmdir`.

## Embedding Nimble in your C++ project

```cpp
#include "nimble.cpp"   // define NIMBLE_NO_MAIN before the include if you have your own main()

Interpreter interp;

// Sandbox: read-only filesystem, no network, no exec
interp.permissions.allowFileWrite  = false;
interp.permissions.allowNetwork    = false;
interp.permissions.allowSystemExec = false;

// Register your own native function
interp.builtins->define("my_function", makeNative("my_function",
    [](std::vector<Value>& args, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        return Value("result from C++");
    }));

interp.run(parsedProgram);
```

See the **Embedding in C++** section of the guide for permissions, execution limits,
`Value` conversion, and how to call script functions from C++ and vice versa.

## Project status / changelog

Full version history (what changed in each release, with before/after examples) is
documented in the guide, under **Novedades**. Quick summary:

- **v0.11** — `\r` is now a real escape sequence in strings.
- **v0.10** — new `net` module (TCP/UDP sockets), `string.starts_with`/`ends_with`,
  `list.index_of`, `math.round(x, decimals)`, fixed `path.basename`/`dirname` on
  Windows-style paths.
- **v0.9** — stability audit: fixed several process crashes (malformed
  `json.decode`, wrong-typed arguments to native functions) and corrected
  `for i, x in enumerate(...)`.
- **v0.8** — `try`/`catch`/`finally`, `keys()`/`values()`, `exit()`, `print_err()`.
- **v0.7** — cycle collector (`gc`).

## Contributing

PRs are welcome. If you add a new function or module:

1. Add it to the interpreter in `nimble.cpp`.
2. If it's a native module, don't forget to also add it to the list of module names
   recognized by `use` (a bug that has bitten this project twice already — `gc` in
   v0.9 and `net` in v0.10).
3. Document it in `nimble_guia.html`.
4. If it touches sandbox security, add a `test "..."` that covers it.

## License

_Add the license you're publishing the project under here (MIT, Apache-2.0, etc.) and
a `LICENSE` file at the repo root._
