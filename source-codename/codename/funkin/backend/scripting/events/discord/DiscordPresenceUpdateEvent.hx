package codename.funkin.backend.scripting.events.discord;

final class DiscordPresenceUpdateEvent extends CancellableEvent {
	/**
	 * Object containing all of the data for the presence. Can be altered.
	 */
	public var presence:codename.funkin.backend.utils.DiscordUtil.DPresence;
}