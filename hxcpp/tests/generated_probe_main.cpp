#include <hxcpp.h>

#include <Main.h>

#include <iostream>

void __boot_all();

int main() {
    try {
        __hxcpp_nova_initialize();
        __boot_all();
        Main_obj::main();
        __hxcpp_collect(true);
        __hxcpp_gc_compact();
        __hxcpp_nova_shutdown();
        return 0;
    } catch (const ::Dynamic& error) {
        std::cerr << "uncaught Haxe probe exception: "
                  << error.toString().view() << '\n';
    } catch (const std::exception& error) {
        std::cerr << "uncaught native probe exception: "
                  << error.what() << '\n';
    } catch (...) {
        std::cerr << "uncaught unknown probe exception\n";
    }
    return 1;
}
