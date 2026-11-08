package codename.funkin.menus.credits;

import flixel.text.FlxText;
import flixel.util.FlxColor;
import codename.funkin.backend.system.github.GitHub;
import codename.funkin.backend.system.github.GitHubContributor.CreditsGitHubContributor;
import codename.funkin.options.PlayerSettings;
import codename.funkin.options.type.GithubIconOption;
import codename.funkin.options.type.TextOption;
#if target.threaded
import sys.thread.Mutex;
#end

using StringTools;

class CreditsCodename extends codename.funkin.options.TreeMenuScreen {
	public var error:Bool = false;
	public var totalContributions:Int = 0;
	public var contribFormats:Array<FlxTextFormatMarkerPair> = [];

	public function new() {
		super("Codename Engine", "credits.allContributors");
		tryUpdating(true);
	}

	private var _canReset:Bool = true;
	private var _statusMessage:String = null;
	private var _pendingDownload:CreditsDownloadResult = null;
	private var _downloadRequestId:Int = 0;
	private var _acceptDownloadResults:Bool = true;
	#if target.threaded
	private final _downloadMutex:Mutex = new Mutex();
	#end

	override function update(elapsed:Float) {
		super.update(elapsed);

		var result = takeDownloadResult();
		if (result != null) {
			_canReset = true;
			error = result.failed;
			_statusMessage = result.statusMessage;
			if (result.contributors != null && result.contributors.length > 0) {
				Options.contributors = result.contributors;
				Options.lastUpdated = result.updatedAt;
			}
			if (result.mainDevs != null) Options.mainDevs = result.mainDevs;
			displayList();
		}
		else if (_canReset && PlayerSettings.solo.controls.RESET) tryUpdating();
	}

	public function tryUpdating(forceDisplaying:Bool = false) {
		var curTime = Date.now().getTime();
		if (Options.lastUpdated != null && curTime < Options.lastUpdated + 120000) {
			_canReset = true;
			if (forceDisplaying) displayList(); else updateMenuDesc();
			return;
		}

		updateMenuDesc(TU.translate("credits.downloadingList"));
		_canReset = false;
		_statusMessage = null;
		var requestId = beginDownloadRequest();
		Main.execAsync(() -> {
			publishDownloadResult(requestId, downloadCreditsSafely());
		});
	}

	public override function updateMenuDesc(?txt:String) {
		if (!_canReset) return;
		super.updateMenuDesc(txt);
		updateMarkup();
	}

	public function updateMarkup() {
		clearContributorFormats();
		if (parent == null || totalContributions <= 0 || curSelected < 0 || curSelected >= Options.contributors.length) return;

		var contributor = Options.contributors[curSelected];
		if (contributor == null || contributor.contributions == null) return;

		var text:String = parent.descLabel.text;
		parent.descLabel.text = "";
		var contributionRatio = FlxMath.bound(contributor.contributions / totalContributions, 0, 1);
		parent.descLabel.applyMarkup(text, contribFormats = [
			new FlxTextFormatMarkerPair(new FlxTextFormat(Flags.MAIN_DEVS_COLOR), '*'),
			new FlxTextFormatMarkerPair(new FlxTextFormat(FlxColor.interpolate(Flags.MIN_CONTRIBUTIONS_COLOR, Flags.MAIN_DEVS_COLOR, contributionRatio)), '~')
		]);
	}

	private function clearContributorFormats() {
		if (parent != null)
			for (frmt in contribFormats)
				if (frmt != null) parent.descLabel.removeFormat(frmt.format);
		contribFormats = [];
	}

	override function close() {
		cancelDownloads();
		clearContributorFormats();
		super.close();
	}

	private function downloadCredits():CreditsDownloadResult {
		var result:CreditsDownloadResult = {
			contributors: null,
			mainDevs: null,
			statusMessage: null,
			updatedAt: null,
			failed: false
		};

		var contributorsFailed = false;
		var rawContributors = GitHub.getContributors(Flags.REPO_OWNER, Flags.REPO_NAME, function(e) {
			contributorsFailed = true;
			var errMsg:String = 'Error while trying to download contributors list:\n${CoolUtil.removeIP(e.message)}';
			Logs.error(errMsg.replace('\n', ' '));
			result.statusMessage = "Unable to load the contributor list. Press RESET to retry.";
		});
		if(contributorsFailed || rawContributors == null || rawContributors.length == 0) {
			result.failed = true;
			if (result.statusMessage == null) {
				result.statusMessage = "No contributor data is available. Press RESET to retry.";
				Logs.warn('[CreditsCodename] GitHub returned an empty contributors list.');
			}
			return result;
		}

		result.contributors = [for(e in rawContributors) {
			login: e.login,
			avatar_url: e.avatar_url,
			html_url: e.html_url,
			id: e.id,
			contributions: e.contributions
		}];
		result.updatedAt = Date.now().getTime();
		Logs.verbose('[CreditsCodename] Contributors list Updated!');

		var membersFailed = false;
		var organizationMembers = GitHub.getOrganizationMembers(Flags.REPO_OWNER, function(e) {
			membersFailed = true;
			var errMsg:String = 'Error while trying to download ${Flags.REPO_OWNER} members list:\n${CoolUtil.removeIP(e.message)}';
			Logs.error(errMsg.replace('\n', ' '));
		});
		if(!membersFailed && organizationMembers != null) {
			result.mainDevs = [for(m in organizationMembers) m.id];
			Logs.verbose('[CreditsCodename] Main Devs list Updated!');
		}

		return result;
	}

	private function downloadCreditsSafely():CreditsDownloadResult {
		try {
			return downloadCredits();
		} catch(e) {
			Logs.error('[CreditsCodename] Unexpected contributor download error: ${e.message}');
			return {
				contributors: null,
				mainDevs: null,
				statusMessage: "Unable to load the contributor list. Press RESET to retry.",
				updatedAt: null,
				failed: true
			};
		}
	}

	/** Compatibility entry point; downloaded data is applied on the main update. */
	public function checkUpdate():Bool {
		var requestId = beginDownloadRequest();
		var result = downloadCreditsSafely();
		publishDownloadResult(requestId, result);
		return result.contributors != null && result.contributors.length > 0;
	}

	private function beginDownloadRequest():Int {
		acquireDownloadMutex();
		_acceptDownloadResults = true;
		_pendingDownload = null;
		var requestId = ++_downloadRequestId;
		releaseDownloadMutex();
		return requestId;
	}

	private function publishDownloadResult(requestId:Int, result:CreditsDownloadResult) {
		acquireDownloadMutex();
		if (_acceptDownloadResults && requestId == _downloadRequestId)
			_pendingDownload = result;
		releaseDownloadMutex();
	}

	private function takeDownloadResult():CreditsDownloadResult {
		acquireDownloadMutex();
		var result = _pendingDownload;
		_pendingDownload = null;
		releaseDownloadMutex();
		return result;
	}

	private function cancelDownloads() {
		acquireDownloadMutex();
		_acceptDownloadResults = false;
		_downloadRequestId++;
		_pendingDownload = null;
		releaseDownloadMutex();
	}

	private inline function acquireDownloadMutex() {
		#if target.threaded
		_downloadMutex.acquire();
		#end
	}

	private inline function releaseDownloadMutex() {
		#if target.threaded
		_downloadMutex.release();
		#end
	}

	public function displayList() {
		while (members.length > 0) {
			members[0].destroy();
			remove(members[0], true);
		}

		totalContributions = 0;
		for(c in Options.contributors)
			if (c != null && c.contributions != null && c.contributions > 0)
				totalContributions += c.contributions;

		if (Options.contributors.length == 0 || totalContributions <= 0) {
			var unavailable = new TextOption(
				"Contributors unavailable",
				_statusMessage != null ? _statusMessage : "No contributor data is available. Press RESET to retry."
			);
			unavailable.locked = true;
			add(unavailable);
			curSelected = 0;
			changeSelection(0, true);
			return;
		}

		for(c in Options.contributors) {
			var contributionCount = c.contributions == null ? 0 : c.contributions;
			var text = TU.translate("credits.totalContributions", [contributionCount, totalContributions, FlxMath.roundDecimal(contributionCount / totalContributions * 100, 2)]);
			var opt:GithubIconOption = new GithubIconOption(c, text);
			if(Options.mainDevs.contains(c.id)) {
				opt.desc += TU.translate("credits.mainDev");
				@:privateAccess opt.__text.color = Flags.MAIN_DEVS_COLOR;
			}
			add(opt);
		}

		if (curSelected < 0) curSelected = 0;
		else if (curSelected >= members.length) curSelected = members.length - 1;
		changeSelection(0, true);
		updateMenuDesc();
	}

	override function destroy() {
		cancelDownloads();
		clearContributorFormats();
		super.destroy();
	}
}

private typedef CreditsDownloadResult = {
	var contributors:Null<Array<CreditsGitHubContributor>>;
	var mainDevs:Null<Array<Int>>;
	var statusMessage:Null<String>;
	var updatedAt:Null<Float>;
	var failed:Bool;
}
