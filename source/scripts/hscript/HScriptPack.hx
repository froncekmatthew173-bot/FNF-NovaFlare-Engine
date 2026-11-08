package scripts.hscript;

import crowplexus.hscript.Tools;
import crowplexus.hscript.ISharedScript;
import crowplexus.hscript.Expr;

@:access(scripts.hscript.HScript)
class HScriptPack implements ISharedScript {
	public var standard(get, never):Dynamic;
	public function get_standard():Dynamic {
		return this;
	}

	public var scriptMembers:Array<HScript>;
	private var globals:Array<{var script:HScript; var name:String; var type:String;}>;

	public var counts(get, never):Int;
	inline function get_counts():Int {
		return scriptMembers.length;
	}

	public function new() {
		scriptMembers = [];
		globals = [];
	}

	public function add(sc:HScript) {
		if(sc.active && !sc.manualRun && !sc.loaded && sc.expr != null) {
			scriptMembers.push(sc);
			asyncScript(sc);
		}
	}

	function asyncScript(sc:HScript) {
		sc.set("_public_", this);
		final e = Tools.expr(sc.expr);
		if(e.match(EBlock(_))) {
			Tools.iter(sc.expr, registerPublics.bind(_, sc));
		} else registerPublics(sc.expr, sc);
	}

	function registerPublics(e, sc:HScript) {
		switch(Tools.expr(e)) {
			case EVar(n, _, _, _, _, _, _, access):
				if(access != null && access.contains("public") && !access.contains("static")) {
					globals.push({name: n, type: "var", script: sc});
				}
			case EFunction(_, _, _, n, _, access) if(n != null):
				if(access != null && access.contains("public") && !access.contains("static")) {
					globals.push({name: n, type: "func", script: sc});
				}
			case _:
		}
	}

	public function remove(sc:HScript, destroy:Bool = false) {
		if(scriptMembers.contains(sc)) {
			scriptMembers.remove(sc);
			if(destroy) sc.destroy();
		}
	}

	@:access(crowplexus.hscript.Interp)
	public function hget(name:String, ?e:Expr):Dynamic {
		for(g in globals) {
			if(g.name == name && g.script.active) {
				final l = g.script.interp.directorFields.get(name);
				if(l != null && l.isPublic == true) {
					return g.script.interp.resolve(name);
				}
				return null;
			}
		}

		throw "public variables has not '" + name + "'.";
		return null;
	}

	public function hset(name:String, v:Dynamic, ?e:Expr):Void {
		for(g in globals) {
			if(g.name == name && g.script.active) {
				final l = g.script.interp.directorFields.get(name);
				if(l != null && l.isPublic == true) {
					g.script.interp.setVar(name, v);
				}
				return;
			}
		}

		throw "public variables has not '" + name + "'.";
	}

	public function call(func:String, ?args:Array<Dynamic>):Dynamic {
		var i:Int = -1;
		var lastRet:Dynamic = null;
		while(i++ < this.counts - 1) {
			final sc:HScript = this.scriptMembers[i];
			if(sc != null) {
				lastRet = sc.call(func, args);
			}
		}

		return lastRet;
	}

	public function destroy(needCall:Bool = false) {
		while(this.counts > 0) {
			final sc:HScript = this.scriptMembers.shift();
			if(sc != null) {
				if(needCall) sc.call("onDestroy");
				sc.destroy();
			}
		}
		globals = [];
	}

	public function execute():Dynamic {
		var i:Int = -1;
		var lastRet:Dynamic = null;
		while(i++ < this.counts - 1) {
			final sc:HScript = this.scriptMembers[i];
			if(sc != null) {
				lastRet = sc.execute();
			}
		}

		return lastRet;
	}
}