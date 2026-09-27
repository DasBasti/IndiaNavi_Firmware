// Required by esp-idf-sys: emits the ESP-IDF link arguments and the
// generated bindings search paths.
fn main() {
    embuild::espidf::sysenv::output();
}
