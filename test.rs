use spidermonkey;
import spidermonkey::js;
import ctypes::size_t;
import comm::{ port, chan, recv };

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
	js::ext::init_rust_library(cx, global);

    let src = "throw new Error(new Port().channel());";
    let script = js::compile_script(cx, global, str::bytes(src), "test.js",
                                    0u);
    let result_opt = js::execute_script(cx, global, script);

	let err = recv(err_port);
	log(core::error, err);

    /*let result_src = js::value_to_source(cx, result);
    #error["Result: %s", js::get_string(cx, result_src)];*/
}

