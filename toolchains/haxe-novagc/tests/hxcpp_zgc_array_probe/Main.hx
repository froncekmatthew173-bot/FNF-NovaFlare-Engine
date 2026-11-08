class Main {
    static var ints:Array<Int>;
    static var nodes:Array<Node>;

    static function main():Void {
        ints = [1, 2, 3];
        ints.push(4);
        nodes = [new Node(9), new Node(10)];
        nodes[0] = nodes[1];
    }
}

class Node {
    public var value:Int;

    public function new(value:Int) {
        this.value = value;
    }
}
