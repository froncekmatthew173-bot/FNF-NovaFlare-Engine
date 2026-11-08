class Main {
    static var retained:Node;

    static function makeAdder(node:Node):(Int)->Int {
        return value -> {
            retained = node;
            return node.value + value;
        };
    }

    static function makeLabelChecker():(String)->Bool {
        return label -> {
            for (_ in 0...3) {
                untyped __cpp__("__hxcpp_collect(true); __hxcpp_gc_compact();");
                untyped __cpp__("for (int i = 0; i < 20000; ++i) { ::String churn(std::string(96, static_cast<char>('a' + (i % 26)))); (void)churn; }");
            }
            return label == "closure-argument";
        };
    }

    static function main():Void {
        final add = makeAdder(new Node(40));
        untyped __cpp__("__hxcpp_collect(true); __hxcpp_gc_compact();");
        if (add(2) != 42)
            untyped __cpp__("throw std::runtime_error(\"closure failed\")");

        final checkLabel = makeLabelChecker();
        final dynamicLabel = ["closure", "argument"].join("-");
        if (!checkLabel(dynamicLabel))
            untyped __cpp__("throw std::runtime_error(\"closure argument relocation failed\")");
    }
}

class Node {
    public var value:Int;
    public function new(value:Int) this.value = value;
}
