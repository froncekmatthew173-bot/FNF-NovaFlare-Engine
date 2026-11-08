class Main {
    static function main():Void {
        // Keeping the public entry reachable forces trandom.Native and its
        // platform-selected entropy implementation into generated C++.
        final value = trandom.Native.get();
        if (value == 0x7fffffff)
            trace(value);
    }
}
