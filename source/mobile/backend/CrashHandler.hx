package mobile.backend;

import openfl.events.UncaughtErrorEvent;
import openfl.events.ErrorEvent;
import openfl.errors.Error;

#if sys
import haxe.io.Path;
import sys.FileSystem;
import sys.io.File;
#end

#if cpp
import cpp.vm.Gc;
#end

using general.backend.CoolUtil;

/**
 * Crash Handler.
 * @author YoshiCrafter29, Ne_Eo and MAJigsaw77
 */
class CrashHandler
{
	private static inline var ERROR_DEBOUNCE_MS:Float = 3000;
	private static var lastErrorTime:Float = 0;

	public static var installed(default, null):Bool = false;

	public static function init():Void
	{
		if (installed)
			return;
		installed = true;

		#if (cpp && (windows || android))
		general.backend.NativeCrashHandler.init(
			states.mainMenuState.MainMenuState.novaFlareEngineCommit);
		#end
		openfl.Lib.current.loaderInfo.uncaughtErrorEvents.addEventListener(UncaughtErrorEvent.UNCAUGHT_ERROR, onUncaughtError);
		#if cpp
		untyped __global__.__hxcpp_set_critical_error_handler(onError);
		#elseif hl
		hl.Api.setErrorHandler(onError);
		#end
	}

	public static function refreshNativeCrashDirectory():Void
	{
		#if (cpp && (windows || android))
		general.backend.NativeCrashHandler.refreshDirectory();
		#end
	}

	private static function onUncaughtError(e:UncaughtErrorEvent):Void
	{
		e.preventDefault();
		e.stopPropagation();
		e.stopImmediatePropagation();

		var now:Float = Date.now().getTime();
		if (now - lastErrorTime < ERROR_DEBOUNCE_MS)
			return;
		lastErrorTime = now;

		var m:String = Std.string(e.error);
		if (Std.isOfType(e.error, Error))
		{
			var err = cast(e.error, Error);
			m = '${err.message}';
		}
		else if (Std.isOfType(e.error, ErrorEvent))
		{
			var err = cast(e.error, ErrorEvent);
			m = '${err.text}';
		}

		var stack:Array<haxe.CallStack.StackItem> = [];
		var callStack:Array<haxe.CallStack.StackItem> = [];
		try stack = haxe.CallStack.exceptionStack(true) catch (_:Dynamic) {}
		try callStack = haxe.CallStack.callStack() catch (_:Dynamic) {}

		var stackLabelArr:Array<String> = [];
		for (item in stack)
		{
			switch (item)
			{
				case CFunction:
					stackLabelArr.push("Non-Haxe (C) Function");
				case Module(c):
					stackLabelArr.push('Module ${c}');
				case FilePos(parent, file, line, col):
					switch (parent)
					{
						case Method(cla, func):
							stackLabelArr.push('${file.replace('.hx', '')}.$func() [line $line]');
						case _:
							stackLabelArr.push('${file.replace('.hx', '')} [line $line]');
					}
				case LocalFunction(v):
					stackLabelArr.push('Local Function ${v}');
				case Method(cl, m):
					stackLabelArr.push('${cl} - ${m}');
			}
		}
		var stackLabel:String = stackLabelArr.join('\r\n');

		#if sys
		var haxeSnapshot:String = "";
		try
			haxeSnapshot = captureHaxeStackSnapshot(stack)
		catch (snapshotError:Dynamic)
			haxeSnapshot = '[snapshot_capture_failed] ${Std.string(snapshotError)}';

		general.backend.NativeCrashHandler.setHaxeRuntimeSnapshot(haxeSnapshot);
		var saveFailure:String = null;
		try
		{
			var diagnosticRoot = Sys.getEnv("NOVAFLARE_DIAGNOSTIC_DIR");
			var crashDirectory = diagnosticRoot != null && diagnosticRoot.length > 0
				? Path.join([diagnosticRoot, "haxe-crash"])
				: "crash";
			if (!FileSystem.exists(crashDirectory))
				FileSystem.createDirectory(crashDirectory);

			var nativeExceptionStack = "";
			#if cpp
			try
				nativeExceptionStack = Std.string(haxe.NativeStackTrace.exceptionStack())
			catch (_:Dynamic) {}
			#end

			var heapSnapshot = "unavailable";
			#if cpp
			try
			{
				#if hxcpp_zgc
				heapSnapshot =
					'used_bytes=${Gc.memInfo64(2)}\n' +
					'committed_bytes=${Gc.memInfo64(4)}\n' +
					'application_bytes=${Gc.memInfo64(8)}';
				#else
				heapSnapshot =
					'used_bytes=${Gc.memInfo64(2)}\n' +
					'committed_bytes=${Gc.memInfo64(1)}\n' +
					'application_bytes=${Gc.memInfo64(4)}';
				#end
			}
			catch (_:Dynamic) {}
			#end

			var saveError =
				'commit=${states.mainMenuState.MainMenuState.novaFlareEngineCommit}\n' +
				'timestamp=${Date.now()}\n' +
				'message=$m\n' +
				'\n[haxe_exception_stack]\n$stackLabel\n' +
				'\n[haxe_runtime_snapshot]\n$haxeSnapshot\n' +
				'\n[haxe_exception_stack_raw]\n${haxe.CallStack.toString(stack)}\n' +
				'\n[haxe_call_stack]\n${haxe.CallStack.toString(callStack)}\n' +
				'\n[native_hxcpp_exception_stack]\n$nativeExceptionStack\n' +
				'\n[heap]\n$heapSnapshot\n';
			#if CODENAME_ENGINE_COMPAT
			try
			{
				var codenameContext = codenamechain.CodeNameCrashContext.capture();
				if (codenameContext.length > 0) saveError += '\n$codenameContext\n';
			}
			catch (contextError:Dynamic)
				saveError += '\n[codename_context_failed]\n${Std.string(contextError)}\n';
			#end
			var fileName = Date.now().toString()
				.replace(' ', '-')
				.replace(':', "'") + '.txt';
			var crashPath = FileSystem.absolutePath(Path.join([crashDirectory, fileName]));
			File.saveContent(crashPath, saveError);
			Sys.println('haxe:uncaught_error message=$m');
			Sys.println('haxe:uncaught_error report=$crashPath');
			Sys.println(saveError);
		}
		catch (saveErrorValue:Dynamic)
		{
			saveFailure = Std.string(saveErrorValue);
			trace('Couldn\'t save error message. ($saveFailure)');
			trace(Std.string(states.mainMenuState.MainMenuState.novaFlareEngineCommit + '\n' + '$m\n$stackLabel'));
		}

		showError(buildErrorText(m, stackLabelArr));
		general.backend.NativeCrashHandler.setHaxeRuntimeSnapshot("");
		#end
	}

	private static function buildErrorText(message:String, stackLines:Array<String>):String
	{
		var lines:Array<String> = [];
		if (message != null && StringTools.trim(message).length > 0)
			lines.push(StringTools.trim(message));

		if (stackLines != null)
		{
			for (line in stackLines)
			{
				if (line != null && line.length > 0)
					lines.push(line);
			}
		}

		return lines.length > 0 ? lines.join("\n") : "Unknown error";
	}

	private static function showError(errorText:String):Void
	{
		var shown:Bool = false;
		try
		{
			if (openfl.Lib.current != null && flixel.FlxG.state != null)
			{
				var failedState = flixel.FlxG.state;
				var screen = new substates.ErrorSubState(errorText);
				// Emergency UI must not call the mod's onSubstateOpen hooks: they
				// can fail too, leaving the broken state running and logging forever.
				failedState.persistentUpdate = false;
				failedState.persistentDraw = false;
				@:privateAccess {
					failedState._requestedSubState = screen;
					failedState._requestSubStateReset = true;
				}
				shown = true;
			}
		}
		catch (_:Dynamic) {}

		if (shown)
			return;

		#if sys
		try
			mobile.backend.SUtil.showPopUp(errorText, "NovaFlare Engine - Error")
		catch (_:Dynamic)
			Sys.println(errorText);
		#else
		trace(errorText);
		#end
	}

	#if sys
	private static final HAXE_RUNTIME_SNAPSHOT_LIMIT:Int = 24;

	private static function captureHaxeStackSnapshot(stack:Array<haxe.CallStack.StackItem>):String
	{
		if (stack == null || stack.length == 0)
			return '';

		var lines:Array<String> = [];
		var index = 0;
		for (item in stack)
		{
			if (index >= HAXE_RUNTIME_SNAPSHOT_LIMIT)
				break;

			var file:String = '';
			var line:Int = 0;
			var column:Int = 0;
			var method:String = '<unknown>';

			switch (item)
			{
				case CFunction:
					method = 'CFunction';
				case FilePos(parent, stackFile, stackLine, stackColumn):
					file = stackFile;
					line = stackLine;
					column = stackColumn;
					method = switch (parent)
					{
						case Method(owner, name): '$owner.$name';
						case Module(owner): 'module $owner';
						case LocalFunction(name): 'local function $name';
						case _: '<unknown>';
					}
				case Method(owner, methodName):
					method = '$owner.$methodName';
				case Module(owner):
					method = 'module $owner';
				case LocalFunction(name):
					method = 'local function $name';
			}

			var location = file.length > 0 ? '$file:$line' : '<no-location>';
			if (column > 0) location += ':$column';
			var sourceLine = readSourceLine(file, line);
			if (sourceLine.length > 0) location += ' -> ${sourceLine}';
			lines.push('#${indexToSnapshotTag(index)} $location in $method');

			index++;
		}

		return lines.length == 0 ? '[no_haxe_runtime_snapshot]' : lines.join('\r\n');
	}

	private static function indexToSnapshotTag(value:Int):String
	{
		return value < 10 ? '0$value' : Std.string(value);
	}

	private static function readSourceLine(path:String, line:Int):String
	{
		if (path == null || path.length == 0 || line <= 0)
			return '';

		var sourcePath = resolveSourcePath(path);
		if (sourcePath == null || sourcePath.length == 0 || !FileSystem.exists(sourcePath))
			return '';

		try
		{
			var lines = File.getContent(sourcePath).split('\n');
			var index = line - 1;
			if (index >= 0 && index < lines.length)
				return lines[index].replace('\r', '').trim();
		}
		catch (_:Dynamic) {}

		return '';
	}

	private static function resolveSourcePath(path:String):String
	{
		if (FileSystem.exists(path))
			return path;

		var cwd = Sys.getCwd();
		var altPath = Path.join([cwd, path]);
		if (FileSystem.exists(altPath))
			return altPath;

		var sourcePath = Path.join([cwd, "source", path]);
		if (FileSystem.exists(sourcePath))
			return sourcePath;

		return path;
	}
	#end

	#if (cpp || hl)
	private static function onError(message:Dynamic):Void
	{
		throw Std.string(message);
	}
	#end
}
