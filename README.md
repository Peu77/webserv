

# Webserv


This project is about writing our own HTTP/1.1-compliant web server in C++, with non-blocking I/O and support for various web server features. It is part of the 42 School curriculum and follows strict coding standards.


## Features

-


## Run Locally

Clone the project

```bash
git clone https://github.com/v1dusss/webserv.git
```

Go to the project directory

```bash
cd webserv
```

Start the server

```bash
make
./webserv config.yaml
```


## Configuration

The server is configured using a custom nginx-style configuration file.


### Simple Example

```nginx
 http{
    client_max_header_count 100;
    client_max_header_size 1MB;

    server {
        listen                 8080;
        server_name            localhost;
        client_max_body_size   200MB;
        client_max_header_size 10KB;
    
    location /{
        return 200 "Hello, World!";
    }
  }
 }
```


### HTTP Options

Specified within the `http` block. The server will use the first matching HTTP options.

| directive                  | description                             | example           |
| -------------------------- | --------------------------------------- | ----------------- |
| `client_max_header_count`  | maximum number of headers               | `100`             |
| `client_max_header_size`   | maximum header size                     | `1MB`             |
| `client_header_timeout`  | timeout for client header               | `10`              |
| `max_request_line_size`    | maximum request line size               | `1MB`             |
| `server`                  | server block                             | `server {...}`    |


### Server Options

Specified within a `server` block. The server will use all matching server options.

_Defining multiple server blocks with identical `listen` and `server_name` values is prohibited and will cause a configuration parsing failure._

| directive                 | description                            | example            |
|---------------------------| -------------------------------------- |--------------------|
| `listen`                  | ip and/or port to listen on            | `0.0.0.0:8080`     |
| `server_name`             | server name                            | `localhost`        |
| `root`                    | root directory                         | `/www`             |
| `index`                   | default index file                     | `/index.html`      |
| `client_max_body_size`    | maximum body size                      | `1024MB`           |
| `client_body_timeout`     | timeout for client body                | `10`               |
| `client_header_timeout`   | timeout for client header              | `10`               |
| `client_max_header_size`  | maximum header size                    | `10KB`             |
| `client_max_header_count` | maximum number of headers              | `100`              |
| `cgi_timeout`             | timeout for CGI scripts                | `10`               |
| `keepalive_timeout`       | timeout for keepalive connections      | `10`               |
| `keepalive_requests`      | maximum number of keepalive requests   | `100`              |
| `error_page`              | custom error page (`<code> <filepath>`) | `404 /404.html`    |
| `internal_api`            | enable internal API                    | `on`               |
| `location`                | location block                          | `location / {...}` |


### Location/Route Options

Specified within a `location` block. The server will use all matching location options.

| directive       | description                                                                           | example            |
| --------------- |---------------------------------------------------------------------------------------|--------------------|
| `root`          | root directory                                                                        | `/www`             |
| `index`         | default index file                                                                    | `/index.html`      |
| `autoindex`     | enable autoindex                                                                      | `on`               |
| `alias`         | alias for the location                                                                | `/alias`           |
| `allow_methods` | allowed methods                                                                       | `GET POST DELETE`  |
| `deny`          | deny access to the location                                                           | `on`               |
| `error_page`   | custom error page (`<code> <filepath>`)                                               | `404 /404.html`    |
| `return`        | costom return code and message (`<code> <message>`), <br/>can be  usesd for redirects | `404 Not Found`    |
| `cgi`           | cgi script (`<ext> <path>`)                                                           | `.php /usr/bin/php` |


## Authors

- [Emil Ebert](https://github.com/Peu77)
- [Vidus Sivanathan](https://github.com/v1dusss)
- [Daniel Ilin](https://github.com/ilindaniel)
