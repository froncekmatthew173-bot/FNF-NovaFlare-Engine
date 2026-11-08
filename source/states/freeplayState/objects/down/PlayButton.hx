package states.freeplayState.objects.down;

class PlayButton extends FlxSpriteGroup {
    var icon:FlxSprite;

    var event:Dynamic -> Void = null;

    public function new(x:Float, y:Float, onClick:Dynamic -> Void = null) {
        super(x, y);
        this.event = onClick;

        icon = new FlxSprite().loadGraphic(Paths.image(FreeplayState.filePath + 'playButton'));
        icon.antialiasing = ClientPrefs.data.antialiasing;
        icon.setGraphicSize(250);
        icon.updateHitbox();
        add(icon);
    }
}