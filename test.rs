use spidermonkey;
import spidermonkey::js;

import ctypes::size_t;
import comm::{ port, chan, recv, send };

use std;

enum child_message {
    set_log(chan<js::log_message>),
    set_err(chan<js::error_report>),
    log_msg(js::log_message),
    err_msg(js::error_report)
}

fn make_children(msg_chan: chan<child_message>) {
    task::spawn {||
        let log_port = port::<js::log_message>();
        let set_msg = set_log(chan(log_port));
        send(msg_chan, set_msg);

        while true {
            send(msg_chan, log_msg(recv(log_port)));
        }
    };

    task::spawn {||
        let err_port = port::<js::error_report>();
        let set_msg = set_err(chan(err_port));
        send(msg_chan, set_msg);

        while true {
            send(msg_chan, err_msg(recv(err_port)));
        }
    };
}

fn main() {
    let msg_port = port::<child_message>();
    make_children(chan(msg_port));

    let rt = js::new_runtime(8u32 * 1024u32 * 1024u32);
    let cx = js::new_context(rt, 8192 as size_t);
    js::set_options(cx, js::options::varobjfix | js::options::methodjit);
    js::set_version(cx, 185u);

    let class = js::new_class({ name: "global", flags: 0x47700u32 });
    let global = js::new_compartment_and_global_object(
        cx, class, js::null_principals());

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
            log_msg(m) {
                log(core::error, m.message);
                if m.level == 1u {
                    exit = true;
                }
            }
            err_msg(err) {
                exit = true;
                log(core::error, err);
            }
        }
        if setup == 2 {
            setup = 3;
            alt std::io::read_whole_file("test.js") {
                result::ok(file) {
                    let script = js::compile_script(
                        cx, global, file, "test.js", 0u);
                    js::execute_script(cx, global, script);
                }
                _ { fail }
            }

            let xit = js::compile_script(
                cx, global, str::bytes("throw new Error('exit');"), "test.js", 0u);
            js::execute_script(cx, global, xit);
        }
    }

    /*let result_src = js::value_to_source(cx, result);
    #error["Result: %s", js::get_string(cx, result_src)];*/
}

