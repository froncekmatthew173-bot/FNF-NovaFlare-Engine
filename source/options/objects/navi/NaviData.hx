package options.objects.navi;

class NaviData{
    public var name:String;
    public var group:Array<String>;
    public var extraPath:String;

    public function new(name:String, group:Array<String>, extraPath:String = '') {
        this.name = name;
        this.group = group;
        this.extraPath = extraPath;
    }
}