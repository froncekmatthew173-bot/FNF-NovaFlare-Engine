class Main {
    static function main():Void {
        final asset:Class<haxe.io.Bytes> = ManifestResources.EmbeddedProject;
        if (asset == null)
            throw "missing embedded project type";
    }
}
