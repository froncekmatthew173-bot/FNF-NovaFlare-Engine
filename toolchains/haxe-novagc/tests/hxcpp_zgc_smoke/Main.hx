class Main {
    static var retained:Node;
    static var ints:Array<Int>;
    static var nodes:Array<Node>;
    static var retainedXml:Xml;

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

        final reflected:Node = Type.createInstance(Node, [7, "reflected"]);
        if (reflected == null || reflected.value != 7 ||
            reflected.label != "reflected" ||
            reflected.baseLabel != "base-reflected")
            throw "reflected constructor arguments were not preserved";

        final empty:Node = Type.createEmptyInstance(Node);
        if (empty == null)
            throw "reflected empty construction returned null";

        final pointer = cpp.Pointer.ofArray([42]);
        final pointerHolder:PointerHolder = Type.createInstance(
            PointerHolder, [pointer]);
        if (pointerHolder == null || pointerHolder.first != 42)
            throw "reflected native pointer argument was not preserved";

        final dynamicNull:Dynamic = opaqueNull();
        if (dynamicNull == false || dynamicNull == 0)
            throw "dynamic null compared equal to a scalar";
        final typedNode:Node = null;
        final typedDynamic:Dynamic = typedNode;
        if (typedDynamic != null || typedDynamic == false)
            throw "typed object null was not normalized as Dynamic null";
        final json:Dynamic = haxe.Json.parse(
            '{"empty":[],"enabled":false,"count":0}');
        if (json.empty == null || json.empty.length != 0 ||
            json.enabled != false || json.count != 0)
            throw "JSON empty-array/dynamic scalar semantics failed";

        // OpenFL ShaderData is an abstract over Dynamic whose constructor is
        // exactly `this = {}`.  Runtime GLSL parsing then adds every uniform
        // with Reflect.setField.  Keep this regression identical to that path:
        // the empty anonymous object must remain dynamically extensible across
        // full collections and relocation.
        final shaderData:Dynamic = createShaderData();
        HxcppGcNative.collect(true);
        final textureScaleParameter = new Node(61, "textureScale");
        Reflect.setField(shaderData, "openfl_Texture", new Node(1, "texture"));
        Reflect.setField(shaderData, "openfl_TextureSize", new Node(2, "size"));
        Reflect.setField(shaderData, "openfl_Matrix", new Node(3, "matrix"));
        Reflect.setField(shaderData, "textureScale", textureScaleParameter);
        HxcppGcNative.collect(true);
        if (Reflect.field(shaderData, "textureScale") != textureScaleParameter)
            throw "ShaderData-style dynamic uniform field was not preserved";
        Reflect.setField(shaderData, "textureScale", new Node(62, "updated"));
        final updatedTextureScale:Node = Reflect.field(shaderData, "textureScale");
        if (updatedTextureScale == null || updatedTextureScale.value != 62)
            throw "ShaderData-style dynamic uniform field was not updated";

        // Runtime code such as FlxRuntimeShader deliberately probes a field on
        // a fixed class and catches the failure as Dynamic.  Haxe language
        // exceptions must never escape as std::exception, otherwise the
        // generated catch block cannot observe them.
        var caughtFixedField = false;
        try {
            Reflect.setField(new FixedFieldTarget(), "textureScale", 1.0);
        } catch (error:Dynamic) {
            caughtFixedField = true;
        }
        if (!caughtFixedField)
            throw "fixed-field reflection failure bypassed catch(Dynamic)";

        // Preserve the original hxcpp distinction between raw fields and
        // properties. Reflect.field/setField use paccNever, while
        // getProperty/setProperty must invoke AccCall accessors.  Camera.zoom
        // is a physical field with a setter that updates render scale, and
        // computed properties such as camera.viewWidth have no backing slot.
        final properties = new PropertyTarget();
        if (!Reflect.fields(properties).contains("physical") ||
            !Reflect.fields(properties).contains("computed"))
            throw "fixed-class property enumeration lost fields";
        Reflect.setField(properties, "physical", 2.0);
        if (properties.setterCalls != 0 || properties.physical != 2.0)
            throw "Reflect.setField unexpectedly invoked a property setter";
        Reflect.setProperty(properties, "physical", 3.0);
        if (properties.setterCalls != 1 || properties.physical != 6.0)
            throw "Reflect.setProperty bypassed a physical-field setter";
        if (Reflect.getProperty(properties, "computed") != 123)
            throw "Reflect.getProperty lost a computed getter";

        // A Haxe Class<T> is also a reflective object.  Freeplay's MouseMove
        // passes FreeplayState itself to Reflect.getProperty/setProperty and
        // follows its static songPosiData field.  Preserve original hxcpp's
        // raw-field/property distinction for class statics as well as its
        // complete class/instance member lists.
        StaticPropertyTarget.physical = 1.0;
        StaticPropertyTarget.setterCalls = 0;
        Reflect.setField(StaticPropertyTarget, "physical", 2.0);
        if (StaticPropertyTarget.setterCalls != 0 ||
            StaticPropertyTarget.physical != 2.0)
            throw "Reflect.setField unexpectedly invoked a static setter";
        Reflect.setProperty(StaticPropertyTarget, "physical", 3.0);
        if (StaticPropertyTarget.setterCalls != 1 ||
            StaticPropertyTarget.physical != 6.0)
            throw "Reflect.setProperty bypassed a static setter";
        if (Reflect.getProperty(StaticPropertyTarget, "computed") != 321)
            throw "Reflect.getProperty lost a computed static getter";
        final staticMethod:Dynamic = Reflect.field(StaticPropertyTarget, "ping");
        if (staticMethod == null ||
            Reflect.callMethod(StaticPropertyTarget, staticMethod, [9]) != 10)
            throw "static method reflection was not registered";
        final classFields = Type.getClassFields(StaticPropertyTarget);
        if (!classFields.contains("physical") ||
            !classFields.contains("ping"))
            throw "Type.getClassFields lost static data or methods";

        final nodeFields = Type.getInstanceFields(Node);
        if (!nodeFields.contains("baseLabel") ||
            !nodeFields.contains("baseDescribe") ||
            !nodeFields.contains("value") ||
            !nodeFields.contains("describe"))
            throw "Type.getInstanceFields lost inherited data or methods";
        var valueOccurrences = 0;
        for (field in nodeFields)
            if (field == "value")
                valueOccurrences++;
        if (valueOccurrences != 1)
            throw "Type.getInstanceFields returned duplicate members";

        final reflectedInteger:Dynamic = Reflect.field(properties, "integer");
        if (!Std.isOfType(reflectedInteger, Int) ||
            Std.string(reflectedInteger) != "7" ||
            Std.string(7.0) != "7" || ("" + 7.0) != "7" ||
            [7.0].toString() != "[7]")
            throw "Int/Float tags or compact number formatting regressed";

        // __novaCffiSet is the fixed-class path behind Reflect.setField.  An
        // old receiver must remember newly stored managed objects and strings
        // exactly like a statically generated assignment does.
        final reflectedOwner = new Node(70, "old-owner");
        HxcppGcNative.collect(true);
        final reflectedChild = new Node(71, "young-child");
        Reflect.setField(reflectedOwner, "next", reflectedChild);
        Reflect.setField(reflectedOwner, "label", "young-label-" + 72);
        HxcppGcNative.collect(false);
        if (reflectedOwner.next == null || reflectedOwner.next.value != 71 ||
            reflectedOwner.label != "young-label-72")
            throw "fixed-class reflection bypassed the managed write barrier";

        // The game loads XML-backed assets while entering Freeplay.  Exercise
        // the same Xml.nodeName/nodeValue graph through repeated young moves;
        // this catches stale exact String slots before loadBarrier dereferences
        // an already reclaimed nursery region.
        for (index in 0...64) {
            retainedXml = Xml.parse('<root id="' + index +
                '"><child>value-' + index + '</child></root>');
            HxcppGcNative.collect(false);
            final element = retainedXml.firstElement();
            if (element == null || element.nodeName != "root" ||
                element.get("id") != Std.string(index))
                throw "Xml exact references were not preserved by young GC";
        }

        // Precise-GC call temporaries must remain roots while C++ evaluates
        // sibling arguments.  A callee-side argument root is too late: the
        // collector can run before control enters that callee.
        final nested:Dynamic = {x: collectAndReturn(17)};
        if (Reflect.field(nested, "x") != 17)
            throw "anonymous-object construction lost its parent root";

        // C++ may choose either argument evaluation order.  These mirrored
        // calls guarantee that one anonymous object is completed before the
        // sibling forces a full collection.
        final left:Dynamic = keepLeft(
            {x: collectAndReturn(31)}, {x: collectAndReturn(0)});
        final right:Dynamic = keepRight(
            {x: collectAndReturn(0)}, {x: collectAndReturn(47)});
        if (Reflect.field(left, "x") != 31 ||
            Reflect.field(right, "x") != 47)
            throw "managed call argument was not rooted across sibling evaluation";

        // A rooted ObjectPtr is not sufficient for a call receiver: C++ may
        // resolve operator->() first and then evaluate a collecting argument.
        // The root slot is healed after relocation, but the raw receiver held
        // by the machine would still address reclaimed memory unless the
        // receiver remains pinned for the complete call expression.
        final chainedReceiver = new ReceiverTarget(73).readAfter(
            collectAndReturn(0));
        if (chainedReceiver != 73)
            throw "managed call receiver moved during argument evaluation";

        // Literal arrays are emitted as C++ static data and therefore exist
        // before NovaGC starts.  Their String elements intentionally carry
        // process-lifetime literal_ bytes with a null managed mPtr.  A precise
        // object-field write must preserve that non-reference metadata as well
        // as length; otherwise the result has a non-zero length but reads from
        // an empty backing buffer.  Freeplay's funcData -> FuncButton ->
        // FlxText path depends on exactly this representation.
        final literalField = new LiteralFieldTarget();
        HxcppGcNative.collect(true);
        if (literalField.value != "options" ||
            literalField.value.charAt(0) != "o" ||
            literalField.value.length != 7)
            throw "pre-runtime String literal metadata was lost by a field write";

        // Week/song data enters Freeplay as nested Array<Dynamic> values and
        // is explicitly cast to Array<String>.  The backing Array<Dynamic>
        // stores a String as {mPtr,length,Tag::String}; it must be converted
        // element-by-element, never reinterpreted as String where the tag byte
        // would become literal_ == 0x5.
        final dynamicCharters:Array<Dynamic> = ["alice", "bob"];
        final nestedSongData:Array<Dynamic> = [dynamicCharters];
        final typedCharters:Array<String> = cast nestedSongData[0];
        HxcppGcNative.collect(true);
        if (typedCharters.length != 2 || typedCharters[0] != "alice" ||
            typedCharters[1] != "bob")
            throw "Array<Dynamic> was reinterpreted instead of converted to Array<String>";
        typedCharters[0] = "carol";
        if (dynamicCharters[0] == "carol")
            throw "cross-element Array cast did not use original hxcpp copy semantics";

        // The original hxcpp maps are native hash objects.  NovaGC replaces
        // them with precisely traced managed open-addressed tables, so cover
        // the complete public contract across growth, tombstones, relocation,
        // iteration, copy, formatting and clear instead of only set/get.
        final stringMap = new haxe.ds.StringMap<Node>();
        final intMap = new haxe.ds.IntMap<String>();
        final objectMap = new haxe.ds.ObjectMap<Node, String>();
        final int64Map = new cpp.Int64Map<String>();
        final objectKeys:Array<Node> = [];
        for (index in 0...96) {
            final mappedNode = new Node(1000 + index, "map-" + index);
            objectKeys.push(mappedNode);
            stringMap.set("key-" + index, mappedNode);
            intMap.set(index * 17, "int-" + index);
            objectMap.set(mappedNode, "object-" + index);
            int64Map.set(haxe.Int64.make(index, index * 31), "int64-" + index);
        }
        for (index in 0...48) {
            if (!stringMap.remove("key-" + (index * 2)) ||
                !intMap.remove(index * 2 * 17) ||
                !objectMap.remove(objectKeys[index * 2]) ||
                !int64Map.remove(haxe.Int64.make(index * 2, index * 2 * 31)))
                throw "map tombstone removal failed";
        }
        HxcppGcNative.collect(true);
        for (index in 0...48) {
            final odd = index * 2 + 1;
            final mapped = stringMap.get("key-" + odd);
            if (mapped == null || mapped.value != 1000 + odd ||
                intMap.get(odd * 17) != "int-" + odd ||
                objectMap.get(objectKeys[odd]) != "object-" + odd ||
                int64Map.get(haxe.Int64.make(odd, odd * 31)) != "int64-" + odd)
                throw "map exact references were not preserved by GC";
        }
        var stringCount = 0;
        var intCount = 0;
        var objectCount = 0;
        var int64Count = 0;
        for (_ in stringMap.keyValueIterator()) stringCount++;
        for (_ in intMap.iterator()) intCount++;
        for (_ in objectMap.keys()) objectCount++;
        for (_ in int64Map.keyValueIterator()) int64Count++;
        if (stringCount != 48 || intCount != 48 ||
            objectCount != 48 || int64Count != 48)
            throw "map iteration lost or duplicated entries";
        final copiedStringMap = stringMap.copy();
        stringMap.clear();
        if (stringMap.exists("key-1") || copiedStringMap.get("key-1") == null ||
            copiedStringMap.toString().indexOf("key-1 => ") < 0 ||
            intMap.toString().indexOf(" => ") < 0 ||
            objectMap.toString().indexOf(" => ") < 0 ||
            int64Map.toString().indexOf(" => ") < 0)
            throw "map copy/clear/toString compatibility failed";
    }

    static function opaqueNull():Dynamic
        return null;

    static function createShaderData():Dynamic
        return {};

    static function collectAndReturn(value:Dynamic):Dynamic {
        HxcppGcNative.collect(true);
        return value;
    }

    static function keepLeft(left:Dynamic, right:Dynamic):Dynamic {
        if (right == "__never_keep_left__")
            return right;
        return left;
    }

    static function keepRight(left:Dynamic, right:Dynamic):Dynamic {
        if (left == "__never_keep_right__")
            return left;
        return right;
    }
}

extern class HxcppGcNative {
    @:native("__hxcpp_collect")
    static function collect(major:Bool):Void;
}

class PointerHolder {
    public var first:Int;

    public function new(pointer:cpp.Pointer<Int>) {
        first = pointer[0];
    }
}

class ReceiverTarget {
    final value:Int;

    public function new(value:Int) {
        this.value = value;
    }

    public function readAfter(barrier:Dynamic):Int {
        if (barrier == "__never_receiver__")
            return 0;
        return value;
    }
}

class LiteralFieldTarget {
    // A member initializer is emitted through a file-scope C++ const array,
    // matching FreeplayState.funcData instead of a runtime-only local array.
    public var preRuntimeLiterals:Array<String> = ["options", "mod"];
    public var value:String;

    public function new() {
        value = preRuntimeLiterals[0];
    }
}

class FixedFieldTarget {
    public var value:Int = 0;

    public function new() {}
}

class PropertyTarget {
    public var integer:Int = 7;
    public var physical(default, set):Float = 1.0;
    public var computed(get, never):Int;
    public var setterCalls:Int = 0;

    public function new() {}

    function set_physical(value:Float):Float {
        setterCalls++;
        return physical = value * 2.0;
    }

    function get_computed():Int
        return 123;
}

class StaticPropertyTarget {
    public static var physical(default, set):Float = 1.0;
    public static var computed(get, never):Int;
    public static var setterCalls:Int = 0;

    static function set_physical(value:Float):Float {
        setterCalls++;
        return physical = value * 2.0;
    }

    static function get_computed():Int
        return 321;

    public static function ping(value:Int):Int
        return value + 1;
}

class BaseNode {
    public var baseLabel:String;

    public function new(baseLabel:String) {
        this.baseLabel = baseLabel;
    }

    public function baseDescribe():String
        return baseLabel;
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

    public function describe():String
        return label;
}
