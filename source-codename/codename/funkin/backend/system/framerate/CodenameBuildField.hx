package codename.funkin.backend.system.framerate;

import codename.funkin.backend.system.macros.GitCommitMacro;
import openfl.text.TextField;

class CodenameBuildField extends TextField {
	public function new() {
		super();
		defaultTextFormat = Framerate.textFormat;
		autoSize = LEFT;
		multiline = wordWrap = false;
		reload();
	}

	public function reload() {
		var versionMessage:String = Flags.VERSION_MESSAGE;
		#if CODENAME_ENGINE_COMPAT
		versionMessage += "\nNovaFlare Engine v1.2.1";
		#end

		#if COMPILE_EXPERIMENTAL
		text = '$versionMessage (Experimental Build)';
		#else
		text = versionMessage;
		#end

		#if (debug || COMPILE_EXPERIMENTAL)
		text += '\n${Flags.COMMIT_MESSAGE}';
		#end
	}
}
