## rabbitmq

This plugin allows you to publish messages to RabbitMQ from HSL.

### Params

- message_body `string` (**required**)
- options `array` 
    - hostname `string` (default `"localhost"`)
    - port `number` (default `5672`)
    - connect_timeout `number` (default `10`)
    - vhost `string` (default `"/"`)
    - username `string` (default `"guest"`)
    - password `string` (default `"guest"`)
    - exchange `string` (default `"amq.direct"`)
    - routing_key `string` (default `""`)
    - content_type `string` (default `"text/plain"`)
    - tls_enabled `boolean` (default `false`)
    - tls_verify_peer `boolean` (default `false`)
    - tls_verify_host `boolean` (default `false`)

### Returns

An associative array with a `result` key (if the message was successfully published) or a `error` key (if an error occurred).

### Example with all default options

```
rabbitmq_publish("hello world", [
    "hostname" => "localhost",
    "port" => 5672,
    "connect_timeout" => 10,
    "vhost" => "/",
    "username" => "guest",
    "password" => "guest",
    "exchange" => "amq.direct",
    "routing_key" => "",
    "content_type" => "text/plain",
    "tls_enabled" => false,
    "tls_verify_peer" => false,
    "tls_verify_host" => false
]);
```
