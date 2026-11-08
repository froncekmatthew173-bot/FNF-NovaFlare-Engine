class Main {
    static function main():Void {
        final add:Dynamic = cpp.Prime._loadPrime(
            "self", "nova_generated_prime_add", "iii", false);
        if (add(19, 23) != 42)
            untyped __cpp__("throw std::runtime_error(\"generated Prime add failed\")");

        final echo:Dynamic = cpp.Prime._loadPrime(
            "self", "nova_generated_prime_echo", "ss", false);
        if (echo("precise") != "precise")
            untyped __cpp__("throw std::runtime_error(\"generated Prime String failed\")");
    }
}
