package server.nakama;

class NakamaAccount extends NakamaAuth {
    
    public function new(config:NakamaClient.NakamaConfig) {
        super(config);
    }
    
    // 获取账户信息
    public function getAccount(callback:Dynamic->Void):Void {
        request("GET", "/v2/account", null, true, function(response) {
            if (response.error == null) {
                callback({
                    success: true,
                    account: response
                });
            } else {
                callback({
                    success: false,
                    error: response.error
                });
            }
        });
    }
    
    // 更新账户
    public function updateAccount(username:String, ?displayName:String, ?avatarUrl:String, 
                                  ?langTag:String, ?location:String, ?timezone:String, 
                                  callback:Dynamic->Void):Void {
        var data = {
            username: username,
            display_name: displayName,
            avatar_url: avatarUrl,
            lang_tag: langTag,
            location: location,
            timezone: timezone
        };
        
        request("PUT", "/v2/account", data, true, function(response) {
            callback({
                success: response.error == null,
                error: response.error
            });
        });
    }
}