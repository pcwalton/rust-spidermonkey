
var XMLHttpRequest = (function(global) {

var _xhrs = {};

function urlparse(url) {
    let result = {};
    let netloc = '';
    let query = '';
    let fragment = '';
    let i = url.indexOf(':')
    if (i > 0) {
        result.scheme = url.substring(0, i);
        url = url.substring(i + 1);
        i = url.indexOf('/', 2);
        if (i > 0) {
            result.netloc = url.substring(2, i);
            url = url.substring(i);
        }
    } else {
        result.scheme = "TODO use relative"
        result.netloc = "TODO use relative"
    }
    i = url.indexOf('#');
    if (i > 0) {
        result.fragment = url.substring(i + 1)
        url = url.substring(0, i);
    }
    i = url.indexOf('?');
    if (i > 0) {
        result.query = url.substring(i + 1);
        url = url.substring(0, i);
    }
    result.url = url;
    return result;
}

var CONN = 0;
var SEND = 1;
var RECV = 2;
var CLOSE = 3;
var TIME = 4;
var URL = 5;

function XMLHttpRequest() {
    this.readyState = 0;
    this.status = 0;
    this.statusText = "";
    this._response = "";
    this.responseText = "";
    this.responseXML = null;
    this._headers = [];
    this._responseHeaders = [];
}
XMLHttpRequest.requests_outstanding = 0;
XMLHttpRequest.prototype = {
    UNSENT: 0,
    OPENED: 1,
    HEADERS_RECEIVED: 2,
    LOADING: 3,
    DONE: 4,
    onload: function() {},
    onreadystatechange: function() {
        if (this.readyState === 4) {
            this.onload();
        }
    },
    open: function open(method, url, async, user, pw) {
        let parts = urlparse(url);
        let host = parts.netloc;
        let port = 0;
        if (parts.scheme === 'http') {
            port = 80;
        } else if (parts.scheme === 'https') {
            port = 443;
        } else {
            throw new Error("Unsupported scheme: " + parts.scheme);
        }

        let i = host.indexOf(':');
        if (i > 0) {
            port = parseInt(host.substring(i + 1));
            host = host.substring(0, i);
        }
        this._fd = jsrust_connect(host); // TODO support port
        XMLHttpRequest.requests_outstanding++;
        this._host = host;
        this._method = method;
        this._url = parts.url;
        this._user = user;
        this._pw = pw;
        this.readyState = XMLHttpRequest.prototype.OPENED;
        _xhrs[this._fd] = this;
    },
    setRequestHeader: function setRequestHeader(header, value) {
        this._headers.push([header, value]);
    },
    send: function send(data) {
        this._request = this._method + ' ' + this._url + ' HTTP/1.0\r\n';
        //this._request += 'Host: ' + this._host + '\r\n';
        if (data) {
            this._request += 'Content-Length: ' + data.length + '\r\n';
        }
        for (let i = 0; i < this._headers.length; i++) {
            let key = this._headers[i][0];
            let val = this._headers[i][1];
            this._request += key + ': ' + val + '\r\n';
        }
        this._request += '\r\n';
        if (data) {
            this._request += data;
        }
        if (this._connected === true) {
            jsrust_send(this._fd, this._request);
            jsrust_recv(xhr._fd, 32768);
        }
    },
    abort: function abort() {
        // TODO ???
    },
    getResponseHeader: function getResponseHeader(header) {
        for (var i = 0; i < this._responseHeaders.length; i++) {
            if (header === this._responseHeaders[i][0]) {
                return this._responseHeaders[i][1];
            }
        }
    },
    getAllResponseHeaders: function getAllResponseHeaders() {
        return this._responseHeaders;
    }
}

global._resume = function _resume(what, data, req_id) {
    //print("Handling request. Total:", XMLHttpRequest.requests_outstanding);
    //print("_resume", what, data, req_id);
    var xhr = _xhrs[req_id]
    if (what === CONN) {
        xhr.readyState = XMLHttpRequest.prototype.OPENED;
        xhr.onreadystatechange.apply(xhr);
        if (xhr._request) {
            jsrust_send(xhr._fd, xhr._request);
            jsrust_recv(xhr._fd, 32768);
        } else {
            xhr._connected = true;
        }
    } else if (what === SEND) {
        // TODO need to get number of bytes actually written back into js
        // to support writing large requests in chunks
        xhr._request = ""; //xhr._request.substring(data[1]);
        if (xhr._request.length) {
            jsrust_send(xhr._fd, xhr._request);
            jsrust_recv(xhr._fd, 32768);
        }
    } else if (what === RECV) {
        xhr._response += data;
//                xhr.responseText += data[1];
        if (!xhr.statusText) {
            let newline = "\r\n";
            let i = xhr._response.indexOf(newline + newline);
            if (i === -1) {
                newline = "\n";
                i = xhr._response.indexOf(newline + newline);
            }
            xhr._bodyIndex = i + 4;
            if (i > 0) {
                let j = xhr._response.indexOf(newline);
                let parts = xhr._response.substring(0, j).split(' ');
                xhr.status = parseInt(parts[1]);
                for (let q = 1; q < parts.length; q++) {
                    xhr.statusText += parts[q] + " ";
                }
                xhr.statusText = xhr.statusText.substring(0, xhr.statusText.length - 1);
                let headers = xhr._response.substring(j + 2, i);
                while (headers) {
                    let k = headers.indexOf(newline);
                    let header = "";
                    if (k > 0) {
                        header = headers.substring(0, k);
                        headers = headers.substring(k + 2);
                    } else {
                        header = headers;
                        headers = "";
                    }
                    let l = header.indexOf(": ");
                    let key = header.substring(0, l);
                    let val = header.substring(l + 2);
                    if (key.toLowerCase() === "content-length") {
                        xhr._contentLength = parseInt(val);
                    }
                    xhr._responseHeaders.push([key, val]);
                }
                xhr.readyState = XMLHttpRequest.prototype.HEADERS_RECEIVED;
                xhr.onreadystatechange.apply(xhr);
            }
        }
        if (xhr._bodyIndex + xhr._contentLength === xhr._response.length) {
            xhr.responseText = xhr._response.substring(xhr._bodyIndex);
            xhr.readyState = XMLHttpRequest.prototype.DONE;
            xhr.onreadystatechange.apply(xhr);
            delete _xhrs[xhr._id];
            close(xhr._id);
        } else {
            xhr.readyState = XMLHttpRequest.prototype.LOADING;
            xhr.onreadystatechange.apply(xhr);
        }
    } else if (what === TIME) {
        timeouts[req_id][0].apply(global, timeouts[req_id][1]);
        timeouts[req_id] = undefined;
        // piggyback on this
        XMLHttpRequest.requests_outstanding--;
    } else if (what === URL) {
        print("LOAD URL", data);
        window.location = data;
    } else if (what === CLOSE) {
        this._fd = undefined;
        XMLHttpRequest.requests_outstanding--;
    }
    if (XMLHttpRequest.requests_outstanding === 0) {
        postMessage(9, "exitproc");
    }
}

var timeouts = {};

global.setTimeout = function(func, time) {
    var args = [];
    for (var i = 0; i < arguments.length; i++) {
        args.push(arguments[i]);
    }
    var timeoutnum = jsrust_timeout(time);
    timeouts[timeoutnum] = [func, args];
    // piggyback on this
    XMLHttpRequest.requests_outstanding++;
}

return XMLHttpRequest;
})(this);

