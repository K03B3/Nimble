// Nimble interpreter - C++17
// v0.20 - Lectura de archivos linea a linea, nativa:
//   read_lines(ruta)  -> lista de lineas (LF/CRLF, sin "" final).
//   open_lines(ruta)  -> lector perezoso de memoria constante:
//                        `for linea in open_lines(ruta)` o r.read_line()
//                        (null al final) y r.close().
//   Misma semantica que conn.recv_line(). Como el `for` copiaba el iterable
//   entero, se agrego un protocolo de iteracion perezosa: un mapa con una
//   funcion "__next__" se recorre hasta que devuelva null. Respeta los
//   mismos permisos y confinamiento de rutas que read().
//   Ademas: el banner del REPL seguia diciendo v0.16 (no se habia
//   actualizado en v0.17-v0.19); ahora muestra v0.20.
// v0.19 - Nuevo modulo nativo `hash`: hash.md5(datos) devuelve el digest MD5
//   como string hex de 32 caracteres, y hash.md5_raw(datos) los 16 bytes
//   crudos. Implementacion propia del RFC 1321 (sin dependencias). Opera
//   sobre bytes, asi que funciona con texto, con \0 y con datos binarios.
//   MD5 sirve para checksums e identificadores; NO es seguro para
//   contrasenas ni firmas.
// v0.18 - Parametros por defecto multiples: func f(a = 1, b = 2) fallaba al
//   llamarla con "Cannot destructure: value is not a list" (y con 3 o mas
//   por defecto, con un error de sintaxis). Causa: los valores por defecto
//   se parseaban con parseExpr(), que admite la asignacion multiple sin
//   corchetes (a, b = 1, 2). Al leer el default "1" de "a = 1, b = 2" veia
//   la coma, probaba "b" y al encontrar "=" lo tomaba como el destructuring
//   [1, b] = 2. Con un solo parametro por defecto no hay coma, por eso
//   nunca se notaba. Fix: los defaults usan parseTernary() (la coma
//   separa parametros) y se agrego parseElemExpr() (parseAssign sin tuplas)
//   para los demas sitios donde la coma es un separador y tenia el mismo
//   defecto latente: argumentos de llamada, elementos de lista y valores de
//   mapa. A nivel de sentencia "a, b = b, a" sigue funcionando igual.
// v0.17 - conn.send_nb() ahora es no bloqueante de verdad. Bug encontrado
//   al escribir un ejemplo de broadcast con cola de salida por conexion:
//   send_nb() hacia poll(POLLOUT, 0ms) y despues un ::send() normal sobre
//   un socket bloqueante. POLLOUT solo garantiza que hay algo de espacio en
//   el buffer de salida, no que quepa todo `data`: si el script le pasaba
//   una cola grande (varios MB) a un peer que no leia, el ::send() esperaba
//   hasta poder colocarla entera, y el loop completo -- todos los demas
//   clientes incluidos -- quedaba congelado, justo lo que send_nb() debia
//   evitar. Fix: POSIX usa MSG_DONTWAIT en ese send (acepta lo que quepa o
//   devuelve EAGAIN, que ya se traducia a 0); Windows, que no tiene esa
//   flag, pone el socket en modo no bloqueante solo durante la llamada
//   (ioctlsocket/FIONBIO) y lo restaura. Sin cambios para scripts
//   existentes: el contrato documentado (devuelve bytes aceptados, 0 si no
//   hay lugar, nunca bloquea) es el mismo, ahora se cumple.
// v0.16 - Non-blocking writes for net sockets. General gap, not specific to
//   any one script: net.poll() only ever asked the OS about POLLIN, and
//   conn.send() loops until every byte is sent, with a timeout that resets
//   per internal ::send() call rather than covering the whole call -- so a
//   single slow peer being served from the same event loop as others (a
//   broadcast/relay/proxy pattern, not just radio_server.nimble) could stall
//   everyone else far longer than set_timeout() suggests, and there was no
//   way for a script to ask "can I write to this socket without blocking?"
//   at all. Three additive changes, all opt-in -- no existing script's
//   behavior changes:
//   - net.poll(list, timeout): a list item can now be a dict
//     {"sock": conn, "read": bool, "write": bool} instead of a bare socket,
//     to also watch for write-readiness (POLLOUT). Bare sockets behave
//     exactly as before (read-only interest). Matched dicts come back with
//     "readable"/"writable" set so the script knows which fired.
//   - conn.send_nb(data): one non-blocking-safe attempt (poll for POLLOUT
//     with a 0ms timeout, then a single ::send()) -- returns bytes actually
//     accepted this call, 0 if the socket has no room right now, never
//     blocks, never throws on would-block. Lets a script keep a per-
//     connection output queue and never let one slow connection hold up the
//     rest of the loop. conn.send() itself is unchanged.
//   - conn.set_nodelay(bool): TCP_NODELAY, useful for any chunked or
//     low-latency protocol, not just one kind of script.
// v0.15 - Windows pass over net + filesystem (found while running
//   radio_server.nimble on Windows; every item below is a spot where the
//   POSIX code path was fine and the Windows one was silently different):
//   - net.tcp_listen(): SO_REUSEADDR on Windows lets TWO processes listen
//     on the same port with no error (the opposite of Linux, where it only
//     skips TIME_WAIT). Windows now uses SO_EXCLUSIVEADDRUSE, so a second
//     server on a busy port fails with a bind error, like on Linux.
//   - Socket errors on Windows were just "codigo 10048". They now carry the
//     system's own text (FormatMessageW, in UTF-8) plus the code.
//   - net.poll() with an empty list: POSIX poll() just waits; WSAPoll()
//     rejects it. Windows now waits too, so both behave the same.
//   - UTF-8 end to end on Windows. Every Nimble string is UTF-8, but the
//     "narrow" Windows APIs (fs::path(std::string), ifstream(std::string),
//     path.string(), argv, _mkdir...) use the ANSI code page. A file called
//     "Cancion con acento.mp3" came out of listdir() as CP1252 bytes (not
//     valid UTF-8, so JSON/HTML built from it showed U+FFFD), and one with
//     characters outside the ANSI page could not be opened at all. All
//     filesystem access now goes through nimblePath()/nimblePathStr()
//     (wide APIs on Windows, plain pass-through on POSIX); argv is read with
//     GetCommandLineW; the console is switched to UTF-8 while the process
//     runs (and restored on exit). Paths in scripts that are NOT valid UTF-8
//     (e.g. a script saved as ANSI) fall back to the ANSI code page instead
//     of failing, so existing scripts keep working.
//   - A UTF-8 BOM at the start of a script/module (Notepad's "UTF-8 with BOM",
//     PowerShell 5's -Encoding utf8) used to fail with "Unexpected character".
//     The Lexer now skips it (source code only; read() of user data is untouched).
//   Not covered: env.get()/env.has()/env.set() and system.run()/exec() still
//   use the narrow ANSI APIs on Windows.
// v0.14 - SIGPIPE fix, found by actually running radio_server.nimble with
//   listeners connecting and disconnecting for real. Writing to a socket
//   whose peer already closed the connection (conn.send() to a listener
//   that dropped) raises SIGPIPE on POSIX; left at its default
//   disposition, that terminates the whole process immediately, before
//   it ever reaches the try/catch around conn.send() -- a signal isn't a
//   C++ exception, no amount of catching in Nimble script code helps.
//   Confirmed the causal link with a paired test: the exact same
//   "listener disconnects mid-stream, a second listener should keep
//   receiving" scenario kills the process with SIGPIPE left at its
//   default (SIG_DFL) and leaves it running fine with it ignored
//   (SIG_IGN) -- same binary, same script, only that one line differs.
//   Fix is one signal(SIGPIPE, SIG_IGN) call in main() (POSIX only --
//   Windows has no SIGPIPE, ::send() there already fails with
//   WSAECONNRESET, which conn.send() already turns into a catchable
//   NimbleError). With the signal ignored, send()/write() to a closed
//   socket now just return -1 with errno=EPIPE like any other socket
//   error, which conn.send()'s existing "n <= 0 -> throw" already
//   handles -- no change needed there at all.
// v0.13 - net.poll() plus the two file-I/O primitives it and real media
//   streaming needed. Written against a second real program: a live
//   internet-radio server (one shared mp3 stream broadcast to every
//   connected listener at once) and a static file server that streams
//   video with byte-range support.
//   - file_size(path): std::filesystem::file_size(), non-throwing (see
//     is_dir/is_file in v0.12 for why "ask the filesystem, don't use an
//     exception as a status code" matters here too). Lets a caller know
//     how large a file is without reading it.
//   - read_range(path, offset, length): reads at most `length` bytes
//     starting at `offset` without loading the rest of the file. Backs
//     HTTP Range/206 responses (seeking in a <video>) and radio_server's
//     chunked broadcast reads. Returns fewer bytes than asked for at EOF
//     rather than erroring -- ifstream::gcount() after read() already
//     tells the caller exactly how much came back.
//   - net.poll(sockets, timeout_seconds): multiplexes N server/conn/udp
//     sockets (they all now carry an internal "fd" field for this) with
//     a single ::poll() (WSAPoll() on Windows -- same struct shape, same
//     fields, so it's one implementation behind an #ifdef, not two).
//     Before this, every script in this file that opened a socket was
//     stuck serving one client at a time: server.accept() and
//     conn.recv()/recv_line() all block, so a second listener connecting
//     while the first was being served just sat in the OS backlog
//     waiting its turn. net.poll() is what let radio_server.nimble
//     accept new listeners and keep pushing audio to already-connected
//     ones in the same loop, with no threads anywhere. Confirmed with an
//     actual two-listener test: a listener that connects mid-broadcast
//     starts receiving audio from wherever the stream currently is (not
//     from byte 0), and two simultaneous listeners receive byte-for-byte
//     identical data -- a real synchronized broadcast, not two
//     independent per-listener streams that happen to read the same
//     files.
// v0.12 - net/io robustness pass plus one lexer bugfix found while writing
//   a real HTTP server against this interpreter (a small manga-reader app
//   that serves images over raw sockets):
//   - net: conn.recv_line() reads up to the next '\n' (accepts bare LF or
//     CRLF), buffering whatever it over-reads on NetSocket so a later
//     recv_line()/recv() picks up exactly where the last one left off.
//     conn.recv() now drains that buffer first before doing a raw ::recv,
//     so interleaving recv() and recv_line() on the same connection can no
//     longer drop or reorder bytes. recv_line() also caps how large a
//     single line can grow (default 64KB, overridable) before raising --
//     without that limit a peer that never sends '\n' would grow the
//     buffer unboundedly, the same class of unguarded-input problem the
//     v0.9 json.decode fix closed.
//   - is_dir(path) / is_file(path): before this, telling a directory from

//     a file meant calling listdir() and using the NimbleError it throws
//     on a non-directory as an ad-hoc "is this a folder?" check -- using
//     exceptions for expected, everyday control flow, which is exactly
//     what v0.6 moved break/continue/return away from. Both are
//     std::filesystem-backed, non-throwing (they return false for a path
//     that doesn't exist rather than raising), and gated behind the same
//     permissions.allowFileRead / confinePath sandbox check as exists().
//   - Lexer: an unterminated single-quote string ("...) no longer silently
//     swallows the rest of the source file looking for the next '"'. It
//     used to keep consuming *across newlines* -- including real code --
//     until it happened to hit some later, unrelated '"' character
//     (closing whatever string literal came next), or hit end-of-file
//     with no error at all. Either way the failure (if any) surfaced many
//     lines away from the actual missing quote, as a confusing, unrelated
//     parse error. Now a raw newline or EOF inside a single-quote string
//     raises "Unterminated string literal" pointing at the line the
//     string opened on. Triple-quoted strings ("""...""") are unaffected
//     -- they're still the intended way to write a literal that spans
//     multiple lines.
//   - ErrHelp::suggest()'s "did you mean 'x'?" text was in Spanish
//     ("¿quisiste decir?"), the only non-English fragment inside an
//     otherwise all-English error message ("Undefined variable: 'x' --
//     ¿quisiste decir 'y'?"). Translated for consistency with the rest of
//     the error strings (the v0.6 pass that translated error/warning text
//     predates this helper, so it was missed then).
// v0.11  func multi-line, new error message
//   - String literals now support \r as an escape sequence. Before this,
//     "\r" fell through to the switch's default case, which kept the
//     backslash literally (a string like "line\r\n" contained the four
//     characters \,r,\n instead of a real carriage return) -- silently
//     wrong input for any code building CRLF-terminated text, which the
//     new net module needs for line-oriented TCP protocols. \n, \t, \",
//     \\, \{ and \} were unaffected; only \r was missing.
// v0.10 - Small, targeted additions plus one more platform-consistency fix
//   found while looking for others like the v0.9 listdir() bug:
//   - path.basename()/dirname() used to search for '/' by hand, so a
//     Windows-style path (C:\Users\foo\bar.txt) silently came back with the
//     whole string as the "basename". Rewritten on std::filesystem::path,
//     which understands both separators.
//   - string.starts_with() / string.ends_with(): the one genuinely missing
//     string check -- contains()/find() don't anchor to a position.
//   - list.index_of(): find a value's position without a manual loop. Uses
//     the same structuralEquals() that already backs == and `in`, so it
//     agrees with those instead of defining equality a third way.
//   - math.round(x, decimals): rounding to N decimal places. Added as
//     math.round rather than a second argument on the existing global
//     round(), so every existing single-arg round() call site is untouched.
// v0.9 - Bugfix pass triggered by a documentation audit (found by testing
//   every documented example against the real interpreter):
//   - json.decode() could crash the whole process (std::terminate, via an
//     uncaught std::invalid_argument from std::stod) on malformed input --
//     including on `{}`, the empty object. Root cause: string interpolation
//     silently substituted a "<e>" placeholder into the string when the
//     content between `{`/`}` failed to parse as an expression (instead of
//     raising an error), and that placeholder text is what then reached
//     json.decode's unguarded number parser. Both are fixed: interpolation
//     failures now raise a catchable NimbleError, and json.decode validates
//     numbers/true/false/null before consuming them, so it can no longer
//     crash the process on any input.
//   - Value gained `Value(int)` and `Value(const char*)` constructors.
//     Before this, `Value(42)` was a compile error (ambiguous between
//     Value(double) and Value(bool)), and worse, `Value("text")` compiled
//     silently but produced Value(true) -- a boolean -- because pointer-to-
//     bool is a standard conversion and beats the user-defined const char*
//     -> std::string conversion in overload resolution. Every embedding
//     example in the docs that wrote a literal was affected.
//   - `for a, b in someList` now destructures each item (assumed to be a
//     2-element list) into the two loop variables, same as it already did
//     for maps. Before this, the two-variable form only worked over maps;
//     over a list it silently bound the WHOLE item to the first variable
//     and left the second variable undefined -- breaking the documented
//     `for i, x in enumerate(list)` idiom, arguably the most common reason
//     to use two loop variables at all.
// v0.8 - Added four language/host features (all with regression tests):
//   - try/catch/finally: `finally` runs unconditionally (success, caught
//     error, uncaught error, or a return/break/continue in flight); its own
//     control flow takes precedence, matching Java/Python/JS semantics.
//     (catch without a variable name already worked before this version.)
//   - keys(m) / values(m) as free functions (parallel to length(v)) --
//     deliberately NOT map.has()/.keys()/.values() as dot-methods, since
//     `.foo` on a map already means "get key foo"; a dot-method would
//     shadow a real key of the same name. map.has() was also skipped
//     because `"k" in m` already does the same thing.
//   - exit(code): raises an ExitSignal exception caught only at the
//     outermost level (main/REPL/test runner) instead of calling
//     std::exit() directly from inside a native function, so every RAII
//     destructor between the call site and the catch (open files, curl
//     handles, ...) still runs normally first. Gated by a new
//     permissions.allowExit sandbox flag, same pattern as system.exec()/
//     http.get().
//   - print_err(): like print(), but writes to stderr. (read_line() was
//     skipped: input() with no arguments already does exactly this.)
// v0.7 - Added a trial-deletion cycle collector (Env/FunctionObj/ClassObj)
//   that closes the two closure-related memory leaks documented in
//   docs/memory-model.md, plus gc.collect()/gc.stats() for manual control.
//   Also caches ClassObj::findMethod's `extends`-chain walk (safe: method
//   maps are only ever populated once, at class-declaration time) --
//   measured ~35% faster method calls on a 40-level-deep inheritance chain.
// v0.6 - Changes over v0.5:
//   - Control flow (break/continue/return) is threaded through explicit
//     return values instead of C++ exceptions, and no longer leaks across
//     function-call or module-import boundaries (see notes near ExecResult
//     and invokeUserFunction).
//   - All error and warning messages translated to English and reworded
//     for clarity/consistency.
//
// v0.5 added: optionalNewline() (one-line blocks), multi-line list literals.
// v0.4 added: env, csv, system.exec, extended time, aggregations,
//             test framework, HTTP via libcurl.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>
#include <memory>
#include <variant>
#include <functional>
#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <stdexcept>
#include <exception>
#include <random>
#include <regex>
#include <chrono>
#include <thread>
#include <filesystem>
#include <optional>
#include <ctime>
#include <csignal>
#include <charconv>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _WIN32
#include <direct.h>
#include <process.h>
// net (módulo TCP/UDP): en Windows hace falta enlazar con ws2_32 a mano,
// el intérprete no lo hace por vos (ej: `g++ ... -lws2_32` con MinGW; con
// MSVC alcanza con este #pragma).
#include <winsock2.h>
#include <ws2tcpip.h>
#include <shellapi.h>   // CommandLineToArgvW (argv en UTF-8)
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "shell32.lib")
#endif
#else
#include <sys/wait.h>
#include <dirent.h>
#include <unistd.h>
// net (módulo TCP/UDP): sockets BSD estándar, sin librerías extra.
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <cerrno>
#include <poll.h>
#endif

#ifdef HAVE_CURL
#include <curl/curl.h>
#endif

// ============================================================
// Rutas y texto: UTF-8 <-> sistema
// ============================================================
// Todo string de Nimble es UTF-8. En Linux/macOS el sistema de archivos
// tambien, asi que no hay nada que convertir. En Windows NO: las APIs
// "narrow" (ifstream(std::string), fs::path(std::string), path.string(),
// argv, _mkdir...) usan la pagina de codigos ANSI (CP1252 en un Windows en
// espanol), no UTF-8. nimblePath()/nimblePathStr()/nimblePathGeneric() son
// la unica puerta hacia y desde std::filesystem; en POSIX son un simple
// paso directo.
#ifdef _WIN32
static std::wstring nimbleUtf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    // MB_ERR_INVALID_CHARS: sin el, Windows "arregla" el UTF-8 invalido con
    // U+FFFD en silencio. Si no es UTF-8 valido (ej. un script guardado como
    // ANSI) se interpreta con la pagina ANSI, como hacia antes de v0.15.
    UINT cp = CP_UTF8;
    int n = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) {
        cp = CP_ACP;
        n = MultiByteToWideChar(cp, 0, s.data(), (int)s.size(), nullptr, 0);
        if (n <= 0) return std::wstring();
    }
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(cp, cp == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
static std::string nimbleWideToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
#endif
static std::filesystem::path nimblePath(const std::string& utf8) {
#ifdef _WIN32
    return std::filesystem::path(nimbleUtf8ToWide(utf8));
#else
    return std::filesystem::path(utf8);
#endif
}
static std::string nimblePathStr(const std::filesystem::path& p) {
#ifdef _WIN32
    return nimbleWideToUtf8(p.wstring());
#else
    return p.string();
#endif
}
static std::string nimblePathGeneric(const std::filesystem::path& p) {
#ifdef _WIN32
    return nimbleWideToUtf8(p.generic_wstring());
#else
    return p.generic_string();
#endif
}


// ============================================================
// Warnings
// ============================================================
namespace Warnings {
    inline bool enabled = true;
    inline void emit(const std::string& msg, int line) {
        if (!enabled) return;
        std::cerr << "warning: [line " << line << "] " << msg << "\n";
    }
}

// ============================================================
// Error message helpers: source-line context + "did you mean...?"
// ============================================================
// Two small, self-contained additions used only when *printing* an error
// (never during normal evaluation, so none of this is perf-sensitive):
//   - sourceLines/setSource/lineText let printError() show the actual
//     offending line next to "[line N]" instead of leaving the user to go
//     count lines by hand.
//   - distance()/suggest() power "did you mean 'x'?" on typo-shaped
//     errors (undefined variable, unknown list/string member).
namespace ErrHelp {
    inline std::vector<std::string> sourceLines;

    inline void setSource(const std::string& src) {
        sourceLines.clear();
        std::string cur;
        for (char c : src) {
            if (c == '\n') { sourceLines.push_back(cur); cur.clear(); }
            else cur += c;
        }
        sourceLines.push_back(cur);
    }

    // Devuelve la línea `line` (1-indexada) recortada de espacios/tabs
    // iniciales, o "" si no hay fuente cargada o el número está fuera de
    // rango (p.ej. errores sintéticos sin línea real).
    inline std::string lineText(int line) {
        if (line <= 0 || (size_t)line > sourceLines.size()) return "";
        const std::string& t = sourceLines[line - 1];
        size_t start = t.find_first_not_of(" \t");
        return start == std::string::npos ? "" : t.substr(start);
    }

    // Distancia de edición (Levenshtein) clásica con tabla O(n*m). Sólo se
    // usa para sugerencias al imprimir un error, nunca en un camino
    // caliente, así que la implementación simple es suficiente.
    inline int distance(const std::string& a, const std::string& b) {
        size_t n = a.size(), m = b.size();
        std::vector<std::vector<int>> d(n + 1, std::vector<int>(m + 1));
        for (size_t i = 0; i <= n; i++) d[i][0] = (int)i;
        for (size_t j = 0; j <= m; j++) d[0][j] = (int)j;
        for (size_t i = 1; i <= n; i++)
            for (size_t j = 1; j <= m; j++)
                d[i][j] = std::min({ d[i-1][j] + 1, d[i][j-1] + 1,
                                      d[i-1][j-1] + (a[i-1] == b[j-1] ? 0 : 1) });
        return (int)d[n][m];
    }

    // Candidato más parecido a `target`, o "" si ninguno está lo bastante
    // cerca como para ser un typo plausible. El umbral escala con el
    // tamaño de la palabra para que nombres cortos ("i", "x") no hagan
    // matchear cualquier cosa.
    inline std::string closest(const std::string& target, const std::vector<std::string>& candidates) {
        std::string best; int bestDist = -1;
        int threshold = std::max(2, (int)target.size() / 3);
        for (auto& c : candidates) {
            if (c == target) continue;
            int d = distance(target, c);
            if (d <= threshold && (bestDist == -1 || d < bestDist)) { bestDist = d; best = c; }
        }
        return best;
    }

    inline std::string suggest(const std::string& target, const std::vector<std::string>& candidates) {
        std::string c = closest(target, candidates);
        return c.empty() ? "" : (" -- did you mean '" + c + "'?");
    }
}

// ============================================================
// Tokens
// ============================================================
enum class Tok {
    END_OF_FILE, NEWLINE,
    NUMBER, STRING, RAW_STRING, NAME,
    KW_IF, KW_ELIF, KW_ELSE, KW_END, KW_FOR, KW_IN, KW_WHILE,
    KW_BREAK, KW_CONTINUE, KW_FUNC, KW_RETURN, KW_TRY, KW_CATCH, KW_FINALLY,
    KW_THROW, KW_USE, KW_AS, KW_CONST, KW_TRUE, KW_FALSE, KW_NULL,
    KW_AND, KW_OR, KW_NOT, KW_CLASS, KW_EXTENDS, KW_OBJECT, KW_MATCH, KW_ENUM,
    PLUS, MINUS, STAR, SLASH, PERCENT, POW,
    EQ, NEQ, LT, GT, LE, GE,
    ASSIGN, PLUS_EQ, MINUS_EQ, STAR_EQ, SLASH_EQ, PERCENT_EQ,
    LPAREN, RPAREN, LBRACKET, RBRACKET, LBRACE, RBRACE,
    COMMA, COLON, DOT, QQ, IDENTITY_EQ, IDENTITY_NEQ,
    ARROW,
    OPT_DOT, OPT_LBRACKET, SPREAD
};

struct Token {
    Tok type;
    std::string text;
    double num = 0;
    int line = 0;
};

static constexpr char SENT_LBRACE = '\x01';
static constexpr char SENT_RBRACE = '\x02';

static const std::unordered_map<std::string, Tok> KEYWORDS = {
    {"if", Tok::KW_IF}, {"elif", Tok::KW_ELIF}, {"else", Tok::KW_ELSE},
    {"end", Tok::KW_END}, {"for", Tok::KW_FOR}, {"in", Tok::KW_IN},
    {"while", Tok::KW_WHILE}, {"break", Tok::KW_BREAK}, {"continue", Tok::KW_CONTINUE},
    {"func", Tok::KW_FUNC}, {"return", Tok::KW_RETURN}, {"try", Tok::KW_TRY},
    {"catch", Tok::KW_CATCH}, {"finally", Tok::KW_FINALLY}, {"throw", Tok::KW_THROW}, {"use", Tok::KW_USE}, {"as", Tok::KW_AS},
    {"const", Tok::KW_CONST}, {"true", Tok::KW_TRUE}, {"false", Tok::KW_FALSE},
    {"null", Tok::KW_NULL}, {"and", Tok::KW_AND}, {"or", Tok::KW_OR}, {"not", Tok::KW_NOT},
    {"class", Tok::KW_CLASS}, {"extends", Tok::KW_EXTENDS}, {"object", Tok::KW_OBJECT},
    {"match", Tok::KW_MATCH}, {"enum", Tok::KW_ENUM},
};

struct NimbleError : std::runtime_error {
    std::string raw;
    int line = 0;
    std::vector<std::pair<std::string,int>> stack;
    NimbleError(const std::string& m, int l = 0)
        : std::runtime_error(l > 0 ? ("[line " + std::to_string(l) + "] " + m) : m),
          raw(m), line(l) {}
};

struct NeedMoreInput {};
struct AbortSignal { std::string msg; int line; };
// Thrown by the exit() native and caught only at the outermost level (main's
// entry point, the REPL loop, or the test runner) -- NOT via std::exit()
// called directly from inside a native function, which would skip the
// normal C++ stack unwind and could leave an open file unflushed or a curl
// handle uncleaned. Throwing lets every RAII destructor between the exit()
// call site and the catch run normally, exactly as if the script had
// returned all the way up on its own.
struct ExitSignal { int code; };

class Lexer {
public:
    Lexer(const std::string& src) : s(src) {
        // Un BOM UTF-8 (EF BB BF) al inicio del codigo fuente no es parte del
        // programa: lo agregan el Bloc de notas ("UTF-8 con BOM") y PowerShell 5
        // (-Encoding utf8). Solo se ignora aqui, en el CODIGO; read() de datos
        // del usuario no se toca.
        if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF)
            s.erase(0, 3);
    }

    std::vector<Token> tokenize() {
        std::vector<Token> out;
        bool lineHasTokens = false;
        // Mientras bracketDepth > 0 (dentro de un (), [] o {} sin cerrar) un
        // '\n' es puramente cosmético y se descarta en vez de emitirse como
        // Tok::NEWLINE. Esto es lo que permite llamadas, agrupaciones e
        // índices multilínea:
        //   ventana(
        //       "objeto",
        //       12
        //   )
        // Antes de esto sólo las listas [ ] y los mapas { } soportaban esto,
        // y de forma manual en el parser (ver skipNewlinesInsideBrackets);
        // ahora que el lexer nunca produce un NEWLINE dentro de un bracket,
        // esas llamadas quedan como no-ops inofensivos.
        int bracketDepth = 0;
        while (true) {
            skipSpacesAndComments();
            if (pos >= s.size()) break;
            char c = s[pos];
            if (c == '\n') {
                pos++; line++;
                if (lineHasTokens && bracketDepth == 0) { out.push_back({Tok::NEWLINE, "\\n", 0, line}); }
                lineHasTokens = false;
                continue;
            }
            Token t = nextToken();
            if (t.type == Tok::LPAREN || t.type == Tok::LBRACKET || t.type == Tok::LBRACE) bracketDepth++;
            else if ((t.type == Tok::RPAREN || t.type == Tok::RBRACKET || t.type == Tok::RBRACE) && bracketDepth > 0) bracketDepth--;
            lineHasTokens = true;
            out.push_back(t);
        }
        if (lineHasTokens && bracketDepth == 0) out.push_back({Tok::NEWLINE, "\\n", 0, line});
        out.push_back({Tok::END_OF_FILE, "", 0, line});
        return out;
    }

private:
    std::string s;
    size_t pos = 0;
    int line = 1;

    void skipSpacesAndComments() {
        while (pos < s.size()) {
            char c = s[pos];
            if (c == ' ' || c == '\t' || c == '\r') { pos++; continue; }
            if (c == '#') { while (pos < s.size() && s[pos] != '\n') pos++; continue; }
            break;
        }
    }

    char peek(int off = 0) { return (pos + off) < s.size() ? s[pos + off] : '\0'; }

    Token nextToken() {
        char c = s[pos];
        int startLine = line;
        if (c == '"' && peek(1) == '"' && peek(2) == '"') {
            pos += 3;
            std::string val;
            while (pos < s.size() && !(peek() == '"' && peek(1) == '"' && peek(2) == '"')) {
                if (s[pos] == '\n') line++;
                val += s[pos++];
            }
            if (pos >= s.size()) {
                // EOF sin encontrar el cierre `"""`: antes se devolvia el resto
                // del archivo como STRING sin ningun error.
                throw NimbleError("Unterminated triple-quoted string (missing closing '\"\"\"')", startLine);
            }
            pos += 3;
            if (!val.empty() && val.front() == '\n') val.erase(val.begin());
            return {Tok::STRING, val, 0, startLine};
        }
        if (c == '"') {
            pos++;
            std::string val;
            while (pos < s.size() && s[pos] != '"') {
                char ch = s[pos];
                if (ch == '\n') {
                    // Un string de comilla simple no puede cruzar líneas: sin
                    // este corte, el loop seguía comiendo el resto del
                    // archivo (código incluido) hasta tropezar con la
                    // próxima '"' que apareciera, generando errores
                    // confusos y lejos de la causa real. Para strings
                    // multilinea a proposito, usar comillas triples """...""".
                    throw NimbleError("Unterminated string literal (missing closing '\"' before end of line)", startLine);
                }
                if (ch == '\\' && pos + 1 < s.size()) {
                    char nx = s[pos + 1];
                    switch (nx) {
                        case 'n': val += '\n'; break;
                        case 't': val += '\t'; break;
                        case 'r': val += '\r'; break;
                        case '"': val += '"'; break;
                        case '\\': val += '\\'; break;
                        case '{': val += SENT_LBRACE; break;
                        case '}': val += SENT_RBRACE; break;
                        default: val += '\\'; val += nx; break;
                    }
                    pos += 2;
                } else {
                    val += ch;
                    pos++;
                }
            }
            if (pos >= s.size()) {
                // Llegamos al final del archivo sin encontrar la comilla de
                // cierre (ej. la ultima linea del archivo es `texto = "algo`).
                throw NimbleError("Unterminated string literal (missing closing '\"')", startLine);
            }
            pos++; // consumir la comilla de cierre
            return {Tok::STRING, val, 0, startLine};
        }
        if (isdigit((unsigned char)c)) {
            if (c == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
                size_t start = pos;
                pos += 2;
                while (pos < s.size() && isxdigit((unsigned char)s[pos])) pos++;
                std::string numText = s.substr(start, pos - start);
                double val = (double)std::stoll(numText.substr(2), nullptr, 16);
                return {Tok::NUMBER, numText, val, startLine};
            }
            if (c == '0' && (peek(1) == 'b' || peek(1) == 'B') && (peek(2) == '0' || peek(2) == '1')) {
                size_t start = pos;
                pos += 2;
                while (pos < s.size() && (s[pos] == '0' || s[pos] == '1')) pos++;
                std::string numText = s.substr(start, pos - start);
                double val = (double)std::stoll(numText.substr(2), nullptr, 2);
                return {Tok::NUMBER, numText, val, startLine};
            }
            size_t start = pos;
            while (pos < s.size() && isdigit((unsigned char)s[pos])) pos++;
            if (pos < s.size() && s[pos] == '.' && isdigit((unsigned char)peek(1))) {
                pos++;
                while (pos < s.size() && isdigit((unsigned char)s[pos])) pos++;
            }
            // Exponent suffix (1e10, 1.5e3, 2.5E-3, ...). Only consumed when
            // it's actually well-formed (an optional sign followed by at
            // least one digit) -- otherwise a bare trailing 'e' is left
            // alone rather than swallowed into a malformed number token.
            if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
                size_t expPos = pos + 1;
                if (expPos < s.size() && (s[expPos] == '+' || s[expPos] == '-')) expPos++;
                if (expPos < s.size() && isdigit((unsigned char)s[expPos])) {
                    pos = expPos;
                    while (pos < s.size() && isdigit((unsigned char)s[pos])) pos++;
                }
            }
            std::string numText = s.substr(start, pos - start);
            Token t{Tok::NUMBER, numText, std::stod(numText), startLine};
            return t;
        }
        if (c == 'r' && peek(1) == '"') {
            pos += 2;
            std::string val;
            while (pos < s.size() && s[pos] != '"') {
                if (s[pos] == '\n') {
                    // Igual que las strings normales: un r"..." no cruza lineas.
                    // Sin este corte se tragaba codigo hasta la proxima '"' o
                    // hasta EOF sin ningun error. Para multilinea usar """...""".
                    throw NimbleError("Unterminated raw string literal (missing closing '\"' before end of line)", startLine);
                }
                val += s[pos++];
            }
            if (pos >= s.size()) {
                throw NimbleError("Unterminated raw string literal (missing closing '\"')", startLine);
            }
            pos++; // consumir la comilla de cierre
            return {Tok::RAW_STRING, val, 0, startLine};
        }
        if (isalpha((unsigned char)c) || c == '_') {
            size_t start = pos;
            while (pos < s.size() && (isalnum((unsigned char)s[pos]) || s[pos] == '_')) pos++;
            std::string name = s.substr(start, pos - start);
            auto it = KEYWORDS.find(name);
            if (it != KEYWORDS.end()) return {it->second, name, 0, startLine};
            return {Tok::NAME, name, 0, startLine};
        }
        if (c == '.' && peek(1) == '.' && peek(2) == '.') {
            pos += 3;
            return {Tok::SPREAD, "...", 0, startLine};
        }
        auto two = [&](char a, char b) { return c == a && peek(1) == b; };
        if (two('*', '*')) { pos += 2; return {Tok::POW, "**", 0, startLine}; }
        if (two('=', '=') && peek(2) == '=') { pos += 3; return {Tok::IDENTITY_EQ, "===", 0, startLine}; }
        if (two('!', '=') && peek(2) == '=') { pos += 3; return {Tok::IDENTITY_NEQ, "!==", 0, startLine}; }
        if (two('=', '=')) { pos += 2; return {Tok::EQ, "==", 0, startLine}; }
        if (two('!', '=')) { pos += 2; return {Tok::NEQ, "!=", 0, startLine}; }
        if (two('<', '=')) { pos += 2; return {Tok::LE, "<=", 0, startLine}; }
        if (two('>', '=')) { pos += 2; return {Tok::GE, ">=", 0, startLine}; }
        if (two('+', '=')) { pos += 2; return {Tok::PLUS_EQ, "+=", 0, startLine}; }
        if (two('-', '=')) { pos += 2; return {Tok::MINUS_EQ, "-=", 0, startLine}; }
        if (two('*', '=')) { pos += 2; return {Tok::STAR_EQ, "*=", 0, startLine}; }
        if (two('/', '=')) { pos += 2; return {Tok::SLASH_EQ, "/=", 0, startLine}; }
        if (two('%', '=')) { pos += 2; return {Tok::PERCENT_EQ, "%=", 0, startLine}; }
        if (two('?', '?')) { pos += 2; return {Tok::QQ, "??", 0, startLine}; }
        if (two('?', '.')) { pos += 2; return {Tok::OPT_DOT, "?.", 0, startLine}; }
        if (two('?', '[')) { pos += 2; return {Tok::OPT_LBRACKET, "?[", 0, startLine}; }
        if (two('-', '>')) { pos += 2; return {Tok::ARROW, "->", 0, startLine}; }
        pos++;
        switch (c) {
            case '+': return {Tok::PLUS, "+", 0, startLine};
            case '-': return {Tok::MINUS, "-", 0, startLine};
            case '*': return {Tok::STAR, "*", 0, startLine};
            case '/': return {Tok::SLASH, "/", 0, startLine};
            case '%': return {Tok::PERCENT, "%", 0, startLine};
            case '=': return {Tok::ASSIGN, "=", 0, startLine};
            case '<': return {Tok::LT, "<", 0, startLine};
            case '>': return {Tok::GT, ">", 0, startLine};
            case '(': return {Tok::LPAREN, "(", 0, startLine};
            case ')': return {Tok::RPAREN, ")", 0, startLine};
            case '[': return {Tok::LBRACKET, "[", 0, startLine};
            case ']': return {Tok::RBRACKET, "]", 0, startLine};
            case '{': return {Tok::LBRACE, "{", 0, startLine};
            case '}': return {Tok::RBRACE, "}", 0, startLine};
            case ',': return {Tok::COMMA, ",", 0, startLine};
            case ':': return {Tok::COLON, ":", 0, startLine};
            case '.': return {Tok::DOT, ".", 0, startLine};
        }
        throw NimbleError("Unexpected character '" + std::string(1, c) + "'", startLine);
    }
};

// ============================================================
// AST
// ============================================================
struct Expr; struct Stmt;
using ExprPtr = std::shared_ptr<Expr>;
using StmtPtr = std::shared_ptr<Stmt>;

struct Param { std::string name; ExprPtr defaultVal; };

enum class EK {
    Number, Str, Bool, Null, Var, Unary, Binary, Logical, Assign,
    Call, Index, Member, ListLit, MapLit, FuncLit, Ternary, ListComp,
    NamedArg, InOp, ObjectLit, Slice,
    OptionalMember, OptionalIndex, Spread
};

struct Expr {
    EK kind;
    int line = 0;
    double num = 0;
    std::string str;
    bool boolean = false;
    std::string name;
    std::string op;
    ExprPtr a, b, c;
    std::vector<ExprPtr> list;
    std::vector<std::pair<std::string, ExprPtr>> namedArgs;
    std::vector<std::pair<ExprPtr, ExprPtr>> mapEntries;
    std::vector<Param> params;
    std::vector<StmtPtr> body;
    std::string iterVar;
    bool isRaw = false;
    bool isOptionalCall = false; // true si esta llamada viene después de un '?.'/'?[' en la misma cadena
};

enum class SK {
    ExprStmt, If, While, For, FuncDecl, Return, Break, Continue,
    Try, Throw, ConstDecl, ClassDecl, Match, Use, EnumDecl, TypeDecl,
    Test
};

struct IfBranch { ExprPtr cond; std::vector<StmtPtr> body; std::vector<ExprPtr> matchLabels; };

struct Stmt {
    SK kind;
    int line = 0;
    ExprPtr expr;
    std::string name;
    std::vector<Param> params;
    std::vector<StmtPtr> body;
    std::vector<IfBranch> branches;
    std::vector<StmtPtr> elseBody;
    bool hasElse = false;
    std::string iterVar;
    std::string iterVar2;
    ExprPtr iterable;
    std::string catchVar;
    std::vector<StmtPtr> catchBody;
    bool hasCatch = false;
    std::vector<StmtPtr> finallyBody;
    bool hasFinally = false;
    std::string extendsName;
    std::vector<std::string> enumMembers;
    std::string modulePath;
    std::vector<std::string> importNames;
    std::string moduleAlias;
    bool moduleIsLiteral = false;
};

static ExprPtr mkExpr(EK k) { auto e = std::make_shared<Expr>(); e->kind = k; return e; }
static StmtPtr mkStmt(SK k) { auto st = std::make_shared<Stmt>(); st->kind = k; return st; }

// ============================================================
// Parser
// ============================================================
class Parser {
public:
    Parser(std::vector<Token> toks) : t(std::move(toks)) {}

    std::vector<StmtPtr> parseProgram() {
        std::vector<StmtPtr> stmts;
        skipNewlines();
        while (!check(Tok::END_OF_FILE)) {
            stmts.push_back(parseStmt());
            skipNewlines();
        }
        return stmts;
    }

private:
    std::vector<Token> t;
    size_t p = 0;

    Token& cur() { return t[p]; }
    bool check(Tok k) { return cur().type == k; }
    bool match(Tok k) { if (check(k)) { p++; return true; } return false; }
    Token advance() { return t[p++]; }

    Token expect(Tok k, const std::string& what) {
        if (!check(k)) {
            if (check(Tok::END_OF_FILE)) throw NeedMoreInput{};
            err("expected " + what + " but found '" + cur().text + "'");
        }
        return advance();
    }
    void err(const std::string& msg) {
        throw NimbleError("Syntax error: " + msg, cur().line);
    }
    void skipNewlines() { while (check(Tok::NEWLINE)) advance(); }

    // Salto de línea opcional: permite escribir un bloque en una sola línea
    // (func f() return 1 end) o en varias, sin que el parser se confunda.
    void optionalNewline() { if (match(Tok::NEWLINE)) skipNewlines(); }

    void endOfStmt() {
        if (check(Tok::END_OF_FILE) || check(Tok::KW_END) || check(Tok::KW_ELSE) ||
            check(Tok::KW_ELIF) || check(Tok::KW_CATCH) || check(Tok::KW_FINALLY)) return;
        expect(Tok::NEWLINE, "end of line");
        skipNewlines();
    }

    std::vector<StmtPtr> parseBlockUntil(std::initializer_list<Tok> enders) {
        std::vector<StmtPtr> body;
        skipNewlines();
        auto isEnder = [&]() {
            for (auto e : enders) if (check(e)) return true;
            return false;
        };
        while (!isEnder() && !check(Tok::END_OF_FILE)) {
            body.push_back(parseStmt());
            skipNewlines();
        }
        if (check(Tok::END_OF_FILE) && !isEnder()) throw NeedMoreInput{};
        return body;
    }

    void warnIfAssign(ExprPtr& cond, const char* kw) {
        if (cond && cond->kind == EK::Assign) {
            Warnings::emit(std::string("assignment in the condition of '") + kw +
                           "' -- did you mean '=='?", cond->line);
        }
    }

    std::vector<Param> parseParams() {
        std::vector<Param> params;
        std::set<std::string> seen;
        expect(Tok::LPAREN, "'('");
        while (!check(Tok::RPAREN)) {
            Param pr;
            int paramLine = cur().line;
            pr.name = expect(Tok::NAME, "parameter name").text;
            if (seen.count(pr.name)) {
                Warnings::emit("duplicate parameter: '" + pr.name + "'", paramLine);
            }
            seen.insert(pr.name);
            if (match(Tok::COLON)) { expect(Tok::NAME, "parameter type"); }
            if (match(Tok::ASSIGN)) pr.defaultVal = parseTernary(); // no parseExpr(): la coma separa parametros
            params.push_back(pr);
            if (!match(Tok::COMMA)) break;
        }
        expect(Tok::RPAREN, "')'");
        return params;
    }

    StmtPtr parseStmt() {
        if (check(Tok::NAME) && cur().text == "test" && t[p+1].type == Tok::STRING)
            return parseTest();
        if (check(Tok::KW_IF)) return parseIf();
        if (check(Tok::KW_WHILE)) return parseWhile();
        if (check(Tok::KW_FOR)) return parseFor();
        if (check(Tok::KW_FUNC)) return parseFuncDecl();
        if (check(Tok::KW_TRY)) return parseTry();
        if (check(Tok::KW_CLASS)) return parseClassDecl();
        if (check(Tok::KW_MATCH)) return parseMatch();
        if (check(Tok::KW_ENUM)) return parseEnumDecl();
        if (check(Tok::NAME) && cur().text == "type" && t[p + 1].type == Tok::NAME && t[p + 2].type == Tok::LPAREN)
            return parseTypeDecl();
        if (check(Tok::KW_THROW)) {
            int ln = cur().line;
            advance();
            auto st = mkStmt(SK::Throw); st->line = ln; st->expr = parseExpr(); endOfStmt(); return st;
        }
        if (check(Tok::KW_RETURN)) {
            int ln = cur().line;
            advance();
            auto st = mkStmt(SK::Return); st->line = ln;
            if (!check(Tok::NEWLINE) && !check(Tok::KW_END) && !check(Tok::END_OF_FILE)) {
                ExprPtr first = parseExpr();
                if (check(Tok::COMMA)) {
                    auto lst = mkExpr(EK::ListLit);
                    lst->line = ln;
                    lst->list.push_back(first);
                    while (match(Tok::COMMA)) lst->list.push_back(parseExpr());
                    st->expr = lst;
                } else {
                    st->expr = first;
                }
            }
            endOfStmt();
            return st;
        }
        if (check(Tok::KW_BREAK)) { auto st = mkStmt(SK::Break); st->line = cur().line; advance(); endOfStmt(); return st; }
        if (check(Tok::KW_CONTINUE)) { auto st = mkStmt(SK::Continue); st->line = cur().line; advance(); endOfStmt(); return st; }
        if (check(Tok::KW_CONST)) {
            int ln = cur().line;
            advance();
            auto st = mkStmt(SK::ConstDecl); st->line = ln;
            st->name = expect(Tok::NAME, "constant name").text;
            expect(Tok::ASSIGN, "'='");
            st->expr = parseExpr();
            endOfStmt();
            return st;
        }
        if (check(Tok::KW_USE)) return parseUse();
        if (check(Tok::NAME) && t[p + 1].type == Tok::COLON && t[p + 2].type == Tok::NAME
            && (t[p + 3].type == Tok::ASSIGN || t[p + 3].type == Tok::NEWLINE || t[p + 3].type == Tok::END_OF_FILE)) {
            int ln = cur().line;
            std::string varName = advance().text;
            advance();
            advance();
            auto st = mkStmt(SK::ExprStmt); st->line = ln;
            auto assignExpr = mkExpr(EK::Assign);
            assignExpr->op = "=";
            assignExpr->line = ln;
            auto target = mkExpr(EK::Var); target->name = varName; target->line = ln;
            assignExpr->a = target;
            if (match(Tok::ASSIGN)) assignExpr->b = parseExpr();
            else assignExpr->b = mkExpr(EK::Null);
            st->expr = assignExpr;
            endOfStmt();
            return st;
        }
        int ln = cur().line;
        auto st = mkStmt(SK::ExprStmt); st->line = ln;
        st->expr = parseExpr();
        endOfStmt();
        return st;
    }

    StmtPtr parseTest() {
        int ln = cur().line;
        advance();
        auto st = mkStmt(SK::Test); st->line = ln;
        st->name = expect(Tok::STRING, "test name").text;
        std::string clean;
        for (char c : st->name) {
            if (c == SENT_LBRACE) clean += '{';
            else if (c == SENT_RBRACE) clean += '}';
            else clean += c;
        }
        st->name = clean;
        optionalNewline();
        st->body = parseBlockUntil({Tok::KW_END});
        expect(Tok::KW_END, "'end'");
        endOfStmt();
        return st;
    }

    StmtPtr parseIf() {
        int ln = cur().line;
        advance();
        auto st = mkStmt(SK::If); st->line = ln;
        IfBranch b0; b0.cond = parseExpr();
        warnIfAssign(b0.cond, "if");
        optionalNewline();
        b0.body = parseBlockUntil({Tok::KW_ELIF, Tok::KW_ELSE, Tok::KW_END});
        st->branches.push_back(b0);
        while (check(Tok::KW_ELIF)) {
            advance();
            IfBranch b; b.cond = parseExpr();
            warnIfAssign(b.cond, "elif");
            optionalNewline();
            b.body = parseBlockUntil({Tok::KW_ELIF, Tok::KW_ELSE, Tok::KW_END});
            st->branches.push_back(b);
        }
        if (check(Tok::KW_ELSE)) {
            advance();
            optionalNewline();
            st->hasElse = true;
            st->elseBody = parseBlockUntil({Tok::KW_END});
        }
        expect(Tok::KW_END, "'end'");
        endOfStmt();
        return st;
    }

    StmtPtr parseWhile() {
        int ln = cur().line;
        advance();
        auto st = mkStmt(SK::While); st->line = ln;
        st->expr = parseExpr();
        warnIfAssign(st->expr, "while");
        optionalNewline();
        st->body = parseBlockUntil({Tok::KW_END});
        expect(Tok::KW_END, "'end'");
        endOfStmt();
        return st;
    }

    StmtPtr parseFor() {
        int ln = cur().line;
        advance();
        auto st = mkStmt(SK::For); st->line = ln;
        st->iterVar = expect(Tok::NAME, "variable name").text;
        if (match(Tok::COMMA)) st->iterVar2 = expect(Tok::NAME, "second variable").text;
        expect(Tok::KW_IN, "'in'");
        st->iterable = parseExpr();
        optionalNewline();
        st->body = parseBlockUntil({Tok::KW_END});
        expect(Tok::KW_END, "'end'");
        endOfStmt();
        return st;
    }

    StmtPtr parseFuncDecl() {
        int ln = cur().line;
        advance();
        auto st = mkStmt(SK::FuncDecl); st->line = ln;
        st->name = expect(Tok::NAME, "function name").text;
        st->params = parseParams();
        if (match(Tok::ARROW)) parsePostfixTypeIgnore();
        optionalNewline();
        st->body = parseBlockUntil({Tok::KW_END});
        expect(Tok::KW_END, "'end'");
        endOfStmt();
        return st;
    }

    void parsePostfixTypeIgnore() { if (check(Tok::NAME)) advance(); }

    StmtPtr parseTry() {
        int ln = cur().line;
        advance();
        auto st = mkStmt(SK::Try); st->line = ln;
        optionalNewline();
        st->body = parseBlockUntil({Tok::KW_CATCH, Tok::KW_FINALLY});
        if (check(Tok::KW_CATCH)) {
            advance();
            st->hasCatch = true;
            if (check(Tok::NAME)) st->catchVar = advance().text;
            optionalNewline();
            st->catchBody = parseBlockUntil({Tok::KW_FINALLY, Tok::KW_END});
        }
        if (check(Tok::KW_FINALLY)) {
            advance();
            st->hasFinally = true;
            optionalNewline();
            st->finallyBody = parseBlockUntil({Tok::KW_END});
        }
        if (!st->hasCatch && !st->hasFinally) err("expected 'catch' or 'finally'");
        expect(Tok::KW_END, "'end'");
        endOfStmt();
        return st;
    }

    StmtPtr parseUse() {
        int ln = cur().line;
        advance();
        auto st = mkStmt(SK::Use); st->line = ln;
        if (check(Tok::STRING) || check(Tok::RAW_STRING)) {
            st->modulePath = advance().text;
            st->moduleIsLiteral = true;
        } else {
            std::string mod = expect(Tok::NAME, "module name").text;
            while (match(Tok::DOT)) mod += "." + expect(Tok::NAME, "module name").text;
            st->modulePath = mod;
            st->moduleIsLiteral = false;
        }
        if (check(Tok::LPAREN)) {
            advance();
            while (!check(Tok::RPAREN)) {
                st->importNames.push_back(expect(Tok::NAME, "name to import").text);
                if (!match(Tok::COMMA)) break;
            }
            expect(Tok::RPAREN, "')'");
        } else if (match(Tok::KW_AS)) {
            st->moduleAlias = expect(Tok::NAME, "alias name").text;
        }
        endOfStmt();
        return st;
    }

    StmtPtr parseEnumDecl() {
        int ln = cur().line;
        advance();
        auto st = mkStmt(SK::EnumDecl); st->line = ln;
        st->name = expect(Tok::NAME, "enum name").text;
        expect(Tok::NEWLINE, "end of line"); skipNewlines();
        while (!check(Tok::KW_END) && !check(Tok::END_OF_FILE)) {
            st->enumMembers.push_back(expect(Tok::NAME, "enum value name").text);
            if (match(Tok::COMMA)) skipNewlines();
            else { expect(Tok::NEWLINE, "end of line"); skipNewlines(); }
        }
        expect(Tok::KW_END, "'end'");
        endOfStmt();
        return st;
    }

    StmtPtr parseTypeDecl() {
        int ln = cur().line;
        advance();
        auto st = mkStmt(SK::TypeDecl); st->line = ln;
        st->name = expect(Tok::NAME, "type name").text;
        st->params = parseParams();
        endOfStmt();
        return st;
    }

    StmtPtr parseClassDecl() {
        int ln = cur().line;
        advance();
        auto st = mkStmt(SK::ClassDecl); st->line = ln;
        st->name = expect(Tok::NAME, "class name").text;
        if (match(Tok::KW_EXTENDS)) st->extendsName = expect(Tok::NAME, "base class name").text;
        optionalNewline();
        st->body = parseBlockUntil({Tok::KW_END});
        for (auto& m : st->body) {
            if (m->kind != SK::FuncDecl)
                err("a class body may only contain functions ('func ... end')");
        }
        expect(Tok::KW_END, "'end'");
        endOfStmt();
        return st;
    }

    bool looksLikeCaseLabelStart() {
        size_t saved = p;
        bool result = false;
        try {
            parseTernary();
            while (check(Tok::COMMA)) { advance(); parseTernary(); }
            result = check(Tok::COLON);
        } catch (...) {
            result = false;
        }
        p = saved;
        return result;
    }

    std::vector<ExprPtr> parseCaseLabelList() {
        std::vector<ExprPtr> labels;
        labels.push_back(parseTernary());
        while (match(Tok::COMMA)) labels.push_back(parseTernary());
        return labels;
    }

    std::vector<StmtPtr> parseMatchBody() {
        std::vector<StmtPtr> body;
        skipNewlines();
        while (!check(Tok::KW_END) && !check(Tok::KW_ELSE) && !check(Tok::END_OF_FILE) && !looksLikeCaseLabelStart()) {
            body.push_back(parseStmt());
            skipNewlines();
        }
        if (check(Tok::END_OF_FILE)) throw NeedMoreInput{};
        return body;
    }

    StmtPtr parseMatch() {
        int ln = cur().line;
        advance();
        auto st = mkStmt(SK::Match); st->line = ln;
        st->expr = parseExpr();
        expect(Tok::NEWLINE, "end of line"); skipNewlines();
        while (!check(Tok::KW_END) && !check(Tok::KW_ELSE) && !check(Tok::END_OF_FILE)) {
            IfBranch c;
            c.matchLabels = parseCaseLabelList();
            expect(Tok::COLON, "':'");
            expect(Tok::NEWLINE, "end of line");
            c.body = parseMatchBody();
            st->branches.push_back(c);
        }
        if (check(Tok::KW_ELSE)) {
            advance();
            expect(Tok::COLON, "':'");
            expect(Tok::NEWLINE, "end of line");
            st->hasElse = true;
            st->elseBody = parseMatchBody();
        }
        expect(Tok::KW_END, "'end'");
        endOfStmt();
        return st;
    }

    // ---- expresiones ----
    ExprPtr parseExpr() { return parseAssign(); }

    // Igual que parseExpr(), pero SIN la asignacion multiple sin corchetes
    // (a, b = 1, 2). Para lugares donde la coma es un SEPARADOR (argumentos,
    // elementos de lista, valores de mapa): ahi "1, b = 2" no es una
    // desestructuracion, es "1" y despues "b = 2".
    ExprPtr parseElemExpr() { return parseAssign(false); }

    ExprPtr parseAssign(bool allowTuple = true) {
        ExprPtr first = parseTernary();
        if (!allowTuple || !check(Tok::COMMA)) {
            static const std::vector<std::pair<Tok,std::string>> ops = {
                {Tok::ASSIGN, "="}, {Tok::PLUS_EQ, "+="}, {Tok::MINUS_EQ, "-="},
                {Tok::STAR_EQ, "*="}, {Tok::SLASH_EQ, "/="}, {Tok::PERCENT_EQ, "%="}
            };
            for (auto& [tk, sym] : ops) {
                if (check(tk)) {
                    int ln = cur().line;
                    advance();
                    ExprPtr value = parseAssign(allowTuple);
                    auto e = mkExpr(EK::Assign); e->line = ln;
                    e->op = sym; e->a = first; e->b = value;
                    return e;
                }
            }
            return first;
        }
        size_t savedP = p;
        std::vector<ExprPtr> exprs = {first};
        try {
            while (match(Tok::COMMA)) {
                exprs.push_back(parseTernary());
            }
        } catch (...) {
            p = savedP;
            return first;
        }
        if (!check(Tok::ASSIGN)) {
            p = savedP;
            return first;
        }
        int ln = cur().line;
        advance();
        ExprPtr rhs = parseTernary();
        if (check(Tok::COMMA)) {
            auto lst = mkExpr(EK::ListLit);
            lst->line = ln;
            lst->list.push_back(rhs);
            while (match(Tok::COMMA)) lst->list.push_back(parseTernary());
            rhs = lst;
        }
        auto lhs = mkExpr(EK::ListLit);
        lhs->line = ln;
        lhs->list = exprs;
        auto assign = mkExpr(EK::Assign);
        assign->line = ln;
        assign->op = "=";
        assign->a = lhs;
        assign->b = rhs;
        return assign;
    }

    ExprPtr parseTernary() {
        ExprPtr thenExpr = parseOr();
        if (check(Tok::KW_IF)) {
            int ln = cur().line;
            advance();
            ExprPtr cond = parseOr();
            expect(Tok::KW_ELSE, "'else'");
            ExprPtr elseExpr = parseTernary();
            auto e = mkExpr(EK::Ternary); e->line = ln;
            e->a = thenExpr; e->b = elseExpr; e->c = cond;
            return e;
        }
        return thenExpr;
    }

    ExprPtr parseOr() {
        ExprPtr left = parseAnd();
        while (check(Tok::KW_OR)) {
            int ln = cur().line; advance();
            auto e = mkExpr(EK::Logical); e->line = ln;
            e->op = "or"; e->a = left; e->b = parseAnd();
            left = e;
        }
        return left;
    }
    ExprPtr parseAnd() {
        ExprPtr left = parseNot();
        while (check(Tok::KW_AND)) {
            int ln = cur().line; advance();
            auto e = mkExpr(EK::Logical); e->line = ln;
            e->op = "and"; e->a = left; e->b = parseNot();
            left = e;
        }
        return left;
    }
    ExprPtr parseNot() {
        if (check(Tok::KW_NOT)) {
            int ln = cur().line; advance();
            auto e = mkExpr(EK::Unary); e->line = ln; e->op = "not"; e->a = parseNot();
            return e;
        }
        return parseEquality();
    }
    ExprPtr parseEquality() {
        ExprPtr left = parseComparison();
        while (check(Tok::EQ) || check(Tok::NEQ) || check(Tok::IDENTITY_EQ) || check(Tok::IDENTITY_NEQ)) {
            int ln = cur().line;
            std::string op = advance().text;
            auto e = mkExpr(EK::Binary); e->line = ln;
            e->op = op; e->a = left; e->b = parseComparison();
            left = e;
        }
        return left;
    }
    ExprPtr parseComparison() {
        ExprPtr left = parseInExpr();
        while (check(Tok::LT) || check(Tok::GT) || check(Tok::LE) || check(Tok::GE)) {
            int ln = cur().line;
            std::string op = advance().text;
            auto e = mkExpr(EK::Binary); e->line = ln;
            e->op = op; e->a = left; e->b = parseInExpr();
            left = e;
        }
        return left;
    }
    ExprPtr parseInExpr() {
        ExprPtr left = parseNullCoalesce();
        if (check(Tok::KW_IN)) {
            int ln = cur().line; advance();
            auto e = mkExpr(EK::InOp); e->line = ln; e->a = left; e->b = parseNullCoalesce();
            return e;
        }
        return left;
    }
    ExprPtr parseNullCoalesce() {
        ExprPtr left = parseAdditive();
        while (check(Tok::QQ)) {
            int ln = cur().line; advance();
            auto e = mkExpr(EK::Binary); e->line = ln; e->op = "??"; e->a = left; e->b = parseAdditive();
            left = e;
        }
        return left;
    }
    ExprPtr parseAdditive() {
        ExprPtr left = parseMultiplicative();
        while (check(Tok::PLUS) || check(Tok::MINUS)) {
            int ln = cur().line;
            std::string op = advance().text;
            auto e = mkExpr(EK::Binary); e->line = ln;
            e->op = op; e->a = left; e->b = parseMultiplicative();
            left = e;
        }
        return left;
    }
    ExprPtr parseMultiplicative() {
        ExprPtr left = parseUnary();
        while (check(Tok::STAR) || check(Tok::SLASH) || check(Tok::PERCENT)) {
            int ln = cur().line;
            std::string op = advance().text;
            auto e = mkExpr(EK::Binary); e->line = ln;
            e->op = op; e->a = left; e->b = parseUnary();
            left = e;
        }
        return left;
    }
    ExprPtr parseUnary() {
        if (check(Tok::MINUS) || check(Tok::PLUS)) {
            int ln = cur().line;
            std::string op = advance().text;
            auto e = mkExpr(EK::Unary); e->line = ln; e->op = op; e->a = parseUnary();
            return e;
        }
        return parsePower();
    }
    ExprPtr parsePower() {
        ExprPtr left = parsePostfix();
        if (check(Tok::POW)) {
            int ln = cur().line; advance();
            auto e = mkExpr(EK::Binary); e->line = ln;
            e->op = "**"; e->a = left; e->b = parseUnary();
            return e;
        }
        return left;
    }

    std::string expectMemberName() {
        if (check(Tok::NAME)) return advance().text;
        static const std::set<Tok> allowedAsMember = {
            Tok::KW_MATCH, Tok::KW_IF, Tok::KW_ELIF, Tok::KW_ELSE, Tok::KW_FOR, Tok::KW_WHILE,
            Tok::KW_CLASS, Tok::KW_OBJECT, Tok::KW_ENUM, Tok::KW_USE, Tok::KW_TRY, Tok::KW_CATCH, Tok::KW_FINALLY,
            Tok::KW_THROW, Tok::KW_RETURN, Tok::KW_BREAK, Tok::KW_CONTINUE, Tok::KW_FUNC,
            Tok::KW_CONST, Tok::KW_TRUE, Tok::KW_FALSE, Tok::KW_NULL, Tok::KW_AND, Tok::KW_OR,
            Tok::KW_NOT, Tok::KW_EXTENDS, Tok::KW_IN, Tok::KW_AS, Tok::KW_END
        };
        if (allowedAsMember.count(cur().type)) return advance().text;
        if (check(Tok::END_OF_FILE)) throw NeedMoreInput{};
        err("expected a member name");
        return "";
    }

    ExprPtr parsePostfix() {
        ExprPtr e = parsePrimary();
        bool optionalChain = false;
        while (true) {
            if (check(Tok::DOT) || check(Tok::OPT_DOT)) {
                bool isOpt = check(Tok::OPT_DOT);
                int ln = cur().line;
                advance();
                std::string name = expectMemberName();
                if (isOpt) optionalChain = true;
                if (optionalChain) {
                    auto m = mkExpr(EK::OptionalMember);
                    m->line = ln; m->a = e; m->name = name;
                    e = m;
                } else {
                    auto m = mkExpr(EK::Member);
                    m->line = ln; m->a = e; m->name = name;
                    e = m;
                }
            } else if (check(Tok::LBRACKET) || check(Tok::OPT_LBRACKET)) {
                bool isOpt = check(Tok::OPT_LBRACKET);
                int ln = cur().line;
                advance();
                if (isOpt) optionalChain = true;
                ExprPtr startExpr, endExpr;
                bool isSlice = false;
                if (!check(Tok::COLON)) startExpr = parseExpr();
                if (check(Tok::COLON)) {
                    isSlice = true;
                    advance();
                    if (!check(Tok::RBRACKET)) endExpr = parseExpr();
                }
                expect(Tok::RBRACKET, "']'");
                if (isSlice) {
                    auto sl = mkExpr(EK::Slice);
                    sl->line = ln; sl->a = e; sl->b = startExpr; sl->c = endExpr;
                    e = sl;
                } else {
                    if (optionalChain) {
                        auto ix = mkExpr(EK::OptionalIndex);
                        ix->line = ln; ix->a = e; ix->b = startExpr;
                        e = ix;
                    } else {
                        auto ix = mkExpr(EK::Index);
                        ix->line = ln; ix->a = e; ix->b = startExpr;
                        e = ix;
                    }
                }
            } else if (check(Tok::LPAREN)) {
                int ln = cur().line;
                advance();
                auto call = mkExpr(EK::Call); call->line = ln;
                call->a = e;
                call->isOptionalCall = optionalChain; // ej. m?.metodo() -> no llamar si m era null
                while (!check(Tok::RPAREN)) {
                    if (check(Tok::SPREAD)) {
                        int spLn = cur().line;
                        advance();
                        auto sp = mkExpr(EK::Spread); sp->line = spLn;
                        sp->a = parseElemExpr();
                        call->list.push_back(sp);
                    } else if (check(Tok::NAME) && t[p+1].type == Tok::COLON) {
                        std::string nm = advance().text;
                        advance();
                        ExprPtr val = parseElemExpr();
                        call->namedArgs.push_back({nm, val});
                    } else {
                        call->list.push_back(parseElemExpr());
                    }
                    if (!match(Tok::COMMA)) break;
                }
                expect(Tok::RPAREN, "')'");
                e = call;
                optionalChain = false;
            } else break;
        }
        return e;
    }

    ExprPtr parsePrimary() {
        Token tok = cur();
        if (check(Tok::NUMBER)) { advance(); auto e = mkExpr(EK::Number); e->num = tok.num; e->line = tok.line; return e; }
        if (check(Tok::STRING)) { advance(); auto e = mkExpr(EK::Str); e->str = tok.text; e->line = tok.line; e->isRaw = false; return e; }
        if (check(Tok::RAW_STRING)) { advance(); auto e = mkExpr(EK::Str); e->str = tok.text; e->line = tok.line; e->isRaw = true; return e; }
        if (check(Tok::KW_TRUE)) { advance(); auto e = mkExpr(EK::Bool); e->boolean = true; e->line = tok.line; return e; }
        if (check(Tok::KW_FALSE)) { advance(); auto e = mkExpr(EK::Bool); e->boolean = false; e->line = tok.line; return e; }
        if (check(Tok::KW_NULL)) { advance(); auto e = mkExpr(EK::Null); e->line = tok.line; return e; }
        if (check(Tok::NAME)) { advance(); auto e = mkExpr(EK::Var); e->name = tok.text; e->line = tok.line; return e; }
        if (check(Tok::LPAREN)) {
            advance();
            ExprPtr e = parseExpr();
            expect(Tok::RPAREN, "')'");
            return e;
        }
        if (check(Tok::LBRACKET)) return parseListOrComp();
        if (check(Tok::LBRACE)) return parseMapLit();
        if (check(Tok::KW_FUNC)) return parseFuncLit();
        if (check(Tok::KW_OBJECT)) return parseObjectLit();
        if (check(Tok::END_OF_FILE)) throw NeedMoreInput{};
        err("unexpected expression ('" + tok.text + "')");
        return nullptr;
    }

    ExprPtr parseListElement() {
        if (check(Tok::SPREAD)) {
            int ln = cur().line;
            advance();
            auto sp = mkExpr(EK::Spread); sp->line = ln;
            sp->a = parseElemExpr();
            return sp;
        }
        return parseElemExpr();
    }

    // Listas multilínea: [ ... ] acepta saltos de línea internos, igual que
    // los mapas { ... }. Se llama a skipNewlinesInsideBrackets() en los puntos
    // donde un \n es puramente cosmético.
    ExprPtr parseListOrComp() {
        int ln = cur().line;
        advance();
        skipNewlinesInsideBrackets();
        if (check(Tok::RBRACKET)) { advance(); auto e = mkExpr(EK::ListLit); e->line = ln; return e; }
        ExprPtr first = parseListElement();
        if (check(Tok::KW_FOR) && first->kind != EK::Spread) {
            advance();
            std::string var = expect(Tok::NAME, "variable name").text;
            expect(Tok::KW_IN, "'in'");
            ExprPtr iterable = parseOr();
            ExprPtr filter;
            if (check(Tok::KW_IF)) { advance(); filter = parseExpr(); }
            skipNewlinesInsideBrackets();
            expect(Tok::RBRACKET, "']'");
            auto e = mkExpr(EK::ListComp); e->line = ln;
            e->a = first; e->iterVar = var; e->list.push_back(iterable); e->c = filter;
            return e;
        }
        auto e = mkExpr(EK::ListLit); e->line = ln;
        e->list.push_back(first);
        while (match(Tok::COMMA)) {
            skipNewlinesInsideBrackets();
            if (check(Tok::RBRACKET)) break;
            e->list.push_back(parseListElement());
        }
        skipNewlinesInsideBrackets();
        expect(Tok::RBRACKET, "']'");
        return e;
    }

    ExprPtr parseMapLit() {
        int ln = cur().line;
        advance();
        auto e = mkExpr(EK::MapLit); e->line = ln;
        skipNewlinesInsideBrackets();
        while (!check(Tok::RBRACE)) {
            if (check(Tok::SPREAD)) {
                advance();
                ExprPtr operand = parseElemExpr();
                e->mapEntries.push_back({nullptr, operand});
            } else {
                ExprPtr key;
                std::string keyName;
                if (check(Tok::STRING)) { keyName = advance().text; auto k = mkExpr(EK::Str); k->str = keyName; key = k; }
                else { keyName = expect(Tok::NAME, "key").text; auto k = mkExpr(EK::Str); k->str = keyName; key = k; }
                ExprPtr val;
                if (match(Tok::COLON)) {
                    skipNewlinesInsideBrackets();
                    val = parseElemExpr();
                } else {
                    auto v = mkExpr(EK::Var); v->name = keyName; val = v;
                }
                e->mapEntries.push_back({key, val});
            }
            skipNewlinesInsideBrackets();
            if (!match(Tok::COMMA)) break;
            skipNewlinesInsideBrackets();
        }
        skipNewlinesInsideBrackets();
        expect(Tok::RBRACE, "'}'");
        return e;
    }

    void skipNewlinesInsideBrackets() { while (check(Tok::NEWLINE)) advance(); }

    ExprPtr parseFuncLit() {
        int ln = cur().line;
        advance();
        auto e = mkExpr(EK::FuncLit); e->line = ln;
        e->params = parseParams();
        if (match(Tok::ARROW)) parsePostfixTypeIgnore();
        optionalNewline();
        e->body = parseBlockUntil({Tok::KW_END});
        expect(Tok::KW_END, "'end'");
        return e;
    }

    ExprPtr parseObjectLit() {
        int ln = cur().line;
        advance();
        auto e = mkExpr(EK::ObjectLit); e->line = ln;
        optionalNewline();
        e->body = parseBlockUntil({Tok::KW_END});
        expect(Tok::KW_END, "'end'");
        return e;
    }
};

// ============================================================
// Valores
// ============================================================
struct ListObj; struct MapObj; struct FunctionObj; struct ClassObj; class Interpreter; struct Env;

struct Value {
    std::variant<std::monostate, double, bool, std::string,
                 std::shared_ptr<ListObj>, std::shared_ptr<MapObj>,
                 std::shared_ptr<FunctionObj>, std::shared_ptr<Env>,
                 std::shared_ptr<ClassObj>> v;

    Value() : v(std::monostate{}) {}
    Value(std::nullptr_t) : v(std::monostate{}) {}
    Value(double d) : v(d) {}
    Value(int i) : v((double)i) {}
    // Without this, `Value("text")` compiles -- but silently picks
    // Value(bool) over Value(const std::string&) (pointer-to-bool is a
    // standard conversion, const char* -> std::string is a user-defined
    // one, and standard conversions always win overload resolution), so a
    // literal string argument silently became `true` instead of a string,
    // with no compiler warning. This exact-match overload removes the
    // ambiguity entirely instead of relying on every embedder remembering
    // to write Value(std::string("text")).
    Value(const char* s) : v(std::string(s)) {}
    Value(bool b) : v(b) {}
    Value(const std::string& s) : v(s) {}
    Value(std::shared_ptr<ListObj> l) : v(l) {}
    Value(std::shared_ptr<MapObj> m) : v(m) {}
    Value(std::shared_ptr<FunctionObj> f) : v(f) {}
    Value(std::shared_ptr<Env> o) : v(o) {}
    Value(std::shared_ptr<ClassObj> c) : v(c) {}

    bool isNull() const { return std::holds_alternative<std::monostate>(v); }
    bool isNum() const { return std::holds_alternative<double>(v); }
    bool isBool() const { return std::holds_alternative<bool>(v); }
    bool isStr() const { return std::holds_alternative<std::string>(v); }
    bool isList() const { return std::holds_alternative<std::shared_ptr<ListObj>>(v); }
    bool isMap() const { return std::holds_alternative<std::shared_ptr<MapObj>>(v); }
    bool isFunc() const { return std::holds_alternative<std::shared_ptr<FunctionObj>>(v); }
    bool isObj() const { return std::holds_alternative<std::shared_ptr<Env>>(v); }
    bool isClass() const { return std::holds_alternative<std::shared_ptr<ClassObj>>(v); }

    double asNum() const { return std::get<double>(v); }
    bool asBool() const { return std::get<bool>(v); }
    const std::string& asStr() const { return std::get<std::string>(v); }
    std::shared_ptr<ListObj> asList() const { return std::get<std::shared_ptr<ListObj>>(v); }
    std::shared_ptr<MapObj> asMap() const { return std::get<std::shared_ptr<MapObj>>(v); }
    std::shared_ptr<FunctionObj> asFunc() const { return std::get<std::shared_ptr<FunctionObj>>(v); }
    std::shared_ptr<Env> asObj() const { return std::get<std::shared_ptr<Env>>(v); }
    std::shared_ptr<ClassObj> asClass() const { return std::get<std::shared_ptr<ClassObj>>(v); }
};

// ---------------- Cycle collector infrastructure ----------------
// See docs/memory-model.md for the full design write-up. In short: shared_ptr
// reference counting already reclaims everything that ISN'T part of a
// reference cycle for free (the common case, left untouched). This adds a
// CPython-style trial-deletion cycle collector on top, scoped to the three
// object types that can form self-referential cycles through closures:
// Env, FunctionObj, ClassObj. ListObj/MapObj are NOT tracked as top-level
// nodes (they're allocated far more often than the above -- every list/map
// literal, every string.split(), etc. -- and don't form cycles on their
// own); gcCollectValueEdges still looks *inside* them so a "list of
// closures" cycle is still found and collected correctly.
//
// KNOWN LEAK (accepted trade-off, see docs/memory-model.md "Self-referential
// lists/maps"): because ListObj/MapObj are plain std::make_shared objects
// outside this system, a list or map that ends up containing itself --
// directly (`l.push(l)`) or through a cycle of containers/objects -- is an
// ordinary shared_ptr reference cycle that nothing here can ever break.
// Confirmed with AddressSanitizer/LeakSanitizer: `lst=[1]; lst.push(lst)`
// leaks permanently, even after gc.collect(). This is NOT the same bug as
// the Env/FunctionObj/ClassObj cycles above; it can't be fixed by running
// the collector more, because these two types are never registered with it
// in the first place. Fixing it for real means making ListObj/MapObj
// GCTracked too (gcMake<> instead of make_shared<>, +gcEdges/+gcClear),
// which adds every list/map -- not just the closure-heavy types above -- to
// the collector's bookkeeping, with the performance cost that was
// specifically the reason they were excluded. Left as-is until someone
// measures whether that cost is acceptable; see the doc for how to avoid
// triggering it in the meantime.
struct GCTracked {
    virtual ~GCTracked() = default;
    // Appends every outgoing edge to another tracked node reachable
    // directly (or through an untracked List/Map) from this node.
    virtual void gcEdges(std::vector<std::shared_ptr<GCTracked>>& out) const = 0;
    // Called only on nodes proven unreachable: drops this node's own
    // outgoing pointers so ordinary shared_ptr refcounting reclaims
    // whatever they pointed to (including cascading into other garbage).
    virtual void gcClear() = 0;
};

struct ListObj { std::vector<Value> items; };
struct MapObj {
    std::vector<std::pair<std::string, Value>> entries;
    // Keeps insertion order in `entries` (everything that iterates a map --
    // for..in, to-string, JSON encode, structural equality, CSV export --
    // depends on that order), while making has()/get()/set() O(1) average
    // instead of the O(n) linear scan this used to be. `entries` itself is
    // never mutated from outside this struct, so `index` can't go stale.
    std::unordered_map<std::string, size_t> index;

    bool has(const std::string& k) const {
        return index.find(k) != index.end();
    }
    Value get(const std::string& k) const {
        auto it = index.find(k);
        return it != index.end() ? entries[it->second].second : Value();
    }
    void set(const std::string& k, const Value& val) {
        auto it = index.find(k);
        if (it != index.end()) { entries[it->second].second = val; return; }
        index[k] = entries.size();
        entries.push_back({k, val});
    }
    // Removes a key, preserving insertion order of the remaining entries.
    // Was previously missing entirely -- has()/get()/set() existed but there
    // was no way at all to delete a key from a map once added. O(n) for the
    // erase + index shift, which is fine: has()/get()/set() stay O(1).
    bool remove(const std::string& k) {
        auto it = index.find(k);
        if (it == index.end()) return false;
        size_t i = it->second;
        entries.erase(entries.begin() + (long)i);
        index.erase(it);
        for (auto& e : index) if (e.second > i) e.second--;
        return true;
    }
};

using NativeFn = std::function<Value(std::vector<Value>&, std::vector<std::pair<std::string,Value>>&, Interpreter&)>;

struct FunctionObj : GCTracked {
    enum class Special { None, Assert, Panic };
    bool isNative = false;
    std::string name;
    Special special = Special::None;
    std::vector<Param> params;
    std::vector<StmtPtr> body;
    std::shared_ptr<Env> closure;
    NativeFn native;
    // Defined out-of-line, after Env is complete.
    void gcEdges(std::vector<std::shared_ptr<GCTracked>>& out) const override;
    void gcClear() override;
};

struct ClassObj : GCTracked {
    std::string name;
    std::shared_ptr<ClassObj> parent;
    std::unordered_map<std::string, std::shared_ptr<FunctionObj>> methods;
    bool isStructType = false;
    // Caches the walk up the `extends` chain. Safe because methods maps are
    // only ever populated once, at class-declaration time (SK::ClassDecl) --
    // there is no way for a Nimble script to add/replace a method on an
    // already-declared class, so a resolved lookup can never go stale.
    // Also caches negative lookups (nullptr) so "no such method" doesn't
    // re-walk the whole chain every time either.
    mutable std::unordered_map<std::string, std::shared_ptr<FunctionObj>> resolvedCache;

    std::shared_ptr<FunctionObj> findMethod(const std::string& n) {
        auto cacheIt = resolvedCache.find(n);
        if (cacheIt != resolvedCache.end()) return cacheIt->second;
        auto it = methods.find(n);
        std::shared_ptr<FunctionObj> result = (it != methods.end()) ? it->second
                                             : (parent ? parent->findMethod(n) : nullptr);
        resolvedCache[n] = result;
        return result;
    }
    void gcEdges(std::vector<std::shared_ptr<GCTracked>>& out) const override {
        if (parent) out.push_back(parent);
        for (auto& kv : methods) if (kv.second) out.push_back(kv.second);
        // resolvedCache entries usually duplicate a reference already found
        // via `methods` or the `parent` chain above, but they're still real,
        // separate shared_ptr copies -- each one must be counted as its own
        // edge so the cycle collector's refcount bookkeeping stays exact.
        for (auto& kv : resolvedCache) if (kv.second) out.push_back(kv.second);
    }
    void gcClear() override { methods.clear(); resolvedCache.clear(); parent.reset(); }
};

struct Env : GCTracked, std::enable_shared_from_this<Env> {
    std::unordered_map<std::string, Value> vars;
    std::unordered_map<std::string, bool> consts;
    std::shared_ptr<Env> parent;
    std::shared_ptr<ClassObj> ownerClass;

    Env(std::shared_ptr<Env> p = nullptr) : parent(p) {}

    bool existsLocal(const std::string& n) { return vars.find(n) != vars.end(); }

    Value* find(const std::string& n) {
        Env* e = this;
        while (e) {
            auto it = e->vars.find(n);
            if (it != e->vars.end()) return &it->second;
            e = e->parent.get();
        }
        return nullptr;
    }
    bool isConst(const std::string& n) {
        Env* e = this;
        while (e) {
            if (e->vars.find(n) != e->vars.end()) return e->consts.count(n) > 0;
            e = e->parent.get();
        }
        return false;
    }
    void define(const std::string& n, const Value& val, bool isConstFlag=false) {
        vars[n] = val;
        if (isConstFlag) consts[n] = true;
    }
    void assign(const std::string& n, const Value& val) {
        Env* e = this;
        while (e) {
            if (e->vars.find(n) != e->vars.end()) {
                if (e->consts.count(n)) throw NimbleError("Cannot reassign constant '" + n + "'");
                e->vars[n] = val;
                return;
            }
            e = e->parent.get();
        }
        vars[n] = val;
    }
    // All names visible from this scope (local variables + the whole
    // parent chain), without duplicates. Only used to build "did you
    // mean...?" suggestions on undefined-variable errors, so it's fine
    // that it's O(scope depth).
    std::vector<std::string> allNames() {
        std::vector<std::string> out;
        std::unordered_set<std::string> seen;
        Env* e = this;
        while (e) {
            for (auto& kv : e->vars) if (seen.insert(kv.first).second) out.push_back(kv.first);
            e = e->parent.get();
        }
        return out;
    }
    // gcEdges is defined out-of-line (needs gcCollectValueEdges, which needs
    // ListObj/MapObj/FunctionObj/ClassObj complete).
    void gcEdges(std::vector<std::shared_ptr<GCTracked>>& out) const override;
    void gcClear() override { vars.clear(); parent.reset(); ownerClass.reset(); }
};

// All four GC-relevant types are complete now: define the out-of-line pieces.
static void gcCollectValueEdges(const Value& v, std::vector<std::shared_ptr<GCTracked>>& out,
                                 std::unordered_set<const void*>& seenContainers) {
    if (v.isObj()) { out.push_back(v.asObj()); }
    else if (v.isFunc()) { out.push_back(v.asFunc()); }
    else if (v.isClass()) { out.push_back(v.asClass()); }
    else if (v.isList()) {
        auto l = v.asList();
        // Guard against a list/map that (directly or indirectly) contains
        // itself, e.g. `l = []; l.push(l)` -- without this, walking its
        // items would recurse forever.
        if (l && seenContainers.insert(l.get()).second)
            for (auto& item : l->items) gcCollectValueEdges(item, out, seenContainers);
    } else if (v.isMap()) {
        auto m = v.asMap();
        if (m && seenContainers.insert(m.get()).second)
            for (auto& kv : m->entries) gcCollectValueEdges(kv.second, out, seenContainers);
    }
}

void Env::gcEdges(std::vector<std::shared_ptr<GCTracked>>& out) const {
    if (parent) out.push_back(parent);
    if (ownerClass) out.push_back(ownerClass);
    std::unordered_set<const void*> seenContainers;
    for (auto& kv : vars) gcCollectValueEdges(kv.second, out, seenContainers);
}

void FunctionObj::gcEdges(std::vector<std::shared_ptr<GCTracked>>& out) const {
    if (closure) out.push_back(closure);
    // NOTE (known limitation): a native function's `native` std::function may
    // capture arbitrary Values by value (e.g. bindMethod's wrapper captures
    // `self`). C++ gives no way to introspect a std::function's captures, so
    // any cycle that runs exclusively through a native closure's captures is
    // invisible to this collector. In practice this only matters for native
    // wrappers created dynamically and stored back onto a long-lived object;
    // bindMethod's own result is no longer cached anywhere (see
    // docs/memory-model.md), so this limitation isn't currently reachable
    // from ordinary Nimble code.
}
void FunctionObj::gcClear() { closure.reset(); }

// ---------------- Cycle collector: registry, algorithm, allocation hook ----------------
struct GCRegistry {
    std::vector<std::weak_ptr<GCTracked>> nodes;
    size_t allocsSinceCollect = 0;
    long lastCollected = 0;
    long totalCollected = 0;
};
static GCRegistry g_gc;
static size_t GC_ALLOC_THRESHOLD = 5000; // run a pass every this-many tracked allocations

// Trial-deletion cycle collection, the same technique CPython's `gc` module
// uses for its cyclic garbage: a shared_ptr's use_count() can't tell "kept
// alive by a real root" apart from "kept alive only by a cycle among
// tracked nodes," so we subtract every edge that originates from another
// tracked node and see what's left.
static void gcCollectCycles() {
    auto& nodes = g_gc.nodes;
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                                [](const std::weak_ptr<GCTracked>& w) { return w.expired(); }),
                nodes.end());

    // Phase 0: snapshot every live node and its real refcount (minus the
    // temporary `sp` used to read it).
    std::vector<std::shared_ptr<GCTracked>> live;
    live.reserve(nodes.size());
    std::unordered_map<const GCTracked*, long> candidateRefs;
    candidateRefs.reserve(nodes.size());
    for (auto& w : nodes) {
        auto sp = w.lock();
        if (!sp) continue;
        candidateRefs[sp.get()] = (long)sp.use_count() - 1;
        live.push_back(std::move(sp));
    }

    // Phase 1: for every edge A -> B where both are tracked, subtract one
    // from B's candidate count. What survives is the number of references
    // to B that do NOT come from another tracked node -- i.e. references
    // held by interpreter state (globals) or a live C++ stack frame.
    std::unordered_map<const GCTracked*, std::vector<std::shared_ptr<GCTracked>>> edgesOf;
    edgesOf.reserve(live.size());
    for (auto& sp : live) {
        std::vector<std::shared_ptr<GCTracked>> edges;
        sp->gcEdges(edges);
        for (auto& target : edges) {
            auto it = candidateRefs.find(target.get());
            if (it != candidateRefs.end()) it->second--;
        }
        edgesOf[sp.get()] = std::move(edges);
    }

    // Phase 2: anything whose candidate count is still > 0 is a genuine
    // root for this pass. Mark everything reachable from those roots alive.
    std::unordered_set<const GCTracked*> alive;
    std::vector<const GCTracked*> stack;
    for (auto& sp : live) if (candidateRefs[sp.get()] > 0) stack.push_back(sp.get());
    while (!stack.empty()) {
        const GCTracked* n = stack.back();
        stack.pop_back();
        if (!alive.insert(n).second) continue;
        auto it = edgesOf.find(n);
        if (it != edgesOf.end()) for (auto& target : it->second) stack.push_back(target.get());
    }

    // Phase 3: sweep. Anything left unmarked is unreachable garbage held up
    // only by a cycle -- break it by clearing its own outgoing pointers;
    // ordinary shared_ptr refcounting reclaims the rest immediately after.
    long collected = 0;
    for (auto& sp : live) {
        if (!alive.count(sp.get())) { sp->gcClear(); collected++; }
    }
    g_gc.lastCollected = collected;
    g_gc.totalCollected += collected;

    // `edgesOf` and `live` both hold extra shared_ptr copies of every node
    // (including the ones we just cleared) -- drop those explicitly before
    // pruning expired entries below, or nothing will look expired yet even
    // though it's already unreachable and structurally disconnected.
    edgesOf.clear();
    live.clear();
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                                [](const std::weak_ptr<GCTracked>& w) { return w.expired(); }),
                nodes.end());
}

template <typename T, typename... Args>
static std::shared_ptr<T> gcMake(Args&&... args) {
    static_assert(std::is_base_of<GCTracked, T>::value, "gcMake<T>() requires a GCTracked type");
    auto p = std::make_shared<T>(std::forward<Args>(args)...);
    g_gc.nodes.push_back(p);
    if (++g_gc.allocsSinceCollect >= GC_ALLOC_THRESHOLD) {
        gcCollectCycles();
        g_gc.allocsSinceCollect = 0;
    }
    return p;
}

// Control flow inside a function body (break/continue/return) is threaded
// through explicit return values instead of C++ exceptions. Throwing and
// catching a C++ exception on every single 'return', and on every 'break' in
// a hot loop, has real runtime cost and also made it possible for a
// break/continue to escape past a function-call boundary and be silently
// caught by an unrelated loop at the call site (see invokeUserFunction).
// 'throw'/'catch' (ThrowSignal) is left exception-based: it's meant to
// unwind an arbitrary number of frames and isn't a hot path.
enum class Flow { Normal, Break, Continue, Return };
struct ExecResult {
    Flow flow = Flow::Normal;
    Value value;
    ExecResult() = default;
    ExecResult(Flow f) : flow(f) {}
    ExecResult(Flow f, Value v) : flow(f), value(std::move(v)) {}
};
struct ThrowSignal { Value value; std::string message; };

// ============================================================
// Utilidades
// ============================================================
static std::string numToStr(double d) {
    if (std::isnan(d)) return "nan";
    if (std::isinf(d)) return d < 0 ? "-inf" : "inf";
    if (d == (long long)d && std::abs(d) < 1e15) {
        char buf[64]; snprintf(buf, sizeof(buf), "%lld", (long long)d);
        return buf;
    }
    // std::to_chars produces the shortest decimal string that round-trips
    // back to the exact same double -- unlike ostringstream's default
    // 6-significant-digit precision, this doesn't silently truncate values
    // like 3.14159265358979 down to "3.14159" or 1234567.891 down to
    // "1.23457e+06". See bug report: numbers were losing precision in
    // print(), string(), concatenation and json.encode().
    char buf[64];
    auto res = std::to_chars(buf, buf + sizeof(buf), d);
    return std::string(buf, res.ptr);
}

std::string toDisplayString(const Value& val);

static std::string joinList(const std::shared_ptr<ListObj>& l, const std::string& sep=", ") {
    std::string out;
    for (size_t i = 0; i < l->items.size(); i++) {
        if (i) out += sep;
        Value& it = l->items[i];
        out += it.isStr() ? ("\"" + it.asStr() + "\"") : toDisplayString(it);
    }
    return out;
}

std::string toDisplayString(const Value& val) {
    if (val.isNull()) return "null";
    if (val.isBool()) return val.asBool() ? "true" : "false";
    if (val.isNum()) return numToStr(val.asNum());
    if (val.isStr()) return val.asStr();
    if (val.isList()) return "[" + joinList(val.asList()) + "]";
    if (val.isMap()) {
        auto m = val.asMap();
        std::string out = "{";
        for (size_t i = 0; i < m->entries.size(); i++) {
            if (i) out += ", ";
            out += m->entries[i].first + ": ";
            Value& it = m->entries[i].second;
            out += it.isStr() ? ("\"" + it.asStr() + "\"") : toDisplayString(it);
        }
        out += "}";
        return out;
    }
    if (val.isFunc()) return "<func " + val.asFunc()->name + ">";
    if (val.isClass()) return "<class " + val.asClass()->name + ">";
    if (val.isObj()) {
        auto e = val.asObj();
        return e->ownerClass ? ("<" + e->ownerClass->name + " instance>") : std::string("<object>");
    }
    return "?";
}

static bool truthy(const Value& v) {
    if (v.isNull()) return false;
    if (v.isBool()) return v.asBool();
    if (v.isNum()) return v.asNum() != 0;
    if (v.isStr()) return !v.asStr().empty();
    if (v.isList()) return !v.asList()->items.empty();
    if (v.isMap()) return !v.asMap()->entries.empty();
    return true;
}

static bool structuralEquals(const Value& a, const Value& b) {
    if (a.isNull() && b.isNull()) return true;
    if (a.isNum() && b.isNum()) return a.asNum() == b.asNum();
    if (a.isBool() && b.isBool()) return a.asBool() == b.asBool();
    if (a.isStr() && b.isStr()) return a.asStr() == b.asStr();
    if (a.isList() && b.isList()) {
        auto la = a.asList(), lb = b.asList();
        if (la->items.size() != lb->items.size()) return false;
        for (size_t i = 0; i < la->items.size(); i++)
            if (!structuralEquals(la->items[i], lb->items[i])) return false;
        return true;
    }
    if (a.isMap() && b.isMap()) {
        auto ma = a.asMap(), mb = b.asMap();
        if (ma->entries.size() != mb->entries.size()) return false;
        for (auto& e : ma->entries) {
            if (!mb->has(e.first)) return false;
            if (!structuralEquals(e.second, mb->get(e.first))) return false;
        }
        return true;
    }
    if (a.isObj() && b.isObj()) {
        auto ea = a.asObj(), eb = b.asObj();
        if (ea == eb) return true;
        if (ea->ownerClass && eb->ownerClass && ea->ownerClass == eb->ownerClass && ea->ownerClass->isStructType) {
            if (ea->vars.size() != eb->vars.size()) return false;
            for (auto& [k, v] : ea->vars) {
                auto it = eb->vars.find(k);
                if (it == eb->vars.end()) return false;
                if (!structuralEquals(v, it->second)) return false;
            }
            return true;
        }
        return false;
    }
    if (a.isClass() && b.isClass()) return a.asClass() == b.asClass();
    return false;
}

static bool identityEquals(const Value& a, const Value& b) {
    if (a.isList() && b.isList()) return a.asList() == b.asList();
    if (a.isMap() && b.isMap()) return a.asMap() == b.asMap();
    if (a.isFunc() && b.isFunc()) return a.asFunc() == b.asFunc();
    if (a.isObj() && b.isObj()) return a.asObj() == b.asObj();
    if (a.isClass() && b.isClass()) return a.asClass() == b.asClass();
    return structuralEquals(a, b);
}

static const char* typeName(const Value& v) {
    if (v.isNull()) return "null";
    if (v.isBool()) return "boolean";
    if (v.isNum()) return "number";
    if (v.isStr()) return "string";
    if (v.isList()) return "list";
    if (v.isMap()) return "map";
    if (v.isFunc()) return "function";
    if (v.isClass()) return "class";
    if (v.isObj()) return v.asObj()->ownerClass ? v.asObj()->ownerClass->name.c_str() : "object";
    return "unknown";
}

static double toNumber(const Value& v) {
    if (v.isNum()) return v.asNum();
    if (v.isBool()) return v.asBool() ? 1 : 0;
    if (v.isStr()) {
        try { return std::stod(v.asStr()); } catch (...) { throw NimbleError("Could not convert \"" + v.asStr() + "\" to a number"); }
    }
    throw NimbleError(std::string("Cannot convert ") + typeName(v) + " to a number");
}

// ---------------------------------------------------------------
// Safe argument accessors for native functions.
//
// Value::asStr()/asNum()/asList()/asMap()/asBool() are thin wrappers over
// std::get<T>(variant), which throws std::bad_variant_access -- a plain C++
// exception, NOT a NimbleError -- if the variant doesn't currently hold T.
// Before these helpers existed, most native functions (ord, chr, read,
// string.replace/find/split, regex.*, csv.*, env.*, time.*, system.*,
// map/filter/reduce/zip/enumerate/sum/any/all/random.choice, and json.decode's
// object-key handling, among others) called `a[i].asStr()` / `.asList()`
// directly on a script-supplied argument with no isStr()/isList() check first,
// and `a[i]` itself on the args vector with no bounds check. A script passing
// the wrong type or too few arguments (e.g. `ord(42)`, `"x".replace(5, "y")`,
// `min([])`, `json.decode(r"{1: 2}")`) reached std::get/vector::operator[]
// straight through and crashed the ENTIRE HOST PROCESS with an uncaught
// std::bad_variant_access/std::out_of_range (SIGABRT) -- something no
// try/catch in the script could stop, since it isn't a NimbleError, and
// something that defeats the whole point of the sandbox permission system:
// a host that locks down every dangerous permission could still have its
// process taken down by a single mistyped argument in an otherwise-idle
// script. These helpers turn every such case into an ordinary, catchable
// NimbleError instead. Use them for any value that comes directly from a
// native function's argument list; toNumber() above already does the
// equivalent safe conversion/validation for numbers, so reqNum() below is
// just toNumber() plus a bounds check.
static const Value& reqArg(std::vector<Value>& a, size_t i, const char* fn) {
    if (i >= a.size())
        throw NimbleError(std::string(fn) + "(): expected at least " + std::to_string(i + 1) +
                           " argument" + (i == 0 ? "" : "s") + ", got " + std::to_string(a.size()));
    return a[i];
}
static const std::string& reqStr(std::vector<Value>& a, size_t i, const char* fn) {
    const Value& v = reqArg(a, i, fn);
    if (!v.isStr())
        throw NimbleError(std::string(fn) + "(): argument " + std::to_string(i + 1) +
                           " must be a string, got " + typeName(v));
    return v.asStr();
}
static double reqNum(std::vector<Value>& a, size_t i, const char* fn) {
    return toNumber(reqArg(a, i, fn));
}
static std::shared_ptr<ListObj> reqList(std::vector<Value>& a, size_t i, const char* fn) {
    const Value& v = reqArg(a, i, fn);
    if (!v.isList())
        throw NimbleError(std::string(fn) + "(): argument " + std::to_string(i + 1) +
                           " must be a list, got " + typeName(v));
    return v.asList();
}
static std::shared_ptr<MapObj> reqMap(std::vector<Value>& a, size_t i, const char* fn) {
    const Value& v = reqArg(a, i, fn);
    if (!v.isMap())
        throw NimbleError(std::string(fn) + "(): argument " + std::to_string(i + 1) +
                           " must be a map, got " + typeName(v));
    return v.asMap();
}
// A non-empty list is required (min/max/random.choice/reduce-without-init):
// items.at(0)/items[0] on an empty vector is either an uncaught
// std::out_of_range or plain undefined behavior -- also not a NimbleError.
static void reqNonEmptyList(const std::shared_ptr<ListObj>& l, const char* fn) {
    if (l->items.empty()) throw NimbleError(std::string(fn) + "(): list must not be empty");
}

// ---- MD5 (RFC 1321) ----
// Devuelve los 16 bytes crudos del digest. Trabaja sobre bytes (std::string
// puede llevar \0 y cualquier valor), asi que sirve igual para texto y para
// datos binarios (p. ej. lo que produce hex.decode).
static std::string md5Raw(const std::string& in) {
    // K[i] = floor(2^32 * |sin(i + 1)|), tal como define el RFC
    static uint32_t K[64];
    static bool kInit = false;
    if (!kInit) {
        for (int i = 0; i < 64; i++)
            K[i] = (uint32_t)(std::fabs(std::sin((double)(i + 1))) * 4294967296.0);
        kInit = true;
    }
    static const int S[64] = {
        7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20, 5,  9, 14, 20,
        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
    };
    uint32_t a0 = 0x67452301u, b0 = 0xefcdab89u, c0 = 0x98badcfeu, d0 = 0x10325476u;

    // relleno: 0x80, ceros hasta 56 mod 64, y la longitud en bits (8 bytes LE)
    std::string msg = in;
    uint64_t bitLen = (uint64_t)in.size() * 8;
    msg += (char)0x80;
    while (msg.size() % 64 != 56) msg += (char)0;
    for (int i = 0; i < 8; i++) msg += (char)((bitLen >> (8 * i)) & 0xFF);

    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t M[16];
        for (int i = 0; i < 16; i++) {
            const unsigned char* q = (const unsigned char*)msg.data() + off + i * 4;
            M[i] = (uint32_t)q[0] | ((uint32_t)q[1] << 8) | ((uint32_t)q[2] << 16) | ((uint32_t)q[3] << 24);
        }
        uint32_t A = a0, B = b0, C = c0, D = d0;
        for (int i = 0; i < 64; i++) {
            uint32_t F; int g;
            if (i < 16)      { F = (B & C) | (~B & D); g = i; }
            else if (i < 32) { F = (D & B) | (~D & C); g = (5 * i + 1) % 16; }
            else if (i < 48) { F = B ^ C ^ D;          g = (3 * i + 5) % 16; }
            else             { F = C ^ (B | ~D);       g = (7 * i) % 16; }
            F = F + A + K[i] + M[g];
            A = D; D = C; C = B;
            B = B + ((F << S[i]) | (F >> (32 - S[i])));
        }
        a0 += A; b0 += B; c0 += C; d0 += D;
    }
    std::string out;
    for (uint32_t v : {a0, b0, c0, d0})
        for (int i = 0; i < 4; i++) out += (char)((v >> (8 * i)) & 0xFF);
    return out;
}
static std::string toHexLower(const std::string& raw) {
    static const char* digits = "0123456789abcdef";
    std::string out; out.reserve(raw.size() * 2);
    for (unsigned char c : raw) { out += digits[c >> 4]; out += digits[c & 0xF]; }
    return out;
}

// ---- utilidades para csv ----
static std::vector<std::vector<std::string>> parseCsvRows(const std::string& text) {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string cell;
    bool inQuotes = false;
    size_t i = 0;
    while (i < text.size()) {
        char c = text[i];
        if (inQuotes) {
            if (c == '"' && i + 1 < text.size() && text[i+1] == '"') { cell += '"'; i += 2; }
            else if (c == '"') { inQuotes = false; i++; }
            else { cell += c; i++; }
        } else {
            if (c == '"') { inQuotes = true; i++; }
            else if (c == ',') { row.push_back(cell); cell.clear(); i++; }
            else if (c == '\n') {
                row.push_back(cell); cell.clear();
                rows.push_back(row); row.clear();
                i++;
            }
            else if (c == '\r') { i++; }
            else { cell += c; i++; }
        }
    }
    if (!cell.empty() || !row.empty()) {
        row.push_back(cell);
        rows.push_back(row);
    }
    return rows;
}

static std::string csvEscape(const std::string& s) {
    bool needsQuotes = s.find_first_of(",\"\n\r") != std::string::npos;
    if (!needsQuotes) return s;
    std::string out = "\"";
    for (char c : s) { if (c == '"') out += "\"\""; else out += c; }
    out += "\"";
    return out;
}

// ---- callback de curl ----
#ifdef HAVE_CURL
static size_t nimbleCurlWrite(char* ptr, size_t size, size_t nmemb, void* ud) {
    std::string* s = static_cast<std::string*>(ud);
    s->append(ptr, size * nmemb);
    return size * nmemb;
}
#endif

// ============================================================
// Interpreter
// ============================================================
class Interpreter {
public:
    std::shared_ptr<Env> builtins = gcMake<Env>();
    std::shared_ptr<Env> globals = gcMake<Env>(builtins);
    std::vector<std::string> programArgs;
    std::string scriptDir = ".";
    std::vector<std::string> importStack;
    std::unordered_map<std::string, Value> moduleCache;

    struct CallFrame { std::string name; int callLine; };
    std::vector<CallFrame> callStack;
    std::vector<std::pair<std::string, std::vector<StmtPtr>>> tests;

    // ================================================================
    // Sandbox / permisos — para uso embebido (ej. scripting de un juego).
    // Por defecto TODO está permitido (así el CLI se comporta igual que
    // siempre); un host que embeba el intérprete puede restringir esto
    // ANTES de llamar a run()/runFile(), incluso después de construir el
    // objeto Interpreter.
    // ================================================================
    struct SandboxPermissions {
        bool allowFileRead = true;
        bool allowFileWrite = true;    // write/append
        bool allowFileSystemOps = true; // delete/listdir/mkdir/rmdir
        bool allowFileModules = true;   // 'use "archivo.nimble"' / 'use paquete.modulo'
        bool allowSystemExec = true;    // system.run / system.exec
        bool allowNetwork = true;       // http.get / http.post
        bool allowEnvRead = true;
        bool allowEnvWrite = true;
        bool allowExit = true;         // exit() -- a script terminating the host process
        // Si confineToRoot es true, TODA ruta de archivo (read/write/.../use de
        // módulos por archivo) se resuelve relativa a sandboxRoot y se rechaza
        // si el resultado normalizado queda fuera de esa carpeta (bloquea
        // "../../etc/passwd" y rutas absolutas fuera del sandbox).
        // Aviso honesto: esto es una defensa por normalización léxica de rutas,
        // NO protege contra symlinks dentro del sandbox que apunten afuera.
        bool confineToRoot = false;
        std::string sandboxRoot = ".";
    };
    SandboxPermissions permissions;

    // ---- límites de ejecución (protegen contra bucles infinitos y
    // recursión descontrolada; importan tanto para scripts embebidos en un
    // juego como para el uso normal desde la CLI) ----
    long long maxSteps = -1;      // -1 = sin límite (comportamiento actual)
    long long stepCount = 0;
    bool hasDeadline = false;
    std::chrono::steady_clock::time_point deadline;
    int maxCallDepth = 500;       // SIEMPRE activo: evita un stack overflow real de C++.
                                   // Medido empíricamente: en este sistema (~8MB de stack),
                                   // 3000 aguanta pero 4000 ya revienta el proceso. 500 deja
                                   // un margen amplio, incluyendo hilos de trabajo con stacks
                                   // más chicos (comunes en motores de juego). Si tu host usa
                                   // un hilo dedicado con stack grande, podés subir este valor;
                                   // si usa un hilo con stack chico, bajalo más.
    int callDepth = 0;

    // Señal de límite excedido: a propósito NO es capturable por 'try/catch'
    // del script (igual que AbortSignal de assert/panic), porque si un script
    // pudiera atraparla y seguir, el límite del host no significaría nada.
    struct ExecutionLimitExceeded { std::string message; };

    void setStepLimit(long long steps) { maxSteps = steps; stepCount = 0; }
    void setTimeLimit(double seconds) {
        hasDeadline = seconds > 0;
        deadline = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds));
    }

    void checkLimits() {
        if (maxSteps >= 0 && ++stepCount > maxSteps)
            throw ExecutionLimitExceeded{"execution step limit exceeded (" + std::to_string(maxSteps) + ")"};
        if (hasDeadline && std::chrono::steady_clock::now() >= deadline)
            throw ExecutionLimitExceeded{"execution time limit exceeded"};
    }

    // Resuelve una ruta de archivo respetando el confinamiento de sandbox
    // (si está activo). 'opName' es solo para el mensaje de error.
    std::string confinePath(const std::string& rawPath, const char* opName) {
        if (!permissions.confineToRoot) return rawPath;
        namespace fs = std::filesystem;
        fs::path root = fs::absolute(nimblePath(permissions.sandboxRoot)).lexically_normal();
        fs::path candidate = nimblePath(rawPath);
        fs::path full = (candidate.is_absolute() ? candidate : (root / candidate)).lexically_normal();
        std::string rootStr = nimblePathGeneric(root);
        std::string fullStr = nimblePathGeneric(full);
        bool inside = fullStr.size() >= rootStr.size() && fullStr.compare(0, rootStr.size(), rootStr) == 0 &&
                      (fullStr.size() == rootStr.size() || fullStr[rootStr.size()] == '/');
        if (!inside)
            throw NimbleError(std::string(opName) + ": access outside the sandbox ('" + rawPath + "')");
        return nimblePathStr(full);
    }

    // ================================================================
    // API de embebido: para que un host en C++ (ej. un juego) registre
    // sus propias funciones nativas y llame funciones definidas en el script.
    // ================================================================
    void registerNative(const std::string& name, NativeFn fn) {
        auto f = gcMake<FunctionObj>();
        f->isNative = true; f->name = name; f->native = std::move(fn);
        builtins->define(name, Value(f));
    }

    // Llama una función definida por el script (ej. "_ready", "_process") por
    // su nombre global. Lanza NimbleError si no existe o no es invocable.
    Value callGlobal(const std::string& name, std::vector<Value> args = {}) {
        Value* v = globals->find(name);
        if (!v) throw NimbleError("callGlobal: no global function named '" + name + "'");
        std::vector<std::pair<std::string, Value>> named;
        return callFunction(*v, args, named);
    }

    explicit Interpreter(std::vector<std::string> args = {})
        : programArgs(std::move(args)) { setupGlobals(); }

    // ================================================================
    // Por qué hace falta este destructor (ver docs/memory-model.md):
    // toda declaración `func`/`class` de nivel superior crea un ciclo
    // globals -> vars[nombre] -> fn -> fn->closure -> globals. Mientras
    // el Interpreter vive, ese ciclo es inofensivo: el propio miembro
    // `globals` (una raíz "real", no rastreada como arista de otro nodo)
    // hace que el recolector de trial-deletion lo detecte como alcanzable
    // desde afuera y lo mantenga vivo correctamente.
    //
    // El problema es al destruirse el Interpreter: esa raíz real
    // desaparece, pero nada dispara una nueva pasada del recolector, así
    // que el ciclo (ahora sí basura) queda huérfano en el registro global
    // para siempre -- una fuga por cada Interpreter creado y destruido.
    //
    // La solución: soltar explícitamente las referencias reales que este
    // Interpreter sostenía (globals, builtins, moduleCache pueden
    // contener Envs/funciones/clases) ANTES de forzar una pasada de
    // gcCollectCycles(). Si algo de eso sigue vivo por una razón legítima
    // -- p.ej. un host embebido guardó en C++ una función devuelta por
    // callGlobal() -- esa referencia externa sigue contando como raíz
    // real para el recolector y no se libera de más.
    ~Interpreter() {
        globals.reset();
        builtins.reset();
        moduleCache.clear();
        gcCollectCycles();
    }

    void run(std::vector<StmtPtr>& program) {
        try {
            ExecResult r = execBlock(program, globals);
            if (r.flow == Flow::Break) std::cerr << "Error: 'break' outside of a loop\n";
            else if (r.flow == Flow::Continue) std::cerr << "Error: 'continue' outside of a loop\n";
            // Flow::Return at top level simply ends the script early; ignore.
        } catch (ThrowSignal& ts) {
            std::cerr << "Uncaught error: " << ts.message << std::endl;
        }
    }

    int runTests() {
        if (tests.empty()) {
            std::cout << "No tests defined.\n";
            return 0;
        }
        int passed = 0, failed = 0;
        for (auto& [name, body] : tests) {
            try {
                ExecResult r = execBlock(body, globals);
                if (r.flow == Flow::Break || r.flow == Flow::Continue) {
                    std::cout << "  \xE2\x9C\x97 " << name << ": 'break'/'continue' outside of a loop\n";
                    failed++;
                    continue;
                }
                std::cout << "  \xE2\x9C\x93 " << name << "\n";
                passed++;
            } catch (NimbleError& e) {
                std::cout << "  \xE2\x9C\x97 " << name << ": " << e.what() << "\n";
                failed++;
            } catch (ThrowSignal& ts) {
                std::cout << "  \xE2\x9C\x97 " << name << ": " << ts.message << "\n";
                failed++;
            } catch (AbortSignal& as) {
                std::cout << "  \xE2\x9C\x97 " << name << ": fatal: " << as.msg << "\n";
                failed++;
            } catch (ExecutionLimitExceeded& le) {
                std::cout << "  \xE2\x9C\x97 " << name << ": execution limit exceeded: " << le.message << "\n";
                failed++;
            }
        }
        std::cout << "\n" << passed << " passed, " << failed << " failed\n";
        return failed > 0 ? 1 : 0;
    }

    ExecResult execBlock(std::vector<StmtPtr>& body, const std::shared_ptr<Env>& env) {
        for (auto& st : body) {
            ExecResult r = execStmt(st, env);
            if (r.flow != Flow::Normal) return r;
        }
        return {};
    }

    ExecResult execStmt(StmtPtr& st, const std::shared_ptr<Env>& env) {
        try {
            return execStmtInner(st, env);
        } catch (NimbleError& e) {
            if (e.line == 0) {
                NimbleError ne(e.raw, st->line);
                ne.stack = std::move(e.stack);
                throw ne;
            }
            throw;
        }
    }

    ExecResult execStmtInner(StmtPtr& st, const std::shared_ptr<Env>& env) {
        switch (st->kind) {
            case SK::ExprStmt: eval(st->expr, env); return {};
            case SK::Test: tests.push_back({st->name, st->body}); return {};
            case SK::ConstDecl: {
                Value val = eval(st->expr, env);
                env->define(st->name, val, true);
                return {};
            }
            case SK::If: {
                for (auto& br : st->branches) {
                    if (truthy(eval(br.cond, env))) {
                        return execBlock(br.body, env);
                    }
                }
                if (st->hasElse) return execBlock(st->elseBody, env);
                return {};
            }
            case SK::While: {
                while (truthy(eval(st->expr, env))) {
                    checkLimits();
                    ExecResult r = execBlock(st->body, env);
                    if (r.flow == Flow::Break) break;
                    if (r.flow == Flow::Return) return r;
                    // Flow::Continue / Flow::Normal: fall through to re-check the condition.
                }
                return {};
            }
            case SK::For: {
                Value iterable = eval(st->iterable, env);
                if (iterable.isMap()) {
                    // Iteracion perezosa (open_lines): un mapa con "__next__" se
                    // recorre de a un elemento sin copiar nada; null = fin.
                    Value nx = iterable.asMap()->get("__next__");
                    if (nx.isFunc()) {
                        if (!st->iterVar2.empty())
                            throw NimbleError("for with two variables is not supported over a line reader "
                                               "(use a counter variable instead)");
                        while (true) {
                            checkLimits();
                            Value item = callFunction(nx, {}, {});
                            if (item.isNull()) break;
                            env->define(st->iterVar, item);
                            ExecResult r = execBlock(st->body, env);
                            if (r.flow == Flow::Break) break;
                            if (r.flow == Flow::Return) return r;
                        }
                        return {};
                    }
                }
                if (iterable.isMap() && !st->iterVar2.empty()) {
                    for (auto& e : iterable.asMap()->entries) {
                        checkLimits();
                        env->define(st->iterVar, Value(e.first));
                        env->define(st->iterVar2, e.second);
                        ExecResult r = execBlock(st->body, env);
                        if (r.flow == Flow::Break) break;
                        if (r.flow == Flow::Return) return r;
                    }
                    return {};
                }
                if (iterable.isList() && !st->iterVar2.empty()) {
                    // `for a, b in someList` destructures each item, assuming it's a
                    // 2-element list -- this is what makes `for i, x in enumerate(lst)`
                    // work, matching enumerate()'s documented [index, value] shape.
                    for (auto& item : iterable.asList()->items) {
                        checkLimits();
                        if (!item.isList() || item.asList()->items.size() != 2)
                            throw NimbleError("for with two variables over a list requires each "
                                               "item to be a 2-element list (like enumerate() produces)");
                        env->define(st->iterVar, item.asList()->items[0]);
                        env->define(st->iterVar2, item.asList()->items[1]);
                        ExecResult r = execBlock(st->body, env);
                        if (r.flow == Flow::Break) break;
                        if (r.flow == Flow::Return) return r;
                    }
                    return {};
                }
                std::vector<Value> items;
                if (iterable.isList()) items = iterable.asList()->items;
                else if (iterable.isStr()) { for (char c : iterable.asStr()) items.push_back(Value(std::string(1, c))); }
                else if (iterable.isMap()) { for (auto& e : iterable.asMap()->entries) items.push_back(Value(e.first)); }
                else throw NimbleError("cannot iterate over this value with 'for'");
                for (auto& item : items) {
                    checkLimits();
                    env->define(st->iterVar, item);
                    ExecResult r = execBlock(st->body, env);
                    if (r.flow == Flow::Break) break;
                    if (r.flow == Flow::Return) return r;
                }
                return {};
            }
            case SK::FuncDecl: {
                // NOTE (reference-cycle leak, see docs/memory-model.md): fn->closure == env,
                // and env->vars[st->name] will hold this same fn, so a named function
                // declaration is a self-referential shared_ptr cycle by construction. If
                // `env` is a short-lived call environment (e.g. a helper function declared
                // inside another function that runs often), that Env, this FunctionObj, and
                // everything `env` transitively points to via its `parent` chain never gets
                // freed. This is required for legitimate closures that escape their
                // defining scope (see make_adder-style factories) so it can't be fixed with
                // a blanket weak_ptr here -- avoid declaring `func` inside hot/frequently
                // called functions; hoist reusable helpers to module scope instead.
                auto fn = gcMake<FunctionObj>();
                fn->isNative = false;
                fn->name = st->name;
                fn->params = st->params;
                fn->body = st->body;
                fn->closure = env;
                env->define(st->name, Value(fn));
                return {};
            }
            case SK::Return: {
                Value val = st->expr ? eval(st->expr, env) : Value();
                return {Flow::Return, val};
            }
            case SK::Break: return {Flow::Break};
            case SK::Continue: return {Flow::Continue};
            case SK::Throw: {
                Value val = eval(st->expr, env);
                throw ThrowSignal{val, toDisplayString(val)};
            }
            case SK::Try: {
                ExecResult r;
                std::exception_ptr pending; // set if body/catch raised something finally must still run before

                try {
                    try {
                        r = execBlock(st->body, env);
                    } catch (ThrowSignal& ts) {
                        if (st->hasCatch) {
                            if (!st->catchVar.empty()) env->define(st->catchVar, ts.value);
                            r = execBlock(st->catchBody, env);
                        } else {
                            throw; // no catch here: let it through (finally still runs first, below)
                        }
                    } catch (NimbleError& ne) {
                        if (st->hasCatch) {
                            if (!st->catchVar.empty()) env->define(st->catchVar, Value(std::string(ne.what())));
                            r = execBlock(st->catchBody, env);
                        } else {
                            throw;
                        }
                    }
                } catch (...) {
                    // Also catches anything a catch-block itself raised, and any
                    // exception type this Try doesn't otherwise handle (e.g.
                    // ExecutionLimitExceeded/AbortSignal) -- finally must run
                    // either way before it propagates.
                    pending = std::current_exception();
                }

                if (st->hasFinally) {
                    ExecResult fr = execBlock(st->finallyBody, env);
                    if (fr.flow != Flow::Normal) {
                        // finally's own return/break/continue takes precedence
                        // over whatever the try/catch was doing, including
                        // suppressing a pending exception -- same rule as
                        // Java/Python/JS's try/finally.
                        return fr;
                    }
                }

                if (pending) std::rethrow_exception(pending);
                return r;
            }
            case SK::ClassDecl: {
                auto cls = gcMake<ClassObj>();
                cls->name = st->name;
                if (!st->extendsName.empty()) {
                    Value* pv = env->find(st->extendsName);
                    if (!pv || !pv->isClass())
                        throw NimbleError("'" + st->extendsName + "' is not a valid class for 'extends'");
                    cls->parent = pv->asClass();
                }
                for (auto& m : st->body) {
                    auto fn = gcMake<FunctionObj>();
                    fn->isNative = false;
                    fn->name = m->name;
                    fn->params = m->params;
                    fn->body = m->body;
                    fn->closure = env;
                    cls->methods[m->name] = fn;
                }
                env->define(st->name, Value(cls));
                return {};
            }
            case SK::Match: {
                Value subject = eval(st->expr, env);
                for (auto& c : st->branches) {
                    bool matched = false;
                    for (auto& labelExpr : c.matchLabels) {
                        Value label = eval(labelExpr, env);
                        if (structuralEquals(subject, label)) { matched = true; break; }
                    }
                    if (matched) {
                        return execBlock(c.body, env);
                    }
                }
                if (st->hasElse) return execBlock(st->elseBody, env);
                return {};
            }
            case SK::EnumDecl: {
                auto m = std::make_shared<MapObj>();
                for (size_t i = 0; i < st->enumMembers.size(); i++) m->set(st->enumMembers[i], Value((double)i));
                env->define(st->name, Value(m), true);
                return {};
            }
            case SK::TypeDecl: {
                auto cls = gcMake<ClassObj>();
                cls->name = st->name;
                cls->isStructType = true;
                auto initFn = gcMake<FunctionObj>();
                initFn->isNative = false;
                initFn->name = "init";
                initFn->params = st->params;
                initFn->closure = env;
                for (auto& pr : st->params) {
                    auto assignExpr = mkExpr(EK::Assign);
                    assignExpr->op = "=";
                    auto memberTarget = mkExpr(EK::Member);
                    auto selfVar = mkExpr(EK::Var); selfVar->name = "self";
                    memberTarget->a = selfVar; memberTarget->name = pr.name;
                    assignExpr->a = memberTarget;
                    auto valueVar = mkExpr(EK::Var); valueVar->name = pr.name;
                    assignExpr->b = valueVar;
                    auto exprStmt = mkStmt(SK::ExprStmt); exprStmt->expr = assignExpr;
                    initFn->body.push_back(exprStmt);
                }
                cls->methods["init"] = initFn;
                env->define(st->name, Value(cls), true);
                return {};
            }
            case SK::Use: {
                // NOTE (v0.9 fix): "gc" was missing from this list even though it's
                // registered as a native module exactly like the others (see
                // g->define("gc", ...) in setupGlobals). Every builtin module is
                // actually reachable without 'use' at all (they live in `builtins`,
                // the parent of `globals`), so this list only controls whether the
                // bare `use gc` / `use gc as g` / `use gc (collect, stats)` forms
                // work -- but before this fix, all three of those documented forms
                // threw "Could not import module 'gc.nimble'" instead of just
                // resolving the already-registered native module.
                static const std::set<std::string> builtinNames = {
                    "math", "random", "time", "system", "json", "path", "http", "regex", "hex",
                    "env", "csv", "gc", "net", "hash"
                };
                bool isBuiltin = !st->moduleIsLiteral && st->modulePath.find('.') == std::string::npos &&
                                 builtinNames.count(st->modulePath);
                Value modVal;
                if (isBuiltin) {
                    Value* bv = builtins->find(st->modulePath);
                    if (!bv) throw NimbleError("Native module not found: " + st->modulePath);
                    modVal = *bv;
                } else {
                    if (!permissions.allowFileModules)
                        throw NimbleError("use: importing file modules is disabled by the host (sandbox)");
                    std::string filePath = st->moduleIsLiteral ? st->modulePath : dottedToPath(st->modulePath) + ".nimble";
                    modVal = loadModule(filePath);
                }
                if (!st->importNames.empty()) {
                    for (auto& nm : st->importNames) {
                        Value fieldVal;
                        bool found = false;
                        if (modVal.isObj()) {
                            auto e = modVal.asObj();
                            auto it = e->vars.find(nm);
                            if (it != e->vars.end()) { fieldVal = it->second; found = true; }
                        } else if (modVal.isMap()) {
                            auto m = modVal.asMap();
                            if (m->has(nm)) { fieldVal = m->get(nm); found = true; }
                        }
                        if (!found) throw NimbleError("Module '" + st->modulePath + "' does not export '" + nm + "'");
                        env->define(nm, fieldVal);
                    }
                    return {};
                }
                if (isBuiltin) {
                    if (!st->moduleAlias.empty()) env->define(st->moduleAlias, modVal);
                    return {};
                }
                std::string bindName = !st->moduleAlias.empty() ? st->moduleAlias
                                        : moduleDefaultName(st->modulePath, st->moduleIsLiteral);
                env->define(bindName, modVal);
                return {};
            }
        }
        return {};
    }

    // ---------------- Expressions ----------------
    Value eval(ExprPtr& e, const std::shared_ptr<Env>& env) {
        try {
            return evalInner(e, env);
        } catch (NimbleError& err) {
            if (err.line == 0) {
                NimbleError ne(err.raw, e->line);
                ne.stack = std::move(err.stack);
                throw ne;
            }
            throw;
        }
    }

    Value evalInner(ExprPtr& e, const std::shared_ptr<Env>& env) {
        switch (e->kind) {
            case EK::Number: return Value(e->num);
            case EK::Str: return Value(e->isRaw ? e->str : interpolate(e->str, env));
            case EK::Bool: return Value(e->boolean);
            case EK::Null: return Value();
            case EK::Var: {
                Value* v = env->find(e->name);
                if (!v) throw NimbleError("Undefined variable: '" + e->name + "'" + ErrHelp::suggest(e->name, env->allNames()));
                return *v;
            }
            case EK::Unary: {
                if (e->op == "not") return Value(!truthy(eval(e->a, env)));
                Value v = eval(e->a, env);
                double n = toNumber(v);
                return Value(e->op == "-" ? -n : n);
            }
            case EK::Logical: {
                Value l = eval(e->a, env);
                if (e->op == "and") return truthy(l) ? eval(e->b, env) : l;
                else return truthy(l) ? l : eval(e->b, env);
            }
            case EK::Binary: return evalBinary(e, env);
            case EK::InOp: {
                Value needle = eval(e->a, env);
                Value hay = eval(e->b, env);
                if (hay.isList()) {
                    for (auto& it : hay.asList()->items) if (structuralEquals(it, needle)) return Value(true);
                    return Value(false);
                }
                if (hay.isStr()) {
                    if (!needle.isStr()) throw NimbleError("'in' on a string requires a string");
                    return Value(hay.asStr().find(needle.asStr()) != std::string::npos);
                }
                if (hay.isMap()) {
                    if (!needle.isStr()) return Value(false);
                    return Value(hay.asMap()->has(needle.asStr()));
                }
                throw NimbleError("'in' is not supported for this type");
            }
            case EK::Ternary: return truthy(eval(e->c, env)) ? eval(e->a, env) : eval(e->b, env);
            case EK::ListLit: {
                auto lst = std::make_shared<ListObj>();
                for (auto& el : e->list) {
                    if (el->kind == EK::Spread) {
                        Value operand = eval(el->a, env);
                        if (!operand.isList()) throw NimbleError("Spread '...' in a list requires a list");
                        for (auto& item : operand.asList()->items) lst->items.push_back(item);
                    } else {
                        lst->items.push_back(eval(el, env));
                    }
                }
                return Value(lst);
            }
            case EK::Spread:
                throw NimbleError("'...' can only be used inside a list or map literal");
            case EK::ListComp: {
                Value iterable = eval(e->list[0], env);
                auto out = std::make_shared<ListObj>();
                std::vector<Value> items;
                if (iterable.isList()) items = iterable.asList()->items;
                else if (iterable.isStr()) { for (char c : iterable.asStr()) items.push_back(Value(std::string(1,c))); }
                else throw NimbleError("A comprehension requires an iterable list or string");
                for (auto& it : items) {
                    auto compEnv = gcMake<Env>(env);
                    compEnv->define(e->iterVar, it);
                    if (e->c && !truthy(eval(e->c, compEnv))) continue;
                    out->items.push_back(eval(e->a, compEnv));
                }
                return Value(out);
            }
            case EK::MapLit: {
                auto m = std::make_shared<MapObj>();
                for (auto& [k, vexpr] : e->mapEntries) {
                    if (k == nullptr) {
                        Value operand = eval(vexpr, env);
                        if (!operand.isMap())
                            throw NimbleError("Spread '...' in a map requires a map");
                        for (auto& entry : operand.asMap()->entries) {
                            m->set(entry.first, entry.second);
                        }
                    } else {
                        Value key = eval(k, env);
                        m->set(key.asStr(), eval(vexpr, env));
                    }
                }
                return Value(m);
            }
            case EK::FuncLit: {
                auto fn = gcMake<FunctionObj>();
                fn->isNative = false;
                fn->name = "<anon>";
                fn->params = e->params;
                fn->body = e->body;
                fn->closure = env;
                return Value(fn);
            }
            case EK::ObjectLit: {
                // NOTE (reference-cycle leak, see docs/memory-model.md): any `func` defined
                // inside this object literal's body gets fn->closure == objEnv (same
                // mechanism as SK::FuncDecl above), and objEnv->parent == env keeps the
                // enclosing scope alive too. An `object ... end` with at least one method,
                // built repeatedly (e.g. in a loop or a factory function), leaks one Env
                // per method plus its whole enclosing scope chain, forever. Prefer `class`
                // + instantiation for objects created repeatedly -- instances don't chain
                // to their declaring scope the way object literals do (see getMember's
                // isObj() branch, which also avoids caching bound methods for this reason).
                auto objEnv = gcMake<Env>(env);
                execBlock(e->body, objEnv);
                return Value(objEnv);
            }
            case EK::Slice: {
                Value obj = eval(e->a, env);
                long len;
                if (obj.isStr()) len = (long)obj.asStr().size();
                else if (obj.isList()) len = (long)obj.asList()->items.size();
                else throw NimbleError(std::string("Cannot slice type ") + typeName(obj));
                long start = e->b ? (long)toNumber(eval(e->b, env)) : 0;
                long stop = e->c ? (long)toNumber(eval(e->c, env)) : len;
                if (start < 0) start += len;
                if (stop < 0) stop += len;
                start = std::max((long)0, std::min(start, len));
                stop = std::max((long)0, std::min(stop, len));
                if (stop < start) stop = start;
                if (obj.isStr()) return Value(obj.asStr().substr(start, stop - start));
                auto out = std::make_shared<ListObj>();
                auto& items = obj.asList()->items;
                for (long i = start; i < stop; i++) out->items.push_back(items[i]);
                return Value(out);
            }
            case EK::Member: {
                Value obj = eval(e->a, env);
                return getMember(obj, e->name);
            }
            case EK::OptionalMember: {
                Value obj = eval(e->a, env);
                if (obj.isNull()) return Value();
                return getMember(obj, e->name);
            }
            case EK::Index: {
                Value obj = eval(e->a, env);
                Value idx = eval(e->b, env);
                return getIndex(obj, idx);
            }
            case EK::OptionalIndex: {
                Value obj = eval(e->a, env);
                if (obj.isNull()) return Value();
                Value idx = eval(e->b, env);
                return getIndex(obj, idx);
            }
            case EK::Call: return evalCall(e, env);
            case EK::Assign: return evalAssign(e, env);
            default: throw NimbleError("Unsupported expression");
        }
    }

    Value evalBinary(ExprPtr& e, const std::shared_ptr<Env>& env) {
        const std::string& op = e->op;
        if (op == "??") {
            Value l = eval(e->a, env);
            if (!l.isNull()) return l;
            return eval(e->b, env);
        }
        Value l = eval(e->a, env);
        Value r = eval(e->b, env);
        if (op == "==") return Value(structuralEquals(l, r));
        if (op == "!=") return Value(!structuralEquals(l, r));
        if (op == "===") return Value(identityEquals(l, r));
        if (op == "!==") return Value(!identityEquals(l, r));
        if (op == "+") {
            if (l.isStr() || r.isStr()) {
                if (!(l.isStr() && r.isStr())) {
                    return Value((l.isStr() ? l.asStr() : toDisplayString(l)) + (r.isStr() ? r.asStr() : toDisplayString(r)));
                }
                return Value(l.asStr() + r.asStr());
            }
            if (l.isList() && r.isList()) {
                auto out = std::make_shared<ListObj>();
                out->items = l.asList()->items;
                for (auto& it : r.asList()->items) out->items.push_back(it);
                return Value(out);
            }
            return Value(toNumber(l) + toNumber(r));
        }
        if (op == "-") return Value(toNumber(l) - toNumber(r));
        if (op == "*") return Value(toNumber(l) * toNumber(r));
        if (op == "/") {
            double rn = toNumber(r);
            if (rn == 0) throw NimbleError("Division by zero");
            return Value(toNumber(l) / rn);
        }
        if (op == "%") return Value(std::fmod(toNumber(l), toNumber(r)));
        if (op == "**") return Value(std::pow(toNumber(l), toNumber(r)));
        if (op == "<" || op == ">" || op == "<=" || op == ">=") {
            if (l.isStr() && r.isStr()) {
                int c = l.asStr().compare(r.asStr());
                if (op == "<") return Value(c < 0);
                if (op == ">") return Value(c > 0);
                if (op == "<=") return Value(c <= 0);
                return Value(c >= 0);
            }
            double ln = toNumber(l), rn = toNumber(r);
            if (op == "<") return Value(ln < rn);
            if (op == ">") return Value(ln > rn);
            if (op == "<=") return Value(ln <= rn);
            return Value(ln >= rn);
        }
        throw NimbleError("Unsupported binary operator: " + op);
    }

    Value evalAssign(ExprPtr& e, const std::shared_ptr<Env>& env) {
        Value newVal = eval(e->b, env);
        if (e->op != "=") {
            Value oldVal = evalTargetGet(e->a, env);
            if (e->op == "+=") newVal = evalBinaryValues("+", oldVal, newVal);
            else if (e->op == "-=") newVal = evalBinaryValues("-", oldVal, newVal);
            else if (e->op == "*=") newVal = evalBinaryValues("*", oldVal, newVal);
            else if (e->op == "/=") newVal = evalBinaryValues("/", oldVal, newVal);
            else if (e->op == "%=") newVal = evalBinaryValues("%", oldVal, newVal);
        }
        assignTo(e->a, newVal, env);
        return newVal;
    }

    Value evalBinaryValues(const std::string& op, Value l, Value r) {
        if (op == "+") {
            if (l.isStr() || r.isStr()) return Value((l.isStr()?l.asStr():toDisplayString(l)) + (r.isStr()?r.asStr():toDisplayString(r)));
            if (l.isList() && r.isList()) { auto out = std::make_shared<ListObj>(); out->items = l.asList()->items; for (auto&it: r.asList()->items) out->items.push_back(it); return Value(out); }
            return Value(toNumber(l) + toNumber(r));
        }
        if (op == "-") return Value(toNumber(l) - toNumber(r));
        if (op == "*") return Value(toNumber(l) * toNumber(r));
        if (op == "/") { double rn = toNumber(r); if (rn==0) throw NimbleError("Division by zero"); return Value(toNumber(l)/rn); }
        if (op == "%") return Value(std::fmod(toNumber(l), toNumber(r)));
        throw NimbleError("Unsupported operator in compound assignment: " + op);
    }

    Value evalTargetGet(ExprPtr& target, const std::shared_ptr<Env>& env) { return eval(target, env); }

    void assignTo(ExprPtr& target, const Value& val, const std::shared_ptr<Env>& env) {
        if (target->kind == EK::Var) { env->assign(target->name, val); return; }
        if (target->kind == EK::Member) {
            Value obj = eval(target->a, env);
            setMember(obj, target->name, val);
            return;
        }
        if (target->kind == EK::OptionalMember) {
            Value obj = eval(target->a, env);
            if (obj.isNull()) return;
            setMember(obj, target->name, val);
            return;
        }
        if (target->kind == EK::Index) {
            Value obj = eval(target->a, env);
            Value idx = eval(target->b, env);
            setIndex(obj, idx, val);
            return;
        }
        if (target->kind == EK::OptionalIndex) {
            Value obj = eval(target->a, env);
            if (obj.isNull()) return;
            Value idx = eval(target->b, env);
            setIndex(obj, idx, val);
            return;
        }
        if (target->kind == EK::ListLit) {
            if (!val.isList()) throw NimbleError("Cannot destructure: value is not a list");
            auto& items = val.asList()->items;
            for (size_t i = 0; i < target->list.size(); i++) {
                Value item = i < items.size() ? items[i] : Value();
                assignTo(target->list[i], item, env);
            }
            return;
        }
        if (target->kind == EK::MapLit) {
            for (auto& [keyExpr, targetExpr] : target->mapEntries) {
                if (keyExpr == nullptr) continue;
                const std::string& key = keyExpr->str;
                Value fieldVal;
                if (val.isMap()) fieldVal = val.asMap()->get(key);
                else if (val.isObj()) {
                    auto e = val.asObj();
                    auto it = e->vars.find(key);
                    fieldVal = (it != e->vars.end()) ? it->second : Value();
                } else {
                    throw NimbleError("Cannot destructure: value is neither a map nor an object");
                }
                assignTo(targetExpr, fieldVal, env);
            }
            return;
        }
        throw NimbleError("Invalid assignment target");
    }

    // ---------------- Miembros / índices ----------------
    Value getMember(Value& obj, const std::string& name) {
        if (obj.isList()) return listMember(obj, name);
        if (obj.isStr()) return stringMember(obj, name);
        if (obj.isMap()) {
            auto m = obj.asMap();
            if (m->has(name)) return m->get(name);
            return Value();
        }
        if (obj.isObj()) {
            auto e = obj.asObj();
            auto it = e->vars.find(name);
            if (it != e->vars.end()) return it->second;
            if (e->ownerClass) {
                auto fn = e->ownerClass->findMethod(name);
                if (fn) {
                    // Do NOT cache the bound method on the instance (e->methodCache used to
                    // do this). bindMethod's wrapper captures `self` (a Value wrapping this
                    // same instance Env) by value inside its std::function; storing that
                    // wrapper back onto the instance created a direct reference cycle
                    // (instance -> cached bound method -> captured self -> same instance)
                    // that leaked the instance forever, for every instance that ever had a
                    // method called on it. See docs/memory-model.md for the full audit.
                    // bindMethod() is a cheap allocation, so recomputing it on every access
                    // is the safe trade-off until the interpreter has a real GC.
                    return bindMethod(obj, fn);
                }
            }
            return Value();
        }
        if (obj.isClass()) {
            auto fn = obj.asClass()->findMethod(name);
            if (fn) return Value(fn);
            return Value();
        }
        throw NimbleError(std::string("Cannot access '.") + name + "' on type " + typeName(obj));
    }

    void setMember(Value& obj, const std::string& name, const Value& val) {
        if (obj.isMap()) { obj.asMap()->set(name, val); return; }
        if (obj.isObj()) { obj.asObj()->vars[name] = val; return; }
        throw NimbleError("Cannot assign '." + name + "' on type " + typeName(obj));
    }

    Value bindMethod(Value selfVal, std::shared_ptr<FunctionObj> fn) {
        auto wrapper = gcMake<FunctionObj>();
        wrapper->isNative = true;
        wrapper->name = fn->name;
        wrapper->native = [this, selfVal, fn](std::vector<Value>& args, std::vector<std::pair<std::string,Value>>& named, Interpreter&) {
            Value self = selfVal;
            return invokeUserFunction(fn, args, named, &self);
        };
        return Value(wrapper);
    }

    Value getIndex(Value& obj, Value& idx) {
        if (obj.isList()) {
            auto l = obj.asList();
            long i = (long)toNumber(idx);
            if (i < 0) i += (long)l->items.size();
            if (i < 0 || i >= (long)l->items.size()) throw NimbleError("List index out of range: " + numToStr(i));
            return l->items[i];
        }
        if (obj.isStr()) {
            long i = (long)toNumber(idx);
            const std::string& s = obj.asStr();
            if (i < 0) i += (long)s.size();
            if (i < 0 || i >= (long)s.size()) throw NimbleError("String index out of range");
            return Value(std::string(1, s[i]));
        }
        if (obj.isMap()) {
            if (!idx.isStr()) throw NimbleError("Map keys must be strings");
            return obj.asMap()->get(idx.asStr());
        }
        throw NimbleError(std::string("Cannot index type ") + typeName(obj));
    }

    void setIndex(Value& obj, Value& idx, const Value& val) {
        if (obj.isList()) {
            auto l = obj.asList();
            long i = (long)toNumber(idx);
            if (i < 0) i += (long)l->items.size();
            if (i < 0 || i >= (long)l->items.size()) throw NimbleError("List index out of range");
            l->items[i] = val;
            return;
        }
        if (obj.isMap()) {
            if (!idx.isStr()) throw NimbleError("Map keys must be strings");
            obj.asMap()->set(idx.asStr(), val);
            return;
        }
        throw NimbleError(std::string("Cannot assign by index on type ") + typeName(obj));
    }

    Value bindNative(const std::string& name, std::function<Value(std::vector<Value>&)> fn) {
        auto f = gcMake<FunctionObj>();
        f->isNative = true; f->name = name;
        f->native = [fn](std::vector<Value>& args, std::vector<std::pair<std::string,Value>>&, Interpreter&) { return fn(args); };
        return Value(f);
    }

    Value listMember(Value& obj, const std::string& name) {
        auto l = obj.asList();
        if (name == "length") return Value((double)l->items.size());
        if (name == "first") { if (l->items.empty()) return Value(); return l->items.front(); }
        if (name == "last") { if (l->items.empty()) return Value(); return l->items.back(); }
        if (name == "push") return bindNative("push", [l](std::vector<Value>& a){ for (auto& v: a) l->items.push_back(v); return Value(); });
        if (name == "pop") return bindNative("pop", [l](std::vector<Value>& a){ if (l->items.empty()) return Value(); Value v = l->items.back(); l->items.pop_back(); return v; });
        if (name == "sort") return bindNative("sort", [l](std::vector<Value>& a){
            std::sort(l->items.begin(), l->items.end(), [](const Value& x, const Value& y){
                if (x.isStr() && y.isStr()) return x.asStr() < y.asStr();
                return toNumber(x) < toNumber(y);
            });
            return Value();
        });
        if (name == "reverse") return bindNative("reverse", [l](std::vector<Value>& a){ std::reverse(l->items.begin(), l->items.end()); return Value(); });
        if (name == "index_of") return bindNative("index_of", [l](std::vector<Value>& a){
            const Value& target = reqArg(a, 0, "index_of");
            for (size_t i = 0; i < l->items.size(); i++) if (structuralEquals(l->items[i], target)) return Value((double)i);
            return Value(-1.0);
        });
        if (name == "join") return bindNative("join", [l](std::vector<Value>& a){
            std::string sep = a.empty() ? "" : reqStr(a, 0, "join");
            std::string out;
            for (size_t i=0;i<l->items.size();i++){ if(i) out+=sep; out += toDisplayString(l->items[i]); }
            return Value(out);
        });
        static const std::vector<std::string> listMemberNames = {
            "length", "first", "last", "push", "pop", "sort", "reverse", "index_of", "join"
        };
        throw NimbleError("List has no member '" + name + "'" + ErrHelp::suggest(name, listMemberNames));
    }

    Value stringMember(Value& obj, const std::string& name) {
        std::string s = obj.asStr();
        if (name == "length") return Value((double)s.size());
        if (name == "upper") return bindNative("upper", [s](std::vector<Value>&){ std::string r=s; for (auto& c: r) c = toupper((unsigned char)c); return Value(r); });
        if (name == "lower") return bindNative("lower", [s](std::vector<Value>&){ std::string r=s; for (auto& c: r) c = tolower((unsigned char)c); return Value(r); });
        if (name == "trim") return bindNative("trim", [s](std::vector<Value>&){
            std::string r = s;
            size_t a = r.find_first_not_of(" \t\n\r");
            size_t b = r.find_last_not_of(" \t\n\r");
            if (a == std::string::npos) return Value(std::string());
            return Value(r.substr(a, b - a + 1));
        });
        if (name == "find") return bindNative("find", [s](std::vector<Value>& a){
            const std::string& sub = reqStr(a, 0, "find");
            size_t start = a.size() > 1 ? (size_t)reqNum(a, 1, "find") : 0;
            size_t pos = s.find(sub, start);
            return Value(pos == std::string::npos ? -1.0 : (double)pos);
        });
        if (name == "contains") return bindNative("contains", [s](std::vector<Value>& a){ return Value(s.find(reqStr(a, 0, "contains")) != std::string::npos); });
        if (name == "starts_with") return bindNative("starts_with", [s](std::vector<Value>& a){
            const std::string& prefix = reqStr(a, 0, "starts_with");
            return Value(s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0);
        });
        if (name == "ends_with") return bindNative("ends_with", [s](std::vector<Value>& a){
            const std::string& suffix = reqStr(a, 0, "ends_with");
            return Value(s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0);
        });
        if (name == "replace") return bindNative("replace", [s](std::vector<Value>& a){
            std::string r = s; const std::string& from = reqStr(a, 0, "replace"); std::string to = a.size()>1?reqStr(a, 1, "replace"):"";
            if (from.empty()) return Value(r);
            size_t pos = 0;
            while ((pos = r.find(from, pos)) != std::string::npos) { r.replace(pos, from.size(), to); pos += to.size(); }
            return Value(r);
        });
        if (name == "split") return bindNative("split", [s](std::vector<Value>& a){
            std::string sep = a.empty() ? " " : reqStr(a, 0, "split");
            auto out = std::make_shared<ListObj>();
            if (sep.empty()) { for (char c : s) out->items.push_back(Value(std::string(1,c))); return Value(out); }
            size_t start = 0, pos;
            while ((pos = s.find(sep, start)) != std::string::npos) {
                out->items.push_back(Value(s.substr(start, pos - start)));
                start = pos + sep.size();
            }
            out->items.push_back(Value(s.substr(start)));
            return Value(out);
        });
        static const std::vector<std::string> stringMemberNames = {
            "length", "upper", "lower", "trim", "find", "contains",
            "starts_with", "ends_with", "replace", "split"
        };
        throw NimbleError("String has no member '" + name + "'" + ErrHelp::suggest(name, stringMemberNames));
    }

    // ---------------- Llamadas ----------------
    Value evalCall(ExprPtr& e, const std::shared_ptr<Env>& env) {
        Value callee = eval(e->a, env);
        // corte de optional chaining: si esta llamada viene después de un '?.'/'?[' y
        // el objetivo resultó null (ya sea porque la cadena se cortó antes, o porque
        // el valor legítimamente es null), toda la llamada se corta a null sin evaluar
        // argumentos ni intentar invocar nada.
        if (e->isOptionalCall && callee.isNull()) return Value();
        std::vector<Value> args;
        for (auto& a : e->list) {
            if (a->kind == EK::Spread) {
                Value spread = eval(a->a, env);
                if (!spread.isList())
                    throw NimbleError("'...' in a call requires a list to expand");
                for (auto& item : spread.asList()->items) args.push_back(item);
            } else {
                args.push_back(eval(a, env));
            }
        }
        std::vector<std::pair<std::string, Value>> named;
        for (auto& [nm, ex] : e->namedArgs) named.push_back({nm, eval(ex, env)});

        if (callee.isFunc()) {
            auto fn = callee.asFunc();
            if (fn->special == FunctionObj::Special::Assert) {
                if (args.empty() || !truthy(args[0])) {
                    std::string msg = args.size() > 1 ? toDisplayString(args[1]) : "assertion failed";
                    throw AbortSignal{msg, e->line};
                }
                return Value();
            }
            if (fn->special == FunctionObj::Special::Panic) {
                std::string msg = args.empty() ? "panic" : toDisplayString(args[0]);
                throw AbortSignal{msg, e->line};
            }
        }

        std::string frameName = "<anon>";
        if (e->a->kind == EK::Var) frameName = e->a->name;
        else if (e->a->kind == EK::Member || e->a->kind == EK::OptionalMember) frameName = e->a->name;

        bool pushFrame = callee.isFunc() && !callee.asFunc()->isNative;
        if (pushFrame) callStack.push_back({frameName, e->line});

        try {
            Value result;
            if (callee.isClass()) result = instantiate(callee.asClass(), args, named);
            else result = callFunction(callee, args, named);
            if (pushFrame) callStack.pop_back();
            return result;
        } catch (NimbleError& err) {
            if (err.stack.empty()) {
                for (auto& f : callStack) err.stack.push_back({f.name, f.callLine});
            }
            if (pushFrame) callStack.pop_back();
            throw;
        } catch (...) {
            if (pushFrame) callStack.pop_back();
            throw;
        }
    }

    Value instantiate(std::shared_ptr<ClassObj> cls, std::vector<Value>& args, std::vector<std::pair<std::string,Value>>& named) {
        auto instEnv = gcMake<Env>(nullptr);
        instEnv->ownerClass = cls;
        Value self(instEnv);
        auto initFn = cls->findMethod("init");
        if (initFn) invokeUserFunction(initFn, args, named, &self);
        return self;
    }

    Value callFunction(Value callee, std::vector<Value> args, std::vector<std::pair<std::string,Value>> named) {
        if (callee.isClass()) return instantiate(callee.asClass(), args, named);
        if (!callee.isFunc()) throw NimbleError(std::string("Attempted to call a value that is not a function (") + typeName(callee) + ")");
        auto fn = callee.asFunc();
        if (fn->isNative) return fn->native(args, named, *this);
        return invokeUserFunction(fn, args, named, nullptr);
    }

    Value invokeUserFunction(std::shared_ptr<FunctionObj> fn, std::vector<Value>& args, std::vector<std::pair<std::string,Value>>& named, Value* selfVal) {
        if (++callDepth > maxCallDepth) {
            callDepth--;
            throw NimbleError("Call stack too deep (possible infinite recursion, limit: " + std::to_string(maxCallDepth) + ")");
        }
        struct DepthGuard { Interpreter* self; ~DepthGuard() { self->callDepth--; } } depthGuard{this};
        auto fnEnv = gcMake<Env>(fn->closure);
        if (selfVal) fnEnv->define("self", *selfVal);
        size_t nPos = std::min(args.size(), fn->params.size());
        for (size_t i = 0; i < nPos; i++) fnEnv->define(fn->params[i].name, args[i]);
        for (auto& [nm, val] : named) fnEnv->define(nm, val);
        for (size_t i = 0; i < fn->params.size(); i++) {
            auto& pr = fn->params[i];
            if (!fnEnv->existsLocal(pr.name)) {
                if (pr.defaultVal) fnEnv->define(pr.name, eval(pr.defaultVal, fnEnv));
                else fnEnv->define(pr.name, Value());
            }
        }
        ExecResult r = execBlock(fn->body, fnEnv);
        if (r.flow == Flow::Break || r.flow == Flow::Continue) {
            // 'break'/'continue' that escaped every loop inside this function body.
            // (With the old exception-based implementation this could leak past the
            // call boundary and be silently caught by an unrelated loop at the
            // call site — treat it the same way a top-level break/continue is
            // treated: a script error, not a signal to keep propagating.)
            throw NimbleError(std::string("'") + (r.flow == Flow::Break ? "break" : "continue") + "' outside of a loop");
        }
        return r.value; // Flow::Return -> its value; Flow::Normal -> implicit null
    }

    // ---------------- Interpolación ----------------
    struct InterpSegment {
        bool isExpr = false;
        std::string text;
        ExprPtr ast;
    };

    std::string interpolate(const std::string& raw, const std::shared_ptr<Env>& env) {
        bool hasRealBrace = false;
        for (char c : raw) {
            if (c == '{') { hasRealBrace = true; break; }
        }
        if (!hasRealBrace) {
            std::string out;
            out.reserve(raw.size());
            for (char c : raw) {
                if (c == SENT_LBRACE) out += '{';
                else if (c == SENT_RBRACE) out += '}';
                else out += c;
            }
            return out;
        }

        static std::unordered_map<std::string, std::vector<InterpSegment>> cache;
        auto it = cache.find(raw);
        if (it == cache.end()) {
            std::vector<InterpSegment> segments;
            std::string literal;
            size_t i = 0;
            while (i < raw.size()) {
                char c = raw[i];
                if (c == SENT_LBRACE) { literal += '{'; i++; continue; }
                if (c == SENT_RBRACE) { literal += '}'; i++; continue; }
                if (c == '{') {
                    if (!literal.empty()) {
                        segments.push_back({false, literal, nullptr});
                        literal.clear();
                    }
                    int depth = 1;
                    size_t j = i + 1;
                    while (j < raw.size() && depth > 0) {
                        char cj = raw[j];
                        if (cj == SENT_LBRACE || cj == SENT_RBRACE) {
                            j++;
                        } else if (cj == '"') {
                            j++;
                            while (j < raw.size() && raw[j] != '"') {
                                if (raw[j] == '\\' && j + 1 < raw.size()) j++;
                                j++;
                            }
                            if (j < raw.size()) j++;
                        } else if (cj == '{') { depth++; j++; }
                        else if (cj == '}') { depth--; j++; }
                        else { j++; }
                    }
                    if (depth != 0) {
                        literal += raw.substr(i);
                        i = raw.size();
                        break;
                    }
                    std::string exprSrc = raw.substr(i + 1, j - i - 2);
                    ExprPtr ast = nullptr;
                    try {
                        Lexer lex(exprSrc);
                        auto toks = lex.tokenize();
                        Parser parser(toks);
                        auto stmts = parser.parseProgram();
                        if (!stmts.empty() && stmts[0]->kind == SK::ExprStmt) ast = stmts[0]->expr;
                    } catch (...) {}
                    // `text` is unused for expression segments when ast is non-null; reuse
                    // it to carry the raw source when parsing failed, so evaluation below
                    // can report a real, catchable error instead of silently inserting a
                    // placeholder into the string (which used to corrupt string values --
                    // e.g. `"{"a": 1}"` for hand-written JSON -- instead of failing loudly).
                    segments.push_back({true, exprSrc, ast});
                    i = j;
                } else {
                    literal += c;
                    i++;
                }
            }
            if (!literal.empty()) segments.push_back({false, literal, nullptr});
            it = cache.emplace(raw, std::move(segments)).first;
        }

        std::string out;
        for (auto& seg : it->second) {
            if (!seg.isExpr) { out += seg.text; continue; }
            if (!seg.ast) throw NimbleError("Invalid interpolation expression: '" + seg.text +
                                             "' -- if you meant a literal '{' or '}', escape it as \\{ or \\}");
            ExprPtr astCopy = seg.ast;
            out += toDisplayString(eval(astCopy, env));
        }
        return out;
    }

    void setupGlobals();

    const std::unordered_map<std::string, std::string>* embeddedSources = nullptr;

    static std::string dottedToPath(const std::string& dotted) {
        std::string out = dotted;
        for (auto& c : out) if (c == '.') c = '/';
        return out;
    }
    static std::string moduleDefaultName(const std::string& raw, bool isLiteral) {
        if (isLiteral) return nimblePathStr(nimblePath(raw).stem());
        size_t pos = raw.find_last_of('.');
        return pos == std::string::npos ? raw : raw.substr(pos + 1);
    }
    static std::string moduleKey(const std::string& scriptDir, const std::string& rawPath) {
        namespace fs = std::filesystem;
        fs::path p = nimblePath(rawPath);
        fs::path full = p.is_absolute() ? p : nimblePath(scriptDir) / p;
        return nimblePathGeneric(full.lexically_normal());
    }

    Value loadModule(const std::string& rawPath) {
        namespace fs = std::filesystem;
        std::string key = moduleKey(scriptDir, rawPath);
        if (permissions.confineToRoot) {
            fs::path root = fs::absolute(nimblePath(permissions.sandboxRoot)).lexically_normal();
            fs::path full = fs::absolute(nimblePath(key)).lexically_normal();
            std::string rootStr = nimblePathGeneric(root);
            std::string fullStr = nimblePathGeneric(full);
            bool inside = fullStr.size() >= rootStr.size() && fullStr.compare(0, rootStr.size(), rootStr) == 0 &&
                          (fullStr.size() == rootStr.size() || fullStr[rootStr.size()] == '/');
            if (!inside) throw NimbleError("use: module outside the sandbox ('" + rawPath + "')");
        }

        auto cacheIt = moduleCache.find(key);
        if (cacheIt != moduleCache.end()) return cacheIt->second;

        for (auto& s : importStack)
            if (s == key) throw NimbleError("Circular import detected while importing: " + key);

        std::string src;
        if (embeddedSources) {
            auto it = embeddedSources->find(key);
            if (it == embeddedSources->end())
                throw NimbleError("Module not bundled into the executable: " + key);
            src = it->second;
        } else {
            std::ifstream f(nimblePath(key), std::ios::binary);
            if (!f) throw NimbleError("Could not import module '" + rawPath + "' (not found: " + key + ")");
            std::ostringstream ss; ss << f.rdbuf();
            src = ss.str();
        }

        Lexer lex(src);
        auto toks = lex.tokenize();
        Parser parser(toks);
        auto stmts = parser.parseProgram();

        auto moduleEnv = gcMake<Env>(builtins);

        importStack.push_back(key);
        std::string savedDir = scriptDir;
        scriptDir = fs::path(key).parent_path().string();
        if (scriptDir.empty()) scriptDir = ".";
        try {
            execBlock(stmts, moduleEnv);
        } catch (...) {
            scriptDir = savedDir;
            importStack.pop_back();
            throw;
        }
        scriptDir = savedDir;
        importStack.pop_back();

        Value modVal(moduleEnv);
        moduleCache[key] = modVal;
        return modVal;
    }
};

// ============================================================
// Biblioteca estándar
// ============================================================
static Value makeNative(const std::string& name, NativeFn fn) {
    auto f = gcMake<FunctionObj>();
    f->isNative = true; f->name = name; f->native = fn;
    return Value(f);
}

// ============================================================
// net: sockets TCP/UDP crudos y bloqueantes, para mini-servidores de
// prueba o de juego (ver docs/net.md). Se cierran solos vía RAII cuando
// se destruye el último shared_ptr<NetSocket> que los referencia -- eso
// es refcounting normal, no involucra al recolector de ciclos (un
// socket no puede formar un ciclo, no es un Value que otro socket
// pueda contener). Gateado por permissions.allowNetwork, igual que
// http.get/post.
// ============================================================
#ifdef _WIN32
using socket_t = SOCKET;
static constexpr socket_t NIMBLE_INVALID_SOCKET = INVALID_SOCKET;
#else
using socket_t = int;
static constexpr socket_t NIMBLE_INVALID_SOCKET = -1;
#endif

static void nimbleNetInit() {
#ifdef _WIN32
    static bool started = false;
    if (!started) {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
        started = true; // sin WSACleanup a propósito: el proceso libera
                         // ese estado al salir, igual que con los sockets
                         // que un script nunca llegó a cerrar.
    }
#endif
}

static void nimbleCloseSocket(socket_t s) {
    if (s == NIMBLE_INVALID_SOCKET) return;
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

static std::string nimbleSocketErr() {
#ifdef _WIN32
    // Texto del propio sistema (en el idioma del usuario) + el codigo, en UTF-8.
    int code = WSAGetLastError();
    wchar_t* buf = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, (DWORD)code, 0, (LPWSTR)&buf, 0, nullptr);
    std::string msg;
    if (n > 0 && buf) {
        msg = nimbleWideToUtf8(std::wstring(buf, n));
        LocalFree(buf);
    }
    while (!msg.empty() && (msg.back() == '\r' || msg.back() == '\n' || msg.back() == ' ' || msg.back() == '.')) msg.pop_back();
    if (msg.empty()) return "código " + std::to_string(code);
    return msg + " (código " + std::to_string(code) + ")";
#else
    return std::string(strerror(errno));
#endif
}

static void nimbleSetTimeout(socket_t fd, double seconds) {
    if (seconds < 0) seconds = 0;
#ifdef _WIN32
    DWORD ms = (DWORD)(seconds * 1000.0);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&ms, sizeof(ms));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&ms, sizeof(ms));
#else
    struct timeval tv;
    tv.tv_sec = (long)seconds;
    tv.tv_usec = (long)((seconds - (double)tv.tv_sec) * 1000000.0);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
#endif
}

// RAII: todas las natives (send/recv/close/...) de un mismo handle
// capturan el mismo shared_ptr<NetSocket>, así que el fd se cierra solo
// cuando el script deja de tener cualquier referencia a ese handle
// (conexión, server o socket UDP), sin necesidad de .close() explícito.
struct NetSocket {
    socket_t fd = NIMBLE_INVALID_SOCKET;
    // Bytes ya leídos del socket real pero todavía no entregados al script:
    // recv_line() necesita poder leer "de más" (hasta encontrar un '\n') y
    // guardar el resto para la próxima llamada. recv() también consume de
    // acá primero, así intercalar recv()/recv_line() en la misma conexión
    // no pierde ni reordena bytes.
    std::string buf;
    ~NetSocket() { nimbleCloseSocket(fd); }
};

// conn: el objeto que devuelven net.tcp_connect() y server.accept().
static Value nimbleMakeConn(std::shared_ptr<NetSocket> sock, const std::string& remoteHost, int remotePort) {
    auto m = std::make_shared<MapObj>();
    m->set("fd", Value((double)sock->fd)); // uso interno de net.poll(); no documentado como API de "lectura" normal
    m->set("remote_host", Value(remoteHost));
    m->set("remote_port", Value((double)remotePort));
    m->set("send", makeNative("send", [sock](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) -> Value {
        if (sock->fd == NIMBLE_INVALID_SOCKET) throw NimbleError("conn.send(): el socket ya está cerrado");
        const std::string& data = reqStr(a, 0, "conn.send");
        size_t sent = 0;
        while (sent < data.size()) {
            long n = (long)::send(sock->fd, data.data() + sent, (int)(data.size() - sent), 0);
            if (n <= 0) throw NimbleError("conn.send(): " + nimbleSocketErr());
            sent += (size_t)n;
        }
        return Value((double)sent);
    }));
    m->set("send_nb", makeNative("send_nb", [sock](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) -> Value {
        // v0.16. Contraparte no bloqueante de conn.send(): UN solo intento,
        // nunca reintenta y nunca bloquea. Devuelve cuantos bytes acepto el
        // kernel esta vez -- puede ser parcial, o 0 si el socket todavia
        // esta lleno -- para que el script arme su propia cola de salida
        // por conexion y siga con la siguiente en vez de quedarse esperando
        // a esta (el problema real de conn.send(): su timeout es por cada
        // ::send() interno del while, no un limite total, asi que un peer
        // que drena de a poquito puede hacerlo tardar mucho mas que
        // set_timeout()).
        //
        // No deja el fd en modo no bloqueante (eso rompería el timeout de
        // conn.recv() en el mismo socket, que usa SO_RCVTIMEO). En cambio
        // pregunta con poll(..., 0ms) si hay lugar en el buffer de salida
        // antes de mandar, y hace como mucho un ::send() no bloqueante
        // (MSG_DONTWAIT; ver v0.17 más abajo). Se usa junto con
        // net.poll([{"sock": conn, "write": true}, ...]) para no tener que
        // pagar ese poll() de nuevo por cada intento cuando se manda a
        // muchas conexiones en el mismo ciclo.
        if (sock->fd == NIMBLE_INVALID_SOCKET) throw NimbleError("conn.send_nb(): el socket ya está cerrado");
        const std::string& data = reqStr(a, 0, "conn.send_nb");
        if (data.empty()) return Value((double)0);
#ifdef _WIN32
        WSAPOLLFD pfd{sock->fd, POLLOUT, 0};
        int rc = WSAPoll(&pfd, 1, 0);
#else
        pollfd pfd{sock->fd, POLLOUT, 0};
        int rc = ::poll(&pfd, 1, 0);
#endif
        if (rc <= 0 || !(pfd.revents & POLLOUT)) return Value((double)0);
        // v0.17: el poll() de arriba solo garantiza que hay ALGO de lugar,
        // no lugar para todo `data`. Con un ::send() bloqueante normal, si
        // data.size() era mayor que el espacio libre, el kernel esperaba
        // hasta que cupiera todo -- o sea, send_nb() bloqueaba (y con el, el
        // loop entero). Ahora este unico send es no bloqueante de verdad:
        // acepta lo que quepa (parcial) o falla con EAGAIN/EWOULDBLOCK, que
        // mas abajo se traduce a "0 bytes".
#ifdef _WIN32
        // Windows no tiene MSG_DONTWAIT: se pone el socket en modo no
        // bloqueante solo durante este send y se restaura enseguida (asi
        // conn.recv() sigue usando SO_RCVTIMEO como siempre).
        u_long nbOn = 1, nbOff = 0;
        ioctlsocket(sock->fd, FIONBIO, &nbOn);
        long n = (long)::send(sock->fd, data.data(), (int)data.size(), 0);
        int sendErr = (n < 0) ? WSAGetLastError() : 0;
        ioctlsocket(sock->fd, FIONBIO, &nbOff);
        if (n < 0) WSASetLastError(sendErr); // que nimbleSocketErr() vea el error del send, no el del ioctlsocket
#else
        long n = (long)::send(sock->fd, data.data(), data.size(), MSG_DONTWAIT);
#endif
        if (n < 0) {
            // Si justo se llenó el buffer entre el poll() y el send() (raro
            // pero posible), no es un error real del socket: el llamador
            // simplemente lo reintenta en el próximo ciclo. Cualquier otro
            // error (conexión cortada, etc.) sí se propaga como siempre.
#ifdef _WIN32
            if (WSAGetLastError() == WSAEWOULDBLOCK) return Value((double)0);
#else
            if (errno == EWOULDBLOCK || errno == EAGAIN) return Value((double)0);
#endif
            throw NimbleError("conn.send_nb(): " + nimbleSocketErr());
        }
        return Value((double)n);
    }));
    m->set("set_nodelay", makeNative("set_nodelay", [sock](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) -> Value {
        // v0.16. Desactiva el algoritmo de Nagle (TCP_NODELAY): sin esto el
        // kernel puede retener un envio chico un rato esperando juntarlo con
        // el siguiente en vez de mandarlo ya. Pensado para trafico
        // interactivo/chunked en general (chat, streaming, cualquier
        // protocolo que ya arma sus propios paquetes) -- no es algo
        // especifico de un tipo de script.
        if (sock->fd == NIMBLE_INVALID_SOCKET) throw NimbleError("conn.set_nodelay(): el socket ya está cerrado");
        int yes = a.empty() ? 1 : (truthy(reqArg(a, 0, "conn.set_nodelay")) ? 1 : 0);
#ifndef TCP_NODELAY
#define TCP_NODELAY 0x0001
#endif
        setsockopt(sock->fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&yes, sizeof(yes));
        return Value();
    }));
    m->set("recv", makeNative("recv", [sock](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) -> Value {
        if (sock->fd == NIMBLE_INVALID_SOCKET) throw NimbleError("conn.recv(): el socket ya está cerrado");
        int maxLen = a.empty() ? 4096 : (int)reqNum(a, 0, "conn.recv");
        if (maxLen <= 0) maxLen = 4096;
        // Primero se sirve lo que recv_line() ya haya leído del socket y
        // dejado pendiente (ver comentario en NetSocket::buf).
        if (!sock->buf.empty()) {
            size_t take = std::min((size_t)maxLen, sock->buf.size());
            std::string out = sock->buf.substr(0, take);
            sock->buf.erase(0, take);
            return Value(out);
        }
        std::vector<char> buf((size_t)maxLen);
        long n = (long)::recv(sock->fd, buf.data(), maxLen, 0);
        if (n < 0) throw NimbleError("conn.recv(): " + nimbleSocketErr());
        return Value(std::string(buf.data(), (size_t)n)); // "" (n==0) => el otro lado cerró la conexión
    }));
    m->set("recv_line", makeNative("recv_line", [sock](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) -> Value {
        if (sock->fd == NIMBLE_INVALID_SOCKET) throw NimbleError("conn.recv_line(): el socket ya está cerrado");
        // Límite de seguridad: sin esto, un peer que nunca manda '\n' haría
        // crecer sock->buf sin límite (mismo tipo de problema que llevó al
        // fix de json.decode en v0.9 -- entrada hostil/rota no debería
        // poder colgar ni tumbar el proceso).
        int maxLen = a.empty() ? 65536 : (int)reqNum(a, 0, "conn.recv_line");
        if (maxLen <= 0) maxLen = 65536;
        while (true) {
            size_t nl = sock->buf.find('\n');
            if (nl != std::string::npos) {
                std::string line = sock->buf.substr(0, nl);
                if (!line.empty() && line.back() == '\r') line.pop_back(); // acepta LF y CRLF
                sock->buf.erase(0, nl + 1);
                return Value(line);
            }
            if (sock->buf.size() >= (size_t)maxLen)
                throw NimbleError("conn.recv_line(): línea más larga que el límite (" +
                                   std::to_string(maxLen) + " bytes) sin encontrar '\\n'");
            char chunk[4096];
            long n = (long)::recv(sock->fd, chunk, sizeof(chunk), 0);
            if (n < 0) throw NimbleError("conn.recv_line(): " + nimbleSocketErr());
            if (n == 0) {
                // El otro lado cerró la conexión. Si quedó texto sin '\n'
                // final, se devuelve como última línea (igual que readline()
                // en la mayoría de los lenguajes); si no quedó nada, nil.
                if (sock->buf.empty()) return Value();
                std::string rest = sock->buf;
                sock->buf.clear();
                return Value(rest);
            }
            sock->buf.append(chunk, (size_t)n);
        }
    }));
    m->set("close", makeNative("close", [sock](std::vector<Value>&, std::vector<std::pair<std::string,Value>>&, Interpreter&) -> Value {
        nimbleCloseSocket(sock->fd);
        sock->fd = NIMBLE_INVALID_SOCKET;
        return Value();
    }));
    m->set("set_timeout", makeNative("set_timeout", [sock](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) -> Value {
        nimbleSetTimeout(sock->fd, reqNum(a, 0, "conn.set_timeout"));
        return Value();
    }));
    return Value(m);
}

// server: lo que devuelve net.tcp_listen().
static Value nimbleMakeServer(std::shared_ptr<NetSocket> sock, int boundPort) {
    auto m = std::make_shared<MapObj>();
    m->set("fd", Value((double)sock->fd)); // uso interno de net.poll()
    m->set("port", Value((double)boundPort));
    m->set("accept", makeNative("accept", [sock](std::vector<Value>&, std::vector<std::pair<std::string,Value>>&, Interpreter&) -> Value {
        if (sock->fd == NIMBLE_INVALID_SOCKET) throw NimbleError("server.accept(): el socket ya está cerrado");
        sockaddr_in clientAddr{};
        socklen_t addrLen = sizeof(clientAddr);
        socket_t clientFd = ::accept(sock->fd, (sockaddr*)&clientAddr, &addrLen);
        if (clientFd == NIMBLE_INVALID_SOCKET) throw NimbleError("server.accept(): " + nimbleSocketErr());
        char ipStr[INET6_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
        auto clientSock = std::make_shared<NetSocket>();
        clientSock->fd = clientFd;
        return nimbleMakeConn(clientSock, std::string(ipStr), (int)ntohs(clientAddr.sin_port));
    }));
    m->set("close", makeNative("close", [sock](std::vector<Value>&, std::vector<std::pair<std::string,Value>>&, Interpreter&) -> Value {
        nimbleCloseSocket(sock->fd);
        sock->fd = NIMBLE_INVALID_SOCKET;
        return Value();
    }));
    m->set("set_timeout", makeNative("set_timeout", [sock](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) -> Value {
        nimbleSetTimeout(sock->fd, reqNum(a, 0, "server.set_timeout"));
        return Value();
    }));
    return Value(m);
}

// ---- lectura de archivos linea a linea (v0.20) ----
// Estado de un lector de lineas perezoso (open_lines). Lee el archivo por
// trozos y entrega una linea por llamada, asi que el uso de memoria es
// constante (un trozo + la linea en curso) sin importar el tamano del
// archivo. Misma semantica que conn.recv_line(): acepta LF y CRLF, la ultima
// linea se entrega aunque no termine en salto de linea, y al final devuelve
// null.
#ifndef NIMBLE_LINE_CHUNK
#define NIMBLE_LINE_CHUNK 65536   // solo se redefine en las pruebas (bordes de trozo)
#endif
struct LineReader {
    std::ifstream f;
    std::string buf;       // bytes ya leidos del archivo y aun no entregados
    size_t pos = 0;        // inicio de lo no entregado dentro de buf
    size_t scanned = 0;    // bytes de buf[pos..] ya revisados sin hallar '\n'
                           // (evita re-escanear una linea muy larga en cada trozo)
    bool eof = false;
    bool closed = false;
};
static inline void nimbleStripCR(std::string& s) { if (!s.empty() && s.back() == '\r') s.pop_back(); }

// Siguiente linea en `out`; false si no quedan mas.
static bool nimbleLineReaderNext(LineReader& r, std::string& out) {
    while (true) {
        size_t nl = r.buf.find('\n', r.pos + r.scanned);
        if (nl != std::string::npos) {
            out.assign(r.buf, r.pos, nl - r.pos);
            r.pos = nl + 1; r.scanned = 0;
            nimbleStripCR(out);
            return true;
        }
        r.scanned = r.buf.size() - r.pos;         // todo lo que hay ya se reviso
        if (r.eof) {
            if (r.pos >= r.buf.size()) return false;
            out.assign(r.buf, r.pos, std::string::npos);   // ultima linea sin '\n'
            r.buf.clear(); r.pos = 0; r.scanned = 0;
            nimbleStripCR(out);
            return true;
        }
        if (r.pos > 0) { r.buf.erase(0, r.pos); r.pos = 0; }   // compactar antes de rellenar
        static thread_local std::string chunk(NIMBLE_LINE_CHUNK, '\0');
        r.f.read(&chunk[0], (std::streamsize)chunk.size());
        std::streamsize n = r.f.gcount();
        if (n <= 0) { r.eof = true; r.f.close(); }   // liberar el descriptor apenas se termina
        else r.buf.append(chunk.data(), (size_t)n);
    }
}

static Value nimbleMakeLineReader(std::shared_ptr<LineReader> r) {
    auto m = std::make_shared<MapObj>();
    auto nextFn = makeNative("read_line", [r](std::vector<Value>&, std::vector<std::pair<std::string,Value>>&, Interpreter&) -> Value {
        if (r->closed) throw NimbleError("read_line(): el archivo ya está cerrado");
        std::string line;
        if (!nimbleLineReaderNext(*r, line)) return Value();   // null = fin del archivo
        return Value(line);
    });
    m->set("read_line", nextFn);
    // Protocolo de iteracion de `for`: un mapa con una funcion "__next__" se
    // recorre llamandola hasta que devuelva null. (Nombre con guiones bajos
    // para no chocar con campos de usuario como el clasico `node.next`.)
    m->set("__next__", nextFn);
    m->set("close", makeNative("close", [r](std::vector<Value>&, std::vector<std::pair<std::string,Value>>&, Interpreter&) -> Value {
        if (!r->closed) {
            r->closed = true;
            if (r->f.is_open()) r->f.close();
            r->buf.clear(); r->buf.shrink_to_fit();
        }
        return Value();
    }));
    return Value(m);
}

void Interpreter::setupGlobals() {
    auto& g = builtins;

    g->define("print", makeNative("print", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        for (size_t i = 0; i < a.size(); i++) { if (i) std::cout << " "; std::cout << toDisplayString(a[i]); }
        std::cout << "\n";
        return Value();
    }));

    g->define("print_err", makeNative("print_err", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        for (size_t i = 0; i < a.size(); i++) { if (i) std::cerr << " "; std::cerr << toDisplayString(a[i]); }
        std::cerr << "\n";
        return Value();
    }));

    g->define("exit", makeNative("exit", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter& interp) -> Value {
        if (!interp.permissions.allowExit) throw NimbleError("exit(): disabled by the host (sandbox)");
        int code = a.empty() ? 0 : (int)toNumber(a[0]);
        throw ExitSignal{code};
    }));

    g->define("input", makeNative("input", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        if (!a.empty()) std::cout << toDisplayString(a[0]);
        std::string line;
        if (!std::getline(std::cin, line)) line = "";
        return Value(line);
    }));

    g->define("length", makeNative("length", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        const Value& v = reqArg(a, 0, "length");
        if (v.isStr()) return Value((double)v.asStr().size());
        if (v.isList()) return Value((double)v.asList()->items.size());
        if (v.isMap()) return Value((double)v.asMap()->entries.size());
        throw NimbleError("length() is not supported for this type");
    }));

    g->define("keys", makeNative("keys", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        auto m = reqMap(a, 0, "keys");
        auto out = std::make_shared<ListObj>();
        for (auto& e : m->entries) out->items.push_back(Value(e.first));
        return Value(out);
    }));
    g->define("remove", makeNative("remove", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        // remove(map, key) -- deletes a key from a map, if present. Returns
        // true if the key existed and was removed, false otherwise. There
        // was previously no way at all to remove a key from a map.
        auto m = reqMap(a, 0, "remove");
        std::string key = reqStr(a, 1, "remove");
        return Value(m->remove(key));
    }));
    g->define("values", makeNative("values", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        auto m = reqMap(a, 0, "values");
        auto out = std::make_shared<ListObj>();
        for (auto& e : m->entries) out->items.push_back(e.second);
        return Value(out);
    }));

    g->define("type", makeNative("type", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        return Value(std::string(typeName(reqArg(a, 0, "type"))));
    }));

    g->define("min", makeNative("min", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        if (a.size() == 1 && a[0].isList()) {
            auto items = reqList(a, 0, "min");
            reqNonEmptyList(items, "min");
            double m = toNumber(items->items[0]);
            for (auto& it : items->items) m = std::min(m, toNumber(it));
            return Value(m);
        }
        double m = reqNum(a, 0, "min");
        for (auto& v : a) m = std::min(m, toNumber(v));
        return Value(m);
    }));
    g->define("max", makeNative("max", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        if (a.size() == 1 && a[0].isList()) {
            auto items = reqList(a, 0, "max");
            reqNonEmptyList(items, "max");
            double m = toNumber(items->items[0]);
            for (auto& it : items->items) m = std::max(m, toNumber(it));
            return Value(m);
        }
        double m = reqNum(a, 0, "max");
        for (auto& v : a) m = std::max(m, toNumber(v));
        return Value(m);
    }));
    g->define("abs", makeNative("abs", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) { return Value(std::fabs(reqNum(a, 0, "abs"))); }));
    g->define("round", makeNative("round", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) { return Value(std::round(reqNum(a, 0, "round"))); }));
    g->define("floor", makeNative("floor", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) { return Value(std::floor(reqNum(a, 0, "floor"))); }));
    g->define("ceil", makeNative("ceil", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) { return Value(std::ceil(reqNum(a, 0, "ceil"))); }));

    g->define("number", makeNative("number", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) { return Value(reqNum(a, 0, "number")); }));
    g->define("string", makeNative("string", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) { return Value(toDisplayString(reqArg(a, 0, "string"))); }));
    g->define("boolean", makeNative("boolean", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) { return Value(truthy(reqArg(a, 0, "boolean"))); }));

    g->define("ord", makeNative("ord", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        const std::string& s = reqStr(a, 0, "ord");
        if (s.empty()) throw NimbleError("ord() requires a string of at least 1 character");
        return Value((double)(unsigned char)s[0]);
    }));
    g->define("chr", makeNative("chr", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        long code = (long)reqNum(a, 0, "chr");
        if (code < 0 || code > 255) throw NimbleError("chr() expects a value between 0 and 255");
        return Value(std::string(1, (char)(unsigned char)code));
    }));

    g->define("range", makeNative("range", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        double start = 0, stop = 0, step = 1;
        if (a.size() == 1) { stop = reqNum(a, 0, "range"); }
        else if (a.size() == 2) { start = reqNum(a, 0, "range"); stop = reqNum(a, 1, "range"); }
        else if (a.size() >= 3) { start = reqNum(a, 0, "range"); stop = reqNum(a, 1, "range"); step = reqNum(a, 2, "range"); }
        if (step == 0) throw NimbleError("range() does not allow a step of 0");
        auto out = std::make_shared<ListObj>();
        if (step > 0) for (double x = start; x < stop; x += step) out->items.push_back(Value(x));
        else for (double x = start; x > stop; x += step) out->items.push_back(Value(x));
        return Value(out);
    }));

    g->define("map", makeNative("map", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter& interp) {
        auto out = std::make_shared<ListObj>();
        for (auto& it : reqList(a, 0, "map")->items) {
            std::vector<Value> callArgs{it};
            out->items.push_back(interp.callFunction(reqArg(a, 1, "map"), callArgs, {}));
        }
        return Value(out);
    }));
    g->define("filter", makeNative("filter", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter& interp) {
        auto out = std::make_shared<ListObj>();
        for (auto& it : reqList(a, 0, "filter")->items) {
            std::vector<Value> callArgs{it};
            if (truthy(interp.callFunction(reqArg(a, 1, "filter"), callArgs, {}))) out->items.push_back(it);
        }
        return Value(out);
    }));
    g->define("reduce", makeNative("reduce", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter& interp) {
        auto& items = reqList(a, 0, "reduce")->items;
        Value acc;
        size_t startIdx = 0;
        if (a.size() >= 3) acc = a[2];
        else { if (items.empty()) throw NimbleError("reduce() on an empty list requires an initial value"); acc = items[0]; startIdx = 1; }
        Value fn = reqArg(a, 1, "reduce");
        for (size_t i = startIdx; i < items.size(); i++) {
            std::vector<Value> callArgs{acc, items[i]};
            acc = interp.callFunction(fn, callArgs, {});
        }
        return acc;
    }));

    // ---- agregaciones ----
    g->define("sort_by", makeNative("sort_by", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter& interp) {
        auto out = std::make_shared<ListObj>();
        out->items = reqList(a, 0, "sort_by")->items;
        Value fn = reqArg(a, 1, "sort_by");
        std::stable_sort(out->items.begin(), out->items.end(), [&](const Value& x, const Value& y) {
            std::vector<Value> ax{x}, ay{y};
            Value kx = interp.callFunction(fn, ax, {});
            Value ky = interp.callFunction(fn, ay, {});
            if (kx.isStr() && ky.isStr()) return kx.asStr() < ky.asStr();
            return toNumber(kx) < toNumber(ky);
        });
        return Value(out);
    }));
    g->define("group_by", makeNative("group_by", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter& interp) {
        auto m = std::make_shared<MapObj>();
        Value fn = reqArg(a, 1, "group_by");
        for (auto& item : reqList(a, 0, "group_by")->items) {
            std::vector<Value> args{item};
            Value k = interp.callFunction(fn, args, {});
            std::string key = k.isStr() ? k.asStr() : toDisplayString(k);
            Value existing = m->get(key);
            if (existing.isList()) {
                existing.asList()->items.push_back(item);
            } else {
                auto lst = std::make_shared<ListObj>();
                lst->items.push_back(item);
                m->set(key, Value(lst));
            }
        }
        return Value(m);
    }));
    g->define("zip", makeNative("zip", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        auto out = std::make_shared<ListObj>();
        auto& xs = reqList(a, 0, "zip")->items;
        auto& ys = reqList(a, 1, "zip")->items;
        size_t n = std::min(xs.size(), ys.size());
        for (size_t i = 0; i < n; i++) {
            auto pair = std::make_shared<ListObj>();
            pair->items.push_back(xs[i]);
            pair->items.push_back(ys[i]);
            out->items.push_back(Value(pair));
        }
        return Value(out);
    }));
    g->define("enumerate", makeNative("enumerate", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        auto out = std::make_shared<ListObj>();
        auto& xs = reqList(a, 0, "enumerate")->items;
        for (size_t i = 0; i < xs.size(); i++) {
            auto pair = std::make_shared<ListObj>();
            pair->items.push_back(Value((double)i));
            pair->items.push_back(xs[i]);
            out->items.push_back(Value(pair));
        }
        return Value(out);
    }));
    g->define("sum", makeNative("sum", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        double total = 0;
        for (auto& it : reqList(a, 0, "sum")->items) total += toNumber(it);
        return Value(total);
    }));
    g->define("any", makeNative("any", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter& interp) {
        for (auto& it : reqList(a, 0, "any")->items) {
            bool ok;
            if (a.size() > 1) { std::vector<Value> args{it}; ok = truthy(interp.callFunction(a[1], args, {})); }
            else ok = truthy(it);
            if (ok) return Value(true);
        }
        return Value(false);
    }));
    g->define("all", makeNative("all", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter& interp) {
        for (auto& it : reqList(a, 0, "all")->items) {
            bool ok;
            if (a.size() > 1) { std::vector<Value> args{it}; ok = truthy(interp.callFunction(a[1], args, {})); }
            else ok = truthy(it);
            if (!ok) return Value(false);
        }
        return Value(true);
    }));

    // ---- assert / panic ----
    {
        auto assertFn = gcMake<FunctionObj>();
        assertFn->isNative = true;
        assertFn->name = "assert";
        assertFn->special = FunctionObj::Special::Assert;
        assertFn->native = [](std::vector<Value>&, std::vector<std::pair<std::string,Value>>&, Interpreter&) { return Value(); };
        g->define("assert", Value(assertFn));

        auto panicFn = gcMake<FunctionObj>();
        panicFn->isNative = true;
        panicFn->name = "panic";
        panicFn->special = FunctionObj::Special::Panic;
        panicFn->native = [](std::vector<Value>&, std::vector<std::pair<std::string,Value>>&, Interpreter&) { return Value(); };
        g->define("panic", Value(panicFn));
    }

    // ---- aserciones para tests ----
    g->define("assert_eq", makeNative("assert_eq", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        const Value& x = reqArg(a, 0, "assert_eq"); const Value& y = reqArg(a, 1, "assert_eq");
        if (!structuralEquals(x, y)) {
            std::string msg = a.size() > 2 ? toDisplayString(a[2])
                                           : "assert_eq: " + toDisplayString(x) + " != " + toDisplayString(y);
            throw NimbleError(msg);
        }
        return Value();
    }));
    g->define("assert_ne", makeNative("assert_ne", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        const Value& x = reqArg(a, 0, "assert_ne"); const Value& y = reqArg(a, 1, "assert_ne");
        if (structuralEquals(x, y)) {
            std::string msg = a.size() > 2 ? toDisplayString(a[2])
                                           : "assert_ne: both are " + toDisplayString(x);
            throw NimbleError(msg);
        }
        return Value();
    }));
    g->define("assert_true", makeNative("assert_true", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        const Value& x = reqArg(a, 0, "assert_true");
        if (!truthy(x)) {
            std::string msg = a.size() > 1 ? toDisplayString(a[1]) : "assert_true: " + toDisplayString(x) + " is not truthy";
            throw NimbleError(msg);
        }
        return Value();
    }));
    g->define("assert_false", makeNative("assert_false", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        const Value& x = reqArg(a, 0, "assert_false");
        if (truthy(x)) {
            std::string msg = a.size() > 1 ? toDisplayString(a[1]) : "assert_false: " + toDisplayString(x) + " is truthy";
            throw NimbleError(msg);
        }
        return Value();
    }));

    // ---- archivos ----
    g->define("read", makeNative("read", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter& interp) {
        if (!interp.permissions.allowFileRead) throw NimbleError("read(): disabled by the host (sandbox)");
        const std::string& raw = reqStr(a, 0, "read");
        std::string path = interp.confinePath(raw, "read");
        std::ifstream f(nimblePath(path), std::ios::binary);
        if (!f) throw NimbleError("Could not read file: " + raw);
        std::ostringstream ss; ss << f.rdbuf();
        return Value(ss.str());
    }));
    // read_lines(ruta) -> lista con las lineas del archivo, sin terminadores
    // (LF o CRLF). Sin elemento vacio final si el archivo termina en salto de
    // linea; archivo vacio -> lista vacia.
    g->define("read_lines", makeNative("read_lines", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter& interp) {
        if (!interp.permissions.allowFileRead) throw NimbleError("read_lines(): disabled by the host (sandbox)");
        const std::string& raw = reqStr(a, 0, "read_lines");
        std::string path = interp.confinePath(raw, "read_lines");
        std::ifstream f(nimblePath(path), std::ios::binary);
        if (!f) throw NimbleError("Could not read file: " + raw);
        std::string text;
        f.seekg(0, std::ios::end);
        std::streamoff sz = f.tellg();
        if (sz >= 0) {                       // archivo normal: una sola lectura, sin copias extra
            f.seekg(0, std::ios::beg);
            text.resize((size_t)sz);
            f.read(&text[0], (std::streamsize)sz);
            text.resize((size_t)f.gcount());
        } else {                             // pipes, /proc, etc.: sin tamano conocido
            f.clear(); f.seekg(0, std::ios::beg);
            std::ostringstream ss; ss << f.rdbuf();
            text = ss.str();
        }
        auto out = std::make_shared<ListObj>();
        size_t start = 0, n = text.size();
        while (start < n) {
            size_t nl = text.find('\n', start);
            size_t end = (nl == std::string::npos) ? n : nl;
            size_t e2 = end;
            if (e2 > start && text[e2 - 1] == '\r') e2--;
            out->items.push_back(Value(text.substr(start, e2 - start)));
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
        return Value(out);
    }));
    // open_lines(ruta) -> lector perezoso: `for linea in open_lines(ruta)` o
    // r.read_line() (null al final). Memoria constante; para archivos grandes.
    g->define("open_lines", makeNative("open_lines", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter& interp) {
        if (!interp.permissions.allowFileRead) throw NimbleError("open_lines(): disabled by the host (sandbox)");
        const std::string& raw = reqStr(a, 0, "open_lines");
        std::string path = interp.confinePath(raw, "open_lines");
        auto r = std::make_shared<LineReader>();
        r->f.open(nimblePath(path), std::ios::binary);
        if (!r->f) throw NimbleError("Could not read file: " + raw);
        return nimbleMakeLineReader(r);
    }));
    g->define("file_size", makeNative("file_size", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter& interp) {
        if (!interp.permissions.allowFileRead) throw NimbleError("file_size(): disabled by the host (sandbox)");
        const std::string& raw = reqStr(a, 0, "file_size");
        std::string path = interp.confinePath(raw, "file_size");
        std::error_code ec;
        auto sz = std::filesystem::file_size(nimblePath(path), ec);
        if (ec) throw NimbleError("file_size(): " + raw + ": " + ec.message());
        return Value((double)sz);
    }));
    g->define("read_range", makeNative("read_range", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter& interp) {
        // Lee como maximo `length` bytes empezando en `offset`, sin cargar
        // el resto del archivo. Pensado para servir video/audio con Range
        // requests y para leer archivos grandes en pedazos (ej. las
        // ultimas N lineas de un log sin traerlo entero a memoria).
        if (!interp.permissions.allowFileRead) throw NimbleError("read_range(): disabled by the host (sandbox)");
        const std::string& raw = reqStr(a, 0, "read_range");
        std::string path = interp.confinePath(raw, "read_range");
        long offset = (long)reqNum(a, 1, "read_range");
        long length = (long)reqNum(a, 2, "read_range");
        if (offset < 0 || length < 0) throw NimbleError("read_range(): offset/length must not be negative");
        std::ifstream f(nimblePath(path), std::ios::binary);
        if (!f) throw NimbleError("Could not read file: " + raw);
        f.seekg(offset, std::ios::beg);
        std::vector<char> buf((size_t)length);
        f.read(buf.data(), length);
        // Al final del archivo f.read() deja menos bytes de los pedidos --
        // no es un error, gcount() dice cuantos se leyeron de verdad.
        return Value(std::string(buf.data(), (size_t)f.gcount()));
    }));
    g->define("write", makeNative("write", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter& interp) {
        if (!interp.permissions.allowFileWrite) throw NimbleError("write(): disabled by the host (sandbox)");
        const std::string& raw = reqStr(a, 0, "write");
        std::string path = interp.confinePath(raw, "write");
        std::ofstream f(nimblePath(path), std::ios::binary);
        if (!f) throw NimbleError("Could not write file: " + raw);
        f << reqStr(a, 1, "write");
        return Value();
    }));
    g->define("append", makeNative("append", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter& interp) {
        if (!interp.permissions.allowFileWrite) throw NimbleError("append(): disabled by the host (sandbox)");
        const std::string& raw = reqStr(a, 0, "append");
        std::string path = interp.confinePath(raw, "append");
        std::ofstream f(nimblePath(path), std::ios::binary | std::ios::app);
        if (!f) throw NimbleError("Could not open file: " + raw);
        f << reqStr(a, 1, "append");
        return Value();
    }));
    g->define("exists", makeNative("exists", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter& interp) {
        if (!interp.permissions.allowFileRead) throw NimbleError("exists(): disabled by the host (sandbox)");
        std::string path = interp.confinePath(reqStr(a, 0, "exists"), "exists");
#ifdef _WIN32
        std::error_code ec;
        return Value(std::filesystem::exists(nimblePath(path), ec) && !ec);
#else
        struct stat st;
        return Value(stat(path.c_str(), &st) == 0);
#endif
    }));
    g->define("is_dir", makeNative("is_dir", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter& interp) {
        if (!interp.permissions.allowFileRead) throw NimbleError("is_dir(): disabled by the host (sandbox)");
        std::string path = interp.confinePath(reqStr(a, 0, "is_dir"), "is_dir");
        std::error_code ec;
        return Value(std::filesystem::is_directory(nimblePath(path), ec) && !ec);
    }));
    g->define("is_file", makeNative("is_file", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter& interp) {
        if (!interp.permissions.allowFileRead) throw NimbleError("is_file(): disabled by the host (sandbox)");
        std::string path = interp.confinePath(reqStr(a, 0, "is_file"), "is_file");
        std::error_code ec;
        return Value(std::filesystem::is_regular_file(nimblePath(path), ec) && !ec);
    }));
    g->define("delete", makeNative("delete", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter& interp) {
        if (!interp.permissions.allowFileSystemOps) throw NimbleError("delete(): disabled by the host (sandbox)");
        std::string path = interp.confinePath(reqStr(a, 0, "delete"), "delete");
#ifdef _WIN32
        return Value(_wremove(nimbleUtf8ToWide(path).c_str()) == 0);
#else
        return Value(std::remove(path.c_str()) == 0);
#endif
    }));
    g->define("listdir", makeNative("listdir", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter& interp) {
        if (!interp.permissions.allowFileSystemOps) throw NimbleError("listdir(): disabled by the host (sandbox)");
        const std::string& raw = reqStr(a, 0, "listdir");
        std::string path = interp.confinePath(raw, "listdir");
        auto out = std::make_shared<ListObj>();
        std::error_code ec;
        std::filesystem::directory_iterator it(nimblePath(path), ec);
        if (ec) throw NimbleError("Could not open directory: " + raw);
        for (const auto& entry : it) {
            out->items.push_back(Value(nimblePathStr(entry.path().filename())));
        }
        return Value(out);
    }));
    g->define("mkdir", makeNative("mkdir", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter& interp) {
        if (!interp.permissions.allowFileSystemOps) throw NimbleError("mkdir(): disabled by the host (sandbox)");
        std::string path = interp.confinePath(reqStr(a, 0, "mkdir"), "mkdir");
#ifndef _WIN32
        return Value(mkdir(path.c_str(), 0755) == 0);
#else
        return Value(_wmkdir(nimbleUtf8ToWide(path).c_str()) == 0);
#endif
    }));
    g->define("rmdir", makeNative("rmdir", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter& interp) {
        if (!interp.permissions.allowFileSystemOps) throw NimbleError("rmdir(): disabled by the host (sandbox)");
        std::string path = interp.confinePath(reqStr(a, 0, "rmdir"), "rmdir");
#ifdef _WIN32
        return Value(_wrmdir(nimbleUtf8ToWide(path).c_str()) == 0);
#else
        return Value(::rmdir(path.c_str()) == 0);
#endif
    }));

    // ---- math ----
    auto math = std::make_shared<MapObj>();
    math->set("sqrt", makeNative("sqrt", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) { return Value(std::sqrt(reqNum(a, 0, "math.sqrt"))); }));
    math->set("sin", makeNative("sin", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) { return Value(std::sin(reqNum(a, 0, "math.sin"))); }));
    math->set("cos", makeNative("cos", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) { return Value(std::cos(reqNum(a, 0, "math.cos"))); }));
    math->set("tan", makeNative("tan", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) { return Value(std::tan(reqNum(a, 0, "math.tan"))); }));
    math->set("abs", makeNative("abs", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) { return Value(std::fabs(reqNum(a, 0, "math.abs"))); }));
    math->set("floor", makeNative("floor", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) { return Value(std::floor(reqNum(a, 0, "math.floor"))); }));
    math->set("ceil", makeNative("ceil", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) { return Value(std::ceil(reqNum(a, 0, "math.ceil"))); }));
    math->set("pow", makeNative("pow", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) { return Value(std::pow(reqNum(a, 0, "math.pow"), reqNum(a, 1, "math.pow"))); }));
    // Rounds to a given number of decimal places (default 0, same behavior as
    // the global round() in that case). Kept separate from the global round()
    // rather than adding an optional second argument there, since round() is
    // used heavily as a single-arg function and this keeps that call site sturdy.
    math->set("round", makeNative("round", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        double x = reqNum(a, 0, "math.round");
        double decimals = a.size() > 1 ? reqNum(a, 1, "math.round") : 0;
        double factor = std::pow(10.0, decimals);
        return Value(std::round(x * factor) / factor);
    }));
    math->set("pi", Value(3.14159265358979323846));
    g->define("math", Value(math));

    // ---- random ----
    auto randomMod = std::make_shared<MapObj>();
    randomMod->set("int", makeNative("int", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        static std::mt19937 rng(std::random_device{}());
        long lo = (long)reqNum(a, 0, "random.int"), hi = (long)reqNum(a, 1, "random.int");
        std::uniform_int_distribution<long> dist(lo, hi);
        return Value((double)dist(rng));
    }));
    randomMod->set("float", makeNative("float", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        static std::mt19937 rng(std::random_device{}());
        double lo = a.size() > 0 ? reqNum(a, 0, "random.float") : 0.0, hi = a.size() > 1 ? reqNum(a, 1, "random.float") : 1.0;
        std::uniform_real_distribution<double> dist(lo, hi);
        return Value(dist(rng));
    }));
    randomMod->set("choice", makeNative("choice", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        static std::mt19937 rng(std::random_device{}());
        auto& items = reqList(a, 0, "random.choice")->items;
        if (items.empty()) return Value();
        std::uniform_int_distribution<size_t> dist(0, items.size() - 1);
        return items[dist(rng)];
    }));
    g->define("random", Value(randomMod));

    // ---- time ----
    auto timeMod = std::make_shared<MapObj>();
    timeMod->set("now", makeNative("now", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        auto t = std::chrono::system_clock::now().time_since_epoch();
        double secs = std::chrono::duration<double>(t).count();
        return Value(secs);
    }));
    timeMod->set("sleep", makeNative("sleep", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        std::this_thread::sleep_for(std::chrono::milliseconds((long)(reqNum(a, 0, "time.sleep") * 1000)));
        return Value();
    }));
    timeMod->set("today", makeNative("today", [](std::vector<Value>&, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        std::time_t t = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char buf[16]; std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
        return Value(std::string(buf));
    }));
    timeMod->set("format", makeNative("format", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        std::time_t t = (std::time_t)reqNum(a, 0, "time.format");
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char buf[256]; std::strftime(buf, sizeof(buf), reqStr(a, 1, "time.format").c_str(), &tm);
        return Value(std::string(buf));
    }));
    timeMod->set("parse", makeNative("parse", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        std::tm tm{};
#ifndef _WIN32
        char* end = strptime(reqStr(a, 0, "time.parse").c_str(), reqStr(a, 1, "time.parse").c_str(), &tm);
        if (!end) throw NimbleError("time.parse: format does not match the input");
#else
        int y=0,m=0,d=0;
        if (sscanf(reqStr(a, 0, "time.parse").c_str(), "%d-%d-%d", &y, &m, &d) != 3)
            throw NimbleError("time.parse: format not supported on Windows");
        tm.tm_year = y - 1900; tm.tm_mon = m - 1; tm.tm_mday = d;
#endif
        return Value((double)std::mktime(&tm));
    }));
    timeMod->set("add_days", makeNative("add_days", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        return Value(reqNum(a, 0, "time.add_days") + reqNum(a, 1, "time.add_days") * 86400.0);
    }));
    timeMod->set("add_seconds", makeNative("add_seconds", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        return Value(reqNum(a, 0, "time.add_seconds") + reqNum(a, 1, "time.add_seconds"));
    }));
    timeMod->set("diff", makeNative("diff", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        return Value(reqNum(a, 0, "time.diff") - reqNum(a, 1, "time.diff"));
    }));
    g->define("time", Value(timeMod));

    // ---- system ----
    auto systemMod = std::make_shared<MapObj>();
    auto argsList = std::make_shared<ListObj>();
    for (auto& s : programArgs) argsList->items.push_back(Value(s));
    systemMod->set("args", Value(argsList));
    systemMod->set("run", makeNative("run", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>& named, Interpreter& interp) {
        if (!interp.permissions.allowSystemExec) throw NimbleError("system.run(): disabled by the host (sandbox)");
        bool capture = false;
        for (auto& [k,v] : named) if (k == "capture") capture = truthy(v);
        const std::string& cmdStr = reqStr(a, 0, "system.run");
        if (!capture) { int rc = std::system(cmdStr.c_str()); return Value((double)rc); }
        std::string cmd = cmdStr + " 2>&1";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) throw NimbleError("Could not execute the command");
        std::string out; char buf[256];
        while (fgets(buf, sizeof(buf), pipe)) out += buf;
        pclose(pipe);
        return Value(out);
    }));
    systemMod->set("exec", makeNative("exec", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter& interp) {
        if (!interp.permissions.allowSystemExec) throw NimbleError("system.exec(): disabled by the host (sandbox)");
        const std::string& cmdStr = reqStr(a, 0, "system.exec");
#ifndef _WIN32
        std::string tmpfile = "/tmp/nimble_stderr_" + std::to_string(getpid());
        std::string cmd = cmdStr + " 2>" + tmpfile;
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) throw NimbleError("Could not execute the command");
        std::string out; char buf[4096];
        while (fgets(buf, sizeof(buf), pipe)) out += buf;
        int status = pclose(pipe);
        std::string err;
        { std::ifstream f(tmpfile); if (f) { std::ostringstream ss; ss << f.rdbuf(); err = ss.str(); } }
        std::remove(tmpfile.c_str());
        int exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        auto m = std::make_shared<MapObj>();
        m->set("status", Value((double)exitCode));
        m->set("stdout", Value(out));
        m->set("stderr", Value(err));
        return Value(m);
#else
        int rc = std::system(cmdStr.c_str());
        auto m = std::make_shared<MapObj>();
        m->set("status", Value((double)rc));
        m->set("stdout", Value(std::string()));
        m->set("stderr", Value(std::string()));
        return Value(m);
#endif
    }));
    g->define("system", Value(systemMod));

    // ---- json ----
    auto jsonMod = std::make_shared<MapObj>();
    jsonMod->set("encode", makeNative("encode", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        // Antes: el escapado solo cubria '"' y '\\' (un "\n" real producia JSON
        // invalido), las claves de mapa no se escapaban, NaN/Inf salian como
        // `nan`/`inf` y cualquier valor no soportado (funcion, clase, objeto)
        // se convertia en `null` en silencio. Ahora todo eso es un error claro.
        const int MAX_DEPTH = 200; // tambien corta estructuras ciclicas
        auto encStr = [](const std::string& str) {
            std::string out = "\"";
            for (unsigned char c : str) {
                switch (c) {
                    case '"':  out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\b': out += "\\b";  break;
                    case '\f': out += "\\f";  break;
                    case '\n': out += "\\n";  break;
                    case '\r': out += "\\r";  break;
                    case '\t': out += "\\t";  break;
                    default:
                        if (c < 0x20) { char buf[8]; snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf; }
                        else out += (char)c;
                }
            }
            out += "\"";
            return out;
        };
        std::function<std::string(const Value&, int)> enc = [&](const Value& v, int depth) -> std::string {
            if (depth > MAX_DEPTH) throw NimbleError("json.encode: structure too deeply nested (or contains a cycle)");
            if (v.isNull()) return "null";
            if (v.isBool()) return v.asBool() ? "true" : "false";
            if (v.isNum()) {
                double d = v.asNum();
                if (std::isnan(d) || std::isinf(d))
                    throw NimbleError("json.encode: cannot encode " + numToStr(d) + " (NaN/Infinity are not valid JSON)");
                return numToStr(d);
            }
            if (v.isStr()) return encStr(v.asStr());
            if (v.isList()) {
                std::string out = "[";
                auto& items = v.asList()->items;
                for (size_t i = 0; i < items.size(); i++) { if (i) out += ","; out += enc(items[i], depth + 1); }
                out += "]"; return out;
            }
            if (v.isMap()) {
                std::string out = "{";
                auto& entries = v.asMap()->entries;
                for (size_t i = 0; i < entries.size(); i++) {
                    if (i) out += ",";
                    out += encStr(entries[i].first) + ":" + enc(entries[i].second, depth + 1);
                }
                out += "}"; return out;
            }
            throw NimbleError(std::string("json.encode: cannot encode a value of type '") + typeName(v) + "'");
        };
        return Value(enc(reqArg(a, 0, "json.encode"), 0));
    }));
    jsonMod->set("decode", makeNative("decode", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        // Parser estricto (RFC 8259). Antes era laxo: strings sin cerrar,
        // objetos/arrays sin cierre, ':' o ',' faltantes, comas finales,
        // numeros como `+1`/`01`/`1.`, escapes invalidos y basura despues del
        // valor se aceptaban en silencio devolviendo datos truncados.
        const std::string& s = reqStr(a, 0, "json.decode");
        const int MAX_DEPTH = 200; // evita stack overflow con `[[[[...`
        size_t i = 0;
        auto fail = [&](const std::string& msg) {
            int ln = 1; size_t lastNl = std::string::npos;
            for (size_t k = 0; k < i && k < s.size(); k++) if (s[k] == '\n') { ln++; lastNl = k; }
            size_t col = (lastNl == std::string::npos ? i : i - lastNl - 1) + 1;
            throw NimbleError("Invalid JSON: " + msg + " (line " + std::to_string(ln) + ", column " + std::to_string(col) + ")");
        };
        auto skipWs = [&]() { while (i < s.size() && (s[i]==' '||s[i]=='\t'||s[i]=='\n'||s[i]=='\r')) i++; };
        auto appendUtf8 = [](std::string& out, unsigned cp) {
            if (cp < 0x80) out += (char)cp;
            else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
            else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
            else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
        };
        auto hex4 = [&](size_t at, unsigned& cp) -> bool {
            if (at + 4 > s.size()) return false;
            cp = 0;
            for (size_t k = 0; k < 4; k++) {
                char h = s[at + k]; cp <<= 4;
                if (h >= '0' && h <= '9') cp |= h - '0';
                else if (h >= 'a' && h <= 'f') cp |= h - 'a' + 10;
                else if (h >= 'A' && h <= 'F') cp |= h - 'A' + 10;
                else return false;
            }
            return true;
        };
        auto parseStr = [&]() -> std::string {
            size_t startPos = i;
            i++; // comilla de apertura
            std::string out;
            while (true) {
                if (i >= s.size()) { i = startPos; fail("unterminated string (missing closing '\"')"); }
                unsigned char c = s[i];
                if (c == '"') { i++; return out; }
                if (c < 0x20) fail("unescaped control character in string");
                if (c != '\\') { out += (char)c; i++; continue; }
                if (i + 1 >= s.size()) { i = startPos; fail("unterminated string (missing closing '\"')"); }
                char e = s[i + 1];
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u': {
                        unsigned cp;
                        if (!hex4(i + 2, cp)) { i += 1; fail("invalid \\u escape"); }
                        i += 6;
                        if (cp >= 0xD800 && cp <= 0xDBFF) {
                            unsigned lo;
                            if (i + 1 < s.size() && s[i] == '\\' && s[i+1] == 'u' && hex4(i + 2, lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                i += 6;
                            } else fail("unpaired surrogate in \\u escape");
                        } else if (cp >= 0xDC00 && cp <= 0xDFFF) fail("unpaired surrogate in \\u escape");
                        appendUtf8(out, cp);
                        continue;
                    }
                    default: i += 1; fail(std::string("invalid escape '\\") + e + "'");
                }
                i += 2;
            }
        };
        std::function<Value(int)> parseVal = [&](int depth) -> Value {
            if (depth > MAX_DEPTH) fail("nesting too deep");
            skipWs();
            if (i >= s.size()) fail("unexpected end of input");
            char c = s[i];
            if (c == '"') return Value(parseStr());
            if (c == '{') {
                size_t openPos = i;
                i++; auto m = std::make_shared<MapObj>(); skipWs();
                if (i < s.size() && s[i] == '}') { i++; return Value(m); }
                while (true) {
                    skipWs();
                    if (i >= s.size()) { i = openPos; fail("unterminated object (missing '}')"); }
                    if (s[i] != '"') fail("object keys must be strings");
                    std::string key = parseStr();
                    skipWs();
                    if (i >= s.size() || s[i] != ':') fail("expected ':' after object key");
                    i++;
                    Value val = parseVal(depth + 1);
                    m->set(key, val);
                    skipWs();
                    if (i < s.size() && s[i] == ',') { i++; continue; }
                    if (i < s.size() && s[i] == '}') { i++; return Value(m); }
                    if (i >= s.size()) { i = openPos; fail("unterminated object (missing '}')"); }
                    fail("expected ',' or '}' in object");
                }
            }
            if (c == '[') {
                size_t openPos = i;
                i++; auto l = std::make_shared<ListObj>(); skipWs();
                if (i < s.size() && s[i] == ']') { i++; return Value(l); }
                while (true) {
                    l->items.push_back(parseVal(depth + 1));
                    skipWs();
                    if (i < s.size() && s[i] == ',') { i++; continue; }
                    if (i < s.size() && s[i] == ']') { i++; return Value(l); }
                    if (i >= s.size()) { i = openPos; fail("unterminated array (missing ']')"); }
                    fail("expected ',' or ']' in array");
                }
            }
            if (c == 't') { if (s.compare(i, 4, "true") != 0) fail("invalid literal"); i += 4; return Value(true); }
            if (c == 'f') { if (s.compare(i, 5, "false") != 0) fail("invalid literal"); i += 5; return Value(false); }
            if (c == 'n') { if (s.compare(i, 4, "null") != 0) fail("invalid literal"); i += 4; return Value(); }
            // Numero: -?(0|[1-9][0-9]*)(.[0-9]+)?([eE][+-]?[0-9]+)?
            size_t start = i;
            auto digits = [&]() { size_t n = 0; while (i < s.size() && isdigit((unsigned char)s[i])) { i++; n++; } return n; };
            if (i < s.size() && s[i] == '-') i++;
            if (i < s.size() && s[i] == '0') i++;
            else if (digits() == 0) { i = start; fail("unexpected character '" + std::string(1, c) + "'"); }
            if (i < s.size() && s[i] == '.') { i++; if (digits() == 0) fail("digits expected after decimal point"); }
            if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
                i++;
                if (i < s.size() && (s[i] == '+' || s[i] == '-')) i++;
                if (digits() == 0) fail("digits expected in exponent");
            }
            try {
                return Value(std::stod(s.substr(start, i - start)));
            } catch (...) {
                i = start; fail("number out of range");
                return Value();
            }
        };
        Value result = parseVal(0);
        skipWs();
        if (i < s.size()) fail("unexpected data after JSON value");
        return result;
    }));
    g->define("json", Value(jsonMod));

    // ---- path ----
    auto pathMod = std::make_shared<MapObj>();
    pathMod->set("join", makeNative("join", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        std::string out;
        for (size_t i = 0; i < a.size(); i++) { if (i) out += "/"; out += reqStr(a, i, "path.join"); }
        return Value(out);
    }));
    // basename/dirname use std::filesystem::path rather than a hand-rolled
    // search for '/', so both '/' and '\' separators are recognized -- a
    // plain find_last_of('/') silently returned the whole string as the
    // "basename" for any Windows-style path (C:\Users\foo\bar.txt), the same
    // class of silent-wrong-answer-on-Windows bug fixed in listdir().
    pathMod->set("basename", makeNative("basename", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        std::filesystem::path p = nimblePath(reqStr(a, 0, "path.basename"));
        return Value(nimblePathStr(p.filename()));
    }));
    pathMod->set("dirname", makeNative("dirname", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        std::filesystem::path p = nimblePath(reqStr(a, 0, "path.dirname"));
        std::string parent = nimblePathStr(p.parent_path());
        return Value(parent.empty() ? std::string(".") : parent);
    }));
    g->define("path", Value(pathMod));

    // ---- regex ----
    auto regexMod = std::make_shared<MapObj>();
    auto compileRegex = [](const std::string& pattern) {
        try { return std::regex(pattern); }
        catch (std::regex_error& ex) { throw NimbleError(std::string("Invalid regular expression pattern: ") + ex.what()); }
    };
    regexMod->set("matches", makeNative("matches", [compileRegex](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        auto re = compileRegex(reqStr(a, 1, "regex.matches"));
        return Value(std::regex_search(reqStr(a, 0, "regex.matches"), re));
    }));
    regexMod->set("match", makeNative("match", [compileRegex](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        auto re = compileRegex(reqStr(a, 1, "regex.match"));
        std::smatch m;
        std::string s = reqStr(a, 0, "regex.match");
        if (std::regex_search(s, m, re)) return Value(m.str(0));
        return Value();
    }));
    regexMod->set("find_all", makeNative("find_all", [compileRegex](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        auto re = compileRegex(reqStr(a, 1, "regex.find_all"));
        std::string s = reqStr(a, 0, "regex.find_all");
        auto out = std::make_shared<ListObj>();
        for (auto it = std::sregex_iterator(s.begin(), s.end(), re); it != std::sregex_iterator(); ++it)
            out->items.push_back(Value(it->str(0)));
        return Value(out);
    }));
    regexMod->set("groups", makeNative("groups", [compileRegex](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        auto re = compileRegex(reqStr(a, 1, "regex.groups"));
        std::string s = reqStr(a, 0, "regex.groups");
        auto out = std::make_shared<ListObj>();
        std::smatch m;
        if (std::regex_search(s, m, re)) {
            for (size_t i = 1; i < m.size(); i++) out->items.push_back(Value(m[i].matched ? m[i].str() : std::string()));
        }
        return Value(out);
    }));
    regexMod->set("replace", makeNative("replace", [compileRegex](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        auto re = compileRegex(reqStr(a, 1, "regex.replace"));
        return Value(std::regex_replace(reqStr(a, 0, "regex.replace"), re, reqStr(a, 2, "regex.replace")));
    }));
    regexMod->set("replace_first", makeNative("replace_first", [compileRegex](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        auto re = compileRegex(reqStr(a, 1, "regex.replace_first"));
        return Value(std::regex_replace(reqStr(a, 0, "regex.replace_first"), re, reqStr(a, 2, "regex.replace_first"), std::regex_constants::format_first_only));
    }));
    regexMod->set("split", makeNative("split", [compileRegex](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        auto re = compileRegex(reqStr(a, 1, "regex.split"));
        std::string s = reqStr(a, 0, "regex.split");
        auto out = std::make_shared<ListObj>();
        std::sregex_token_iterator it(s.begin(), s.end(), re, -1);
        std::sregex_token_iterator end;
        for (; it != end; ++it) out->items.push_back(Value(it->str()));
        return Value(out);
    }));
    g->define("regex", Value(regexMod));

    // ---- hex ----
    auto hexMod = std::make_shared<MapObj>();
    hexMod->set("encode", makeNative("encode", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        const std::string& s = reqStr(a, 0, "hex.encode");
        static const char* digits = "0123456789abcdef";
        std::string out; out.reserve(s.size() * 2);
        for (unsigned char c : s) { out += digits[c >> 4]; out += digits[c & 0xF]; }
        return Value(out);
    }));
    hexMod->set("decode", makeNative("decode", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        const std::string& s = reqStr(a, 0, "hex.decode");
        if (s.size() % 2 != 0) throw NimbleError("hex.decode(): string length must be even");
        auto hexVal = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            throw NimbleError("hex.decode(): invalid hexadecimal character");
        };
        std::string out; out.reserve(s.size() / 2);
        for (size_t i = 0; i < s.size(); i += 2) {
            int hi = hexVal(s[i]), lo = hexVal(s[i + 1]);
            out += (char)((hi << 4) | lo);
        }
        return Value(out);
    }));
    g->define("hex", Value(hexMod));

    // ---- hash ----
    auto hashMod = std::make_shared<MapObj>();
    // hash.md5(datos) -> string hex de 32 caracteres (minusculas)
    hashMod->set("md5", makeNative("md5", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        return Value(toHexLower(md5Raw(reqStr(a, 0, "hash.md5"))));
    }));
    // hash.md5_raw(datos) -> los 16 bytes del digest como string (para
    // encadenar con hex.encode, construir HMAC a mano, etc.)
    hashMod->set("md5_raw", makeNative("md5_raw", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        return Value(md5Raw(reqStr(a, 0, "hash.md5_raw")));
    }));
    g->define("hash", Value(hashMod));

    // ---- env ----
    auto envMod = std::make_shared<MapObj>();
    envMod->set("get", makeNative("get", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter& interp) {
        if (!interp.permissions.allowEnvRead) throw NimbleError("env.get(): disabled by the host (sandbox)");
        const char* v = std::getenv(reqStr(a, 0, "env.get").c_str());
        return v ? Value(std::string(v)) : Value();
    }));
    envMod->set("has", makeNative("has", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter& interp) {
        if (!interp.permissions.allowEnvRead) throw NimbleError("env.has(): disabled by the host (sandbox)");
        return Value(std::getenv(reqStr(a, 0, "env.has").c_str()) != nullptr);
    }));
    envMod->set("set", makeNative("set", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter& interp) {
        if (!interp.permissions.allowEnvWrite) throw NimbleError("env.set(): disabled by the host (sandbox)");
        const std::string& name = reqStr(a, 0, "env.set");
        const std::string& val = reqStr(a, 1, "env.set");
#ifndef _WIN32
        setenv(name.c_str(), val.c_str(), 1);
#else
        _putenv_s(name.c_str(), val.c_str());
#endif
        return Value();
    }));
    envMod->set("all", makeNative("all", [](std::vector<Value>&, std::vector<std::pair<std::string,Value>>&, Interpreter& interp) {
        if (!interp.permissions.allowEnvRead) throw NimbleError("env.all(): disabled by the host (sandbox)");
        auto m = std::make_shared<MapObj>();
#ifndef _WIN32
        extern char** environ;
        for (char** e = environ; *e; e++) {
            std::string s = *e;
            size_t eq = s.find('=');
            if (eq != std::string::npos) m->set(s.substr(0, eq), Value(s.substr(eq + 1)));
        }
#endif
        return Value(m);
    }));
    g->define("env", Value(envMod));

    // ---- csv ----
    auto csvMod = std::make_shared<MapObj>();
    csvMod->set("parse", makeNative("parse", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>& named, Interpreter&) {
        bool header = true;
        for (auto& [k,v] : named) if (k == "header") header = truthy(v);
        auto rows = parseCsvRows(reqStr(a, 0, "csv.parse"));
        auto out = std::make_shared<ListObj>();
        if (rows.empty()) return Value(out);
        if (!header) {
            for (auto& r : rows) {
                auto lst = std::make_shared<ListObj>();
                for (auto& cell : r) lst->items.push_back(Value(cell));
                out->items.push_back(Value(lst));
            }
            return Value(out);
        }
        auto& hdr = rows[0];
        for (size_t i = 1; i < rows.size(); i++) {
            auto m = std::make_shared<MapObj>();
            for (size_t j = 0; j < hdr.size(); j++) {
                m->set(hdr[j], Value(j < rows[i].size() ? rows[i][j] : std::string()));
            }
            out->items.push_back(Value(m));
        }
        return Value(out);
    }));
    csvMod->set("write", makeNative("write", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>& named, Interpreter&) {
        bool header = true;
        for (auto& [k,v] : named) if (k == "header") header = truthy(v);
        auto& items = reqList(a, 0, "csv.write")->items;
        if (items.empty()) return Value(std::string());
        std::string out;
        if (items[0].isMap()) {
            auto& first = items[0].asMap()->entries;
            if (header) {
                for (size_t j = 0; j < first.size(); j++) {
                    if (j) out += ",";
                    out += csvEscape(first[j].first);
                }
                out += "\n";
            }
            for (auto& it : items) {
                if (!it.isMap()) continue;
                auto& e = it.asMap()->entries;
                for (size_t j = 0; j < first.size(); j++) {
                    if (j) out += ",";
                    Value cell;
                    for (auto& kv : e) if (kv.first == first[j].first) { cell = kv.second; break; }
                    out += csvEscape(toDisplayString(cell));
                }
                out += "\n";
            }
        } else if (items[0].isList()) {
            for (auto& it : items) {
                if (!it.isList()) continue;
                auto& row = it.asList()->items;
                for (size_t j = 0; j < row.size(); j++) {
                    if (j) out += ",";
                    out += csvEscape(toDisplayString(row[j]));
                }
                out += "\n";
            }
        }
        return Value(out);
    }));
    g->define("csv", Value(csvMod));

    // ---- http ----
    auto httpMod = std::make_shared<MapObj>();
#ifdef HAVE_CURL
    auto httpPerform = [](const std::string& method, const std::string& url,
                          const std::string& body, MapObj* headers) -> Value {
        CURL* curl = curl_easy_init();
        if (!curl) throw NimbleError("Could not initialize curl");
        std::string resp;
        struct curl_slist* hdrs = nullptr;
        if (headers) {
            for (auto& [k, v] : headers->entries)
                hdrs = curl_slist_append(hdrs, (k + ": " + toDisplayString(v)).c_str());
        }
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, nimbleCurlWrite);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "nimble/0.5");
        if (hdrs) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
        if (method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        }
        CURLcode rc = curl_easy_perform(curl);
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        if (hdrs) curl_slist_free_all(hdrs);
        curl_easy_cleanup(curl);
        if (rc != CURLE_OK) throw NimbleError(std::string("HTTP error: ") + curl_easy_strerror(rc));
        auto m = std::make_shared<MapObj>();
        m->set("status", Value((double)status));
        m->set("body", Value(resp));
        return Value(m);
    };
    httpMod->set("get", makeNative("get", [httpPerform](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>& named, Interpreter& interp) {
        if (!interp.permissions.allowNetwork) throw NimbleError("http.get(): disabled by the host (sandbox)");
        MapObj* hdrs = nullptr;
        for (auto& [k,v] : named) if (k == "headers" && v.isMap()) hdrs = v.asMap().get();
        return httpPerform("GET", reqStr(a, 0, "http.get"), "", hdrs);
    }));
    httpMod->set("post", makeNative("post", [httpPerform](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>& named, Interpreter& interp) {
        if (!interp.permissions.allowNetwork) throw NimbleError("http.post(): disabled by the host (sandbox)");
        std::string body;
        MapObj* hdrs = nullptr;
        for (auto& [k,v] : named) {
            if (k == "body") body = toDisplayString(v);
            else if (k == "headers" && v.isMap()) hdrs = v.asMap().get();
        }
        return httpPerform("POST", reqStr(a, 0, "http.post"), body, hdrs);
    }));
#else
    httpMod->set("get", makeNative("get", [](std::vector<Value>&, std::vector<std::pair<std::string,Value>>&, Interpreter&) -> Value {
        throw NimbleError("http.get is not available: compile with -DHAVE_CURL -lcurl");
    }));
    httpMod->set("post", makeNative("post", [](std::vector<Value>&, std::vector<std::pair<std::string,Value>>&, Interpreter&) -> Value {
        throw NimbleError("http.post is not available: compile with -DHAVE_CURL -lcurl");
    }));
#endif
    g->define("http", Value(httpMod));

    // net: TCP/UDP crudo y bloqueante (ver docs/net.md). Mismo permiso que
    // http.get/post: allowNetwork.
    auto netMod = std::make_shared<MapObj>();
    netMod->set("tcp_listen", makeNative("tcp_listen", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter& interp) -> Value {
        if (!interp.permissions.allowNetwork) throw NimbleError("net.tcp_listen(): disabled by the host (sandbox)");
        nimbleNetInit();
        std::string host = a.empty() ? "0.0.0.0" : reqStr(a, 0, "net.tcp_listen");
        int port = a.size() > 1 ? (int)reqNum(a, 1, "net.tcp_listen") : 0;
        socket_t fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd == NIMBLE_INVALID_SOCKET) throw NimbleError("net.tcp_listen(): " + nimbleSocketErr());
        int yes = 1;
#ifdef _WIN32
        // En Windows SO_REUSEADDR deja que DOS procesos escuchen en el mismo
        // puerto sin error; lo equivalente a lo que hace en Linux es
        // SO_EXCLUSIVEADDRUSE (un segundo server en un puerto ocupado falla).
        setsockopt(fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&yes, sizeof(yes));
#else
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
#endif
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons((uint16_t)port);
        if (host.empty() || host == "0.0.0.0") addr.sin_addr.s_addr = INADDR_ANY;
        else if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
            nimbleCloseSocket(fd);
            throw NimbleError("net.tcp_listen(): host inválido: " + host);
        }
        if (::bind(fd, (sockaddr*)&addr, sizeof(addr)) != 0) {
            std::string err = nimbleSocketErr(); nimbleCloseSocket(fd);
            throw NimbleError("net.tcp_listen(): bind falló: " + err);
        }
        if (::listen(fd, 16) != 0) {
            std::string err = nimbleSocketErr(); nimbleCloseSocket(fd);
            throw NimbleError("net.tcp_listen(): listen falló: " + err);
        }
        sockaddr_in bound{};
        socklen_t bl = sizeof(bound);
        getsockname(fd, (sockaddr*)&bound, &bl);
        auto sock = std::make_shared<NetSocket>();
        sock->fd = fd;
        return nimbleMakeServer(sock, (int)ntohs(bound.sin_port));
    }));
    netMod->set("poll", makeNative("poll", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) -> Value {
        // Multiplexa N sockets (server/conn/udp, mezclados sin problema, ya
        // que los tres exponen "fd") sin threads: bloquea hasta que alguno
        // tenga datos para leer (o una conexion nueva, para un server) o se
        // cumpla el timeout. Es lo que permite atender a mas de un cliente
        // sin que server.accept()/conn.recv() de uno bloqueen a los demas
        // -- el limite de "un cliente a la vez" que tienen manga_server.nimble
        // y web_server.nimble es justamente no tener esto.
        //
        // v0.16: cada item de la lista puede ser un socket "pelado" (server/
        // conn/udp -- igual que siempre, se interpreta como "avisame si hay
        // algo para leer") o un dict {"sock": conn, "read": bool, "write":
        // bool} para cuando el script tambien necesita saber si puede
        // escribir en ese socket sin bloquear (ver conn.send_nb()). Antes no
        // habia forma de pedir eso: net.poll() solo armaba POLLIN, asi que
        // un script que manda datos a muchas conexiones a la vez no tenia
        // como evitar que una lenta frenara a las demas dentro de un mismo
        // conn.send() bloqueante. "read"/"write" son opcionales -- un dict
        // sin ninguno de los dos pide "read" por defecto, igual que un
        // socket pelado.
        auto socks = reqList(a, 0, "net.poll");
        double timeoutSec = a.size() > 1 ? reqNum(a, 1, "net.poll") : -1.0; // <0 = bloquear sin limite
        int timeoutMs = timeoutSec < 0 ? -1 : (int)(timeoutSec * 1000.0);

#ifdef _WIN32
        std::vector<WSAPOLLFD> pfds;
#else
        std::vector<pollfd> pfds;
#endif
        // specs[i] == nullptr  -> socks->items[i] era un socket pelado.
        // specs[i] != nullptr  -> socks->items[i] era un dict {"sock":...};
        // specs[i] apunta al MapObj de ese dict para poder escribirle
        // "readable"/"writable" antes de devolverlo.
        std::vector<std::shared_ptr<MapObj>> specs;
        pfds.reserve(socks->items.size());
        specs.reserve(socks->items.size());
        for (auto& v : socks->items) {
            if (v.isMap() && v.asMap()->has("fd")) {
                pfds.push_back({(socket_t)(long long)v.asMap()->get("fd").asNum(), POLLIN, 0});
                specs.push_back(nullptr);
                continue;
            }
            if (v.isMap() && v.asMap()->has("sock") && v.asMap()->get("sock").isMap() &&
                v.asMap()->get("sock").asMap()->has("fd")) {
                auto spec = v.asMap();
                auto sockMap = spec->get("sock").asMap();
                bool wantRead = spec->has("read") ? truthy(spec->get("read")) : true;
                bool wantWrite = spec->has("write") ? truthy(spec->get("write")) : false;
                if (!wantRead && !wantWrite)
                    throw NimbleError("net.poll(): el dict debe pedir \"read\" y/o \"write\"");
                short events = (short)((wantRead ? POLLIN : 0) | (wantWrite ? POLLOUT : 0));
                pfds.push_back({(socket_t)(long long)sockMap->get("fd").asNum(), events, 0});
                specs.push_back(spec);
                continue;
            }
            throw NimbleError("net.poll(): la lista debe contener sockets (server/conn/udp) o dicts {\"sock\": conn, \"read\": bool, \"write\": bool}");
        }

#ifdef _WIN32
        int rc = 0;
        if (pfds.empty()) {
            // poll() de POSIX con lista vacia solo espera; WSAPoll la rechaza.
            if (timeoutMs != 0) Sleep(timeoutMs < 0 ? INFINITE : (DWORD)timeoutMs);
        } else {
            rc = WSAPoll(pfds.data(), (ULONG)pfds.size(), timeoutMs);
        }
#else
        int rc = ::poll(pfds.data(), pfds.size(), timeoutMs);
#endif
        if (rc < 0) throw NimbleError("net.poll(): " + nimbleSocketErr());

        auto out = std::make_shared<ListObj>();
        for (size_t i = 0; i < pfds.size(); i++) {
            // POLLHUP/POLLERR cuentan como "listo" para los dos lados: el
            // socket esta en un estado que el script solo puede terminar de
            // diagnosticar llamando a recv()/send_nb() (que van a devolver 0
            // o tirar error), no quedandose sin ser notificado.
            bool hup = (pfds[i].revents & (POLLHUP | POLLERR)) != 0;
            bool sawRead = hup || (pfds[i].revents & POLLIN) != 0;
            bool sawWrite = hup || (pfds[i].revents & POLLOUT) != 0;
            if (!specs[i]) {
                if (sawRead) out->items.push_back(socks->items[i]); // socket pelado: comportamiento de siempre
                continue;
            }
            if (sawRead || sawWrite) {
                specs[i]->set("readable", Value(sawRead));
                specs[i]->set("writable", Value(sawWrite));
                out->items.push_back(socks->items[i]);
            }
        }
        return Value(out);
    }));
    netMod->set("tcp_connect", makeNative("tcp_connect", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter& interp) -> Value {
        if (!interp.permissions.allowNetwork) throw NimbleError("net.tcp_connect(): disabled by the host (sandbox)");
        nimbleNetInit();
        std::string host = reqStr(a, 0, "net.tcp_connect");
        int port = (int)reqNum(a, 1, "net.tcp_connect");
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res)
            throw NimbleError("net.tcp_connect(): no se pudo resolver " + host);
        socket_t fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (fd == NIMBLE_INVALID_SOCKET) { freeaddrinfo(res); throw NimbleError("net.tcp_connect(): " + nimbleSocketErr()); }
        if (::connect(fd, res->ai_addr, (int)res->ai_addrlen) != 0) {
            std::string err = nimbleSocketErr(); freeaddrinfo(res); nimbleCloseSocket(fd);
            throw NimbleError("net.tcp_connect(): " + err);
        }
        freeaddrinfo(res);
        auto sock = std::make_shared<NetSocket>();
        sock->fd = fd;
        return nimbleMakeConn(sock, host, port);
    }));
    netMod->set("udp_socket", makeNative("udp_socket", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter& interp) -> Value {
        if (!interp.permissions.allowNetwork) throw NimbleError("net.udp_socket(): disabled by the host (sandbox)");
        nimbleNetInit();
        std::string host = a.empty() ? "0.0.0.0" : reqStr(a, 0, "net.udp_socket");
        int port = a.size() > 1 ? (int)reqNum(a, 1, "net.udp_socket") : 0;
        socket_t fd = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (fd == NIMBLE_INVALID_SOCKET) throw NimbleError("net.udp_socket(): " + nimbleSocketErr());
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons((uint16_t)port);
        if (host.empty() || host == "0.0.0.0") addr.sin_addr.s_addr = INADDR_ANY;
        else if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
            nimbleCloseSocket(fd);
            throw NimbleError("net.udp_socket(): host inválido: " + host);
        }
        if (::bind(fd, (sockaddr*)&addr, sizeof(addr)) != 0) {
            std::string err = nimbleSocketErr(); nimbleCloseSocket(fd);
            throw NimbleError("net.udp_socket(): bind falló: " + err);
        }
        sockaddr_in bound{};
        socklen_t bl = sizeof(bound);
        getsockname(fd, (sockaddr*)&bound, &bl);
        auto sock = std::make_shared<NetSocket>();
        sock->fd = fd;

        auto m = std::make_shared<MapObj>();
        m->set("fd", Value((double)sock->fd)); // uso interno de net.poll()
        m->set("port", Value((double)ntohs(bound.sin_port)));
        m->set("send_to", makeNative("send_to", [sock](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) -> Value {
            if (sock->fd == NIMBLE_INVALID_SOCKET) throw NimbleError("udp.send_to(): el socket ya está cerrado");
            const std::string& dhost = reqStr(a, 0, "udp.send_to");
            int dport = (int)reqNum(a, 1, "udp.send_to");
            const std::string& data = reqStr(a, 2, "udp.send_to");
            sockaddr_in dst{};
            dst.sin_family = AF_INET;
            dst.sin_port = htons((uint16_t)dport);
            if (inet_pton(AF_INET, dhost.c_str(), &dst.sin_addr) != 1)
                throw NimbleError("udp.send_to(): host inválido: " + dhost);
            long n = (long)::sendto(sock->fd, data.data(), (int)data.size(), 0, (sockaddr*)&dst, sizeof(dst));
            if (n < 0) throw NimbleError("udp.send_to(): " + nimbleSocketErr());
            return Value((double)n);
        }));
        m->set("recv_from", makeNative("recv_from", [sock](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) -> Value {
            if (sock->fd == NIMBLE_INVALID_SOCKET) throw NimbleError("udp.recv_from(): el socket ya está cerrado");
            int maxLen = a.empty() ? 4096 : (int)reqNum(a, 0, "udp.recv_from");
            if (maxLen <= 0) maxLen = 4096;
            std::vector<char> buf((size_t)maxLen);
            sockaddr_in from{};
            socklen_t fl = sizeof(from);
            long n = (long)::recvfrom(sock->fd, buf.data(), maxLen, 0, (sockaddr*)&from, &fl);
            if (n < 0) throw NimbleError("udp.recv_from(): " + nimbleSocketErr());
            char ipStr[INET6_ADDRSTRLEN] = {0};
            inet_ntop(AF_INET, &from.sin_addr, ipStr, sizeof(ipStr));
            auto result = std::make_shared<MapObj>();
            result->set("data", Value(std::string(buf.data(), (size_t)n)));
            result->set("host", Value(std::string(ipStr)));
            result->set("port", Value((double)ntohs(from.sin_port)));
            return Value(result);
        }));
        m->set("close", makeNative("close", [sock](std::vector<Value>&, std::vector<std::pair<std::string,Value>>&, Interpreter&) -> Value {
            nimbleCloseSocket(sock->fd);
            sock->fd = NIMBLE_INVALID_SOCKET;
            return Value();
        }));
        m->set("set_timeout", makeNative("set_timeout", [sock](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter&) -> Value {
            nimbleSetTimeout(sock->fd, reqNum(a, 0, "udp.set_timeout"));
            return Value();
        }));
        return Value(m);
    }));
    netMod->set("resolve", makeNative("resolve", [](std::vector<Value>& a, std::vector<std::pair<std::string,Value>>&, Interpreter& interp) -> Value {
        if (!interp.permissions.allowNetwork) throw NimbleError("net.resolve(): disabled by the host (sandbox)");
        nimbleNetInit();
        std::string host = reqStr(a, 0, "net.resolve");
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res)
            throw NimbleError("net.resolve(): no se pudo resolver " + host);
        char ipStr[INET6_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &((sockaddr_in*)res->ai_addr)->sin_addr, ipStr, sizeof(ipStr));
        freeaddrinfo(res);
        return Value(std::string(ipStr));
    }));
    g->define("net", Value(netMod));

    // Manual/diagnostic access to the cycle collector (see docs/memory-model.md).
    auto gcMod = std::make_shared<MapObj>();
    gcMod->set("collect", makeNative("collect", [](std::vector<Value>&, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        gcCollectCycles();
        return Value((double)g_gc.lastCollected);
    }));
    gcMod->set("stats", makeNative("stats", [](std::vector<Value>&, std::vector<std::pair<std::string,Value>>&, Interpreter&) {
        auto m = std::make_shared<MapObj>();
        m->set("tracked", Value((double)g_gc.nodes.size()));
        m->set("last_collected", Value((double)g_gc.lastCollected));
        m->set("total_collected", Value((double)g_gc.totalCollected));
        return Value(m);
    }));
    g->define("gc", Value(gcMod));
}

// ============================================================
// main / REPL
// ============================================================
static std::string readFile(const std::string& path) {
    std::ifstream f(nimblePath(path), std::ios::binary);
    if (!f) throw NimbleError("Could not open file: " + path);
    std::ostringstream ss; ss << f.rdbuf();
    return ss.str();
}

static void printError(const NimbleError& e) {
    std::cerr << "Error: " << e.what() << "\n";
    std::string ctx = ErrHelp::lineText(e.line);
    if (!ctx.empty()) std::cerr << "   " << e.line << " | " << ctx << "\n";
    if (!e.stack.empty()) {
        std::cerr << "Stack trace:\n";
        for (auto it = e.stack.rbegin(); it != e.stack.rend(); ++it) {
            std::cerr << "   at " << it->first;
            if (it->second > 0) std::cerr << " (called from line " << it->second << ")";
            std::cerr << "\n";
        }
    }
}

static void runRepl() {
    Interpreter interp;
    std::string buffer;
    std::cout << "Nimble REPL (v0.20) -- type 'exit' to quit\n";
    while (true) {
        std::cout << (buffer.empty() ? "> " : "... ");
        std::cout.flush();
        std::string line;
        if (!std::getline(std::cin, line)) break;
        if (buffer.empty() && (line == "salir" || line == "exit")) break;
        buffer += line;
        buffer += '\n';
        ErrHelp::setSource(buffer);

        try {
            Lexer lex(buffer);
            auto toks = lex.tokenize();
            Parser parser(toks);
            auto stmts = parser.parseProgram();
            for (auto& st : stmts) {
                if (st->kind == SK::ExprStmt && st->expr->kind != EK::Assign && st->expr->kind != EK::Call) {
                    Value v = interp.eval(st->expr, interp.globals);
                    std::cout << toDisplayString(v) << "\n";
                } else {
                    ExecResult r = interp.execStmt(st, interp.globals);
                    if (r.flow == Flow::Break) std::cout << "Error: 'break' outside of a loop\n";
                    else if (r.flow == Flow::Continue) std::cout << "Error: 'continue' outside of a loop\n";
                    // Flow::Return / Flow::Normal at the REPL top level: nothing more to do.
                }
            }
            buffer.clear();
        } catch (NeedMoreInput&) {
            continue;
        } catch (NimbleError& e) {
            printError(e);
            buffer.clear();
        } catch (Interpreter::ExecutionLimitExceeded& le) {
            std::cout << "Execution limit exceeded: " << le.message << "\n";
            buffer.clear();
        } catch (ThrowSignal& ts) {
            std::cout << "Uncaught error: " << ts.message << "\n";
            buffer.clear();
        } catch (AbortSignal& as) {
            std::cout << "Fatal: [line " << as.line << "] " << as.msg << "\n";
            buffer.clear();
        } catch (ExitSignal& es) {
            // Reached here, the C++ stack has already fully unwound back to
            // this catch (every RAII destructor between the exit() call and
            // here already ran), so it's safe to actually terminate now.
            std::exit(es.code);
        }
    }
}

#ifndef NIMBLE_NO_MAIN
#ifdef _WIN32
// Pone la consola en UTF-8 mientras corre el programa y la restaura al salir
// (si no, el cambio le queda a la sesion de cmd/PowerShell que lo lanzo).
struct NimbleConsoleUtf8 {
    UINT oldOut = 0, oldIn = 0;
    bool active = false;
    void enable() {
        oldOut = GetConsoleOutputCP(); oldIn = GetConsoleCP();
        if (oldOut != 0) { SetConsoleOutputCP(CP_UTF8); SetConsoleCP(CP_UTF8); active = true; }
    }
    ~NimbleConsoleUtf8() { if (active) { SetConsoleOutputCP(oldOut); SetConsoleCP(oldIn); } }
};
#endif
int main(int argc, char** argv) {
#ifdef _WIN32
    static NimbleConsoleUtf8 consoleGuard;
    consoleGuard.enable();
#endif
#ifndef _WIN32
    // Sin esto: escribir con send()/write() a un socket cuyo otro lado ya
    // cerro la conexion manda SIGPIPE al proceso, que por default lo mata
    // de inmediato -- sin pasar por ningun try/catch de Nimble, porque una
    // senal de sistema no es una excepcion de C++. Cualquier script que
    // mande datos a un cliente que se desconecto (un server con varios
    // clientes, tipico de net.poll()) se caia entero por esto. Ignorarla
    // hace que send()/::write() devuelvan -1 con errno=EPIPE en su lugar,
    // que conn.send() ya convierte en un NimbleError atrapable normal.
    signal(SIGPIPE, SIG_IGN);
#endif
    // argv en UTF-8. En Windows main() recibe argv en la pagina ANSI, asi que
    // se relee la linea de comandos completa como UTF-16 y se convierte.
    std::vector<std::string> argvUtf8;
#ifdef _WIN32
    {
        int wargc = 0;
        LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
        if (wargv) {
            for (int i = 0; i < wargc; i++) argvUtf8.push_back(nimbleWideToUtf8(wargv[i]));
            LocalFree(wargv);
        }
    }
#endif
    if (argvUtf8.empty()) for (int i = 0; i < argc; i++) argvUtf8.push_back(argv[i]);

    std::vector<std::string> args;
    bool noWarn = false;
    for (size_t i = 1; i < argvUtf8.size(); i++) {
        std::string a = argvUtf8[i];
        if (a == "--no-warn") { noWarn = true; continue; }
        args.push_back(a);
    }
    Warnings::enabled = !noWarn;

    bool testMode = false;
    size_t fileIdx = 0;
    bool haveFile = false;
    std::string filePath;
    if (!args.empty()) {
        if (args[0] == "test" && args.size() > 1) {
            filePath = args[1]; fileIdx = 2; haveFile = true; testMode = true;
        }
        else if (args[0] == "run" && args.size() > 1) {
            filePath = args[1]; fileIdx = 2; haveFile = true;
        }
        else {
            filePath = args[0]; fileIdx = 1; haveFile = true;
        }
    }

    if (!haveFile) { runRepl(); return 0; }

    try {
        std::string src = readFile(filePath);
        ErrHelp::setSource(src);
        Lexer lex(src);
        auto toks = lex.tokenize();
        Parser parser(toks);
        auto program = parser.parseProgram();

        std::vector<std::string> interpArgs;
        for (size_t i = fileIdx; i < args.size(); i++) interpArgs.push_back(args[i]);

        Interpreter interp(interpArgs);
        interp.scriptDir = nimblePathStr(nimblePath(filePath).parent_path());
        if (interp.scriptDir.empty()) interp.scriptDir = ".";

        interp.run(program);
        if (testMode) return interp.runTests();
    } catch (ExitSignal& es) {
        return es.code;
    } catch (Interpreter::ExecutionLimitExceeded& le) {
        std::cerr << "Execution limit exceeded: " << le.message << std::endl;
        return 1;
    } catch (AbortSignal& as) {
        std::cerr << "Fatal: [line " << as.line << "] " << as.msg << std::endl;
        return 1;
    } catch (NimbleError& e) {
        printError(e);
        return 1;
    } catch (NeedMoreInput&) {
        std::cerr << "Error: unclosed block (missing 'end')" << std::endl;
        return 1;
    }
    return 0;
}
#endif // NIMBLE_NO_MAIN
