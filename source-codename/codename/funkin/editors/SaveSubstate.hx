package codename.funkin.editors;

import haxe.io.Path;
#if !mobile
import lime.ui.FileDialog;
#else
import codename.funkin.backend.utils.NativeAPI.MessageBoxIcon;
#end

class SaveSubstate extends MusicBeatSubstate {
	public var saveOptions:Map<String, Bool>;
	public var options:SaveSubstateData;

	public var data:String;

	public var cam:FlxCamera;

	public function new(data:String, ?options:SaveSubstateData, ?saveOptions:Map<String, Bool>) {
		super();
		this.data = data;

		if (saveOptions == null)
			saveOptions = [];
		this.saveOptions = saveOptions;

		if (options != null)
			this.options = options;
	}

	public override function create() {
		super.create();

		#if mobile
		var fileName = options == null ? null : options.defaultSaveFile;
		if (fileName == null || fileName.trim().length == 0) fileName = "export";
		fileName = Path.withoutDirectory(fileName);
		var existingExtension = Path.extension(fileName);
		if ((existingExtension == null || existingExtension.length == 0) && options != null && options.saveExt != null) {
			var extension = options.saveExt.replace("*", "");
			if (extension.length > 0 && !extension.startsWith(".")) extension = "." + extension;
			fileName += extension;
		}

		var resolvedRoot = codenamechain.CodeNameMode.root;
		var baseDirectory:String = resolvedRoot == null || resolvedRoot.length == 0 ? Sys.getCwd() : resolvedRoot;
		var savePath = Path.join([baseDirectory, "exports", fileName]);
		try {
			CoolUtil.addMissingFolders(Path.directory(savePath));
			sys.io.File.saveContent(savePath, data);
			codename.funkin.backend.utils.NativeAPI.showMessageBox("File Saved", 'Saved to:\n$savePath', MSG_INFORMATION);
		} catch (error:Dynamic) {
			codename.funkin.backend.utils.NativeAPI.showMessageBox("Save Error", 'Could not save $fileName:\n${Std.string(error)}', MSG_ERROR);
		}
		close();
		#else
		var fileDialog = new FileDialog();
		fileDialog.onCancel.add(function() close());
		fileDialog.onSelect.add(function(str) {
			CoolUtil.safeSaveFile(str, data);
			close();
		});
		fileDialog.browse(SAVE, options.saveExt.getDefault(Path.extension(options.defaultSaveFile)), options.defaultSaveFile);
		#end
	}

	public override function update(elapsed:Float) {
		super.update(elapsed);
		parent.persistentUpdate = false;
	}
}

typedef SaveSubstateData = {
	var ?defaultSaveFile:String;
	var ?saveExt:String;
}
