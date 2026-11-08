package options.objects.backend;

class BoolButton extends FlxSpriteGroup {
    var bg:Rect;
    var dis:FlxSprite;

    var follow:Option;

    var innerX:Float; //该摁键在option的x
    var innerY:Float; //该摁键在option的y

    public function new(X:Float, Y:Float, width:Float, height:Float, follow:Option) {
        super(X, Y);

        this.follow = follow;
        innerX = X;
        innerY = Y;

        bg = new Rect(0, 0, width, height, width / 20, width / 20, 0xFF6363, 0.8);
        bg.antialiasing = ClientPrefs.data.antialiasing;
        add(bg);

        dis = new Rect(2, 3, width / 2 - 4, height - 6, width / 20, width / 20, 0xFFFFFF, 0.8);
        dis.antialiasing = ClientPrefs.data.antialiasing;
        add(dis);

        if (follow.defaultValue == true) {
            bg.color = 0x63FF75;
            dis.x += width / 2 - 1;
        }
    }

    public var allowUpdate:Bool = true;
    override function update(elapsed:Float) {
        super.update(elapsed);

        if (!follow.allowUpdate) return;
        
        if (!allowUpdate) return;
        
        var mouse = FlxG.mouse;

        var inputAllow:Bool = true;

        if (Math.abs(OptionsState.instance.cataMove.velocity) > 2) inputAllow = false;

        if (OptionsState.instance.mouseEvent.overlaps(OptionsState.instance.specBG) || OptionsState.instance.mouseEvent.overlaps(OptionsState.instance.downBG)) return;
        
        if (inputAllow) {
            // Check if mouse is over the button
            if (mouse.overlaps(bg)) {
                // Mouse released
                if (OptionsState.instance.mouseEvent.justPressed) {
                    change(!follow.defaultValue);
                }
            }
        }
        updateBgColor();
    }

    function change(data:Bool) {
        // Only proceed if value is actually changing
        if (follow.defaultValue == data) return;
        
        follow.defaultValue = data;
        follow.setValue(data);

        updateDisplay();
        
        follow.change();
    }

    var moveTween:FlxTween;
    public function updateDisplay()
    {
        if (moveTween != null) moveTween.cancel();
        var targetX = follow.defaultValue ? bg.width / 2 + 1 : 2;
        moveTween = FlxTween.tween(dis, { x: follow.followX + follow.innerX + innerX + targetX}, 0.2, { ease: FlxEase.quadOut });
        
        // Tween the background color
        
        //FlxTween.color(bg, 0.2, bg.color, targetColor, { ease: FlxEase.quadOut });
        //bg.color = targetColor;  //为什么几把不能用tween
    }
    
    function updateBgColor(){
        var targetColor = follow.defaultValue ? 0x63FF75 : 0xFF6363;
        bg.color = FlxColor.interpolate(bg.color, targetColor, 0.2);
    }
}