// Required by esp-idf-sys: emits the ESP-IDF link arguments and the
// generated bindings search paths. On a host build it is a no-op (the
// ESP-IDF sysenv variables it reads are simply absent), which is what lets
// `cargo test --target x86_64-unknown-linux-gnu` run in this directory.
fn main() {
    embuild::espidf::sysenv::output();
}
