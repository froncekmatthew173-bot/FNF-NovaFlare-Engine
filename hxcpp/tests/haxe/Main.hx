class Main {
    static var retained:Node;
    static var ints:Array<Int>;
    static var nodes:Array<Node>;

    static function main():Void {
        final root = new Node(1, "root");
        root.next = new Node(2, "child");
        root.label = "root-updated";
        root.any = root.next;
        ints = [1, 2, 3];
        ints.push(4);
        ints.push(5);
        nodes = [root, root.next];
        nodes[0] = nodes[1];
        retained = root;
    }
}

class BaseNode {
    public var baseLabel:String;

    public function new(baseLabel:String) {
        this.baseLabel = baseLabel;
    }
}

class Node extends BaseNode {
    public var next:Node;
    public var value:Int;
    public var label:String;
    public var any:Dynamic;

    public function new(value:Int, label:String) {
        super("base-" + label);
        this.value = value;
        this.label = label;
    }
}
