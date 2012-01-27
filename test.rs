use spidermonkey;
import spidermonkey::js;

import ctypes::size_t;
import comm::{ port, chan, recv, send };

use std;

import std::{ io, treemap, uvtmp };

enum child_message {
    set_log(chan<js::log_message>),
    set_err(chan<js::error_report>),
    log_msg(js::log_message),
    err_msg(js::error_report),
    io_cb(u32, str, u32),
    stdout(str),
    stderr(str),
    spawn(str, str),
    cast(str, str),
    exitproc,
    done
}

enum ioop {
    op_stdout = 0,
    op_stderr = 1,
    op_spawn = 2,
    op_cast = 3,
    op_connect = 4,
    op_recv = 5,
    op_send = 6,
    op_close = 7,
    op_time = 8,
    op_exit = 9
}

fn populate_global_scope(cx : js::context, global : js::object) {
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
    alt std::io::read_whole_file_str("test.js") {
        result::ok(file) {
            let script = js::compile_script(
                cx, global, str::bytes(#fmt("try { %s } catch (e) { print('Error: ', e, e.stack) }", file)), "test.js", 0u);
            js::execute_script(cx, global, script);
        }
        _ { fail }
    }
}

fn make_children(msg_chan : chan<child_message>, senduv_chan: chan<chan<uvtmp::iomsg>>) {
    task::spawn {||
        let log_port = port::<js::log_message>();
        send(msg_chan, set_log(chan(log_port)));

        while true {
            let msg = recv(log_port);
            if msg.level == 9u32 {
                send(msg_chan, exitproc);
                break;
            } else {
                send(msg_chan, log_msg(msg));
            }
        }
    };

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
                            uvtmp::delete_buf(buf);
                        }
                    }
                }
                uvtmp::whatever {
                    send(msg_chan, done);
                    break;
                }
            }
        }
    };
}

fn make_actor(myid : int, thread : uvtmp::thread, maxbytes : u32, out : chan<child_message>, sendchan : chan<(int, chan<child_message>)>) {

    task::spawn {||
        let rt = js::get_thread_runtime(maxbytes);
        let msg_port = port::<child_message>();
        send(sendchan, (myid, chan(msg_port)));
        let senduv_port = port::<chan<uvtmp::iomsg>>();
        make_children(chan(msg_port), chan(senduv_port));
        let uv_chan = recv(senduv_port);

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
        let childid = 0;

        while !exit {
            let msg = recv(msg_port);
            alt msg {
                set_log(ch) {
                    js::ext::set_log_channel(
                        cx, global, ch);
                    setup += 1;
                }
                log_msg(m) {                
                    // messages from javascript
                    alt m.level{
                        0u32 { // stdout
                        send(out, stdout(
                            #fmt("[Actor %d] %s",
                            myid, m.message)));
                        }
                        1u32 { // stderr
                            send(out, stderr(
                                #fmt("[ERROR %d] %s",
                                myid, m.message)));
                        }
                        2u32 { // spawn
                            send(out, spawn(
                                #fmt("%d:%d", myid, childid),
                                m.message));
                            childid = childid + 1;
                        }
                        3u32 { // cast
                        }
                        4u32 { // CONNECT
                            uvtmp::connect(
                                thread, m.tag, m.message, uv_chan);
                        }
                        5u32 { // SEND
                            uvtmp::write(
                                thread, m.tag,
                                str::bytes("GET / HTTP/1.0\n\n"),
                                uv_chan);
                        }
                        6u32 { // RECV
                            uvtmp::read_start(thread, m.tag, uv_chan);
                        }
                        7u32 { // CLOSE
                            //log(core::error, "close");
                            uvtmp::close_connection(thread, m.tag);
                        }
                        8u32 { // SETTIMEOUT
                            //log(core::error, "settimeout");

                        }
                        _ {
                            log(core::error, "...");
                        }
                    }
                }
                io_cb(a1, a2, a3) {
                    js::set_data_property(cx, global, a2);
                    let code = #fmt("_resume(%u, _data, %u); _data = undefined; XMLHttpRequest.requests_outstanding", a1 as uint, a3 as uint);
                    let script = js::compile_script(cx, global, str::bytes(code), "io", 0u);
                    js::execute_script(cx, global, script);
                }
                exitproc {
                    send(uv_chan, uvtmp::whatever);
                }
                done {
                    exit = true;
                    send(out, done);
                }
            }
            if setup == 1 {
                setup = 2;
                populate_global_scope(cx, global);
                let checkwait = js::compile_script(
                    cx, global, str::bytes("if (XMLHttpRequest.requests_outstanding === 0) jsrust_exit();"), "test.js", 0u);
                js::execute_script(cx, global, checkwait);
            }
        }
    };
}


fn main() {
    let maxbytes = 8u32 * 1024u32 * 1024u32;
    let thread = uvtmp::create_thread();
    uvtmp::start_thread(thread);

    let stdoutport = port::<child_message>();
    let stdoutchan = chan(stdoutport);

    let sendchanport = port::<(int, chan<child_message>)>();
    let sendchanchan = chan(sendchanport);

    let map = treemap::init();

    for x in [1, 2] {
        make_actor(x, thread, maxbytes, stdoutchan, sendchanchan);
    }
    for _ in [1, 2] {
        let (theid, thechan) = recv(sendchanport);
        treemap::insert(map, theid, thechan);
    }

    let left = 2;
    let actorid = left;
    while true {
        alt recv(stdoutport) {
            stdout(x) { log(core::error, x); }
            stderr(x) { log(core::error, x); }
            spawn(id, src) {
                log(core::error, ("spawn", id, src));
                actorid = actorid + 1;
                left = left + 1;
                task::spawn {||
                    make_actor(actorid, thread, maxbytes, stdoutchan, sendchanchan);
                };
            }
            cast(id, msg) {}
            exitproc {
                left = left - 1;
                if left == 0 {
                    let n = @mutable 0;
                    fn t(n: @mutable int, &&_k: int, &&v: chan<child_message>) {
                        send(v, exitproc);
                        *n += 1;
                    }
                    treemap::traverse(map, bind t(n, _, _));
                    left = *n;
                }
            }
            done {
                left = left - 1;
                if left == 0 {
                    break;
                }
            }
        }
    }
    // temp hack: join never returns right now
    js::ext::rust_exit_now(0);
    uvtmp::join_thread(thread);
    uvtmp::delete_thread(thread);
}

