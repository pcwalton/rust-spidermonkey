use spidermonkey;
import spidermonkey::js;

import ctypes::size_t;
import comm::{ port, chan, recv, send };

use std;

import std::{ uvtmp, io };


enum child_message {
    set_log(chan<js::log_message>),
    set_err(chan<js::error_report>),
    set_io(chan<js::io_message>),
    log_msg(js::log_message),
    err_msg(js::error_report),
    io_msg(js::io_message),
    io_cb(u32, str, u32)
}

enum connection {
    disconnected,
    connected(uvtmp::connect_data)
}

enum ioop {
    op_connect = 0,
    op_recv = 1,
    op_send = 2,
    op_close = 3,
    op_time = 4
}

fn make_uv_child(senduv_chan: chan<chan<uvtmp::iomsg>>, msg_chan: chan<child_message>) {
    task::spawn {||
        let uv_port = port::<uvtmp::iomsg>();
        send(senduv_chan, chan(uv_port));
        while true {
            let msg = recv(uv_port);
            alt msg {
                uvtmp::connected(cd) {
                    send(msg_chan, io_cb(0u32, "onconnect", uvtmp::get_req_id(cd)));
                }
                uvtmp::wrote(cd) {
                    send(msg_chan, io_cb(1u32, "onsend", uvtmp::get_req_id(cd)));
                }
                uvtmp::read(cd, buf, len) {
                    if len == -1 {
                        send(msg_chan, io_cb(3u32, "", uvtmp::get_req_id(cd)));
                    } else {
                        unsafe {
                            let vecbuf = vec::unsafe::from_buf(buf, len as uint);
                            let bufstr = str::unsafe_from_bytes(vecbuf);
                            send(msg_chan, io_cb(2u32, bufstr, uvtmp::get_req_id(cd)));
                        }
                    }
                    uvtmp::delete_buf(buf);
                }
            }
        }
    };
}

fn make_children(msg_chan: chan<child_message>)-> uvtmp::thread {
    let senduv_port = port::<chan<uvtmp::iomsg>>();
    make_uv_child(chan(senduv_port), msg_chan);
    let uv_chan = recv(senduv_port);

    let thread = uvtmp::create_thread();
    uvtmp::start_thread(thread);


    task::spawn {||
        let log_port = port::<js::log_message>();
        send(msg_chan, set_log(chan(log_port)));

        while true {
            send(msg_chan, log_msg(recv(log_port)));
        }
    };

    task::spawn {||
        let err_port = port::<js::error_report>();
        send(msg_chan, set_err(chan(err_port)));

        while true {
            send(msg_chan, err_msg(recv(err_port)));
        }
    };

    task::spawn {||
        let io_port = port::<js::io_message>();
        let io_chan = chan(io_port);
        send(msg_chan, set_io(io_chan));

        while true {
            let result = recv(io_port);
            alt result.a1 {
                0u32 { // CONNECT
                    uvtmp::connect(
                        thread, result.a3, result.a2, uv_chan);
                    /*todo check return value is ok
                    if cd == ptr::null() {
                        log(core::error, "send exception to js");
                    }*/
                }
                1u32 { // SEND
                    uvtmp::write(
                        thread, result.a3,
                        str::bytes("GET / HTTP/1.0\n\n"),
                        uv_chan);
                }
                2u32 { // RECV
                    uvtmp::read_start(thread, result.a3, uv_chan);
                }
                3u32 { // CLOSE
                    log(core::error, "close");

                }
                4u32 { // SETTIMEOUT
                    log(core::error, "settimeout");

                }
            }
        }
    };

    ret thread;
}

fn main() {
    let msg_port = port::<child_message>();
    let thread = make_children(chan(msg_port));

    let rt = js::new_runtime(8u32 * 1024u32 * 1024u32);
    let cx = js::new_context(rt, 8192 as size_t);
    js::set_options(cx, js::options::varobjfix | js::options::methodjit);
    js::set_version(cx, 185u);

    let clas = js::new_class({ name: "global", flags: 0x47700u32 });
    let global = js::new_compartment_and_global_object(
        cx, clas, js::null_principals());

    js::init_standard_classes(cx, global);
    js::ext::init_rust_library(cx, global);

    let exit = false;
    let setup = 0;

    while !exit {
        let msg = recv(msg_port);
        alt msg {
            set_log(ch) {
                js::ext::set_log_channel(
                    cx, global, ch);
                setup += 1;
            }
            set_err(ch) {
                js::ext::set_error_channel(
                    cx, ch);
                setup += 1;
            }
            set_io(ch) {
                js::ext::set_io_channel(
                    cx, global, ch);
                setup += 1;
            }
            log_msg(m) {
                log(core::error, m.message);
                if m.level == 1u {
                    //exit = true;
                }
            }
            err_msg(err) {
                //exit = true;
                log(core::error, ("err_msg", err));
            }
            io_msg(io) {
                //exit = true;
                //js::ext::fire_io_callback(cx, global, io.a3);
            }
            io_cb(a1, a2, a3) {
                js::set_data_property(cx, global, a2);
                let code = #fmt("_resume(%u, _data, %u); _data = undefined", a1 as uint, a3 as uint);
                let script = js::compile_script(cx, global, str::bytes(code), "io", 0u);
                js::execute_script(cx, global, script);
            }
        }
        if setup == 3 {
            setup = 4;
            alt std::io::read_whole_file("xmlhttprequest.js") {
                result::ok(file) {
                    let script = js::compile_script(
                        cx, global, file, "xmlhttprequest.js", 0u);
                    js::execute_script(cx, global, script);
                }
                _ { fail }
            }
            alt std::io::read_whole_file("dom.js") {
                result::ok(file) {
                    let script = js::compile_script(
                        cx, global, file, "dom.js", 0u);
                    js::execute_script(cx, global, script);
                }
                _ { fail }
            }
            alt std::io::read_whole_file("test.js") {
                result::ok(file) {
                    let script = js::compile_script(
                        cx, global, file, "dom.js", 0u);
                    js::execute_script(cx, global, script);
                }
                _ { fail }
            }

            //let xit = js::compile_script(
            //    cx, global, str::bytes("throw new Error('exit');"), "test.js", 0u);
            //js::execute_script(cx, global, xit);
        }
    }

    uvtmp::join_thread(thread);
    uvtmp::delete_thread(thread);

    /*let result_src = js::value_to_source(cx, result);
    #error["Result: %s", js::get_string(cx, result_src)];*/
}

