package lime.system;

class BackendThread {
	public static function run(f:Void -> Void):Void {
		f();
	}
}
