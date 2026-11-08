import sys.thread.Thread;

class Main {
    static function main():Void {
        final mainThread = Thread.current();
        final worker = Thread.create(() -> {
            final node = new Node(77);
            untyped __cpp__("__hxcpp_collect(true); __hxcpp_gc_compact();");
            mainThread.sendMessage(node);
        });
        final received:Node = cast Thread.readMessage(true);
        untyped __cpp__("__hxcpp_zgc_join_threads()");
        untyped __cpp__("__hxcpp_collect(true); __hxcpp_gc_compact();");
        if (received.value != 77)
            untyped __cpp__("throw std::runtime_error(\"thread result failed\")");
    }
}

class Node {
    public var value:Int;
    public function new(value:Int) this.value = value;
}
