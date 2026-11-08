enum Token {
    Empty;
    Number(value:Int);
    Pair(left:Node, right:Node);
}

class Main {
    static var token:Token;
    static var result:Int;

    static function main():Void {
        token = Pair(new Node(1), new Node(2));
        untyped __cpp__("__hxcpp_collect(true); __hxcpp_gc_compact();");
        switch (token) {
            case Pair(left, right): result = left.value + right.value;
            case Number(value): value++;
            case Empty:
        }
        if (result != 3)
            untyped __cpp__("throw std::runtime_error(\"precise enum graph failed\")");

        // Exercise the generated enum Class identity as well as the object
        // graph.  A previous ClassOf implementation accidentally inherited
        // EnumBase's descriptor, so ordinary switches worked while
        // Type.createEnum and Type.getEnumConstructs failed at runtime.
        var enumType:Dynamic = Token;
        var reflected:Token = cast Type.createEnum(enumType, "Number", [7]);
        if (Type.enumConstructor(reflected) != "Number" ||
            Type.enumParameters(reflected)[0] != 7 ||
            Type.getEnum(reflected) != Token)
            untyped __cpp__("throw std::runtime_error(\"enum reflection identity failed\")");
        var constructs = Type.getEnumConstructs(enumType);
        if (constructs.indexOf("Empty") < 0 ||
            constructs.indexOf("Number") < 0 ||
            constructs.indexOf("Pair") < 0)
            untyped __cpp__("throw std::runtime_error(\"enum constructor registry failed\")");
    }
}

class Node {
    public var value:Int;
    public function new(value:Int) this.value = value;
}
