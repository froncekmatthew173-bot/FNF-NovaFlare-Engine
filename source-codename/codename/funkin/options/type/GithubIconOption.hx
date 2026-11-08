package codename.funkin.options.type;

import flixel.graphics.FlxGraphic;
import flixel.util.FlxColor;
import codename.funkin.backend.shaders.CustomShader;
import codename.funkin.backend.system.github.GitHub;
import haxe.io.Bytes;
import openfl.display.BitmapData;
import codename.funkin.backend.system.github.GitHubContributor.CreditsGitHubContributor;

class GithubIconOption extends TextOption
{
	public var user(default, null):CreditsGitHubContributor;  // Can possibly be GitHubUser or GitHubContributor, but CreditsGitHubContributor has only the fields we need
	public var icon:GithubUserIcon = null;
	public var usePortrait(default, set) = true;

	public function set_usePortrait(value:Bool)
	{
		if (icon == null) return usePortrait = false;
		icon.shader = (value ? new CustomShader('engine/circleProfilePicture') : null);
		return usePortrait = value;
	}

	public function new(user:CreditsGitHubContributor, desc:String, ?callback:Void->Void, ?customName:String, size:Int = 96, usePortrait:Bool = true, waitUntilLoad:Float = 0.25) {
		super(customName == null ? user.login : customName, desc, callback == null ? function() CoolUtil.openURL(user.html_url) : callback);
		this.user = user;
		this.icon = new GithubUserIcon(user, size, waitUntilLoad);
		this.usePortrait = usePortrait;
		add(icon);
		__text.x = 100;
	}
}

class GithubUserIcon extends FlxSprite
{
	public var waitUntilLoad:Null<Float>;
	private var user:CreditsGitHubContributor;
	private var size:Int;
	private var cacheKey:String;
	private var downloadStarted:Bool = false;
	private var downloadFinished:Bool = false;
	private var pendingBytes:Bytes = null;
	private var disposed:Bool = false;
	private var appliedOffset:Float = 0;

	public override function new(user:CreditsGitHubContributor, size:Int = 96, waitUntilLoad:Float = 0.25) {
		this.user = user;
		this.size = size;
		this.waitUntilLoad = waitUntilLoad;
		this.cacheKey = 'GITHUB-USER:${user.login}';
		super();
		makeGraphic(size, size, FlxColor.TRANSPARENT);
		antialiasing = true;
	}

	override function update(elapsed:Float) {
		if(waitUntilLoad != null && waitUntilLoad > 0) waitUntilLoad -= elapsed;
		consumePendingDownload();
		super.update(elapsed);
	}

	#if target.threaded
	final mutex = new sys.thread.Mutex();
	#end

	inline function acquireMutex() {
		#if target.threaded
		mutex.acquire();
		#end
	}
	inline function releaseMutex() {
		#if target.threaded
		mutex.release();
		#end
	}

	override function drawComplex(camera:FlxCamera):Void {
		// Start network work only after the option is actually visible. The worker
		// may download bytes, but every Flixel/OpenFL display mutation stays on the
		// main update thread.
		if(!downloadStarted && waitUntilLoad != null && waitUntilLoad <= 0) {
			waitUntilLoad = null;
			startAvatarDownload();
		}
		super.drawComplex(camera);
	}

	private function startAvatarDownload() {
		downloadStarted = true;
		var cached = FlxG.bitmap.get(cacheKey);
		if (cached != null) {
			updateDaFunni(cached);
			return;
		}

		var login = user.login;
		var avatarURL = user.avatar_url;
		var requestedSize = size;
		Main.execAsync(function() {
			var bytes:Bytes = null;
			try {
				trace('Downloading avatar: $login');
				var directPNG = avatarURL != null && StringTools.endsWith(avatarURL, '.png');
				if (directPNG) {
					try bytes = HttpUtil.requestBytes(withSize(avatarURL, requestedSize))
					catch(e) Logs.error('Failed to download github pfp for $login: ${CoolUtil.removeIP(e.message)} - (Retrying using the api..)');
				}

				if (bytes == null) {
					if (directPNG) {
						var apiUser = GitHub.getUser(login, function(e)
							Logs.error('Failed to download github user info for $login: ${CoolUtil.removeIP(e.message)}'));
						if (apiUser != null && apiUser.avatar_url != null) avatarURL = apiUser.avatar_url;
					}
					if (avatarURL != null)
						try bytes = HttpUtil.requestBytes(withSize(avatarURL, requestedSize))
						catch(e) Logs.error('Failed to download github pfp for $login: ${CoolUtil.removeIP(e.message)}');
				}
			} catch(e) {
				Logs.error('Failed to prepare the github pfp for $login: ${e.message}');
			}
			publishDownloadedBytes(bytes);
		});
	}

	private static inline function withSize(url:String, requestedSize:Int):String
		return '$url${url.indexOf("?") >= 0 ? "&" : "?"}size=$requestedSize';

	private function publishDownloadedBytes(bytes:Bytes) {
		acquireMutex();
		if (!disposed) {
			pendingBytes = bytes;
			downloadFinished = true;
		}
		releaseMutex();
	}

	private function consumePendingDownload() {
		acquireMutex();
		var ready = downloadFinished;
		var bytes = pendingBytes;
		if (ready) {
			downloadFinished = false;
			pendingBytes = null;
		}
		releaseMutex();

		if (!ready || bytes == null || disposed) return;
		try {
			var graphic:FlxGraphic = FlxG.bitmap.get(cacheKey);
			if (graphic == null) {
				var bitmap = BitmapData.fromBytes(bytes);
				if (bitmap == null) return;
				graphic = FlxG.bitmap.add(bitmap, false, cacheKey);
				graphic.persist = true;
			}
			updateDaFunni(graphic);
		} catch(e) {
			Logs.error('Failed to apply the pfp for ${user.login}: ${e.message}');
		}
	}

	public inline function updateDaFunni(graphic:FlxGraphic) {
		x -= appliedOffset;
		loadGraphic(graphic);
		this.setUnstretchedGraphicSize(size, size, false);
		updateHitbox();
		appliedOffset = 90 - width;
		x += appliedOffset;
	}

	override function destroy() {
		acquireMutex();
		disposed = true;
		pendingBytes = null;
		downloadFinished = false;
		releaseMutex();
		super.destroy();
	}
}
