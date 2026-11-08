class BaseNode {
	public var parent:BaseNode;
	public var baseValue:Int;

	public function new(parent:BaseNode) {
		this.parent = parent;
		baseValue = 7;
	}
}

class Node extends BaseNode {
	public var left:Node;
	public var right:Node;
	public var label:String;
	public var scalar:Float;

	public function new(parent:BaseNode, label:String) {
		super(parent);
		this.label = label;
		scalar = 3.5;
	}
}

class Main {
	static var root:Node;

	static function make(label:String):Node {
		var local = new Node(root, label);
		local.right = root;
		return local;
	}

	static function stress(count:Int):Node {
		var head:Node = root;
		for (i in 0...count) {
			var next = new Node(head, "stress");
			next.left = head;
			head = next;
		}
		return head;
	}

	static function main():Void {
		root = make("nova");
		root.left = make("left");
		make("unreachable");
		untyped __cpp__("::hx::novagc::Runtime::collect()");
		if (root == null || root.left == null || root.left.label != "left" || root.baseValue != 7)
			Sys.exit(23);
		untyped __cpp__("::hx::novagc::Runtime::collect()");
		if (root.right != null || root.left.right != root)
			Sys.exit(24);
		root = stress(180000);
		untyped __cpp__("::hx::novagc::Runtime::collect()");
		if (root == null || root.left == null || root.parent == null || root.label != "stress")
			Sys.exit(25);

		// Nova -> legacy: the dynamically allocated String must survive an Immix
		// major collection even though its only durable owner is a Nova object.
		var bridgeLabel = "bridge-label-" + Std.string(17001);
		root = new Node(null, bridgeLabel);
		bridgeLabel = null;
		cpp.vm.Gc.run(true);
		if (root.label != "bridge-label-17001")
			Sys.exit(26);

		// Legacy -> Nova: a legacy Array is the only durable owner. Run legacy GC
		// once to publish the cross-heap pinned handle, then move-collect Nova.
		var arrayNode = new Node(null, "array-owned");
		var legacyArray:Array<Node> = [arrayNode];
		root = null;
		cpp.vm.Gc.run(true);
		arrayNode = null;
		untyped __cpp__("::hx::novagc::Runtime::collect()");
		if (legacyArray[0] == null || legacyArray[0].label != "array-owned")
			Sys.exit(27);
		Sys.println("cross_heap_bridge=passed");
	}
}
