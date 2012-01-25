print("Hello, world!");

var CONN = 0;
var SEND = 1;
var RECV = 2;
var CLOSE = 3;
var TIME = 4;

var fd = connect("107.21.70.111");
print("test.js got fd", fd);

function _resume(what, data, req_id) {
    if (what === CONN) {
        send(fd, "GET / HTTP/1.0\n\n");
    } else if (what === SEND) {
        recv(fd, 20000);
    }
    print("resume!", what, data, req_id);
}