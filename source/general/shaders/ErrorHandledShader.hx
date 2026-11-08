package general.shaders;

import lime.graphics.opengl.GLProgram;
import lime.app.Application;

import flixel.addons.display.FlxRuntimeShader;

class ErrorHandledShader extends FlxShader implements IErrorHandler
{
	public var shaderName:String = '';

	public dynamic function onError(error:Dynamic):Void
	{
	}

	public function new(?shaderName:String)
	{
		this.shaderName = shaderName;
		super();
	}

	override function __createGLProgram(vertexSource:String, fragmentSource:String):GLProgram
	{
		try
		{
			final res = super.__createGLProgram(vertexSource, fragmentSource);
			return res;
		}
		catch (error)
		{
			ErrorHandledShader.crashSave(this.shaderName, error, onError, vertexSource, fragmentSource);
			return null;
		}
	}

	public static inline var SHADER_SOURCE_PREVIEW_LINES:Int = 5;

	public static function crashSave(shaderName:String, error:Dynamic, onError:Dynamic, ?vertexSource:String,
			?fragmentSource:String)
	{
		if (shaderName == null)
			shaderName = 'unnamed';

		trace(error);

		var detail:String = error == null ? '' : Std.string(error);

		var crashLogPath:Null<String> = null;
		#if sys
		try
		{
			var safeName = ~/[^a-zA-Z0-9_.-]/g.replace(shaderName, "_");
			var dateNow = Date.now().toString().split(" ").join("_").split(":").join("-");
			var logDirectory = haxe.io.Path.join([Sys.getCwd(), "logs"]);
			if (!FileSystem.exists(logDirectory)) FileSystem.createDirectory(logDirectory);
			var basePath = haxe.io.Path.join([logDirectory, 'shader_${safeName}_${dateNow}']);
			crashLogPath = '$basePath.txt';
			var suffix = 1;
			while (FileSystem.exists(crashLogPath)) crashLogPath = '${basePath}_${suffix++}.txt';
			File.saveContent(crashLogPath,
				'shader=$shaderName\n\n[error]\n$detail\n\n[vertex]\n$vertexSource\n\n[fragment]\n$fragmentSource');
			trace('Shader error log saved to: $crashLogPath');
		}
		catch (saveError:Dynamic)
		{
			crashLogPath = null;
			trace('Could not save shader error log: $saveError');
		}
		#end

		var message:String = 'Shader Compile Error!\nshader: $shaderName';

		var errorHead:String = headLines(detail, SHADER_SOURCE_PREVIEW_LINES);
		if (errorHead.length > 0)
			message += '\n\n[error]\n' + errorHead;

		var sourcePreview:String = headLines(fragmentSource != null ? fragmentSource : vertexSource, SHADER_SOURCE_PREVIEW_LINES);
		if (sourcePreview.length > 0)
			message += '\n\n[source]\n' + sourcePreview;
		if (crashLogPath != null) message += '\n\nError log saved to: $crashLogPath';

		#if sys
		try
			mobile.backend.SUtil.showPopUp(message, 'Shader Compile Error!')
		catch (_:Dynamic)
			trace(message);
		#else
		try
			Application.current.window.alert(message, 'Shader Compile Error!')
		catch (_:Dynamic)
			trace(message);
		#end

		if (onError != null) onError(error);
	}

	private static function headLines(value:String, limit:Int):String
	{
		if (value == null || value.length == 0)
			return '';

		var normalized:String = StringTools.replace(StringTools.replace(value, '\r\n', '\n'), '\r', '\n');
		var lines:Array<String> = normalized.split('\n');
		if (lines.length > limit)
			lines = lines.slice(0, limit);
		return lines.join('\n');
	}
}

class ErrorHandledRuntimeShader extends FlxRuntimeShader implements IErrorHandler
{
	public var shaderName:String = '';

	public dynamic function onError(error:Dynamic):Void
	{
	}

	public function new(?shaderName:String, ?fragmentSource:String, ?vertexSource:String)
	{
		this.shaderName = shaderName;
		super(fragmentSource, vertexSource);
	}

	override function __createGLProgram(vertexSource:String, fragmentSource:String):GLProgram
	{
		try
		{
			final res = super.__createGLProgram(vertexSource, fragmentSource);
			return res;
		}
		catch (error)
		{
			ErrorHandledShader.crashSave(this.shaderName, error, onError, vertexSource, fragmentSource);
			return null;
		}
	}
}

interface IErrorHandler
{
	public var shaderName:String;
	public dynamic function onError(error:Dynamic):Void;
}
