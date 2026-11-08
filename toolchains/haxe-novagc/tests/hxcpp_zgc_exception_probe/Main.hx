class Failure {
    public var node:Node;
    public var message:String;

    public function new(node:Node, message:String) {
        this.node = node;
        this.message = message;
    }
}

class Main {
    static var retained:Node;

    static function raise():Void {
        throw new Failure(new Node(41), "failure");
    }

    static function main():Void {
        try {
            raise();
        } catch (failure:Failure) {
            retained = failure.node;
            untyped __cpp__("__hxcpp_collect(true); __hxcpp_gc_compact();");
            if (retained.value != 41 || failure.message != "failure")
                untyped __cpp__("throw std::runtime_error(\"exception graph failed\")");
        }
    }
}

class Node {
    public var value:Int;
    public function new(value:Int) this.value = value;
}
