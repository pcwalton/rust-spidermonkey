use spidermonkey;
import spidermonkey::js;
import ctypes::size_t;
import comm::{ port, chan, recv };

use std;

fn main() {
    let rt = js::new_runtime(8u32 * 1024u32 * 1024u32);
    let cx = js::new_context(rt, 8192 as size_t);
    js::set_options(cx, js::options::varobjfix | js::options::methodjit);
    js::set_version(cx, 185u);

    let err_port = port();
    js::ext::set_error_channel(cx, chan(err_port));

    let class = js::new_class({ name: "global", flags: 0x47700u32 });
    let global = js::new_compartment_and_global_object(cx, class,
                                                       js::null_principals());

    js::init_standard_classes(cx, global);

    let log_port = port();
    js::ext::set_log_channel(cx, global, chan(log_port));

    js::ext::init_rust_library(cx, global);

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

    let exit = false;
    while !exit {
        let msg = recv(log_port);
        log(core::error, msg.message);

        if msg.level == 1u {
            exit = true;
        }
    }

/*    if error {
        let err = recv(err_port);
        log(core::error, err);
    }*/

    /*let result_src = js::value_to_source(cx, result);
    #error["Result: %s", js::get_string(cx, result_src)];*/
}

