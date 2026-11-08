#include <hxcpp.h>

#include <array>
#include <chrono>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <conio.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#endif

namespace {

struct NativeFile final {
    std::FILE* stream = nullptr;
    bool owned = true;
    std::mutex mutex;
};

std::mutex filesMutex;
std::vector<std::unique_ptr<NativeFile>> files;

[[noreturn]] void nativeFailure(const std::string& message,
                                bool eof = false);

#ifdef _WIN32
struct NativeSocket final {
    SOCKET socket = INVALID_SOCKET;
    bool udp = false;
    bool ipv6 = false;
};

std::once_flag winsockOnce;
std::mutex socketsMutex;
std::vector<std::unique_ptr<NativeSocket>> sockets;

struct NativeProcess final {
    PROCESS_INFORMATION information{};
    HANDLE stdinWrite = nullptr;
    HANDLE stdoutRead = nullptr;
    HANDLE stderrRead = nullptr;
    bool closed = false;
};

std::mutex processesMutex;
std::vector<std::unique_ptr<NativeProcess>> processes;

void ensureWinsock() {
    std::call_once(winsockOnce, [] {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
            nativeFailure("unable to initialize Winsock");
    });
}

NativeSocket* decodeSocket(const ::Dynamic& value) {
    auto* socket = static_cast<NativeSocket*>(value.nativePointer());
    if (socket == nullptr || socket->socket == INVALID_SOCKET)
        nativeFailure("invalid or closed socket");
    return socket;
}

::Dynamic registerSocket(SOCKET socket, bool udp, bool ipv6) {
    if (socket == INVALID_SOCKET)
        nativeFailure("unable to create socket");
    auto value = std::make_unique<NativeSocket>();
    value->socket = socket;
    value->udp = udp;
    value->ipv6 = ipv6;
    NativeSocket* raw = value.get();
    std::scoped_lock guard(socketsMutex);
    sockets.push_back(std::move(value));
    return ::Dynamic::fromNativePointer(raw);
}

int socketError() { return WSAGetLastError(); }

[[noreturn]] void socketFailure(const char* operation) {
    nativeFailure(std::string(operation) + " failed: " +
                  std::to_string(socketError()));
}

::Array<::Dynamic> socketAddress(const sockaddr_storage& storage) {
    auto output = ::Array_obj<::Dynamic>::__new();
    if (storage.ss_family == AF_INET) {
        const auto& address = reinterpret_cast<const sockaddr_in&>(storage);
        output->push(::Dynamic(static_cast<std::int32_t>(
            address.sin_addr.s_addr)));
        output->push(::Dynamic(static_cast<int>(ntohs(address.sin_port))));
    } else {
        const auto& address = reinterpret_cast<const sockaddr_in6&>(storage);
        output->push(::Dynamic(0));
        output->push(::Dynamic(static_cast<int>(ntohs(address.sin6_port))));
    }
    return output;
}

NativeSocket* socketFromHaxeValue(const ::Dynamic& value) {
    if (value.tag() == ::Dynamic::Tag::NativePointer)
        return decodeSocket(value);
    ::Dynamic handle = value.__Field(::String("__s"), ::hx::paccDynamic);
    return decodeSocket(handle);
}

NativeProcess* decodeProcess(const ::Dynamic& value) {
    auto* process = static_cast<NativeProcess*>(value.nativePointer());
    if (process == nullptr || process->closed)
        nativeFailure("invalid or closed process");
    return process;
}

std::wstring quoteProcessArgument(const std::wstring& input) {
    if (input.find_first_of(L" \t\"") == std::wstring::npos)
        return input;
    std::wstring output = L"\"";
    std::size_t slashes = 0;
    for (wchar_t character : input) {
        if (character == L'\\') {
            ++slashes;
        } else if (character == L'\"') {
            output.append(slashes * 2 + 1, L'\\');
            output.push_back(L'\"');
            slashes = 0;
        } else {
            output.append(slashes, L'\\');
            slashes = 0;
            output.push_back(character);
        }
    }
    output.append(slashes * 2, L'\\');
    output.push_back(L'\"');
    return output;
}
#endif

class BlockingScope final {
public:
    BlockingScope()
        : attached_(::hx::gc::Runtime::instance().currentThreadAttached()) {
        if (attached_)
            ::hx::gc::Runtime::instance().enterNativeBlocking();
    }
    ~BlockingScope() {
        if (attached_)
            ::hx::gc::Runtime::instance().leaveNativeBlocking();
    }
private:
    bool attached_;
};

[[noreturn]] void nativeFailure(const std::string& message, bool eof) {
    auto values = ::Array_obj<::Dynamic>::__new();
    values->push(::Dynamic(::String(eof ? "Eof" : message)));
    throw ::Dynamic(values);
}

NativeFile* decodeFile(const ::Dynamic& value) {
    auto* file = static_cast<NativeFile*>(value.nativePointer());
    if (file == nullptr || file->stream == nullptr)
        nativeFailure("invalid or closed file handle");
    return file;
}

::Dynamic registerFile(std::FILE* stream, bool owned = true) {
    if (stream == nullptr)
        nativeFailure("unable to open file");
    auto value = std::make_unique<NativeFile>();
    value->stream = stream;
    value->owned = owned;
    NativeFile* raw = value.get();
    std::scoped_lock guard(filesMutex);
    files.push_back(std::move(value));
    return ::Dynamic::fromNativePointer(raw);
}

std::filesystem::path nativePath(const ::String& value) {
#ifdef _WIN32
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.c_str(), -1, nullptr, 0);
    if (required <= 0)
        nativeFailure("invalid UTF-8 path");
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1,
        wide.data(), required);
    wide.resize(static_cast<std::size_t>(required - 1));
    return std::filesystem::path(wide);
#else
    return std::filesystem::path(std::string(value.view()));
#endif
}

::String haxePath(const std::filesystem::path& value) {
#ifdef _WIN32
    const std::wstring wide = value.wstring();
    const int required = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1,
        nullptr, 0, nullptr, nullptr);
    if (required <= 0)
        nativeFailure("unable to encode path");
    std::string utf8(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, utf8.data(), required,
        nullptr, nullptr);
    utf8.resize(static_cast<std::size_t>(required - 1));
    return ::String(utf8);
#else
    return ::String(value.string());
#endif
}

template<class T>
::Array<T> arrayFromVector(const std::vector<T>& values) {
    return ::Array_obj<T>::fromData(values.data(),
                                    static_cast<int>(values.size()));
}

} // namespace

double __time_stamp() noexcept {
    using Clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

::Dynamic _hx_std_file_open(::String path, ::String mode) {
    BlockingScope blocking;
#ifdef _WIN32
    std::FILE* stream = nullptr;
    std::wstring nativeMode;
    for (char byte : mode.view())
        nativeMode.push_back(static_cast<wchar_t>(byte));
    if (_wfopen_s(&stream, nativePath(path).wstring().c_str(),
                  nativeMode.c_str()) != 0)
        stream = nullptr;
#else
    std::FILE* stream = std::fopen(path.c_str(), mode.c_str());
#endif
    return registerFile(stream);
}

::String _hx_std_file_contents_string(::String path) {
    ::Dynamic handle = _hx_std_file_open(path, ::String("rb"));
    NativeFile* file = decodeFile(handle);
    std::string output;
    std::array<char, 16384> buffer{};
    for (;;) {
        const std::size_t count = std::fread(buffer.data(), 1, buffer.size(),
                                             file->stream);
        output.append(buffer.data(), count);
        if (count != buffer.size())
            break;
    }
    _hx_std_file_close(handle);
    return ::String(output);
}

::Array<unsigned char> _hx_std_file_contents_bytes(::String path) {
    const ::String contents = _hx_std_file_contents_string(path);
    const auto view = contents.view();
    return ::Array_obj<unsigned char>::fromData(
        reinterpret_cast<const unsigned char*>(view.data()),
        static_cast<int>(view.size()));
}

int _hx_std_file_read_char(::Dynamic handle) {
    NativeFile* file = decodeFile(handle);
    BlockingScope blocking;
    std::scoped_lock guard(file->mutex);
    const int value = std::fgetc(file->stream);
    if (value == EOF)
        nativeFailure("end of file", true);
    return value;
}

int _hx_std_file_read(::Dynamic handle, ::Array<unsigned char> bytes,
                      int position, int length) {
    if (!bytes || position < 0 || length < 0 ||
        position > bytes->length - length)
        nativeFailure("invalid file read range");
    NativeFile* file = decodeFile(handle);
    BlockingScope blocking;
    std::scoped_lock guard(file->mutex);
    const std::size_t count = std::fread(bytes->getBase() + position, 1,
        static_cast<std::size_t>(length), file->stream);
    if (count == 0 && length != 0 && std::feof(file->stream))
        nativeFailure("end of file", true);
    return static_cast<int>(count);
}

void _hx_std_file_write_char(::Dynamic handle, int value) {
    NativeFile* file = decodeFile(handle);
    BlockingScope blocking;
    std::scoped_lock guard(file->mutex);
    if (std::fputc(value, file->stream) == EOF)
        nativeFailure("file write failed");
}

int _hx_std_file_write(::Dynamic handle, ::Array<unsigned char> bytes,
                       int position, int length) {
    if (!bytes || position < 0 || length < 0 ||
        position > bytes->length - length)
        nativeFailure("invalid file write range");
    NativeFile* file = decodeFile(handle);
    BlockingScope blocking;
    std::scoped_lock guard(file->mutex);
    const auto count = std::fwrite(bytes->getBase() + position, 1,
        static_cast<std::size_t>(length), file->stream);
    if (count != static_cast<std::size_t>(length) && std::ferror(file->stream))
        nativeFailure("file write failed");
    return static_cast<int>(count);
}

void _hx_std_file_flush(::Dynamic handle) {
    NativeFile* file = decodeFile(handle);
    BlockingScope blocking;
    std::scoped_lock guard(file->mutex);
    if (std::fflush(file->stream) != 0)
        nativeFailure("file flush failed");
}

void _hx_std_file_close(::Dynamic handle) {
    auto* file = static_cast<NativeFile*>(handle.nativePointer());
    if (file == nullptr)
        return;
    std::scoped_lock guard(file->mutex);
    if (file->stream != nullptr && file->owned)
        std::fclose(file->stream);
    file->stream = nullptr;
}

void _hx_std_file_seek(::Dynamic handle, int position, int origin) {
    NativeFile* file = decodeFile(handle);
    BlockingScope blocking;
    std::scoped_lock guard(file->mutex);
    const int nativeOrigin = origin == 0 ? SEEK_SET :
                             origin == 1 ? SEEK_CUR : SEEK_END;
    if (std::fseek(file->stream, position, nativeOrigin) != 0)
        nativeFailure("file seek failed");
}

int _hx_std_file_tell(::Dynamic handle) {
    NativeFile* file = decodeFile(handle);
    std::scoped_lock guard(file->mutex);
    const long value = std::ftell(file->stream);
    if (value < 0)
        nativeFailure("file tell failed");
    return static_cast<int>(value);
}

bool _hx_std_file_eof(::Dynamic handle) {
    NativeFile* file = decodeFile(handle);
    std::scoped_lock guard(file->mutex);
    return std::feof(file->stream) != 0;
}

::Dynamic _hx_std_file_stdin() { return registerFile(stdin, false); }
::Dynamic _hx_std_file_stdout() { return registerFile(stdout, false); }
::Dynamic _hx_std_file_stderr() { return registerFile(stderr, false); }

bool _hx_std_sys_exists(::String path) {
    std::error_code error;
    return std::filesystem::exists(nativePath(path), error);
}

void _hx_std_sys_rename(::String path, ::String destination) {
    std::error_code error;
    std::filesystem::rename(nativePath(path), nativePath(destination), error);
    if (error)
        nativeFailure(error.message());
}

::Dynamic _hx_std_sys_stat(::String path) {
    std::error_code error;
    const auto native = nativePath(path);
    const auto status = std::filesystem::status(native, error);
    if (error)
        return ::Dynamic();
    const auto size = std::filesystem::is_regular_file(status)
        ? std::filesystem::file_size(native, error) : 0;
    const auto now = static_cast<double>(std::time(nullptr));
    return ::Dynamic(::hx::Anon_obj::Create(11)
        ->setFixed(0, ::String("nlink"), 1)
        ->setFixed(1, ::String("dev"), 0)
        ->setFixed(2, ::String("gid"), 0)
        ->setFixed(3, ::String("ino"), 0)
        ->setFixed(4, ::String("uid"), 0)
        ->setFixed(5, ::String("mtime"), now)
        ->setFixed(6, ::String("atime"), now)
        ->setFixed(7, ::String("mode"), static_cast<int>(status.permissions()))
        ->setFixed(8, ::String("rdev"), 0)
        ->setFixed(9, ::String("size"), static_cast<double>(size))
        ->setFixed(10, ::String("ctime"), now));
}

::String _hx_std_file_full_path(::String path) {
    std::error_code error;
    auto result = std::filesystem::weakly_canonical(nativePath(path), error);
    if (error)
        result = std::filesystem::absolute(nativePath(path), error);
    return haxePath(result);
}

::String _hx_std_sys_file_type(::String path) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(nativePath(path), error);
    if (error)
        nativeFailure(error.message());
    if (std::filesystem::is_directory(status)) return ::String("dir");
    if (std::filesystem::is_regular_file(status)) return ::String("file");
    if (std::filesystem::is_symlink(status)) return ::String("link");
    return ::String("other");
}

bool _hx_std_sys_create_dir(::String path, int mode) {
    (void)mode;
    std::error_code error;
    const auto native = nativePath(path);
    return std::filesystem::create_directory(native, error) ||
           (!error && std::filesystem::is_directory(native));
}

void _hx_std_file_delete(::String path) {
    std::error_code error;
    if (!std::filesystem::remove(nativePath(path), error))
        nativeFailure(error ? error.message() :
            std::string("path does not exist: ") + path.c_str());
}

void _hx_std_sys_remove_dir(::String path) { _hx_std_file_delete(path); }

::Array<::String> _hx_std_sys_read_dir(::String path) {
    std::vector<::String> output;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(
             nativePath(path), error))
        output.push_back(haxePath(entry.path().filename()));
    if (error)
        nativeFailure(error.message());
    return arrayFromVector(output);
}

int _hx_std_sys_getch(bool echo) {
#ifdef _WIN32
    return echo ? _getche() : _getch();
#else
    (void)echo;
    return std::getchar();
#endif
}

::String _hx_std_get_env(::String name) {
#ifdef _WIN32
    const auto wideName = nativePath(name).wstring();
    const DWORD length = GetEnvironmentVariableW(wideName.c_str(), nullptr, 0);
    if (length == 0)
        return nullptr;
    std::wstring value(length, L'\0');
    const DWORD written = GetEnvironmentVariableW(
        wideName.c_str(), value.data(), length);
    if (written == 0 || written >= length)
        return nullptr;
    value.resize(written);
    return haxePath(std::filesystem::path(value));
#else
    const char* value = std::getenv(name.c_str());
    return value == nullptr ? ::String(nullptr) : ::String(value);
#endif
}

void _hx_std_put_env(::String name, ::String value) {
#ifdef _WIN32
    if (!SetEnvironmentVariableW(nativePath(name).wstring().c_str(),
                                 nativePath(value).wstring().c_str()))
        nativeFailure("unable to update environment variable");
#else
    setenv(name.c_str(), value.c_str(), 1);
#endif
}

void _hx_std_sys_sleep(double seconds) {
    if (seconds <= 0) return;
    BlockingScope blocking;
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
}

bool _hx_std_set_time_locale(::String locale) {
    return std::setlocale(LC_TIME, locale.c_str()) != nullptr;
}

::String _hx_std_get_cwd() { return haxePath(std::filesystem::current_path()); }
void _hx_std_set_cwd(::String path) {
    std::filesystem::current_path(nativePath(path));
}

::String _hx_std_sys_string() {
#ifdef _WIN32
    return ::String("Windows");
#elif defined(__APPLE__)
    return ::String("Mac");
#else
    return ::String("Linux");
#endif
}

int _hx_std_sys_command(::String command) {
    BlockingScope blocking;
    return std::system(command.c_str());
}

double _hx_std_sys_time() {
    return std::chrono::duration<double>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

double _hx_std_sys_cpu_time() {
    return static_cast<double>(std::clock()) / CLOCKS_PER_SEC;
}

::String _hx_std_sys_exe_path() {
#ifdef _WIN32
    std::wstring path(32768, L'\0');
    const DWORD count = GetModuleFileNameW(nullptr, path.data(),
                                           static_cast<DWORD>(path.size()));
    path.resize(count);
    return haxePath(std::filesystem::path(path));
#else
    return ::String();
#endif
}

::Array<::String> _hx_std_sys_env() {
    std::vector<::String> output;
#ifdef _WIN32
    wchar_t* environment = GetEnvironmentStringsW();
    if (environment != nullptr) {
        for (const wchar_t* cursor = environment; *cursor != L'\0';) {
            std::wstring entry(cursor);
            output.push_back(haxePath(std::filesystem::path(entry)));
            cursor += entry.size() + 1;
        }
        FreeEnvironmentStringsW(environment);
    }
#endif
    return arrayFromVector(output);
}

void _hx_std_socket_init() {
#ifdef _WIN32
    ensureWinsock();
#endif
}

int _hx_std_host_resolve(::String name) {
#ifdef _WIN32
    ensureWinsock();
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    BlockingScope blocking;
    if (getaddrinfo(name.c_str(), nullptr, &hints, &result) != 0 ||
        result == nullptr)
        socketFailure("host resolve");
    const auto* address = reinterpret_cast<const sockaddr_in*>(result->ai_addr);
    const int output = static_cast<std::int32_t>(address->sin_addr.s_addr);
    freeaddrinfo(result);
    return output;
#else
    (void)name; nativeFailure("sockets unsupported");
#endif
}

::Array<unsigned char> _hx_std_host_resolve_ipv6(::String name) {
#ifdef _WIN32
    ensureWinsock();
    addrinfo hints{};
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    BlockingScope blocking;
    if (getaddrinfo(name.c_str(), nullptr, &hints, &result) != 0 ||
        result == nullptr)
        socketFailure("IPv6 host resolve");
    const auto* address = reinterpret_cast<const sockaddr_in6*>(result->ai_addr);
    auto output = ::Array_obj<unsigned char>::fromData(
        reinterpret_cast<const unsigned char*>(&address->sin6_addr), 16);
    freeaddrinfo(result);
    return output;
#else
    (void)name; nativeFailure("sockets unsupported");
#endif
}

::String _hx_std_host_to_string(int address) {
#ifdef _WIN32
    in_addr value{};
    value.s_addr = static_cast<u_long>(address);
    char output[INET_ADDRSTRLEN]{};
    if (inet_ntop(AF_INET, &value, output, sizeof(output)) == nullptr)
        socketFailure("IPv4 format");
    return ::String(output);
#else
    (void)address; return ::String();
#endif
}

::String _hx_std_host_to_string_ipv6(::Array<unsigned char> address) {
#ifdef _WIN32
    if (!address || address->length < 16)
        nativeFailure("invalid IPv6 address");
    char output[INET6_ADDRSTRLEN]{};
    if (inet_ntop(AF_INET6, address->getBase(), output, sizeof(output)) == nullptr)
        socketFailure("IPv6 format");
    return ::String(output);
#else
    (void)address; return ::String();
#endif
}

::String _hx_std_host_reverse(int address) {
#ifdef _WIN32
    sockaddr_in value{};
    value.sin_family = AF_INET;
    value.sin_addr.s_addr = static_cast<u_long>(address);
    char host[NI_MAXHOST]{};
    BlockingScope blocking;
    if (getnameinfo(reinterpret_cast<sockaddr*>(&value), sizeof(value),
                    host, sizeof(host), nullptr, 0, NI_NAMEREQD) != 0)
        return _hx_std_host_to_string(address);
    return ::String(host);
#else
    return _hx_std_host_to_string(address);
#endif
}

::String _hx_std_host_reverse_ipv6(::Array<unsigned char> address) {
#ifdef _WIN32
    if (!address || address->length < 16)
        nativeFailure("invalid IPv6 address");
    sockaddr_in6 value{};
    value.sin6_family = AF_INET6;
    std::memcpy(&value.sin6_addr, address->getBase(), 16);
    char host[NI_MAXHOST]{};
    BlockingScope blocking;
    if (getnameinfo(reinterpret_cast<sockaddr*>(&value), sizeof(value),
                    host, sizeof(host), nullptr, 0, NI_NAMEREQD) != 0)
        return _hx_std_host_to_string_ipv6(address);
    return ::String(host);
#else
    return _hx_std_host_to_string_ipv6(address);
#endif
}

::String _hx_std_host_local() {
#ifdef _WIN32
    ensureWinsock();
    char host[256]{};
    if (gethostname(host, sizeof(host)) != 0)
        socketFailure("gethostname");
    return ::String(host);
#else
    return ::String("localhost");
#endif
}

::Dynamic _hx_std_socket_new(bool udp, bool ipv6) {
#ifdef _WIN32
    ensureWinsock();
    return registerSocket(::socket(ipv6 ? AF_INET6 : AF_INET,
        udp ? SOCK_DGRAM : SOCK_STREAM, udp ? IPPROTO_UDP : IPPROTO_TCP),
        udp, ipv6);
#else
    (void)udp; (void)ipv6; nativeFailure("sockets unsupported");
#endif
}

void _hx_std_socket_close(::Dynamic value) {
#ifdef _WIN32
    auto* socket = static_cast<NativeSocket*>(value.nativePointer());
    if (socket != nullptr && socket->socket != INVALID_SOCKET) {
        closesocket(socket->socket);
        socket->socket = INVALID_SOCKET;
    }
#else
    (void)value;
#endif
}

std::uintptr_t _hx_std_socket_native_handle(::Dynamic value) {
#ifdef _WIN32
    return static_cast<std::uintptr_t>(decodeSocket(value)->socket);
#else
    (void)value;
    nativeFailure("sockets unsupported");
#endif
}

int _hx_std_socket_send(::Dynamic value, ::Array<unsigned char> bytes,
                        int position, int length) {
#ifdef _WIN32
    if (!bytes || position < 0 || length < 0 ||
        position > bytes->length - length)
        nativeFailure("invalid socket send range");
    NativeSocket* socket = decodeSocket(value);
    BlockingScope blocking;
    const int result = ::send(socket->socket,
        reinterpret_cast<const char*>(bytes->getBase() + position), length, 0);
    if (result == SOCKET_ERROR)
        socketFailure("socket send");
    return result;
#else
    (void)value; (void)bytes; (void)position; (void)length; return -1;
#endif
}

int _hx_std_socket_recv(::Dynamic value, ::Array<unsigned char> bytes,
                        int position, int length) {
#ifdef _WIN32
    if (!bytes || position < 0 || length < 0 ||
        position > bytes->length - length)
        nativeFailure("invalid socket receive range");
    NativeSocket* socket = decodeSocket(value);
    BlockingScope blocking;
    const int result = ::recv(socket->socket,
        reinterpret_cast<char*>(bytes->getBase() + position), length, 0);
    if (result == SOCKET_ERROR)
        socketFailure("socket receive");
    if (result == 0 && length != 0)
        nativeFailure("socket closed", true);
    return result;
#else
    (void)value; (void)bytes; (void)position; (void)length; return -1;
#endif
}

int _hx_std_socket_send_char(::Dynamic value, int byte) {
    auto buffer = ::Array_obj<unsigned char>::__new(1);
    buffer->setCtx(nullptr, 0, static_cast<unsigned char>(byte));
    return _hx_std_socket_send(value, buffer, 0, 1);
}

int _hx_std_socket_recv_char(::Dynamic value) {
    auto buffer = ::Array_obj<unsigned char>::__new(1);
    (void)_hx_std_socket_recv(value, buffer, 0, 1);
    return buffer->__get(0);
}

::Array<unsigned char> _hx_std_socket_read(::Dynamic socket) {
    std::vector<unsigned char> output;
    auto chunk = ::Array_obj<unsigned char>::__new(16384);
    for (;;) {
        try {
            const int count = _hx_std_socket_recv(socket, chunk, 0, 16384);
            output.insert(output.end(), chunk->getBase(),
                          chunk->getBase() + count);
            if (count < 16384)
                break;
        } catch (const ::Dynamic&) {
            if (output.empty()) throw;
            break;
        }
    }
    return arrayFromVector(output);
}

void _hx_std_socket_write(::Dynamic socket, ::Array<unsigned char> bytes) {
    int position = 0;
    while (position < bytes->length)
        position += _hx_std_socket_send(socket, bytes, position,
                                        bytes->length - position);
}

void _hx_std_socket_connect(::Dynamic value, int address, int port) {
#ifdef _WIN32
    NativeSocket* socket = decodeSocket(value);
    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_addr.s_addr = static_cast<u_long>(address);
    destination.sin_port = htons(static_cast<u_short>(port));
    BlockingScope blocking;
    if (::connect(socket->socket, reinterpret_cast<sockaddr*>(&destination),
                  sizeof(destination)) == SOCKET_ERROR)
        socketFailure("socket connect");
#else
    (void)value; (void)address; (void)port;
#endif
}

void _hx_std_socket_connect_ipv6(::Dynamic value,
                                 ::Array<unsigned char> address, int port) {
#ifdef _WIN32
    if (!address || address->length < 16)
        nativeFailure("invalid IPv6 address");
    NativeSocket* socket = decodeSocket(value);
    sockaddr_in6 destination{};
    destination.sin6_family = AF_INET6;
    std::memcpy(&destination.sin6_addr, address->getBase(), 16);
    destination.sin6_port = htons(static_cast<u_short>(port));
    BlockingScope blocking;
    if (::connect(socket->socket, reinterpret_cast<sockaddr*>(&destination),
                  sizeof(destination)) == SOCKET_ERROR)
        socketFailure("IPv6 socket connect");
#else
    (void)value; (void)address; (void)port;
#endif
}

void _hx_std_socket_listen(::Dynamic value, int backlog) {
#ifdef _WIN32
    if (::listen(decodeSocket(value)->socket, backlog) == SOCKET_ERROR)
        socketFailure("socket listen");
#else
    (void)value; (void)backlog;
#endif
}

void _hx_std_socket_shutdown(::Dynamic value, bool read, bool write) {
#ifdef _WIN32
    int how = read && write ? SD_BOTH : read ? SD_RECEIVE : SD_SEND;
    if (::shutdown(decodeSocket(value)->socket, how) == SOCKET_ERROR)
        socketFailure("socket shutdown");
#else
    (void)value; (void)read; (void)write;
#endif
}

void _hx_std_socket_bind(::Dynamic value, int address, int port) {
#ifdef _WIN32
    NativeSocket* socket = decodeSocket(value);
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = static_cast<u_long>(address);
    local.sin_port = htons(static_cast<u_short>(port));
    if (::bind(socket->socket, reinterpret_cast<sockaddr*>(&local),
               sizeof(local)) == SOCKET_ERROR)
        socketFailure("socket bind");
#else
    (void)value; (void)address; (void)port;
#endif
}

void _hx_std_socket_bind_ipv6(::Dynamic value,
                              ::Array<unsigned char> address, int port) {
#ifdef _WIN32
    if (!address || address->length < 16)
        nativeFailure("invalid IPv6 address");
    NativeSocket* socket = decodeSocket(value);
    sockaddr_in6 local{};
    local.sin6_family = AF_INET6;
    std::memcpy(&local.sin6_addr, address->getBase(), 16);
    local.sin6_port = htons(static_cast<u_short>(port));
    if (::bind(socket->socket, reinterpret_cast<sockaddr*>(&local),
               sizeof(local)) == SOCKET_ERROR)
        socketFailure("IPv6 socket bind");
#else
    (void)value; (void)address; (void)port;
#endif
}

::Dynamic _hx_std_socket_accept(::Dynamic value) {
#ifdef _WIN32
    NativeSocket* socket = decodeSocket(value);
    BlockingScope blocking;
    SOCKET accepted = ::accept(socket->socket, nullptr, nullptr);
    if (accepted == INVALID_SOCKET)
        socketFailure("socket accept");
    return registerSocket(accepted, false, socket->ipv6);
#else
    (void)value; return ::Dynamic();
#endif
}

static ::Dynamic querySocketAddress(::Dynamic value, bool peer) {
#ifdef _WIN32
    NativeSocket* socket = decodeSocket(value);
    sockaddr_storage storage{};
    int length = sizeof(storage);
    const int result = peer
        ? getpeername(socket->socket, reinterpret_cast<sockaddr*>(&storage),
                      &length)
        : getsockname(socket->socket, reinterpret_cast<sockaddr*>(&storage),
                      &length);
    return result == SOCKET_ERROR ? ::Dynamic() : ::Dynamic(socketAddress(storage));
#else
    (void)value; (void)peer; return ::Dynamic();
#endif
}

::Dynamic _hx_std_socket_peer(::Dynamic value) {
    return querySocketAddress(value, true);
}
::Dynamic _hx_std_socket_host(::Dynamic value) {
    return querySocketAddress(value, false);
}

void _hx_std_socket_set_timeout(::Dynamic value, double timeout) {
#ifdef _WIN32
    const DWORD milliseconds = timeout <= 0 ? 0 :
        static_cast<DWORD>(timeout * 1000.0);
    SOCKET socket = decodeSocket(value)->socket;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&milliseconds), sizeof(milliseconds));
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO,
               reinterpret_cast<const char*>(&milliseconds), sizeof(milliseconds));
#else
    (void)value; (void)timeout;
#endif
}

void _hx_std_socket_set_blocking(::Dynamic value, bool blocking) {
#ifdef _WIN32
    u_long mode = blocking ? 0 : 1;
    if (ioctlsocket(decodeSocket(value)->socket, FIONBIO, &mode) == SOCKET_ERROR)
        socketFailure("socket blocking mode");
#else
    (void)value; (void)blocking;
#endif
}

void _hx_std_socket_set_fast_send(::Dynamic value, bool fast) {
#ifdef _WIN32
    const BOOL noDelay = fast ? TRUE : FALSE;
    if (setsockopt(decodeSocket(value)->socket, IPPROTO_TCP, TCP_NODELAY,
        reinterpret_cast<const char*>(&noDelay), sizeof(noDelay)) == SOCKET_ERROR)
        socketFailure("socket TCP_NODELAY");
#else
    (void)value; (void)fast;
#endif
}

::cpp::VirtualArray _hx_std_socket_select(::Array<::Dynamic> read,
    ::Array<::Dynamic> write, ::Array<::Dynamic> others, ::Dynamic timeout) {
#ifdef _WIN32
    fd_set readSet{}, writeSet{}, errorSet{};
    FD_ZERO(&readSet); FD_ZERO(&writeSet); FD_ZERO(&errorSet);
    auto add = [](const ::Array<::Dynamic>& values, fd_set& set) {
        if (!values) return;
        for (int index = 0; index < values->length; ++index)
            FD_SET(socketFromHaxeValue(values->__get(index))->socket, &set);
    };
    add(read, readSet); add(write, writeSet); add(others, errorSet);
    timeval nativeTimeout{};
    timeval* timeoutPointer = nullptr;
    if (!timeout.isNull()) {
        const double seconds = timeout.Cast<double>();
        nativeTimeout.tv_sec = static_cast<long>(seconds);
        nativeTimeout.tv_usec = static_cast<long>((seconds - nativeTimeout.tv_sec) * 1000000.0);
        timeoutPointer = &nativeTimeout;
    }
    BlockingScope blocking;
    if (::select(0, &readSet, &writeSet, &errorSet, timeoutPointer) == SOCKET_ERROR)
        return nullptr;
    auto filter = [](const ::Array<::Dynamic>& values, fd_set& set) {
        auto result = ::Array_obj<::Dynamic>::__new();
        if (values) for (int index = 0; index < values->length; ++index) {
            ::Dynamic value = values->__get(index);
            if (FD_ISSET(socketFromHaxeValue(value)->socket, &set))
                result->push(value);
        }
        return result;
    };
    auto output = ::Array_obj<::Dynamic>::__new();
    output->push(::Dynamic(filter(read, readSet)));
    output->push(::Dynamic(filter(write, writeSet)));
    output->push(::Dynamic(filter(others, errorSet)));
    return output;
#else
    (void)read; (void)write; (void)others; (void)timeout; return nullptr;
#endif
}

::Dynamic _hx_std_process_run(::String command,
                              ::Array<::String> arguments) {
#ifdef _WIN32
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE stdinRead = nullptr;
    HANDLE stdoutWrite = nullptr;
    HANDLE stderrWrite = nullptr;
    auto process = std::make_unique<NativeProcess>();
    if (!CreatePipe(&stdinRead, &process->stdinWrite, &security, 0) ||
        !CreatePipe(&process->stdoutRead, &stdoutWrite, &security, 0) ||
        !CreatePipe(&process->stderrRead, &stderrWrite, &security, 0))
        nativeFailure("unable to create process pipes");
    SetHandleInformation(process->stdinWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(process->stdoutRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(process->stderrRead, HANDLE_FLAG_INHERIT, 0);

    std::wstring commandLine = quoteProcessArgument(nativePath(command).wstring());
    if (arguments) {
        for (int index = 0; index < arguments->length; ++index) {
            commandLine.push_back(L' ');
            commandLine += quoteProcessArgument(
                nativePath(arguments->__get(index)).wstring());
        }
    }
    std::vector<wchar_t> writable(commandLine.begin(), commandLine.end());
    writable.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = stdinRead;
    startup.hStdOutput = stdoutWrite;
    startup.hStdError = stderrWrite;
    BlockingScope blocking;
    const BOOL created = CreateProcessW(nullptr, writable.data(), nullptr,
        nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
        &process->information);
    CloseHandle(stdinRead);
    CloseHandle(stdoutWrite);
    CloseHandle(stderrWrite);
    if (!created) {
        CloseHandle(process->stdinWrite);
        CloseHandle(process->stdoutRead);
        CloseHandle(process->stderrRead);
        nativeFailure("process creation failed: " +
                      std::to_string(GetLastError()));
    }
    NativeProcess* raw = process.get();
    std::scoped_lock guard(processesMutex);
    processes.push_back(std::move(process));
    return ::Dynamic::fromNativePointer(raw);
#else
    (void)command; (void)arguments; nativeFailure("processes unsupported");
#endif
}

int _hx_std_process_pid(::Dynamic value) {
#ifdef _WIN32
    return static_cast<int>(decodeProcess(value)->information.dwProcessId);
#else
    (void)value; return -1;
#endif
}

::Dynamic _hx_std_process_exit(::Dynamic value, bool block) {
#ifdef _WIN32
    NativeProcess* process = decodeProcess(value);
    if (block) {
        BlockingScope blocking;
        WaitForSingleObject(process->information.hProcess, INFINITE);
    }
    DWORD code = STILL_ACTIVE;
    if (!GetExitCodeProcess(process->information.hProcess, &code))
        nativeFailure("unable to query process exit code");
    return code == STILL_ACTIVE ? ::Dynamic() :
        ::Dynamic(static_cast<int>(code));
#else
    (void)value; (void)block; return ::Dynamic();
#endif
}

void _hx_std_process_close(::Dynamic value) {
#ifdef _WIN32
    auto* process = static_cast<NativeProcess*>(value.nativePointer());
    if (process == nullptr || process->closed)
        return;
    if (process->stdinWrite) CloseHandle(process->stdinWrite);
    if (process->stdoutRead) CloseHandle(process->stdoutRead);
    if (process->stderrRead) CloseHandle(process->stderrRead);
    if (process->information.hThread) CloseHandle(process->information.hThread);
    if (process->information.hProcess) CloseHandle(process->information.hProcess);
    process->stdinWrite = process->stdoutRead = process->stderrRead = nullptr;
    process->information = {};
    process->closed = true;
#else
    (void)value;
#endif
}

void _hx_std_process_kill(::Dynamic value) {
#ifdef _WIN32
    NativeProcess* process = decodeProcess(value);
    if (!TerminateProcess(process->information.hProcess, 1))
        nativeFailure("unable to terminate process");
#else
    (void)value;
#endif
}

#ifdef _WIN32
static int processRead(::Dynamic value, HANDLE NativeProcess::*member,
                       ::Array<unsigned char> bytes, int position,
                       int length) {
    if (!bytes || position < 0 || length < 0 ||
        position > bytes->length - length)
        nativeFailure("invalid process read range");
    NativeProcess* process = decodeProcess(value);
    DWORD count = 0;
    BlockingScope blocking;
    if (!ReadFile(process->*member, bytes->getBase() + position,
                  static_cast<DWORD>(length), &count, nullptr)) {
        const DWORD error = GetLastError();
        if (error == ERROR_BROKEN_PIPE)
            return 0;
        nativeFailure("process pipe read failed: " + std::to_string(error));
    }
    return static_cast<int>(count);
}
#else
static int processRead(::Dynamic value, ::Array<unsigned char> bytes,
                       int position, int length) {
    (void)value;
    (void)bytes;
    (void)position;
    (void)length;
    return 0;
}
#endif

int _hx_std_process_stdout_read(::Dynamic process,
    ::Array<unsigned char> bytes, int position, int length) {
#ifdef _WIN32
    return processRead(process, &NativeProcess::stdoutRead,
                       bytes, position, length);
#else
    return processRead(process, bytes, position, length);
#endif
}

int _hx_std_process_stderr_read(::Dynamic process,
    ::Array<unsigned char> bytes, int position, int length) {
#ifdef _WIN32
    return processRead(process, &NativeProcess::stderrRead,
                       bytes, position, length);
#else
    return processRead(process, bytes, position, length);
#endif
}

void _hx_std_process_stdin_close(::Dynamic value) {
#ifdef _WIN32
    NativeProcess* process = decodeProcess(value);
    if (process->stdinWrite != nullptr) {
        CloseHandle(process->stdinWrite);
        process->stdinWrite = nullptr;
    }
#else
    (void)value;
#endif
}

int _hx_std_process_stdin_write(::Dynamic value,
    ::Array<unsigned char> bytes, int position, int length) {
#ifdef _WIN32
    if (!bytes || position < 0 || length < 0 ||
        position > bytes->length - length)
        nativeFailure("invalid process write range");
    NativeProcess* process = decodeProcess(value);
    DWORD count = 0;
    BlockingScope blocking;
    if (!WriteFile(process->stdinWrite, bytes->getBase() + position,
                   static_cast<DWORD>(length), &count, nullptr))
        nativeFailure("process pipe write failed: " +
                      std::to_string(GetLastError()));
    return static_cast<int>(count);
#else
    (void)value; (void)bytes; (void)position; (void)length; return 0;
#endif
}
