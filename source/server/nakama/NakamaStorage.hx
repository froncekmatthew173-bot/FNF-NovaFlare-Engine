package server.nakama;

typedef StorageObject = {
    key: String,
    collection: String,
    userId: String,
    value: Dynamic,
    version: String,
    permissionRead: Int,
    permissionWrite: Int
}

class NakamaStorage extends NakamaAccount {
    
    public function new(config:NakamaClient.NakamaConfig) {
        super(config);
    }
    
    // 写入存储
    public function storageWrite(objects:Array<{
        collection: String,
        key: String,
        value: Dynamic,
        permissionRead: Int,
        permissionWrite: Int
    }>, callback:Dynamic->Void):Void {
        
        var data = {
            objects: objects
        };
        
        request("PUT", "/v2/storage", data, true, function(response) {
            if (response.error == null) {
                callback({
                    success: true,
                    acks: response.acks
                });
            } else {
                callback({
                    success: false,
                    error: response.error
                });
            }
        });
    }
    
    // 读取存储
    public function storageRead(keys:Array<{
        collection: String,
        key: String,
        userId: String
    }>, callback:Dynamic->Void):Void {
        
        var data = {
            object_ids: keys
        };
        
        request("POST", "/v2/storage", data, true, function(response) {
            if (response.error == null) {
                callback({
                    success: true,
                    objects: response.objects
                });
            } else {
                callback({
                    success: false,
                    error: response.error
                });
            }
        });
    }
    
    // 列出存储
    public function storageList(collection:String, userId:String, limit:Int = 100, callback:Dynamic->Void):Void {
        var path = '/v2/storage?collection=$collection&userId=$userId&limit=$limit';
        request("GET", path, null, true, function(response) {
            if (response.error == null) {
                callback({
                    success: true,
                    objects: response.objects,
                    cursor: response.cursor
                });
            } else {
                callback({
                    success: false,
                    error: response.error
                });
            }
        });
    }
    
    // 删除存储
    public function storageDelete(keys:Array<{
        collection: String,
        key: String,
        userId: String
    }>, callback:Dynamic->Void):Void {
        
        var data = {
            object_ids: keys
        };
        
        request("PUT", "/v2/storage/delete", data, true, function(response) {
            callback({
                success: response.error == null,
                error: response.error
            });
        });
    }
}