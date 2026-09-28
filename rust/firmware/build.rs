// Required by esp-idf-sys: emits the ESP-IDF link arguments and the
// generated bindings search paths.
//
// A build script is always compiled *for the host*, so `cfg!(target_os)` in
// here is the host's, not the firmware's. `CARGO_CFG_TARGET_OS` is the one that
// names the platform being built for -- and on a host `cargo test` (which is
// how the pure parts of src/board/ are tested, see PORTING.md "Host build and
// test") there is no ESP-IDF to talk to, so the script must do nothing.
fn main() {
    if std::env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("espidf") {
        embuild::espidf::sysenv::output();
    }
}
