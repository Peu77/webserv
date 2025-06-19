const request = require('supertest');
const net = require("net");
const customSocket = require('./customSocket');

const url = 'http://localhost:8080';
describe('test headers', function () {
    it('can connect', function (done) {
        request(url)
            .get('/')
            .expect(200)
            .expect('Hello, World!', done)
    });

    // the client_max_header_count is set to 1 in the server config in the http part
    // so we expect a 400 Bad Request if we send more than one header
    // because the server is with the new client_max_header_count is only after the host header is found
    it('host header not first place', async function () {
        const data = await customSocket("localhost", 8080,
            'GET / HTTP/1.1\r\n' +
            'Connection: close\r\n' +
            'Host: 127.0.0.1:8080\r\n' +
            '\r\n' +
            '\r\n'
        )

        if (!data.includes('HTTP/1.1 400 Bad Request')) {
            throw new Error("Expected 400 Bad Request, got: " + data);
        }
    });

    it("lower as max header count", async function () {
        const req = request(url)
            .get('/')

        for (let i = 0; i < 15; i++) {
            req.set(`test${i}`, `test${i}`);
        }
        await req.expect(200);
    })

    it("too many headers", async function () {
        const req = request(url)
            .get('/')

        for (let i = 0; i < 21; i++) {
            req.set(`test${i}`, `test${i}`);
        }
        await req.expect(400);
    })

    it("too long header", function (done) {
        const longHeader = 'A'.repeat(1126);
        request(url)
            .get('/')
            .set("test", longHeader)
            .expect(400, done)
    })

    it("good header", function (done) {
        const longHeader = 'A'.repeat(500);
        request(url)
            .get('/')
            .set("test", longHeader)
            .expect(200, done)
    })


    it("too large request line", async function () {
        const longRequestLine = 'A'.repeat(103);
        const data = await customSocket("localhost", 8080,
            `GET /${longRequestLine} HTTP/1.1\r` +
            'Connection: close\r\n ' +
            'Host: 127.0.0.1:8080\r\n ' +
            '\r\n ' +
            '\r\n '
        )

        if (!data.includes('414 Request URI Too Long')) {
            throw new Error("Expected 414 Request URI Too Long, got: " + data);
        }
    })

    it("duplicated host header", async function () {
        const content = 'GET / HTTP/1.1\r\n' +
            'Host: localhost\r\n' +
            'Connection: close\r\n' +
            'Host: localhost:8080\r\n' +
            '\r\n' +
            '\r\n'

        const data = await customSocket("localhost", 8080, content)
        if (!data.includes('HTTP/1.1 400 Bad Request')) {
            throw new Error("Expected 400 Bad Request, got: " + data);
        }
    })

    it("wrong formated header", async function () {
        const content = 'GET / HTTP/1.1\r\n' +
            'Host: localhost\r\n' +
            'Connection: close\r\n' +
            'test localhost:8080\r\n' + // missing colon
            '\r\n' +
            '\r\n'

        const data = await customSocket("localhost", 8080, content)
        if (!data.includes('HTTP/1.1 400 Bad Request')) {
            throw new Error("Expected 400 Bad Request, got: " + data);
        }
    })

// Test when both Content-Length and Transfer-Encoding headers are present
    it("both content-length and transfer-encoding present", async function () {
        const content = 'GET / HTTP/1.1\r\n' +
            'Host: localhost:8080\r\n' +
            'Content-Length: 10\r\n' +
            'Transfer-Encoding: chunked\r\n' +
            'Connection: close\r\n' +
            '\r\n' +
            '\r\n';

        const data = await customSocket("localhost", 8080, content);
        if (!data.includes('HTTP/1.1 400 Bad Request')) {
            throw new Error("Expected 400 Bad Request, got: " + data);
        }
    });

// Test with extremely large Content-Length
    it("extremely large content-length", async function () {
        const content = 'GET / HTTP/1.1\r\n' +
            'Host: localhost:8080\r\n' +
            'Content-Length: 999999999999999999999\r\n' +
            'Connection: close\r\n' +
            '\r\n' +
            '\r\n';

        const data = await customSocket("localhost", 8080, content);
        if (!data.includes('HTTP/1.1 400 Bad Request')) {
            throw new Error("Expected 400 Bad Request, got: " + data);
        }
    });

// Test with invalid Transfer-Encoding value
    it("invalid transfer-encoding value", async function () {
        const content = 'GET / HTTP/1.1\r\n' +
            'Host: localhost:8080\r\n' +
            'Transfer-Encoding: invalid-value\r\n' +
            'Connection: close\r\n' +
            '\r\n' +
            '\r\n';

        const data = await customSocket("localhost", 8080, content);
        if (!data.includes('HTTP/1.1 501 Not Implemented')) {
            throw new Error("Expected 501 Not Implemented, got: " + data);
        }
    });

// Test header with empty value
    it("header with empty value", async function () {
        const content = 'GET / HTTP/1.1\r\n' +
            'Host: localhost:8080\r\n' +
            'EmptyHeader:\r\n' +
            'Connection: close\r\n' +
            '\r\n' +
            '\r\n';

        const data = await customSocket("localhost", 8080, content);
        // This could be 200 or 400 depending on implementation
        // Most servers accept empty values, so we'll test for 200
        if (!data.includes('HTTP/1.1 200 OK')) {
            throw new Error("Expected 200 OK, got: " + data);
        }
    });

// Test header with invalid characters
    it("header with invalid characters", async function () {
        const content = 'GET / HTTP/1.1\r\n' +
            'Host: localhost:8080\r\n' +
            'X-Test: \x01\x02\x03\r\n' +
            'Connection: close\r\n' +
            '\r\n' +
            '\r\n';

        const data = await customSocket("localhost", 8080, content);
        if (!data.includes('HTTP/1.1 400 Bad Request')) {
            throw new Error("Expected 400 Bad Request, got: " + data);
        }
    });

// Test with invalid chunked encoding format
    it("invalid chunked transfer-encoding format", async function () {
        const content = 'POST / HTTP/1.1\r\n' +
            'Host: localhost:8080\r\n' +
            'Transfer-Encoding: chunked\r\n' +
            'Connection: close\r\n' +
            '\r\n' +
            'INVALID\r\n' + // Invalid chunk size (not hex)
            'data\r\n' +
            '0\r\n' +
            '\r\n';

        const data = await customSocket("localhost", 8080, content);
        if (!data.includes('HTTP/1.1 400 Bad Request')) {
            throw new Error("Expected 400 Bad Request, got: " + data);
        }
    });


// Test too much data in body compared to Content-Length
    it("body larger than content-length", async function () {
        const content = 'POST / HTTP/1.1\r\n' +
            'Host: localhost:8080\r\n' +
            'Content-Length: 5\r\n' +
            'Connection: close\r\n' +
            '\r\n' +
            'HelloWorld'; // 10 bytes but Content-Length says 5

        const data = await customSocket("localhost", 8080, content);
        if (!data.includes('HTTP/1.1 400 Bad Request')) {
            throw new Error("Expected 400 Bad Request, got: " + data);
        }
    });

// Test with negative Content-Length
    it("negative content-length", async function () {
        const content = 'POST / HTTP/1.1\r\n' +
            'Host: localhost:8080\r\n' +
            'Content-Length: -5\r\n' +
            'Connection: close\r\n' +
            '\r\n' +
            'Hello';

        const data = await customSocket("localhost", 8080, content);
        if (!data.includes('HTTP/1.1 400 Bad Request')) {
            throw new Error("Expected 400 Bad Request, got: " + data);
        }
    });
});


