@:build(lime.system.CFFI.build("self"))
class Main {
    @:cffi("self", "nova_generated_prime_add")
    static function nativeAdd(left:Int, right:Int):Int;

    @:cffi("self", "nova_generated_prime_echo")
    static function nativeEcho(input:String):String;

    static function main():Void {
        if (nativeAdd(11, 31) != 42)
            untyped __cpp__("throw std::runtime_error(\"Lime CFFI macro add failed\")");
        if (nativeEcho("lime-exact") != "lime-exact")
            untyped __cpp__("throw std::runtime_error(\"Lime CFFI macro String failed\")");
    }
}
