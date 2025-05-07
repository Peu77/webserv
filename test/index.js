const net = require('net');

const client = new net.Socket();
client.connect(8080, '127.0.0.1', async function () {
    console.log('Connected');
    client.write('POST / HTTP/1.1\r\n');
    client.write("Host: 127.0.0.1:8080\r\n")
    client.write("Connection: keep-alive\r\n");
    client.write('Transfer-Encoding: chunked\r\n');
    client.write("\r\n");

    client.write("4\r\n");
    await new Promise(resolve => setTimeout(resolve, 1000));
    client.write("Wi");
    client.write("st\r\n");
    client.write("0\r\n");

});

client.on('data', function (data) {
    console.log('Received: ' + data);
});

client.on('close', function () {
    console.log('Connection closed');
});