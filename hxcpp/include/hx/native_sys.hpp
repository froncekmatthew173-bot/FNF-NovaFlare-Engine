#pragma once

double __time_stamp() noexcept;

::Dynamic _hx_inflate_init(::Dynamic windowBits);
::Dynamic _hx_inflate_buffer(::Dynamic handle,
    const ::Array<unsigned char>& source, int sourcePosition,
    const ::Array<unsigned char>& destination, int destinationPosition);
void _hx_inflate_end(::Dynamic handle);
::Dynamic _hx_deflate_init(int level);
::Dynamic _hx_deflate_buffer(::Dynamic handle,
    const ::Array<unsigned char>& source, int sourcePosition,
    const ::Array<unsigned char>& destination, int destinationPosition);
void _hx_deflate_end(::Dynamic handle);
int _hx_deflate_bound(::Dynamic handle, int sourceLength);
void _hx_zip_set_flush_mode(::Dynamic handle, ::String mode);

::String _hx_std_file_contents_string(::String path);
::Array<unsigned char> _hx_std_file_contents_bytes(::String path);
::Dynamic _hx_std_file_open(::String path, ::String mode);
int _hx_std_file_read_char(::Dynamic file);
int _hx_std_file_read(::Dynamic file, ::Array<unsigned char> bytes,
                      int position, int length);
void _hx_std_file_write_char(::Dynamic file, int value);
int _hx_std_file_write(::Dynamic file, ::Array<unsigned char> bytes,
                       int position, int length);
void _hx_std_file_flush(::Dynamic file);
void _hx_std_file_close(::Dynamic file);
void _hx_std_file_seek(::Dynamic file, int position, int origin);
int _hx_std_file_tell(::Dynamic file);
bool _hx_std_file_eof(::Dynamic file);
::Dynamic _hx_std_file_stdin();
::Dynamic _hx_std_file_stdout();
::Dynamic _hx_std_file_stderr();

bool _hx_std_sys_exists(::String path);
void _hx_std_sys_rename(::String path, ::String destination);
::Dynamic _hx_std_sys_stat(::String path);
::String _hx_std_file_full_path(::String path);
::String _hx_std_sys_file_type(::String path);
bool _hx_std_sys_create_dir(::String path, int mode);
void _hx_std_file_delete(::String path);
void _hx_std_sys_remove_dir(::String path);
::Array<::String> _hx_std_sys_read_dir(::String path);
int _hx_std_sys_getch(bool echo);
::String _hx_std_get_env(::String name);
void _hx_std_put_env(::String name, ::String value);
void _hx_std_sys_sleep(double seconds);
bool _hx_std_set_time_locale(::String locale);
::String _hx_std_get_cwd();
void _hx_std_set_cwd(::String path);
::String _hx_std_sys_string();
int _hx_std_sys_command(::String command);
double _hx_std_sys_time();
double _hx_std_sys_cpu_time();
::String _hx_std_sys_exe_path();
::Array<::String> _hx_std_sys_env();

void _hx_std_socket_init();
int _hx_std_host_resolve(::String name);
::Array<unsigned char> _hx_std_host_resolve_ipv6(::String name);
::String _hx_std_host_to_string(int address);
::String _hx_std_host_to_string_ipv6(::Array<unsigned char> address);
::String _hx_std_host_reverse(int address);
::String _hx_std_host_reverse_ipv6(::Array<unsigned char> address);
::String _hx_std_host_local();
::Dynamic _hx_std_socket_new(bool udp, bool ipv6 = false);
void _hx_std_socket_close(::Dynamic socket);
std::uintptr_t _hx_std_socket_native_handle(::Dynamic socket);
::Array<unsigned char> _hx_std_socket_read(::Dynamic socket);
void _hx_std_socket_write(::Dynamic socket, ::Array<unsigned char> bytes);
void _hx_std_socket_connect(::Dynamic socket, int address, int port);
void _hx_std_socket_connect_ipv6(::Dynamic socket,
                                 ::Array<unsigned char> address, int port);
void _hx_std_socket_listen(::Dynamic socket, int backlog);
void _hx_std_socket_shutdown(::Dynamic socket, bool read, bool write);
void _hx_std_socket_bind(::Dynamic socket, int address, int port);
void _hx_std_socket_bind_ipv6(::Dynamic socket,
                              ::Array<unsigned char> address, int port);
::Dynamic _hx_std_socket_accept(::Dynamic socket);
::Dynamic _hx_std_socket_peer(::Dynamic socket);
::Dynamic _hx_std_socket_host(::Dynamic socket);
void _hx_std_socket_set_timeout(::Dynamic socket, double timeout);
void _hx_std_socket_set_blocking(::Dynamic socket, bool blocking);
void _hx_std_socket_set_fast_send(::Dynamic socket, bool fast);
::cpp::VirtualArray _hx_std_socket_select(::Array<::Dynamic> read,
    ::Array<::Dynamic> write, ::Array<::Dynamic> others, ::Dynamic timeout);
int _hx_std_socket_send_char(::Dynamic socket, int value);
int _hx_std_socket_send(::Dynamic socket, ::Array<unsigned char> bytes,
                        int position, int length);
int _hx_std_socket_recv_char(::Dynamic socket);
int _hx_std_socket_recv(::Dynamic socket, ::Array<unsigned char> bytes,
                        int position, int length);

::Dynamic _hx_std_process_run(::String command, ::Array<::String> arguments);
int _hx_std_process_pid(::Dynamic process);
::Dynamic _hx_std_process_exit(::Dynamic process, bool block);
void _hx_std_process_close(::Dynamic process);
void _hx_std_process_kill(::Dynamic process);
int _hx_std_process_stdout_read(::Dynamic process,
    ::Array<unsigned char> bytes, int position, int length);
int _hx_std_process_stderr_read(::Dynamic process,
    ::Array<unsigned char> bytes, int position, int length);
void _hx_std_process_stdin_close(::Dynamic process);
int _hx_std_process_stdin_write(::Dynamic process,
    ::Array<unsigned char> bytes, int position, int length);

::Dynamic _hx_ssl_init();
::Dynamic _hx_ssl_conf_new(bool server);
::Dynamic _hx_ssl_conf_close(::Dynamic config);
::Dynamic _hx_ssl_conf_set_cert(::Dynamic config, ::Dynamic certificate,
                                ::Dynamic key);
::Dynamic _hx_ssl_conf_set_ca(::Dynamic config, ::Dynamic certificate);
::Dynamic _hx_ssl_conf_set_verify(::Dynamic config, int mode);
::Dynamic _hx_ssl_conf_set_servername_callback(::Dynamic config,
                                               ::Dynamic callback);
::Dynamic _hx_ssl_new(::Dynamic config);
::Dynamic _hx_ssl_set_socket(::Dynamic ssl, ::Dynamic socket);
::Dynamic _hx_ssl_set_hostname(::Dynamic ssl, ::String hostname);
::Dynamic _hx_ssl_handshake(::Dynamic ssl);
::Dynamic _hx_ssl_read(::Dynamic ssl);
::Dynamic _hx_ssl_write(::Dynamic ssl, ::Array<unsigned char> bytes);
::Dynamic _hx_ssl_close(::Dynamic ssl);
::Dynamic _hx_ssl_get_peer_certificate(::Dynamic ssl);
int _hx_ssl_recv_char(::Dynamic ssl);
int _hx_ssl_recv(::Dynamic ssl, ::Array<unsigned char> bytes,
                 int position, int length);
::Dynamic _hx_ssl_send_char(::Dynamic ssl, int value);
int _hx_ssl_send(::Dynamic ssl, ::Array<unsigned char> bytes,
                 int position, int length);
::Dynamic _hx_ssl_key_from_pem(::String data, bool isPublic, ::String pass);
::Dynamic _hx_ssl_key_from_der(::Array<unsigned char> data, bool isPublic);
::Array<unsigned char> _hx_ssl_dgst_make(::Array<unsigned char> data,
                                         ::String algorithm);
::Array<unsigned char> _hx_ssl_dgst_sign(::Array<unsigned char> data,
                                         ::Dynamic key,
                                         ::String algorithm);
bool _hx_ssl_dgst_verify(::Array<unsigned char> data,
                         ::Array<unsigned char> signature,
                         ::Dynamic key, ::String algorithm);
::Dynamic _hx_ssl_cert_get_altnames(::Dynamic certificate);
::String _hx_ssl_cert_get_subject(::Dynamic certificate, ::String field);
::String _hx_ssl_cert_get_issuer(::Dynamic certificate, ::String field);
::Array<int> _hx_ssl_cert_get_notbefore(::Dynamic certificate);
::Array<int> _hx_ssl_cert_get_notafter(::Dynamic certificate);
::Dynamic _hx_ssl_cert_get_next(::Dynamic certificate);
::Dynamic _hx_ssl_cert_add_pem(::Dynamic certificate, ::String pem);
::Dynamic _hx_ssl_cert_add_der(::Dynamic certificate,
                               ::Array<unsigned char> der);
::Dynamic _hx_ssl_cert_load_file(::String path);
::Dynamic _hx_ssl_cert_load_path(::String path);
::Dynamic _hx_ssl_cert_load_defaults();
