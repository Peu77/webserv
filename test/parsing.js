const request = require('supertest');
const net = require("net");
const customSocket = require('./customSocket');

describe('parsing', function () {
    // Test with incomplete chunked data
    it("incomplete chunked data", async function () {
        const content = 'POST / HTTP/1.1\r\n' +
            'Host: localhost:8080\r\n' +
            'Transfer-Encoding: chunked\r\n' +
            'Connection: close\r\n' +
            '\r\n' +
            '5\r\n' + // Chunk size
            'Hello' + // Missing chunk end CRLF
            '0\r\n' +
            '\r\n';

        const data = await customSocket("localhost", 8080, content);
        if (!data.includes('HTTP/1.1 400 Bad Request')) {
            throw new Error("Expected 400 Bad Request, got: " + data);
        }
    });

// Test with missing chunk terminator
    it("missing chunk terminator", async function () {
        const content = 'POST / HTTP/1.1\r\n' +
            'Host: localhost:8080\r\n' +
            'Transfer-Encoding: chunked\r\n' +
            'Connection: close\r\n' +
            '\r\n' +
            '5\r\n' +
            'Hello\r\n' +
            // Missing final "0\r\n\r\n"
            '\r\n';

        const data = await customSocket("localhost", 8080, content);
        if (!data.includes('HTTP/1.1 400 Bad Request')) {
            throw new Error("Expected 400 Bad Request, got: " + data);
        }
    });

    // Test with malformed chunked encoding
    it("malformed chunked encoding", async function () {
        const content = 'GET / HTTP/1.1\r\n' +
            'Host: localhost:8080\r\n' +
            'Transfer-Encoding: chunked\r\n' +
            'Connection: close\r\n' +
            '\r\n' +
            '5\r\n' +
            'Hello\r\n' +
            'x\r\n' + // Invalid hex chunk size
            'World\r\n' +
            '0\r\n' +
            '\r\n';

        const data = await customSocket("localhost", 8080, content);
        if (!data.includes('HTTP/1.1 400 Bad Request')) {
            throw new Error("Expected 400 Bad Request, got: " + data);
        }
    });

// Test with chunk size containing trailing garbage
    it("chunk size with trailing garbage", async function () {
        const content = 'POST / HTTP/1.1\r\n' +
            'Host: localhost:8080\r\n' +
            'Transfer-Encoding: chunked\r\n' +
            'Connection: close\r\n' +
            '\r\n' +
            '5xyz\r\n' + // Invalid chunk size format (hex + garbage)
            'Hello\r\n' +
            '0\r\n' +
            '\r\n';

        const data = await customSocket("localhost", 8080, content);
        if (!data.includes('HTTP/1.1 400 Bad Request')) {
            throw new Error("Expected 400 Bad Request, got: " + data);
        }
    });

// Test with incorrect chunk length (shorter than declared)
    it("chunk length shorter than declared", async function () {
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
        if (!data.includes('HTTP/1.1 408 Request Timeout')) {
            throw new Error("Expected 408 Request Timeout, got: " + data);
        }
    });

// Test with incorrect line endings (only \n instead of \r\n)
    it("chunks with incorrect line endings", async function () {
        const content = 'POST / HTTP/1.1\r\n' +
            'Host: localhost:8080\r\n' +
            'Transfer-Encoding: chunked\r\n' +
            'Connection: close\r\n' +
            '\r\n' +
            '5\n' + // Missing \r
            'Hello\n' + // Missing \r
            '0\n' + // Missing \r
            '\n'; // Missing \r

        const data = await customSocket("localhost", 8080, content);
        if (!data.includes('HTTP/1.1 408 Request Timeout')) {
            throw new Error("Expected 408 Request Timeout, got: " + data);
        }
    });

// Test with extremely large chunk size
    it("extremely large chunk size", async function () {
        const content = 'POST / HTTP/1.1\r\n' +
            'Host: localhost:8080\r\n' +
            'Transfer-Encoding: chunked\r\n' +
            'Connection: close\r\n' +
            '\r\n' +
            'FFFFFFFFFFFFFFFF\r\n' + // Massive chunk size (could cause overflow)
            'Hello\r\n' +
            '0\r\n' +
            '\r\n';

        const data = await customSocket("localhost", 8080, content);
        if (!data.includes('HTTP/1.1 400 Bad Request')) {
            throw new Error("Expected 400 Bad Request, got: " + data);
        }
    });

// Test with multiple terminating chunks
    it("multiple terminating chunks", async function () {
        const content = 'GET / HTTP/1.1\r\n' +
            'Host: localhost:8080\r\n' +
            'Transfer-Encoding: chunked\r\n' +
            'Connection: close\r\n' +
            '\r\n' +
            '5\r\n' +
            'Hello\r\n' +
            '0\r\n' +
            '\r\n' +
            '0\r\n' + // Second terminating chunk
            '\r\n';

        const data = await customSocket("localhost", 8080, content);
        if (!data.includes('HTTP/1.1 400 Bad Request')) {
            throw new Error("Expected 400 Bad Request, got: " + data);
        }
    });
})

