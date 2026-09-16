# Nimble — Language Guide (v0.11)

Interpreted scripting language, simple and embeddable. Full guide, v0.11.

## Table of contents

**Getting started**
- [What's new in v0.10 / v0.11](#whats-new-v10-v11)
- [What's new in v0.9](#whats-new-v09)
- [Introduction](#introduction)
- [Building and running](#building-and-running)
- [Interactive REPL](#repl)
- [CLI and arguments](#cli)

**Language**
- [Comments](#comments)
- [Variables and constants](#variables)
- [Data types](#data-types)
- [Strings and interpolation](#strings)
- [Operators](#operators)
- [Control flow](#control-flow)
- [Functions](#functions)
- [Lists](#lists)
- [Maps and objects](#maps-and-objects)
- [Classes](#classes)
- [Enums and structs](#enums-and-structs)
- [Match](#match)
- [Errors and exceptions](#errors)
- [Modules](#modules)

**Standard library**
- [Global functions](#globals)
- [math](#math)
- [random](#random)
- [time](#time)
- [system](#system)
- [json](#json)
- [path](#path)
- [regex](#regex)
- [hex](#hex)
- [env](#env)
- [csv](#csv)
- [http](#http)
- [net](#net) — new
- [Files (I/O)](#files)
- [gc — cycle collector](#gc)

**Extras**
- [Test framework](#tests)

**Embedding in C++**
- [Introduction](#embed-intro)
- [Basic usage](#embed-basic)
- [Registering native functions](#embed-natives)
- [Calling script functions from C++](#embed-call)
- [Sandbox and permissions](#embed-sandbox)
- [Execution limits](#embed-limits)
- [Modules embedded in the executable](#embed-modules)
- [Working with `Value`](#embed-values)
- [Cycle collector on the host side](#embed-gc)
- [Full example: game scripting](#embed-example)

---

<a id="whats-new-v10-v11"></a>
## What's new in v0.10 / v0.11

Summary of what changed after v0.9 (the historical detail of the v0.9 audit is further
below). If you're coming from v0.9, here's what's new:

### `net` module: TCP/UDP sockets (v0.10)

New `net` module with raw, blocking TCP and UDP sockets and no external dependencies
(BSD sockets on Linux/macOS, Winsock2 on Windows). Built for test mini-servers, simple
game protocols, or poking a client by hand. See the full [net](#net) section below.

```nimble
use net

srv = net.tcp_listen("127.0.0.1", 9000)
conn = srv.accept()
print(conn.recv())
conn.send("ok\r\n")
```

> **Bug fixed during this update:** just like what happened to `gc` in v0.9 (see
> below), `net` was registered as a native module but got left out of the internal list
> the `use` statement uses to recognize native modules. The symptom was identical:
> `use net` failed with `Could not import module 'net.nimble' (not found: net.nimble)`,
> as if `use` were trying to load it as a project file. Now fixed — `use net`,
> `use net as n`, and `use net (tcp_connect, resolve)` all work.

### `\r` escape in strings (v0.11)

Before, the only recognized escapes inside a quoted string were `\n`, `\t`, `\"`, `\\`,
`\{` and `\}`. `\r` wasn't on that list, so it fell through to the default case and
stayed as-is — **two literal characters**, the backslash and the letter `r` — instead
of turning into a real carriage return (`0x0D`). This went unnoticed in most code, but
became a real problem once `net` arrived: many text protocols over sockets (hand-rolled
HTTP, SMTP, IRC, CRLF-terminated lines) need an exact `"\r\n"` to separate lines.

```nimble
# Before v0.11:
s = "line1\r\nline2"
length(s)     # 16 -- \r stayed as the TWO characters '\' and 'r', not 0x0D

# From v0.11 on, \r is a real escape:
s = "line1\r\nline2"
length(s)     # 15 -- \r is a single byte (0x0D), just like \n

# Useful for line-based protocols over net.*:
use net
c = net.tcp_connect("example.com", 80)
c.send("GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n")
```

> **If you depended on the old behavior:** code that wrote `"\r"` expecting the literal
> backslash and the letter `r` (instead of a carriage return) now gets a different byte
> and probably a string one character shorter. It's unlikely anything relied on that bug
> on purpose, but if you had tests comparing exact string lengths with an unescaped `\r`
> that wasn't a raw string (`r"..."`), double-check them.

### Other standard library additions (v0.10)

- **`string.starts_with(prefix)` / `string.ends_with(suffix)`** — the one thing missing
  from string checks; `contains()` and `find()` don't work for this because they don't
  anchor to a position.
- **`list.index_of(value)`** — a value's position inside a list (or `-1` if it isn't
  there), without writing the loop by hand. Uses the same structural equality already
  used by `==` and `in`.
- **`math.round(x, decimals)`** — rounding to N decimal places, added as a new function
  on `math` rather than a second argument on the global `round()` (which keeps working
  exactly as before, single-argument only).
- **`path.basename()` / `path.dirname()`** — rewritten on top of
  `std::filesystem::path`. Before, they searched for `'/'` by hand, so a Windows-style
  path (`C:\Users\foo\bar.txt`) on Linux/macOS came back with the whole path as the
  "basename". Now they understand both separators.

```nimble
use math

"nimble.txt".starts_with("nim")   # true
"nimble.txt".ends_with(".txt")    # true

[10, 20, 30].index_of(20)   # 1
[10, 20, 30].index_of(99)   # -1

math.round(3.14159, 2)   # 3.14
round(3.14159)           # 3  (global round(), unchanged: always 0 decimals)
```

---

<a id="whats-new-v09"></a>
## What's new in v0.9 <sup>(fixes)</sup>

v0.9 came out of an audit of the documentation itself: *every documented example* was
tested against the real interpreter, and that turned up several bugs — three
behavior/API bugs, and two more serious stability issues found while going through the
source in detail. If you're coming from v0.8, here's what changes for you:

### 1. `json.decode()` can no longer bring down the process (crash fixed)

Before, a `json.decode(...)` call with malformed input — including cases as simple as
`"{}"` on certain code paths — could end in a `std::terminate`, with `try/catch`
powerless to stop it, because the error escaped as a C++ exception unrelated to Nimble
(`std::invalid_argument`) instead of a catchable `NimbleError`.

The root cause was in string interpolation: when the content between `{ }` failed to
parse as an expression, instead of failing it silently substituted filler text, and that
text was what later reached `json.decode`'s unguarded number parser. Both are now fixed:

- An invalid interpolation raises a catchable `NimbleError`, instead of silently
  producing an incorrect result.
- `json.decode()` validates numbers, `true`, `false` and `null` before consuming them,
  so no input can crash the process — at most it raises a normal `NimbleError`.
- `json.decode()` also requires object keys to be strings (as the JSON standard
  itself mandates): an input like `{1: 2}` used to try reading the key as a string and
  crashed the same way malformed numbers did; now it's rejected with a clear
  `NimbleError`.

```nimble
# Before v0.9 this could crash the whole process.
# Now it simply raises a catchable error:
use json

try
    obj = json.decode("{this is not valid json}")
catch err
    print("Invalid JSON: {err}")
end

# This also affects interpolation: a broken expression between { }
# is now an error, not silent filler text.
try
    x = "result: {1 +}"   # invalid expression inside { }
catch err
    print("Invalid interpolation: {err}")
end
```

### 2. `Value(42)` and `Value("text")` finally do what they look like they do (embedding API)

This only affects C++ code embedding Nimble (see [Working with Value](#embed-values)),
but it was a serious trap: before v0.9, `Value(42)` didn't even compile (ambiguous
between `Value(double)` and `Value(bool)`), and `Value("text")` compiled **without any
error** but produced a boolean `true` — because the pointer-to-`bool` conversion wins
overload resolution over the (user-defined) `const char*` to `std::string` conversion.
Any documentation example that wrote a literal directly was affected.

```cpp
// Before v0.9:
Value(42)         // compile ERROR (ambiguous)
Value("hello")    // compiles, but is Value(true) -- a boolean!, not the string "hello"

// From v0.9 on, both do the expected thing:
Value n(42);           // number: 42
Value s("hello");      // string: "hello"
```

> **If you have embedding code from before v0.9:** check any `Value("literal")` you
> wrote directly with a quoted string. Before, you had to wrap it as
> `Value(std::string("literal"))` for it to work; now both forms give the same correct
> result, but if your code depended (on purpose or by accident) on the `true` value the
> bug produced, that behavior no longer happens.

### 3. `for i, x in enumerate(list)` finally works (behavior bug)

The two-variable `for` already worked correctly over maps (`for key, value in map`),
but over a *list* of pairs — like the one `enumerate()` produces — it used to assign
the **whole pair** to the first variable and leave the second undefined. This silently
broke the most common idiom for using two loop variables. Now, over a list, the
two-variable `for` destructures each element (assuming it's a 2-element list) into the
two variables, just like it already did for maps.

```nimble
names = ["ana", "luis", "eva"]

# Before v0.9: `i` received the whole [0, "ana"] and `x` was left undefined.
# Since v0.9: `i` gets 0 and `x` gets "ana", as you'd expect.
for i, x in enumerate(names)
    print("{i}: {x}")
end
# 0: ana
# 1: luis
# 2: eva

# Also works with any hand-built list of pairs:
for key, value in [["a", 1], ["b", 2]]
    print("{key} = {value}")
end
```

> If a list element isn't exactly a 2-element list, a `NimbleError` is raised explaining
> that each item must be a 2-element list (like the one `enumerate()` produces), instead
> of silently failing.

---

<a id="introduction"></a>
## Introduction

**Nimble** is a scripting interpreter written in C++17. Its syntax is clean, with
`end`-terminated blocks (Ruby/Lua style), dynamic typing, and a practical standard
library. It's designed for two uses:

- **As a CLI:** run `.nimble` scripts from the terminal or in an interactive REPL.
- **Embedded in C++:** built into a game, tool or application to allow user scripting,
  with full control over permissions and limits.

> **Philosophy:** clear blocks, zero magic, and a small, explicit embedding API.

---

<a id="building-and-running"></a>
## Building and running

Nimble is a single file, `nimble.cpp`. It has no external dependencies unless you want
to use `http`, which needs `libcurl`.

### Basic build (CLI)

```bash
# Linux / macOS
g++ -std=c++17 -O2 nimble.cpp -o nimble

# With HTTP support
g++ -std=c++17 -O2 -DHAVE_CURL nimble.cpp -o nimble -lcurl

# Windows (MinGW)
g++ -std=c++17 -O2 nimble.cpp -o nimble.exe
```

### Running a script

```bash
./nimble my_script.nimble
./nimble run my_script.nimble arg1 arg2
./nimble test my_script.nimble          # runs the defined tests
./nimble --no-warn my_script.nimble     # disables warnings
```

Arguments after the file name are available in the script as `system.args` (a list).

---

<a id="repl"></a>
## Interactive REPL

Running `nimble` with no arguments starts interactive mode:

```
$ ./nimble
Nimble REPL (v0.9) -- type 'exit' to quit
> 2 + 2
4
> func greet(name) return "Hello, " + name end
> greet("world")
Hello, world
> exit
```

You can write multi-line blocks; the prompt switches to `...` until they're closed. You
can also exit with `exit()` or `salir`.

---

<a id="cli"></a>
## CLI and arguments

| Command | Description |
|---|---|
| `nimble file.nimble` | Runs the script. |
| `nimble run file.nimble [args...]` | Runs it, passing arguments. |
| `nimble test file.nimble` | Runs the `test "..."` blocks. |
| `nimble --no-warn file.nimble` | Silences parser warnings. |

---

<a id="comments"></a>
## Comments

Only line comments exist, with `#`:

```nimble
# This is a comment
x = 5   # end-of-line comment
```

---

<a id="variables"></a>
## Variables and constants

Variables are created by plain assignment. There's no `var` keyword.

```nimble
name = "Ana"
age = 30
active = true

# Constants (can't be reassigned)
const PI = 3.14159

# Multiple assignment
a, b = 1, 2

# List destructuring
[first, second] = [10, 20]

# Map destructuring
{name, age} = {name: "Luis", age: 25}
```

> **Optional type annotation:** you can write `x: number = 5`, but the type is purely
> informational (it isn't validated at runtime).

---

<a id="data-types"></a>
## Data types

| Type | Example | Notes |
|---|---|---|
| `number` | `42`, `3.14`, `0xFF`, `0b1010` | Double-precision floating point. |
| `string` | `"hello"`, `"""..."""`, `r"raw"` | Interpolation with `{expr}`. |
| `boolean` | `true`, `false` | |
| `null` | `null` | Absence of a value. |
| `list` | `[1, 2, 3]` | Indexable, mutable. |
| `map` | `{a: 1, b: 2}` | String keys, insertion order preserved. |
| `function` | `func(x) return x end` | First-class citizens. |
| `object` | `object ... end` | Dynamic struct. |
| `class` | `class X ... end` | With single inheritance. |

You can check a value's type with `type(value)`:

```nimble
type(3)          # "number"
type("hello")    # "string"
type([1,2])      # "list"
```

---

<a id="strings"></a>
## Strings and interpolation

### Normal strings

```nimble
s = "Hello\nworld"
escaped = "quotes \" and backslash \\"
```

Escapes: `\n`, `\t`, `\r` **(v0.11)**, `\"`, `\\`, `\{`, `\}`.

> `\r` has only been a real escape since v0.11 — before it stayed as the two literal
> characters `\` and `r`. Mostly useful for building `"...\r\n"` lines when speaking
> text protocols with the new [net](#net) module.

### Multi-line strings (v0.5)

```nimble
text = """
Line 1
Line 2
"""
```

### Raw strings

```nimble
path = r"C:\Users\test\nothing"   # no escapes, no interpolation
```

### Interpolation

Inside a normal string, `{expr}` evaluates and prints the value:

```nimble
name = "Ana"
age = 30
print("Hello {name}, you're {age} years old")
print("2 + 2 = {2 + 2}")
print("uppercase: {name.upper()}")
```

> If you want literal braces, escape them: `"\{not interpolated\}"`.

> **Interpolation errors (v0.9 fix):** if what's between `{ }` isn't a valid
> expression, a catchable `NimbleError` is raised with `try/catch` — before v0.9 it was
> silently replaced with filler text, which could produce incorrect strings without
> warning (and combined with `json.decode`, could even crash the process).
>
> ```nimble
> try
>     x = "total: {1 + }"   # invalid expression
> catch err
>     print("invalid interpolation expression: {err}")
> end
> ```

### String members

| Member | Description |
|---|---|
| `s.length` | Length. |
| `s.upper()`, `s.lower()` | Uppercase / lowercase. |
| `s.trim()` | Strips whitespace from both ends. |
| `s.find(sub, [start])` | Position, or `-1`. |
| `s.contains(sub)` | Returns a boolean. |
| `s.starts_with(prefix)` (v0.10) | Boolean; anchors at the start of the string. |
| `s.ends_with(suffix)` (v0.10) | Boolean; anchors at the end of the string. |
| `s.replace(from, to)` | Replaces all. |
| `s.split(sep)` | Returns a list. |
| `s[i]`, `s[a:b]` | Indexing and slicing. |

---

<a id="operators"></a>
## Operators

### Arithmetic

| Operator | Meaning |
|---|---|
| `+` | Add / concatenate strings / join lists |
| `-` `*` `/` `%` | Subtraction, multiplication, division, modulo |
| `**` | Power (`2 ** 8 == 256`) |

### Comparison

`==` and `!=` compare *structurally* (lists and maps included). `===` and `!==` compare
reference *identity*.

```nimble
[1,2] == [1,2]     # true  (structural)
[1,2] === [1,2]    # false (different lists)

a = [1,2]
b = a
a === b            # true  (same reference)
```

### Logical

`and`, `or`, `not`. They return the value, not just a boolean (handy for default
values).

### Membership

`x in list`, `"sub" in "string"`, `"key" in map`.

### Special operators

| Operator | Meaning |
|---|---|
| `??` | Null coalescing: `a ?? b` returns `b` if `a` is null |
| `?.` `?[` | Optional access: short-circuits to `null` if the value is null |
| `...x` | Spread (in lists, maps and calls) |

```nimble
user = null
name = user?.name ?? "guest"   # "guest"

# Spread in lists
a = [1, 2]
b = [0, ...a, 3]              # [0, 1, 2, 3]

# Spread in maps
base = {a: 1}
ext = {...base, b: 2}        # {a: 1, b: 2}

# Spread in calls
nums = [1, 2, 3]
print(max(...nums))
```

### Compound assignment

`+=`, `-=`, `*=`, `/=`, `%=`:

```nimble
x = 10
x += 5     # 15
x *= 2     # 30
s = "a"
s += "b"    # "ab"
lst = [1]
lst += [2] # [1, 2]
```

### Ternary operator

```nimble
status = "adult" if age >= 18 else "minor"
```

---

<a id="control-flow"></a>
## Control flow

### if / elif / else

```nimble
if age < 13
    print("child")
elif age < 18
    print("teenager")
else
    print("adult")
end
```

> **One-line blocks (v0.5):** you can write the body on the same line.
> ```nimble
> if x > 0 return "positive" end
> ```

### while

```nimble
i = 0
while i < 5
    print(i)
    i += 1
end
```

### for

The **single-variable** form works over lists, strings (character by character) and
maps (iterating over the keys):

```nimble
for x in [1, 2, 3]
    print(x)
end

# Iterate over a string, character by character
for c in "abc"
    print(c)
end

# Iterate over a map: one variable = keys only
ages = {ana: 30, luis: 25}
for name in ages
    print(name)     # "ana", then "luis"
end
```

The **two-variable** form (`for a, b in ...`) covers two different cases, depending on
what you iterate over:

- Over a **map**: `a` receives the key and `b` the value.
- Over a **list**: each element must itself be a 2-element list (a "pair"), and that
  pair is destructured into `a` and `b`. This is exactly what's needed for the
  `for i, x in enumerate(list)` idiom, the most common use of the two-variable form.

```nimble
# Two variables over a map: key + value
for name, age in ages
    print("{name} is {age}")
end

# Two variables over a list of pairs (what enumerate() produces):
# index + value, the most common idiom for numbering elements
fruits = ["apple", "pear", "grape"]
for i, fruit in enumerate(fruits)
    print("{i}: {fruit}")
end
# 0: apple
# 1: pear
# 2: grape

# Also works with any hand-built list of pairs,
# for example the result of zip():
names = ["ana", "luis"]
scores  = [10, 20]
for name, score in zip(names, scores)
    print("{name}: {score} pts")
end
```

> **Fixed in v0.9:** before, the two-variable form over a list only assigned the
> *whole* pair to the first variable and left the second undefined. If you were
> already using `for i, x in enumerate(...)` before v0.9, check those loops: now `x`
> gets the real value, not `null`/undefined.
>
> If a list element isn't exactly a 2-element list, `NimbleError` is raised instead of
> silently failing.

### 4. `use gc` finally works (module unreachable via `use`)

The `gc` module (see [memory management / gc](#gc)) was always registered as a native
module just like `math`, `json`, etc., but the internal list the `use` statement uses to
recognize native modules had gone stale and didn't include it. The result: typing
exactly the example the documentation itself showed threw an error.

```nimble
# Before v0.9, this failed with:
# "Could not import module 'gc.nimble' (not found: gc.nimble)"
# because 'use' tried to load it as a project file.
use gc
gc.collect()
print(gc.stats())
```

> Fun fact found while investigating this: native modules (`math`, `json`, `gc`, etc.)
> are actually visible as global variables *without needing `use` at all* — `use` for a
> native module only matters when you want an alias (`use gc as g`) or a selective
> import (`use gc (collect, stats)`). That's why the real-world impact of this bug was
> limited (`gc.collect()` without the `use gc` line already worked), but all three
> forms of `use gc` — plain, aliased, or with a name list — were broken until this
> version.

### 5. Wrong-typed arguments no longer crash the process (sandbox security)

This is the most serious finding of the audit. Dozens of native functions —
`ord`, `read`/`write`/`append`, the string methods (`replace`, `find`, `contains`,
`split`, `join`), `min`/`max`, `map`/`filter`/`reduce`/`zip`/`enumerate`/`sum`/`any`/
`all`, and practically all of `time`, `system`, `regex`, `hex`, `env`, `csv` and
`http` — took an argument from the script and accessed it directly (`a[0].asStr()`,
`a[0].asList()`, sometimes even `a[0]` without checking there were enough arguments)
without first verifying it was the expected type.

When the type didn't match, that triggered a pure C++ exception
(`std::bad_variant_access` or `std::out_of_range`) which is **not a `NimbleError`**. No
`try/catch` in the script could stop it, and the whole process ended up aborted
(`SIGABRT`). For a host using the [permissions system](#embed-sandbox) to run untrusted
scripts, this completely nullified the protection: a single wrong-typed argument —
whether by accident or on purpose — was enough to bring down the process, and neither
`allowFileWrite: false`, `allowNetwork: false`, nor any other permission could stop it.

```nimble
# The three examples below crashed the ENTIRE PROCESS before v0.9
# (not just raised a Nimble error):
"abc".replace(5, "x")     # replace() expected a string, not a number
ord(42)                   # ord() expected a string
min([])                   # min() of an empty list

# Now, all three simply raise a normal, catchable NimbleError:
try
    "abc".replace(5, "x")
catch err
    print("error: {err}")   # replace(): argument 1 must be a string, got number
end
```

> If you embed Nimble to run untrusted scripts, this is the most important security
> fix in v0.9: before, no combination of `SandboxPermissions` could stop a script
> (accidental or malicious) from taking down the whole host process with a single
> wrong-typed argument.

### break and continue

```nimble
for x in range(10)
    if x == 5 break end
    if x % 2 == 0 continue end
    print(x)   # prints 1, 3
end
```

---

<a id="functions"></a>
## Functions

### Declaration

```nimble
func greet(name)
    return "Hello, {name}!"
end

print(greet("Ana"))
```

### Default parameters

```nimble
func power(base, exp = 2)
    return base ** exp
end

power(5)      # 25
power(2, 10)  # 1024
```

### Named arguments

```nimble
func create(name, age, city)
    return {name, age, city}
end

create("Ana", age: 30, city: "Madrid")
```

### Returning multiple values (as a list)

```nimble
func minmax(list)
    return min(list), max(list)
end

lo, hi = minmax([3, 1, 7])
```

### Anonymous functions (lambdas)

```nimble
double = func(x) return x * 2 end
print(double(5))   # 10

# Very useful with map/filter/reduce
nums = [1, 2, 3, 4]
squares = map(nums, func(x) return x * x end)
evens = filter(nums, func(x) return x % 2 == 0 end)
```

### Closures

```nimble
func counter()
    n = 0
    return func()
        n += 1
        return n
    end
end

c = counter()
print(c())   # 1
print(c())   # 2
```

> **Recursion:** the interpreter enforces a stack depth limit (`maxCallDepth`, 500 by
> default) to avoid overflowing the real C++ stack. Very deep recursion will raise
> `"Call stack too deep"`.

---

<a id="lists"></a>
## Lists

```nimble
nums = [1, 2, 3]

# Multi-line lists
colors = [
    "red",
    "green",
    "blue"
]
```

### Indexing and slicing

```nimble
nums[0]        # 1
nums[-1]       # 3  (last)
nums[1:3]      # [2, 3]
nums[:2]       # [1, 2]
nums[1:]       # [2, 3]

nums[0] = 99   # mutation
```

### List members

| Member | Description |
|---|---|
| `l.length` | Number of elements. |
| `l.first` / `l.last` | First / last element. |
| `l.push(x)` | Appends at the end (mutates). |
| `l.pop()` | Removes and returns the last element. |
| `l.sort()` | Sorts in place. |
| `l.reverse()` | Reverses in place. |
| `l.index_of(value)` (v0.10) | Position of the value (structural equality, like `==`) or `-1` if absent. |
| `l.join(sep)` | Concatenates elements as a string. |

### List comprehensions

```nimble
squares = [x * x for x in range(5)]
# [0, 1, 4, 9, 16]

evens = [x for x in range(10) if x % 2 == 0]
# [0, 2, 4, 6, 8]
```

---

<a id="maps-and-objects"></a>
## Maps and objects

### Maps

```nimble
person = {
    name: "Ana",
    age: 30,
    city: "Madrid"
}

print(person.name)         # "Ana"
print(person["age"])       # 30
person.email = "a@b.com"   # adds a field

# Simple-name keys can skip the quotes
config = {debug: true, port: 8080}
# and shorthand values too (key = variable with that name)
name = "X"
m = {name}                  # {name: "X"}

# Spread in maps
base = {a: 1}
ext = {...base, b: 2}        # {a: 1, b: 2}
```

### Objects (dynamic struct)

```nimble
point = object
    x = 0
    y = 0
    func move(dx, dy)
        self.x += dx
        self.y += dy
    end
end

point.x = 5
point.move(1, 2)
```

Objects are isolated environments. You can use `self` inside their functions.

---

<a id="classes"></a>
## Classes

```nimble
class Animal
    func init(name)
        self.name = name
    end

    func speak()
        return "...?"
    end

    func describe()
        return "{self.name} says {self.speak()}"
    end
end

class Dog extends Animal
    func speak()
        return "Woof"
    end
end

d = Dog("Rex")
print(d.describe())    # "Rex says Woof"
```

The constructor is always the `init` method. It's invoked automatically when you call
`Class(args)`.

> **Inheritance:** single (one parent). Methods are looked up recursively through the
> base-class chain. Method lookup is cached for better performance (v0.7).

---

<a id="enums-and-structs"></a>
## Enums and structs

### Enum

```nimble
enum Color
    RED
    GREEN
    BLUE
end

print(Color.RED)   # 0
print(Color.BLUE)  # 2
```

Enums are immutable maps: name → numeric index.

### Structs (`type`)

A struct is a class with an automatic constructor and structural comparison:

```nimble
type Point(x, y)

p = Point(1, 2)
q = Point(1, 2)

print(p.x)         # 1
print(p == q)      # true (compared by value)
```

---

<a id="match"></a>
## Match

Value matching with structural comparison:

```nimble
match command
    "quit", "exit":
        print("bye")
        break
    "help":
        print("commands: quit, help")
    0:
        print("zero")
    else:
        print("unknown")
end
```

> `match` compares the subject against each label with structural `==`. The `else`
> branch is optional.

---

<a id="errors"></a>
## Errors and exceptions

### throw / try / catch / finally (v0.8)

```nimble
func divide(a, b)
    if b == 0
        throw "Cannot divide by zero"
    end
    return a / b
end

try
    print(divide(10, 0))
catch err
    print("Error: {err}")
finally
    print("This always runs")
end
```

`catch` captures both script `throw`s and internal interpreter errors (undefined
variables, type errors, etc.). The captured value is either the thrown value or the
error message.

> **finally** always runs: whether the `try` block finishes fine, raises an error, has
> that error caught, or has a `return`/`break`/`continue` in flight. Its own control
> flow takes precedence (same as in Java/Python/JS).

### assert and panic

```nimble
assert(x > 0)                    # aborts if false
assert(x > 0, "x must be positive")

panic("impossible state")          # always aborts
```

> **Important:** `assert` and `panic` raise an *uncatchable signal* (`AbortSignal`). A
> `try/catch` in the script **cannot** stop them. The same applies to execution limits.

### exit(code) (v0.8)

```nimble
if critical_error
    print("Exiting...")
    exit(1)   # terminates the process with code 1
end
```

`exit()` raises an `ExitSignal` that's only caught at the outermost level
(main/REPL/test runner), letting every RAII destructor run properly before it
terminates.

### Stack trace

Uncaught errors print a trace:

```
Error: [line 5] Undefined variable: 'x'
Stack trace:
   at foo (called from line 12)
   at main (called from line 20)
```

---

<a id="modules"></a>
## Modules

### Native modules

```nimble
use math
print(math.sqrt(16))

use json
print(json.encode({a: 1}))

# With an alias
use math as m
print(m.pi)

# Importing specific names
use math (sqrt, pi)
print(sqrt(9))
```

### File modules

```nimble
# Imports "utils.nimble" from the same directory
use "utils.nimble"

# With an alias
use "utils.nimble" as u

# Dotted notation: package.module -> package/module.nimble
use package.module

# Import specific names from a file module
use "utils.nimble" (add, subtract)
```

> Modules are loaded only once (cached), and circular imports are detected. The
> default name of a file module is the file's *stem* (`"utils.nimble"` → `utils`).

---

<a id="globals"></a>
## Global functions

| Function | Description |
|---|---|
| `print(...)` | Prints values separated by spaces. |
| `print_err(...)` (v0.8) | Prints to stderr. |
| `exit([code])` (v0.8) | Terminates the process (requires permission). |
| `input([prompt])` | Reads a line from stdin. |
| `length(x)` | Length of a string/list/map. |
| `keys(m)` (v0.8) | List of a map's keys. |
| `values(m)` (v0.8) | List of a map's values. |
| `type(x)` | The type's name, as a string. |
| `number(x)`, `string(x)`, `boolean(x)` | Conversions. |
| `ord(c)`, `chr(n)` | Character ↔ code (0–255). |
| `min(...)`, `max(...)` | With a list or varargs. |
| `abs`, `round`, `floor`, `ceil` | Numeric functions. |
| `range(stop)`, `range(start,stop)`, `range(start,stop,step)` | Generates a list. |
| `map(list, fn)` | Applies a function, returns a new list. |
| `filter(list, fn)` | Filters by predicate. |
| `reduce(list, fn, [init])` | Reduces to a single value. |
| `sort_by(list, fn)` | Sorts by key (stable). |
| `group_by(list, fn)` | Groups into a map. |
| `zip(a, b)` | List of pairs. |
| `enumerate(list)` | List of `[i, x]`. |
| `sum(list)` | Sum of numbers. |
| `any(list, [fn])`, `all(list, [fn])` | Quantifiers. |
| `assert(...)`, `panic(...)` | Abort (uncatchable). |
| `assert_eq`, `assert_ne`, `assert_true`, `assert_false` | Test assertions. |
| `read`, `write`, `append`, `exists`, `delete`, `listdir`, `mkdir`, `rmdir` | Files and directories. |

### Aggregations — examples

```nimble
people = [
    {name: "Ana", age: 30},
    {name: "Luis", age: 25},
    {name: "Eva", age: 30}
]

sorted_people = sort_by(people, func(p) return p.age end)
by_age = group_by(people, func(p) return string(p.age) end)
total_age = sum(map(people, func(p) return p.age end))

for i, p in enumerate(people)
    print("{i}: {p.name}")
end
```

---

<a id="math"></a>
## math

```nimble
use math

math.sqrt(16)     # 4
math.sin(0)        # 0
math.cos(0)        # 1
math.tan(0)        # 0
math.abs(-3)      # 3
math.floor(3.7)    # 3
math.ceil(3.1)     # 4
math.pow(2, 10)    # 1024
math.round(3.14159, 2)  # 3.14  -- rounding to N decimal places
math.pi            # 3.14159...
```

> `math.round(x, decimals)` (v0.10) is different from the global `round(x)`: the
> global one still accepts a single argument (rounds to an integer), unchanged;
> `math.round` adds the optional second argument for decimal places (default `0`, same
> result as the global in that case).

---

<a id="random"></a>
## random

```nimble
use random

random.int(1, 6)              # integer between 1 and 6 (inclusive)
random.float()                 # [0.0, 1.0)
random.float(-1, 1)           # arbitrary range
random.choice(["a", "b"])    # a random element
```

---

<a id="time"></a>
## time

```nimble
use time

t = time.now()                           # epoch seconds (float)
time.sleep(0.5)                       # sleeps for 500 ms
time.today()                             # "2025-01-15"
time.format(t, "%Y-%m-%d %H:%M")        # formats it
time.parse("2025-01-15", "%Y-%m-%d")  # -> epoch
time.add_days(t, 7)
time.add_seconds(t, 3600)
time.diff(t2, t1)                        # t2 - t1 in seconds
```

---

<a id="system"></a>
## system

```nimble
use system

system.args                              # list of CLI arguments

# Run and show output in the terminal
rc = system.run("ls -la")

# Capture stdout
output = system.run("ls", capture: true)

# Run and capture stdout, stderr and status separately
r = system.exec("command")
print(r.status)     # exit code
print(r.stdout)     # standard output
print(r.stderr)     # standard error
```

---

<a id="json"></a>
## json

```nimble
use json

text = json.encode({name: "Ana", age: 30})
# '{"name":"Ana","age":30}'

obj = json.decode('{"a": [1, 2, 3], "b": true}')
print(obj.a)    # [1, 2, 3]
print(obj.b)    # true

# json.decode understands objects, lists, strings, numbers,
# true/false/null nested to any depth:
config = json.decode('{"server": {"port": 8080, "active": true}, "tags": ["a","b"]}')
print(config.server.port)   # 8080
print(config.tags[0])       # "a"
```

### Malformed input

Always treat `json.decode()`'s input as untrusted (it comes from a file, an HTTP
response, etc.) and wrap it in `try/catch`:

```nimble
try
    obj = json.decode(untrusted_text)
catch err
    print("Invalid JSON: {err}")
    obj = {}
end
```

> **Fixed in v0.9:** before, `json.decode()` with malformed input could terminate the
> whole process (`std::terminate`) with no script `try/catch` able to prevent it,
> because the number parser didn't validate the text before converting it. Now it
> validates numbers, `true`, `false` and `null` before consuming them, so any invalid
> input — including an empty string, a malformed empty object, or arbitrary garbage —
> produces at most a catchable `NimbleError`, never a crash.

---

<a id="path"></a>
## path

```nimble
use path

path.join("a", "b", "c.txt")   # "a/b/c.txt"
path.basename("/home/x/f.txt")   # "f.txt"
path.dirname("/home/x/f.txt")    # "/home/x"
```

---

<a id="regex"></a>
## regex

```nimble
use regex

regex.matches("abc123", r"\d+")              # true
regex.match("abc123", r"\d+")                # "123"
regex.find_all("a1b2c3", r"\d")             # ["1","2","3"]
regex.groups("2025-01-15", r"(\d+)-(\d+)-(\d+)")
# ["2025", "01", "15"]
regex.replace("a-b-c", "-", "_")              # "a_b_c"
regex.replace_first("a-a-a", "a", "X")         # "X-a-a"
regex.split("a,b;c", r"[,;]")                # ["a","b","c"]
```

Use raw strings `r"..."` to avoid double escaping in patterns.

---

<a id="hex"></a>
## hex

```nimble
use hex

hex.encode("abc")    # "616263"
hex.decode("616263")  # "abc"
```

---

<a id="env"></a>
## env

```nimble
use env

env.get("HOME")           # value or null
env.has("PATH")           # boolean
env.set("MY_VAR", "x")   # writes an environment variable
env.all()                  # map with all of them
```

---

<a id="csv"></a>
## csv

```nimble
use csv

# Parse CSV with a header (default)
rows = csv.parse("name,age\nAna,30\nLuis,25")
# [ {name: "Ana", age: "30"}, {name: "Luis", age: "25"} ]

# Without a header -> lists of lists
rows2 = csv.parse(text, header: false)

# Generate CSV from a list of maps or a list of lists
text = csv.write(rows)
text2 = csv.write(rows, header: false)
```

---

<a id="http"></a>
## http

> Requires building with `-DHAVE_CURL -lcurl`. Otherwise, the functions raise an error
> explaining how to build with it.

```nimble
use http

r = http.get("https://api.example.com/data")
print(r.status)    # 200
print(r.body)      # response text

# With headers
r = http.get("https://api.example.com",
    headers: {"Authorization": "Bearer xyz"})

# POST
r = http.post("https://api.example.com/x",
    body: json.encode({a: 1}),
    headers: {"Content-Type": "application/json"})
```

---

<a id="net"></a>
## net (v0.10)

Raw and **blocking** TCP and UDP sockets: no event loop, no async — every
`send`/`recv`/`accept` call blocks until it's done (or until a `set_timeout` fires).
Built for test mini-servers, simple game protocols, or talking to something over a
socket by hand. It doesn't depend on libcurl or any external library — on Linux/macOS
it uses standard BSD sockets, on Windows it uses Winsock2 (you need to link `ws2_32`
by hand if you build with MinGW/g++; with MSVC, the `#pragma comment(lib, ...)` the
interpreter already ships is enough).

> Gated by the same permission as `http.get`/`http.post`:
> `interp.permissions.allowNetwork`. With `allowNetwork = false`, any `net` function
> raises a `NimbleError` instead of opening a socket.

> IPv4 only for now (`AF_INET`). No IPv6 or TLS/SSL support — for HTTPS keep using
> `http.get`/`http.post` (which do use libcurl underneath).

### Module functions

| Function | Description |
|---|---|
| `net.tcp_listen([host], [port])` | Creates a listening TCP socket and returns a **server**. `host` defaults to `"0.0.0.0"` (all interfaces); `port` defaults to `0` (the OS picks a free one — check `server.port` to find out which). |
| `net.tcp_connect(host, port)` | Connects over TCP and returns an already-connected **conn**. `host` can be a domain name (resolved via `getaddrinfo`) or an IP. |
| `net.udp_socket([host], [port])` | Creates and binds a UDP socket; returns a **udp** object. Same defaults as `tcp_listen`. |
| `net.resolve(host)` | Resolves a name to an IPv4 address (string), or raises an error if it couldn't be resolved. |

### server — what `net.tcp_listen()` returns

| Member | Description |
|---|---|
| `server.port` | The actual port it ended up listening on (useful when you asked for port `0`). |
| `server.accept()` | Blocks until an incoming connection arrives; returns a **conn**. |
| `server.set_timeout(seconds)` | If nobody connects within the given time, `accept()` raises `NimbleError` instead of blocking forever. |
| `server.close()` | Closes the listening socket. |

### conn — what `net.tcp_connect()` and `server.accept()` return

| Member | Description |
|---|---|
| `conn.remote_host`, `conn.remote_port` | IP and port of the other end. |
| `conn.send(data)` | Sends the full string (retries internally until it's all sent); returns the number of bytes sent. |
| `conn.recv([max])` | Reads up to `max` bytes (4096 by default) and returns them as a string. An empty string (`""`) means the other side closed the connection, not that nothing arrived. |
| `conn.set_timeout(seconds)` | Maximum time for `send`/`recv` before raising an error. |
| `conn.close()` | Closes the connection. |

### udp — what `net.udp_socket()` returns

| Member | Description |
|---|---|
| `udp.port` | The actual port it got bound to. |
| `udp.send_to(host, port, data)` | Sends a datagram to a specific destination. Returns bytes sent. |
| `udp.recv_from([max])` | Blocks until a datagram arrives; returns a map `{data, host, port}` with the content and where it came from. |
| `udp.set_timeout(seconds)` | Same as for TCP. |
| `udp.close()` | Closes the socket. |

> **No explicit `.close()` needed:** each handle (conn, server or udp) keeps its file
> descriptor internally via RAII. As soon as the script stops holding any reference to
> that handle, the socket closes itself. Calling `.close()` by hand is still good
> surgical practice (closing it right when you're done, rather than waiting for the
> garbage collector to notice).

### Example: TCP echo server and client

```nimble
# server.nimble
use net

srv = net.tcp_listen("127.0.0.1", 9000)
print("listening on port {srv.port}")

while true
    conn = srv.accept()
    print("connection from {conn.remote_host}:{conn.remote_port}")
    data = conn.recv()
    conn.send("echo: {data}")
    conn.close()
end
```

```nimble
# client.nimble
use net

c = net.tcp_connect("127.0.0.1", 9000)
c.send("hello\r\n")
print(c.recv())    # "echo: hello\r\n"
c.close()
```

### Example: speaking HTTP by hand (line by line, with `\r\n`)

A typical case where the new `\r` escape from v0.11 matters: hand-building the request
for a text protocol like HTTP, which requires exact CRLF line terminators.

```nimble
use net

c = net.tcp_connect("example.com", 80)
c.set_timeout(5)
c.send("GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n")

response = ""
while true
    chunk = c.recv()
    if chunk == "" break end   # "" == the other side closed
    response += chunk
end
print(response)
c.close()
```

> For HTTPS, or to avoid dealing with the protocol by hand, keep using
> [http.get / http.post](#http), which speak full HTTP(S) for you via libcurl. `net`
> is the lower-level layer, for when you need the raw socket (custom protocols, UDP,
> servers).

### Example: UDP

```nimble
use net

a = net.udp_socket("127.0.0.1", 9100)
b = net.udp_socket("127.0.0.1")   # port 0 -> the OS picks one

b.send_to("127.0.0.1", 9100, "ping")
msg = a.recv_from()
print("{msg.data} from {msg.host}:{msg.port}")   # "ping from 127.0.0.1:<b's port>"
```

### Name resolution

```nimble
use net

net.resolve("example.com")   # e.g. "93.184.216.34"
```

---

<a id="files"></a>
## Files (I/O)

Global functions, no module needed:

```nimble
write("output.txt", "Hello\n")         # creates/overwrites
append("output.txt", "World\n")        # appends at the end
content = read("output.txt")
exists("output.txt")                   # true/false
listdir(".")                          # list of names
mkdir("new_folder")
rmdir("new_folder")
delete("output.txt")                   # returns true if it worked
```

---

<a id="gc"></a>
## gc — Cycle collector (v0.7)

Nimble uses `shared_ptr` for memory management, which automatically frees everything
that isn't part of a reference cycle. For cycles (closures that reference themselves,
objects that point at each other, etc.), there's a trial-deletion cycle collector
(similar to CPython's).

```nimble
use gc

gc.collect()   # forces a collection; returns how many nodes it freed
gc.stats()     # returns a map with statistics
```

`gc.stats()` returns:

| Field | Description |
|---|---|
| `tracked` | Number of nodes currently tracked. |
| `last_collected` | Nodes freed in the last collection. |
| `total_collected` | Total nodes freed since startup. |

> The collector runs automatically every ~5000 allocations of tracked types (Env,
> FunctionObj, ClassObj). Most scripts won't need to call it manually.

---

<a id="tests"></a>
## Test framework

You can define `test "name"` blocks and run them with `nimble test file.nimble`:

```nimble
func add(a, b)
    return a + b
end

test "basic addition"
    assert_eq(add(2, 3), 5)
end

test "addition with negatives"
    assert_eq(add(-1, 1), 0)
    assert_ne(add(1, 1), 3)
end

test "empty list"
    assert_true(length([]) == 0)
    assert_false(length([1]) == 0)
end
```

```
$ nimble test examples.nimble
  ✓ basic addition
  ✓ addition with negatives
  ✓ empty list

3 passed, 0 failed
```

> `test` blocks are registered while the main program runs. If a test hits `return`,
> it counts as passed. If `assert`/`panic` aborts it, it's reported as a failure.

---

<a id="embed-intro"></a>
## Embedding in C++

The interpreter can be used as a library inside a C++ application. This section
documents the public embedding API.

### Requirements

- C++17 compiler or newer.
- Define `NIMBLE_NO_MAIN` before including `nimble.cpp`.
- If your build system would rather not include a `.cpp` file, compile `nimble.cpp`
  with `-DNIMBLE_NO_MAIN` as a separate compilation unit and declare the class in your
  own header. For quick use, just including the `.cpp` is simplest.

---

<a id="embed-basic"></a>
## Basic usage

```cpp
// host.cpp
#define NIMBLE_NO_MAIN
#include "nimble.cpp"

#include <iostream>

int main() {
    // Arguments visible to the script as system.args
    Interpreter interp({"my_app", "--mode", "debug"});

    // Nimble source code
    std::string src = R"(
print("Hello from the script")
for i in range(3)
    print("iteration {i}")
end
)";

    // Lex + parse
    Lexer lex(src);
    auto toks = lex.tokenize();
    Parser parser(toks);
    auto program = parser.parseProgram();

    // Run
    interp.run(program);
    return 0;
}
```

### Building the host

```bash
g++ -std=c++17 -O2 host.cpp -o my_app
# With curl
g++ -std=c++17 -O2 -DHAVE_CURL host.cpp -o my_app -lcurl
```

---

<a id="embed-natives"></a>
## Registering native functions

Expose C++ functions to the script with `registerNative`:

```cpp
#define NIMBLE_NO_MAIN
#include "nimble.cpp"

int main() {
    Interpreter interp;

    // Signature: Value(std::vector<Value>& args,
    //              std::vector<std::pair<std::string,Value>>& named,
    //              Interpreter& interp)
    interp.registerNative("greet",
        [](std::vector<Value>& args,
           std::vector<std::pair<std::string,Value>>&,
           Interpreter&) -> Value {
            std::string name = args.empty() ? "world" : args[0].asStr();
            return Value("Hello, " + name + "!");
        });

    interp.registerNative("double",
        [](std::vector<Value>& args, auto&, Interpreter&) -> Value {
            return Value(args[0].asNum() * 2);
        });

    // ...load and run a script that uses greet(...) and double(...)
}
```

> **Note:** when a native function is called, the arguments passed by the script are
> already evaluated. Named parameters arrive in the second vector. Validating the
> count isn't required, but it's good practice — raise a `NimbleError` with a clear
> message if something's missing.

---

<a id="embed-call"></a>
## Calling script functions from C++

Use `callGlobal` to invoke global functions defined by the script (for example hooks
like `_ready`, `_process`, `on_event`):

```cpp
Interpreter interp;
// ... load and run the script ...

// Call with no arguments
interp.callGlobal("_ready");

// Pass arguments
std::vector<Value> args;
args.push_back(Value(0.016));   // delta time
args.push_back(Value("frame"));
interp.callGlobal("_process", args);

// Collect the return value
Value v = interp.callGlobal("get_score");
if (v.isNum()) std::cout << "Score: " << v.asNum() << "\n";
```

`callGlobal` raises `NimbleError` if the function doesn't exist or isn't callable.

---

<a id="embed-sandbox"></a>
## Sandbox and permissions

The interpreter exposes `interp.permissions`, a `SandboxPermissions` struct. By
default **everything is allowed**, so the CLI behaves the same as always. In embedded
mode you can restrict it before running any script.

| Field | Controls |
|---|---|
| `allowFileRead` | `read`, `exists` |
| `allowFileWrite` | `write`, `append` |
| `allowFileSystemOps` | `delete`, `listdir`, `mkdir`, `rmdir` |
| `allowFileModules` | `use "file.nimble"` and file modules |
| `allowSystemExec` | `system.run`, `system.exec` |
| `allowNetwork` | `http.get`, `http.post`, and all of `net.*` (v0.10) (`tcp_listen`, `tcp_connect`, `udp_socket`, `resolve`) |
| `allowEnvRead` | `env.get`, `env.has`, `env.all` |
| `allowEnvWrite` | `env.set` |
| `allowExit` (v0.8) | `exit()` |
| `confineToRoot` + `sandboxRoot` | Confines all paths to a base folder |

### Example: blocking everything dangerous

```cpp
Interpreter interp;

interp.permissions.allowFileRead      = true;
interp.permissions.allowFileWrite     = false;
interp.permissions.allowFileSystemOps = false;
interp.permissions.allowFileModules   = true;
interp.permissions.allowSystemExec    = false;
interp.permissions.allowNetwork       = false;
interp.permissions.allowEnvRead       = true;
interp.permissions.allowEnvWrite      = false;
interp.permissions.allowExit          = false;

// Confine all I/O to ./scripts/assets/
interp.permissions.confineToRoot = true;
interp.permissions.sandboxRoot   = "./scripts/assets";

// ... run the script ...
```

> **Honest warning about the sandbox:** path confinement is a *lexical* defense
> (normalization). It does **not** protect against symlinks pointing outside the
> sandbox. If you need real isolation, consider running the interpreter in a separate
> process with `seccomp`, a container, or similar.

---

<a id="embed-limits"></a>
## Execution limits

To protect yourself against infinite loops and runaway recursion:

### Step limit

```cpp
interp.setStepLimit(1'000'000);   // max "steps" (iterations and similar)
```

### Time limit

```cpp
interp.setTimeLimit(2.5);   // seconds
```

### Recursion depth

```cpp
interp.maxCallDepth = 200;   // 500 by default (important if you run this on threads with a small stack)
```

When a limit is exceeded, `Interpreter::ExecutionLimitExceeded` is raised. This
exception **cannot** be caught by the script's `try/catch`: if it could, a malicious
script could escape the limit just by catching it.

```cpp
Interpreter interp;
interp.setStepLimit(100000);
interp.setTimeLimit(1.0);

try {
    interp.run(program);
} catch (Interpreter::ExecutionLimitExceeded& e) {
    std::cerr << "Script exceeded the limit: " << e.message << "\n";
}
```

### When to call `checkLimits`

Internally it's called inside `while`/`for` loops and other hot spots. If your host
needs finer control (say, per frame), you can call `interp.checkLimits()` manually
between hooks.

---

<a id="embed-modules"></a>
## Modules embedded in the executable

If you're distributing a single binary, you can embed `.nimble` modules inside the
executable instead of reading them from disk:

```cpp
std::unordered_map<std::string, std::string> modules;

// The key is the path as resolved by the interpreter. For example, if the
// script does  use "utils.nimble"  and scriptDir is ".",
// the key will be something like "./utils.nimble" (normalized).
modules["./utils.nimble"] = R"(
func add(a, b)
    return a + b
end
)";

Interpreter interp;
interp.embeddedSources = &modules;

// Now use "utils.nimble" will pull the module from the map.
```

> When `embeddedSources` is set, any `use "file.nimble"` is looked up in the map
> first. If it isn't there, `"Module not bundled into the executable: ..."` is
> raised.

---

<a id="embed-values"></a>
## Working with `Value`

The `Value` class is the type of every value in the interpreter. Relevant API:

### Construction

```cpp
Value d(3.14);                    // number (double)
Value n(42);                      // number (int)   -- see v0.9 note below
Value b(true);                    // boolean
Value s("hello");                 // string (const char*) -- see v0.9 note below
Value s2(std::string("hello"));   // string (std::string), still works the same way
Value nul;                        // null

// List
auto l = std::make_shared<ListObj>();
l->items.push_back(Value(1));
l->items.push_back(Value(2));
Value list(l);

// Map
auto m = std::make_shared<MapObj>();
m->set("key", Value(42));
Value map(m);
```

> **Important change in v0.9:** `Value(int)` and `Value(const char*)` were added in
> v0.9. Before:
> - `Value(42)` was a **compile error** (ambiguous between `Value(double)` and
>   `Value(bool)`).
> - `Value("text")` compiled **with no error or warning at all**, but the result was
>   `Value(true)` — a boolean — because the standard pointer-to-`bool` conversion wins
>   overload resolution over the user-defined conversion from `const char*` to
>   `std::string`.
>
> If you have embedding code written before v0.9 that avoided this problem by manually
> wrapping literals (`Value(std::string("text"))` or `Value((double)42)`), that code
> keeps working exactly the same. The risk is only in *new* code that writes
> `Value("text")` or `Value(42)` today expecting the old behavior (a different compile
> result, or failing); now both do the obvious thing.

### Querying

```cpp
if (v.isNum())    { double d = v.asNum(); ... }
if (v.isStr())    { const std::string& s = v.asStr(); ... }
if (v.isBool())   { bool b = v.asBool(); ... }
if (v.isList())   { auto l = v.asList(); ... }
if (v.isMap())    { auto m = v.asMap(); ... }
if (v.isFunc())   { ... }
if (v.isObj())    { auto e = v.asObj(); ... }
if (v.isClass())  { auto c = v.asClass(); ... }
if (v.isNull())   { ... }
```

### Converting to a string

```cpp
std::string txt = toDisplayString(v);
```

### Returning lists to the script

```cpp
interp.registerNative("get_enemies",
    [](std::vector<Value>&, auto&, Interpreter&) -> Value {
        auto l = std::make_shared<ListObj>();
        l->items.push_back(Value("goblin"));
        l->items.push_back(Value("orc"));
        return Value(l);
    });
```

### Returning maps to the script

```cpp
interp.registerNative("get_config",
    [](std::vector<Value>&, auto&, Interpreter&) -> Value {
        auto m = std::make_shared<MapObj>();
        m->set("width",  Value(800));
        m->set("height", Value(600));
        m->set("title",  Value(std::string("My game")));
        return Value(m);
    });
```

### Raising errors into the script

```cpp
throw NimbleError("something went wrong");
```

The script can catch it with `try/catch`.

---

<a id="embed-gc"></a>
## Cycle collector on the host side (v0.7)

If your host creates a lot of long-lived Nimble objects (closures, classes), the
cycle collector works automatically. But you can also expose it to the script:

```cpp
// In the script:
use gc
gc.collect()   # force a collection
stats = gc.stats()
print(stats.tracked)
```

> **Known limitation:** the collector can't inspect the captures of a native
> `std::function`. If a native function captures a `Value` that in turn creates a
> cycle, that cycle won't be detected. In practice, this only affects native wrappers
> created dynamically and stored inside long-lived objects.

---

<a id="embed-example"></a>
## Full example: game scripting

```cpp
// game.cpp
#define NIMBLE_NO_MAIN
#include "nimble.cpp"
#include <iostream>

int main() {
    Interpreter interp({"game"});

    // --- Sandbox: user script with minimal permissions ---
    interp.permissions.allowFileRead      = false;
    interp.permissions.allowFileWrite     = false;
    interp.permissions.allowFileSystemOps = false;
    interp.permissions.allowFileModules   = false;
    interp.permissions.allowSystemExec    = false;
    interp.permissions.allowNetwork       = false;
    interp.permissions.allowEnvRead       = false;
    interp.permissions.allowEnvWrite      = false;
    interp.permissions.allowExit          = false;

    // --- Limits ---
    interp.setStepLimit(500000);
    interp.setTimeLimit(1.0);   // 1 s per _process call
    interp.maxCallDepth = 300;

    // --- Engine API exposed to the script ---
    interp.registerNative("spawn_enemy",
        [](std::vector<Value>& a, auto&, Interpreter&) -> Value {
            std::string type = a.empty() ? "goblin" : a[0].asStr();
            std::cout << "[engine] spawn " << type << "\n";
            return Value((double)std::rand());
        });

    interp.registerNative("get_delta",
        [](std::vector<Value>&, auto&, Interpreter&) -> Value {
            return Value(0.016);
        });

    // --- Load the user script ---
    std::string src = R"(
func _ready()
    print("Script ready")
    self.time = 0
end

func _process(dt)
    self.time = (self.time ?? 0) + dt
    if self.time > 1.0
        spawn_enemy("orc")
        self.time = 0
    end
end
)";

    Lexer lex(src);
    auto toks = lex.tokenize();
    Parser parser(toks);
    auto program = parser.parseProgram();

    try {
        interp.run(program);
        interp.callGlobal("_ready");

        // Simulate 3 seconds of frames
        for (int i = 0; i < 180; ++i) {
            std::vector<Value> args{ interp.callGlobal("get_delta") };
            interp.callGlobal("_process", args);
        }
    } catch (Interpreter::ExecutionLimitExceeded& e) {
        std::cerr << "Script exceeded limits: " << e.message << "\n";
    } catch (NimbleError& e) {
        std::cerr << "Error in script: " << e.what() << "\n";
    }

    return 0;
}
```

---

## Changelog summary

**Nimble v0.11** — guide generated from `nimble.cpp`. See the source file's comments
for internal details.

**v0.11:** `\r` is now a real escape sequence in strings.

**v0.10:** new `net` module (TCP/UDP sockets); `string.starts_with`/`ends_with`;
`list.index_of`; `math.round(x, decimals)`; fixed `path.basename`/`dirname` on
Windows-style paths; fixed `use net` (was missing from the native-module allowlist).

**v0.9 (bug fixes, see above):** `json.decode()` can no longer crash the process on
malformed input (neither invalid numbers nor non-string object keys); invalid string
interpolations now raise a catchable `NimbleError` instead of substituting silent
filler text; `Value(int)` and `Value(const char*)` were added to the embedding API
(before, `Value(42)` didn't compile and `Value("text")` silently produced a boolean);
`for a, b in list` now correctly destructures each pair (before it only worked over
maps); `use gc` finally works (it was missing from `use`'s native-module allowlist);
and, the most important fix for the sandbox, dozens of native functions (`ord`,
`read`/`write`, the string methods, `min`/`max`, `map`/`filter`/`reduce`, and
practically all of the `time`/`system`/`regex`/`hex`/`env`/`csv`/`http` modules) no
longer crash the whole process when given a wrong-typed or missing argument — they now
raise a catchable `NimbleError`, closing a serious hole in the sandbox model.

**v0.8:** `try/catch/finally`, `keys()`/`values()`, `exit()` with `ExitSignal`,
`print_err()`, the `allowExit` permission.

**v0.7:** cycle collector (`gc.collect()`, `gc.stats()`), method lookup caching.

**v0.6:** exception-free control flow, error messages translated to English.

**v0.5:** `optionalNewline()` (one-line blocks), multi-line lists.
