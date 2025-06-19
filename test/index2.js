import customSocket from "./customSocket.js";

const content = 'POST / HTTP/1.1\r\n' +
    'Host: localhost:8080\r\n' +
    'Transfer-Encoding: chunked\r\n' +
    'Connection: close\r\n' +
    '\r\n' +
    'A\r\n' + // Declares 10 bytes
    'Hello\r\n' + // Only 5 bytes
    '0\r\n' +
    '\r\n';

const data = await customSocket("localhost", 8080, content);
console.log(data)