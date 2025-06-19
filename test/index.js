const net = require('net');

const client = new net.Socket();
client.connect(8080, '127.0.0.1', async function () {
    console.log('Connected');
    client.write('GET / HTTP/1.1\r\n');
    client.write("Host: 127.0.0.1:8080\r\n")
    client.write("Connection: keep-alive\r\n");
   // client.write('Transfer-Encoding: chunked\r\n');
    client.write("Content-Length: 3\r\n");

    client.write("123");


    /*
    client.write("4\r\n");
    client.write("Wi");
    client.write("fw\r\n");
    client.write("0\r\n");


    client.write("\r\n");
     */

});

client.on('data', function (data) {
    console.log('Received: ' + data);
});

client.on('close', function () {
    console.log('Connection closed');
});