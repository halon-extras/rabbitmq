#include <sys/time.h>
#include <HalonMTA.h>
#include <amqp.h>
#include <amqp_tcp_socket.h>
#include <amqp_ssl_socket.h>
#include <cstring>
#include <string>
#include <list>
#include <mutex>

struct open_conn_t
{
    amqp_connection_state_t conn;
    std::string hostname;
    int port;
    std::string vhost;
};

std::list<open_conn_t> open_conn_list;
std::mutex lock;

HALON_EXPORT
int Halon_version()
{
    return HALONMTA_PLUGIN_VERSION;
}

void set_ret_error(HalonHSLValue* ret, char const *value)
{
    HalonHSLValue *error_key, *error_value;
    HalonMTA_hsl_value_array_add(ret, &error_key, &error_value);
    HalonMTA_hsl_value_set(error_key, HALONMTA_HSL_TYPE_STRING, "error", 0);
    HalonMTA_hsl_value_set(error_value, HALONMTA_HSL_TYPE_STRING, value, 0);
}

void set_ret_result(HalonHSLValue* ret, char const *value)
{
    HalonHSLValue *result_key, *result_value;
    HalonMTA_hsl_value_array_add(ret, &result_key, &result_value);
    HalonMTA_hsl_value_set(result_key, HALONMTA_HSL_TYPE_STRING, "result", 0);
    HalonMTA_hsl_value_set(result_value, HALONMTA_HSL_TYPE_STRING, value, 0);
}

bool open_connection(
    amqp_connection_state_t &conn,
    char const *hostname,
    int port,
    int connect_timeout,
    char const *vhost,
    char const *username,
    char const *password,
    bool tls_enabled,
    bool tls_verify_peer,
    bool tls_verify_host,
    std::string &error
) {
    conn = amqp_new_connection();
    if (!conn) {
        error = "failed to allocate and initialize connection object";
        return false;
    }

    amqp_socket_t *socket;
    if (tls_enabled) {
        socket = amqp_ssl_socket_new(conn);
    } else {
        socket = amqp_tcp_socket_new(conn);
    }
    if (!socket) {
        error = "failed to create tcp socket";
        amqp_destroy_connection(conn);
        return false;
    }

    if (tls_enabled) {
        amqp_set_initialize_ssl_library(false);
        amqp_ssl_socket_set_verify_peer(socket, tls_verify_peer);
        amqp_ssl_socket_set_verify_hostname(socket, tls_verify_host);
    }

    struct timeval tval;
    struct timeval *tv;
    if (connect_timeout > 0) {
        tv = &tval;
        tv->tv_sec = connect_timeout;
        tv->tv_usec = 0;
    } else {
        tv = NULL;
    }

    int status;
    amqp_rpc_reply_t reply;

    status = amqp_socket_open_noblock(socket, hostname, port, tv);
    if (status != AMQP_STATUS_OK) {
        error = "failed to open socket connection";
        amqp_destroy_connection(conn);
        return false;
    }

    reply = amqp_login(conn, vhost, 0, 131072, 0, AMQP_SASL_METHOD_PLAIN, username, password);
    if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
        error = "failed to login to the broker";
        amqp_destroy_connection(conn);
        return false;
    }

    amqp_channel_open(conn, 1);
    reply = amqp_get_rpc_reply(conn);
    if (reply.reply_type != AMQP_RESPONSE_NORMAL) {
        error = "failed to open channel";
        amqp_destroy_connection(conn);
        return false;
    }

    open_conn_t open_conn;
    open_conn.conn = conn;
    open_conn.hostname = hostname;
    open_conn.port = port;
    open_conn.vhost = vhost;
    open_conn_list.push_back(open_conn);

    return true;
}

void remove_connection(char const *hostname, int port, char const *vhost)
{
    for (auto open_conn = open_conn_list.begin(); open_conn != open_conn_list.end(); ) {
        if (open_conn->hostname == hostname && open_conn->port == port && open_conn->vhost == vhost) {
            open_conn_list.erase(open_conn++);
        } else {
            ++open_conn;
        }
    }
}

void rabbitmq_publish(HalonHSLContext* hhc, HalonHSLArguments* args, HalonHSLValue* ret)
{
    char const *message_body = "";
    char const *hostname = "localhost";
    int port = 5672;
    int connect_timeout = 10;
    char const *vhost = "/";
    char const *username = "guest";
    char const *password = "guest";
    char const *exchange = "amq.direct";
    char const *routing_key = "";
    char const *content_type = "text/plain";
    amqp_delivery_mode_enum delivery_mode = AMQP_DELIVERY_NONPERSISTENT;
    bool tls_enabled = false;
    bool tls_verify_peer = false;
    bool tls_verify_host = false;

    HalonHSLValue* message_body_argument;
    message_body_argument = HalonMTA_hsl_argument_get(args, 0);
    if (message_body_argument) {
        if (HalonMTA_hsl_value_type(message_body_argument) == HALONMTA_HSL_TYPE_STRING) {
            HalonMTA_hsl_value_get(message_body_argument, HALONMTA_HSL_TYPE_STRING, &message_body, nullptr);
        } else {
            set_ret_error(ret, "invalid message_body");
            return;
        }
    } else {
        set_ret_error(ret, "missing message_body");
        return;
    }

    HalonHSLValue* options_argument;
    options_argument = HalonMTA_hsl_argument_get(args, 1);
    if (options_argument) {
        if (HalonMTA_hsl_value_type(options_argument) == HALONMTA_HSL_TYPE_ARRAY) {
            HalonHSLValue *options_hostname = HalonMTA_hsl_value_array_find(options_argument, "hostname");
            if (options_hostname != NULL) {
                if (HalonMTA_hsl_value_type(options_hostname) == HALONMTA_HSL_TYPE_STRING) {
                    HalonMTA_hsl_value_get(options_hostname, HALONMTA_HSL_TYPE_STRING, &hostname, nullptr);
                } else {
                    set_ret_error(ret, "invalid hostname");
                    return;
                }
            }

            HalonHSLValue *options_port = HalonMTA_hsl_value_array_find(options_argument, "port");
            if (options_port != NULL) {
                double options_port_double;
                if (HalonMTA_hsl_value_type(options_port) == HALONMTA_HSL_TYPE_NUMBER) {
                    HalonMTA_hsl_value_get(options_port, HALONMTA_HSL_TYPE_NUMBER, &options_port_double, nullptr);
                    port = (int) options_port_double;
                } else {
                    set_ret_error(ret, "invalid port");
                    return;
                }
            }

            HalonHSLValue *options_connect_timeout = HalonMTA_hsl_value_array_find(options_argument, "connect_timeout");
            if (options_connect_timeout != NULL) {
                double options_connect_timeout_double;
                if (HalonMTA_hsl_value_type(options_connect_timeout) == HALONMTA_HSL_TYPE_NUMBER) {
                    HalonMTA_hsl_value_get(options_connect_timeout, HALONMTA_HSL_TYPE_NUMBER, &options_connect_timeout_double, nullptr);
                    connect_timeout = (int) options_connect_timeout_double;
                } else {
                    set_ret_error(ret, "invalid connect_timeout");
                    return;
                }
            }

            HalonHSLValue *options_vhost = HalonMTA_hsl_value_array_find(options_argument, "vhost");
            if (options_vhost != NULL) {
                if (HalonMTA_hsl_value_type(options_vhost) == HALONMTA_HSL_TYPE_STRING) {
                    HalonMTA_hsl_value_get(options_vhost, HALONMTA_HSL_TYPE_STRING, &vhost, nullptr);
                } else {
                    set_ret_error(ret, "invalid vhost");
                    return;
                }
            }

            HalonHSLValue *options_username = HalonMTA_hsl_value_array_find(options_argument, "username");
            if (options_username != NULL) {
                if (HalonMTA_hsl_value_type(options_username) == HALONMTA_HSL_TYPE_STRING) {
                    HalonMTA_hsl_value_get(options_username, HALONMTA_HSL_TYPE_STRING, &username, nullptr);
                } else {
                    set_ret_error(ret, "invalid username");
                    return;
                }
            }

            HalonHSLValue *options_password = HalonMTA_hsl_value_array_find(options_argument, "password");
            if (options_password != NULL) {
                if (HalonMTA_hsl_value_type(options_password) == HALONMTA_HSL_TYPE_STRING) {
                    HalonMTA_hsl_value_get(options_password, HALONMTA_HSL_TYPE_STRING, &password, nullptr);
                } else {
                    set_ret_error(ret, "invalid password");
                    return;
                }
            }

            HalonHSLValue *options_exchange = HalonMTA_hsl_value_array_find(options_argument, "exchange");
            if (options_exchange != NULL) {
                if (HalonMTA_hsl_value_type(options_exchange) == HALONMTA_HSL_TYPE_STRING) {
                    HalonMTA_hsl_value_get(options_exchange, HALONMTA_HSL_TYPE_STRING, &exchange, nullptr);
                } else {
                    set_ret_error(ret, "invalid exchange");
                    return;
                }
            }

            HalonHSLValue *options_routing_key = HalonMTA_hsl_value_array_find(options_argument, "routing_key");
            if (options_routing_key != NULL) {
                if (HalonMTA_hsl_value_type(options_routing_key) == HALONMTA_HSL_TYPE_STRING) {
                    HalonMTA_hsl_value_get(options_routing_key, HALONMTA_HSL_TYPE_STRING, &routing_key, nullptr);
                } else {
                    set_ret_error(ret, "invalid routing_key");
                    return;
                }
            }

            HalonHSLValue *options_content_type = HalonMTA_hsl_value_array_find(options_argument, "content_type");
            if (options_content_type != NULL) {
                if (HalonMTA_hsl_value_type(options_content_type) == HALONMTA_HSL_TYPE_STRING) {
                    HalonMTA_hsl_value_get(options_content_type, HALONMTA_HSL_TYPE_STRING, &content_type, nullptr);
                } else {
                    set_ret_error(ret, "invalid content_type");
                    return;
                }
            }

            char* _delivery_mode = nullptr;
            HalonHSLValue *options_delivery_mode = HalonMTA_hsl_value_array_find(options_argument, "delivery_mode");
            if (options_delivery_mode != NULL) {
                if (HalonMTA_hsl_value_type(options_delivery_mode) == HALONMTA_HSL_TYPE_STRING) {
                    HalonMTA_hsl_value_get(options_delivery_mode, HALONMTA_HSL_TYPE_STRING, &_delivery_mode, nullptr);
                    if (strcmp(_delivery_mode, "persistent") == 0) {
                        delivery_mode = AMQP_DELIVERY_PERSISTENT;
                    } else if (strcmp(_delivery_mode, "nonpersistent") == 0) {
                        delivery_mode = AMQP_DELIVERY_NONPERSISTENT;
                    } else {
                        set_ret_error(ret, "unsupported delivery_mode");
                        return;
                    }
                } else {
                    set_ret_error(ret, "invalid delivery_mode");
                    return;
                }
            }

            HalonHSLValue *options_tls_enabled = HalonMTA_hsl_value_array_find(options_argument, "tls_enabled");
            if (options_tls_enabled != NULL) {
                if (HalonMTA_hsl_value_type(options_tls_enabled) == HALONMTA_HSL_TYPE_BOOLEAN) {
                    HalonMTA_hsl_value_get(options_tls_enabled, HALONMTA_HSL_TYPE_BOOLEAN, &tls_enabled, nullptr);
                } else {
                    set_ret_error(ret, "invalid tls_enabled");
                    return;
                }
            }

            HalonHSLValue *options_tls_verify_peer = HalonMTA_hsl_value_array_find(options_argument, "tls_verify_peer");
            if (options_tls_verify_peer != NULL) {
                if (HalonMTA_hsl_value_type(options_tls_verify_peer) == HALONMTA_HSL_TYPE_BOOLEAN) {
                    HalonMTA_hsl_value_get(options_tls_verify_peer, HALONMTA_HSL_TYPE_BOOLEAN, &tls_verify_peer, nullptr);
                } else {
                    set_ret_error(ret, "invalid tls_verify_peer");
                    return;
                }
            }

            HalonHSLValue *options_tls_verify_host = HalonMTA_hsl_value_array_find(options_argument, "tls_verify_host");
            if (options_tls_verify_host != NULL) {
                if (HalonMTA_hsl_value_type(options_tls_verify_host) == HALONMTA_HSL_TYPE_BOOLEAN) {
                    HalonMTA_hsl_value_get(options_tls_verify_host, HALONMTA_HSL_TYPE_BOOLEAN, &tls_verify_host, nullptr);
                } else {
                    set_ret_error(ret, "invalid tls_verify_host");
                    return;
                }
            }
        } else {
            set_ret_error(ret, "invalid options");
            return;
        }
    }

    std::lock_guard<std::mutex> lg(lock);

    amqp_connection_state_t conn;

    bool f = false;
    for (const auto & open_conn : open_conn_list)
    {
        if (open_conn.hostname == hostname && open_conn.port == port && open_conn.vhost == vhost)
        {
            f = true;
            conn = open_conn.conn;
            break;
        }
    }
    if (!f)
    {
        std::string error;
        auto s = open_connection(conn, hostname, port, connect_timeout, vhost, username, password, tls_enabled, tls_verify_peer, tls_verify_host, error);
        if (!s) {
            set_ret_error(ret, error.c_str());
            return;
        }
    }

    amqp_basic_properties_t props;
    props._flags = AMQP_BASIC_CONTENT_TYPE_FLAG | AMQP_BASIC_DELIVERY_MODE_FLAG;
    props.content_type = amqp_cstring_bytes(content_type);
    props.delivery_mode = delivery_mode;

    int status;
    status = amqp_basic_publish(conn, 1, amqp_cstring_bytes(exchange), amqp_cstring_bytes(routing_key), 0, 0, &props, amqp_cstring_bytes(message_body));
    if (status == AMQP_STATUS_OK) {
        set_ret_result(ret, "published");
    } else {
        remove_connection(hostname, port, vhost);
        amqp_destroy_connection(conn);
        if (f) {
            amqp_connection_state_t conn_2;
            std::string error;
            auto s = open_connection(conn_2, hostname, port, connect_timeout, vhost, username, password, tls_enabled, tls_verify_peer, tls_verify_host, error);
            if (!s) {
                set_ret_error(ret, error.c_str());
                return;
            }
            status = amqp_basic_publish(conn_2, 1, amqp_cstring_bytes(exchange), amqp_cstring_bytes(routing_key), 0, 0, &props, amqp_cstring_bytes(message_body));
            if (status == AMQP_STATUS_OK) {
                set_ret_result(ret, "published");
            } else {
                remove_connection(hostname, port, vhost);
                amqp_destroy_connection(conn_2);
                set_ret_error(ret, "failed to publish message to broker");
                return;
            }
        } else {
            set_ret_error(ret, "failed to publish message to broker");
            return;
        }
    }
}

HALON_EXPORT
bool Halon_hsl_register(HalonHSLRegisterContext* hhrc)
{
    HalonMTA_hsl_register_function(hhrc, "rabbitmq_publish", rabbitmq_publish);
    return true;
}

HALON_EXPORT
void Halon_cleanup()
{
    std::lock_guard<std::mutex> lg(lock);
    for (const auto & open_conn : open_conn_list)
    {
        amqp_channel_close(open_conn.conn, 1, AMQP_REPLY_SUCCESS);
        amqp_connection_close(open_conn.conn, AMQP_REPLY_SUCCESS);
        amqp_destroy_connection(open_conn.conn);
    }
    open_conn_list.clear();
}