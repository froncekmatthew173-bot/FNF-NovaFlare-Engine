interface Formatter {
    function format(depth:Int):String;
}

class FormatterImpl implements Formatter {
    public var prefix:String;

    public function new(?prefix:String) {
        this.prefix = prefix == null ? "v" : prefix;
    }

    public function format(depth:Int):String {
        return prefix + depth;
    }
}

class Main {
    static function readDynamic(values:Dynamic):Int {
        return values[1];
    }

    static function writeDynamic(values:Dynamic, value:Int):Void {
        values[1] = value;
    }

    static function main():Void {
        final formatter:Formatter = new FormatterImpl();
        if (formatter.format(3) != "v3")
            throw "interface dispatch failed";

        final values:Array<Int> = [1, 2, 3];
        final dynamicValues:Dynamic = values;
        if (readDynamic(dynamicValues) != 2)
            throw "dynamic array read failed";
        writeDynamic(dynamicValues, 9);
        if (values[1] != 9)
            throw "dynamic array write failed";
    }
}
