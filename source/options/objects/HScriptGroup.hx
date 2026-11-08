package options.objects;

import scripts.hscript.HScript;

import options.objects.Option;

class HScriptGroup extends OptionCata {
	var sc:HScript;

	public function new(X:Float, Y:Float, width:Float, height:Float, modName:String, path:String, file:String) {
		super(X, Y, width, height);
		this.modAdd = true;
		this.modsName = modName;

		sc = new HScript(path + file + ".hx", this);
		sc.set("Option", Option);
		for(construct in Type.getEnumConstructs(OptionType)) {
			sc.set(construct, Reflect.getProperty(OptionType, construct));
		}
		sc.execute();
		sc.call("onCreate");
	}

	override function destroy():Void {
		sc.call("onDestroy");
		sc.destroy();

		super.destroy();
	}
}