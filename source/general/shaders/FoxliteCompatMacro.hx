package general.shaders;

#if macro
import haxe.macro.Context;
import haxe.macro.Expr;
#end

/** Integrate FoxLite without modifying the selected haxelib checkout. */
class FoxliteCompatMacro
{
	#if macro
	public static macro function buildRenderer():Array<Field>
	{
		var fields = Context.getBuildFields();
		for (field in fields)
		{
			switch (field.kind)
			{
				case FFun(fn):
					var original = fn.expr;
					switch (field.name)
					{
						case "uploadFromGLSLProgram3D":
							fn.expr = macro general.shaders.FoxliteCompat.upload(program, vertexSource, fragmentSource);
						case "begin":
							fn.expr = macro {
								$e{original};
								general.shaders.FoxliteCompat.begin(context);
							};
						case "setTarget":
							// The null-target branch returns before any material is drawn.
							fn.expr = macro {
								$e{original};
								general.shaders.FoxliteCompat.targetChanged(context);
							};
						case "backToFlixel":
							fn.expr = macro {
								$e{original};
								general.shaders.FoxliteCompat.backToFlixel(context);
							};
						default:
					}
				default:
			}
		}
		return fields;
	}
	#end
}
